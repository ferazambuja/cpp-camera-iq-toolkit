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
  slope/intercept, and RMSE after affine fit.
- `camera_iq patches`, producing per-patch JSON and optional comparison output.

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

| Channel | Correlation | Affine slope | RMSE after affine |
|---|---:|---:|---:|
| R | 0.999999982 | 0.999982371 | 0.350 DN |
| G | 1.000000000 | 1.000001748 | 0.040 DN |
| B | 0.999999980 | 1.000013457 | 0.379 DN |

The first patch (`A1`) extracted by C++:

```json
{"r":4139.5935268265885,"g":7602.262039919428,"b":4651.185008850638}
```

RawDigger reports `4139.45, 7602.30, 4651.25`. The sub-DN residuals are
consistent with identical rectangles and only tiny rounding/implementation
differences.

## Important Negative Finding

`Images/coord.csv` is valid for the historical MATLAB TIFF/rendered workflow,
but it is **not** a RAW-space coordinate source for LibRaw patch extraction. On
the same RAW series, using `coord.csv` against the RAW image gives only about
`0.30 / 0.31 / 0.36` correlation against `ccsg_matlab.csv`. This is not a color
failure; it is a coordinate-domain mismatch.

Use RawDigger coordinates for RAW-space validation. Treat `ccsg_matlab.csv` as a
historical rendered/TIFF pipeline target until the C++ tool has an explicit
TIFF/flat-field parity path or an automatic chart-localization step.

## Scientific Boundaries

- The command uses bilinear demosaic only.
- No sphere flat-field/vignetting correction is applied yet.
- `--rawdigger-csv` validates RAW-space rectangle extraction against RawDigger
  patch means; it is not a DeltaE or CCM metric.
- `--coords` supports MATLAB-style rectangle files, but the caller must ensure
  those coordinates belong to the same image domain as the RAW being read.
- There is no automatic chart detection or perspective fitting yet.

## Next Risks

1. Add an explicit flat-field path using sphere/dark frames before using RAW
   patch means as the production CCM input.
2. Decide whether to reproduce the historical TIFF workflow for parity or move
   directly to RAW-space chart localization.
3. Feed validated RAW-space patch means into `ccm-fit` once white balance and
   flat-field policy are explicit.
