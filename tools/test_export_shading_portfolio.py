#!/usr/bin/env python3
"""Negative-path checks for the shading publication exporter."""

from __future__ import annotations

import importlib.util
import copy
import csv
import json
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "export_shading_portfolio", ROOT / "tools" / "export_shading_portfolio.py"
)
assert SPEC and SPEC.loader
EXPORT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(EXPORT)


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
            "does not match portfolio policy",
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
