# Copyright (c) Nordic Semiconductor ASA
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause

"""Sanity-check that parsed Kconfig matches firmware expectations."""

from __future__ import annotations

import pytest


@pytest.mark.mqtt
def test_device_id_matches_overlay(integration_device_id: str) -> None:
    assert integration_device_id == "remoteguard-test-001"


@pytest.mark.mqtt
def test_broker_host_not_empty(mqtt_broker_host: str) -> None:
    assert len(mqtt_broker_host) > 0
    assert "." in mqtt_broker_host or mqtt_broker_host == "localhost"
