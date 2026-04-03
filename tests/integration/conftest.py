# Copyright (c) Nordic Semiconductor ASA
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause

from __future__ import annotations

import os
from pathlib import Path
from typing import Generator

import pytest

from kconfig_utils import (
    get_example_device_id,
    get_mqtt_broker_host,
    load_build_config,
)


@pytest.fixture(scope="session")
def zephyr_build_dir() -> Path | None:
    """West build directory containing zephyr/.config (set ZEPHYR_BUILD_DIR for CI)."""
    env = os.environ.get("ZEPHYR_BUILD_DIR")
    if env:
        return Path(env)
    # Default: repo native_sim build path (local dev)
    here = Path(__file__).resolve().parent
    repo = here.parent.parent
    default = repo / "build-native-sim" / "app"
    if (default / "zephyr" / ".config").is_file():
        return default
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

