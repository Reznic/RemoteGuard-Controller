# Copyright (c) 2023 Nordic Semiconductor ASA
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause

"""native_sim E2E: MQTT command handling."""

from __future__ import annotations

import pytest


@pytest.mark.mqtt
@pytest.mark.e2e
def test_take_photo_mqtt_command_reaches_transport(
    broker_client,
    native_sim_dut,
    mqtt_transport_subscribe_topic: str,
) -> None:
    broker_client.drain()
    broker_client.publish(mqtt_transport_subscribe_topic, "take_photo", qos=1)
    native_sim_dut.wait_for_substring("Published take_photo command to camera", timeout=30.0)
