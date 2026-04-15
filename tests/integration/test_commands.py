"""E2E Tests"""
from __future__ import annotations
import json
import pytest

pytestmark = [pytest.mark.usefixtures("network")]


@pytest.mark.mqtt
@pytest.mark.e2e
@pytest.mark.usefixtures("dev_simulator")
def test_get_gps_location(
    broker_client,
    mqtt_gps_cmd_topic: str,
    mqtt_gps_data_topic: str
) -> None:
    # Must match STUB_* in tests/mocks/gnss_modem_mock.c
    expected_lat = 59.913900
    expected_lon = 10.752200
    expected_acc = 5.0

    broker_client.drain()
    broker_client.subscribe(mqtt_gps_data_topic, qos=1)
    broker_client.publish(mqtt_gps_cmd_topic, b"get", qos=1)

    try:
        gps_data_msg = broker_client.wait_for_message_on_topic(mqtt_gps_data_topic, timeout=45.0)
    except TimeoutError as e:
        pytest.fail(f"Timeout waiting for GPS data message: {e}")
    
    try:
        coords = json.loads(gps_data_msg.decode("utf-8"))
    except (json.JSONDecodeError, UnicodeDecodeError):
        pytest.fail(f"Invalid GPS data message: {gps_data_msg}")
    assert coords["lat"] == pytest.approx(expected_lat, rel=1e-5, abs=1e-5), f"Expected gps latitude: {expected_lat}, got {coords['lat']}"
    assert coords["lon"] == pytest.approx(expected_lon, rel=1e-5, abs=1e-5), f"Expected gps longitude: {expected_lon}, got {coords['lon']}"
    assert coords["accuracy"] == pytest.approx(expected_acc, rel=1e-3, abs=1e-3), f"Expected gps accuracy: {expected_acc}, got {coords['accuracy']}"


@pytest.mark.e2e
def test_take_photo(
    broker_client,
    dev_simulator,
    mqtt_transport_subscribe_topic: str,
) -> None:
    broker_client.drain()
    broker_client.publish(mqtt_transport_subscribe_topic, "take_photo", qos=1)
    dev_simulator.wait_for_substring("Published take_photo command to camera", timeout=30.0)
