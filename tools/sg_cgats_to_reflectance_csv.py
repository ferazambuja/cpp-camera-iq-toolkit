#!/usr/bin/env python3
"""Convert an i1Pro/PatchTool spectral CGATS export to the canonical reflectance
CSV form (patch_id,380,390,...,730) consumed by `camera_iq spectral-smi
--reflectance-csv` and read_spectral_reference_csv.

Handles the old-Mac CR-only line endings and the paired SPECTRAL_NM/SPECTRAL_DEC
column layout that the stock CGATS reader does not. Wavelengths are taken from
the SPECTRAL_NM values in each data row; reflectance from SPECTRAL_DEC.

Usage: python3 tools/sg_cgats_to_reflectance_csv.py <in.cgats> <out.csv>
"""
import csv
import sys


def normalize_lines(raw):
    # CR-only, CRLF or LF -> logical lines.
    return raw.replace("\r\n", "\n").replace("\r", "\n").split("\n")


def main(argv):
    if len(argv) != 3:
        print(__doc__, file=sys.stderr)
        return 2
    with open(argv[1], "r", errors="replace") as f:
        lines = normalize_lines(f.read())

    fmt = []
    in_fmt = in_data = False
    rows = []
    for ln in lines:
        s = ln.strip()
        if s == "BEGIN_DATA_FORMAT":
            in_fmt = True
            continue
        if s == "END_DATA_FORMAT":
            in_fmt = False
            continue
        if s == "BEGIN_DATA":
            in_data = True
            continue
        if s == "END_DATA":
            in_data = False
            continue
        if in_fmt:
            fmt = ln.split("\t")
            fmt = [c.strip() for c in fmt if c.strip()]
        elif in_data and s:
            rows.append([c.strip() for c in ln.split("\t")])

    if not fmt or not rows:
        print("ERROR: no DATA_FORMAT or DATA rows found", file=sys.stderr)
        return 1

    # Positions of the id column and the paired NM/DEC columns.
    id_idx = fmt.index("SAMPLE_NAME") if "SAMPLE_NAME" in fmt else \
        fmt.index("SAMPLE_ID")
    nm_idx = [i for i, c in enumerate(fmt) if c == "SPECTRAL_NM"]
    dec_idx = [i for i, c in enumerate(fmt) if c == "SPECTRAL_DEC"]
    if len(nm_idx) != len(dec_idx) or not nm_idx:
        print("ERROR: unpaired SPECTRAL_NM/SPECTRAL_DEC", file=sys.stderr)
        return 1

    wavelengths = [int(round(float(rows[0][i]))) for i in nm_idx]
    with open(argv[2], "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["patch_id"] + [str(nm) for nm in wavelengths])
        for r in rows:
            pid = r[id_idx] if id_idx < len(r) and r[id_idx] else \
                f"P{rows.index(r) + 1}"
            w.writerow([pid] + [r[i] for i in dec_idx])
    print(f"wrote {argv[2]}: {len(rows)} patches x {len(wavelengths)} bands "
          f"({wavelengths[0]}-{wavelengths[-1]} nm)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
