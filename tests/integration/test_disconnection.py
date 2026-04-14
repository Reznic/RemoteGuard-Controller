# Copyright (c) Nordic Semiconductor ASA
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause

"""MQTT Last Will and Testament integration tests (native_sim + public broker)."""

from __future__ import annotations

import pytest

import simulator_network_mock
from kconfig_utils import get_mqtt_keepalive_seconds, get_mqtt_lwt_will_message

@pytest.mark.mqtt
@pytest.mark.e2e
@pytest.mark.expect_errors_in_log
def test_disconnect_and_reconnect_notification(broker_client, dev_simulator, mqtt_lwt_topic, kconfig):
    """Test reception of device connection/disconnection messages over mqtt LWT topic."""
    if kconfig.get("CONFIG_MQTT_CLIENT_LAST_WILL") != "y":
        pytest.skip("CONFIG_MQTT_CLIENT_LAST_WILL is not enabled in this build")

    device_keepalive = get_mqtt_keepalive_seconds(kconfig)
    timeout = float(max(45, device_keepalive * 2 + 20))
    expected_disconnect_msg = get_mqtt_lwt_will_message(kconfig).encode("utf-8")

    broker_client.clear_retained(mqtt_lwt_topic)
    broker_client.subscribe(mqtt_lwt_topic, qos=1)
    broker_client.drain()

    # No disconnect message should be received while the simulator is still running.
    try:
        msg = broker_client.wait_for_message_on_topic(mqtt_lwt_topic, timeout=timeout)
    except TimeoutError:
        pass  # Expected: no disconnect message.
    except Exception as e:
        pytest.fail(f"Error waiting for device disconnect message on the LWT topic: {e}")
    else:
        pytest.fail(f"Received a device disconnect message on the LWT topic before the simulator was stopped.\n{msg!r}")

    msg = None
    try:
        # Simulate a network disconnection by blocking simulator's network interface.
        simulator_network_mock.block_zeth()
        msg = broker_client.wait_for_message_on_topic(mqtt_lwt_topic, timeout=timeout)
        assert msg == expected_disconnect_msg, \
            f"Expected LWT payload {expected_disconnect_msg!r}, got {msg!r}"
    except TimeoutError as e:
        pytest.fail(
            f"Failed to get notification on device abrupt disconnect.\n"
            f"Timeout waiting for broker LWT on {mqtt_lwt_topic!r}: {e}"
        )
    finally:
        # Restore device network connectivity.
        simulator_network_mock.unblock_zeth()

    # Verify that the device is able to reconnect to mqtt broker again.
    try:
        online_msg = broker_client.wait_for_message_on_topic(mqtt_lwt_topic, timeout=timeout)
    except TimeoutError as e:
        pytest.fail(
            f"Failed to get online message from device after reconnect.\n"
            f"Timeout waiting for online message on {mqtt_lwt_topic!r}: {e}"
        )
    
    assert online_msg == b"online", f"Expected online message from device after reconnect, got {online_msg!r}"