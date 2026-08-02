#!/usr/bin/env python3
"""Validate the committed corrected-patch baseline against its own report.

`docs/data/ccsg_f8_flat_wb_patches.csv` is the accepted-flat regression table:
the headerless R/G/B output of the documented `camera_iq patches` run. It is
committed so that a change to the flat-field gate — or to anything upstream of
patch means — cannot silently move published color numbers.

A committed table only guards anything if something checks it. Nothing here
re-runs the command, because the source RAW files are private; what it does
check is that the table still has the shape the command produces and still
agrees with the A1 value `PATCH_EXTRACTION.md` publishes in prose. Those two
drift apart exactly when someone updates one and not the other.

Usage: python3 tools/check_patch_baseline.py [--repo-root PATH]
"""

from __future__ import annotations

import argparse
import math
import re
import sys
from pathlib import Path


BASELINE = Path("docs/data/ccsg_f8_flat_wb_patches.csv")
REPORT = Path("docs/reports/PATCH_EXTRACTION.md")
EXPECTED_ROWS = 140

# | first patch A1 corrected RGB | 7677.11 / 7639.68 / 8712.55 |
A1_RE = re.compile(
    r"first patch A1 corrected RGB\s*\|\s*"
    r"([0-9.]+)\s*/\s*([0-9.]+)\s*/\s*([0-9.]+)"
)


def check(root: Path) -> list[str]:
    errors: list[str] = []
    path = root / BASELINE
    if not path.is_file():
        return [f"{BASELINE}: missing"]

    lines = [ln for ln in path.read_text().splitlines() if ln.strip()]

    if len(lines) != EXPECTED_ROWS:
        errors.append(
            f"{BASELINE}: expected {EXPECTED_ROWS} patch rows, found {len(lines)}"
        )

    values: list[list[float]] = []
    for index, line in enumerate(lines):
        fields = line.split(",")
        if len(fields) != 3:
            errors.append(
                f"{BASELINE}:{index + 1}: expected 3 R/G/B fields, "
                f"found {len(fields)}"
            )
            continue
        try:
            row = [float(f) for f in fields]
        except ValueError:
            # Row 1 is the place a stray header shows up; name that case so the
            # failure explains itself instead of reading as corrupt data.
            what = "header" if index == 0 else "non-numeric field"
            errors.append(f"{BASELINE}:{index + 1}: unexpected {what} ({line!r})")
            continue
        if not all(math.isfinite(v) for v in row):
            errors.append(f"{BASELINE}:{index + 1}: values must be finite")
            continue
        if any(v <= 0 for v in row):
            errors.append(
                f"{BASELINE}:{index + 1}: corrected patch means must be positive"
            )
        values.append(row)

    report = root / REPORT
    if not report.is_file():
        errors.append(f"{REPORT}: missing")
        return errors

    match = A1_RE.search(report.read_text())
    if not match:
        errors.append(f"{REPORT}: no 'first patch A1 corrected RGB' row to check")
    elif values:
        published = [float(g) for g in match.groups()]
        # The report quotes A1 to 2 decimals; compare at that precision so this
        # pins the published claim without pinning digits it never stated.
        for channel, (want, got) in enumerate(zip(published, values[0])):
            if round(got, 2) != want:
                errors.append(
                    f"{BASELINE}: A1 {'RGB'[channel]} is {got:.2f}, but "
                    f"{REPORT} publishes {want:.2f}"
                )

    return errors


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo-root", default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()

    errors = check(Path(args.repo_root))
    for error in errors:
        print(error, file=sys.stderr)
    if errors:
        return 1
    print(f"corrected-patch baseline ok: {EXPECTED_ROWS} rows, A1 matches report")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
