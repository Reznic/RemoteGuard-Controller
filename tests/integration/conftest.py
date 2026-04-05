# Copyright (c) Nordic Semiconductor ASA
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause

from __future__ import annotations

import os
from pathlib import Path
from typing import Any, Generator

import pytest


def pytest_configure(config: pytest.Config) -> None:
    config._integration_github_failures = []  # type: ignore[attr-defined]


def pytest_runtest_logreport(report: Any) -> None:
    if report.outcome != "failed" or report.when not in ("setup", "call", "teardown"):
        return
    if not os.environ.get("GITHUB_STEP_SUMMARY"):
        return
    failures: list[tuple[str, str, str]] = getattr(
        report.config, "_integration_github_failures", []
    )
    failures.append((report.nodeid, report.when, str(report.longrepr)))
    report.config._integration_github_failures = failures  # type: ignore[attr-defined]


def pytest_collectreport(report: Any) -> None:
    if report.outcome != "failed":
        return
    if not os.environ.get("GITHUB_STEP_SUMMARY"):
        return
    config = report.config
    failures: list[tuple[str, str, str]] = getattr(
        config, "_integration_github_failures", []
    )
    nodeid = getattr(report, "nodeid", "collection")
    failures.append((nodeid, "collect", str(report.longrepr)))
    config._integration_github_failures = failures  # type: ignore[attr-defined]


def pytest_sessionfinish(session: pytest.Session, exitstatus: int) -> None:
    summary_path = os.environ.get("GITHUB_STEP_SUMMARY")
    if not summary_path or exitstatus == 0:
        return
    failures: list[tuple[str, str, str]] | None = getattr(
        session.config, "_integration_github_failures", None
    )
    if not failures:
        return
    parts: list[str] = ["\n## Integration test failures\n\n"]
    for nodeid, when, text in failures:
        parts.append(f"### `{nodeid}` ({when})\n\n")
        parts.append("```\n")
        parts.append(text)
        if not text.endswith("\n"):
            parts.append("\n")
        parts.append("```\n\n")
    try:
        with open(summary_path, "a", encoding="utf-8") as f:
            f.write("".join(parts))
    except OSError:
        pass

from simulator_runner import SimulatorRunner

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
def simulator_runner(zephyr_build_dir: Path | None) -> Generator[SimulatorRunner, None, None]:
    """Runs native_sim until tests finish; first waits for MQTT connected log line."""
    if zephyr_build_dir is None:
        pytest.skip("No Zephyr build dir (set ZEPHYR_BUILD_DIR or build build-native-sim/app)")
    exe = find_native_sim_executable(zephyr_build_dir)
    if exe is None:
        pytest.skip(f"No native_sim executable under {zephyr_build_dir / 'zephyr'!s}")

    dut = SimulatorRunner(exe)
    dut.start(boot_timeout=30.0)
    try:
        dut.wait_for_substring("Connected to MQTT broker", timeout=120.0)
        yield dut

    except TimeoutError as e:
        pytest.fail(f"App failed to connect to mqtt broker: {e}")

    finally:
        dut.stop()
        dut.assert_no_error_logs()
