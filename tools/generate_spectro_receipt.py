#!/usr/bin/env python3
"""Derive a compact public receipt from spectro-ingest artifacts."""

from __future__ import annotations

import argparse
import csv
import hashlib
import io
import json
import math
import pathlib
import re
import statistics
import sys
from typing import Any


RESULT_SCHEMA_VERSION = 2
RECEIPT_SCHEMA_VERSION = 2
SHA256_PATTERN = re.compile(r"[0-9a-f]{64}")


def read_csv(data: bytes) -> list[dict[str, str]]:
    return list(csv.DictReader(io.StringIO(data.decode("utf-8"), newline="")))


def metric(values: list[float]) -> dict[str, float]:
    if not values:
        raise ValueError("receipt requires at least one repeated group")
    return {"median": statistics.median(values), "maximum": max(values)}


def finite_float(value: str | int | float, label: str) -> float:
    parsed = float(value)
    if not math.isfinite(parsed):
        raise ValueError(f"{label} must be finite")
    return parsed


def optional_float(value: str, label: str) -> float | None:
    return None if value == "" else finite_float(value, label)


def require_equal(actual: object, expected: object, label: str) -> None:
    if actual != expected:
        raise ValueError(f"result JSON and CSV disagree for {label}")


def validate_group_rows(
    result_groups: list[dict[str, Any]], group_rows: list[dict[str, str]]
) -> None:
    by_id: dict[str, dict[str, Any]] = {}
    for group in result_groups:
        group_id = str(group["group_id"])
        if group_id in by_id:
            raise ValueError(f"duplicate result group: {group_id}")
        by_id[group_id] = group
    csv_ids = [row["group_id"] for row in group_rows]
    if len(set(csv_ids)) != len(csv_ids) or set(csv_ids) != set(by_id):
        raise ValueError("result JSON and group CSV identities do not match")
    for row in group_rows:
        group_id = row["group_id"]
        group = by_id[group_id]
        absolute = group["absolute"]
        shape = group["shape"]
        chromaticity = group["recorded_xyz_chromaticity"]
        count = int(row["count"])
        require_equal(count, int(group["count"]), f"{group_id}.count")
        require_equal(
            count, len(group["readings"]), f"{group_id}.count versus readings"
        )
        comparisons = {
            "mean_spectral_integral": absolute["mean_spectral_integral"],
            "sample_stddev_spectral_integral": absolute[
                "sample_stddev_spectral_integral"
            ],
            "coefficient_of_variation": absolute["coefficient_of_variation"],
            "max_pair_delta_u_prime_v_prime": chromaticity[
                "max_pair_delta_u_prime_v_prime"
            ],
            "max_shape_relative_l2": shape["max_relative_l2"],
        }
        for field, expected in comparisons.items():
            observed = optional_float(row[field], f"{group_id}.{field}")
            require_equal(observed, expected, f"{group_id}.{field}")
        expected_label = (
            "not_established_single_measurement"
            if count == 1
            else "within_group_observed_variation"
        )
        require_equal(row["variation_label"], expected_label, f"{group_id}.label")


def validate_reading_rows(
    result_groups: list[dict[str, Any]], reading_rows: list[dict[str, str]]
) -> None:
    result_readings: dict[tuple[str, int], dict[str, Any]] = {}
    for group in result_groups:
        group_id = str(group["group_id"])
        for reading in group["readings"]:
            identity = (group_id, int(reading["measurement_index"]))
            if identity in result_readings:
                raise ValueError(f"duplicate result reading: {identity}")
            result_readings[identity] = reading

    csv_readings: dict[tuple[str, int], dict[str, str]] = {}
    for row in reading_rows:
        identity = (row["group_id"], int(row["measurement_index"]))
        if identity in csv_readings:
            raise ValueError(f"duplicate reading CSV identity: {identity}")
        csv_readings[identity] = row
    if set(result_readings) != set(csv_readings):
        raise ValueError("result JSON and reading CSV identities do not match")

    scalar_fields = {
        "canonical_path": "path",
        "sha256": "sha256",
        "spectral_integral": "spectral_integral",
        "recorded_total_radiance": "recorded_total_radiance",
        "recorded_cct_k": "recorded_cct_k",
        "recorded_duv": "recorded_duv",
    }
    vector_fields = {
        "recorded_x": ("recorded_xyz", 0),
        "recorded_y": ("recorded_xyz", 1),
        "recorded_z": ("recorded_xyz", 2),
        "computed_x": ("computed_xyz", 0),
        "computed_y": ("computed_xyz", 1),
        "computed_z": ("computed_xyz", 2),
        "residual_x_percent": ("signed_relative_residual_percent", 0),
        "residual_y_percent": ("signed_relative_residual_percent", 1),
        "residual_z_percent": ("signed_relative_residual_percent", 2),
    }
    chromaticity_fields = {
        "chromaticity_x": "x",
        "chromaticity_y": "y",
        "u_prime": "u_prime",
        "v_prime": "v_prime",
    }
    for identity, row in csv_readings.items():
        reading = result_readings[identity]
        label = f"{identity[0]}[{identity[1]}]"
        for csv_field, json_field in scalar_fields.items():
            observed: object = row[csv_field]
            expected = reading[json_field]
            if csv_field not in {"canonical_path", "sha256"}:
                observed = finite_float(row[csv_field], f"{label}.{csv_field}")
            require_equal(observed, expected, f"{label}.{csv_field}")
        if not SHA256_PATTERN.fullmatch(row["sha256"]):
            raise ValueError(f"{label}.sha256 is not lowercase SHA-256")
        for hash_field in (
            "wavelength_binary64_le_sha256",
            "radiance_binary64_le_sha256",
        ):
            if not SHA256_PATTERN.fullmatch(row[hash_field]):
                raise ValueError(f"{label}.{hash_field} is not lowercase SHA-256")
        for csv_field, (json_field, index) in vector_fields.items():
            observed = finite_float(row[csv_field], f"{label}.{csv_field}")
            require_equal(observed, reading[json_field][index], f"{label}.{csv_field}")
        for csv_field, json_field in chromaticity_fields.items():
            observed = finite_float(row[csv_field], f"{label}.{csv_field}")
            require_equal(
                observed, reading["chromaticity"][json_field], f"{label}.{csv_field}"
            )


def summarize(
    result: dict[str, Any],
    group_rows: list[dict[str, str]],
    reading_rows: list[dict[str, str]],
    artifact_hashes: dict[str, str],
) -> dict[str, Any]:
    if result.get("schema_version") != RESULT_SCHEMA_VERSION:
        raise ValueError(
            f"receipt requires spectro-ingest schema version {RESULT_SCHEMA_VERSION}"
        )
    result_groups = result.get("groups", [])
    validate_group_rows(result_groups, group_rows)
    validate_reading_rows(result_groups, reading_rows)
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
        "receipt_schema_version": RECEIPT_SCHEMA_VERSION,
        "result_schema_version": result["schema_version"],
        "derivation": {
            "tool": "tools/generate_spectro_receipt.py",
            "version": 1,
        },
        "dataset": dataset,
        "artifacts": {
            "public_group_summary": {"sha256": artifact_hashes["groups_csv"]},
            "archive_run_result": {"sha256": artifact_hashes["result_json"]},
            "archive_run_readings": {"sha256": artifact_hashes["readings_csv"]},
        },
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
        result_bytes = args.result.read_bytes()
        groups_bytes = args.groups_csv.read_bytes()
        readings_bytes = args.readings_csv.read_bytes()
        result = json.loads(result_bytes)
        receipt = summarize(
            result,
            read_csv(groups_bytes),
            read_csv(readings_bytes),
            {
                "result_json": hashlib.sha256(result_bytes).hexdigest(),
                "groups_csv": hashlib.sha256(groups_bytes).hexdigest(),
                "readings_csv": hashlib.sha256(readings_bytes).hexdigest(),
            },
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
