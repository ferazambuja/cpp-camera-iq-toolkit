# Spectral sensitivity and camera color fidelity

## What this is about

Camera color begins with the sensor itself. Each red, green, and blue channel
responds differently across the visible spectrum, and no later matrix can make
those three curves behave exactly like the human observer if their shapes are
fundamentally mismatched. A monochromator reveals those spectral sensitivity
curves by presenting one narrow wavelength band at a time.

This study asks two related questions. First, do the measured sensitivities
predict separately retained chart captures from the same lab run? Second, how
closely can each camera's three sensitivities be transformed into the CIE 1931
standard observer—the theoretical condition for colorimetric camera response?
The distinction matters: the first checks a measurement chain, while the second
compares a sensor's colorimetric potential.

In the physical closure check, four camera/chart sets whose sessions retained
every input needed to close the loop predicted their own chart captures to
within **9.5–13.8% RMS per channel**, with patch-order correlation above 0.992.
The correlation is the weaker of the two: it is scale-invariant and largely
driven by the light-to-dark spread across 140 patches, so the RMS is the number
that could have failed. Both are reported together throughout.

Across the five available spectral-sensitivity sets, the Canon 5D2 was closest
to the human-observer subspace under the declared fit and the Phase One IQ3 was
farthest. The practical spread is small: on the ISO-recommended chromatic patch
set under D55, ISO 17321-style sensitivity metric (SMI) values span **90.7 down
to 88.3** against a colorimetric ideal of 100, and mean CIEDE2000 against the
reference spans **0.88 to 1.10** — every camera within roughly one
just-noticeable difference. Differences among the middle cameras changed with
analysis choices and are not presented as a firm ranking.

[Documentation index](../README.md) ·
[detailed report](../reports/SPECTRAL_SENSITIVITY.md) ·
[measurement inventory](../reports/SPECTRAL_ARCHIVE_INVENTORY.md) ·
[aggregate CSV](../data/spectral_color_fidelity.csv) ·
[implementation companion](../implementation/spectral-fidelity.md)

The archive combines one four-camera monochromator, camSPECS measurement, and target
laboratory run with a separate Phase One IQ3 camSPECS session on a different
rig. The four-camera set retains same-session spectral sensitivity functions
(SSFs), a broadband target,
illuminant, and chart-reflectance records; the IQ3 session retains the SSF
capture but no same-session broadband target or chart reflectance. The analysis
therefore closes only the evidence-complete set and keeps IQ3 in the SSF-only
comparison instead of filling the missing physical links by assumption.

![Five-camera spectral color-fidelity comparison](../figures/spectral_color_fidelity.svg)

*Each circle is one camera scored on one chart set — five cameras, three sets
(the 18 chromatic ColorChecker patches, the full 24, and the 140-patch SG).
Higher is better on an ISO 17321-style sensitivity metamerism index (SMI).
Note the vertical
axis spans 86 to 94 rather than starting at zero, so the visual spread is much
wider than the numeric spread; that is why only the endpoints are read as a
result and the middle ordering is not. `QI` beside each camera is the separate
Luther-fit quality index, which asks how closely that sensor's spectral
sensitivities can be matched to the human observer by any linear transform. The
two answer different questions, so they are shown side by side rather than
merged. `QI` runs to a ceiling of 1.0, which would mean the sensitivities are an
exact linear transform of the CIE observer. Only the Canon row comes from this
toolkit's own RAW extraction; the others use sensitivity functions measured in
the same laboratory run and retained in the archive.*

## Method

A monochromator steps a narrow band of light across the visible range while the
camera photographs each step. Dark-subtracted response per color channel, read
from the RAW captures, gives that camera's spectral sensitivity functions — how
strongly each channel answers at every wavelength. Saturated and below-dark
samples are excluded rather than fitted.

Those sensitivities are then tested three ways:

- **Physical closure.** Using the measured sensitivities, the measured
  illuminant spectrum, and the measured chart reflectances, predict what the
  camera should have recorded for every patch, allowing a single global exposure
  scale. Comparing that prediction to a separately retained same-session target
  capture tests the sensitivities against physical evidence beyond the
  monochromator sweep, without pretending the shared rig is independent.
- **Luther condition.** Fit a linear transform from the camera's sensitivities
  to the CIE 1931 color-matching functions. The residual reports how closely
  the sensitivities approach that colorimetric subspace under the declared
  unweighted fit; it is not a scene-performance bound.
- **ISO 17321-style sensitivity metamerism index (SMI)** over D55 and the measured chart sets,
  with a white-preserving variant run as a sensitivity check on the ranking.

## Cross-checks

For the retained Canon 5D2 end-to-end extraction, toolkit-vs-legacy normalized
response correlation was **0.99937 / 0.99979 / 0.99991** for R/G/B.

The dense 140-patch closure used a toolkit-extracted Canon SSF and measured
retained SSFs for the Nikon D810, Sony A7RII, and Sony A7SII. All four cameras
matched all 140 patches, at **9.5–13.8% relative RMS per channel** and
patch-order correlation above **0.992**. A 24-patch complementary run was
tighter, at 5–8% RMS and correlation above 0.997.

Those retained curves are comparison references for reimplementation fidelity,
not independent truth: they were measured on the same rig in the same session,
so agreement with them shows this toolkit reproduces that measurement, not that
either is correct. Toolkit RAW extractions for the other three cameras in the
shared run reproduced the reported Luther ordering at the shown precision, which
checks that the ranking is not an artifact of choosing the retained CSVs.

## Findings

The endpoint ordering stayed stable across the Luther residual and all three
SMI test sets: Canon 5D2 was the closest of the five measured sensitivities to a
color-matching-function subspace (SMI 90.7, mean CIEDE2000 0.93), and the
separate Phase One IQ3 run was the farthest (SMI 88.3, 1.10). Sony A7RII was
second across the SMI sets (90.0) and effectively tied with D810 under the
Luther residual at published precision. The middle A7SII/D810 ordering moved by
a few tenths when the illuminant changed, so it is reported as a close
comparison rather than a large-margin camera ranking.

The two endpoints are not equally well supported. The four 2016 cameras share
one rig, one session, and one illuminant, so their ordering is a controlled
comparison. The Phase One IQ3 was measured on a different rig in a different
year, and no file identifies either monochromator, so its 2.4-point SMI gap
below the Canon cannot be separated from cross-rig systematics. A second
retained IQ3 run differs from the first by about 0.1 SMI, which bounds
within-rig repeatability but says nothing about the between-rig offset. The
firm result is therefore the Canon's position within the shared run; the IQ3
endpoint is directional.

The chart-closure residual is not used to rank camera quality. Closure contains
session, lens, chart, sidecar, illuminant, and SSF effects; the separate
SSF-vs-CMF metrics answer the color-fidelity question.

## What the result does not establish

The SMI calculation is **ISO 17321-style**, not claimed as a bit-exact
implementation of the standard's Annex-B optimizer and normalization. Canon
uses a toolkit RAW-derived SSF in the retained aggregate; the other plotted rows
use measured legacy SSFs.
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

## What to take from this

The opening asked how faithfully these sensors could reproduce colour, given
that the answer is bounded before any processing. Across five measured
sensitivity sets the ordering was stable, but the spread was small: SMI 90.7 to
88.3, every camera inside roughly one just-noticeable difference. On this axis
the five cameras are closer to each other than a specification sheet would
suggest, and sensor choice among them is not the decisive colour-fidelity
variable.

The comparison is only as sharp as the method behind it. Closure predicted the
chart captures to 9.5–13.8% RMS per channel, which is the resolution at which
this evidence can separate cameras at all — comfortably enough for the Canon's
position within the shared rig, and not enough to defend a cross-rig endpoint.
Anyone reusing these curves should carry the rig and session with them.

Closing that endpoint needs one specific capture, not more analysis: measure the
IQ3 and at least one camera from the shared run on the same monochromator in the
same session. That single overlap turns the between-rig offset into a measured
quantity instead of an unbounded one, and it is the only thing that does — the
archive records no monochromator make or model for either run, so bandwidth,
wavelength accuracy, and stray light cannot be reconciled from the files.
