# ColorChecker-SG reference scope

A chart-based color fit needs a target value for every photographed patch. The
best reference for this capture would be a spectral measurement of the exact
physical ColorChecker-SG used in the session. That measurement was not retained.
This report explains the bounded substitute used by the color-characterization
study and what the resulting color differences can—and cannot—mean.

[Patch extraction](PATCH_EXTRACTION.md) ·
[CCM fit](CCM_FIT.md) ·
[implementation companion](../implementation/color-characterization.md)

## What was available

The retained project spectroradiometry covers neutral material: a 15-step
luminance ramp and repeated Perfect Reflecting Diffuser measurements under two
illuminant conditions. Those measurements are useful for studying neutral
response and spectral-record ingestion, but they cannot supply expected values
for the colored chart patches.

A separate compatible ColorChecker-SG reference contains all 140 patch spectra
from 380–730 nm at 10 nm intervals. It represents the same chart product and
edition class, but the archive does not establish that it came from the physical
chart photographed in this study.

## Why the compatible reference was accepted

Three checks were kept separate:

1. **Shape and completeness.** The reference contains 140 labeled patches and
   36 spectral bands on the declared wavelength grid.
2. **Manufacturer consistency.** Rendering the spectra under the declared D50,
   CIE 1931 2° conditions and comparing them with the edition-matched
   manufacturer nominal values gives mean CIE76 color difference **1.34** over
   all 140 patches. This supports product-level compatibility, not per-unit
   identity.
3. **Physical ordering.** Broadband proxies derived from the reference agree
   with the retained camera-patch sweep: luminance correlation **0.9775**,
   red-to-green correlation **0.9498**, and blue-to-green correlation **0.9617**.
   The configured pairing gate passes. Alternate row, column, and 180-degree
   orderings are substantially worse, which protects against using the right
   spectra in the wrong patch order.

The camera extraction and an independent ROI export also agree at **0.99984**
green-channel correlation in the retained physical order. This checks the
camera-table handoff; it does not turn either table into chart ground truth.

## How the reference is used

Each patch reflectance is integrated with the selected illuminant and the CIE
1931 2° color-matching functions, then normalized by the illuminant white to
produce reference XYZ. The patch order follows the verified physical sweep
rather than assuming that two tools use the same label convention.

The resulting reference is declared as:

- **role:** compatible SG spectral reference;
- **physical identity:** compatible chart product, exact unit not proven; and
- **scope:** suitable for demonstrating patch extraction, matrix fitting,
  held-out evaluation, and residual analysis, but not a calibration certificate
  for the photographed chart.

The CCM output carries that scope explicitly. The software refuses other
reference roles until their scientific meaning and output scope are defined;
changing a metadata label cannot silently upgrade the evidence.

## Interpretation boundary

The reported CIEDE2000 and CIE76 values answer: how well does this capture fit a
compatible full-chart spectral reference under the declared processing and
validation split? They do not establish the error against the exact physical
chart unit. A portion of the dark-patch residual may therefore come from
capture flare, reference-unit variation, or both.

The missing per-unit reference does not make the exercise meaningless. The
study still tests the complete data flow, patch ordering, flat-field and white-
balance handling, held-out matrix behavior, and residual localization. It does
set the ceiling on the claim: this is a characterization demonstration using a
compatible reference, not a traceable per-unit camera calibration.

## Resolving measurement

The stronger experiment is direct and specific: measure the exact chart unit's
140 patch reflectances with recorded instrument mode, geometry, calibration
state, wavelength grid, and uncertainty; photograph that same chart in the
camera session with matched dark and flat controls; then rerun the identical
fit and held-out evaluation. That would replace the compatibility assumption
with a measured physical link and show how much of the current residual belongs
to the reference substitution.

## Engineering companion

The [color-characterization implementation companion](../implementation/color-characterization.md)
explains how the reference role, patch ordering, integration, fit, and output
scope are represented in software and verified by focused tests.
