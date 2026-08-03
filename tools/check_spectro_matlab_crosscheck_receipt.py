#!/usr/bin/env python3
"""Validate the public MATLAB/C++ spectroradiometer cross-check receipt."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import pathlib
import re
import sys
from typing import Any


SCHEMA = "camera_iq.spectro_matlab_crosscheck.v1"
HASH_FIELDS = (
    "wavelength_binary64_le_sha256",
    "radiance_binary64_le_sha256",
)
NUMERIC_FIELDS = (
    "spectral_integral",
    "recorded_x",
    "recorded_y",
    "recorded_z",
    "recorded_total_radiance",
    "recorded_cct_k",
    "recorded_duv",
)
SHA256_PATTERN = re.compile(r"[0-9a-f]{64}")
MATLAB_RELEASE_PATTERN = re.compile(r"R20[0-9]{2}[ab]")


def file_sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def ledger_reading_count(path: pathlib.Path) -> int:
    required = {"group_id", "repeat_index", "canonical_path", "sha256"}
    with path.open(newline="", encoding="utf-8") as stream:
        reader = csv.DictReader(stream)
        missing = required - set(reader.fieldnames or [])
        if missing:
            raise ValueError(
                f"identity ledger is missing fields: {', '.join(sorted(missing))}"
            )
        count = sum(1 for _ in reader)
    if count < 1:
        raise ValueError("identity ledger has no readings")
    return count


def strings(value: Any):
    if isinstance(value, str):
        yield value
    elif isinstance(value, dict):
        for key, item in value.items():
            yield str(key)
            yield from strings(item)
    elif isinstance(value, list):
        for item in value:
            yield from strings(item)


def is_path_like_private_value(value: str) -> bool:
    return (
        value.startswith("/")
        or re.match(r"^[A-Za-z]:[\\/]", value) is not None
        or "Documents/" in value
    )


def validate(
    document: dict[str, Any],
    *,
    result_receipt: dict[str, Any],
    ledger: pathlib.Path,
    matlab_exporter: pathlib.Path,
    comparator: pathlib.Path,
    cpp_csv: pathlib.Path | None,
    matlab_csv: pathlib.Path | None,
) -> list[str]:
    errors: list[str] = []
    if document.get("schema") != SCHEMA:
        errors.append(f"schema must be {SCHEMA}")
    if not isinstance(document.get("dataset_id"), str) or not document["dataset_id"]:
        errors.append("dataset_id must be a non-empty string")
    elif document["dataset_id"] != result_receipt.get("dataset"):
        errors.append("dataset_id does not match the result receipt")
    release = document.get("matlab_release")
    if not isinstance(release, str) or MATLAB_RELEASE_PATTERN.fullmatch(release) is None:
        errors.append("matlab_release must look like R2026a")
    if any(is_path_like_private_value(value) for value in strings(document)):
        errors.append("receipt contains a path-like private value")

    comparison = document.get("comparison")
    if not isinstance(comparison, dict):
        errors.append("comparison must be an object")
        comparison = {}
    if comparison.get("result") != "match":
        errors.append("comparison.result must be match")
    reading_count = comparison.get("reading_count")
    if not isinstance(reading_count, int) or isinstance(reading_count, bool) or reading_count < 1:
        errors.append("comparison.reading_count must be a positive integer")
        reading_count = 0
    try:
        result_reading_count = result_receipt["evidence"]["canonical_readings"]
    except (KeyError, TypeError):
        errors.append("result receipt is missing evidence.canonical_readings")
    else:
        if (
            not isinstance(result_reading_count, int)
            or isinstance(result_reading_count, bool)
            or result_reading_count < 1
        ):
            errors.append(
                "result receipt evidence.canonical_readings must be a positive integer"
            )
        elif reading_count != result_reading_count:
            errors.append("comparison.reading_count does not match the result receipt")
    if reading_count != ledger_reading_count(ledger):
        errors.append("comparison.reading_count does not match the identity ledger")
    if comparison.get("hash_fields") != list(HASH_FIELDS):
        errors.append("comparison.hash_fields do not match the comparator contract")
    if comparison.get("source_file_hash_comparisons") != reading_count:
        errors.append("comparison.source_file_hash_comparisons is inconsistent")
    if comparison.get("exact_hash_comparisons") != reading_count * len(HASH_FIELDS):
        errors.append("comparison.exact_hash_comparisons is inconsistent")
    if comparison.get("numeric_comparisons") != reading_count * len(NUMERIC_FIELDS):
        errors.append("comparison.numeric_comparisons is inconsistent")

    relative_tolerance = comparison.get("relative_tolerance")
    absolute_tolerance = comparison.get("absolute_tolerance")
    for name, value in (
        ("relative_tolerance", relative_tolerance),
        ("absolute_tolerance", absolute_tolerance),
    ):
        if (
            not isinstance(value, (int, float))
            or isinstance(value, bool)
            or not math.isfinite(value)
            or value < 0
        ):
            errors.append(f"comparison.{name} must be finite and non-negative")

    numeric = comparison.get("numeric_fields")
    if not isinstance(numeric, dict) or set(numeric) != set(NUMERIC_FIELDS):
        errors.append("comparison.numeric_fields do not match the comparator contract")
        numeric = {}
    for field in NUMERIC_FIELDS:
        differences = numeric.get(field)
        if not isinstance(differences, dict):
            errors.append(f"comparison.numeric_fields.{field} must be an object")
            continue
        expected_difference_fields = {
            "max_absolute_difference",
            "max_relative_difference",
            "max_tolerance_ratio",
        }
        if set(differences) != expected_difference_fields:
            errors.append(
                f"comparison.numeric_fields.{field} fields are incomplete"
            )
            continue
        absolute = differences.get("max_absolute_difference")
        relative = differences.get("max_relative_difference")
        tolerance_ratio = differences.get("max_tolerance_ratio")
        for name, value in (
            ("max_absolute_difference", absolute),
            ("max_relative_difference", relative),
            ("max_tolerance_ratio", tolerance_ratio),
        ):
            if (
                not isinstance(value, (int, float))
                or isinstance(value, bool)
                or not math.isfinite(value)
                or value < 0
            ):
                errors.append(f"comparison.numeric_fields.{field}.{name} is invalid")
        if (
            isinstance(tolerance_ratio, (int, float))
            and math.isfinite(tolerance_ratio)
            and tolerance_ratio > 1.0
        ):
            errors.append(
                f"comparison.numeric_fields.{field}.max_tolerance_ratio exceeds 1"
            )

    artifact_hashes = document.get("artifact_sha256")
    expected_artifacts = {"cpp_readings_csv", "matlab_readings_csv"}
    if not isinstance(artifact_hashes, dict) or set(artifact_hashes) != expected_artifacts:
        errors.append("artifact_sha256 fields are incomplete")
    else:
        for name, digest in artifact_hashes.items():
            if not isinstance(digest, str) or SHA256_PATTERN.fullmatch(digest) is None:
                errors.append(f"artifact_sha256.{name} is not a SHA-256 digest")
        try:
            result_readings_hash = result_receipt["artifacts"][
                "archive_run_readings"
            ]["sha256"]
        except (KeyError, TypeError):
            errors.append(
                "result receipt is missing artifacts.archive_run_readings.sha256"
            )
        else:
            if artifact_hashes["cpp_readings_csv"] != result_readings_hash:
                errors.append(
                    "artifact_sha256.cpp_readings_csv does not match the result receipt"
                )
        for name, path in (
            ("cpp_readings_csv", cpp_csv),
            ("matlab_readings_csv", matlab_csv),
        ):
            if path is not None and artifact_hashes[name] != file_sha256(path):
                errors.append(f"artifact_sha256.{name} hash mismatch")

    source_hashes = document.get("source_sha256")
    source_paths = {
        "identity_ledger": ledger,
        "matlab_exporter": matlab_exporter,
        "comparator": comparator,
    }
    if not isinstance(source_hashes, dict) or set(source_hashes) != set(source_paths):
        errors.append("source_sha256 fields are incomplete")
    else:
        for name, path in source_paths.items():
            expected = file_sha256(path)
            if source_hashes[name] != expected:
                errors.append(f"source_sha256.{name} hash mismatch")
        try:
            result_ledger_hash = result_receipt["inputs"]["identity_ledger"][
                "sha256"
            ]
        except (KeyError, TypeError):
            errors.append(
                "result receipt is missing inputs.identity_ledger.sha256"
            )
        else:
            if source_hashes["identity_ledger"] != result_ledger_hash:
                errors.append(
                    "source_sha256.identity_ledger does not match the result receipt"
                )
    return errors


def main() -> int:
    tools = pathlib.Path(__file__).parent
    default_repo = tools.parent
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo-root", type=pathlib.Path, default=default_repo)
    parser.add_argument(
        "--receipt",
        type=pathlib.Path,
        default=pathlib.Path("docs/data/spectro_matlab_crosscheck_receipt.json"),
    )
    parser.add_argument(
        "--ledger",
        type=pathlib.Path,
        default=pathlib.Path("data/spectro_identity_ledger.csv"),
    )
    parser.add_argument(
        "--matlab-exporter",
        type=pathlib.Path,
        default=pathlib.Path("tools/matlab/export_spectro_crosscheck.m"),
    )
    parser.add_argument(
        "--comparator",
        type=pathlib.Path,
        default=pathlib.Path("tools/compare_spectro_crosscheck.py"),
    )
    parser.add_argument(
        "--result-receipt",
        type=pathlib.Path,
        default=pathlib.Path("docs/data/spectro_result_receipt.json"),
    )
    parser.add_argument("--cpp-csv", type=pathlib.Path)
    parser.add_argument("--matlab-csv", type=pathlib.Path)
    args = parser.parse_args()
    repo = args.repo_root.resolve()

    def from_repo(path: pathlib.Path | None) -> pathlib.Path | None:
        if path is None or path.is_absolute():
            return path
        return repo / path

    args.receipt = from_repo(args.receipt)
    args.ledger = from_repo(args.ledger)
    args.matlab_exporter = from_repo(args.matlab_exporter)
    args.comparator = from_repo(args.comparator)
    args.result_receipt = from_repo(args.result_receipt)
    args.cpp_csv = from_repo(args.cpp_csv)
    args.matlab_csv = from_repo(args.matlab_csv)
    try:
        if (args.cpp_csv is None) != (args.matlab_csv is None):
            raise ValueError("--cpp-csv and --matlab-csv must be provided together")
        with args.receipt.open(encoding="utf-8") as stream:
            document = json.load(stream)
        if not isinstance(document, dict):
            raise ValueError("receipt root must be an object")
        with args.result_receipt.open(encoding="utf-8") as stream:
            result_receipt = json.load(stream)
        if not isinstance(result_receipt, dict):
            raise ValueError("result receipt root must be an object")
        errors = validate(
            document,
            result_receipt=result_receipt,
            ledger=args.ledger,
            matlab_exporter=args.matlab_exporter,
            comparator=args.comparator,
            cpp_csv=args.cpp_csv,
            matlab_csv=args.matlab_csv,
        )
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(f"spectro MATLAB receipt: {error}", file=sys.stderr)
        return 2
    if errors:
        for error in errors:
            print(f"spectro MATLAB receipt: {error}", file=sys.stderr)
        return 1
    print("spectro MATLAB cross-check receipt valid")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
