# Spectral sensitivity and camera color fidelity

## Overview

Four same-session camera/chart datasets reached minimum channel correlation
above 0.992 after RAW monochromator extraction and physical target closure. A
five-camera comparison then evaluated Luther-condition residuals and an ISO
17321-style SMI approximation while preserving the mixed-source provenance and
ranking sensitivity needed to interpret the result.

[Documentation index](../README.md) ·
[detailed report](../reports/SPECTRAL_SENSITIVITY.md) ·
[archive map](../reports/SPECTRAL_ARCHIVE_INVENTORY.md) ·
[aggregate CSV](../data/spectral_color_fidelity.csv)

The archive combines one four-camera monochromator, camSPECS, and target
laboratory run with a separate Phase One IQ3 camSPECS session on a different
rig. The four-camera set retains same-session SSF, broadband target,
illuminant, and chart-reflectance records; the IQ3 session retains the SSF
capture but no same-session broadband target or chart reflectance. The analysis
therefore closes only the evidence-complete set and keeps IQ3 in the SSF-only
comparison instead of filling the missing physical links by assumption.

![Five-camera spectral color-fidelity comparison](../figures/spectral_color_fidelity.svg)

## Problem and relevance

Camera color is constrained by the relationship between sensor spectral
sensitivity and the CIE color-matching functions. A plausible curve is not
enough: file selection, dark subtraction, wavelength normalization, illuminant
pairing, chart reflectance, and target-capture closure all have to agree.

## Technical approach

- RAW sweep discovery and wavelength/file-sidecar validation.
- Dark-subtracted per-channel response extraction from monochromator captures.
- Normalized spectral-sensitivity CSV output with saturation and below-dark
  diagnostics.
- Physical closure that predicts same-session chart responses from camera SSF,
  measured illuminant, and measured reflectance using one global exposure
  scale.
- Luther-condition fitting against CIE 1931 color-matching functions.
- An ISO 17321-style SMI approximation over D55 and measured chart sets, plus a
  white-preserving optimization sensitivity check.

## Validation

For the retained Canon 5D2 end-to-end extraction, toolkit-vs-legacy normalized
response correlation was **0.99937 / 0.99979 / 0.99991** for R/G/B.

The dense 140-patch closure used a toolkit-extracted Canon SSF and measured
legacy SSFs for the Nikon D810, Sony A7RII, and Sony A7SII. All four cameras
matched all 140 patches, with minimum channel correlation above **0.992**. A
24-patch complementary run held minimum correlation above **0.997**.

Toolkit RAW extractions for the other three cameras in the shared run
reproduced the reported Luther ordering at the shown precision, which checks
that the ranking is not an artifact of choosing the retained legacy CSVs.

## Results and engineering decision

The endpoint ordering stayed stable across the Luther residual and all three
SMI test sets: Canon 5D2 was the closest of the five measured sensitivities to a
color-matching-function subspace, and the separate Phase One IQ3 run was the
farthest. Sony A7RII was second across the SMI sets and effectively tied with
D810 under the Luther residual at published precision. The middle A7SII/D810
ordering moved by a few tenths under test-set or optimization choices, so it is
reported as a close comparison rather than a large-margin camera ranking.

The chart-closure residual is not used to rank camera quality. Closure contains
session, lens, chart, sidecar, illuminant, and SSF effects; the separate
SSF-vs-CMF metrics answer the color-fidelity question.

## Interpretation limits

The SMI calculation is **ISO 17321-style**, not claimed bit-exact to the
paywalled Annex-B optimizer and normalization. Canon uses a toolkit RAW-derived
SSF in the retained aggregate; the other plotted rows use measured legacy SSFs.
The Phase One session lacks a same-session broadband target and chart
reflectance, so it is valid for an SSF-only comparison but not physical closure.

Instrument identity is bounded by what the archive records. The chart
reflectance CGATS.17 files declare `INSTRUMENTATION i1Pro`; their paired native
SpectraShop projects also record `i1Pro` and probable unit identifier `1001351`.
The text illuminant SPD carries no header, but its paired native project records
`PR-655`. No file identifies the monochromator by make or model, leaving its
bandwidth, wavelength accuracy, and stray-light behavior uncharacterized. See the
[archive map](../reports/SPECTRAL_ARCHIVE_INVENTORY.md#instrument-identity-as-the-files-record-it).
The shared four-camera rig improves within-set comparability but does not make
relative ordering immune to those systematics; the separate Phase One IQ3
session is a cross-rig comparison.

## Code and verification

- RAW extraction:
  [`src/spectral_response.cpp`](../../src/spectral_response.cpp) and
  [`tests/test_spectral_response.cpp`](../../tests/test_spectral_response.cpp)
- Physical closure:
  [`src/spectral_closure.cpp`](../../src/spectral_closure.cpp) and
  [`tests/test_spectral_closure.cpp`](../../tests/test_spectral_closure.cpp)
- Luther metric:
  [`src/spectral_quality.cpp`](../../src/spectral_quality.cpp) and
  [`tests/test_spectral_quality.cpp`](../../tests/test_spectral_quality.cpp)
- SMI-style metric:
  [`src/spectral_smi.cpp`](../../src/spectral_smi.cpp) and
  [`tests/test_spectral_smi.cpp`](../../tests/test_spectral_smi.cpp)
- Figure generator:
  [`tools/generate_portfolio_figures.py`](../../tools/generate_portfolio_figures.py)
