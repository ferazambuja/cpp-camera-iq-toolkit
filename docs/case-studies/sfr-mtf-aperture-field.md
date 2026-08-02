# Nikon D800/D810 + 50 mm f/1.4G slanted-edge SFR: aperture and field behavior

## Overview

This study implements a green-linear slanted-edge SFR pipeline in C++ and
applies it to two archived 50 mm aperture sweeps, one on a D800 body and one on
a D810 body. Slanted-edge SFR measures a whole capture system — lens, aperture,
focus and alignment state, OLPF, sensor sampling, and processing path — so every
result below belongs to a capture system, not to a camera body or a lens alone.
All 299 field ROIs were accepted. The result is not a universal lens rule: the
D810 system showed a strong f/5.6 peak, while the D800 system retained a
different aperture trend and off-axis pattern.

[Documentation index](../README.md) ·
[detailed report](../reports/SFR_MTF.md) ·
[archive/oracle notes](../reports/SFR_MTF_ARCHIVE_INVENTORY.md) ·
[aggregate CSV](../data/sfr_aperture_summary.csv)

![Nikon D800 and D810 SFR aperture and field summary](../figures/sfr_aperture_field.svg)

## Problem and relevance

A center-only MTF value can hide field tilt, decentering, corner behavior, or
capture-specific focus. The engineering question was therefore two-part:

1. Does the center response follow a physically plausible aperture trend?
2. Does that trend transfer across capture systems and across the image field?

![Reduced crop showing slanted-edge regions distributed across the SFR target](../images/sfr-field-target.jpg)

*Illustrative crop from the source test capture. The implementation measures
sensor-linear green samples inside selected edge regions; this reduced image is
not an analysis input.*

## Implementation

The `sfr` command:

- reads LibRaw active-area, black-subtracted Bayer samples;
- extracts the green CFA positions without demosaic, luma conversion, or gamma;
- fits the slanted edge from scan-line centroids;
- bins a 0.25 px ESF, differentiates to an LSF, applies a Hamming window and
  in-repo DFT, and corrects adjacent-difference attenuation;
- reports MTF50, MTF50P, MTF at Nyquist, R1090, edge angle, saturation, and
  rejection diagnostics;
- parses one coherent `_Y_multi.csv` batch for advisory comparison and 23-ROI
  field mapping.

The current implementation also rejects non-finite or out-of-range numeric
options, bounds ESF interpolation/allocation, validates oracle geometry and
duplicates, and preserves diagnostic values when a measurement is rejected.

## Data and validation model

The study uses archived D800 and D810 slanted-edge RAW captures plus matching
per-file result tables. Source captures remain outside Git. Imatest values are
an advisory fidelity reference because its luma/gamma pipeline differs from the
toolkit's sensor-linear green path.

### Recorded capture configuration

All 18 aperture-sweep files record the same AF-S Nikkor 50mm f/1.4G lens
model at 50 mm, an approximate 0.84 m focus distance, and ISO 100. The
full per-file metadata audit is in the
[archive inventory](../reports/SFR_MTF_ARCHIVE_INVENTORY.md#capture-metadata-audit).
The two sets differ in ways that matter:

| Property | D810 set | D800 set |
|---|---|---|
| Files audited | 9/9 | 9/9 |
| Focus mode | AF-S | Manual |
| `FocusPosition` raw code | `0x11` in all files | `0x11` in all files |
| Capture time | 2016-12-09 17:53–17:54 | 2016-12-09 18:44–18:48 |
| Optical low-pass filter | absent | present |

`FocusPosition` is an opaque maker-note code, and ExifTool identifies the
related focus distance as approximate. Its constant value documents metadata
consistency; it does not prove unchanged focus or focus accuracy. The lens
serial number is absent, so the archive proves the same lens model, not the same
physical sample. Captures about 50 minutes apart make a single copy plausible
but unverified.

Nikon specifies the D800 with an enhanced OLPF and the D810 without an OLPF;
both are specified at 35.9 x 24.0 mm and 7360 x 4912 pixels. Their nominal
sampling pitch therefore matches, so the cycles/pixel comparison is not
confounded by a nominal pixel-pitch difference. The matching scale does not
remove the other capture-system differences.

### Validation

Validation combines:

- synthetic edge, orientation, clipping, ESF-gap, and rejection tests;
- filename/EXIF and ROI-geometry checks;
- coherent single-batch advisory comparisons;
- full archive sweeps: 92 D810 ROIs and 207 D800 ROIs.

## Results and engineering decision

- The D810 capture system peaked at **0.2714 cycles/pixel at f/5.6** in center
  MTF50. That location is consistent with the usual balance between residual
  lens aberrations wide open and diffraction on stopping down, but it is a
  property of this system and setup, not of the camera or the lens alone.
- D810 center exceeded the strongest physical corner at f/5.6, f/8, and f/11;
  f/4 was a near tie/slight corner win.
- D800 did **not** satisfy the D810 aperture-trend gate. Both toolkit and
  advisory results keep f/4 below f/16 at center.
- The D800 field maximum moved away from center through the mid apertures; the
  toolkit and advisory source agreed on the dominant location at f/4 through
  f/11. At f/4 the toolkit's strongest physical corner exceeds its center by
  32%. The advisory table for that file labels no ROI `Corner` — its regions are
  `Center` and `Pt Way` — so the comparable advisory figure is its
  most-peripheral ROI, +19% over center. The mid-aperture maximum sits at grid
  point N=12, a top-center edge 1414 px above center, and +60% over center in
  the advisory path.
- **Near-mirrored ROI pairs measure field symmetry directly.** Every edge in this
  archive is fixed-axis and near-vertical, so for an exactly vertical edge the
  sagittal/tangential mixture is set by `|x| / r`. A reflection through the
  horizontal axis preserves radius and mixture together, which means any centered
  rotationally symmetric system — astigmatic ones included — must return the same
  MTF50 for both members of the pair. The grid offers three such near-reflections:

  | Pair | Δ radius | Δ mixture | MTF50 upper | MTF50 lower |
  |---|---:|---:|---:|---:|
  | N=14 / N=16 (f/4) | 0.54% | 0.80% | 0.1403 | 0.1054 |
  | N=2 / N=4 | 0.76% | — | 0.1647 | 0.0945 |
  | N=18 / N=20 | 4.06% | — | 0.1878 | 0.1055 |

  All three favor the upper field, across three radii and two apertures, and in
  every pair the upper site sits at the *larger* radius — the opposite ordering
  from what a centered profile falling with radius would predict. At the tightest
  pair MTF50 changes by 33% while radius and mixture differ by 0.54% and 0.80%.
- **The result is bounded as capture-system asymmetry.** The evidence above is
  strong against centered rotational symmetry but short of a formal exclusion,
  since only near-vertical edges are recorded and a centered
  radius-plus-orientation response could distribute the difference across both
  variables. Closing that alternative needs controlled radial/tangential
  orientations. The responsible component is likewise unresolved: tilt,
  decentering and alignment all produce an upper/lower imbalance, and aperture
  behavior does not separate them, because stopping down reduces several
  geometric aberrations as well as widening depth-of-field tolerance.
- Center agreement stayed within ±0.015 cycles/pixel on D800, while off-axis
  differences were larger—consistent with comparing green-linear CFA SFR to a
  rendered-luma reference path. The center agreement makes a toolkit-only gross
  numerical artifact less likely, but both paths use the same captures and do
  not independently identify the physical cause.

Acceptance criteria are therefore defined per capture system. A single threshold
wide enough to pass both bodies would report agreement where the measurement
shows two different field behaviors.

## Interpretation limits

This is a system SFR study, not a standalone lens characterization: it lacks
verified lens-sample identity, controlled refocusing, repeat captures, lp/mm
normalization, and sagittal/tangential coverage. It is also sensor-linear green
SFR, not rendered-Y equivalence.

The D800/D810 gap cannot be attributed to one component. Their OLPF designs
differ, which is a plausible body-side contribution, but this archive does not
isolate its magnitude. Focus mode, capture/alignment state, and unverified focus
accuracy also differ. The common lens model and close capture times make a
single lens copy plausible, not proven, and do not authorize ranking lens,
body, or setup contributions.

## Implementation and tests

- Core: [`src/sfr.cpp`](../../src/sfr.cpp) and
  [`include/camera_iq/sfr.hpp`](../../include/camera_iq/sfr.hpp)
- CLI/JSON: [`src/cmd_sfr.cpp`](../../src/cmd_sfr.cpp)
- Algorithm tests: [`tests/test_sfr.cpp`](../../tests/test_sfr.cpp)
- CLI and serialization tests:
  [`tests/test_cmd_sfr.cpp`](../../tests/test_cmd_sfr.cpp)
- Figure generator:
  [`tools/generate_portfolio_figures.py`](../../tools/generate_portfolio_figures.py)
