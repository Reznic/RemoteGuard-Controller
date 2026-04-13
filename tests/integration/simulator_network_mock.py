# Copyright (c) Nordic Semiconductor ASA
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause

"""Host-side network for native_sim integration: zeth NAT and optional zeth partition (iptables)."""

from __future__ import annotations

import contextlib
import os
import shutil
import subprocess
from collections.abc import Generator
from dataclasses import dataclass
from pathlib import Path

# --- Host NAT (parity with former run_integration_tests.sh) ---


def skip_nat() -> bool:
    return os.environ.get("SKIP_NAT", "").strip() in ("1", "true", "yes")


def _sudo_run(argv: list[str], *, check: bool = True) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        ["sudo", *argv],
        check=check,
        capture_output=True,
        text=True,
    )


def default_route_wan_iface() -> str | None:
    """Return IPv4 default route egress interface name, or None."""
    r = subprocess.run(
        ["ip", "-4", "route", "show", "default"],
        capture_output=True,
        text=True,
        check=False,
    )
    if r.returncode == 0 and r.stdout.strip():
        parts = r.stdout.strip().split()
        for i, p in enumerate(parts):
            if p == "dev" and i + 1 < len(parts):
                return parts[i + 1]
    r2 = subprocess.run(
        ["ip", "route", "show", "default"],
        capture_output=True,
        text=True,
        check=False,
    )
    if r2.returncode == 0 and r2.stdout.strip():
        toks = r2.stdout.strip().split()
        if len(toks) >= 5:
            return toks[4]
    return None


def _read_ip_forward() -> str:
    return Path("/proc/sys/net/ipv4/ip_forward").read_text(encoding="utf-8").strip()


def _write_ip_forward(value: str) -> None:
    subprocess.run(
        ["sudo", "tee", "/proc/sys/net/ipv4/ip_forward"],
        input=value + "\n",
        text=True,
        check=True,
        capture_output=True,
    )


def _iptables_available() -> bool:
    return shutil.which("iptables") is not None


@dataclass
class _HostNatState:
    active: bool = False
    wan: str | None = None
    ip_forward_prev: str | None = None


_nat_state = _HostNatState()


def start() -> None:
    """Enable MASQUERADE + FORWARD for zeth <-> WAN. Idempotent if already active."""
    global _nat_state
    if skip_nat() or _nat_state.active:
        return
    if not _iptables_available():
        raise RuntimeError("iptables not found on PATH (install iptables package)")
    wan = default_route_wan_iface()
    if not wan:
        raise RuntimeError("could not detect default route interface (needed for NAT)")

    prev = _read_ip_forward()
    _write_ip_forward("1")

    _sudo_run(["sysctl", "-q", "-w", "net.ipv4.conf.zeth.rp_filter=0"], check=False)

    _sudo_run(["iptables", "-t", "nat", "-A", "POSTROUTING", "-o", wan, "-j", "MASQUERADE"])
    _sudo_run(["iptables", "-A", "FORWARD", "-i", "zeth", "-o", wan, "-j", "ACCEPT"])
    _sudo_run(
        [
            "iptables",
            "-A",
            "FORWARD",
            "-i",
            wan,
            "-o",
            "zeth",
            "-m",
            "state",
            "--state",
            "RELATED,ESTABLISHED",
            "-j",
            "ACCEPT",
        ]
    )
    _nat_state.active = True
    _nat_state.wan = wan
    _nat_state.ip_forward_prev = prev


def stop() -> None:
    """Remove NAT rules and restore ip_forward. Idempotent."""
    global _nat_state
    if not _nat_state.active:
        return
    wan = _nat_state.wan
    if wan:
        _sudo_run(
            ["iptables", "-t", "nat", "-D", "POSTROUTING", "-o", wan, "-j", "MASQUERADE"],
            check=False,
        )
        _sudo_run(
            ["iptables", "-D", "FORWARD", "-i", "zeth", "-o", wan, "-j", "ACCEPT"],
            check=False,
        )
        _sudo_run(
            [
                "iptables",
                "-D",
                "FORWARD",
                "-i",
                wan,
                "-o",
                "zeth",
                "-m",
                "state",
                "--state",
                "RELATED,ESTABLISHED",
                "-j",
                "ACCEPT",
            ],
            check=False,
        )
    if _nat_state.ip_forward_prev is not None:
        try:
            _write_ip_forward(_nat_state.ip_forward_prev)
        except (OSError, subprocess.CalledProcessError):
            pass
    _nat_state = _HostNatState()


def ensure_started() -> None:
    """No-op when SKIP_NAT; otherwise same as start()."""
    if skip_nat():
        return
    start()


# --- zeth partition (iptables user chain; independent of NAT rules) ---

_CHAIN = "RG_ZETH_PARTITION"
_JUMP_RULE = ["FORWARD", "-j", _CHAIN]


def _sudo_iptables(args: list[str], *, check: bool = True) -> None:
    subprocess.run(["sudo", "iptables", *args], check=check, capture_output=True, text=True)


def _partition_chain_exists() -> bool:
    r = subprocess.run(
        ["sudo", "iptables", "-nL", _CHAIN],
        capture_output=True,
        text=True,
        check=False,
    )
    return r.returncode == 0


def unblock_zeth() -> None:
    """Remove partition jump and chain if present. Idempotent."""
    while True:
        r = subprocess.run(
            ["sudo", "iptables", "-D", *_JUMP_RULE],
            capture_output=True,
            text=True,
            check=False,
        )
        if r.returncode != 0:
            break
    if not _partition_chain_exists():
        return
    _sudo_iptables(["-F", _CHAIN], check=False)
    _sudo_iptables(["-X", _CHAIN], check=False)


def block_zeth() -> None:
    """Insert FORWARD jump that DROPs traffic involving zeth."""
    unblock_zeth()
    _sudo_iptables(["-N", _CHAIN])
    _sudo_iptables(["-A", _CHAIN, "-i", "zeth", "-j", "DROP"])
    _sudo_iptables(["-A", _CHAIN, "-o", "zeth", "-j", "DROP"])
    _sudo_iptables(["-I", "FORWARD", "1", "-j", _CHAIN])


@contextlib.contextmanager
def zeth_partitioned() -> Generator[None, None, None]:
    try:
        block_zeth()
        yield
    finally:
        unblock_zeth()
