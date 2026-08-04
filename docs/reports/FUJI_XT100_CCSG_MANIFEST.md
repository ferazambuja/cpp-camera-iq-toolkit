# Fujifilm X-T100 ColorChecker-SG Dataset Manifest

Before color or image-quality results can be trusted, the archive has to answer
basic questions: which files belong to the study, what camera state they record,
and which dates or filenames are reliable enough to organize them. This report
establishes that dataset boundary. It inventories the retained material and its
caveats; it does not calculate a color, noise, or sharpness result.

Dataset: private local copy of an archived Fujifilm X-T100 ColorChecker-SG
validation capture set. The dataset is **not** distributed with this
repository; paths below are relative to the dataset root.

[Documentation index](../README.md) ·
[RAW implementation companion](../implementation/raw-foundation.md)

## Scope

The machine-readable manifest records file, metadata, exposure-series, and
provenance checks while keeping source paths private.

## Method

The manifest records, per file: relative path, size, filesystem mtime from the
local/imported file copy, filename-encoded exposure metadata
(`<Group>_f<aperture>_1:<shutter-denominator>[_ISO<iso>]_DSCF<frame>.RAF`),
Decoded EXIF (make/model/ISO/shutter/aperture/camera-clock timestamp), derived
CFA pattern, black/white levels, and CSV shape probes. Supplementary `.mat`
inspection was done with a Python helper; results are recorded below.

## Dataset enumeration

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
- CFA pattern decoded independently for every file: **RGGB for all 480** —
  standard Bayer, not X-Trans.
- Decoded white level: 16383 (14-bit).

## Filename ↔ EXIF cross-check

Zero mismatches across all 480 RAFs (shutter within 5 % relative, aperture
within 0.11, ISO exact). Filename-encoded exposure metadata is trustworthy.

## Dataset caveats found

1. **Camera-clock dates are not capture-date authority.** EXIF timestamps span
   2020-03-09 → 2020-03-20 for the archived set, but the camera clock was not
   independently controlled. Filesystem mtimes support local file provenance
   and deterministic ordering, not capture dating; this retained copy carries
   2026 mtimes from a later archive transfer. Use EXIF for camera controls and
   rough within-session ordering only when independently consistent.

   The configured capture year comes from the owner-assigned archival label
   naming the year and course. The camera timestamps are consistent with that
   label but are not its authority.
2. **The repeating black metadata, not the scalar alone, carries the pedestal**
   (resolved). The scalar and per-color metadata are zero on this camera, while
   a repeating 2×2 metadata block supplies 1024 DN at all four CFA positions.
   The decoded effective result is therefore **black = 1024 DN** across RGGB.
   A sampled dark frame
   (`Dark_Frame_f8.0_1:1000_DSCF0437.RAF`, mean ≈ **1024 DN**, min 1005)
   independently confirms it. Sensor-domain statistics and the demosaic
   baseline subtract that decoder-derived pedestal directly; the 21 dark
   frames remain a cross-check, not the sole source.
3. **`PRD_SPD_all.csv` has 46 rows for 45 measurements** — the last row is an
   exact duplicate of row 45 (`PRD_47`). `XYZ_all.csv` (45 rows) is consistent.
   The `.mat` files are the source of truth; the combined CSVs are derived and
   partially stale.

## Exposure-series candidates

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

The sphere series vary shutter time under a nominally fixed integrating-sphere
illumination, so they contain candidate exposure ladders. This manifest does
not inspect pixel-level framing or illumination stability and therefore does
not establish PTC/OECF suitability by itself. The `Images/PRD` group is
deliberately *not* a series: 23 frames all at f9, 1/30 s, ISO 200.

## PRD relationship

**Classification: PRD-scene-only reference.** Not valid as a ColorChecker-SG
capture illuminant reference. No fabricated links.

What was recovered:

- **Wavelength axis:** each `.mat` carries `measurements.wl` = 380–780 nm at
  2 nm (201 points), matching the 201 CSV columns. The commented-out header
  line in `create_single_file.m` explains why the CSVs lack the axis.
- **Row labels fully mapped:** `PRD measurments copy/` is an exact-content alias
  directory for the same 45 measurements under their original scene names.
  Exact radiance matching proves `PRD1sceneK → PRD_(2K−1)` and
  `PRD2sceneK → PRD_2K`:
  **two spectroradiometer readings per scene, 24 scenes**, with scenes 22–24
  missing the second reading (hence no `PRD_44/46/48`).
- **Measurement content:** spot radiance (W·sr⁻¹·m⁻²·nm⁻¹ scale), XYZ
  (Y ≈ 290–330 for sampled scenes), CCT ≈ 5545 K, Duv ≈ −0.001 for `PRD_01` —
  scene measurements under a daylight-like source, not a chart-illuminant
  characterization.
- **Open pairing question:** 23 PRD RAFs vs 24 measured scenes; frame
  numbers have gaps (0314, 0320, 0324, 0329, 0333, 0337 missing). RAF↔scene
  pairing needs visual/scene inspection, not filename arithmetic.

## Additional finding — `Old/` patch measurement set

`Old/1 to 6`, `Old/7 to 9`, `Old/10 to 15` hold `patch_<N>trail_<M>.mat`
spectroradiometer readings: patches 1–15 measured in triplicate (duplicate for
7–9), averaged by `Old/load_all.m` into `Old/SPD_all.csv` (wl header + 15 rows)
and `Old/XYZ_all.csv` (16 rows; Y ≈ 165–692 cd/m²; the 16th row's provenance is
unclear — `Old/prd/` holds `prd_1.mat` and `prd_2.mat`, and
`Old/Old code/patch_data.m` averages that pair).
**PRD-like scene-domain data, not a reference chart.** Primary evidence is
measured, not inferred from scripts: **all 42** `patch_<N>trail_<M>` trials
best-match a PRD scene with correlation **≥ 0.97** (weakest 0.9695 @
`patch_10trail_3`) over the identical 380–780 nm @ 2 nm axis, and every trail
carries the PRD `measurements` struct (radiance/wl/XYZ). Adjacent scripts
corroborate: `load_all.m` averages the `patch_<N>trail_<M>` trials into
`SPD_all.csv`/`XYZ_all.csv`, and `Old/Old code/patch_data.m` builds a `prd_avg`
from `prd_1.mat`/`prd_2.mat` with a **commented** `% prd_3 =
load("patch_15trail_3.mat")` line. So `Old/patch_*` is an earlier
scene-radiance set (15 scenes), **not** a ColorChecker/paint reference chart and
not Reference-B data. Exact scene identity vs the final PRD set is unresolved
(numbering differs).

## Evidence status

The file counts and metadata observations above can be recomputed from the
retained archive. The current inventory records a 1024 DN effective black level
for all 480 RAF files after unpacking.
- MAT/PRD observations (wavelength axis, scene→numbered mapping over **all 45** files,
  CSV duplicate row, Old/patch reclassification) were verified during the
  dataset-inventory pass and are summarized here without publishing the source
  measurements.

## Enumeration caveat

- Group classification is **filename-only**. `Images/Dark Frame/DSCF0497.RAF`
  has a bare name (no `Dark_Frame_` prefix), so it is fully enumerated
  (directory + size + EXIF) but its `filename_meta.group` is null — the group
  census reports 20 dark frames though the folder holds 21. The physical count
  (21) stands; directory-fallback grouping is a candidate refinement, not a data
  gap.

## Interpretation limits

- The report contains no color accuracy, noise, PTC, or ΔE calculations.
- Original project outputs remain comparison-only and are not used as
  correctness oracles.
- The course-project capture campaign predates this repository; the repository
  reprocesses the archived RAW files.

## Relationship to scientific analyses

This manifest is the dataset/provenance foundation for the implemented RAW/CFA,
demosaic, dark calibration/noise, exposure/OECF, ColorChecker extraction, and
CCM reports. The [documentation index](../README.md) separates those completed
analyses from the remaining calibration gaps: electron gain/read noise, full
well, engineering dynamic range, exact ISO conformance, and blind chart
localization.

## Engineering companion

The [RAW implementation companion](../implementation/raw-foundation.md)
explains how the archive inventory enters the C++ analysis and routes readers
to the public source and tests.
