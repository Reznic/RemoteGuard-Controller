#!/usr/bin/env bash
# Copyright (c) Nordic Semiconductor ASA
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#
# Build native_sim (32-bit x86 host) firmware (optional) and run Python MQTT integration tests.
# Matches embedded pointer width better than native_sim/native/64.
# On Ubuntu/Debian, install: sudo apt install gcc-multilib libc6-dev-i386 libcap2-bin
# ETH_NATIVE_POSIX needs CAP_NET_ADMIN (and CAP_NET_RAW) on the zephyr binary so
# Linux can create the TAP interface (zeth). This script applies them via setcap
# when possible (sudo may prompt once on a dev machine).
# Usage (from nrf9151-connectkit repo root):
#   ./scripts/run_integration_tests.sh
# Env:
#   INTEGRATION_ZEPHYR_BUILD_DIR  If set, pytest uses this build for .config and native_sim (overrides auto-detect)
#   ZEPHYR_BUILD_DIR  Used when build-native-sim/app is missing; also exported for tools (default: build-native-sim/app)
#   INTEGRATION_NATIVE_SIM_LOG  Optional path to write captured native_sim stdout (default: tests/integration/artifacts/native_sim_stdout.log)
#   SKIP_NATIVE_BUILD  If set to 1, skip `west build` (use existing build)

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

trap integration_net_setup_stop EXIT INT TERM

if [[ "${SKIP_NET_SETUP:-0}" != "1" ]]; then
	integration_net_setup_start
else
	NET_TOOLS_DIR=""
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
pytest tests/integration -m mqtt "$@"
