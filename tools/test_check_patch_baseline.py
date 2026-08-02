#!/usr/bin/env python3
"""Negative-path checks for the corrected-patch baseline validator.

The baseline table is only a regression guard if something fails when it drifts.
These cases pin the drifts that would otherwise pass silently: a table that no
longer matches its published full-file digest, an A1 value that diverges from
its report, a row count that stopped being 140, and byte-format changes that
would break comparison against `--rgb-csv-out`.
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
        report = root / REPORT
        rows = path.read_text().splitlines()
        report_text = report.read_text()

        # A Git checkout may materialize this text file with CRLF. That
        # platform-only conversion must not invalidate the canonical LF digest.
        lf = path.read_bytes().replace(b"\r\n", b"\n")
        path.write_bytes(lf.replace(b"\n", b"\r\n"))
        errors = CHECK.check(root)
        if errors:
            raise SystemExit(f"CRLF baseline should validate cleanly: {errors}")
        path.write_text("\n".join(rows) + "\n")

        # A valid non-A1 value moves. Shape and A1 alone cannot pin the other
        # 417 published channel values; the full-file digest must catch this.
        moved = list(rows)
        moved[1] = "1675.1507632526844,1658.6290152841639,1908.2708721847655"
        path.write_text("\n".join(moved) + "\n")
        expect_error(root, "SHA-256")

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

        # Blank lines and the final newline are part of the byte baseline even
        # though numeric parsing alone would discard both differences.
        path.write_text(rows[0] + "\n\n" + "\n".join(rows[1:]) + "\n")
        expect_error(root, "SHA-256")
        path.write_text("\n".join(rows))
        expect_error(root, "SHA-256")

        # A non-finite value survives into the table.
        broken = list(rows)
        broken[5] = "nan,1.0,1.0"
        path.write_text("\n".join(broken) + "\n")
        expect_error(root, "finite")

        # The report is the public digest authority; stale prose must fail too.
        path.write_text("\n".join(rows) + "\n")
        report.write_text(
            report_text.replace(
                "4b8429cdacbb982d33ef56a76e09cc46d8c7aadde927e805e52bc5feec2c8f92",
                "0" * 64,
            )
        )
        expect_error(root, "SHA-256")

    print("check_patch_baseline negative paths ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
