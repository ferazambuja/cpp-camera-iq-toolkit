# Evidence Report - Exposure Response Readiness

Date: 2026-07-03
Tool: `camera_iq exposure-response` (this repository, v0.1.0)
Dataset: private local RAW captures used only for validation. Source RAW files
are not distributed with this repository.

## Scope

This slice adds the first objective-IQ series layer:

- Detect RAW exposure series from filename metadata across supported LibRaw
  extensions, not only RAF.
- Parse the Nikon D800 archive pattern
  `NIKON D800_i100_s1-40_8.NEF` as group / ISO / shutter / frame metadata.
- Run full-frame or ROI raw-CFA statistics for each selected series frame.
- Emit black-subtracted per-shutter CFA response summaries.
- Optionally restrict the response summary to a CFA-balanced active-area ROI
  with `--roi x,y,width,height`.

This report is the readiness layer, not a final OECF, PTC, read-noise,
dynamic-range, or ISO conformance metric. A later slice adds a relative-exposure
linearity fit in `OECF_FIT.md`, still explicitly outside ISO 14524
conformance.

## Scientific Handling

- Per-frame statistics reuse the post-`unpack()` black subtraction and active-area
  crop from `raw-stats`.
- Series keys remain conservative: directory, group, aperture, and ISO token.
  Missing ISO is not merged with explicit ISO.
- `mean_signal_by_plane` is the average of per-frame black-subtracted CFA means
  at the same shutter.
- `mean_spatial_stddev_by_plane` is named as spatial stddev. It is not temporal
  noise and must not be used as PTC/read-noise evidence.
- ROI mode uses active-area coordinates, clips the requested rectangle to image
  bounds, and rounds inward to an even origin and even dimensions so every
  selected region contains complete 2x2 Bayer blocks. The actual ROI is recorded
  in each frame's JSON as `measurement_roi`.
- ROI mode applies a coarse uniformity readiness gate:
  `max_spatial_stddev_fraction_of_range <= 0.05`, where the denominator is the
  black-subtracted sensor range for each CFA plane. This prevents an obviously
  textured or gradient ROI from being promoted as OECF-ready before chart/patch
  detection exists. It is **not** an ISO uniformity/conformance threshold.
- `oecf_candidate` is only a readiness flag. It requires all selected frames to
  read successfully, at least three shutter points, and at least three usable
  points. A usable point must have positive mean signal above black in every
  CFA plane, max mean signal below 98% of the black-subtracted white range, and
  less than 1% saturated pixels, and must pass the ROI uniformity gate when
  measured over an ROI. The lower bound prevents any at/below-black CFA plane
  from being counted as usable just because another plane is above black; the
  saturation veto catches non-uniform highlight clipping that a spatial mean can
  hide.
- Readable frames must also share EXIF make/model/CFA/ISO/aperture controls;
  shutter is the intended varying field.
- `ptc_candidate` remains false. Photon-transfer/read-noise validation needs
  repeated-frame temporal or per-pixel variance plus ROI/flat-field controls.

## Real-Data Validation Runs

### Rejected saturated sphere ladder

```bash
./build/camera_iq exposure-response \
  clrs589_project_camera \
  --subdir "Images/Sphere" \
  --series-min 3 --series-limit 1 \
  --out out/sphere_exposure_response.json
```

Result summary:

| Field | Value |
|---|---:|
| Series | Sphere, f5.6 |
| Distinct shutters | 18 |
| Readable frames | 18 / 18 |
| Usable OECF points | 0 |
| EXIF consistent | true |
| OECF candidate | false |
| PTC candidate | false |
| First point headroom metric | 0.997752 |
| Last point headroom metric | 0.999870 |

This run is the important negative control: the ladder is reported, but the
near-white plateau is not promoted as OECF-candidate-ready. The same readiness
gate also rejects at/below-black points and heavily clipped points, covered by
synthetic tests.

### Accepted non-uniform f8 response ladder

```bash
./build/camera_iq exposure-response \
  clrs589_project_camera \
  --subdir "Images/Non_Unifform_f8" \
  --series-min 3 --series-limit 1 \
  --out out/nonuniform_exposure_response.json
```

Result summary:

| Field | Value |
|---|---:|
| Series | Non_unifform, f8 |
| Distinct shutters | 16 |
| Readable frames | 16 / 16 |
| Usable OECF points | 16 |
| EXIF consistent | true |
| OECF candidate | true |
| PTC candidate | false |
| First point | 1:1000, max fraction 0.005290 |
| Last point | 1:13, max fraction 0.397036 |

This is a candidate response ladder only. The scene is non-uniform, so the next
OECF step still needs chart/patch selection and reference handling before any
curve or standard metric is claimed.

### Accepted non-uniform f8 response ladder, manual ROI

```bash
./build/camera_iq exposure-response \
  clrs589_project_camera \
  --subdir "Images/Non_Unifform_f8" \
  --series-min 3 --series-limit 1 \
  --roi 1000,1000,500,500 \
  --out out/nonuniform_f8_roi_exposure_response.json
```

Result summary:

| Field | Value |
|---|---:|
| Series | Non_unifform, f8 |
| Distinct shutters | 16 |
| Readable frames | 16 / 16 |
| Usable OECF points | 16 |
| EXIF consistent | true |
| OECF candidate | true |
| PTC candidate | false |
| Actual ROI | x=1000, y=1000, width=500, height=500 |
| First point | 1:1000, max fraction 0.005123 |
| Last point | 1:13, max fraction 0.386026 |
| ROI uniformity checked | true |
| Max ROI stddev / range | 0.021073 |

This proves the ROI plumbing and provenance path on real RAFs. It is still only
a manually selected active-area rectangle, not an identified chart patch or
measured reflectance target.

### Nikon D800 archive parse check

```bash
./build/camera_iq manifest \
  d800_oecf_2016 \
  --no-exif --series-min 3 \
  --out out/d800_manifest_noexif.json
```

Result summary:

| Field | Value |
|---|---:|
| Files scanned | 206 |
| Parsed `NIKON D800_i..._s...` files | 91 |
| Exposure-series candidates | 0 |
| Example parse | ISO 100, `s1-40`, 0.025 s, frame 1 |

This verifies that the parser sees the archive metadata, while the current
fixed-ISO/fixed-aperture series key does not falsely manufacture a shutter
ladder from the D800 folder.

The D800 folder's Imatest Stepchart summaries are planned as a separate oracle
slice, not as a change to this fixed-ISO exposure-series detector. That plan is
`docs/reports/OECF_STEPCHART.md`: the capture set compensates shutter
as ISO changes, so the chart zones supply the rendered-luminance log-exposure
axis and the current `exposure-response` zero-series result remains correct.

## Validation

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Targeted local checks during implementation:

- `test_filename_meta`, `test_exposure_series`, `test_roi`,
  `test_exposure_response`
  passed after red/green tests for NEF parsing, RAW-extension series discovery,
  CFA-balanced ROI handling, odd-origin CFA phase preservation, ROI uniformity
  gating, JSON serialization, missing-frame handling, and near-white plateau
  rejection.
- Real-data validation outputs were written under `out/`, not tracked in git.

## Not Claimed

- No ISO 14524 OECF conformance result. The first non-ISO relative-exposure
  linearity fit is reported separately in `OECF_FIT.md`.
- No PTC, temporal noise, read noise, DSNU, PRNU, or dynamic-range result.
- No claim that full-frame spatial stddev is a noise metric.
- CLRS-589 dark-frame-vs-metadata black reconciliation now exists in
  `DARK_CALIBRATION.md`; this exposure-response slice still does not
  compute dark-current, temporal-noise, PTC, or dynamic-range metrics.
- No automatic chart/patch detection or color-reference pairing yet.
