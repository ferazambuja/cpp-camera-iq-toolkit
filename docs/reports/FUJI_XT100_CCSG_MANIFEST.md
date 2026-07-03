# Evidence Report — CLRS-589 "Project Camera" Dataset Manifest

Date: 2026-07-02
Tool: `camera_iq manifest` (this repository, v0.1.0)
Dataset: private local copy of the 2023 CLRS-589 course "Project Camera" capture
campaign (Fujifilm X-T100). The dataset is **not** distributed with this
repository; paths below are relative to the dataset root.

## Scope

Evidence deliverable per the project plan: one machine-readable dataset manifest
before any analysis claims. This report records what the manifest run verified.
No color-correction, noise, or image-quality numbers are claimed here.

## Method

```bash
./build/camera_iq manifest "<dataset-root>" --out out/clrs589_manifest.json
# scanned 690 files; exif read for 480/480 raw files; 9 exposure-series candidates
# runtime ≈ 0.4 s (metadata-only LibRaw open, no pixel unpack)
```

The manifest records, per file: relative path, size, filename-encoded exposure
metadata (`<Group>_f<aperture>_1:<shutter-denominator>[_ISO<iso>]_DSCF<frame>.RAF`),
LibRaw EXIF (make/model/ISO/shutter/aperture/timestamp), derived CFA pattern,
black/white levels, and CSV shape probes. Supplementary `.mat` inspection was
done with a Python/scipy helper (analysis-side, per plan; results below).

## Enumeration (exit criterion 1: complete)

690 files total: 480 RAF, 150 MAT, 16 CSV, 23 TIF, 8 MATLAB scripts, misc.

| Directory | RAF count |
|---|---|
| `Images/CCSG` | 16 |
| `Images/CCSG_f8` | 17 |
| `Images/Dark Frame` | 21 |
| `Images/Sphere` | 52 |
| `Images/Non_Unifform_f8` | 16 |
| `Images/Flat Image` | 2 |
| `Images/PRD` | 23 |
| `Images/Validation Images` | 18 (15 `Validation_CC`, 3 `Validation_Paint`) |
| `1st Try` | 274 |
| `Old/0418measuremnt` | 41 |
| **Total** | **480** |

`Images/Validation Images/untitled folder` is empty. `Validation_CC` and
`Validation_Paint` are tracked as separate groups throughout.

## Camera and CFA (verified, not hardcoded)

- All 480 RAFs: **Fujifilm X-T100**, 6016×4014, zero sensor margins.
- CFA pattern derived per file via LibRaw `COLOR()`: **RGGB for all 480** —
  standard Bayer, not X-Trans, as the plan required verifying.
- White level (LibRaw `maximum`): 16383 (14-bit).

## Filename ↔ EXIF cross-check

Zero mismatches across all 480 RAFs (shutter within 5 % relative, aperture
within 0.11, ISO exact). Filename-encoded exposure metadata is trustworthy.

## Dataset caveats found

1. **Camera clock is wrong.** EXIF timestamps span 2020-03-09 → 2020-03-20 for
   a 2023 course capture. Relative ordering within a session is usable;
   absolute dates are not.
2. **LibRaw black level needed the `cblack` *tile*, not the scalar** (resolved).
   `color.black` and `cblack[0..3]` are all zero on this camera, but the real
   pedestal lives in LibRaw's repeating black tile: `cblack[4]=2, cblack[5]=2`
   (a 2×2 block) with `cblack[6..9] = 1024`. The manifest reader now combines
   `black + cblack[color] + cblack[6 + (r%bh)*bw + (c%bw)]` and reports
   **black = 1024 DN** across all four RGGB positions. A sampled dark frame
   (`Dark_Frame_f8.0_1:1000_DSCF0437.RAF`, mean ≈ **1024 DN**, min 1005)
   independently confirms it. The Evidence `raw-stats` and `demosaic` paths now
   subtract the LibRaw-derived pedestal directly; the 21 dark frames remain a
   cross-check, not the sole source.
3. **`PRD_SPD_all.csv` has 46 rows for 45 measurements** — the last row is an
   exact duplicate of row 45 (`PRD_47`). `XYZ_all.csv` (45 rows) is consistent.
   The `.mat` files are the source of truth; the combined CSVs are derived and
   partially stale.

## Exposure-series candidates (exit criterion 2: complete)

9 candidates with ≥ 3 distinct shutter values (keyed by directory, filename
group, aperture, ISO token; missing ISO token kept as a separate key on
purpose — EXIF confirms ISO 200 across the set):

| Series | Distinct shutters | Frames |
|---|---|---|
| CCSG f9 ISO200 | 15 | 16 |
| CCSG_f8 f8 | 17 | 17 |
| Dark Frame f8 (two filename conventions) | 13 + 5 | 14 + 6 |
| Non_Unifform f8 | 16 | 16 |
| Sphere f5.6 / f8 / f9 | 18 / 20 / 13 | 18 / 21 / 13 |
| Validation_CC f8 | 14 | 14 |

The sphere series are fixed-illumination by construction (integrating sphere),
so **PTC/OECF feasibility is promising** — pending Phase 2/3 checks on framing
and illumination stability from the pixel data itself. The `Images/PRD` group
is deliberately *not* a series: 23 frames all at f9, 1/30 s, ISO 200.

## PRD relationship (exit criterion 3: classified)

**Classification: PRD-scene-only reference.** Not valid as a ColorChecker-SG
capture illuminant reference. No fabricated links.

What was recovered (stronger than the plan expected):

- **Wavelength axis:** each `.mat` carries `measurements.wl` = 380–780 nm at
  2 nm (201 points), matching the 201 CSV columns. The commented-out header
  line in `create_single_file.m` explains why the CSVs lack the axis.
- **Row labels fully mapped:** `PRD measurments copy/` is not a copy — it holds
  the same 45 measurements under their original scene names. Exact radiance
  matching proves `PRD1sceneK → PRD_(2K−1)` and `PRD2sceneK → PRD_2K`:
  **two spectroradiometer readings per scene, 24 scenes**, with scenes 22–24
  missing the second reading (hence no `PRD_44/46/48`).
- **Measurement content:** spot radiance (W·sr⁻¹·m⁻²·nm⁻¹ scale), XYZ
  (Y ≈ 290–330 for sampled scenes), CCT ≈ 5545 K, Duv ≈ −0.001 for `PRD_01` —
  scene measurements under a daylight-like source, not a chart-illuminant
  characterization.
- **Open pairing question (Phase 5):** 23 PRD RAFs vs 24 measured scenes; frame
  numbers have gaps (0314, 0320, 0324, 0329, 0333, 0337 missing). RAF↔scene
  pairing needs visual/scene inspection, not filename arithmetic.

## Additional finding — `Old/` patch measurement set

`Old/1 to 6`, `Old/7 to 9`, `Old/10 to 15` hold `patch_<N>trail_<M>.mat`
spectroradiometer readings: patches 1–15 measured in triplicate (duplicate for
7–9), averaged by `Old/load_all.m` into `Old/SPD_all.csv` (wl header + 15 rows)
and `Old/XYZ_all.csv` (16 rows; Y ≈ 165–692 cd/m²; the 16th row's provenance is
unclear — `Old/prd/` now holds a single file although the script expects pairs).
**PRD-like scene-domain data, not a reference chart.** Primary evidence is
measured, not inferred from scripts: **all 42** `patch_<N>trail_<M>` trials
best-match a PRD scene with correlation **≥ 0.97** (weakest 0.9695 @
`patch_10trail_3`) over the identical 380–780 nm @ 2 nm axis, and every trail
carries the PRD `measurements` struct (radiance/wl/XYZ). Adjacent scripts
corroborate: `load_all.m` averages the `patch_<N>trail_<M>` trials into
`SPD_all.csv`/`XYZ_all.csv`, and `Old/Old code/patch_data.m` builds a `prd_avg`
from `prd_1.mat`/`prd_2.mat` with a **commented** `% prd_3 =
load("patch_15trail_3.mat")` line — the author treated a patch trail as
interchangeable with a PRD input. So `Old/patch_*` is an earlier scene-radiance
set (15 scenes), **not** a ColorChecker/paint reference chart and not Reference-B
data. Exact scene identity vs the final PRD set is unresolved (numbering differs).
Reproduce with `tools/verify_ccsg_vs_xrite.py --old-patch`.

## Reproducibility

Every claim above is regenerable from the raw dataset:

- Manifest (RAW EXIF, CFA, **black level**, CSV shape, exposure series):
  `camera_iq manifest "<Project Camera>" --out out/clrs589_manifest.json`.
  Regenerated after `ac5ac35`; `black_level` is now **1024** for all 480 RAF.
- MAT/PRD claims (wavelength axis, scene→numbered mapping over **all 45** files,
  CSV duplicate row, Old/patch reclassification): install deps with
  `pip install -r tools/requirements.txt` (numpy, scipy), then
  `python3 tools/verify_ccsg_vs_xrite.py --root "<Project Camera>" --old-patch` → 5/5 pass.

`out/` is git-ignored (derived output over a private dataset path) — regenerate
locally. The black-level logic is independently proven in CI by `test_raw_meta`
(synthetic `cblack` tile → 1024), needing no dataset.

## Manifest tool notes

- Group classification is **filename-only**. `Images/Dark Frame/DSCF0497.RAF`
  has a bare name (no `Dark_Frame_` prefix), so it is fully enumerated
  (directory + size + EXIF) but its `filename_meta.group` is null — the group
  census reports 20 dark frames though the folder holds 21. The physical count
  (21) stands; directory-fallback grouping is a candidate refinement, not a data
  gap.

## Not claimed

- No color accuracy, noise, PTC, or ΔE numbers.
- No claim that the 2023 course outputs are correct (they remain untrusted
  priors, comparison-only).
- No claim of authorship over the original 2023 group capture campaign; this
  project reprocesses the raw data with new, independently written code.

## Status after Evidence slices

Completed follow-on slices now cover:

- LibRaw `unpack()` raw-CFA plane statistics over the active Bayer area.
- Post-`unpack()` black/pitch metadata for maker-dependent RAW formats.
- Active-area-local `cblack` tile handling, matching LibRaw's black subtraction
  convention.
- First hand-written bilinear demosaic summary command.
- Exposure-response readiness grouping with conservative OECF candidate gates.

Remaining objective-IQ work starts from the exposure-response candidates and
still needs measured black reconciliation, chart/patch selection beyond manual
ROI coordinates, OECF fitting, PTC/read-noise handling, and color-reference
pairing before any final camera-IQ metric is claimed.
