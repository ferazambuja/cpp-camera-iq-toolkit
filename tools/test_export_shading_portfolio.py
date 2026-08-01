#!/usr/bin/env python3
"""Negative-path checks for the shading publication exporter."""

from __future__ import annotations

import importlib.util
import copy
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
        "near_ceiling_fraction_gate": [0.0 if accepted else 0.02] * 4,
        "near_ceiling_fraction_frame": [0.0 if accepted else 0.001] * 4,
        "negative_fraction": [0.0] * 4,
        "center_signal_fraction": [0.5] * 4,
        "min_bin_coverage": 1.0,
        "near_ceiling_ok": accepted,
        "low_signal_ok": True,
        "negative_ok": True,
        "coverage_ok": True,
        "finite_ok": True,
    }
    corner_values = [0.8, 1.0, 0.8, 1.0]
    corner_relative = [[value] * 4 for value in corner_values]
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
        "schema_version": EXPORT.SCHEMA_VERSION,
        "file": label,
        "analysis_options": dict(EXPORT.EXPECTED_OPTIONS),
        "accepted": accepted,
        "gates": gates,
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
