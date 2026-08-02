#!/usr/bin/env python3
"""Negative-path checks for the corrected-patch baseline validator.

The baseline table is only a regression guard if something fails when it drifts.
These cases pin the drifts that would otherwise pass silently: a table that no
longer matches the A1 value its own report publishes, a row count that stopped
being 140, and a header line that would break byte-comparison against
`--rgb-csv-out`.
"""

from __future__ import annotations

import importlib.util
import shutil
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "check_patch_baseline", ROOT / "tools" / "check_patch_baseline.py"
)
assert SPEC and SPEC.loader
CHECK = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CHECK)

BASELINE = Path("docs/data/ccsg_f8_flat_wb_patches.csv")
REPORT = Path("docs/reports/PATCH_EXTRACTION.md")


def staged(tmp: Path) -> Path:
    """Copy the two files the validator reads into a writable tree."""
    for rel in (BASELINE, REPORT):
        (tmp / rel.parent).mkdir(parents=True, exist_ok=True)
        shutil.copy(ROOT / rel, tmp / rel)
    return tmp


def expect_error(root: Path, needle: str) -> None:
    errors = CHECK.check(root)
    if not errors:
        raise SystemExit(f"expected an error mentioning {needle!r}, got none")
    if not any(needle in e for e in errors):
        raise SystemExit(f"expected {needle!r} in {errors!r}")


def main() -> int:
    errors = CHECK.check(ROOT)
    if errors:
        raise SystemExit(f"committed baseline should validate cleanly: {errors}")

    with tempfile.TemporaryDirectory() as raw:
        root = staged(Path(raw))
        path = root / BASELINE
        rows = path.read_text().splitlines()

        # A1 drifts away from the value the report publishes.
        moved = list(rows)
        moved[0] = "7677.99,7639.68,8712.55"
        path.write_text("\n".join(moved) + "\n")
        expect_error(root, "A1")

        # A patch is dropped.
        path.write_text("\n".join(rows[:-1]) + "\n")
        expect_error(root, "140")

        # A header line appears, which would break byte-comparison with the
        # command's own `--rgb-csv-out` output.
        path.write_text("r,g,b\n" + "\n".join(rows) + "\n")
        expect_error(root, "header")

        # A non-finite value survives into the table.
        broken = list(rows)
        broken[5] = "nan,1.0,1.0"
        path.write_text("\n".join(broken) + "\n")
        expect_error(root, "finite")

    print("check_patch_baseline negative paths ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
