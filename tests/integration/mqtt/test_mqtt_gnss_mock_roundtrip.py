# Copyright (c) 2023 Nordic Semiconductor ASA
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause

"""MQTT get_location command → GPS JSON on data topic (GNSS mock on native_sim)."""

from __future__ import annotations

import json

import pytest

from kconfig_utils import (
    get_gnss_mock_accuracy,
    get_gnss_mock_latitude,
    get_gnss_mock_longitude,
)


@pytest.mark.mqtt
@pytest.mark.e2e
@pytest.mark.usefixtures("native_sim_dut")
def test_mqtt_get_location_returns_mock_gps_json(
    broker_client,
    mqtt_gps_cmd_topic: str,
    mqtt_gps_data_topic: str,
    kconfig: dict[str, str],
) -> None:
    expected_lat = get_gnss_mock_latitude(kconfig)
    expected_lon = get_gnss_mock_longitude(kconfig)
    expected_acc = get_gnss_mock_accuracy(kconfig)

    broker_client.drain()
    broker_client.subscribe(mqtt_gps_data_topic, qos=1)
    broker_client.publish(mqtt_gps_cmd_topic, b"get", qos=1)

    def match_gps_data_msg(_topic: str, data: bytes) -> bool:
        try:
            o = json.loads(data.decode("utf-8"))
        except (json.JSONDecodeError, UnicodeDecodeError):
            return False
        return "lat" in o and "lon" in o and "accuracy" in o
    try:
        _topic, payload = broker_client.wait_for_message(match_gps_data_msg, timeout=45.0)
    except TimeoutError as e:
        pytest.fail(f"Timeout waiting for GPS data message: {e}")
    
    body = json.loads(payload.decode("utf-8"))
    assert body["lat"] == pytest.approx(expected_lat, rel=1e-5, abs=1e-5), f"Expected gps latitude: {expected_lat}, got {body['lat']}"
    assert body["lon"] == pytest.approx(expected_lon, rel=1e-5, abs=1e-5), f"Expected gps longitude: {expected_lon}, got {body['lon']}"
    assert body["accuracy"] == pytest.approx(expected_acc, rel=1e-3, abs=1e-3), f"Expected gps accuracy: {expected_acc}, got {body['accuracy']}"
