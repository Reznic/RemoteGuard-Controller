# Copyright (c) Nordic Semiconductor ASA
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause

from __future__ import annotations

import os
from pathlib import Path
from typing import Generator

import pytest

from native_sim_dut import (
    NATIVE_SIM_APP_BOOT_LOG_LINE,
    NativeSimDut,
    native_sim_log_file_path,
)

from kconfig_utils import (
    find_native_sim_executable,
    full_topic,
    get_example_device_id,
    get_mqtt_broker_host,
    get_mqtt_gps_cmd_suffix,
    get_mqtt_gps_data_suffix,
    get_mqtt_transport_subscribe_suffix,
    load_build_config,
)


@pytest.fixture(scope="session")
def zephyr_build_dir() -> Path | None:
    """West build directory containing zephyr/.config.

    Prefer the integration overlay build (build-native-sim/app) when present so a
    shell-wide ZEPHYR_BUILD_DIR pointing at another variant (e.g. native/64) does
    not drive these tests. Override with INTEGRATION_ZEPHYR_BUILD_DIR if needed.
    """
    here = Path(__file__).resolve().parent
    repo = here.parent.parent
    integration = repo / "build-native-sim" / "app"
    env_integration = os.environ.get("INTEGRATION_ZEPHYR_BUILD_DIR")
    if env_integration:
        p = Path(env_integration)
        return p if (p / "zephyr" / ".config").is_file() else None
    if (integration / "zephyr" / ".config").is_file():
        return integration
    env = os.environ.get("ZEPHYR_BUILD_DIR")
    if env:
        p = Path(env)
        return p if (p / "zephyr" / ".config").is_file() else None
    return None


@pytest.fixture(scope="session")
def kconfig(zephyr_build_dir: Path | None) -> dict[str, str]:
    return load_build_config(zephyr_build_dir)


@pytest.fixture(scope="session")
def mqtt_broker_host(kconfig: dict[str, str]) -> str:
    return get_mqtt_broker_host(kconfig)


@pytest.fixture(scope="session")
def integration_device_id(kconfig: dict[str, str]) -> str:
    return get_example_device_id(kconfig)


@pytest.fixture
def broker_client(
    mqtt_broker_host: str,
) -> Generator["BrokerClient", None, None]:
    from mqtt.broker_client import BrokerClient

    client = BrokerClient(hostname=mqtt_broker_host, port=1883)
    client.connect()
    try:
        yield client
    finally:
        client.disconnect()


@pytest.fixture
def topic_prefix(integration_device_id: str) -> str:
    return integration_device_id


@pytest.fixture(scope="session")
def mqtt_transport_subscribe_topic(kconfig: dict[str, str], integration_device_id: str) -> str:
    return full_topic(integration_device_id, get_mqtt_transport_subscribe_suffix(kconfig))


@pytest.fixture(scope="session")
def mqtt_gps_cmd_topic(kconfig: dict[str, str], integration_device_id: str) -> str:
    return full_topic(integration_device_id, get_mqtt_gps_cmd_suffix(kconfig))


@pytest.fixture(scope="session")
def mqtt_gps_data_topic(kconfig: dict[str, str], integration_device_id: str) -> str:
    return full_topic(integration_device_id, get_mqtt_gps_data_suffix(kconfig))


@pytest.fixture(scope="session")
def native_sim_dut(zephyr_build_dir: Path | None) -> Generator[NativeSimDut, None, None]:
    """Runs native_sim until tests finish; first waits for MQTT connected log line."""
    if zephyr_build_dir is None:
        pytest.skip("No Zephyr build dir (set ZEPHYR_BUILD_DIR or build build-native-sim/app)")
    exe = find_native_sim_executable(zephyr_build_dir)
    if exe is None:
        pytest.skip(f"No native_sim executable under {zephyr_build_dir / 'zephyr'!s}")

    dut = NativeSimDut(exe)
    dut.start()
    
    try:
        # Confirms Zephyr + app threads started (see network.c).
        dut.wait_for_substring(NATIVE_SIM_APP_BOOT_LOG_LINE, timeout=30.0)
    except TimeoutError as e:
        dut.stop()
        pytest.fail(f"Zephyr app did not boot on simulator: {e}")

    try:
        dut.wait_for_substring("Connected to MQTT broker", timeout=120.0)
        yield dut

    except TimeoutError as e:
        pytest.fail(f"App failed to connect to mqtt broker: {e}")
    finally:
        dut.stop()

