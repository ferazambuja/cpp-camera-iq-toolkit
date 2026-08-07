#!/usr/bin/env python3
"""Validate the official CIE source copies and project-specific subsets.

The committed 10 nm table remains the observer for the chart-reflectance work,
whose references are themselves on a 10 nm grid. This 1 nm table exists for
measurements sampled finer than that: interpolating the 10 nm table up to a 2 nm
spectroradiometer axis under-resolves the short-wavelength z-bar lobe, which is
a property of the table rather than of the measurement.

The third-party notice records whether each committed source copy was retained
byte-for-byte or had line endings normalized. This guard pins those copies and
every derived table, then checks the declared selection and decimal-rounding
transformations.

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
    "D65": (
        Path("data/third_party/CIE_std_illum_D65.csv"),
        "e76f210bffff3d552ef7113025da5f325d5dfec200dd4b878b1a2f3a507032cb",
    ),
    "observer10": (
        Path("data/third_party/CIE_xyz_1964_10deg.csv"),
        "1b27fd4e8ca1167b47c3a6aee3aafe56abc57eae51fa20032cb83704224a27dc",
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
    "D65 subset": (
        Path("data/cie_d65.csv"),
        "20f1b23d861cf2aad426a0e5768e1ef2cf6847751ad220eacea143b1d3a07abc",
    ),
    "10-degree observer": (
        Path("data/cie1964_10deg_cmf.csv"),
        "42aa665a8ab85d80a33fefc1776e92d2cd6cb6584b4ca9ea4b51df16cbacd232",
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
            if not row or not row[0]:
                continue
            rows[int(float(row[0]))] = tuple(float(value) for value in row[1:])
    return rows


def max_abs_difference(
    derived: dict[int, tuple[float, ...]],
    source: dict[int, tuple[float, ...]],
    *,
    source_nan_as_zero: bool = False,
) -> float:
    return max(
        abs(got - (0.0 if source_nan_as_zero and want != want else want))
        for wavelength, values in derived.items()
        for got, want in zip(values, source[wavelength])
    )


def chromaticity(
    illuminant: dict[int, tuple[float, ...]],
    observer: dict[int, tuple[float, ...]],
) -> tuple[float, float]:
    xyz = [
        sum(illuminant[w][0] * observer[w][channel] for w in observer)
        for channel in range(3)
    ]
    total = sum(xyz)
    return xyz[0] / total, xyz[1] / total


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
    official_d65 = read_table(root / OFFICIAL_FILES["D65"][0], header=False)
    official_observer10 = read_table(
        root / OFFICIAL_FILES["observer10"][0], header=False
    )
    fine = read_table(root / DERIVED_FILES["1 nm observer"][0], header=True)
    coarse = read_table(root / DERIVED_FILES["10 nm observer"][0], header=True)
    d50 = read_table(root / DERIVED_FILES["D50 subset"][0], header=True)
    d55 = read_table(root / DERIVED_FILES["D55 subset"][0], header=True)
    d65 = read_table(root / DERIVED_FILES["D65 subset"][0], header=True)
    observer10 = read_table(
        root / DERIVED_FILES["10-degree observer"][0], header=True
    )

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
    if sorted(d65) != subset_grid or max_abs_difference(d65, official_d65) != 0.0:
        failures.append("D65 subset is not the declared 380-730 nm selection")
    if sorted(observer10) != subset_grid or max_abs_difference(
        observer10, official_observer10, source_nan_as_zero=True
    ) != 0.0:
        failures.append(
            "10-degree observer is not the declared selection with zero extrapolation"
        )

    for label, observer, target in (
        ("2-degree", coarse, (0.31272, 0.32903)),
        ("10-degree", observer10, (0.31382, 0.33100)),
    ):
        x, y = chromaticity(d65, observer)
        if abs(x - target[0]) > 2e-4 or abs(y - target[1]) > 2e-4:
            failures.append(
                f"D65/{label} white is ({x:.6f}, {y:.6f}), not the published white"
            )

        sums = [sum(observer[w][i] for w in subset_grid) for i in range(3)]
        total = sum(sums)
        for name, value in (("x", sums[0] / total), ("y", sums[1] / total)):
            if abs(value - 1 / 3) > 5e-4:
                failures.append(
                    f"equal-energy {label} {name} is {value:.6f}, not 1/3"
                )

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
        "CIE reference data valid: official source copies and six derived "
        "tables match their declared hashes, transformations, and white points"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
