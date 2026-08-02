#!/usr/bin/env python3
"""Mutation checks for the cross-language result-contract gate."""

from __future__ import annotations

import argparse
import copy
import shutil
import tempfile
from pathlib import Path
from types import ModuleType


ROOT = Path(__file__).resolve().parents[1]


def load_source_module(name: str, path: Path) -> ModuleType:
    module = ModuleType(name)
    module.__file__ = str(path)
    exec(compile(path.read_text(), str(path), "exec"), module.__dict__)
    return module


CHECK = load_source_module(
    "check_schema_contract", ROOT / "tools" / "check_schema_contract.py"
)

SPECTRO_FILES = [rel for rel, _, _ in CHECK.SPECTRO_PYTHON_SOURCES]


def expect_error(errors: list[str], needle: str) -> None:
    if not errors:
        raise AssertionError(f"expected an error mentioning {needle!r}, got none")
    if not any(needle in error for error in errors):
        raise AssertionError(f"expected {needle!r} in {errors!r}")


def staged_spectro(tmp: Path) -> Path:
    for rel in SPECTRO_FILES:
        (tmp / rel.parent).mkdir(parents=True, exist_ok=True)
        shutil.copy(ROOT / rel, tmp / rel)
    return tmp


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--shading-producer", type=Path, required=True)
    parser.add_argument("--spectro-producer", type=Path, required=True)
    args = parser.parse_args()

    exporter, producer, fixture = CHECK.load_shading_contract(
        ROOT, args.shading_producer.resolve()
    )
    spectro_version = CHECK.load_spectro_producer_version(
        args.spectro_producer.resolve()
    )
    errors = CHECK.check_shading_documents(exporter, producer, fixture)
    if errors:
        raise AssertionError(f"committed shading contract should validate: {errors}")

    bumped_producer = copy.deepcopy(producer)
    bumped_producer["schema_version"] = 4
    expect_error(
        CHECK.check_shading_documents(exporter, bumped_producer, fixture),
        "live C++ shading JSON: unsupported shading schema",
    )

    bumped_exporter_version = exporter.SCHEMA_VERSION
    exporter.SCHEMA_VERSION = 4
    try:
        expect_error(
            CHECK.check_shading_documents(exporter, producer, fixture),
            "unsupported shading schema",
        )
    finally:
        exporter.SCHEMA_VERSION = bumped_exporter_version

    stale_fixture = copy.deepcopy(fixture)
    stale_fixture["schema_version"] = 2
    expect_error(
        CHECK.check_shading_documents(exporter, producer, stale_fixture),
        "canonical exporter fixture: unsupported shading schema",
    )

    incomplete_producer = copy.deepcopy(producer)
    del incomplete_producer["analysis_options"]["min_finite_coverage"]
    expect_error(
        CHECK.check_shading_documents(exporter, incomplete_producer, fixture),
        "live C++ shading JSON: incomplete or unexpected analysis options",
    )

    missing_file = copy.deepcopy(producer)
    del missing_file["file"]
    expect_error(
        CHECK.check_shading_documents(exporter, missing_file, fixture),
        "live C++ shading JSON: missing file label",
    )

    mismatched_grid = copy.deepcopy(producer)
    mismatched_grid["grid"]["cols"] = 15
    mismatched_grid["relative_response"] = [
        values[: 15 * 12] for values in mismatched_grid["relative_response"]
    ]
    for key in ("c_rg", "c_bg", "c_g1g2"):
        mismatched_grid[key] = mismatched_grid[key][: 15 * 12]
    expect_error(
        CHECK.check_shading_documents(exporter, mismatched_grid, fixture),
        "live C++ shading JSON: grid 15x12 does not match analysis_options 16x12",
    )

    expanded_fixture = copy.deepcopy(fixture)
    expanded_fixture["analysis_options"]["unpublished_option"] = 1
    expect_error(
        CHECK.check_shading_documents(exporter, producer, expanded_fixture),
        "canonical exporter fixture: incomplete or unexpected analysis options",
    )

    with tempfile.TemporaryDirectory() as raw:
        root = staged_spectro(Path(raw))
        spectro_errors = CHECK.check_spectro(root, spectro_version)
        if spectro_errors:
            raise AssertionError(
                f"committed spectro contract should validate: {spectro_errors}"
            )

        generator_path = root / Path("tools/generate_spectro_receipt.py")
        generator = generator_path.read_text()
        generator_path.write_text(
            generator.replace("RESULT_SCHEMA_VERSION = 2", "RESULT_SCHEMA_VERSION = 3", 1)
        )
        expect_error(
            CHECK.check_spectro(root, spectro_version),
            "generate_spectro_receipt.py",
        )
        generator_path.write_text(generator)

        generator_path.write_text(
            generator.replace(
                "RESULT_SCHEMA_VERSION = 2",
                '"""\nRESULT_SCHEMA_VERSION = 2\n"""',
                1,
            )
        )
        expect_error(
            CHECK.check_spectro(root, spectro_version),
            "generate_spectro_receipt.py",
        )
        generator_path.write_text(generator)

        generator_path.write_text(
            generator.replace(
                'result.get("schema_version") != RESULT_SCHEMA_VERSION',
                'result.get("schema_version") != 3',
                1,
            )
        )
        expect_error(
            CHECK.check_spectro(root, spectro_version),
            "does not control the production generator schema check",
        )
        generator_path.write_text(generator)

        checker_path = root / Path("tools/check_spectro_receipt.py")
        checker_source = checker_path.read_text()
        checker_path.write_text(
            checker_source.replace(
                'receipt.get("result_schema_version") != RESULT_SCHEMA_VERSION',
                'receipt.get("result_schema_version") != 3',
                1,
            )
        )
        expect_error(
            CHECK.check_spectro(root, spectro_version),
            "does not control the production checker schema check",
        )
        checker_path.write_text(checker_source)

        expect_error(
            CHECK.check_spectro(root, spectro_version + 1),
            "compiled C++ spectro schema",
        )

    print("check_schema_contract mutations ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
