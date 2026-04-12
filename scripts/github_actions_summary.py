# Copyright (c) Nordic Semiconductor ASA
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause

"""Append GitHub Actions job summary sections (GITHUB_STEP_SUMMARY).

Used by ``tests/integration/conftest.py`` during the integration pytest session.
"""

from __future__ import annotations

import html
from pathlib import Path
from typing import Any

import pytest

# GitHub-style semantic colors (readable on light job-summary background).
_COLOR_PASS = "#1a7f37"  # green
_COLOR_FAIL = "#cf222e"  # red
_COLOR_SKIP = "#9a6700"  # orange


def _terminal_reporter(session: pytest.Session) -> Any | None:
    pm = session.config.pluginmanager
    try:
        return pm.get_plugin("terminalreporter")
    except AttributeError:
        return getattr(pm, "getplugin", lambda _n: None)("terminalreporter")


def _nodeid(entry: Any) -> str | None:
    if entry is None:
        return None
    nid = getattr(entry, "nodeid", None)
    if isinstance(nid, str) and nid:
        return nid
    s = str(entry)
    return s if s else None


def _gather_outcome_lists_from_tr(tr: Any) -> dict[str, list[str]]:
    """Map terminal outcome bucket -> sorted nodeids (from pytest's terminal reporter)."""
    out: dict[str, list[str]] = {}
    stats = getattr(tr, "stats", {}) or {}
    for key in ("failed", "error", "skipped", "xfailed", "xpassed", "passed", "deselected"):
        raw = stats.get(key) or []
        nodeids: list[str] = []
        for entry in raw:
            nid = _nodeid(entry)
            if nid:
                nodeids.append(nid)
        if nodeids:
            out[key] = sorted(set(nodeids))
    return out


def _colored_li(label: str, nodeid: str, color: str) -> str:
    safe_label = html.escape(label, quote=True)
    safe_node = html.escape(nodeid, quote=True)
    return (
        f'<li><span style="color: {color};"><strong>{safe_label}</strong> '
        f"<code>{safe_node}</code></span></li>\n"
    )


def append_session_test_outcomes(session: pytest.Session, summary_path: Path) -> None:
    """Write a colored list of passed / failed / skipped / deselected tests to the job summary."""
    tr = _terminal_reporter(session)
    if tr is None:
        try:
            with summary_path.open("a", encoding="utf-8") as f:
                f.write(
                    "\n## Integration test outcomes\n\n"
                    "_Could not read results (pytest terminal reporter not available)._\n"
                )
        except OSError:
            pass
        return

    buckets = _gather_outcome_lists_from_tr(tr)
    failed = buckets.get("failed", [])
    errors = buckets.get("error", [])
    skipped = buckets.get("skipped", [])
    xfailed = buckets.get("xfailed", [])
    xpassed = buckets.get("xpassed", [])
    passed = buckets.get("passed", [])
    deselected = buckets.get("deselected", [])

    np = len(passed)
    nf = len(failed) + len(errors)
    ns = len(skipped) + len(xfailed) + len(xpassed)
    nd = len(deselected)
    lines: list[str] = [
        "\n## Integration test outcomes\n\n",
        f"**Totals:** {np} passed, {nf} failed, {ns} skipped/xfail/xpass, {nd} deselected\n\n",
        "<ul>\n",
    ]

    for nodeid in failed:
        lines.append(_colored_li("FAIL", nodeid, _COLOR_FAIL))
    for nodeid in errors:
        lines.append(_colored_li("ERROR", nodeid, _COLOR_FAIL))
    for nodeid in skipped:
        lines.append(_colored_li("SKIP", nodeid, _COLOR_SKIP))
    for nodeid in xfailed:
        lines.append(_colored_li("XFAIL", nodeid, _COLOR_SKIP))
    for nodeid in xpassed:
        lines.append(_colored_li("XPASS", nodeid, _COLOR_SKIP))
    for nodeid in passed:
        lines.append(_colored_li("PASS", nodeid, _COLOR_PASS))
    for nodeid in deselected:
        lines.append(_colored_li("DESELECTED", nodeid, _COLOR_SKIP))

    lines.append("</ul>\n")

    try:
        with summary_path.open("a", encoding="utf-8") as f:
            f.write("".join(lines))
    except OSError:
        pass


def append_session_failure_details(session: pytest.Session, summary_path: Path) -> None:
    """Append failure longrepr blocks collected during the run (see conftest hooks)."""
    failures: list[tuple[str, str, str]] | None = getattr(
        session.config, "_integration_github_failures", None
    )
    if not failures:
        return
    parts: list[str] = ["\n## Integration test failures (details)\n\n"]
    for nodeid, when, text in failures:
        parts.append(f"### `{html.escape(nodeid, quote=True)}` ({when})\n\n")
        parts.append("```\n")
        parts.append(text)
        if not text.endswith("\n"):
            parts.append("\n")
        parts.append("```\n\n")
    try:
        with summary_path.open("a", encoding="utf-8") as f:
            f.write("".join(parts))
    except OSError:
        pass
