# Copyright (c) Nordic Semiconductor ASA
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause

"""Smoke tests: Python can reach the public MQTT broker (no firmware required)."""

from __future__ import annotations

import pytest


@pytest.mark.mqtt
def test_broker_connect(broker_client) -> None:
    assert broker_client._connected.is_set()


@pytest.mark.mqtt
def test_broker_pub_sub_roundtrip(broker_client, topic_prefix: str) -> None:
    t = f"{topic_prefix}/integration/pytest/ping"
    broker_client.subscribe(t)
    payload = b"ping"
    broker_client.publish(t, payload)

    def match(topic: str, data: bytes) -> bool:
        return topic == t and data == payload

    _topic, data = broker_client.wait_for_message(match, timeout=20.0)
    assert data == payload
