#!/usr/bin/env python3
"""Validate the official CIE source copies and project-specific subsets.

The committed 10 nm table remains the observer for the chart-reflectance work,
whose references are themselves on a 10 nm grid. This 1 nm table exists for
measurements sampled finer than that: interpolating the 10 nm table up to a 2 nm
spectroradiometer axis under-resolves the short-wavelength z-bar lobe, which is
a property of the table rather than of the measurement.

The official files are normalized from CRLF to LF when committed. Both the
published source SHA-256 and the normalized-copy SHA-256 are documented in
THIRD_PARTY_NOTICES.md; this guard pins the committed copies and every derived
table, then checks the declared selection and decimal-rounding transformations.

Usage: python3 tools/check_cie_cmf_1nm.py [--repo-root PATH]
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import sys
from pathlib import Path

OFFICIAL_FILES = {
    "observer": (
        Path("data/third_party/CIE_xyz_1931_2deg.csv"),
        "bd7973e895a97e543815614b19c51ceff552ae9910a424724ae04ed89bd863a3",
    ),
    "D50": (
        Path("data/third_party/CIE_std_illum_D50.csv"),
        "1f0ce0e7261c2ac2901d5ac286e7d656400b357a52315f334df4b7548b98632a",
    ),
    "D55": (
        Path("data/third_party/CIE_illum_D55.csv"),
        "89d72e9ce57afb504f5a6de20608f1a713562519dbde248f12e54cb14011518d",
    ),
}

DERIVED_FILES = {
    "1 nm observer": (
        Path("data/cie1931_2deg_cmf_1nm.csv"),
        "8116b60c868fd7844a3a96b9ce2041ce57b3ecc3455a630d11ff054b4ad78c51",
    ),
    "10 nm observer": (
        Path("data/cie1931_2deg_cmf.csv"),
        "9add34b9f47c3d275066d22466b70f23f9ed8bc16a578158dbfb1112585827b7",
    ),
    "D50 subset": (
        Path("data/cie_d50.csv"),
        "c4074f0fea78c473f33f92deb87e57a277aebf57dfdcee3eba6c94c609e9ee6e",
    ),
    "D55 subset": (
        Path("data/cie_d55.csv"),
        "1e49fd3b6f25b6b408d379636305951964f917da935a366b39827454aba09445",
    ),
}


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read_table(path: Path, *, header: bool) -> dict[int, tuple[float, ...]]:
    rows = {}
    with path.open(encoding="utf-8") as handle:
        reader = csv.reader(handle)
        if header:
            next(reader)
        for row in reader:
            rows[int(float(row[0]))] = tuple(float(value) for value in row[1:])
    return rows


def max_abs_difference(
    derived: dict[int, tuple[float, ...]],
    source: dict[int, tuple[float, ...]],
) -> float:
    return max(
        abs(got - want)
        for wavelength, values in derived.items()
        for got, want in zip(values, source[wavelength])
    )


def check(root: Path) -> list[str]:
    failures: list[str] = []

    for label, (relative, expected) in OFFICIAL_FILES.items():
        path = root / relative
        if not path.is_file():
            failures.append(f"official {label} copy is missing")
        elif sha256(path) != expected:
            failures.append(f"official {label} copy SHA-256 does not match")

    for label, (relative, expected) in DERIVED_FILES.items():
        path = root / relative
        if not path.is_file():
            failures.append(f"{label} is missing")
        elif sha256(path) != expected:
            failures.append(f"{label} SHA-256 does not match")

    if failures:
        return failures

    official_observer = read_table(
        root / OFFICIAL_FILES["observer"][0], header=False
    )
    official_d50 = read_table(root / OFFICIAL_FILES["D50"][0], header=False)
    official_d55 = read_table(root / OFFICIAL_FILES["D55"][0], header=False)
    fine = read_table(root / DERIVED_FILES["1 nm observer"][0], header=True)
    coarse = read_table(root / DERIVED_FILES["10 nm observer"][0], header=True)
    d50 = read_table(root / DERIVED_FILES["D50 subset"][0], header=True)
    d55 = read_table(root / DERIVED_FILES["D55 subset"][0], header=True)

    wavelengths = sorted(fine)
    if wavelengths != list(range(360, 831)):
        failures.append("grid is not a complete 1 nm run from 360 to 830 nm")
    elif max_abs_difference(fine, official_observer) > 5.1e-13:
        failures.append("1 nm observer exceeds its declared decimal rounding")

    # y-bar is the photopic luminosity function: exactly 1 at its 555 nm peak.
    peak = max(wavelengths, key=lambda w: fine[w][1])
    if peak != 555 or abs(fine[555][1] - 1.0) > 1e-9:
        failures.append(f"y-bar peak is {fine[peak][1]} at {peak} nm, not 1.0 at 555")

    # The equal-energy stimulus sits on the white point of this observer.
    sums = [sum(fine[w][i] for w in wavelengths) for i in range(3)]
    total = sum(sums)
    for name, value in (("x", sums[0] / total), ("y", sums[1] / total)):
        if abs(value - 1 / 3) > 5e-4:
            failures.append(f"equal-energy {name} is {value:.6f}, not 1/3")

    subset_grid = list(range(380, 731, 10))
    if sorted(coarse) != subset_grid or max_abs_difference(
        coarse, official_observer
    ) > 4.1e-5:
        failures.append("10 nm observer is not the declared rounded subset")
    if sorted(d50) != subset_grid or max_abs_difference(d50, official_d50) > 5.1e-4:
        failures.append("D50 subset is not the declared 380-730 nm rounded selection")
    if sorted(d55) != subset_grid or max_abs_difference(d55, official_d55) != 0.0:
        failures.append("D55 subset is not the declared 380-730 nm selection")

    return failures


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo-root", default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    root = Path(args.repo_root)
    failures = check(root)

    if failures:
        print("CIE 1 nm observer table validation failed:", file=sys.stderr)
        for failure in failures:
            print(f"  {failure}", file=sys.stderr)
        return 1
    print(
        "CIE reference data valid: official source copies and four derived "
        "tables match their declared hashes and transformations"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
