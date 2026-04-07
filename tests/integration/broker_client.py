# Copyright (c) Nordic Semiconductor ASA
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause

from __future__ import annotations

import threading
import time
import uuid
from collections import deque
from typing import Any, Callable, Optional

import paho.mqtt.client as mqtt


class BrokerClient:
    """Thin synchronous wrapper over paho-mqtt for tests."""

    def __init__(self, hostname: str, port: int = 1883) -> None:
        self._hostname = hostname
        self._port = port
        self._client = mqtt.Client(
            mqtt.CallbackAPIVersion.VERSION2,
            client_id=f"pytest-{uuid.uuid4().hex[:12]}",
        )
        self._lock = threading.Lock()
        self._messages: deque[tuple[str, bytes]] = deque(maxlen=256)
        self._connected = threading.Event()

        def on_connect(
            client: mqtt.Client,
            userdata: Any,
            flags: Any,
            reason_code: Any,
            properties: Any = None,
        ) -> None:
            self._connected.set()

        def on_message(client: mqtt.Client, userdata: Any, msg: mqtt.MQTTMessage) -> None:
            with self._lock:
                self._messages.append((msg.topic, msg.payload))

        self._client.on_connect = on_connect
        self._client.on_message = on_message

    def connect(self, timeout: float = 15.0) -> None:
        self._client.connect(self._hostname, self._port, 60)
        self._client.loop_start()
        if not self._connected.wait(timeout):
            raise TimeoutError(f"MQTT connect to {self._hostname}:{self._port}")

    def disconnect(self) -> None:
        self._client.loop_stop()
        try:
            self._client.disconnect()
        except Exception:
            pass

    def subscribe(self, topic: str, qos: int = 1) -> None:
        self._client.subscribe(topic, qos)

    def publish(self, topic: str, payload: bytes | str, qos: int = 1) -> None:
        if isinstance(payload, str):
            payload = payload.encode("utf-8")
        self._client.publish(topic, payload, qos)

    def wait_for_message(
        self,
        predicate: Callable[[str, bytes], bool],
        timeout: float = 30.0,
        poll: float = 0.05,
    ) -> tuple[str, bytes]:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            with self._lock:
                for i, (topic, payload) in enumerate(self._messages):
                    if predicate(topic, payload):
                        del self._messages[i]
                        return topic, payload
            time.sleep(poll)
        raise TimeoutError("No matching MQTT message received")

    def wait_for_message_on_topic(self, topic: str, timeout: float = 30.0, poll: float = 0.05) -> bytes:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            with self._lock:
                for i, (_topic, payload) in enumerate(self._messages):
                    if _topic == topic:
                        del self._messages[i]
                        return payload
            time.sleep(poll)
        raise TimeoutError(f"Timeout receiving message on topic {topic}")

    def drain(self) -> None:
        with self._lock:
            self._messages.clear()
