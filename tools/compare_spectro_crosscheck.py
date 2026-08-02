#!/usr/bin/env python3
"""Compare MATLAB and C++ readings of the spectroradiometer archive."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import pathlib
import sys


KEY_FIELDS = ("group_id", "measurement_index", "canonical_path")
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
REQUIRED_FIELDS = KEY_FIELDS + HASH_FIELDS + NUMERIC_FIELDS


def read_rows(path: pathlib.Path) -> dict[tuple[str, ...], dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as stream:
        reader = csv.DictReader(stream)
        missing = [field for field in REQUIRED_FIELDS if field not in (reader.fieldnames or [])]
        if missing:
            raise ValueError(f"{path.name}: missing fields: {', '.join(missing)}")
        rows: dict[tuple[str, ...], dict[str, str]] = {}
        for line_number, row in enumerate(reader, start=2):
            key = tuple(row[field] for field in KEY_FIELDS)
            if key in rows:
                raise ValueError(f"{path.name}:{line_number}: duplicate reading key {key}")
            rows[key] = row
    if not rows:
        raise ValueError(f"{path.name}: no readings")
    return rows


def read_ledger_keys(path: pathlib.Path) -> set[tuple[str, ...]]:
    required = ("group_id", "repeat_index", "canonical_path")
    with path.open(newline="", encoding="utf-8") as stream:
        reader = csv.DictReader(stream)
        missing = [field for field in required if field not in (reader.fieldnames or [])]
        if missing:
            raise ValueError(f"{path.name}: missing ledger fields: {', '.join(missing)}")
        keys: set[tuple[str, ...]] = set()
        for line_number, row in enumerate(reader, start=2):
            try:
                measurement_index = str(int(row["repeat_index"]))
            except ValueError as error:
                raise ValueError(
                    f"{path.name}:{line_number}: repeat_index is not an integer"
                ) from error
            key = (row["group_id"], measurement_index, row["canonical_path"])
            if key in keys:
                raise ValueError(f"{path.name}:{line_number}: duplicate ledger key {key}")
            keys.add(key)
    if not keys:
        raise ValueError(f"{path.name}: no ledger readings")
    return keys


def finite_number(text: str, field: str, key: tuple[str, ...]) -> float:
    try:
        value = float(text)
    except ValueError as error:
        raise ValueError(f"{key}: {field} is not numeric") from error
    if not math.isfinite(value):
        raise ValueError(f"{key}: {field} is not finite")
    return value


def file_sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def write_receipt(
    path: pathlib.Path,
    *,
    cpp_path: pathlib.Path,
    matlab_path: pathlib.Path,
    dataset_id: str,
    matlab_release: str,
    ledger_path: pathlib.Path,
    matlab_exporter_path: pathlib.Path,
    reading_count: int,
    relative_tolerance: float,
    absolute_tolerance: float,
    numeric_differences: dict[str, dict[str, float]],
) -> None:
    receipt = {
        "schema": "camera_iq.spectro_matlab_crosscheck.v1",
        "dataset_id": dataset_id,
        "matlab_release": matlab_release,
        "comparison": {
            "result": "match",
            "reading_count": reading_count,
            "hash_fields": list(HASH_FIELDS),
            "exact_hash_comparisons": reading_count * len(HASH_FIELDS),
            "numeric_fields": numeric_differences,
            "numeric_comparisons": reading_count * len(NUMERIC_FIELDS),
            "relative_tolerance": relative_tolerance,
            "absolute_tolerance": absolute_tolerance,
        },
        "artifact_sha256": {
            "cpp_readings_csv": file_sha256(cpp_path),
            "matlab_readings_csv": file_sha256(matlab_path),
        },
        "source_sha256": {
            "identity_ledger": file_sha256(ledger_path),
            "matlab_exporter": file_sha256(matlab_exporter_path),
            "comparator": file_sha256(pathlib.Path(__file__)),
        },
    }
    with path.open("w", encoding="utf-8") as stream:
        json.dump(receipt, stream, indent=2, sort_keys=True)
        stream.write("\n")


def compare(
    cpp_path: pathlib.Path,
    matlab_path: pathlib.Path,
    relative_tolerance: float,
    absolute_tolerance: float,
    *,
    receipt_path: pathlib.Path | None = None,
    dataset_id: str | None = None,
    matlab_release: str | None = None,
    ledger_path: pathlib.Path | None = None,
    matlab_exporter_path: pathlib.Path | None = None,
) -> int:
    if (
        not math.isfinite(relative_tolerance)
        or not math.isfinite(absolute_tolerance)
        or relative_tolerance < 0.0
        or absolute_tolerance < 0.0
    ):
        raise ValueError("tolerances must be finite and non-negative")
    if receipt_path is not None:
        if not all((dataset_id, matlab_release, ledger_path, matlab_exporter_path)):
            raise ValueError(
                "--receipt requires --dataset-id, --matlab-release, --ledger, "
                "and --matlab-exporter"
            )
        protected_inputs = (
            cpp_path,
            matlab_path,
            ledger_path,
            matlab_exporter_path,
            pathlib.Path(__file__),
        )
        if receipt_path.resolve() in {path.resolve() for path in protected_inputs}:
            raise ValueError("--receipt must not overwrite an input")
    cpp = read_rows(cpp_path)
    matlab = read_rows(matlab_path)
    if receipt_path is not None:
        ledger_keys = read_ledger_keys(ledger_path)
        if set(cpp) != ledger_keys:
            raise ValueError("C++ reading keys do not match the ledger")
    failures: list[str] = []
    cpp_keys = set(cpp)
    matlab_keys = set(matlab)
    for key in sorted(cpp_keys - matlab_keys):
        failures.append(f"missing from MATLAB output: {key}")
    for key in sorted(matlab_keys - cpp_keys):
        failures.append(f"missing from C++ output: {key}")

    numeric_differences = {
        field: {
            "max_absolute_difference": 0.0,
            "max_relative_difference": 0.0,
            "max_tolerance_ratio": 0.0,
        }
        for field in NUMERIC_FIELDS
    }
    for key in sorted(cpp_keys & matlab_keys):
        for field in HASH_FIELDS:
            if cpp[key][field] != matlab[key][field]:
                failures.append(
                    f"{key}: {field} differs: C++={cpp[key][field]} "
                    f"MATLAB={matlab[key][field]}"
                )
        for field in NUMERIC_FIELDS:
            cpp_value = finite_number(cpp[key][field], field, key)
            matlab_value = finite_number(matlab[key][field], field, key)
            absolute_difference = abs(cpp_value - matlab_value)
            relative_difference = absolute_difference / max(
                abs(cpp_value), abs(matlab_value), 1e-300
            )
            allowed_difference = max(
                absolute_tolerance,
                relative_tolerance * max(abs(cpp_value), abs(matlab_value)),
            )
            tolerance_ratio = (
                0.0
                if absolute_difference == 0.0
                else (
                    absolute_difference / allowed_difference
                    if allowed_difference > 0.0
                    else math.inf
                )
            )
            numeric_differences[field]["max_absolute_difference"] = max(
                numeric_differences[field]["max_absolute_difference"],
                absolute_difference,
            )
            numeric_differences[field]["max_relative_difference"] = max(
                numeric_differences[field]["max_relative_difference"],
                relative_difference,
            )
            numeric_differences[field]["max_tolerance_ratio"] = max(
                numeric_differences[field]["max_tolerance_ratio"],
                tolerance_ratio,
            )
            if not math.isclose(
                cpp_value,
                matlab_value,
                rel_tol=relative_tolerance,
                abs_tol=absolute_tolerance,
            ):
                failures.append(
                    f"{key}: {field} differs: C++={cpp_value:.17g} "
                    f"MATLAB={matlab_value:.17g}"
                )

    if failures:
        for failure in failures[:20]:
            print(f"spectro cross-check: {failure}", file=sys.stderr)
        if len(failures) > 20:
            print(
                f"spectro cross-check: {len(failures) - 20} more differences",
                file=sys.stderr,
            )
        return 1
    if receipt_path is not None:
        write_receipt(
            receipt_path,
            cpp_path=cpp_path,
            matlab_path=matlab_path,
            dataset_id=dataset_id,
            matlab_release=matlab_release,
            ledger_path=ledger_path,
            matlab_exporter_path=matlab_exporter_path,
            reading_count=len(cpp),
            relative_tolerance=relative_tolerance,
            absolute_tolerance=absolute_tolerance,
            numeric_differences=numeric_differences,
        )
    print(f"spectro cross-check matches: {len(cpp)} readings")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("cpp_csv", type=pathlib.Path)
    parser.add_argument("matlab_csv", type=pathlib.Path)
    parser.add_argument("--relative-tolerance", type=float, default=1e-12)
    parser.add_argument("--absolute-tolerance", type=float, default=1e-12)
    parser.add_argument("--receipt", type=pathlib.Path)
    parser.add_argument("--dataset-id")
    parser.add_argument("--matlab-release")
    parser.add_argument("--ledger", type=pathlib.Path)
    parser.add_argument("--matlab-exporter", type=pathlib.Path)
    args = parser.parse_args()
    try:
        return compare(
            args.cpp_csv,
            args.matlab_csv,
            args.relative_tolerance,
            args.absolute_tolerance,
            receipt_path=args.receipt,
            dataset_id=args.dataset_id,
            matlab_release=args.matlab_release,
            ledger_path=args.ledger,
            matlab_exporter_path=args.matlab_exporter,
        )
    except (OSError, ValueError) as error:
        print(f"spectro cross-check: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
