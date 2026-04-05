# Copyright (c) 2023 Nordic Semiconductor ASA
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause

"""Run the Zephyr native_sim executable and capture stdout for integration tests."""

from __future__ import annotations

import os
import subprocess
import threading
import time
from pathlib import Path
import pytest



def native_sim_log_file_path() -> Path:
    """Where CI / local runs persist captured native_sim stdout (see conftest native_sim_dut)."""
    env = os.environ.get("INTEGRATION_NATIVE_SIM_LOG")
    if env:
        return Path(env)
    here = Path(__file__).resolve().parent
    return here / "artifacts" / "native_sim_stdout.log"


class NativeSimDut:
    APP_BOOT_MSG = "Booting nRF Connect"
    LOG_ERR_MARKER = "<err>"
    
    def __init__(self, executable: Path) -> None:
        self._executable = executable
        self._proc: subprocess.Popen[str] | None = None
        self._lines: list[str] = []
        self._stdout_lock = threading.Lock()
        self._reader: threading.Thread | None = None
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

        self._reader = threading.Thread(target=self._read_stdout, daemon=True)
        self._reader.start()
        try:
            self.wait_for_substring(self.APP_BOOT_MSG, timeout=boot_timeout)
        except TimeoutError as e:
            pytest.fail(f"Zephyr app did not boot on simulator: {e}")

    def stop(self) -> None:
        if self._proc is None:
            return
        self._proc.terminate()
        try:
            self._proc.wait(timeout=15)
        except subprocess.TimeoutExpired:
            self._proc.kill()
            self._proc.wait(timeout=5)
        self._proc = None
        self._log_file_path.write_text(self.joined_output(), encoding="utf-8")

    def _read_stdout(self) -> None:
        assert self._proc is not None
        assert self._proc.stdout is not None
        for line in self._proc.stdout:
            with self._stdout_lock:
                self._lines.append(line)
    
    def joined_output(self) -> str:
        with self._stdout_lock:
            return "".join(self._lines)
    
    def wait_for_substring(self, needle: str, timeout: float = 120.0) -> None:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if needle in self.joined_output():
                return
            time.sleep(0.15)
        raise TimeoutError(f"Timeout waiting for {needle!r} in native_sim output")

    def log_file_path(self) -> Path:
        return self._log_file_path

    def find_error_logs(self, log: str) -> list[str]:
        """Return stripped lines that contain Zephyr error-level log marker."""
        return [line.rstrip("\n\r") 
                for line in self.joined_output().splitlines() 
                if self.LOG_ERR_MARKER in line]

    def assert_no_error_logs(self) -> None:
        """Fail the current test if the log contains any errors"""
        error_logs = self.find_error_logs()
        if error_logs:
            pytest.fail("simulator log had errors:\n" + "\n".join(error_logs))
