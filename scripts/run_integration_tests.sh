#!/usr/bin/env bash
# Copyright (c) Nordic Semiconductor ASA
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#
# Build native_sim (32-bit x86 host) firmware (optional) and run Python MQTT integration tests.
# Matches embedded pointer width better than native_sim/native/64.
# On Ubuntu/Debian, install: sudo apt install gcc-multilib libc6-dev-i386
# Usage (from nrf9151-connectkit repo root):
#   ./scripts/run_integration_tests.sh
# Env:
#   ZEPHYR_BUILD_DIR  Path to west build dir containing zephyr/.config (default: build-native-sim/app)
#   SKIP_NATIVE_BUILD  If set to 1, skip `west build` (use existing build)

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${ROOT}"

BUILD_DIR="${ZEPHYR_BUILD_DIR:-${ROOT}/build-native-sim/app}"

if [[ "${SKIP_NATIVE_BUILD:-0}" != "1" ]]; then
	west build -b native_sim app -d "${ROOT}/build-native-sim" -- \
		-DEXTRA_CONF_FILE=overlay-native-sim.conf
fi

export ZEPHYR_BUILD_DIR="${BUILD_DIR}"

pip install -q -r tests/integration/requirements.txt
pytest tests/integration -m mqtt "$@"
