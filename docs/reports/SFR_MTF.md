# Evidence SFR / MTF

Date: 2026-07-08

## Scope

This slice implements a first slanted-edge SFR/MTF path for the Nikon D810
archive, limited to:

- green-plane linear RAW samples only;
- one explicit center ROI per aperture;
- no demosaic, no luma conversion, and no gamma replication of Imatest;
- Imatest `_Y_multi.csv` values as advisory tier-1 fidelity references only.

The hard validation target is the offset-independent aperture trend, not
absolute equality with Imatest.

## Implementation

New command:

```bash
camera_iq sfr <dataset-root-or-id> \
  --raw "NIKON D810_50mm_f5.6_.NEF" \
  --oracle-y-multi "Results/NIKON D810_50mm_f5.6__Y_multi.csv" \
  --out out/sfr_f5_6.json
```

Core algorithm:

- reads LibRaw active-area, black-subtracted Bayer samples;
- uses only green CFA positions, preserving native sensor pixel coordinates;
- parses the per-file Imatest `_Y_multi.csv` center ROI and translates the
  full-frame ROI into active-area coordinates;
- estimates a slanted edge by per-line 50% crossing plus local
  derivative-centroid refinement;
- bins the projected ESF at 0.25 px, differentiates to an LSF, applies a
  Hamming window, and computes an in-repo DFT;
- applies the adjacent-difference finite-difference correction
  `sinc(pi * f * delta)`, with `delta = 0.25 px`;
- reports MTF50, MTF50P, MTF at sensor Nyquist, R1090, edge angle, saturation,
  and filename/EXIF provenance checks.

## D810 50 mm Center ROI Result

Dataset label: `archive:2016_esensi_images/2016_12_09_D810_SFR/`

Oracle source: the 10-Dec-2016 per-file `Results/NIKON D810_50mm_f...__Y_multi.csv`
files documented in `SFR_MTF_ARCHIVE_INVENTORY.md`. The center ROI is processed
as a near-vertical edge in the toolkit convention.

| Aperture | Accepted | Orientation | Angle deg | Toolkit MTF50 | Imatest MTF50 | Delta | MTF@Nyq | R1090 px | Filename/EXIF checks |
|---|---:|---|---:|---:|---:|---:|---:|---:|---|
| f/1.4 | true | vertical | -6.320 | 0.1074 | 0.1158 | -0.0084 | 0.0210 | 5.142 | pass |
| f/1.8 | true | vertical | -6.320 | 0.0837 | 0.0899 | -0.0062 | 0.0802 | 6.852 | pass |
| f/2 | true | vertical | -6.326 | 0.1085 | 0.1121 | -0.0036 | 0.0271 | 5.311 | pass |
| f/2.8 | true | vertical | -6.428 | 0.1992 | 0.1707 | +0.0285 | 0.0928 | 2.727 | pass |
| f/4 | true | vertical | -6.528 | 0.2000 | 0.1949 | +0.0051 | 0.0894 | 2.639 | pass |
| f/5.6 | true | vertical | -6.423 | 0.2714 | 0.2400 | +0.0314 | 0.1727 | 2.249 | pass |
| f/8 | true | vertical | -6.438 | 0.2218 | 0.2388 | -0.0170 | 0.1682 | 2.703 | pass |
| f/11 | true | vertical | -6.419 | 0.2049 | 0.1989 | +0.0060 | 0.0862 | 3.060 | pass |
| f/16 | true | vertical | -6.431 | 0.1666 | 0.1735 | -0.0069 | 0.0237 | 3.560 | pass |

Trend gate:

```text
min(f/4,f/5.6,f/8,f/11) = 0.2000
f/16                    = 0.1666
max(f/1.4,f/1.8,f/2)    = 0.1085
argmax                  = f/5.6 at 0.2714
```

Result: **PASS**.

## Not Claimed

- Absolute Imatest equivalence. The Imatest archives contain repeat batches with
  material self-disagreement, so absolute deltas are advisory.
- Luma/gamma parity with Imatest. This slice is linear green-plane only.
- lp/mm or LW/PH final reporting. Pixel-pitch and output-unit policy are a later
  reporting layer.
- Field SFR. The per-file Imatest tables expose 23 ROIs, but this slice uses
  only the center ROI.

## Next

future mode can add an Imatest-replication path: demosaiced/luma pipeline,
gamma/OECF handling, and an advisory absolute tolerance once the processing
model is intentionally aligned to Imatest. A later field-MTF slice can use the
remaining 22 ROIs per aperture.
