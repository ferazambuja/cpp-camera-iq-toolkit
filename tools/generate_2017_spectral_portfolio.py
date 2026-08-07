#!/usr/bin/env python3
"""Generate and verify the retained-2017 spectral portfolio artifacts."""

from __future__ import annotations

import argparse
import csv
import hashlib
import html
import json
import math
import shutil
import subprocess
import tempfile
from pathlib import Path


EXPECTED_SOURCE_HASHES = {
    "PR655_HID.txt": "9fc63adc1f2a4161b78077590d74aadf5c8eda61dca9c0f706ca15754ae22271",
    "i1Pro_HID.txt": "2ad8fb2bbdad78b3e8401eb64d909fdcc26459638892b56d1493b732d3226ebc",
    "CC4.txt": "21649841724d6cdac863bfe20f6702169d4a8998436446579a46eb8e74b17ad5",
    "CC_Measurement_1.txt": "40883066432c86d720ff65c5ffa02a5c98d5e3af7f5805d44e96f2ff37564b04",
    "CC4_CGATS.txt": "4bae51849b714a403f1b788c50dac3444a4705ab62589e760f71b3d96305c00c",
    "CC4_4.txt": "a20ea31e925f63c1f875ac1216cce0d76d095a6e5aa8083addd222091a77001f",
    "CC4_CGATS_M0.txt": "8a5450b737202606936a726a9e795672119fb7cc678c2ab8a9f1ebc3bd611bbb",
    "CC4_4_M0.txt": "ffc4a35022458a5fef2a930d825bec33d040424773d98c6f0ba261aa33417488",
}

EXPECTED_OUTPUT_HASHES = {
    "hid_repeats.csv": "5127d06aff3be9cf67f81717a41f9ba23c73bbf9bf59a8f314d2b1cb0dfc62a7",
    "colorchecker_measurement_01.csv": "201a41dcca6ea5d6c371f02e956d595ade6c17638a73a6ff4dbf1f35ac55fc1a",
    "colorchecker_measurement_02.csv": "3a3e1a369b75a0f72c6e7c19453f814a73a570293cac19136e0e0c2fbded7d0b",
    "CC4_CGATS.txt": "4bae51849b714a403f1b788c50dac3444a4705ab62589e760f71b3d96305c00c",
    "CC4_4.txt": "a20ea31e925f63c1f875ac1216cce0d76d095a6e5aa8083addd222091a77001f",
    "CC4_CGATS_M0.txt": "8a5450b737202606936a726a9e795672119fb7cc678c2ab8a9f1ebc3bd611bbb",
    "CC4_4_M0.txt": "ffc4a35022458a5fef2a930d825bec33d040424773d98c6f0ba261aa33417488",
    "archive_summary.json": "466bf0c9b71b4247d94ee710dfebc648ff02cf3df80c42c8e719c952ce050f4d",
}

EXPECTED_LEGACY_METHOD_RECEIPT_HASH = (
    "a213a9602f758760b03098e4d114aba322bec7a6e55c0f70b9645aa244c0b378"
)

EXPECTED_LEGACY_SOURCE_FILES = [
    {
        "file": "spectral_v2_1.py",
        "archive_relative_routes": ["Bobby's Programs/folders/spectral_v2_1.py"],
        "bytes": 10027,
        "sha256": "7efbcda44ce9c36ad0328649f85a6520daa3cbfcaa75d85362bb829970538317",
        "line_count": 317,
    },
    {
        "file": "raw2tiff.py",
        "archive_relative_routes": [
            "Bobby's Programs/folders/raw2tiff.py",
            "Bobby's Programs/raw2tiff.py",
        ],
        "bytes": 2674,
        "sha256": "072849dfae68d45de853ac9c596bdfbaac2e4f4f32683282278270f05ec45e18",
        "line_count": 84,
    },
]
EXPECTED_LEGACY_ACQUISITION_INPUTS = [
    {
        "file": "spd.csv",
        "archive_relative_routes": [
            "Bobby's Programs/folders/spd.csv",
            "Bobby's Programs/spd.csv",
        ],
        "bytes": 787,
        "sha256": "f36fe548364f98c079c364a6c9a300be2f78f7f970208b92133b649ea23f0fcf",
        "spectral_row_count": 35,
    },
    {
        "file": "SPD.xlsx",
        "archive_relative_routes": ["Bobby's Programs/folders/SPD.xlsx"],
        "bytes": 10288,
        "sha256": "d5f44c1996245d1ef3e60f993865448673a954f110fd1762b09d417a4a9fad73",
    },
]
EXPECTED_LEGACY_DERIVED_ARTIFACTS = [
    {
        "file": "Nikon D800_Spectral_Sensitivity_Data.csv",
        "archive_relative_routes": [
            "Bobby's Programs/folders/Nikon D800_Spectral_Sensitivity_Data.csv"
        ],
        "bytes": 2311,
        "sha256": "b5b0102a03617b51e049368e1b12daf7568559245722202536e799a450dfa5a8",
        "spectral_row_count": 35,
    },
    {
        "file": "SpectralResponseGraph.pdf",
        "archive_relative_routes": [
            "Bobby's Programs/folders/SpectralResponseGraph.pdf"
        ],
        "bytes": 14200,
        "sha256": "480d1f451ca96f21fc29ffbf730faea2435f737924b679f82b578c92501ce71a",
    },
]
EXPECTED_LEGACY_NEF_INVENTORY = [
    {
        "file": "red.NEF",
        "archive_relative_routes": ["Bobby's Programs/red.NEF"],
        "bytes": 31367485,
        "sha256": "18a6b559a23e796c1a68b209ff7f938ed18084037a90bf9f840b6c581a8dadf7",
    },
    {
        "file": "darkframe.NEF",
        "archive_relative_routes": ["Bobby's Programs/folders/darkframe.NEF"],
        "bytes": 31043684,
        "sha256": "a8ef0496dc3e9d115e6dd1b57a3565f9bdefc9b5a05d5078bd31cd89dc5f2166",
    },
]

NUMERIC_REL_TOLERANCE = 5e-12
NUMERIC_ABS_TOLERANCE = 5e-12


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def _json_values_match(left: object, right: object) -> bool:
    if isinstance(left, bool) or isinstance(right, bool):
        return type(left) is type(right) and left == right
    if isinstance(left, (int, float)) or isinstance(right, (int, float)):
        if not isinstance(left, (int, float)) or not isinstance(
            right, (int, float)
        ):
            return False
        if isinstance(left, int) and isinstance(right, int):
            return left == right
        return (
            math.isfinite(left)
            and math.isfinite(right)
            and math.isclose(
                left,
                right,
                rel_tol=NUMERIC_REL_TOLERANCE,
                abs_tol=NUMERIC_ABS_TOLERANCE,
            )
        )
    if isinstance(left, dict) or isinstance(right, dict):
        return (
            isinstance(left, dict)
            and isinstance(right, dict)
            and left.keys() == right.keys()
            and all(_json_values_match(left[key], right[key]) for key in left)
        )
    if isinstance(left, list) or isinstance(right, list):
        return (
            isinstance(left, list)
            and isinstance(right, list)
            and len(left) == len(right)
            and all(_json_values_match(a, b) for a, b in zip(left, right))
        )
    return type(left) is type(right) and left == right


def _csv_cells_match(left: str, right: str) -> bool:
    try:
        left_number = float(left)
    except ValueError:
        left_number = None
    try:
        right_number = float(right)
    except ValueError:
        right_number = None
    if left_number is None or right_number is None:
        return left_number is None and right_number is None and left == right
    return (
        math.isfinite(left_number)
        and math.isfinite(right_number)
        and math.isclose(
            left_number,
            right_number,
            rel_tol=NUMERIC_REL_TOLERANCE,
            abs_tol=NUMERIC_ABS_TOLERANCE,
        )
    )


def artifact_matches(name: str, generated: Path, committed: Path) -> bool:
    if not generated.is_file() or not committed.is_file():
        return False
    try:
        if name.endswith(".json"):
            return _json_values_match(
                json.loads(generated.read_text()),
                json.loads(committed.read_text()),
            )
        if name.endswith(".csv"):
            with generated.open(newline="", encoding="utf-8") as left_handle:
                left_rows = list(csv.reader(left_handle))
            with committed.open(newline="", encoding="utf-8") as right_handle:
                right_rows = list(csv.reader(right_handle))
            return (
                len(left_rows) == len(right_rows)
                and all(len(left) == len(right)
                        for left, right in zip(left_rows, right_rows))
                and all(
                    _csv_cells_match(left_cell, right_cell)
                    for left, right in zip(left_rows, right_rows)
                    for left_cell, right_cell in zip(left, right)
                )
            )
    except (OSError, UnicodeError, json.JSONDecodeError, csv.Error):
        return False
    return generated.read_bytes() == committed.read_bytes()


def _json_difference(left: object, right: object, path: str = "$") -> str:
    if isinstance(left, (int, float)) and not isinstance(left, bool) and \
            isinstance(right, (int, float)) and not isinstance(right, bool):
        if _json_values_match(left, right):
            return ""
        return f"{path}: numeric values {left!r} != {right!r}"
    if type(left) is not type(right):
        return (
            f"{path}: types {type(left).__name__} {left!r} != "
            f"{type(right).__name__} {right!r}"
        )
    if isinstance(left, dict):
        if left.keys() != right.keys():
            return f"{path}: object keys differ"
        for key in left:
            difference = _json_difference(left[key], right[key], f"{path}.{key}")
            if difference:
                return difference
        return ""
    if isinstance(left, list):
        if len(left) != len(right):
            return f"{path}: list lengths {len(left)} != {len(right)}"
        for index, (left_item, right_item) in enumerate(zip(left, right)):
            difference = _json_difference(
                left_item, right_item, f"{path}[{index}]"
            )
            if difference:
                return difference
        return ""
    if isinstance(left, float):
        if _json_values_match(left, right):
            return ""
        return f"{path}: {left!r} != {right!r}, abs={abs(left-right):.17g}"
    return "" if left == right else f"{path}: {left!r} != {right!r}"


def artifact_difference(name: str, generated: Path, committed: Path) -> str:
    try:
        if name.endswith(".json"):
            return _json_difference(
                json.loads(generated.read_text()),
                json.loads(committed.read_text()),
            )
        if name.endswith(".csv"):
            with generated.open(newline="", encoding="utf-8") as left_handle:
                left_rows = list(csv.reader(left_handle))
            with committed.open(newline="", encoding="utf-8") as right_handle:
                right_rows = list(csv.reader(right_handle))
            if len(left_rows) != len(right_rows):
                return f"row counts {len(left_rows)} != {len(right_rows)}"
            for row_index, (left, right) in enumerate(zip(left_rows, right_rows)):
                if len(left) != len(right):
                    return f"row {row_index}: column counts differ"
                for column_index, (left_cell, right_cell) in enumerate(
                    zip(left, right)
                ):
                    if not _csv_cells_match(left_cell, right_cell):
                        return (
                            f"row {row_index}, column {column_index}: "
                            f"{left_cell!r} != {right_cell!r}"
                        )
            return "no semantic difference found"
    except (OSError, UnicodeError, json.JSONDecodeError, csv.Error) as error:
        return str(error)
    return "byte content differs"


def validate_legacy_method_receipt(receipt: dict) -> None:
    expected_scalars = {
        "schema_version": 1,
        "archive_label": "retained_2017_coursework_archive",
        "archive_scope_id": "full_2017_coursework_tree",
        "audit_scope": "code_level_method_audit_not_empirical_reprocessing",
        "method_evidence_scope": (
            "selected_method_sources_inputs_and_outputs_not_complete_directory_inventory"
        ),
        "retained_nef_inventory_scope": (
            "all_nef_files_case_insensitively_and_recursively_below_the_full_2017_coursework_tree_scope"
        ),
        "source_availability": (
            "private_not_redistributed_no_redistribution_license_identified"
        ),
    }
    if any(receipt.get(key) != value for key, value in expected_scalars.items()):
        raise ValueError("D800 legacy method receipt scope is incomplete")
    expected_manifests = {
        "source_files": EXPECTED_LEGACY_SOURCE_FILES,
        "acquisition_inputs": EXPECTED_LEGACY_ACQUISITION_INPUTS,
        "derived_artifacts": EXPECTED_LEGACY_DERIVED_ARTIFACTS,
        "retained_nef_inventory": EXPECTED_LEGACY_NEF_INVENTORY,
    }
    for key, expected in expected_manifests.items():
        if receipt.get(key) != expected:
            raise ValueError(f"D800 legacy method receipt {key} is incomplete")


def validate_source_receipt(source_dir: Path) -> None:
    receipt = json.loads((source_dir / "source_receipt.json").read_text())
    if receipt.get("schema_version") != 1:
        raise ValueError("source receipt schema version is not supported")
    if receipt.get("archive_label") != "retained_2017_coursework_archive":
        raise ValueError("source receipt archive label is not the expected path-free identifier")
    if receipt.get("archive_scope_id") != "spectral_yes_subset":
        raise ValueError("source receipt archive scope is not the expected subset")
    source_entries = receipt.get("sources")
    if not isinstance(source_entries, list) or not all(
        isinstance(item, dict) for item in source_entries
    ):
        raise ValueError("source receipt source manifest is invalid")
    if not all(
        isinstance(item.get("file"), str)
        and isinstance(item.get("sha256"), str)
        for item in source_entries
    ):
        raise ValueError("source receipt source manifest is invalid")
    sources = {item.get("file"): item.get("sha256") for item in source_entries}
    if len(source_entries) != len(sources):
        raise ValueError("source receipt source manifest contains duplicate entries")
    if sources != EXPECTED_SOURCE_HASHES:
        raise ValueError("source receipt does not match the retained source hashes")
    for name in sources:
        if not isinstance(name, str) or "/" in name or "\\" in name:
            raise ValueError("source receipt contains a path instead of a file name")

    output_entries = receipt.get("outputs")
    if not isinstance(output_entries, list) or not all(
        isinstance(item, dict) for item in output_entries
    ):
        raise ValueError("source receipt output manifest is invalid")
    if not all(
        isinstance(item.get("file"), str)
        and isinstance(item.get("sha256"), str)
        for item in output_entries
    ):
        raise ValueError("source receipt output manifest is invalid")
    outputs = {item["file"]: item["sha256"] for item in output_entries}
    if len(output_entries) != len(outputs) or outputs != EXPECTED_OUTPUT_HASHES:
        raise ValueError("source receipt output manifest is incomplete or duplicated")
    for item in output_entries:
        name = item.get("file")
        if not isinstance(name, str) or "/" in name or "\\" in name:
            raise ValueError("source receipt output contains a path")
        path = source_dir / name
        if not path.is_file() or sha256(path) != item.get("sha256"):
            raise ValueError(f"normalized source hash mismatch: {name}")
    for name in ("CC4_CGATS.txt", "CC4_4.txt", "CC4_CGATS_M0.txt", "CC4_4_M0.txt"):
        if outputs[name] != EXPECTED_SOURCE_HASHES[name]:
            raise ValueError(f"copied CGATS output is not source-identical: {name}")
    legacy_receipt = source_dir / "d800_legacy_method_receipt.json"
    if not legacy_receipt.is_file():
        raise ValueError("D800 legacy method receipt is missing")
    validate_legacy_method_receipt(json.loads(legacy_receipt.read_text()))
    if sha256(legacy_receipt) != EXPECTED_LEGACY_METHOD_RECEIPT_HASH:
        raise ValueError("D800 legacy method receipt hash mismatch")


def _read_bands(path: Path) -> list[dict[str, float]]:
    with path.open(newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle)
        expected = [
            "wavelength_nm",
            "reference_normalized",
            "candidate_normalized",
            "signed_residual",
            "squared_residual_fraction",
        ]
        if reader.fieldnames != expected:
            raise ValueError("unexpected HID comparison CSV schema")
        result = []
        for row in reader:
            if None in row:
                raise ValueError("unexpected HID comparison CSV row width")
            item = {key: float(row[key]) for key in expected}
            if not all(math.isfinite(value) for value in item.values()):
                raise ValueError("non-finite HID comparison value")
            result.append(item)
    if [row["wavelength_nm"] for row in result] != list(range(380, 731, 10)):
        raise ValueError("HID comparison grid differs")
    return result


def _line(points: list[tuple[float, float]], css_class: str) -> str:
    encoded = " ".join(f"{x:.2f},{y:.2f}" for x, y in points)
    return f'<polyline class="{css_class}" points="{encoded}"/>'


def render_svg(comparison_csv: Path, comparison: dict,
               reference: dict) -> str:
    bands = _read_bands(comparison_csv)
    if comparison.get("normalization") != "common_grid_equal_weight_integral":
        raise ValueError("HID comparison normalization differs")
    if comparison.get("relative_l2_denominator") != "reference_l2_norm":
        raise ValueError("HID comparison denominator differs")
    if comparison.get("schema_version") != 2:
        raise ValueError("HID comparison schema differs")
    if comparison.get("offset_objective_scope") != (
        "per_offset_equal_weight_integral_normalization_on_fixed_common_grid"
    ):
        raise ValueError("HID offset objective scope differs")
    if comparison.get("reference_group", {}).get("reading_count") != 8 or \
       comparison.get("candidate_group", {}).get("reading_count") != 8:
        raise ValueError("HID repeat counts differ")
    residual = float(comparison["directional_relative_l2"])
    excluded = float(comparison["diagnostic_exclusions"][0]["directional_relative_l2"])
    sweep_sample_count = int(comparison["offset_common_grid_sample_count"])
    zero_offset_objective = comparison["zero_offset_objective"]
    zero_offset_residual = float(
        zero_offset_objective["directional_relative_l2"]
    )
    zero_offset_rows = [
        row for row in comparison.get("offset_sensitivity", [])
        if float(row["wavelength_offset_nm"]) == 0.0
    ]
    offset = float(comparison["best_wavelength_offset_nm"])
    offset_residual = float(comparison["best_offset_directional_relative_l2"])
    best_offset_rows = [
        row for row in comparison.get("offset_sensitivity", [])
        if float(row["wavelength_offset_nm"]) == offset
    ]
    if len(best_offset_rows) != 1:
        raise ValueError("HID comparison best offset row differs")
    best_offset_objective = best_offset_rows[0]
    offset_relative_l2_reduction = 1.0 - offset_residual / zero_offset_residual
    squared_objective_ratio = (offset_residual / zero_offset_residual) ** 2
    squared_residual_ratio = (
        float(best_offset_objective["residual_l2_norm"])
        / float(zero_offset_objective["residual_l2_norm"])
    ) ** 2
    contribution = sum(
        row["squared_residual_fraction"]
        for row in bands if row["wavelength_nm"] in (530.0, 540.0)
    )
    shifted_contribution = sum(
        float(row["squared_residual_fraction"])
        for row in comparison.get("best_offset_bands", [])
        if float(row["wavelength_nm"]) in (530.0, 540.0)
    )
    if not (math.isclose(residual, 0.0432733790086, rel_tol=0, abs_tol=1e-12)
            and math.isclose(excluded, 0.0227615491516, rel_tol=0, abs_tol=1e-12)
            and sweep_sample_count == 35
            and len(zero_offset_rows) == 1
            and math.isclose(zero_offset_residual, 0.0432741600137,
                             rel_tol=0, abs_tol=1e-12)
            and math.isclose(
                float(zero_offset_rows[0]["directional_relative_l2"]),
                zero_offset_residual, rel_tol=0, abs_tol=1e-15)
            and math.isclose(offset, -0.95, rel_tol=0, abs_tol=1e-12)
            and math.isclose(offset_residual, 0.0308414328745,
                             rel_tol=0, abs_tol=1e-12)
            and math.isclose(
                float(best_offset_objective["directional_relative_l2"]),
                offset_residual, rel_tol=0, abs_tol=1e-15)
            and math.isclose(
                float(zero_offset_objective["residual_l2_norm"]),
                0.000807579465693, rel_tol=0, abs_tol=1e-15)
            and math.isclose(
                float(zero_offset_objective["reference_l2_norm"]),
                0.0186619327894, rel_tol=0, abs_tol=1e-13)
            and math.isclose(
                float(best_offset_objective["residual_l2_norm"]),
                0.000574647192582, rel_tol=0, abs_tol=1e-15)
            and math.isclose(
                float(best_offset_objective["reference_l2_norm"]),
                0.0186323117645, rel_tol=0, abs_tol=1e-13)
            and math.isclose(squared_objective_ratio, 0.507939281814,
                             rel_tol=0, abs_tol=1e-12)
            and math.isclose(squared_residual_ratio, 0.506328115199,
                             rel_tol=0, abs_tol=1e-12)
            and abs(squared_objective_ratio - squared_residual_ratio) > 1e-3
            and math.isclose(offset_relative_l2_reduction,
                             0.287301408860, rel_tol=0, abs_tol=1e-12)
            and math.isclose(shifted_contribution, 0.400816293758,
                             rel_tol=0, abs_tol=1e-12)):
        raise ValueError("HID comparison headline metrics differ")

    matching = reference["spectrashop_d65_10_degree"]
    alternative = reference["spectrashop_d65_2_degree_alternative"]
    repeat = reference["candidate_repeat"]
    if reference.get("cgats_evidence_scope") != "one_measurement_reserialized" \
       or reference.get("all_four_exports_exact_spectral_content") is not True \
       or reference.get("stable_id_pairing_across_exports") is not True \
       or reference.get("layout_labels_differ") is not True:
        raise ValueError("CGATS evidence scope differs")
    schemas = {
        item.get("export_id"): item
        for item in reference.get("cgats_exports", [])
        if isinstance(item, dict)
    }
    expected_schema = {
        "spectrashop_primary": (38, 41, False),
        "spectrashop_alternate_layout": (38, 41, False),
        "babelcolor_xyz": (41, 41, True),
        "babelcolor_layout": (38, 38, True),
    }
    if {
        key: (
            schemas.get(key, {}).get("declared_field_count"),
            schemas.get(key, {}).get("actual_field_count"),
            schemas.get(key, {}).get("field_count_matches"),
        )
        for key in expected_schema
    } != expected_schema:
        raise ValueError("per-export CGATS schema evidence differs")

    x0, y0, width, height = 74.0, 158.0, 650.0, 300.0
    maximum = max(
        max(row["reference_normalized"], row["candidate_normalized"])
        for row in bands
    )
    x = lambda wavelength: x0 + width * (wavelength - 380.0) / 350.0
    y = lambda value: y0 + height * (1.0 - value / maximum)
    reference_points = [(x(row["wavelength_nm"]), y(row["reference_normalized"]))
                        for row in bands]
    candidate_points = [(x(row["wavelength_nm"]), y(row["candidate_normalized"]))
                        for row in bands]

    bars = []
    bar_base = 685.0
    bar_height = 80.0
    bar_width = width / len(bands) - 2.0
    maximum_contribution = max(row["squared_residual_fraction"] for row in bands)
    for index, row in enumerate(bands):
        height_px = bar_height * row["squared_residual_fraction"] / maximum_contribution
        css = "band-hot" if row["wavelength_nm"] in (530.0, 540.0) else "band"
        bars.append(
            f'<rect class="{css}" x="{x0 + index * width / len(bands):.2f}" '
            f'y="{bar_base - height_px:.2f}" width="{bar_width:.2f}" '
            f'height="{height_px:.2f}"/>'
        )

    wavelength_ticks = []
    for wavelength in (380, 450, 520, 590, 660, 730):
        tick_x = x(float(wavelength))
        wavelength_ticks.append(
            f'<line class="grid" x1="{tick_x:.2f}" y1="{y0}" '
            f'x2="{tick_x:.2f}" y2="{y0 + height}"/>'
            f'<text class="small" x="{tick_x:.2f}" y="476" '
            f'text-anchor="middle">{wavelength}</text>'
        )

    svg = f'''<svg xmlns="http://www.w3.org/2000/svg" width="1200" height="820" viewBox="0 0 1200 820" role="img" aria-labelledby="spectral-title spectral-desc">
<title id="spectral-title">Repeated spectral measurements and ColorChecker interchange audit</title>
<desc id="spectral-desc">Two repeated spectral series are compared on a common grid. Separate panels show the observer metadata discrepancy, one measurement serialized four ways, and candidate paired-series chart variation.</desc>
<style>
  .bg{{fill:#f7f4ee}} .panel{{fill:#fff;stroke:#d7d0c4;stroke-width:1.5}}
  .title{{font:700 28px system-ui;fill:#17212b}} .subtitle{{font:15px system-ui;fill:#52606d}}
  .head{{font:700 18px system-ui;fill:#17212b}} .body{{font:14px system-ui;fill:#34414d}}
  .small{{font:12px system-ui;fill:#66717c}} .metric{{font:700 29px system-ui;fill:#0b6e75}}
  .axis{{stroke:#9ca7b0;stroke-width:1}} .grid{{stroke:#e6e1d8;stroke-width:1}}
  .ref{{fill:none;stroke:#d05b32;stroke-width:3}} .cand{{fill:none;stroke:#167c80;stroke-width:3}}
  .band{{fill:#9cc4c5}} .band-hot{{fill:#d05b32}}
</style>
<rect class="bg" width="1200" height="820"/>
<text class="title" x="52" y="54">Spectral cross-check: repetition, grids, and metadata</text>
<text class="subtitle" x="52" y="82">Retained coursework measurements, re-evaluated with explicit wavelength grids and observer models</text>
<rect class="panel" x="42" y="108" width="720" height="660" rx="14"/>
<text class="head" x="74" y="140">Repeated spectral measurements</text>
{''.join(wavelength_ticks)}
<line class="axis" x1="{x0}" y1="{y0 + height}" x2="{x0 + width}" y2="{y0 + height}"/>
<line class="axis" x1="{x0}" y1="{y0}" x2="{x0}" y2="{y0 + height}"/>
{_line(reference_points, "ref")}
{_line(candidate_points, "cand")}
<text class="small" x="{x0 + width / 2}" y="497" text-anchor="middle">Wavelength (nm)</text>
<line class="ref" x1="92" y1="516" x2="124" y2="516"/><text class="body" x="132" y="521">PR-655 mean (8 readings; 4 nm native)</text>
<line class="cand" x1="405" y1="516" x2="437" y2="516"/><text class="body" x="445" y="521">i1Pro mean (8 readings; 10 nm native)</text>
<text class="small" x="{x0}" y="545">Common-grid normalized spectral density (scaled to shared plot maximum)</text>
<text class="head" x="74" y="580">Where the squared residual is concentrated</text>
<line class="axis" x1="{x0}" y1="{bar_base}" x2="{x0 + width}" y2="{bar_base}"/>
{''.join(bars)}
<text class="metric" x="74" y="716">{100*residual:.2f}%</text>
<text class="body" x="190" y="712">directional relative-L2 difference</text>
<text class="body" x="74" y="741">530 + 540 nm: {100*contribution:.1f}% of 36-band original / {100*shifted_contribution:.1f}% of 35-band post-shift squared residual.</text>
<text class="small" x="74" y="760">35-band zero-offset baseline {100*zero_offset_residual:.2f}% → best fitted shift {offset:+.2f} nm: {100*offset_residual:.2f}% ({100*offset_relative_l2_reduction:.1f}% lower); no cause identified.</text>

<rect class="panel" x="786" y="108" width="372" height="200" rx="14"/>
<text class="head" x="812" y="142">Observer metadata changes the answer</text>
<text class="metric" x="812" y="190">{matching['mean_delta_e_76']:.3f} ΔE76</text>
<text class="body" x="812" y="214">D65 / CIE 1964 10° vs embedded Lab</text>
<text class="metric" x="812" y="258">{alternative['mean_delta_e_76']:.2f} ΔE76</text>
<text class="body" x="812" y="282">D65 / CIE 1931 2° alternative</text>

<rect class="panel" x="786" y="330" width="372" height="188" rx="14"/>
<text class="head" x="812" y="365">One measurement, four serializations</text>
<text class="metric" x="812" y="414">24 / 24</text>
<text class="body" x="812" y="438">spectra match exactly by stable SAMPLE_ID</text>
<text class="body" x="812" y="468">Layout labels differ; two SpectraShop files declare 38 fields but carry 41.</text>
<text class="small" x="812" y="493">Interoperability evidence, not independent measurement agreement</text>

<rect class="panel" x="786" y="540" width="372" height="228" rx="14"/>
<text class="head" x="812" y="575">Candidate paired-series variation</text>
<text class="metric" x="812" y="624">{repeat['mean_delta_e_76']:.2f} ΔE76</text>
<text class="body" x="812" y="649">mean across 24 paired chart rows</text>
<text class="metric" x="812" y="695">{repeat['max_delta_e_76']:.2f} ΔE76 max</text>
<text class="body" x="812" y="720">mean reflectance RMS {repeat['mean_reflectance_rms']:.4f}</text>
<text class="small" x="812" y="746">Observed variation only; acquisition metadata is incomplete</text>
</svg>
'''
    return svg


def _run(command: list[str]) -> None:
    result = subprocess.run(command, text=True, capture_output=True)
    if result.returncode != 0:
        raise SystemExit(result.stderr or result.stdout)


def generate(repo_root: Path, camera_iq: Path, output: Path) -> None:
    samples = repo_root / "data/samples/spectral_2017"
    validate_source_receipt(samples)
    output.mkdir(parents=True, exist_ok=True)
    _run([
        str(camera_iq), "spectro-compare", str(samples / "hid_repeats.csv"),
        "--reference", "pr655", "--candidate", "i1pro",
        "--common-start", "380", "--common-end", "730", "--common-step", "10",
        "--exclude", "530,540", "--offset-min", "-2", "--offset-max", "2",
        "--offset-step", "0.05", "--offset-series", "reference",
        "--out-json", str(output / "hid_spectral_comparison.json"),
        "--out-csv", str(output / "hid_spectral_comparison.csv"),
    ])
    _run([
        str(camera_iq), "spectral-reference-audit",
        "--spectrashop", str(samples / "CC4_CGATS.txt"),
        "--alternate-spectrashop", str(samples / "CC4_4.txt"),
        "--babelcolor", str(samples / "CC4_CGATS_M0.txt"),
        "--layout-export", str(samples / "CC4_4_M0.txt"),
        "--repeat-first", str(samples / "colorchecker_measurement_01.csv"),
        "--repeat-second", str(samples / "colorchecker_measurement_02.csv"),
        "--d65", str(repo_root / "data/cie_d65.csv"),
        "--observer-10", str(repo_root / "data/cie1964_10deg_cmf.csv"),
        "--observer-2", str(repo_root / "data/cie1931_2deg_cmf.csv"),
        "--d55", str(repo_root / "data/cie_d55.csv"),
        "--out-json", str(output / "spectral_reference_audit.json"),
        "--out-csv", str(output / "spectral_reference_repeat.csv"),
    ])
    comparison = json.loads((output / "hid_spectral_comparison.json").read_text())
    reference = json.loads((output / "spectral_reference_audit.json").read_text())
    (output / "spectral_archive_crosscheck.svg").write_text(
        render_svg(output / "hid_spectral_comparison.csv", comparison, reference),
        encoding="utf-8",
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo-root", type=Path,
                        default=Path(__file__).resolve().parents[1])
    parser.add_argument("--camera-iq", type=Path)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    repo_root = args.repo_root.resolve()
    camera_iq = (args.camera_iq or repo_root / "build/camera_iq").resolve()
    targets = {
        "hid_spectral_comparison.json": repo_root / "docs/data/hid_spectral_comparison.json",
        "hid_spectral_comparison.csv": repo_root / "docs/data/hid_spectral_comparison.csv",
        "spectral_reference_audit.json": repo_root / "docs/data/spectral_reference_audit.json",
        "spectral_reference_repeat.csv": repo_root / "docs/data/spectral_reference_repeat.csv",
        "spectral_archive_crosscheck.svg": repo_root / "docs/figures/spectral_archive_crosscheck.svg",
    }
    with tempfile.TemporaryDirectory() as raw:
        generated = Path(raw)
        generate(repo_root, camera_iq, generated)
        if args.check:
            stale = [name for name, target in targets.items()
                     if not artifact_matches(name, generated / name, target)]
            if stale:
                details = "; ".join(
                    f"{name}: {artifact_difference(name, generated / name, targets[name])}"
                    for name in stale
                )
                raise SystemExit(
                    "stale 2017 spectral artifacts: " + ", ".join(stale) +
                    "\n" + details
                )
        else:
            for name, target in targets.items():
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(generated / name, target)
    print("2017 spectral portfolio artifacts are current")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
