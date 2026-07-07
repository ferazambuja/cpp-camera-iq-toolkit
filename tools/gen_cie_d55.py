#!/usr/bin/env python3
"""Generate data/cie_d55.csv from the official CIE D55 relative SPD.

The values below are the 380-730 nm, 10 nm subset of the open CIE dataset
`CIE_illum_D55.csv`:
  https://cie.co.at/datatable/relative-spectral-power-distributions-cie-illuminant-d55
  DOI: 10.25039/CIE.DS.qewfb3kp
  source checksum: 8b5678358f265567a0759b9800f17d7f

The output shape intentionally matches data/cie_d50.csv because the toolkit's
current CMF and reflectance inputs are 380-730 nm at 10 nm. As a transcription
guard, this script computes the D55 chromaticity against the committed CIE 1931
2-degree CMF and checks it against the known D55 white point.

Usage: python3 tools/gen_cie_d55.py  (writes data/cie_d55.csv)
"""
import csv
import os
import sys


D55 = {
    380: 32.584, 390: 38.087, 400: 60.949, 410: 68.554, 420: 71.577,
    430: 67.914, 440: 85.605, 450: 97.993, 460: 100.463, 470: 99.913,
    480: 102.739, 490: 98.078, 500: 100.680, 510: 100.695, 520: 99.987,
    530: 104.210, 540: 102.102, 550: 102.968, 560: 100.000, 570: 97.216,
    580: 97.749, 590: 91.432, 600: 94.419, 610: 95.140, 620: 94.220,
    630: 90.448, 640: 92.330, 650: 88.854, 660: 90.317, 670: 93.950,
    680: 89.956, 690: 79.677, 700: 82.840, 710: 84.844, 720: 70.235,
    730: 79.301,
}

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CMF_PATH = os.path.join(REPO, "data", "cie1931_2deg_cmf.csv")
OUT_PATH = os.path.join(REPO, "data", "cie_d55.csv")

# CIE D55 2-degree white point, allowing 10 nm sampling tolerance.
D55_X, D55_Y = 0.33242, 0.34743
TOL = 0.002


def load_cmf(path):
    cmf = {}
    with open(path, newline="") as f:
        reader = csv.reader(f)
        next(reader)
        for row in reader:
            if not row:
                continue
            wl = int(round(float(row[0])))
            cmf[wl] = (float(row[1]), float(row[2]), float(row[3]))
    return cmf


def chromaticity(spd, cmf):
    x = y = z = 0.0
    for wl, power in spd.items():
        if wl not in cmf:
            continue
        cx, cy, cz = cmf[wl]
        x += power * cx
        y += power * cy
        z += power * cz
    total = x + y + z
    return x / total, y / total


def main():
    cmf = load_cmf(CMF_PATH)
    x, y = chromaticity(D55, cmf)
    print(f"D55 chromaticity from committed CMF: x={x:.5f} y={y:.5f} "
          f"(expected x={D55_X} y={D55_Y})")
    if abs(x - D55_X) > TOL or abs(y - D55_Y) > TOL:
        print("ERROR: D55 chromaticity gate failed; SPD or CMF is inconsistent.",
              file=sys.stderr)
        return 1

    with open(OUT_PATH, "w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(["Wavelength (nm)", "Power"])
        for wl in sorted(D55):
            writer.writerow([wl, f"{D55[wl]:.3f}"])
    print(f"wrote {OUT_PATH} ({len(D55)} samples, 380-730 nm @ 10 nm)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
