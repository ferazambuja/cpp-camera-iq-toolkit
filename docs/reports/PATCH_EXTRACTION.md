# RAW Patch Extraction

Date: 2026-07-04
Dataset: `clrs589_project_camera`
Command: `camera_iq patches`

## Scope

The command extracts ColorChecker-SG patch RGB means from a RAW capture using the
toolkit's own LibRaw unpack, black handling, and hand-written bilinear demosaic.
It is the patch-statistics component of the documented extraction-to-CCM
pipeline.

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
  single hot or near-ceiling sample defining the correction scale. JSON records
  this as `normalization: "per_channel_mean_valid_samples"`.
- Flat-field RAWs are screened before demosaic in the black-subtracted CFA
  domain. The command uses the same CFA-balanced center helper and declared
  20% geometry, 98% signal-ceiling level, and 1% policy as `shading`; all four
  mosaic positions are decided independently over both the whole frame and the
  central gate. Any invalid or over-policy value rejects. JSON records both
  four-position arrays, the effective rectangles, and the failing position.
  The correction normalizer remains the full-frame valid-sample mean of each
  demosaiced channel; the center gate protects local headroom, not its scale.
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
- For `--sg-corners` runs with a configured spectral SG reference, JSON emits
  `orientation_validation`: direct physical order plus column-flip, row-flip,
  and 180-degree controls using the same broad luminance/chroma proxy as
  `reference-info`. `orientation_valid` is true only when direct order is the
  best control by minimum luminance/R-G/B-G correlation and passes the
  configured correlation thresholds.
- Optional `--rawdigger-oracle-csv`, allowed only with `--sg-corners`, compares
  generated uncorrected patch means and ROI centers against a RawDigger export
  without using RawDigger as the extraction coordinate source. It records the
  predeclared 140-patch / 5 px / 0.999 correlation / 25 DN gates in
  `localization_validation` and exits nonzero when any hard gate fails.
- `camera_iq patches`, producing per-patch JSON and optional comparison / CSV
  output.

## Real-Data Validation

Command:

```bash
./build/camera_iq patches \
  "Images/CCSG_f8/CCSG_f8.0_1:10_DSCF0402.RAF" \
  --dataset clrs589_project_camera \
  --rawdigger-csv Images/CCSG_rawdigger.csv \
  --out out/clrs589_patches_rawdigger.json
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

## Corner-Seeded Orientation Gate Validation

The orientation gate was validated on the f/8 CCSG RAW using a
RawDigger-derived four-corner seed. The seed was computed from the A1, A14, J14,
and J1 patch centers and then projected back to the SG outer chart corners, so
this is **not** an independent localization result; it verifies the command
wiring, JSON artifact, and direct-vs-flip orientation gate.

Command excerpt:

```bash
./build/camera_iq patches \
  "Images/CCSG_f8/CCSG_f8.0_1:10_DSCF0402.RAF" \
  --dataset clrs589_project_camera \
  --sg-corners "1242.489159,707.131935;4835.468326,692.253409;4816.545845,3254.656481;1252.609404,3220.163201" \
  --flat-field-raw "Images/Sphere/Sphere_f8.0_1:1000_DSCF0387.RAF" \
  --wb-from-flat-field \
  --out out/camera_iq_sg_orientation_validation.json
```

Result:

| Orientation | Aggregate min corr | Luminance corr | R-G proxy corr | B-G proxy corr | Passes thresholds |
|---|---:|---:|---:|---:|---|
| direct | 0.960203 | 0.982774 | 0.960203 | 0.960970 | true |
| column flip | 0.052004 | 0.376739 | 0.248921 | 0.052004 | false |
| row flip | 0.080909 | 0.491728 | 0.140243 | 0.080909 | false |
| 180 rotation | -0.280493 | 0.396087 | -0.211692 | -0.280493 | false |

The output reports `orientation_valid: true` and `best_orientation: "direct"`.

## RawDigger Oracle Localization Gate

The RawDigger-oracle validation path was added, but the first f/8 `1:10`
corner-seeded run **does not pass** the predeclared geometry gate. With corners
derived from RawDigger A1/A14/J14/J1 centers, the command writes
`out/camera_iq_rawdigger_oracle_validation.json` and exits `1` because:

- patch count: 140, pass
- max center error: **16.449 px**, fail against the 5 px gate
- per-channel correlations: all >= 0.999, pass
- max absolute RGB mean error: R 12.169 DN, G 20.482 DN, B 11.554 DN, pass
- orientation: direct, pass

This confirms the orientation and mean extraction are close, but the generated
projective grid fails the 5 px center-error gate and is not used as a
replacement for RawDigger rectangles. See
`docs/reports/RAW_CHART_LOCALIZATION.md`. That report records
the serialized per-patch residuals: the four corner patches are pinned near
zero, while the middle columns bow by about 15 px relative to RawDigger. The
miss is therefore a systematic geometry/model mismatch, not a coordinate-origin
artifact or simple global offset.

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
  --rgb-csv-out out/clrs589_raw_flat_wb_patches.csv \
  --out out/clrs589_raw_flat_wb_patches.json
```

Result:

| Field | Value |
|---|---:|
| patch rows | 140 |
| flat normalization | per-channel mean of valid samples |
| flat normalizer R/G/B | 3240.165 / 5979.162 / 3199.320 DN |
| flat clamped samples | 0 |
| flat near-ceiling fractions, R/G1/G2/B, full frame | 0 / 0 / 0 / 0 |
| flat near-ceiling fractions, R/G1/G2/B, centered 20% gate | 0 / 0 / 0 / 0 |
| flat-derived WB gains R/G/B | 1.845327 / 1.000000 / 1.868886 |
| first patch A1 corrected RGB | 7677.11 / 7639.68 / 8712.55 |

`comparison` is intentionally `null` in this corrected mode unless an explicit
corrected reference RGB table is supplied. RawDigger's `Ravg/Gavg/Bavg` values
are uncorrected RAW rectangle means, so they are valid as a geometry/extraction
oracle only for the uncorrected mode above.

The same-aperture `Sphere_f8.0_1:10` through `1:500` frames are too near the
signal ceiling for meaningful flat-field correction. The validation run
uses `Sphere_f8.0_1:1000_DSCF0387.RAF`, whose CFA means are well below the
ceiling and preserve spatial variation. The command rejects
`Sphere_f8.0_1:10_DSCF0369.RAF` with `flat-field RAW is too close to the sensor
ceiling for correction`.

The guard measures two regions, not one. A whole-frame near-ceiling fraction
cannot protect a flat whose brightest region is central: the darker surround
keeps the frame-wide fraction small. `Sphere_f8.0_1:500_DSCF0386.RAF` is the
measured case —
[11.6319% of its center gate near ceiling against 0.4964% frame-wide](FLAT_FIELD_RESPONSE.md#the-shared-gate-protects-correction-inputs) —
and the old pooled post-demosaic whole-frame implementation accepted it.

The command now measures the source mosaic before demosaic. A shared helper and
policy constants give `patches` and `shading` the same effective rectangle and
per-position decision. On the `1:500` frame both report:

| Region | R | G1 | G2 | B | Policy |
|---|---:|---:|---:|---:|---:|
| Whole frame | 0% | 0.3664% | 0.4964% | 0% | 1% |
| Centered gate (`x=2406, y=1606, w=1202, h=802`) | 0% | 8.6908% | 11.6319% | 0% | 1% |

The command rejects G2. It emits `status: "rejected"` and returns non-zero;
whether written to stdout or `--out`, the JSON retains the measurement domain,
both rectangles, both four-position arrays, policy, and failing label. Accepted
runs embed the same block under `corrections.flat_field.near_ceiling_gate`.

The documented command's 1/1000 s flat measures 0% near ceiling in every CFA
position in both regions and remains accepted. The correction math after that
gate is unchanged. An archive-backed before/after run produced byte-identical
140-row RGB CSVs: all 420 patch-channel values changed by 0 DN and both files
had SHA-256 `4b8429cdacbb982d33ef56a76e09cc46d8c7aadde927e805e52bc5feec2c8f92`.
Only the feature-side output is committed as
[`ccsg_f8_flat_wb_patches.csv`](../data/ccsg_f8_flat_wb_patches.csv);
equality to the pre-change output remains archive-backed. After rerunning the
command above, current admitted output is directly checkable against that
committed feature baseline with:

```bash
cmp out/clrs589_raw_flat_wb_patches.csv \
  docs/data/ccsg_f8_flat_wb_patches.csv
```

This verifies admitted correction output, not a correction result for the
rejected 1/500 s flat.

Same-aperture flat coverage is not available for the f/9 CCSG series in the
private dataset. The f/9 sphere folder contains 13 frames (`1:10` through `1:180`);
all 13 are rejected by the near-ceiling guard, including the shortest
exposure, `Sphere_f9.0_1:180_DSCF0400.RAF`. The f/8 folder has four
same-aperture candidates: `1:500`, two `1:1000` frames, and `1:1600`. The
shared gate rejects `1:500`, leaving the other three usable. The current
flat-fielded RAW patch extraction evidence is therefore scoped to
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
  orientation gate confirms the direct physical sweep beats flip
  controls, but the first RawDigger-oracle run fails the predeclared 5 px
  center gate, so the corner-seeded path is not used in the reported analysis.

## Open engineering questions

1. Decide whether to reproduce the historical TIFF workflow for parity or move
   directly to RAW-space chart localization.
2. Diagnose why the corner-seeded projective grid misses the RawDigger oracle
   centers by up to 16.449 px before changing any predeclared validation gate.
   The current residual pattern is an interior-column bow, so any
   lens-distortion, chart-geometry, or non-projective placement hypothesis must
   be measured against the serialized residuals.
3. Diagnose the dark-patch / neutral-axis error before adding higher-order color
   models; root-polynomial variants need held-out evidence before they are
   treated as an improvement.

## Implementation and tests

- [`src/patches.cpp`](../../src/patches.cpp)
- [`src/cmd_patches.cpp`](../../src/cmd_patches.cpp)
- [`src/chart_localization.cpp`](../../src/chart_localization.cpp)
- [`tests/test_patches.cpp`](../../tests/test_patches.cpp)
- [`tests/test_cmd_patches.cpp`](../../tests/test_cmd_patches.cpp)
- [Case study](../case-studies/colorchecker-ccm.md)
