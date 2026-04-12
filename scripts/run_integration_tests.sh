#!/usr/bin/env bash
# Copyright (c) Nordic Semiconductor ASA
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#
# Build native_sim (32-bit x86 host) firmware (optional) and run Python MQTT integration tests.
# Matches embedded pointer width better than native_sim/native/64.
# On Ubuntu/Debian, install: sudo apt install gcc-multilib libc6-dev-i386 libcap2-bin iptables
#
# native_sim + ETH_NATIVE_POSIX: the host TAP (zeth) must exist before the simulator runs.
# net-setup.sh creates zeth; this script also enables IPv4 forwarding + iptables MASQUERADE
# from zeth to the default route (NAT) so the DUT can use broker.hivemq.com and public DNS.
# Teardown runs NAT removal then net-setup stop. Optional setcap on the zephyr binary.
#
# Usage (from nrf9151-connectkit repo root):
#   ./scripts/run_integration_tests.sh
# Env:
#   INTEGRATION_ZEPHYR_BUILD_DIR  If set, pytest uses this build for .config and native_sim (overrides auto-detect)
#   ZEPHYR_BUILD_DIR  Used when build-native-sim/app is missing; also exported for tools (default: build-native-sim/app)
#   INTEGRATION_NATIVE_SIM_LOG  Optional path to write captured native_sim stdout
#   SKIP_NATIVE_BUILD  If set to 1, skip `west build` (use existing build)
#   INTEGRATION_NET_TOOLS_DIR  Directory containing net-setup.sh (overrides search). See https://github.com/zephyrproject-rtos/net-tools
#   INTEGRATION_FETCH_NET_TOOLS  If 1 (or GITHUB_ACTIONS is set), clone net-tools into .integration-net-tools when missing
#   SKIP_NET_SETUP  If 1, do not run net-setup.sh (only setcap / manual TAP setup)
#   SKIP_NAT  If 1, do not enable IPv4 MASQUERADE / forwarding for zeth (public MQTT/DNS will not work from the DUT)
#   GCOV_EXE  Optional gcov binary for gcovr when it does not match the native_sim compiler (rare; Ubuntu host gcc is usually fine).

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${ROOT}"

BUILD_DIR="${ZEPHYR_BUILD_DIR:-${ROOT}/build-native-sim/app}"

if [[ "${SKIP_NATIVE_BUILD:-0}" != "1" ]]; then
	west build -b native_sim app -d "${ROOT}/build-native-sim" -- \
		-DEXTRA_CONF_FILE=overlay-native-sim.conf
fi

# west -d build-native-sim places zephyr/.config under build-native-sim/, not build-native-sim/app/
if [[ -z "${ZEPHYR_BUILD_DIR:-}" ]] && [[ -f "${ROOT}/build-native-sim/zephyr/.config" ]]; then
	BUILD_DIR="${ROOT}/build-native-sim"
fi

export ZEPHYR_BUILD_DIR="${BUILD_DIR}"

# --- Zephyr net-tools: net-setup.sh (creates zeth on the host; matches overlay 192.0.2.x) ---
NET_TOOLS_DIR=""
NET_SETUP_ACTIVE=0

integration_clone_net_tools() {
	local cache="${ROOT}/.integration-net-tools"
	if [[ ! -d "${cache}/.git" ]]; then
		rm -rf "${cache}"
		git clone --depth 1 https://github.com/zephyrproject-rtos/net-tools "${cache}"
	fi
	[[ -f "${cache}/net-setup.sh" ]] && echo "${cache}"
}

integration_find_net_tools_dir() {
	local d p wt

	if [[ -n "${INTEGRATION_NET_TOOLS_DIR:-}" ]]; then
		d="${INTEGRATION_NET_TOOLS_DIR}"
		[[ -f "${d}/net-setup.sh" ]] && { echo "${d}"; return 0; }
		echo "error: INTEGRATION_NET_TOOLS_DIR=${d} does not contain net-setup.sh" >&2
		return 1
	fi

	if [[ -n "${ZEPHYR_BASE:-}" ]]; then
		d="$(cd "${ZEPHYR_BASE}/../tools/net-tools" 2>/dev/null && pwd)" || true
		if [[ -f "${d}/net-setup.sh" ]]; then
			echo "${d}"
			return 0
		fi
	fi

	if command -v west >/dev/null 2>&1; then
		wt="$(cd "${ROOT}" && west topdir 2>/dev/null)" || true
		if [[ -n "${wt}" ]]; then
			d="${wt}/tools/net-tools"
			if [[ -f "${d}/net-setup.sh" ]]; then
				echo "${d}"
				return 0
			fi
		fi
	fi

	p="${ROOT}"
	while [[ "${p}" != "/" ]]; do
		d="${p}/tools/net-tools"
		if [[ -f "${d}/net-setup.sh" ]]; then
			echo "${d}"
			return 0
		fi
		p="$(dirname "${p}")"
	done

	if [[ "${INTEGRATION_FETCH_NET_TOOLS:-}" == "1" ]] || [[ "${GITHUB_ACTIONS:-}" == "true" ]]; then
		d="$(integration_clone_net_tools)" || true
		if [[ -n "${d}" && -f "${d}/net-setup.sh" ]]; then
			echo "${d}"
			return 0
		fi
	fi

	echo "error: Zephyr net-tools not found (need net-setup.sh). Clone:" >&2
	echo "  git clone https://github.com/zephyrproject-rtos/net-tools" >&2
	echo "Then: export INTEGRATION_NET_TOOLS_DIR=/path/to/net-tools" >&2
	echo "Or: export INTEGRATION_FETCH_NET_TOOLS=1 to auto-clone under ${ROOT}/.integration-net-tools" >&2
	return 1
}

integration_net_setup_stop() {
	if [[ "${NET_SETUP_ACTIVE}" -ne 1 ]] || [[ -z "${NET_TOOLS_DIR}" ]]; then
		return 0
	fi
	sudo "${NET_TOOLS_DIR}/net-setup.sh" stop 2>/dev/null || true
	NET_SETUP_ACTIVE=0
}

integration_net_setup_start() {
	NET_TOOLS_DIR="$(integration_find_net_tools_dir)"
	sudo ip link del zeth 2>/dev/null || true
	sudo "${NET_TOOLS_DIR}/net-setup.sh" start
	NET_SETUP_ACTIVE=1
}

# IPv4 NAT: forward + MASQUERADE zeth -> default WAN so the DUT can reach broker.hivemq.com and DNS.
NAT_ACTIVE=0
NAT_WAN=""
IPV4_FORWARD_PREV=""

integration_nat_start() {
	NAT_WAN=$(ip -4 route show default 2>/dev/null | awk '/default/ { for (i = 1; i <= NF; i++) if ($i == "dev") { print $(i + 1); exit } }')
	if [[ -z "${NAT_WAN}" ]]; then
		NAT_WAN=$(ip route show default 2>/dev/null | awk '{ print $5; exit }')
	fi
	if [[ -z "${NAT_WAN}" ]]; then
		echo "error: could not detect default route interface (needed for NAT)" >&2
		exit 1
	fi

	if ! command -v iptables >/dev/null 2>&1; then
		echo "error: iptables not found. Install: sudo apt install iptables" >&2
		exit 1
	fi

	IPV4_FORWARD_PREV=$(cat /proc/sys/net/ipv4/ip_forward)
	echo 1 | sudo tee /proc/sys/net/ipv4/ip_forward >/dev/null
	# Loose forwarding from TAP (avoids drops when strict rp_filter is on).
	sudo sysctl -q -w net.ipv4.conf.zeth.rp_filter=0 2>/dev/null || true

	sudo iptables -t nat -A POSTROUTING -o "${NAT_WAN}" -j MASQUERADE
	sudo iptables -A FORWARD -i zeth -o "${NAT_WAN}" -j ACCEPT
	sudo iptables -A FORWARD -i "${NAT_WAN}" -o zeth -m state --state RELATED,ESTABLISHED -j ACCEPT

	NAT_ACTIVE=1
}

integration_nat_stop() {
	if [[ "${NAT_ACTIVE:-0}" -ne 1 ]]; then
		return 0
	fi
	if [[ -n "${NAT_WAN}" ]]; then
		sudo iptables -t nat -D POSTROUTING -o "${NAT_WAN}" -j MASQUERADE 2>/dev/null || true
		sudo iptables -D FORWARD -i zeth -o "${NAT_WAN}" -j ACCEPT 2>/dev/null || true
		sudo iptables -D FORWARD -i "${NAT_WAN}" -o zeth -m state --state RELATED,ESTABLISHED -j ACCEPT 2>/dev/null || true
	fi
	if [[ -n "${IPV4_FORWARD_PREV}" ]]; then
		echo "${IPV4_FORWARD_PREV}" | sudo tee /proc/sys/net/ipv4/ip_forward >/dev/null
	fi
	NAT_ACTIVE=0
}

integration_cleanup() {
	integration_nat_stop
	integration_net_setup_stop
}

trap integration_cleanup EXIT INT TERM

if [[ "${SKIP_NET_SETUP:-0}" != "1" ]]; then
	integration_net_setup_start
else
	NET_TOOLS_DIR=""
fi

if [[ "${SKIP_NET_SETUP:-0}" != "1" ]] && [[ "${SKIP_NAT:-0}" != "1" ]]; then
	integration_nat_start
fi

# TAP/zeth: optional capabilities on the zephyr binary (supplement to net-setup).
native_sim_zephyr_exe() {
	local d
	for d in "${ROOT}/build-native-sim" "${ROOT}/build-native-sim/app"; do
		if [[ -x "${d}/zephyr/zephyr" ]]; then
			echo "${d}/zephyr/zephyr"
			return 0
		fi
	done
	return 1
}

apply_native_sim_net_caps() {
	local exe
	exe="$(native_sim_zephyr_exe)" || return 0
	command -v setcap >/dev/null 2>&1 || return 0
	if setcap cap_net_admin,cap_net_raw+ep "${exe}" 2>/dev/null; then
		return 0
	fi
	if command -v sudo >/dev/null 2>&1; then
		sudo setcap cap_net_admin,cap_net_raw+ep "${exe}" 2>/dev/null || true
	fi
}

apply_native_sim_net_caps

pip install -q -r tests/integration/requirements.txt

_pytest_status=0
pytest tests/integration -m mqtt "$@" || _pytest_status=$?

# Collect gcov after tests (even if pytest failed).
python3 "${ROOT}/scripts/integration_coverage_report.py" \
	--repo-root "${ROOT}" \
	--build-dir "${ZEPHYR_BUILD_DIR}" || true

exit "${_pytest_status}"
