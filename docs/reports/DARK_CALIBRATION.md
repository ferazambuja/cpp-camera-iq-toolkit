# Dark Calibration Reconciliation

Date: 2026-07-03
Tool: `camera_iq dark-calibration` (this repository, v0.1.0)
Dataset: private local RAW captures used only for validation. Source RAW files
are not distributed with this repository.

## Scope

This slice reconciles LibRaw-derived metadata black against measured dark-frame
RAW data:

- Scan a selected dark-frame folder using the existing dataset-ID path privacy
  layer.
- Run post-`unpack()` raw-CFA statistics for each dark-frame candidate.
- Report each frame's signed mean residual after metadata black subtraction.
- Report measured dark raw means as `metadata_black + residual_mean`.
- Count frames inside and outside a configurable residual tolerance.

This is a black-level reconciliation diagnostic. It is not a dark-current model,
DSNU/PRNU result, temporal-noise result, PTC, or dynamic-range metric.

## Scientific Handling

- The tool reuses `read_raw_cfa_stats()`, so black and pitch are read after
  `unpack()` and use the same active-area crop and `cblack` tile handling as
  `raw-stats` and `demosaic`.
- A dark-frame residual near zero means the metadata black subtraction agrees
  with measured dark RAW values for that frame. The reported measured raw dark
  level is the metadata black plus the signed residual mean.
- Dark frames can include dark current, light leaks, capture mistakes, or
  mislabeled files. Therefore this tool does **not** replace metadata black with
  a dark-frame mean. It reports agreement and outliers before later
  noise/dynamic-range work decides which frames are scientifically usable.
- The default tolerance is 2 DN. It is a diagnostic guard for this development
  slice, not an ISO/EMVA threshold.
- Aggregate means are reported two ways: all readable frames, and only frames
  whose per-plane mean residuals stay within tolerance.
- The JSON deliberately separates two questions:
  `all_dark_frames_within_tolerance` answers whether every selected dark frame
  is clean, while `in_tolerance_supports_metadata_black` answers whether the
  clean-frame consensus supports the metadata black level.

## Real-Data Validation Run

```bash
./build/camera_iq dark-calibration \
  clrs589_project_camera \
  --subdir "Images/Dark Frame" \
  --out out/clrs_dark_calibration.json
```

Result summary:

| Field | Value |
|---|---:|
| Candidate frames | 21 |
| Readable frames | 21 |
| Missing reports | 0 |
| Frames within 2 DN | 20 |
| Outlier frames | 1 |
| `all_dark_frames_within_tolerance` | false |
| `in_tolerance_supports_metadata_black` | true |
| Metadata black by plane | [1024, 1024, 1024, 1024] DN |
| In-tolerance metadata black mean | [1024, 1024, 1024, 1024] DN |
| In-tolerance residual mean | [0.0207, 0.1749, 0.1841, 0.2235] DN |
| In-tolerance measured dark raw mean | [1024.0207, 1024.1749, 1024.1841, 1024.2235] DN |
| All-frame residual mean | [2.1516, 4.0292, 4.0441, 2.5923] DN |

Interpretation: 20 of the 21 dark-frame candidates independently support the
1024 DN pedestal recovered from LibRaw's `cblack` tile. The all-frame mean is
dominated by one outlier, so `all_dark_frames_within_tolerance` is false, but
`in_tolerance_supports_metadata_black` is true.

Outlier:

| File | Shutter | Max abs residual | Residuals by plane |
|---|---:|---:|---:|
| `Dark_Frame_f8.0_1:1000_DSCF0434.RAF` | 1:1000 | 81.2448 DN | [44.770, 81.115, 81.245, 49.969] DN |

The outlier should not be used as a black/noise/dynamic-range calibration frame
until the capture provenance is resolved. The tool keeps it visible instead of
silently discarding it.

## Validation

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Targeted checks:

- `test_dark_calibration` covers missing reports, signed residual averaging,
  measured raw dark reconstruction, outlier counts, in-tolerance aggregates, the
  strict all-frame flag, the consensus metadata-black support flag, and JSON
  fields.
- `camera_iq_tests` verifies the CLI command is routed.
- The real-data validation output was written under `out/`, not tracked in git.

## Not Claimed

- No dark-current slope, DSNU, PRNU, read-noise, PTC, or dynamic-range metric.
- No automatic decision that the outlier is a bad capture; only that it is not
  consistent with the rest of this dark-frame set at the configured tolerance.
- No reconciliation for every archive camera body yet; this report validates the
  CLRS-589 Fujifilm X-T100 dark-frame set used by the first public slices.
