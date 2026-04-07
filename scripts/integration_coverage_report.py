#!/usr/bin/env python3
# Copyright (c) Nordic Semiconductor ASA
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#
# After native_sim integration tests with CONFIG_COVERAGE=y, collect .gcda/.gcno
# under the Zephyr build directory and produce gcovr HTML + JSON summary for app/src only.

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
from pathlib import Path


def _append_github_step_summary(
    path: Path,
    repo_root: Path,
    line_percent: float,
    line_covered: int,
    line_total: int,
    html_index: Path,
) -> None:
    """Write a short markdown section to GITHUB_STEP_SUMMARY (CI job summary page)."""
    try:
        rel_html = html_index.resolve().relative_to(repo_root.resolve())
    except ValueError:
        rel_html = html_index.name
    lines = [
        "\n## Integration test coverage (`app/src`)\n\n",
        f"**Line coverage: {line_percent:.2f}%** "
        f"({line_covered} of {line_total} lines)\n\n",
        f"Detailed HTML report: `{rel_html}` (CI artifact `integration-coverage-html`).\n",
    ]
    try:
        with path.open("a", encoding="utf-8") as f:
            f.write("".join(lines))
    except OSError:
        pass


def _append_github_output(
    path: Path,
    line_percent: float,
    line_covered: int,
    line_total: int,
) -> None:
    """Set step outputs for downstream jobs / reuse (KEY=value format)."""
    try:
        with path.open("a", encoding="utf-8") as f:
            f.write(f"integration_app_src_line_percent={line_percent:.4f}\n")
            f.write(f"integration_app_src_lines_covered={line_covered}\n")
            f.write(f"integration_app_src_lines_total={line_total}\n")
    except OSError:
        pass


def _github_actions_notice(line_percent: float) -> None:
    if os.environ.get("GITHUB_ACTIONS") != "true":
        return
    # Shows in the Actions log UI as a notice annotation.
    print(f"::notice title=Integration coverage (app/src)::Line coverage {line_percent:.2f}%")


def run_gcovr(
    repo_root: Path,
    build_dir: Path,
    out_dir: Path,
    gcov_executable: str | None,
) -> int:
    out_dir.mkdir(parents=True, exist_ok=True)
    html_index = out_dir / "index.html"
    summary_json = out_dir / "coverage-summary.json"

    cmd: list[str] = [
        os.environ.get("GCOVR", "gcovr"),
        "--root",
        str(repo_root),
        "--object-directory",
        str(build_dir),
        # Only include application sources under app/src (not Zephyr, not app/drivers, etc.).
        # Match both repo-relative and absolute paths after gcov resolves locations.
        "--filter",
        r"(^|/)app/src/",
        "--html-details",
        str(html_index),
        "--json-summary",
        str(summary_json),
        "--json-summary-pretty",
        "--print-summary",
    ]
    if gcov_executable:
        cmd.extend(["--gcov-executable", gcov_executable])

    print("+", " ".join(cmd), flush=True)
    r = subprocess.run(cmd, cwd=str(repo_root))
    if r.returncode != 0:
        return r.returncode

    if not summary_json.is_file():
        print("error: gcovr did not write JSON summary", file=sys.stderr)
        return 1

    data = json.loads(summary_json.read_text(encoding="utf-8"))
    line_total = int(data.get("line_total", 0))
    line_covered = int(data.get("line_covered", 0))
    line_percent = float(data.get("line_percent", 0.0))

    if line_total == 0:
        print(
            "warning: no covered lines under app/src — "
            "check filters and that .gcda files exist after the simulator exited.",
            file=sys.stderr,
        )

    _github_actions_notice(line_percent)

    gh_summary = os.environ.get("GITHUB_STEP_SUMMARY")
    if gh_summary:
        _append_github_step_summary(
            Path(gh_summary),
            repo_root,
            line_percent,
            line_covered,
            line_total,
            html_index,
        )

    gh_output = os.environ.get("GITHUB_OUTPUT")
    if gh_output:
        _append_github_output(Path(gh_output), line_percent, line_covered, line_total)

    return 0


def main() -> int:
    p = argparse.ArgumentParser(
        description="Generate gcovr HTML + summary for app/src from a Zephyr native_sim build."
    )
    p.add_argument(
        "--repo-root",
        type=Path,
        required=True,
        help="Repository root (gcovr --root).",
    )
    p.add_argument(
        "--build-dir",
        type=Path,
        required=True,
        help="West/Zephyr build directory containing coverage data (gcovr --object-directory).",
    )
    p.add_argument(
        "--output-dir",
        type=Path,
        default=None,
        help="Output directory for index.html and coverage-summary.json "
        "(default: <repo>/tests/integration/artifacts/coverage-html).",
    )
    p.add_argument(
        "--gcov-executable",
        default=os.environ.get("GCOV_EXE"),
        help="gcov binary (default: $GCOV_EXE or PATH). For -m32 native_sim builds, "
        "this must match the compiler (e.g. plain 'gcov' on Ubuntu with gcc-multilib).",
    )
    args = p.parse_args()

    repo_root = args.repo_root.resolve()
    build_dir = args.build_dir.resolve()
    out_dir = (args.output_dir or (repo_root / "tests/integration/artifacts/coverage-html")).resolve()

    if not build_dir.is_dir():
        print(f"error: build directory does not exist: {build_dir}", file=sys.stderr)
        return 1

    return run_gcovr(repo_root, build_dir, out_dir, args.gcov_executable)


if __name__ == "__main__":
    sys.exit(main())
