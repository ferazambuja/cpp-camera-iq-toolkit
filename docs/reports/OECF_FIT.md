# Evidence Report - Relative OECF Fit

Date: 2026-07-04
Tool: `camera_iq oecf-fit` (this repository, v0.1.0)
Dataset: private local RAW captures used only for validation. Source RAW files
are not distributed with this repository.

## Scope

This slice adds a first sensor-linearity fit over the existing
exposure-response chain:

- Reuse exposure-series grouping, post-`unpack()` black subtraction, active-area
  ROI handling, ROI uniformity, lower-bound signal, near-white, saturation, and
  EXIF-consistency gates.
- Fit black-subtracted mean CFA signal versus relative exposure
  (`shutter_s / fastest_usable_shutter_s`) for each CFA plane.
- Emit slope, intercept, R-squared, max nonlinearity percent, per-point fitted
  signal, and residuals.

This is not ISO 14524 conformance. It is not PTC, read-noise, dark-current,
dynamic-range, reflectance, or color accuracy analysis.

## Scientific Handling

- The signal entering the fit is already black-subtracted by `raw-stats` /
  `exposure-response`.
- Only `usable_oecf` points reach the fit. That inherited gate requires
  positive mean signal above black in every CFA plane, mean below 98% of the
  black-subtracted sensor range, less than 1% saturated pixels, matching EXIF
  controls, and ROI uniformity when an ROI was measured.
- A series needs at least three usable shutter points before any plane fit is
  emitted.
- Relative exposure is anchored at the fastest usable shutter in the selected
  series. The fit intercept is left free and reported as a black-subtraction
  sanity check; it is not forced to zero.
- `max_nonlinearity_pct` is `max(abs(residual)) / fitted_signal_range * 100`
  over the usable fit points for that plane.
- The JSON carries both `oecf_candidate` from the readiness layer and
  `fit_candidate` from the fit layer so consumers can distinguish data
  readiness from fit emission.

## Real-Data Validation Run

```bash
./build/camera_iq oecf-fit \
  clrs589_project_camera \
  --subdir "Images/Non_Unifform_f8" \
  --series-min 3 --series-limit 1 \
  --roi 1000,1000,500,500 \
  --out out/nonuniform_f8_roi_oecf_fit.json
```

Result summary:

| Field | Value |
|---|---:|
| Series | Non_unifform, f8 |
| Readable frames | 16 / 16 |
| Usable OECF points | 16 |
| EXIF consistent | true |
| OECF candidate | true |
| Fit candidate | true |
| Relative exposure span | 1.0 to 76.9231 |

Per-plane fit:

| Channel | Slope | Intercept | R-squared | Max nonlinearity |
|---|---:|---:|---:|---:|
| R | 42.8188 | -3.5021 | 0.999669 | 1.4700% |
| G1 | 77.6469 | -6.5990 | 0.999677 | 1.4758% |
| G2 | 77.7836 | -6.5474 | 0.999673 | 1.4855% |
| B | 47.9627 | -3.7047 | 0.999672 | 1.4781% |

The free intercepts sit near zero relative to the full fitted signal span, which
is a useful sanity check for the 1024 DN black subtraction. The high R-squared
and low percent residuals are a relative-linearity result for this manually
selected ROI only. They are not an ISO OECF result because the ROI is not an
identified standard chart patch with reflectance/illumination controls.

## Validation

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Targeted checks:

- `test_oecf_fit` covers a perfectly linear ladder, an injected knee with known
  residuals and nonlinearity percent, inherited exclusion of below-black and
  non-uniform ROI points, the all-CFA-planes-above-black lower-bound gate,
  fewer-than-three usable points, and JSON fields.
- `test_exposure_response` covers the per-point `usable_oecf` gate that feeds
  this fit.
- `camera_iq_tests` verifies that `camera_iq oecf-fit` is routed.
- The real-data validation output was written under `out/`, not tracked in git.

## Not Claimed

- No ISO 14524 OECF conformance result.
- No PTC, temporal noise, read noise, DSNU, PRNU, dark-current, or dynamic-range
  metric.
- No automatic chart/patch detection or reflectance pairing.
- No colorimetric or perceptual image-quality claim.
