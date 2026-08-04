#!/usr/bin/env python3
"""Negative-path checks for the shading publication exporter."""

from __future__ import annotations

import copy
import csv
import json
import math
import tempfile
from pathlib import Path
from types import ModuleType


ROOT = Path(__file__).resolve().parents[1]


def load_source_module(name: str, path: Path) -> ModuleType:
    module = ModuleType(name)
    module.__file__ = str(path)
    exec(compile(path.read_text(), str(path), "exec"), module.__dict__)
    return module


EXPORT = load_source_module(
    "export_shading_portfolio", ROOT / "tools" / "export_shading_portfolio.py"
)


def expect_error(action, needle: str) -> None:
    try:
        action()
    except ValueError as error:
        if needle not in str(error):
            raise AssertionError(f"expected {needle!r}, got {error!r}") from error
        return
    raise AssertionError(f"expected ValueError containing {needle!r}")


def document(label: str, accepted: bool) -> dict:
    bins = 16 * 12
    gates = {
        "measured": True,
        "near_ceiling_fraction_gate": [0.0 if accepted else 0.02] * 4,
        "near_ceiling_fraction_frame": [0.0 if accepted else 0.001] * 4,
        "finite_fraction_gate": [1.0] * 4,
        "finite_fraction_frame": [1.0] * 4,
        "negative_fraction": [0.0] * 4,
        "center_signal_fraction": [0.5] * 4,
        "min_bin_coverage": 1.0,
        "near_ceiling_ok": accepted,
        "screening_coverage_ok": True,
        "low_signal_ok": True,
        "negative_ok": True,
        "coverage_ok": True,
        "finite_ok": True,
    }
    corner_values = [0.8, 1.0, 0.8, 1.0]
    corner_relative = [[value] * 4 for value in corner_values]
    corner_median = [[1000.0 * value] * 4 for value in corner_values]
    green_asymmetry = (max(corner_values) - min(corner_values)) / (
        sum(corner_values) / 4.0
    )
    pedestal = {
        "measured": accepted,
        "pedestal_unverified": not accepted,
        "compatible": accepted,
        "make_model_metadata_matches": accepted,
        "body_serials_present": accepted,
        "body_serials_match": accepted,
        "body_serials_consistent": accepted,
        "residual_dn": [0.0] * 4,
        "max_abs_residual_dn": 0.0,
        "finite_fraction": [1.0] * 4,
        "center_residual_dn": [0.0] * 4,
        "corner_residual_dn": [[0.0] * 4 for _ in range(4)],
        "max_abs_spatial_residual_dn": 0.0,
        "exposure_metadata_present": accepted,
        "exposure_metadata_matches": accepted,
        "full_finite_coverage": accepted,
        "spatial_checked": accepted,
        "within_tolerance": accepted,
        "verified": accepted,
    }
    return {
        # Producer-shaped literals are deliberately independent of exporter
        # constants. A stale consumer must fail this fixture instead of making
        # its own obsolete contract look internally consistent.
        "schema_version": 3,
        "file": label,
        "analysis_options": {
            "grid_cols": 16,
            "grid_rows": 12,
            "gate_center_frac": 0.20,
            "corner_block_px": 400,
            "corner_inset_px": 120,
            "near_ceiling_level": 0.98,
            "near_ceiling_max": 0.01,
            "min_finite_coverage": 0.90,
            "min_center_signal": 0.05,
            "max_negative_frac": 0.01,
            "min_bin_coverage": 0.90,
            "asymmetry_policy": 0.05,
        },
        "accepted": accepted,
        "gates": gates,
        "signal_ceiling_dn": [2000.0] * 4,
        "geometry": {
            "gate": {"x": 0, "y": 0, "width": 800, "height": 800},
            "center": {"x": 200, "y": 200, "width": 400, "height": 400},
            "corners": [
                {"x": 0, "y": 0, "width": 100, "height": 100},
                {"x": 700, "y": 0, "width": 100, "height": 100},
                {"x": 0, "y": 700, "width": 100, "height": 100},
                {"x": 700, "y": 700, "width": 100, "height": 100},
            ],
        },
        "center_block_median": [1000.0] * 4,
        "corner_median": corner_median,
        "grid": {"cols": 16, "rows": 12},
        "cfa_positions": {"r": 0, "g1": 1, "g2": 2, "b": 3},
        "green_asymmetry": green_asymmetry if accepted else None,
        "asymmetry_exceeds_policy": True if accepted else None,
        "corner_relative": corner_relative if accepted else None,
        "pedestal": pedestal,
        "relative_response": [[1.0] * bins for _ in range(4)] if accepted else None,
        "c_rg": [1.0] * bins if accepted else None,
        "c_bg": [1.0] * bins if accepted else None,
        "c_g1g2": [1.0] * bins if accepted else None,
        "chromatic_complete": accepted,
        "missing_chromatic_bin_count": 0,
    }


def write_json(path: Path, value: dict) -> None:
    path.write_text(json.dumps(value), encoding="utf-8")


def main() -> None:
    legacy = {
        **document("dataset:fixture/legacy.RAF", False),
        "schema_version": 2,
    }
    expect_error(
        lambda: EXPORT.validate_options(legacy, "legacy fixture"),
        "unsupported shading schema",
    )

    shared = document(
        "dataset:fixture/Images/Sphere/Sphere_f8.0_1:1000_DSCF0001.RAF",
        True,
    )
    if not hasattr(EXPORT, "validate_inventory_document"):
        raise AssertionError(
            "exporter lacks the shared per-document publication validator"
        )
    validated = EXPORT.validate_inventory_document(
        shared, "shared fixture", require_verified_pedestal=True
    )
    if validated["file_label"] != shared["file"]:
        raise AssertionError("shared validator did not preserve the file label")

    missing_file = copy.deepcopy(shared)
    del missing_file["file"]
    expect_error(
        lambda: EXPORT.validate_inventory_document(missing_file, "missing file"),
        "missing file label",
    )

    mismatched_grid = copy.deepcopy(shared)
    mismatched_grid["grid"]["cols"] = 15
    mismatched_grid["relative_response"] = [
        values[: 15 * 12] for values in mismatched_grid["relative_response"]
    ]
    for key in ("c_rg", "c_bg", "c_g1g2"):
        mismatched_grid[key] = mismatched_grid[key][: 15 * 12]
    expect_error(
        lambda: EXPORT.validate_inventory_document(
            mismatched_grid, "mismatched grid"
        ),
        "grid 15x12 does not match analysis_options 16x12",
    )

    peripheral = document("dataset:fixture/peripheral.RAF", False)
    peripheral["gates"]["near_ceiling_fraction_gate"] = [0.0] * 4
    peripheral["gates"]["near_ceiling_fraction_frame"] = [0.02, 0.0, 0.0, 0.0]
    EXPORT.validated_gates(peripheral, "peripheral near-ceiling fixture")

    sparse = document("dataset:fixture/sparse.RAF", False)
    sparse["gates"]["near_ceiling_fraction_gate"] = [0.0] * 4
    sparse["gates"]["near_ceiling_fraction_frame"] = [0.0] * 4
    sparse["gates"]["near_ceiling_ok"] = True
    sparse["gates"]["finite_fraction_gate"][2] = 0.5
    sparse["gates"]["screening_coverage_ok"] = False
    *_, sparse_failed = EXPORT.validated_gates(sparse, "sparse fixture")
    if sparse_failed != ["screening_coverage"]:
        raise AssertionError("screening coverage is not an independent failed gate")

    unmeasured = document("dataset:fixture/unmeasured.RAF", False)
    unmeasured["gates"]["measured"] = False
    expect_error(
        lambda: EXPORT.validated_gates(unmeasured, "unmeasured fixture"),
        "gate evidence was not measured",
    )

    undefined_coverage = document("dataset:fixture/undefined.RAF", False)
    undefined_coverage["gates"]["finite_fraction_gate"][0] = None
    expect_error(
        lambda: EXPORT.validated_gates(
            undefined_coverage, "undefined coverage fixture"
        ),
        "expected a finite number",
    )

    valid_label = (
        "dataset:fixture/Images/Sphere/"
        "Sphere_f8.0_1:1000_DSCF0001.RAF"
    )
    exact_policy = document(valid_label, True)
    exact_policy["gates"]["near_ceiling_fraction_gate"] = [0.01] * 4
    exact_policy["gates"]["near_ceiling_fraction_frame"] = [0.01] * 4
    exact_policy["gates"]["finite_fraction_gate"] = [0.90] * 4
    exact_policy["gates"]["finite_fraction_frame"] = [0.90] * 4
    EXPORT.validate_inventory_document(
        exact_policy, "exact-boundary fixture", require_verified_pedestal=True
    )

    just_over_near_ceiling = document(valid_label, True)
    just_over_near_ceiling["gates"]["near_ceiling_fraction_gate"][0] = (
        math.nextafter(0.01, math.inf)
    )
    expect_error(
        lambda: EXPORT.validated_gates(
            just_over_near_ceiling, "just-over near-ceiling fixture"
        ),
        "near_ceiling_ok disagrees with measured fractions",
    )

    just_below_coverage = document(valid_label, True)
    just_below_coverage["gates"]["finite_fraction_frame"][0] = math.nextafter(
        0.90, 0.0
    )
    expect_error(
        lambda: EXPORT.validated_gates(
            just_below_coverage, "just-below coverage fixture"
        ),
        "screening_coverage_ok disagrees with measured fractions",
    )

    nonpositive_center = document(valid_label, True)
    nonpositive_center["center_block_median"][0] = 0.0
    expect_error(
        lambda: EXPORT.validate_inventory_document(
            nonpositive_center,
            "nonpositive center fixture",
            require_verified_pedestal=True,
        ),
        "center medians must be positive",
    )

    negative_center = document(valid_label, True)
    negative_center["center_block_median"][2] = -1.0
    expect_error(
        lambda: EXPORT.validate_inventory_document(
            negative_center,
            "negative center fixture",
            require_verified_pedestal=True,
        ),
        "center medians must be positive",
    )

    outside_center = document(valid_label, True)
    outside_center["geometry"]["center"]["x"] = 800
    expect_error(
        lambda: EXPORT.validate_inventory_document(
            outside_center,
            "outside center fixture",
            require_verified_pedestal=True,
        ),
        "center geometry is outside the screening gate",
    )

    incomplete_chromatic = document(valid_label, True)
    incomplete_chromatic["chromatic_complete"] = False
    incomplete_chromatic["missing_chromatic_bin_count"] = 1
    expect_error(
        lambda: EXPORT.validate_inventory_document(
            incomplete_chromatic,
            "incomplete chromatic fixture",
            require_verified_pedestal=True,
        ),
        "accepted chromatic maps must be complete",
    )

    noninteger_missing_count = document(valid_label, True)
    noninteger_missing_count["missing_chromatic_bin_count"] = False
    expect_error(
        lambda: EXPORT.validate_inventory_document(
            noninteger_missing_count,
            "noninteger chromatic count fixture",
            require_verified_pedestal=True,
        ),
        "missing chromatic-bin count must be integer zero",
    )
    floating_missing_count = document(valid_label, True)
    floating_missing_count["missing_chromatic_bin_count"] = 0.0
    expect_error(
        lambda: EXPORT.validate_inventory_document(
            floating_missing_count,
            "floating chromatic count fixture",
            require_verified_pedestal=True,
        ),
        "missing chromatic-bin count must be integer zero",
    )

    noninteger_positions = document(valid_label, True)
    noninteger_positions["cfa_positions"] = {
        "r": False,
        "g1": True,
        "g2": 2,
        "b": 3,
    }
    expect_error(
        lambda: EXPORT.validate_inventory_document(
            noninteger_positions,
            "noninteger positions fixture",
            require_verified_pedestal=True,
        ),
        "CFA positions must be integers",
    )
    string_positions = document(valid_label, True)
    string_positions["cfa_positions"] = {
        "r": "0",
        "g1": "1",
        "g2": "2",
        "b": "3",
    }
    expect_error(
        lambda: EXPORT.validate_inventory_document(
            string_positions,
            "string positions fixture",
            require_verified_pedestal=True,
        ),
        "CFA positions must be integers",
    )

    unverified_detail = document(valid_label, True)
    unverified_detail["pedestal"]["verified"] = False
    unverified_detail["pedestal"]["pedestal_unverified"] = True
    expect_error(
        lambda: EXPORT.validate_inventory_document(
            unverified_detail,
            "unverified detailed fixture",
            require_verified_pedestal=True,
        ),
        "accepted result lacks verified dark controls",
    )

    with tempfile.TemporaryDirectory() as temp:
        root = Path(temp)
        inventory = root / "inventory"
        inventory.mkdir()
        docs: list[dict] = []
        index = 0
        for aperture, count in (("5.6", 18), ("8.0", 21), ("9.0", 13)):
            for _ in range(count):
                accepted = index in (18, 19, 20)
                label = (
                    "dataset:fixture/Images/Sphere/"
                    f"Sphere_f{aperture}_1:{1000 + index}_DSCF{3000 + index}.RAF"
                )
                item = document(label, accepted)
                docs.append(item)
                write_json(inventory / f"{index:02d}.json", item)
                index += 1

        primary, repeat, third = docs[18], docs[19], docs[20]
        docs[0]["gates"]["near_ceiling_fraction_gate"] = [0.02, 0.03, 0.04, 0.05]
        docs[0]["gates"]["near_ceiling_fraction_frame"] = [
            0.001,
            0.002,
            0.003,
            0.004,
        ]
        write_json(inventory / "00.json", docs[0])
        comparison = {
            "measured": True,
            "max_corner_delta_pp": 0.0,
            "rms_corner_delta_pp": 0.0,
            "primary_file": primary["file"],
            "repeat_file": repeat["file"],
        }
        detailed = {item["file"]: item for item in (primary, repeat, third)}
        accepted = EXPORT.write_summary(
            inventory, detailed, comparison, root / "summary.csv"
        )
        with (root / "summary.csv").open(newline="", encoding="utf-8") as handle:
            summary_rows = list(csv.DictReader(handle))
        first = summary_rows[0]
        expected_plane_evidence = {
            "near_ceiling_gate_r": "0.02000000",
            "near_ceiling_gate_g1": "0.03000000",
            "near_ceiling_gate_g2": "0.04000000",
            "near_ceiling_gate_b": "0.05000000",
            "near_ceiling_frame_r": "0.00100000",
            "near_ceiling_frame_g1": "0.00200000",
            "near_ceiling_frame_g2": "0.00300000",
            "near_ceiling_frame_b": "0.00400000",
            "finite_fraction_gate_r": "1.00000000",
            "finite_fraction_gate_g1": "1.00000000",
            "finite_fraction_gate_g2": "1.00000000",
            "finite_fraction_gate_b": "1.00000000",
            "finite_fraction_frame_r": "1.00000000",
            "finite_fraction_frame_g1": "1.00000000",
            "finite_fraction_frame_g2": "1.00000000",
            "finite_fraction_frame_b": "1.00000000",
        }
        if any(first.get(key) != value for key, value in expected_plane_evidence.items()):
            raise AssertionError("summary omits or misorders per-CFA gate evidence")
        if first.get("analysis_policy") != "shading-v2-grid16x12-screening-coverage":
            raise AssertionError("summary does not identify the schema-3 screening policy")
        response_paths: list[Path] = []
        for number, item in enumerate((primary, repeat, third)):
            path = root / f"response-{number}.json"
            write_json(path, item)
            response_paths.append(path)
        EXPORT.write_response(response_paths, root / "response.csv", accepted)

        bad_comparison = dict(comparison, measured=False)
        expect_error(
            lambda: EXPORT.write_summary(
                inventory, detailed, bad_comparison, root / "bad-summary.csv"
            ),
            "comparison must be measured",
        )

        inconsistent_comparison = dict(comparison, max_corner_delta_pp=0.3)
        expect_error(
            lambda: EXPORT.write_summary(
                inventory,
                detailed,
                inconsistent_comparison,
                root / "inconsistent-comparison.csv",
            ),
            "comparison max delta disagrees with corner evidence",
        )

        inconsistent_asymmetry = copy.deepcopy(primary)
        inconsistent_asymmetry["green_asymmetry"] += 0.01
        expect_error(
            lambda: EXPORT.write_summary(
                inventory,
                {**detailed, primary["file"]: inconsistent_asymmetry},
                comparison,
                root / "inconsistent-asymmetry.csv",
            ),
            "detailed and inventory green_asymmetry disagree",
        )

        incomplete_pedestal = copy.deepcopy(primary)
        incomplete_pedestal["pedestal"]["full_finite_coverage"] = False
        expect_error(
            lambda: EXPORT.write_summary(
                inventory,
                {**detailed, primary["file"]: incomplete_pedestal},
                comparison,
                root / "incomplete-pedestal.csv",
            ),
            "verified pedestal lacks full_finite_coverage",
        )

        inconsistent_residual = copy.deepcopy(primary)
        inconsistent_residual["pedestal"]["residual_dn"][0] = 2.0
        inconsistent_residual["pedestal"]["max_abs_residual_dn"] = 2.0
        expect_error(
            lambda: EXPORT.write_summary(
                inventory,
                {**detailed, primary["file"]: inconsistent_residual},
                comparison,
                root / "inconsistent-residual.csv",
            ),
            "tolerance verdict disagrees with residuals",
        )

        primary_inventory = inventory / "18.json"
        unverified_inventory = copy.deepcopy(primary)
        unverified_inventory["pedestal"]["verified"] = False
        unverified_inventory["pedestal"]["pedestal_unverified"] = True
        write_json(primary_inventory, unverified_inventory)
        try:
            EXPORT.write_summary(
                inventory,
                detailed,
                comparison,
                root / "inventory-defers-dark-evidence.csv",
            )
        except ValueError as error:
            raise AssertionError(
                "screening inventory should defer dark verification to detailed evidence"
            ) from error
        write_json(primary_inventory, primary)

        missing_center = copy.deepcopy(primary)
        missing_center["center_block_median"] = None
        write_json(primary_inventory, missing_center)
        expect_error(
            lambda: EXPORT.write_summary(
                inventory, detailed, comparison, root / "missing-center.csv"
            ),
            "expected four values",
        )
        write_json(primary_inventory, primary)

        undefined_corner = copy.deepcopy(primary)
        undefined_corner["corner_median"][2][1] = None
        write_json(primary_inventory, undefined_corner)
        expect_error(
            lambda: EXPORT.write_summary(
                inventory, detailed, comparison, root / "undefined-corner.csv"
            ),
            "expected a finite number",
        )
        write_json(primary_inventory, primary)

        bad_geometry = copy.deepcopy(primary)
        bad_geometry["geometry"]["center"]["x"] = 201
        write_json(primary_inventory, bad_geometry)
        expect_error(
            lambda: EXPORT.write_summary(
                inventory, detailed, comparison, root / "bad-geometry.csv"
            ),
            "geometry is not CFA-balanced",
        )
        write_json(primary_inventory, primary)

        original_gate_fraction = primary["gates"]["near_ceiling_fraction_gate"]
        primary["gates"]["near_ceiling_fraction_gate"] = [0.02] * 4
        write_json(primary_inventory, primary)
        expect_error(
            lambda: EXPORT.write_summary(
                inventory,
                detailed,
                comparison,
                root / "inconsistent-gate-verdict.csv",
            ),
            "near_ceiling_ok disagrees with measured fractions",
        )
        primary["gates"]["near_ceiling_fraction_gate"] = original_gate_fraction
        write_json(primary_inventory, primary)

        original_c_rg = primary["c_rg"][0]
        primary["c_rg"][0] = 2.0
        write_json(primary_inventory, primary)
        write_json(response_paths[0], primary)
        expect_error(
            lambda: EXPORT.write_summary(
                inventory,
                detailed,
                comparison,
                root / "inconsistent-chromatic-map.csv",
            ),
            "c_rg disagrees with relative maps",
        )
        primary["c_rg"][0] = original_c_rg
        write_json(primary_inventory, primary)
        write_json(response_paths[0], primary)

        original_options = primary["analysis_options"]
        primary["analysis_options"] = dict(original_options, near_ceiling_max=0.2)
        expect_error(
            lambda: EXPORT.write_summary(
                inventory, {primary["file"]: primary}, None, root / "mixed.csv"
            ),
            "does not match the published method",
        )
        primary["analysis_options"] = original_options

        inconsistent_response = copy.deepcopy(primary)
        inconsistent_response["relative_response"][0][0] = 0.5
        inconsistent_response["c_rg"][0] = 0.5
        inconsistent_response_path = root / "inconsistent-response.json"
        write_json(inconsistent_response_path, inconsistent_response)
        expect_error(
            lambda: EXPORT.write_response(
                [inconsistent_response_path, *response_paths[1:]],
                root / "inconsistent-response.csv",
                accepted,
            ),
            "response and detailed relative_response disagree",
        )

        expect_error(
            lambda: EXPORT.write_response(
                response_paths[:2], root / "missing-response.csv", accepted
            ),
            "does not match accepted inventory set",
        )

        write_json(inventory / "duplicate.json", docs[0])
        expect_error(
            lambda: EXPORT.write_summary(
                inventory, detailed, comparison, root / "duplicate.csv"
            ),
            "duplicate inventory file label",
        )

    print("shading portfolio exporter tests passed")


if __name__ == "__main__":
    main()
