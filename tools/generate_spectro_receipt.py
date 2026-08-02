#!/usr/bin/env python3
"""Derive a compact public receipt from spectro-ingest artifacts."""

from __future__ import annotations

import argparse
import csv
import json
import pathlib
import statistics
import sys
from typing import Any


def read_csv(path: pathlib.Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as stream:
        return list(csv.DictReader(stream))


def metric(values: list[float]) -> dict[str, float]:
    if not values:
        raise ValueError("receipt requires at least one repeated group")
    return {"median": statistics.median(values), "maximum": max(values)}


def summarize(
    result: dict[str, Any],
    group_rows: list[dict[str, str]],
    reading_rows: list[dict[str, str]],
) -> dict[str, Any]:
    if result.get("schema_version") != 2:
        raise ValueError("receipt requires spectro-ingest schema version 2")
    result_groups = {
        row["group_id"]: int(row["count"]) for row in result.get("groups", [])
    }
    csv_groups = {row["group_id"]: int(row["count"]) for row in group_rows}
    if result_groups != csv_groups or len(csv_groups) != len(group_rows):
        raise ValueError("result JSON and group CSV identities do not match")
    evidence = result["evidence"]
    if evidence["measurement_groups"] != len(group_rows):
        raise ValueError("result group count does not match the group CSV")
    if evidence["canonical_readings"] != len(reading_rows):
        raise ValueError("result reading count does not match the reading CSV")

    repeated = [row for row in group_rows if int(row["count"]) >= 2]
    singletons = [row for row in group_rows if int(row["count"]) == 1]
    if any(
        row["variation_label"] != "within_group_observed_variation"
        for row in repeated
    ):
        raise ValueError("a repeated group lacks the observed-variation label")
    if any(
        row["variation_label"] != "not_established_single_measurement"
        for row in singletons
    ):
        raise ValueError("a singleton group claims an observed variation")

    numbered_ratios: list[float] = []
    other_ratios: list[float] = []
    for row in reading_rows:
        integral = float(row["spectral_integral"])
        ratio = float(row["recorded_total_radiance"]) / integral
        if row["canonical_path"].startswith("PRD measurments/PRD_"):
            numbered_ratios.append(ratio)
        else:
            other_ratios.append(ratio)
    if not numbered_ratios or not other_ratios:
        raise ValueError("receipt requires numbered PRD and other record families")

    closure = result["closure"]
    dataset = str(result["dataset"])
    for prefix in ("dataset-root:", "dataset:"):
        if dataset.startswith(prefix):
            dataset = dataset[len(prefix) :]
            break
    return {
        "receipt_schema_version": 1,
        "result_schema_version": result["schema_version"],
        "derivation": {
            "tool": "tools/generate_spectro_receipt.py",
            "version": 1,
        },
        "dataset": dataset,
        "inputs": {
            "identity_ledger": result["ledger"],
            "observer": {
                "file": closure["observer_file"],
                "sha256": closure["observer_sha256"],
            },
        },
        "evidence": {
            "canonical_readings": evidence["canonical_readings"],
            "declared_aliases": evidence["declared_aliases"],
            "aliases_verified": evidence["aliases_verified"],
            "measurement_groups": evidence["measurement_groups"],
            "repeated_groups": len(repeated),
            "singleton_groups": len(singletons),
        },
        "group_metrics": {
            "spectral_integral_coefficient_of_variation_percent": metric(
                [100.0 * float(row["coefficient_of_variation"]) for row in repeated]
            ),
            "normalized_shape_relative_l2_percent": metric(
                [100.0 * float(row["max_shape_relative_l2"]) for row in repeated]
            ),
            "recorded_xyz_max_pair_delta_u_prime_v_prime": metric(
                [
                    float(row["max_pair_delta_u_prime_v_prime"])
                    for row in repeated
                ]
            ),
        },
        "closure": {
            "sample_weighting": closure["sample_weighting"],
            "scale_source": closure["scale_source"],
            "scale_value": closure["scale_value"],
            "max_absolute_relative_residual_percent": closure[
                "max_absolute_relative_residual_percent"
            ],
            "rms_relative_residual_percent": closure[
                "rms_relative_residual_percent"
            ],
        },
        "recorded_metadata_checks": {
            "numbered_prd": {
                "count": len(numbered_ratios),
                "max_absolute_integral_ratio_error": max(
                    abs(value - 1.0) for value in numbered_ratios
                ),
            },
            "other_records": {
                "count": len(other_ratios),
                "total_radiance_to_integral_ratio": {
                    "minimum": min(other_ratios),
                    "maximum": max(other_ratios),
                },
            },
        },
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--result", type=pathlib.Path, required=True)
    parser.add_argument("--groups-csv", type=pathlib.Path, required=True)
    parser.add_argument("--readings-csv", type=pathlib.Path, required=True)
    parser.add_argument("--out", type=pathlib.Path, required=True)
    args = parser.parse_args()
    try:
        result = json.loads(args.result.read_text(encoding="utf-8"))
        receipt = summarize(
            result, read_csv(args.groups_csv), read_csv(args.readings_csv)
        )
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(
            json.dumps(receipt, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )
        return 0
    except (OSError, KeyError, TypeError, ValueError, json.JSONDecodeError) as error:
        print(f"spectro receipt: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
