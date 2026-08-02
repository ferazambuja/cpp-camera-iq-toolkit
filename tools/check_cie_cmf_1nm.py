#!/usr/bin/env python3
"""Validate data/cie1931_2deg_cmf_1nm.csv against properties of the standard
CIE 1931 2-degree observer.

The committed 10 nm table remains the observer for the chart-reflectance work,
whose references are themselves on a 10 nm grid. This 1 nm table exists for
measurements sampled finer than that: interpolating the 10 nm table up to a 2 nm
spectroradiometer axis under-resolves the short-wavelength z-bar lobe, which is
a property of the table rather than of the measurement.

A transcription or truncation error would move one of the checks below.

Usage: python3 tools/check_cie_cmf_1nm.py [--repo-root PATH]
"""
from __future__ import annotations

import argparse
import csv
import sys
from pathlib import Path

SOURCE = "http://cvrl.ioo.ucl.ac.uk/database/data/cmfs/ciexyz31_1.csv"


def read_table(path: Path) -> dict[int, tuple[float, float, float]]:
    rows = {}
    with path.open(encoding="utf-8") as handle:
        reader = csv.reader(handle)
        next(reader)
        for row in reader:
            rows[int(float(row[0]))] = (
                float(row[1]), float(row[2]), float(row[3])
            )
    return rows


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo-root", default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    root = Path(args.repo_root)

    fine = read_table(root / "data" / "cie1931_2deg_cmf_1nm.csv")
    coarse = read_table(root / "data" / "cie1931_2deg_cmf.csv")
    failures: list[str] = []

    wavelengths = sorted(fine)
    if wavelengths != list(range(360, 831)):
        failures.append("grid is not a complete 1 nm run from 360 to 830 nm")

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

    # The two committed tables must describe the same observer where they meet.
    worst = 0.0
    for wl, values in coarse.items():
        if wl not in fine:
            failures.append(f"{wl} nm is in the 10 nm table but not the 1 nm table")
            continue
        for got, want in zip(values, fine[wl]):
            if want > 1e-6:
                worst = max(worst, abs(got - want) / want)
    if worst > 2e-3:
        failures.append(
            f"the 10 nm and 1 nm tables disagree by {worst:.2%} at a shared wavelength"
        )

    if failures:
        print("CIE 1 nm observer table validation failed:", file=sys.stderr)
        for failure in failures:
            print(f"  {failure}", file=sys.stderr)
        return 1
    print(
        f"CIE 1931 2-degree 1 nm observer valid: {len(fine)} rows, "
        f"agrees with the 10 nm table to {worst:.2%}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
