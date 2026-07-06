# Evidence RAW Patch Extraction

Date: 2026-07-04
Dataset: `clrs589_project_camera`
Command: `camera_iq patches`

## Scope

This slice extracts ColorChecker-SG patch RGB means from a RAW capture using the
toolkit's own LibRaw unpack, black handling, and hand-written bilinear demosaic.
It is a patch-statistics slice, not a full color pipeline replacement yet.

Two coordinate sources are supported:

- `--coords FILE`: four-column checker2colors-style `x,y,width,height` rows,
  interpreted as MATLAB one-based image rectangles.
- `--rawdigger-csv FILE`: RawDigger patch export filtered to the selected RAW
  filename. RawDigger `Left`/`Top` are zero-based and are converted internally to
  the extractor's one-based coordinate convention. `Sample_Name` is emitted as
  `sample_name` in JSON.
- `--sg-corners "x1,y1;x2,y2;x3,y3;x4,y4"`: four ColorChecker-SG outer corners
  in top-left, top-right, bottom-right, bottom-left order. The command uses the
  verified 14x10 SG physical layout and a planar homography to generate 140
  one-based extraction rectangles. This is corner-seeded geometry, not blind
  chart detection.

JSON separates the original coordinate source from the normalized extraction
convention:

- `coordinate_source_format`: e.g. `rawdigger_csv_zero_based_left_top` or
  `checker2colors_csv_one_based_top_left`.
- `extraction_coordinate_convention`:
  `one_based_top_left_rectangles_after_source_conversion`.

## Implemented

- `read_patch_coords_csv()` for simple checker2colors coordinate tables.
- `read_rawdigger_patch_table()` for quoted RawDigger CSV exports containing
  `Filename`, `Sample_Name`, `Left`, `Top`, `Width`, `Height`, `Ravg`, `Gavg`,
  and `Bavg`.
- `extract_patch_means()` over row-major `RgbPixel` images with clipping and
  sample counts.
- `compare_patch_means_to_rgb()` with per-channel Pearson correlation, affine
  slope/intercept, direct RMSE/bias/max-error, and RMSE after affine fit.
- Optional image-domain flat-field correction from a local RAW flat/sphere
  capture: black-subtracted bilinear RGB is multiplied by
  `channel_mean(flat) / flat_pixel`, with an explicit denominator floor and
  clamped-sample count in JSON. The flat normalizer is the per-channel mean of
  valid samples, not the original MATLAB max-based normalization; this avoids a
  single hot or near-clipped sample defining the correction scale. JSON records
  this as `normalization: "per_channel_mean_valid_samples"`.
- Flat-field RAWs are rejected if more than 1% of demosaiced channel samples
  are above 98% of that channel's black-subtracted sensor ceiling, preventing
  clipped/near-clipped flats from producing authoritative-looking corrections.
  JSON records both the measured fraction and the 98% threshold.
- Optional white-balance policy: explicit `--wb-gains R,G,B`, or
  `--wb-from-flat-field`, which anchors the flat/sphere green normalizer and
  scales red/blue to match it.
- Optional `--rgb-csv-out`, producing a three-column camera RGB table that
  `camera_iq ccm-fit --camera-rgb` can consume directly.
- Optional `--sg-corners`, producing generated SG rectangles with
  `coordinate_source_format:
  "colorchecker_sg_corner_seeded_projective_grid"`. JSON records the chart
  model, corner order, input corners, patch IDs, physical row/column, and each
  generated rectangle.
- `camera_iq patches`, producing per-patch JSON and optional comparison / CSV
  output.

## Real-Data Validation

Command:

```bash
./build/camera_iq patches \
  "Images/CCSG_f8/CCSG_f8.0_1:10_DSCF0402.RAF" \
  --dataset clrs589_project_camera \
  --rawdigger-csv Images/CCSG_rawdigger.csv \
  --out /tmp/clrs589_patches_rawdigger.json
```

Result against RawDigger's own exported patch averages:

| Channel | Correlation | Mean direct error | Direct RMSE | Max direct abs error | Affine slope | RMSE after affine |
|---|---:|---:|---:|---:|---:|---:|
| R | 0.999999982 | +0.015 DN | 0.352 DN | 1.224 DN | 0.999982371 | 0.350 DN |
| G | 1.000000000 | -0.003 DN | 0.041 DN | 0.259 DN | 1.000001748 | 0.040 DN |
| B | 0.999999980 | -0.028 DN | 0.381 DN | 1.415 DN | 1.000013457 | 0.379 DN |

The first patch (`A1`) extracted by C++:

```json
{"r":4139.5935268265885,"g":7602.262039919428,"b":4651.185008850638}
```

RawDigger reports `4139.45, 7602.30, 4651.25`. The direct (pre-affine) RMSE is
within 1% of the after-affine RMSE and the signed channel bias is below `0.03`
DN, so the agreement is absolute-DN agreement, not a scale/offset artifact.

## Important Negative Finding

`Images/coord.csv` is valid for the historical MATLAB TIFF/rendered workflow,
but it is **not** a RAW-space coordinate source for LibRaw patch extraction. On
the same RAW series, using `coord.csv` against the RAW image gives only about
`0.30 / 0.31 / 0.36` correlation against `ccsg_matlab.csv`. This is not a color
failure; it is a coordinate-domain mismatch.

Use RawDigger coordinates for RAW-space validation. Treat `ccsg_matlab.csv` as a
historical rendered/TIFF pipeline target until the C++ tool has an explicit
TIFF/flat-field parity path or an automatic chart-localization step.

RawDigger's `Sample_Name` labels are coordinate-grid labels, not the same label
axis as the compatible SG workbook. For the f/8 `1:10` capture, RawDigger row
order starts `A1,A2,...A14,B1...`; the reference workbook order starts
`A1,B1,...N1,A2...`. The current row-order pairing is still the correct
physical sweep: RawDigger green vs MATLAB green corr is **0.99984**, and
RawDigger green vs the reference 560-nm proxy is **0.958** in the current
orientation versus **0.327 / 0.433 / 0.353** for reference-grid column flip,
reference-grid row flip, and 180-degree rotation. Literal RawDigger-label
matching is wrong for this
chart pairing (shared-label corr only **0.407**). Downstream reports therefore
name excluded patches by **reference patch ID**, not RawDigger grid label.
Standalone `spectral-diversity-toolkit` columns named `patch_row` and
`patch_col` are parsed from reference label text and are not authoritative
physical SG geometry.

## Corrected RAW Patch Table Validation

Command:

```bash
./build/camera_iq patches \
  "Images/CCSG_f8/CCSG_f8.0_1:10_DSCF0402.RAF" \
  --dataset clrs589_project_camera \
  --rawdigger-csv Images/CCSG_rawdigger.csv \
  --flat-field-raw "Images/Sphere/Sphere_f8.0_1:1000_DSCF0387.RAF" \
  --wb-from-flat-field \
  --rgb-csv-out /tmp/clrs589_raw_flat_wb_patches.csv \
  --out /tmp/clrs589_raw_flat_wb_patches.json
```

Result:

| Field | Value |
|---|---:|
| patch rows | 140 |
| flat normalization | per-channel mean of valid samples |
| flat normalizer R/G/B | 3240.165 / 5979.162 / 3199.320 DN |
| flat clamped samples | 0 |
| flat near-ceiling samples | 0 / 72.43M |
| flat-derived WB gains R/G/B | 1.845327 / 1.000000 / 1.868886 |
| first patch A1 corrected RGB | 7677.11 / 7639.68 / 8712.55 |

`comparison` is intentionally `null` in this corrected mode unless an explicit
corrected reference RGB table is supplied. RawDigger's `Ravg/Gavg/Bavg` values
are uncorrected RAW rectangle means, so they are valid as a geometry/extraction
oracle only for the uncorrected mode above.

The same-aperture `Sphere_f8.0_1:10` through roughly `1:200` frames are too near
the clipped flat maximum for meaningful vignetting correction. The validation run
uses `Sphere_f8.0_1:1000_DSCF0387.RAF`, whose CFA means are well below the
ceiling and preserve spatial variation. The command now rejects
`Sphere_f8.0_1:10_DSCF0369.RAF` with `flat-field RAW is too close to the sensor
ceiling for correction`.

Same-aperture flat coverage is not available for the f/9 CCSG series in the
local cache. The f/9 sphere folder contains 13 frames (`1:10` through `1:180`);
all 13 are rejected by the near-ceiling guard, including the shortest
exposure, `Sphere_f9.0_1:180_DSCF0400.RAF`. The f/8 folder has usable
same-aperture candidates (`1:500`, two `1:1000` frames, and `1:1600`). This
means the current flat-fielded RAW patch extraction evidence is scoped to
`Images/CCSG_f8`; using an f/8 flat on the f/9 CCSG series would be a
cross-aperture approximation, not a measured same-aperture correction.

## Scientific Boundaries

- The command uses bilinear demosaic only.
- Flat-field correction is multiplicative image-domain correction, not a full
  ISP shading model.
- Flat-field validity assumes a suitable flat for the target optical state; the
  local CLRS-589 cache has usable same-aperture sphere flats for f/8 CCSG only.
- White balance is explicit: either caller-provided gains or the documented
  flat-field green-anchor policy.
- `--rawdigger-csv` validates RAW-space rectangle extraction against RawDigger
  patch means only when no correction is applied; corrected patch tables need a
  corrected reference or downstream CCM/DeltaE evaluation.
- `--coords` supports MATLAB-style rectangle files, but the caller must ensure
  those coordinates belong to the same image domain as the RAW being read.
- `--sg-corners` removes the 140-rectangle dependency but still depends on
  caller-supplied chart corners; there is no blind chart detection yet. The
  corner-seeded path still needs real-data validation against the RawDigger
  oracle before it replaces RawDigger coordinates in evidence reports.

## Next Risks

1. Decide whether to reproduce the historical TIFF workflow for parity or move
   directly to RAW-space chart localization.
2. Validate corner-seeded SG localization against the RawDigger oracle using the
   predeclared absolute geometry and mean-error gates.
3. Diagnose the dark-patch / neutral-axis error before adding higher-order color
   models; root-polynomial variants need held-out evidence before they are
   treated as an improvement.
