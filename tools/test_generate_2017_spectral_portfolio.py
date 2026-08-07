#!/usr/bin/env python3
"""Focused tests for the 2017 spectral portfolio renderer and receipt gate."""

from __future__ import annotations

import importlib.util
import hashlib
import json
import shutil
import tempfile
from pathlib import Path


SCRIPT = Path(__file__).with_name("generate_2017_spectral_portfolio.py")
SPEC = importlib.util.spec_from_file_location("spectral_portfolio", SCRIPT)
assert SPEC and SPEC.loader
PORTFOLIO = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PORTFOLIO)


def copy_source(source_dir: Path, target: Path) -> None:
    target.mkdir()
    for source in source_dir.iterdir():
        if source.is_file():
            shutil.copyfile(source, target / source.name)


def expect_receipt_error(source_dir: Path, target: Path, mutate, needle: str) -> None:
    copy_source(source_dir, target)
    receipt_path = target / "source_receipt.json"
    receipt = json.loads(receipt_path.read_text())
    mutate(receipt)
    receipt_path.write_text(json.dumps(receipt))
    try:
        PORTFOLIO.validate_source_receipt(target)
    except ValueError as error:
        if needle not in str(error):
            raise
    else:
        raise SystemExit(f"invalid receipt was accepted: {needle}")


def mutate_output_and_receipt(receipt: dict, target: Path) -> None:
    output = target / "hid_repeats.csv"
    output.write_bytes(output.read_bytes() + b"\n")
    changed_hash = hashlib.sha256(output.read_bytes()).hexdigest()
    for item in receipt["outputs"]:
        if item["file"] == output.name:
            item["sha256"] = changed_hash
            return
    raise SystemExit("HID output fixture is absent from receipt")


def main() -> int:
    repo_root = SCRIPT.parents[1]
    source_dir = repo_root / "data/samples/spectral_2017"
    PORTFOLIO.validate_source_receipt(source_dir)
    with tempfile.TemporaryDirectory() as raw:
        root = Path(raw)
        expect_receipt_error(
            source_dir,
            root / "bad-label",
            lambda receipt: receipt.__setitem__("archive_label", "/private/archive"),
            "archive label",
        )
        expect_receipt_error(
            source_dir,
            root / "reused-scope",
            lambda receipt: receipt.__setitem__(
                "archive_scope_id", "full_2017_coursework_tree"
            ),
            "archive scope",
        )
        expect_receipt_error(
            source_dir,
            root / "missing-output",
            lambda receipt: receipt["outputs"].pop(0),
            "output manifest",
        )
        expect_receipt_error(
            source_dir,
            root / "duplicate-source",
            lambda receipt: receipt["sources"].append(receipt["sources"][0]),
            "source manifest",
        )
        expect_receipt_error(
            source_dir,
            root / "bad-schema",
            lambda receipt: receipt.__setitem__("schema_version", 2),
            "schema version",
        )
        coordinated = root / "coordinated-output-change"
        copy_source(source_dir, coordinated)
        receipt_path = coordinated / "source_receipt.json"
        receipt = json.loads(receipt_path.read_text())
        mutate_output_and_receipt(receipt, coordinated)
        receipt_path.write_text(json.dumps(receipt))
        try:
            PORTFOLIO.validate_source_receipt(coordinated)
        except ValueError as error:
            if "output manifest" not in str(error):
                raise
        else:
            raise SystemExit("coordinated output and receipt mutation was accepted")
        legacy_change = root / "legacy-receipt-change"
        copy_source(source_dir, legacy_change)
        legacy_receipt = legacy_change / "d800_legacy_method_receipt.json"
        legacy_receipt.write_bytes(legacy_receipt.read_bytes() + b"\n")
        try:
            PORTFOLIO.validate_source_receipt(legacy_change)
        except ValueError as error:
            if "legacy method receipt" not in str(error):
                raise
        else:
            raise SystemExit("changed D800 legacy method receipt was accepted")

        legacy_manifest = json.loads(
            (source_dir / "d800_legacy_method_receipt.json").read_text()
        )
        if legacy_manifest.get("archive_scope_id") != "full_2017_coursework_tree":
            raise SystemExit("D800 receipt lacks a distinct archive scope ID")
        for group in (
            "source_files",
            "acquisition_inputs",
            "derived_artifacts",
            "retained_nef_inventory",
        ):
            if not all(item.get("archive_relative_routes")
                       for item in legacy_manifest[group]):
                raise SystemExit(
                    f"D800 receipt {group} lacks archive-relative routes"
                )
        legacy_manifest["acquisition_inputs"].pop(0)
        try:
            PORTFOLIO.validate_legacy_method_receipt(legacy_manifest)
        except ValueError as error:
            if "acquisition_inputs" not in str(error):
                raise
        else:
            raise SystemExit("incomplete D800 acquisition-input manifest was accepted")

        reused_scope = json.loads(
            (source_dir / "d800_legacy_method_receipt.json").read_text()
        )
        reused_scope["archive_scope_id"] = "spectral_yes_subset"
        try:
            PORTFOLIO.validate_legacy_method_receipt(reused_scope)
        except ValueError as error:
            if "scope" not in str(error):
                raise
        else:
            raise SystemExit("reused archive scope ID was accepted for D800")

        numeric_left = root / "numeric-left.json"
        numeric_right = root / "numeric-right.json"
        numeric_left.write_text('{"label":"same","value":1.0,"rows":[2]}')
        numeric_right.write_text(
            '{"label":"same","value":1.0000000000001,"rows":[2]}'
        )
        if not PORTFOLIO.artifact_matches(
            "result.json", numeric_left, numeric_right
        ):
            raise SystemExit("harmless JSON floating variation was rejected")
        numeric_right.write_text(
            '{"label":"same","value":1.0,"rows":[2.0]}'
        )
        if not PORTFOLIO.artifact_matches(
            "result.json", numeric_left, numeric_right
        ):
            raise SystemExit("equivalent JSON number representation was rejected")
        numeric_right.write_text(
            '{"label":"same","value":1.0,"rows":[2.0000000000000004]}'
        )
        if not PORTFOLIO.artifact_matches(
            "result.json", numeric_left, numeric_right
        ):
            raise SystemExit(
                "harmless mixed integer/float JSON variation was rejected"
            )
        numeric_right.write_text('{"label":"changed","value":1.0,"rows":[2]}')
        if PORTFOLIO.artifact_matches("result.json", numeric_left, numeric_right):
            raise SystemExit("changed JSON structure was accepted")
        numeric_right.write_text('{"label":"same","value":1.001,"rows":[2]}')
        if PORTFOLIO.artifact_matches("result.json", numeric_left, numeric_right):
            raise SystemExit("material JSON numeric change was accepted")

        csv_left = root / "numeric-left.csv"
        csv_right = root / "numeric-right.csv"
        csv_left.write_text("patch_id,value\nA1,1.000000000000\n")
        csv_right.write_text("patch_id,value\nA1,1.000000000001\n")
        if not PORTFOLIO.artifact_matches("result.csv", csv_left, csv_right):
            raise SystemExit("harmless CSV floating variation was rejected")
        csv_right.write_text("patch_id,value\nA2,1.000000000000\n")
        if PORTFOLIO.artifact_matches("result.csv", csv_left, csv_right):
            raise SystemExit("changed CSV identity was accepted")
        csv_right.write_text("patch_id,value\nA1,1.001000000000\n")
        if PORTFOLIO.artifact_matches("result.csv", csv_left, csv_right):
            raise SystemExit("material CSV numeric change was accepted")

    comparison = json.loads(
        (repo_root / "docs/data/hid_spectral_comparison.json").read_text()
    )
    reference = json.loads(
        (repo_root / "docs/data/spectral_reference_audit.json").read_text()
    )
    svg = PORTFOLIO.render_svg(
        repo_root / "docs/data/hid_spectral_comparison.csv",
        comparison,
        reference,
    )
    required = (
        "Repeated spectral measurements",
        "Retained coursework measurements",
        "530 + 540 nm",
        "40.1% after best shift",
        "Reference-axis offset -0.95 nm",
        "Observer metadata changes the answer",
        "One measurement, four serializations",
        "two SpectraShop files declare 38 fields but carry 41",
        "Candidate paired-series variation",
        "Wavelength (nm)",
        ">380<",
        ">730<",
        'role="img"',
    )
    if any(text not in svg for text in required):
        raise SystemExit("portfolio SVG is missing a required interpretation")
    print("2017 spectral portfolio generator tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
