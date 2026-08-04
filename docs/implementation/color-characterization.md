# Color chart extraction and CCM implementation

[Implementation index](README.md) ·
[case study](../case-studies/colorchecker-ccm.md) ·
[patch report](../reports/PATCH_EXTRACTION.md) ·
[localization report](../reports/RAW_CHART_LOCALIZATION.md) ·
[CCM report](../reports/CCM_FIT.md) ·
[reference provenance](../reports/SG_REFERENCE_PROVENANCE.md)

## Software boundary

This pipeline turns one black-subtracted RAW chart capture and a compatible
spectral chart reference into patch RGB values, a fitted 3 by 3 RGB-to-XYZ
matrix, and held-out color-difference statistics. Patch extraction, reference
rendering, matrix fitting, and evaluation are separate typed stages so a
coordinate error or reference mismatch cannot hide inside one command result.

## Code-level data flow

```text
configured RAW + patch geometry + optional flat field
  -> read_raw_cfa_image() for the chart
  -> demosaic_bilinear() for chart RGB
  -> optional flat RAW read and source-CFA validation
  -> optional flat demosaic and apply_flat_field() to chart RGB
  -> optional white-balance gains
  -> patch means in chart order
  -> CameraRgbPatch[140]

spectral reference + illuminant
  -> parse and validate wavelength/patch layout
  -> integrate reflectance * illuminant * CIE observer
  -> reference XYZ[140]

camera RGB + reference XYZ
  -> select folds and dark-patch policy
  -> least-squares 3x3 fit
  -> held-out XYZ and Lab
  -> CIEDE2000 summaries and diagnostics
  -> JSON + aggregate CSV/figure
```

## Patch geometry and extraction

Two geometry paths feed the same extraction stage:

- retained rectangles provide explicit `x, y, width, height` values;
- a corner-seeded projective grid maps normalized 14 by 10 chart coordinates
  through a homography and shrinks each cell to an interior sampling region.

`localize_colorchecker_sg_grid()` emits chart IDs, row/column identity, and
coordinates rather than only rectangles. `extract_patch_means()` checks geometry,
samples the demosaiced linear-DN image, and keeps chart order explicit. The
localization diagnosis fits several residual models and performs held-out
scoring; high RGB correlation cannot override a failed coordinate-distance
threshold.

When a flat field is supplied, its near-ceiling and geometry gates run on the
source CFA before correction. A per-channel full-frame mean normalizes the
flat-field gain, then the corrected RGB is white-balanced. These operations are
ordered so clipping in the correction frame is not concealed by demosaic or
normalization.

## Spectral reference rendering

For patch reflectance `R_p(lambda)`, illuminant `E(lambda)`, and CIE color
matching functions `x_bar`, `y_bar`, and `z_bar`, the implementation applies
the report's discrete integration model:

```text
X_p = k * sum w_i R_p(lambda_i) E(lambda_i) x_bar(lambda_i)
Y_p = k * sum w_i R_p(lambda_i) E(lambda_i) y_bar(lambda_i)
Z_p = k * sum w_i R_p(lambda_i) E(lambda_i) z_bar(lambda_i)
```

`integration_weights()` derives weights from the wavelength grid and
`render_reference_xyz()` normalizes the perfect diffuser under the selected
illuminant. The reference parser verifies patch count, chart order, wavelength
coverage, and provenance fields before rendering.

## Matrix fit and evaluation

The fitted matrix `M` minimizes the summed squared XYZ residuals:

```text
XYZ_predicted = M * RGB_camera

M = arg min sum ||M * RGB_i - XYZ_reference_i||^2
```

`fit_rgb_to_xyz_ccm()` forms and solves the 3 by 3 normal equations for the
declared training patches. `cross_validate_rgb_to_xyz_ccm()` generates folds,
fits each training subset, and evaluates only the held-out patches. Predictions
are converted to CIELAB under the declared reference white before
`delta_e_2000()` is evaluated.

The code retains mean, maximum, RMS, and patch-level diagnostics. Lightness
selection and dark-patch diagnosis are explicit typed stages because flare and
near-black behavior can dominate a summary without representing the rest of
the chart.

## Invariants and failure behavior

- Camera/reference row counts must agree, and their declared order pairing must
  pass the luminance, R-G, and B-G proxy-correlation gates.
- Fewer than three patch rows is refused; a singular camera design matrix is a
  separate refusal even when the row count is sufficient.
- Non-finite spectra, grids, coordinates, matrices, and predicted colors are
  rejected rather than serialized.
- Reference provenance and capture/reference timeline fields remain attached to
  output.
- The output records the cross-project relationship in `timeline_provenance`
  and sets `reference_scope` to
  `compatible_sg_spectral_not_exact_per_unit`; the compatible-reference scope
  is not inferred from surrounding prose.
- A localization candidate must pass geometric error, not only RGB correlation.

## Verification evidence

Patch fixtures pin selected `5 × 5`-image ROI means to `1e-12`. Projective
geometry tests retain all 140 cells in `A1…N10` order and refuse degenerate,
crossed, or non-finite corner sets. The localization gate is tested against two
misleading cases: a `6 px` shift fails the declared `5 px` center limit while
RGB correlation still passes, and a `30 DN` offset fails the `25 DN` absolute
mean-error limit while correlation again passes.

The known linear CCM fixture recovers all nine matrix coefficients and pins
four zero training summaries, held-out mean Delta E 76, and held-out maximum
CIEDE2000 to `1e-9`. A nonlinear fixture then requires held-out mean Delta E 76
to exceed training mean Delta E 76 by more than `1.0`, which establishes why
the report does not use training error alone. CIEDE2000 is checked against ten
Sharma/Wu/Dalal reference assertions—pairs 1–6, a neutral-chroma case in both
orders, and hue-wrap pairs 9 and 11—each to `1e-4`.

Row-count and order-pairing tests distinguish an aligned reference from a
shifted order; they do not match camera rows by patch ID because those rows do
not carry IDs. Other refusal cases cover singular fits, invalid spectra,
flat-field gate failures, and stale reference provenance.

The public corrected-patch guard separately pins the canonical-LF SHA-256,
exactly 140 nonblank rows, three finite positive R/G/B fields per row, and A1
agreement at the report's published two-decimal precision. Mutation tests break
the check after digest or A1 drift, a dropped row, an added header, changed byte
layout, non-finite data, or a stale published digest. This preserves the
committed table; it does not rerun the private RAW extraction or prove
producer-to-baseline equality.

These tests establish the extraction, fitting, evaluation, and failure
contracts. They do not turn the compatible spectral chart reference into a
per-unit measurement of the photographed chart; that scientific boundary comes
from the reference-provenance report and remains serialized in every CCM result.

## Source and tests

- Chart and patch types: [`patches.hpp`](../../include/camera_iq/patches.hpp),
  [`chart_localization.hpp`](../../include/camera_iq/chart_localization.hpp)
- Extraction and localization: [`patches.cpp`](../../src/patches.cpp),
  [`chart_localization.cpp`](../../src/chart_localization.cpp),
  [`localization_diagnosis.cpp`](../../src/localization_diagnosis.cpp)
- Colorimetry and fitting: [`colorimetry.hpp`](../../include/camera_iq/colorimetry.hpp),
  [`colorimetry.cpp`](../../src/colorimetry.cpp)
- Command orchestration: [`cmd_patches.cpp`](../../src/cmd_patches.cpp),
  [`cmd_ccm_fit.cpp`](../../src/cmd_ccm_fit.cpp)
- Reference preparation and independent manufacturer comparison:
  [`export_ccsg_xlsx.py`](../../tools/export_ccsg_xlsx.py),
  [`verify_ccsg_vs_xrite.py`](../../tools/verify_ccsg_vs_xrite.py)
- Focused tests: [`test_patches.cpp`](../../tests/test_patches.cpp),
  [`test_chart_localization.cpp`](../../tests/test_chart_localization.cpp),
  [`test_localization_diagnosis.cpp`](../../tests/test_localization_diagnosis.cpp),
  [`test_colorimetry.cpp`](../../tests/test_colorimetry.cpp), and
  [`test_color_reference.cpp`](../../tests/test_color_reference.cpp)
- Command and published-baseline tests:
  [`test_cmd_patches.cpp`](../../tests/test_cmd_patches.cpp),
  [`test_cmd_ccm_fit.cpp`](../../tests/test_cmd_ccm_fit.cpp),
  [`check_patch_baseline.py`](../../tools/check_patch_baseline.py), and
  [`test_check_patch_baseline.py`](../../tools/test_check_patch_baseline.py)
