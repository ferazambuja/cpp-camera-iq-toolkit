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
- Run `read_raw_cfa_stats()` for each selected series frame.
- Emit black-subtracted per-shutter CFA response summaries.

This is not a final OECF fit, PTC, read-noise, dynamic-range, or ISO conformance
metric.

## Scientific Handling

- Per-frame statistics reuse the post-`unpack()` black subtraction and active-area
  crop from `raw-stats`.
- Series keys remain conservative: directory, group, aperture, and ISO token.
  Missing ISO is not merged with explicit ISO.
- `mean_signal_by_plane` is the average of per-frame black-subtracted CFA means
  at the same shutter.
- `mean_spatial_stddev_by_plane` is named as spatial stddev. It is not temporal
  noise and must not be used as PTC/read-noise evidence.
- `oecf_candidate` is only a readiness flag. It requires all selected frames to
  read successfully, at least three shutter points, and at least three usable
  points whose max mean signal is below 98% of the black-subtracted white range.
- Readable frames must also share EXIF make/model/CFA/ISO/aperture controls;
  shutter is the intended varying field.
- `ptc_candidate` remains false. Photon-transfer/read-noise validation needs
  repeated-frame temporal or per-pixel variance plus ROI/flat-field controls.

## Real-Data Validation Runs

### Rejected saturated sphere ladder

```bash
./build/camera_iq exposure-response \
  "dataset:clrs589_project_camera/2023/CLRS-589.689 Lighting Technology & Perception/Project Camera/Images/Sphere" \
  --series-min 3 --series-limit 1 \
  --out /tmp/camera_iq_sphere_exposure_response.json
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
near-white plateau is not promoted as OECF-candidate-ready.

### Accepted non-uniform f8 response ladder

```bash
./build/camera_iq exposure-response \
  "dataset:clrs589_project_camera/2023/CLRS-589.689 Lighting Technology & Perception/Project Camera/Images/Non_Unifform_f8" \
  --series-min 3 --series-limit 1 \
  --out /tmp/camera_iq_nonuniform_exposure_response.json
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
OECF step still needs ROI/patch selection and reference handling before any
curve or standard metric is claimed.

### Nikon D800 archive parse check

```bash
./build/camera_iq manifest \
  "dataset:archive_backup/Fernando/2_Archive/Disk_2/2016_esensi_images/2016_12_10_D800_OECF" \
  --no-exif --series-min 3 \
  --out /tmp/camera_iq_d800_manifest_noexif.json
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

## Validation

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Targeted local checks during implementation:

- `test_filename_meta`, `test_exposure_series`, `test_exposure_response`
  passed after red/green tests for NEF parsing, RAW-extension series discovery,
  JSON serialization, missing-frame handling, and near-white plateau rejection.
- Real-data validation outputs were written under `/tmp`, not tracked in git.

## Not Claimed

- No ISO 14524 OECF fit or conformance result.
- No PTC, temporal noise, read noise, DSNU, PRNU, or dynamic-range result.
- No claim that full-frame spatial stddev is a noise metric.
- No dark-frame-vs-metadata black reconciliation yet.
