#!/usr/bin/env python3
"""Generate data/cie_d50.csv, the CIE D50 relative spectral power distribution.

The 10 nm D50 values below are the standard CIE D50 relative SPD (normalized to
100 at 560 nm). As a correctness gate this script computes the D50 chromaticity
against the committed CIE 1931 2-degree CMF (data/cie1931_2deg_cmf.csv) and
asserts it lands on the known D50 white point (x=0.34567, y=0.35850) within a
10 nm-sampling tolerance. A transcription error in the SPD would move the
chromaticity off the white point and fail the gate.

Usage: python3 tools/gen_cie_d50.py  (writes data/cie_d50.csv)
"""
import csv
import os
import sys

# CIE D50 relative spectral power distribution, 380-730 nm at 10 nm (P(560)=100).
D50 = {
    380: 24.488, 390: 29.871, 400: 49.308, 410: 56.513, 420: 60.034,
    430: 57.818, 440: 74.825, 450: 87.247, 460: 90.612, 470: 91.368,
    480: 95.109, 490: 91.963, 500: 95.724, 510: 96.613, 520: 97.129,
    530: 102.099, 540: 100.755, 550: 102.317, 560: 100.000, 570: 97.735,
    580: 98.918, 590: 93.499, 600: 97.688, 610: 99.269, 620: 99.042,
    630: 95.722, 640: 98.857, 650: 95.667, 660: 98.190, 670: 103.003,
    680: 99.133, 690: 87.381, 700: 91.604, 710: 92.889, 720: 76.854,
    730: 86.511,
}

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CMF_PATH = os.path.join(REPO, "data", "cie1931_2deg_cmf.csv")
OUT_PATH = os.path.join(REPO, "data", "cie_d50.csv")

# Known CIE D50 2-degree white point.
D50_X, D50_Y = 0.34567, 0.35850
TOL = 0.002  # 10 nm sampling vs the 5 nm-derived canonical value


def load_cmf(path):
    cmf = {}
    with open(path, newline="") as f:
        reader = csv.reader(f)
        next(reader)  # header: Wavelength (nm),X,Y,Z
        for row in reader:
            if not row:
                continue
            wl = int(round(float(row[0])))
            cmf[wl] = (float(row[1]), float(row[2]), float(row[3]))
    return cmf


def chromaticity(spd, cmf):
    X = Y = Z = 0.0
    for wl, p in spd.items():
        if wl not in cmf:
            continue
        cx, cy, cz = cmf[wl]
        X += p * cx
        Y += p * cy
        Z += p * cz
    s = X + Y + Z
    return X / s, Y / s


def main():
    cmf = load_cmf(CMF_PATH)
    x, y = chromaticity(D50, cmf)
    print(f"D50 chromaticity from committed CMF: x={x:.5f} y={y:.5f} "
          f"(expected x={D50_X} y={D50_Y})")
    if abs(x - D50_X) > TOL or abs(y - D50_Y) > TOL:
        print("ERROR: D50 chromaticity gate failed; SPD or CMF is inconsistent.",
              file=sys.stderr)
        return 1

    with open(OUT_PATH, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["Wavelength (nm)", "Power"])
        for wl in sorted(D50):
            w.writerow([wl, f"{D50[wl]:.3f}"])
    print(f"wrote {OUT_PATH} ({len(D50)} samples, 380-730 nm @ 10 nm)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
