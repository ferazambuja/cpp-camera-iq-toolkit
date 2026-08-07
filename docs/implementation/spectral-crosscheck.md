# Spectral cross-check implementation

This companion maps the [spectral cross-check report](../reports/SPECTRAL_CROSSCHECK_2017.md)
to the C++20 implementation. The software has two deliberately separate paths:
a generic repeated-spectrum comparison with no implied colorimetric unit, and a
reflectance audit whose illuminant and observer are explicit inputs.

## Software boundary

The public entry points are `camera_iq spectro-compare` and
`camera_iq spectral-reference-audit`. The first consumes a strict long-form
spectral table and compares two named series. The second reads four retained
CGATS exports, two normalized reflectance tables, and explicit CIE reference
tables. Both write through the checked-output boundary, so invalid inputs and
path aliases fail before a partial result replaces a source file.

## Typed data flow

```text
long-form spectral CSV
  -> SpectralSeries
  -> SampledSpectrum groups
  -> SampledSpectrumGroupAnalysis
  -> SpectralComparison
  -> JSON + per-band CSV

CGATS + normalized reflectance CSV + explicit CIE tables
  -> SpectralReference + CgatsSchemaDiagnostics
  -> interchange / colorimetry / paired-series audits
  -> SpectralReferenceColorimetryAudit + SpectralReferenceRepeatAudit
  -> JSON + per-patch CSV
```

[`SampledSpectrum`](../../include/camera_iq/sampled_spectrum.hpp) carries only a
wavelength axis and numeric values; it does not invent XYZ or a radiometric
unit. [`analyze_sampled_spectrum_group()`](../../src/sampled_spectrum.cpp)
separates the native equal-weight spectral integral from the integral-normalized
shape. The existing spectroradiometer analysis reuses this layer and adds
recorded-XYZ chromaticity only where those fields actually exist.

[`read_spectral_series_csv()`](../../src/spectral_series.cpp) accepts the exact
schema `series_id,reading_id,wavelength_nm,value`. It groups by both identifiers,
preserves first-seen order, and rejects blank rows, non-finite values,
non-increasing axes, inconsistent grids, and malformed CSV widths.

## Common-grid comparison

[`compare_spectral_groups()`](../../src/spectral_compare.cpp) analyzes each
native series, linearly resamples the two mean normalized shapes to a
caller-declared common grid, and normalizes each one again over that common
support. This ordering prevents a longer native wavelength tail from changing
the comparison scale.

The output names its direction: the L2 residual denominator is always the
declared reference norm. Per-band squared residuals sum to the full residual
energy. Exclusions are diagnostic views over named common-grid bands; they do
not silently remove samples from the primary result. The offset sweep uses an
explicit source-series and sign convention, restricts evaluation to the
interior supported for every requested shift, and generates its coordinates by
integer index so decimal step accumulation cannot skip zero or overrun an
endpoint. The aggregate result also retains per-band residual evidence at the
best offset, so post-shift localization claims do not depend on an unpublished
scratch calculation.

## CGATS identity and schema diagnostics

[`read_spectral_reference_cgats()`](../../src/color_reference.cpp) retains
`SAMPLE_ID` and `SAMPLE_NAME` independently. Interchange comparisons bind
physical sequence identity to `SAMPLE_ID`, while layout-label differences stay
visible rather than changing the join key. Exact spectral multisets provide a
second check that survives row reordering.

Declared field and set counts are diagnostics rather than unconditional
rejections: the two SpectraShop exports declare 38 fields while their actual
`DATA_FORMAT` and rows carry 41. The two PatchTool exports match their declared
41- and 38-field layouts. These diagnostics are serialized per export rather
than flattened into one schema claim. Observer declarations are also preserved.
A `WEIGHTING_FUNCTION` contributes to the observer-conflict check only when its
value names an observer, preventing `ILLUMINANT,D65` from being misread as a
65-degree observer.

## Explicit colorimetry

[`render_reference_xyz()`](../../src/colorimetry.cpp) has an overload that
requires a complete `SpectroCmfTable` on exactly the same wavelength axis as the
reflectance data. It refuses implicit observer interpolation. The illuminant is
also caller-supplied, and the perfect diffuser defines the XYZ normalization
and CIELAB reference white.

[`audit_spectral_reference_colorimetry()`](../../src/spectral_reference_audit.cpp)
compares the computed result with embedded Lab or XYZ while retaining the
illuminant, observer, integration rule, and source-metadata conflict in its
typed result. [`audit_spectral_reference_repeat()`](../../src/spectral_reference_audit.cpp)
computes per-row reflectance RMS and Delta E for a separately normalized pair;
its type labels the result as observed variation rather than repeatability.

## Numerical behavior and failure paths

Group means and sample standard deviations scale finite inputs before
accumulation so representable results do not depend on extended-precision
`long double` behavior. Integrals and norms must be finite and positive.
Common-grid interpolation never extrapolates. Offset bounds must form an exact
integer number of steps. The reference audit requires matching axes, stable
patch identities, positive declared XYZ norms, and distinct input/output paths.

## What the tests establish

The library fixtures in
[`test_spectral_compare.cpp`](../../tests/test_spectral_compare.cpp) contain
three assertions registered for this claim: means are resampled before shared-grid
normalization, the normalization/interpolation/direction choices remain named,
and the synthetic directional residual is exactly `1/3` within `1e-12`.
They establish the numerical contract, not the physical identity or accuracy of
the retained instruments.

<!-- test-evidence: spectral_crosscheck_common_grid -->

The archive-backed fixture in
[`test_spectral_reference_audit.cpp`](../../tests/test_spectral_reference_audit.cpp)
contains four assertions registered for this claim: D65/10° reproduces the embedded
SpectraShop values at `0.0118573` mean and `0.0412437` maximum ΔE76 within
`5e-7`, while D65/2° gives `3.90919` mean and exceeds the matching result by
more than 300 times. This validates the explicit observer interpretation
against an independent application's retained output; it does not validate the
chart, instrument, or observer model against a physical reference.

<!-- test-evidence: spectral_reference_observer_oracle -->

Command-level tests in
[`test_cmd_spectro_compare.cpp`](../../tests/test_cmd_spectro_compare.cpp) and
[`test_cmd_spectral_reference_audit.cpp`](../../tests/test_cmd_spectral_reference_audit.cpp)
exercise orchestration, serialization, source/output alias refusal, and
failure-before-write behavior. The generated-artifact check executes both
compiled commands and validates the source receipt. JSON structure, CSV schema,
identifiers, and other nonnumeric values must match exactly; finite numeric
values use a `5e-12` absolute/relative tolerance so compiler-level rounding does
not make a scientific artifact stale. The SVG remains byte-exact. These checks
establish software and artifact consistency; the report remains the authority
for physical interpretation.

## Artifact generation

[`import_2017_spectral_archive.py`](../../tools/import_2017_spectral_archive.py)
normalizes the retained text blocks into the strict public schemas and binds
source/output identities without recording a machine path. CGATS files are
copied byte-for-byte because their malformed declarations and conflicting
metadata are part of the evidence.

[`generate_2017_spectral_portfolio.py`](../../tools/generate_2017_spectral_portfolio.py)
validates the receipt, runs both commands, checks the interpretation-bearing
metrics, and renders the figure. Its check mode regenerates every artifact in a
temporary directory and compares it with the committed public output.

The D800 comparison is a bounded code audit rather than a reproducible
reprocessing lane. Its private source scripts and surviving artifacts are
[hash-bound](../../data/samples/spectral_2017/d800_legacy_method_receipt.json)
with line-scoped observations and archive-relative routes under the path-free
`full_2017_coursework_tree` scope. The primary cross-check uses the distinct
`spectral_yes_subset` scope. The scripts are not redistributed because no
redistribution license was identified.
