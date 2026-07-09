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
  --raw "2016_12_09_D810_SFR/NIKON D810_50mm_f5.6_.NEF" \
  --oracle-y-multi "2016_12_09_D810_SFR/Results/NIKON D810_50mm_f5.6__Y_multi.csv" \
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
- Full sagittal/tangential lens MTF. Field-map mode is green-linear CFA SFR
  over Imatest-selected ROIs, not a full lens-characterization replacement.

## Next

Implemented next: field-MTF over all 23 ROIs per aperture from the same
per-file `_Y_multi.csv` oracle tables. `camera_iq sfr --field-map` reuses the
green-linear SFR core for every ROI and emits one JSON field map per
RAW/aperture.

Example:

```bash
camera_iq sfr d800_d810_sfr_2016 \
  --raw "2016_12_09_D810_SFR/NIKON D810_50mm_f5.6_.NEF" \
  --oracle-y-multi "2016_12_09_D810_SFR/Results/NIKON D810_50mm_f5.6__Y_multi.csv" \
  --field-map \
  --out out/sfr_field_f5_6.json
```

Field-map implementation gates and caveats:

- parse all 23 ROI rows and matching MTF rows from one `_Y_multi.csv` batch;
- preserve row number, region, direction label, edge ID, field offsets, ROI, and
  per-ROI advisory MTF50/MTF50P. Per-ROI `CSV summary file` values are emitted
  as basenames only, not absolute Imatest paths;
- report detected orientation from pixels, not from Imatest `L`/`R`/`AL` labels;
  a real f/5.6 probe classified all 23 ROIs as near-vertical in the toolkit
  convention;
- do not make strict center > corner a universal plateau gate. The f/4 oracle
  and toolkit probes are effectively tied with a slight corner win; f/5.6, f/8,
  and f/11 show the expected center-above-corner relationship.

Verified D810 field-map plateau probe:

| Aperture | ROIs | Accepted | Detected orientations | Center MTF50 | Physical-corner max | Center > corner |
|---|---:|---:|---|---:|---:|---|
| f/4 | 23 | 23 | vertical | 0.2000 | 0.2005 | false |
| f/5.6 | 23 | 23 | vertical | 0.2714 | 0.2002 | true |
| f/8 | 23 | 23 | vertical | 0.2218 | 0.1955 | true |
| f/11 | 23 | 23 | vertical | 0.2049 | 0.1831 | true |

future mode can later add an Imatest-replication path: demosaiced/luma pipeline,
gamma/OECF handling, and an advisory absolute tolerance once the processing
model is intentionally aligned to Imatest.

## D800 Field Replication

Full 9-aperture D800 field-map sweep against the per-file 10-Dec-2016 12:47:10
oracle batch (see the inventory's D800 Oracle Contract). All 207 ROI
measurements (23 ROIs x 9 apertures) accepted; all detected orientations
vertical, matching the D810 chart geometry.

| Aperture | Oracle center | Toolkit center | Delta | Toolkit corner max | Center > corner (oracle / toolkit) | Argmax N (oracle / toolkit) |
|---|---:|---:|---:|---:|---|---|
| f/1.4 | 0.1029 | 0.1082 | +0.0053 | 0.0978 | true / true | 1 / 1 |
| f/1.8 | 0.1204 | 0.1304 | +0.0100 | 0.1058 | true / true | 1 / 1 |
| f/2 | 0.1377 | 0.1439 | +0.0062 | 0.1113 | true / true | 1 / 1 |
| f/2.8 | 0.1395 | 0.1447 | +0.0052 | 0.1535 | true / false | 12 / 8 |
| f/4 | 0.1385 | 0.1428 | +0.0043 | 0.1885 | false / false | 12 / 12 |
| f/5.6 | 0.1649 | 0.1647 | -0.0002 | 0.1886 | false / false | 12 / 12 |
| f/8 | 0.1831 | 0.1684 | -0.0147 | 0.1849 | true / false | 12 / 12 |
| f/11 | 0.1707 | 0.1674 | -0.0033 | 0.1592 | true / true | 12 / 12 |
| f/16 | 0.1583 | 0.1478 | -0.0105 | 0.1367 | true / true | 1 / 13 |

Findings (the D810 recipe does NOT transfer; gates are camera/capture-specific):

- **Aperture-trend gate fails on D800 — correctly.** Oracle plateau minimum
  (f/4 = 0.1385) sits BELOW f/16 (0.1583); the toolkit sweep agrees
  (0.1428 < 0.1478). This is real behavior of the 2016 capture (f/4 center is
  depressed to f/2.8 level), not an estimator bug. The failure is unit-pinned
  so the gate stays honest about being capture-specific.
- **Field tilt: the field maximum is N=12 (grid 0,-2, top-center) at
  f/2.8-f/11 in BOTH oracle and toolkit.** Wide open (f/1.4-f/2) the center is
  the field max. Compare D810, where N=13 (grid 0,2) won everywhere.
- **Center-above-corner is only gate-worthy at f/8 and f/11 (oracle);
  f/4 and f/5.6 have the corner max ABOVE center** (oracle and toolkit agree)
  and stay diagnostic. f/2.8 is too marginal to pin (+0.0031 oracle margin,
  toolkit disagrees).
- **Toolkit reads physical-corner MTF50 higher than the Imatest oracle**
  (green-linear vs luma+gamma pipelines diverge more off-axis); this flips the
  center-corner comparison at f/2.8 and f/8. Center deltas stay within
  +/-0.015. Absolute agreement remains advisory-only, as established.
- D800 centers sit below D810 centers at matched plateau apertures
  (f/5.6: 0.1649 vs 0.2400 oracle), directionally consistent with the D800's
  OLPF, but the gap cannot be attributed to the OLPF alone (focus/field state
  differs between the captures).

Unit pins: D800 f/8 23-row fixture (all four `_C` corners Region-labeled
`Pt Way` — harsher label trap than D810), `summarize_imatest_field_mtf` pins
(argmax N=12, center 0.1831 > corner max 0.1618), the four-aperture
center-corner block, and the pinned trend-gate FAILURE.
