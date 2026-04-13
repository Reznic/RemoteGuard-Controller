# Copyright (c) 2023 Nordic Semiconductor ASA
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause

"""Run the Zephyr native_sim executable and capture stdout for integration tests."""

from __future__ import annotations

import os
import signal
import subprocess
import threading
import time
from pathlib import Path

import pytest


def native_sim_log_file_path() -> Path:
    """Where CI / local runs persist captured native_sim stdout (see conftest simulator_runner)."""
    env = os.environ.get("INTEGRATION_NATIVE_SIM_LOG")
    if env:
        return Path(env)
    here = Path(__file__).resolve().parent
    return here / "artifacts" / "zephyr_simulator_log.txt"


class SimulatorRunner:
    APP_BOOT_MSG = "Booting nRF Connect"
    MQTT_CONNECTED_MSG = "Connected to MQTT broker"
    LOG_ERR_MARKER = "<err>"

    def __init__(self, executable: Path) -> None:
        self._executable = executable
        self._proc: subprocess.Popen[str] | None = None
        self._log_lines: list[str] = []
        self._log_reader_lock = threading.Lock()
        self._log_reader: threading.Thread | None = None
        self._log_file_path = native_sim_log_file_path()

    def start(self, boot_timeout: float = 30.0) -> None:
        if self._proc is not None:
            return
        self._log_file_path.parent.mkdir(parents=True, exist_ok=True)
        self._proc = subprocess.Popen(
            [str(self._executable)],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1,
        )

        self._log_reader = threading.Thread(target=self._read_stdout, daemon=True)
        self._log_reader.start()
        self.wait_for_bootup(timeout=boot_timeout)
        self.wait_for_mqtt_connection()

    def wait_for_bootup(self, timeout: float = 30.0) -> None:
        try:
            self.wait_for_substring(self.APP_BOOT_MSG, timeout)
        except TimeoutError as e:
            pytest.fail(f"Zephyr app did not boot on simulator: {e}")

    def wait_for_mqtt_connection(self, timeout: float = 120.0) -> None:
        try:
            self.wait_for_substring(self.MQTT_CONNECTED_MSG, timeout)
        except TimeoutError as e:
            pytest.fail(f"App failed to connect to mqtt broker: {e}")

    def _close_proc_stdout(self) -> None:
        """Close the child's stdout pipe so :meth:`_read_stdout` unblocks and can exit."""
        if self._proc is None:
            return
        if self._proc.stdout is not None and not self._proc.stdout.closed:
            try:
                self._proc.stdout.close()
            except OSError:
                pass

    def stop(self) -> None:
        if self._proc is None:
            return
        if self._proc.poll() is not None:
            self._close_proc_stdout()
            self._proc = None
            self._log_file_path.write_text(self.joined_output(), encoding="utf-8")
            return
        # Gcov writes .gcda on normal process exit; SIGINT tends to flush before SIGTERM.
        self._proc.send_signal(signal.SIGINT)
        try:
            self._proc.wait(timeout=10.0)
        except subprocess.TimeoutExpired:
            self._proc.terminate()
            try:
                self._proc.wait(timeout=15)
            except subprocess.TimeoutExpired:
                self._proc.kill()
                self._proc.wait(timeout=5)
        self._close_proc_stdout()
        self._proc = None
        self._log_file_path.write_text(self.joined_output(), encoding="utf-8")

    def _read_stdout(self) -> None:
        assert self._proc is not None
        assert self._proc.stdout is not None
        for line in self._proc.stdout:
            with self._log_reader_lock:
                self._log_lines.append(line)

    def joined_output(self) -> str:
        with self._log_reader_lock:
            return "".join(self._log_lines)

    def inject_to_captured_log(self, string: str) -> None:
        """Append a string to captured stdout."""
        with self._log_reader_lock:
            self._log_lines.append(string)

    def wait_for_substring(self, needle: str, timeout: float = 120.0) -> None:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if needle in self.joined_output():
                return
            time.sleep(0.15)
        raise TimeoutError(f"Timeout waiting for {needle!r} in native_sim output")

    def log_file_path(self) -> Path:
        return self._log_file_path

    @classmethod
    def find_error_logs_in_text(cls, text: str) -> list[str]:
        """Return stripped lines that contain Zephyr error-level log marker."""
        return [
            line.rstrip("\n\r")
            for line in text.splitlines()
            if cls.LOG_ERR_MARKER in line
        ]

    def find_error_logs(self) -> list[str]:
        """Return stripped lines that contain Zephyr error-level log marker."""
        return self.find_error_logs_in_text(self.joined_output())

    def assert_no_error_logs(self, *, log_segment: str | None = None) -> None:
        """Fail the current test if the log contains any errors.

        If ``log_segment`` is set, only that substring is scanned (e.g. output
        since the start of the current test). Otherwise the full captured log
        is scanned.
        """
        text = self.joined_output() if log_segment is None else log_segment
        error_logs = self.find_error_logs_in_text(text)
        if error_logs:
            pytest.fail("simulator log had errors:\n" + "\n".join(error_logs))
