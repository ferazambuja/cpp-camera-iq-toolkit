#!/usr/bin/env python3
"""Validate the public spectroradiometer receipt against committed inputs."""

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


def rows(data: bytes) -> list[dict[str, str]]:
    return list(csv.DictReader(io.StringIO(data.decode("utf-8"), newline="")))


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


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


def require_sha256(value: object, label: str) -> str:
    if not isinstance(value, str) or not SHA256_PATTERN.fullmatch(value):
        raise ValueError(f"{label} is not a lowercase SHA-256 digest")
    return value


def require_nonnegative_finite(value: object, label: str) -> float:
    parsed = float(value)
    if not math.isfinite(parsed) or parsed < 0.0:
        raise ValueError(f"{label} must be finite and nonnegative")
    return parsed


def data_source(repo_root: pathlib.Path, name: object, label: str) -> pathlib.Path:
    if not isinstance(name, str):
        raise ValueError(f"{label} file name must be a string")
    relative = pathlib.PurePosixPath(name)
    if relative.is_absolute() or ".." in relative.parts:
        raise ValueError(f"{label} file must remain under data/")
    return repo_root / "data" / pathlib.Path(*relative.parts)


def validate(
    repo_root: pathlib.Path,
    receipt_path: pathlib.Path,
    groups_path: pathlib.Path,
) -> None:
    receipt = json.loads(receipt_path.read_text(encoding="utf-8"))
    groups_bytes = groups_path.read_bytes()
    if receipt.get("receipt_schema_version") != RECEIPT_SCHEMA_VERSION:
        raise ValueError("unsupported receipt schema")
    if receipt.get("result_schema_version") != RESULT_SCHEMA_VERSION:
        raise ValueError(
            "receipt is not bound to spectro-ingest schema "
            f"{RESULT_SCHEMA_VERSION}"
        )

    derivation = receipt["derivation"]
    if derivation != {
        "tool": "tools/generate_spectro_receipt.py",
        "version": 1,
    }:
        raise ValueError("unsupported receipt derivation")

    artifacts = receipt["artifacts"]
    public_digest = require_sha256(
        artifacts["public_group_summary"]["sha256"],
        "public group summary hash",
    )
    if digest(groups_bytes) != public_digest:
        raise ValueError("public group summary hash does not match the receipt")
    for key in ("archive_run_result", "archive_run_readings"):
        require_sha256(artifacts[key]["sha256"], f"{key} hash")

    inputs = receipt["inputs"]
    input_bytes: dict[str, bytes] = {}
    for key in ("identity_ledger", "observer"):
        described = inputs[key]
        source = data_source(repo_root, described["file"], key)
        source_bytes = source.read_bytes()
        expected_digest = require_sha256(described["sha256"], f"{key} hash")
        if digest(source_bytes) != expected_digest:
            raise ValueError(f"{key} hash does not match the receipt")
        input_bytes[key] = source_bytes

    ledger_rows = rows(input_bytes["identity_ledger"])
    group_rows = rows(groups_bytes)
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

    closure = receipt["closure"]
    if closure.get("sample_weighting") != "uniform_equal_weight":
        raise ValueError("closure sample weighting is not the exercised rule")
    if closure.get("scale_source") != "derived_from_recorded_xyz":
        raise ValueError("closure scale source is not the exercised fit")
    scale_value = float(closure["scale_value"])
    if not math.isfinite(scale_value) or scale_value <= 0.0:
        raise ValueError("closure scale value must be finite and positive")
    require_nonnegative_finite(
        closure["max_absolute_relative_residual_percent"],
        "closure maximum residual",
    )
    require_nonnegative_finite(
        closure["rms_relative_residual_percent"], "closure RMS residual"
    )

    metadata = receipt["recorded_metadata_checks"]
    metadata_count = metadata["numbered_prd"]["count"] + metadata["other_records"][
        "count"
    ]
    if metadata_count != len(ledger_rows):
        raise ValueError("recorded-metadata receipt counts are incomplete")
    require_nonnegative_finite(
        metadata["numbered_prd"]["max_absolute_integral_ratio_error"],
        "numbered PRD integral-ratio error",
    )
    ratios = metadata["other_records"]["total_radiance_to_integral_ratio"]
    minimum = float(ratios["minimum"])
    maximum = float(ratios["maximum"])
    if (
        not math.isfinite(minimum)
        or not math.isfinite(maximum)
        or minimum <= 0.0
        or maximum < minimum
    ):
        raise ValueError("other-record integral-ratio range is invalid")


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
