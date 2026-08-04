# Relative OECF Fit

An opto-electronic conversion function asks how recorded camera signal changes
as exposure changes. This report fits a relative linear response only after
black subtraction, headroom, saturation, EXIF-consistency, and target-uniformity
checks. Four usable points in the selected f/8 sphere region produced per-plane
maximum nonlinearity of 0.72–1.04%; the result is a bounded linearity check, not
ISO 14524 conformance or proof of source stability.

Analysis date: 2026-07-04
Dataset: private local RAW captures used only for validation. Source RAW files
are not distributed with this repository.

## Scope

The fit reuses the existing exposure-response chain:

- Reuse the established exposure-series grouping, decoded active-area crop,
  black subtraction, ROI uniformity, lower-bound signal, near-white,
  saturation, and EXIF-consistency gates.
- Fit black-subtracted mean CFA signal versus relative exposure
  (shutter duration divided by the fastest usable shutter duration) for each
  CFA plane.
- For each CFA plane, estimate slope, intercept, R-squared, maximum nonlinearity,
  fitted signal, and residuals.

This is not ISO 14524 conformance. It is not PTC, read-noise, dark-current,
dynamic-range, reflectance, or color accuracy analysis.

## Scientific Handling

- The signal entering the fit is already black-subtracted by the common RAW
  measurement and exposure-response stages.
- Only points that pass the inherited OECF-readiness gate reach the fit. The
  gate requires
  positive mean signal above black in every CFA plane, mean below 98% of the
  black-subtracted sensor range, less than 1% saturated pixels, matching EXIF
  controls, and ROI uniformity when an ROI was measured.
- A series needs at least three usable shutter points before any plane fit is
  reported.
- Relative exposure is anchored at the fastest usable shutter in the selected
  series. The fit intercept is left free and reported as a black-subtraction
  sanity check; it is not forced to zero.
- The fit assumes constant illumination and scene radiance across the selected
  shutter ladder. The tool cannot verify light-source stability; illumination
  drift is mathematically indistinguishable from sensor nonlinearity in this
  relative fit.
- Maximum nonlinearity is
  `max(abs(residual)) / fitted_signal_range × 100%` over the usable fit points
  for that plane.
- Data readiness and fit validity are reported as separate verdicts. A series
  can contain enough usable exposure points to attempt a fit without that fit
  satisfying the declared linearity conditions.

## Real-Data Validation Run

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

## Interpretation limits

- The relative-exposure fit is not ISO 14524 conformance and has no independent
  illumination-stability validation.
- It does not provide PTC, temporal/read noise, DSNU/PRNU, dark current, or
  dynamic range.
- Chart/reflectance pairing and colorimetric or perceptual quality are separate
  analyses.

## Engineering companion

The [RAW implementation companion](../implementation/raw-foundation.md)
explains how the fit is realized in C++ and routes readers to the public source
and tests. The input-gating results remain in the
[exposure-response report](EXPOSURE_RESPONSE.md).
