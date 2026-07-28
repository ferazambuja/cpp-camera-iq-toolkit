# D800/D810 slanted-edge SFR: aperture and field behavior

> **Hiring-manager summary:** I implemented a green-linear slanted-edge SFR
> pipeline in C++ and applied it to D800 and D810 50 mm aperture sweeps. All 299
> field ROIs were accepted. The useful result was not a universal lens rule:
> the D810 showed a strong f/5.6 peak, while the D800 retained a different
> aperture trend and off-axis pattern.

[Portfolio index](../README.md) ·
[detailed report](../reports/SFR_MTF.md) ·
[archive/oracle notes](../reports/SFR_MTF_ARCHIVE_INVENTORY.md) ·
[aggregate CSV](../data/sfr_aperture_summary.csv)

![D800 and D810 SFR aperture and field summary](../figures/sfr_aperture_field.svg)

## Problem and relevance

A center-only MTF value can hide field tilt, decentering, corner behavior, or
capture-specific focus. The engineering question was therefore two-part:

1. Does the center response follow a physically plausible aperture trend?
2. Does that trend transfer across camera/capture sets and across the image
   field?

## What I built

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

Validation combines:

- synthetic edge, orientation, clipping, ESF-gap, and rejection tests;
- filename/EXIF and ROI-geometry checks;
- coherent single-batch advisory comparisons;
- full archive sweeps: 92 D810 ROIs and 207 D800 ROIs.

## Results and engineering decision

- D810 center MTF50 peaked at **0.2714 cycles/pixel at f/5.6**.
- D810 center exceeded the strongest physical corner at f/5.6, f/8, and f/11;
  f/4 was a near tie/slight corner win.
- D800 did **not** satisfy the D810 aperture-trend gate. Both toolkit and
  advisory results keep f/4 below f/16 at center.
- The D800 field maximum moved away from center through the mid apertures; the
  toolkit and advisory source agreed on the dominant location at f/4 through
  f/11.
- Center agreement stayed within ±0.015 cycles/pixel on D800, while off-axis
  differences were larger—consistent with comparing green-linear CFA SFR to a
  rendered-luma reference path.

The decision was to keep gates camera/capture-specific. The D800 non-transfer
result is more informative than redefining a rule until both datasets pass.

## Interpretation limits

This is sensor-linear green SFR, not rendered-Y equivalence, lp/mm reporting, or
a full sagittal/tangential lens model. Focus state, optical low-pass filtering,
and capture differences prevent attributing the D800/D810 gap to one component
alone.

## Implementation and tests

- Core: [`src/sfr.cpp`](../../src/sfr.cpp) and
  [`include/camera_iq/sfr.hpp`](../../include/camera_iq/sfr.hpp)
- CLI/JSON: [`src/cmd_sfr.cpp`](../../src/cmd_sfr.cpp)
- Algorithm tests: [`tests/test_sfr.cpp`](../../tests/test_sfr.cpp)
- CLI and serialization tests:
  [`tests/test_cmd_sfr.cpp`](../../tests/test_cmd_sfr.cpp)
- Figure generator:
  [`tools/generate_portfolio_figures.py`](../../tools/generate_portfolio_figures.py)
