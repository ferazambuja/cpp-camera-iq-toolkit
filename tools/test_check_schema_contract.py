#!/usr/bin/env python3
"""Negative-path checks for the producer/exporter schema pin.

The check only earns its place if it fails when the two sides drift, so these
cases move each side in turn and require an error naming both values.
"""

from __future__ import annotations

import importlib.util
import re
import shutil
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "check_schema_contract", ROOT / "tools" / "check_schema_contract.py"
)
assert SPEC and SPEC.loader
CHECK = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CHECK)

PRODUCER = Path("src/cmd_shading.cpp")
EXPORTER = Path("tools/export_shading_portfolio.py")
FIXTURE = Path("tools/test_export_shading_portfolio.py")


def staged(tmp: Path) -> Path:
    for rel in (PRODUCER, EXPORTER, FIXTURE):
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
        raise SystemExit(f"committed tree should validate cleanly: {errors}")

    with tempfile.TemporaryDirectory() as raw:
        root = staged(Path(raw))

        # The producer is bumped and the Python side is not. This is the exact
        # sequence that broke the documented export pipeline between #16 and
        # #19: every real result file was rejected while the suite stayed green,
        # because the exporter's fixture still declared the old version.
        original = (root / PRODUCER).read_text()
        (root / PRODUCER).write_text(
            original.replace(
                "constexpr int kShadingSchemaVersion = 3;",
                "constexpr int kShadingSchemaVersion = 4;",
            )
        )
        expect_error(root, "cmd_shading.cpp")
        (root / PRODUCER).write_text(original)

        # The exporter is bumped and the producer is not.
        exporter = (root / EXPORTER).read_text()
        (root / EXPORTER).write_text(
            re.sub(r"SCHEMA_VERSION = 3", "SCHEMA_VERSION = 4", exporter)
        )
        expect_error(root, "export_shading_portfolio.py")
        (root / EXPORTER).write_text(exporter)

        # The exporter's own fixture drifts. A fixture that encodes the old
        # contract is what let the suite stay green through the regression.
        fixture = (root / FIXTURE).read_text()
        (root / FIXTURE).write_text(
            fixture.replace('"schema_version": 3', '"schema_version": 2', 1)
        )
        expect_error(root, "test_export_shading_portfolio.py")
        (root / FIXTURE).write_text(fixture)

        # A missing declaration must fail loudly rather than silently pass.
        (root / EXPORTER).write_text(
            exporter.replace("SCHEMA_VERSION = 3", "# removed")
        )
        expect_error(root, "no SCHEMA_VERSION")

    print("check_schema_contract negative paths ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
