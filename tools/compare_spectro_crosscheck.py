#!/usr/bin/env python3
"""Compare MATLAB and C++ readings of the spectroradiometer archive."""

from __future__ import annotations

import argparse
import csv
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


def finite_number(text: str, field: str, key: tuple[str, ...]) -> float:
    try:
        value = float(text)
    except ValueError as error:
        raise ValueError(f"{key}: {field} is not numeric") from error
    if not math.isfinite(value):
        raise ValueError(f"{key}: {field} is not finite")
    return value


def compare(
    cpp_path: pathlib.Path,
    matlab_path: pathlib.Path,
    relative_tolerance: float,
    absolute_tolerance: float,
) -> int:
    cpp = read_rows(cpp_path)
    matlab = read_rows(matlab_path)
    failures: list[str] = []
    cpp_keys = set(cpp)
    matlab_keys = set(matlab)
    for key in sorted(cpp_keys - matlab_keys):
        failures.append(f"missing from MATLAB output: {key}")
    for key in sorted(matlab_keys - cpp_keys):
        failures.append(f"missing from C++ output: {key}")

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
    print(f"spectro cross-check matches: {len(cpp)} readings")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("cpp_csv", type=pathlib.Path)
    parser.add_argument("matlab_csv", type=pathlib.Path)
    parser.add_argument("--relative-tolerance", type=float, default=1e-12)
    parser.add_argument("--absolute-tolerance", type=float, default=1e-12)
    args = parser.parse_args()
    try:
        return compare(
            args.cpp_csv,
            args.matlab_csv,
            args.relative_tolerance,
            args.absolute_tolerance,
        )
    except (OSError, ValueError) as error:
        print(f"spectro cross-check: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
