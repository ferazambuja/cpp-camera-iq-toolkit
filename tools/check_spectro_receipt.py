#!/usr/bin/env python3
"""Validate the public spectroradiometer receipt against committed inputs."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import pathlib
import statistics
import sys
from typing import Any


def rows(path: pathlib.Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as stream:
        return list(csv.DictReader(stream))


def digest(path: pathlib.Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def metric(values: list[float]) -> dict[str, float]:
    return {"median": statistics.median(values), "maximum": max(values)}


def equal_numbers(actual: dict[str, Any], expected: dict[str, float]) -> bool:
    return all(
        key in actual
        and math.isclose(
            float(actual[key]), value, rel_tol=1e-12, abs_tol=1e-15
        )
        for key, value in expected.items()
    )


def validate(
    repo_root: pathlib.Path,
    receipt_path: pathlib.Path,
    groups_path: pathlib.Path,
) -> None:
    receipt = json.loads(receipt_path.read_text(encoding="utf-8"))
    if receipt.get("receipt_schema_version") != 1:
        raise ValueError("unsupported receipt schema")
    if receipt.get("result_schema_version") != 2:
        raise ValueError("receipt is not bound to spectro-ingest schema 2")

    inputs = receipt["inputs"]
    for key in ("identity_ledger", "observer"):
        described = inputs[key]
        source = repo_root / "data" / described["file"]
        if digest(source) != described["sha256"]:
            raise ValueError(f"{key} hash does not match the receipt")

    ledger_rows = rows(repo_root / "data" / inputs["identity_ledger"]["file"])
    group_rows = rows(groups_path)
    evidence = receipt["evidence"]
    group_ids = {row["group_id"] for row in ledger_rows}
    alias_count = sum(
        len([name for name in row["alias_paths"].split(";") if name])
        for row in ledger_rows
    )
    repeated = [row for row in group_rows if int(row["count"]) >= 2]
    singletons = [row for row in group_rows if int(row["count"]) == 1]
    expected_evidence = {
        "canonical_readings": len(ledger_rows),
        "declared_aliases": alias_count,
        "measurement_groups": len(group_ids),
        "repeated_groups": len(repeated),
        "singleton_groups": len(singletons),
    }
    if any(evidence.get(key) != value for key, value in expected_evidence.items()):
        raise ValueError("receipt evidence counts do not match committed data")
    if not evidence.get("aliases_verified"):
        raise ValueError("receipt does not record alias verification")
    if any(
        row["variation_label"] != "within_group_observed_variation"
        for row in repeated
    ) or any(
        row["variation_label"] != "not_established_single_measurement"
        for row in singletons
    ):
        raise ValueError("group variation labels are inconsistent with counts")

    expected_metrics = {
        "spectral_integral_coefficient_of_variation_percent": metric(
            [100.0 * float(row["coefficient_of_variation"]) for row in repeated]
        ),
        "normalized_shape_relative_l2_percent": metric(
            [100.0 * float(row["max_shape_relative_l2"]) for row in repeated]
        ),
        "recorded_xyz_max_pair_delta_u_prime_v_prime": metric(
            [float(row["max_pair_delta_u_prime_v_prime"]) for row in repeated]
        ),
    }
    actual_metrics = receipt["group_metrics"]
    if any(
        key not in actual_metrics
        or not equal_numbers(actual_metrics[key], expected)
        for key, expected in expected_metrics.items()
    ):
        raise ValueError("receipt metrics do not match the public group table")

    metadata = receipt["recorded_metadata_checks"]
    metadata_count = metadata["numbered_prd"]["count"] + metadata["other_records"][
        "count"
    ]
    if metadata_count != len(ledger_rows):
        raise ValueError("recorded-metadata receipt counts are incomplete")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo-root", type=pathlib.Path, required=True)
    parser.add_argument(
        "--receipt", type=pathlib.Path, default="docs/data/spectro_result_receipt.json"
    )
    parser.add_argument(
        "--groups-csv", type=pathlib.Path, default="docs/data/spectro_group_summary.csv"
    )
    args = parser.parse_args()
    try:
        validate(
            args.repo_root,
            args.repo_root / args.receipt,
            args.repo_root / args.groups_csv,
        )
        return 0
    except (OSError, KeyError, TypeError, ValueError, json.JSONDecodeError) as error:
        print(f"spectro receipt: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
