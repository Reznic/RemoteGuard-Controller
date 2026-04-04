# Copyright (c) Nordic Semiconductor ASA
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
"""Parse Zephyr build .config for integration tests (broker URL, client ID)."""

from __future__ import annotations

import os
import re
from pathlib import Path
from typing import Optional


def find_zephyr_dotconfig(build_dir: Optional[Path] = None) -> Optional[Path]:
    """Return path to zephyr/.config inside a west build directory."""
    if build_dir is None:
        env = os.environ.get("ZEPHYR_BUILD_DIR")
        if env:
            build_dir = Path(env)
        else:
            return None
    candidates = [
        build_dir / "zephyr" / ".config",
        build_dir / "app" / "zephyr" / ".config",
    ]
    for p in candidates:
        if p.is_file():
            return p
    return None


def parse_dotconfig(path: Path) -> dict[str, str]:
    """Parse CONFIG_KEY=value lines (Zephyr kconfig export format)."""
    out: dict[str, str] = {}
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        m = re.match(r"^(CONFIG_[A-Za-z0-9_]+)=(.*)$", line)
        if m:
            key, val = m.group(1), m.group(2)
            if val.startswith('"') and val.endswith('"'):
                val = val[1:-1].replace('\\"', '"')
            out[key] = val
    return out


def get_mqtt_broker_host(cfg: dict[str, str]) -> str:
    return cfg.get(
        "CONFIG_MQTT_SAMPLE_TRANSPORT_BROKER_HOSTNAME",
        os.environ.get("MQTT_BROKER_HOST", "broker.hivemq.com"),
    )


def get_example_device_id(cfg: dict[str, str]) -> str:
    return cfg.get(
        "CONFIG_MQTT_SAMPLE_TRANSPORT_CLIENT_ID",
        os.environ.get("INTEGRATION_TEST_DEVICE_ID", "remoteguard-test-001"),
    )


def load_build_config(build_dir: Optional[Path] = None) -> dict[str, str]:
    path = find_zephyr_dotconfig(build_dir)
    if path is None:
        return {}
    return parse_dotconfig(path)


def full_topic(device_id: str, suffix: str) -> str:
    """Mirror transport `topics_prefix()`: {client_id}/{suffix}."""
    return f"{device_id}/{suffix}"


def get_mqtt_transport_subscribe_suffix(cfg: dict[str, str]) -> str:
    return cfg.get("CONFIG_MQTT_SAMPLE_TRANSPORT_SUBSCRIBE_TOPIC", "my/subscribe/topic")


def get_mqtt_gps_cmd_suffix(cfg: dict[str, str]) -> str:
    return cfg.get("CONFIG_MQTT_GPS_CMD_TOPIC", "device/get_gps")


def get_mqtt_gps_data_suffix(cfg: dict[str, str]) -> str:
    return cfg.get("CONFIG_MQTT_GPS_DATA_TOPIC", "device/gps")


def get_gnss_mock_latitude(cfg: dict[str, str]) -> float:
    return float(cfg.get("CONFIG_MQTT_SAMPLE_GNSS_MOCK_LATITUDE", "59.913900"))


def get_gnss_mock_longitude(cfg: dict[str, str]) -> float:
    return float(cfg.get("CONFIG_MQTT_SAMPLE_GNSS_MOCK_LONGITUDE", "10.752200"))


def get_gnss_mock_accuracy(cfg: dict[str, str]) -> float:
    return float(cfg.get("CONFIG_MQTT_SAMPLE_GNSS_MOCK_ACCURACY", "5.0"))


def find_native_sim_executable(build_dir: Path) -> Optional[Path]:
    """Return the native_sim host runner under zephyr/ (Linux or Windows name)."""
    for name in ("zephyr", "zephyr.exe"):
        candidate = build_dir / "zephyr" / name
        if candidate.is_file():
            return candidate
    return None
