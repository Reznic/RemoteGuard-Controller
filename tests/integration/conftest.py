"""Integration tests configuration and fixtures."""
from __future__ import annotations

import os
import sys
from time import sleep
from pathlib import Path
from typing import Any, Generator
import pytest

from broker_client import BrokerClient

_SCRIPTS_DIR = Path(__file__).resolve().parent.parent.parent / "scripts"
if _SCRIPTS_DIR.is_dir() and str(_SCRIPTS_DIR) not in sys.path:
    sys.path.insert(0, str(_SCRIPTS_DIR))

from github_actions_summary import (  # type: ignore[import-untyped]
    append_session_failure_details,
    append_session_test_outcomes,
)

# TestReport / CollectReport do not expose .config in all pytest versions; stash
# the active Config from pytest_configure for GitHub Actions summary hooks.
_integration_config: pytest.Config | None = None


def pytest_configure(config: pytest.Config) -> None:
    global _integration_config
    _integration_config = config
    config._integration_github_failures = []  # type: ignore[attr-defined]


def pytest_runtest_logreport(report: Any) -> None:
    cfg = _integration_config
    if cfg is None:
        return
    if report.outcome != "failed" or report.when not in ("setup", "call", "teardown"):
        return
    if not os.environ.get("GITHUB_STEP_SUMMARY"):
        return
    failures: list[tuple[str, str, str]] = getattr(
        cfg, "_integration_github_failures", []
    )
    failures.append((report.nodeid, report.when, str(report.longrepr)))
    cfg._integration_github_failures = failures  # type: ignore[attr-defined]


def pytest_collectreport(report: Any) -> None:
    cfg = _integration_config
    if cfg is None:
        return
    if report.outcome != "failed":
        return
    if not os.environ.get("GITHUB_STEP_SUMMARY"):
        return
    failures: list[tuple[str, str, str]] = getattr(
        cfg, "_integration_github_failures", []
    )
    nodeid = getattr(report, "nodeid", "collection")
    failures.append((nodeid, "collect", str(report.longrepr)))
    cfg._integration_github_failures = failures  # type: ignore[attr-defined]


def pytest_sessionfinish(session: pytest.Session, exitstatus: int) -> None:
    summary_path = os.environ.get("GITHUB_STEP_SUMMARY")
    if not summary_path:
        return
    sp = Path(summary_path)
    append_session_test_outcomes(session, sp)
    if exitstatus != 0:
        append_session_failure_details(session, sp)

import kconfig_utils
import simulator_network_mock
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


def _zephyr_dotconfig_ok(build_dir: Path) -> bool:
    return (build_dir / "zephyr" / ".config").is_file()


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
def mqtt_lwt_topic(kconfig: dict[str, str], integration_device_id: str) -> str:
    return kconfig_utils.mqtt_lwt_topic(integration_device_id, kconfig)


@pytest.fixture(autouse=True)
def _simulator_per_test_log_checks(request: pytest.FixtureRequest) -> Generator[None, None, None]:
    """Tag each test in the simulator log and assert no <err> lines for that test."""
    if "dev_simulator" not in request.fixturenames:
        yield
        return

    simulator = request.getfixturevalue("dev_simulator")
    bar = "=" * 30
    test_start_mark = f"{bar}\nStarting: {request.node.name}\n{bar}\n"
    simulator.inject_to_captured_log(test_start_mark)
    log_start_offset = len(simulator.joined_output())

    yield

    if not request.node.get_closest_marker("expect_errors_in_log"):
        segment = simulator.joined_output()[log_start_offset:]
        simulator.assert_no_error_logs(log_segment=segment)


@pytest.fixture(scope="session")
def dev_simulator(zephyr_build_dir: Path | None) -> Generator[SimulatorRunner, None, None]:
    """Runs zephyr device simulator on host using native_sim."""
    exe = find_native_sim_executable(zephyr_build_dir)
    if exe is None:
        pytest.fail(f"No native_sim executable under {zephyr_build_dir / 'zephyr'!s}")

    simulator = SimulatorRunner(exe)

    try:
        simulator_network_mock.start()
    except Exception as e:
        pytest.fail(f"simulator network (Host NAT) setup failed: {e}")

    try:
        simulator.start(boot_timeout=30.0)
    except Exception as e:
        simulator_network_mock.unblock_zeth()
        simulator_network_mock.stop()
        pytest.fail(f"Failed to start zephyr device simulator: {e}")

    sleep(1.0)
    simulator.assert_no_error_logs()

    try:
        yield simulator
    finally:
        simulator_network_mock.unblock_zeth()
        simulator.stop()
        simulator_network_mock.stop()


@pytest.fixture
def network() -> Generator[None, None, None]:
    """Ensure host NAT is active and zeth is not partitioned (omit in LWT/disconnect tests)."""
    simulator_network_mock.ensure_started()
    simulator_network_mock.unblock_zeth()
    yield
    simulator_network_mock.unblock_zeth()
