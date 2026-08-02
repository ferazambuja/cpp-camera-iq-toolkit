#!/usr/bin/env python3
"""Validate the committed corrected-patch baseline against its own report.

`docs/data/ccsg_f8_flat_wb_patches.csv` is the accepted-flat regression table:
the headerless R/G/B output of the documented `camera_iq patches` run. It is
committed so accidental baseline edits cannot silently move published color
numbers and private-archive reruns have an exact comparator.

A committed table only guards anything if something checks it. Nothing here
re-runs the command, because the source RAW files are private; what it does
check is that the complete table still has the SHA-256 the report publishes,
has the shape the command produces, and agrees with the rounded A1 value the
report publishes in prose. Code-output drift still requires a private-archive
rerun against this baseline.

Usage: python3 tools/check_patch_baseline.py [--repo-root PATH]
"""

from __future__ import annotations

import argparse
import hashlib
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
SHA256_RE = re.compile(r"had SHA-256\s+`([0-9a-fA-F]{64})`")


def check(root: Path) -> list[str]:
    errors: list[str] = []
    path = root / BASELINE
    if not path.is_file():
        return [f"{BASELINE}: missing"]

    raw = path.read_bytes()
    # Git may materialize text with CRLF on some platforms. The published
    # digest is for the canonical LF form; all other bytes, including blank
    # lines and the final newline, remain part of the enforced baseline.
    canonical = raw.replace(b"\r\n", b"\n")
    digest = hashlib.sha256(canonical).hexdigest()
    try:
        text = raw.decode("utf-8")
    except UnicodeDecodeError as error:
        return [f"{BASELINE}: not UTF-8 text ({error})"]
    lines = [ln for ln in text.splitlines() if ln.strip()]

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

    report_text = report.read_text()
    digests = SHA256_RE.findall(report_text)
    if len(digests) != 1:
        errors.append(
            f"{REPORT}: expected one published corrected-patch SHA-256, "
            f"found {len(digests)}"
        )
    elif digest != digests[0].lower():
        errors.append(
            f"{BASELINE}: SHA-256 is {digest}, but {REPORT} publishes "
            f"{digests[0].lower()}"
        )

    match = A1_RE.search(report_text)
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
    print(
        f"corrected-patch baseline ok: {EXPECTED_ROWS} rows, "
        "SHA-256 and A1 match report"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
