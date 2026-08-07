#!/usr/bin/env python3
"""Derive the project's 10 nm D65 and CIE 1964 observer tables.

The source files are the unmodified CIE downloads committed under
``data/third_party``.  This script selects 380--730 nm at 10 nm without
interpolation.  The CIE 1964 z-bar source uses ``NaN`` after its defined range;
the source metadata declares zero extrapolation, so the derived table writes
those unavailable values as zero.
"""

from __future__ import annotations

import csv
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE_D65 = ROOT / "data/third_party/CIE_std_illum_D65.csv"
SOURCE_OBSERVER = ROOT / "data/third_party/CIE_xyz_1964_10deg.csv"
OUT_D65 = ROOT / "data/cie_d65.csv"
OUT_OBSERVER = ROOT / "data/cie1964_10deg_cmf.csv"
GRID = set(range(380, 731, 10))


def selected_rows(path: Path) -> list[list[str]]:
    with path.open(encoding="utf-8", newline="") as handle:
        rows = [row for row in csv.reader(handle) if row]
    selected = [row for row in rows if int(row[0]) in GRID]
    if [int(row[0]) for row in selected] != sorted(GRID):
        raise SystemExit(f"{path}: source does not cover the required grid")
    return selected


def write_rows(path: Path, header: list[str], rows: list[list[str]]) -> None:
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.writer(handle, lineterminator="\n")
        writer.writerow(header)
        writer.writerows(rows)


def main() -> int:
    d65 = selected_rows(SOURCE_D65)
    observer = selected_rows(SOURCE_OBSERVER)
    for row in observer:
        if row[3] == "NaN":
            row[3] = "0"
    write_rows(OUT_D65, ["Wavelength (nm)", "Power"], d65)
    write_rows(OUT_OBSERVER, ["Wavelength (nm)", "X", "Y", "Z"], observer)
    print("wrote D65 and CIE 1964 10-degree 380-730 nm / 10 nm tables")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
