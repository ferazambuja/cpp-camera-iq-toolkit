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
- The fit assumes constant illumination and scene radiance across the selected
  shutter ladder. The tool cannot verify light-source stability; illumination
  drift is mathematically indistinguishable from sensor nonlinearity in this
  relative fit.
- `max_nonlinearity_pct` is `max(abs(residual)) / fitted_signal_range * 100`
  over the usable fit points for that plane.
- The JSON carries both `oecf_candidate` from the readiness layer and
  `fit_candidate` from the fit layer so consumers can distinguish data
  readiness from fit emission.

## Real-Data Validation Run

```bash
./build/camera_iq oecf-fit \
  clrs589_project_camera \
  --subdir "Images/Sphere" \
  --series-min 3 --series-limit 3 \
  --roi 1000,1000,500,500 \
  --out out/sphere_roi_oecf_fit.json
```

Result summary:

| Field | Value |
|---|---:|
| Series | Sphere, f8 |
| Readable frames | 21 / 21 |
| Usable OECF points | 4 |
| EXIF consistent | true |
| OECF candidate | true |
| Fit candidate | true |
| Relative exposure span | 1.0 to 4.0 |

Per-plane fit:

| Channel | Slope | Intercept | R-squared | Max nonlinearity |
|---|---:|---:|---:|---:|
| R | 1964.8393 | 137.0960 | 0.999785 | 0.9337% |
| G1 | 3613.0653 | 279.1564 | 0.999739 | 1.0293% |
| G2 | 3618.2810 | 280.4354 | 0.999736 | 1.0362% |
| B | 1948.4849 | 137.7033 | 0.999849 | 0.7237% |

The free intercepts sit near zero relative to the full fitted signal span, which
is a useful sanity check for the 1024 DN black subtraction. The high R-squared
and low percent residuals are a relative-linearity result for this manually
selected sphere ROI only. The same run keeps the f5.6 and f9 sphere series out
of the fit because they have zero usable points after the readiness gates. This
is still not an ISO OECF result because the ROI is not an identified standard
chart patch with reflectance/illumination controls, and the tool does not
independently prove illumination stability.

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
- No independent illumination-stability validation.
- No colorimetric or perceptual image-quality claim.
