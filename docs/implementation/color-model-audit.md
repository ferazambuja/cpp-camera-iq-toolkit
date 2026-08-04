# Color-model equation-audit implementation

[Implementation index](README.md) ·
[case study](../case-studies/color-model-equation-audit.md) ·
[scientific report](../reports/CAM16_EQUATION_AUDIT.md) ·
[generated data](../data/cam16_equation_audit.csv)

## Software boundary

This is a deliberately bounded equation harness, not a general CAM16 forward
model. It evaluates declared brightness and chroma relations over fixed sweeps,
checks the corrected 2022 colorfulness coefficient, serializes the resulting
curves, and keeps the historical CIE94 recalculation in the shared colorimetry
library.

## Code-level data flow

```text
declared equation constants and sweep axes
  -> build_cam16_equation_audit()
       -> normalized brightness curves
       -> isolated N_cb^0.9 background curve
       -> coupled chroma curve over background and reference J
       -> published R-squared context values
  -> typed Cam16EquationAuditReport
  -> JSON and CSV serializers
  -> Python schema/numeric check and SVG generator

historical Lab pairs
  -> directional CIE94 variants
  -> patch-level values
  -> retained summary comparisons
```

## Equation mapping

The scientific report is canonical for the equations and their interpretation.
The C++ functions map to them directly:

| Scientific quantity | Function |
|---|---|
| CAM16 normalized brightness `sqrt(J / 100)` | `cam16_normalized_brightness()` |
| proposed normalized brightness `J / 100` | `hellwig_2022_normalized_brightness()` |
| isolated background contribution | `cam16_isolated_ncb_chroma_factor()` |
| fixed-adapted-response complete chroma ratio | `cam16_relative_chroma_fixed_adapted_response()` |
| corrected `43 N_c e_t sqrt(a^2 + b^2)` relation | `hellwig_2022_colorfulness()` |

`build_cam16_equation_audit()` samples `J` from 0 to 100 in steps of 5. It
samples eight declared background values and, for the coupled expression,
reference `J` from 10 to 90 in steps of 10. Those axes are serialized with the
scope so a plotted point cannot be mistaken for a full appearance prediction.

## Numeric contracts

Every public equation function validates finite inputs and the non-negative
domains required by the relation. Roots, powers, ratios, and final expressions
are checked for representability. Overflow and invalid background/lightness
combinations are refusals.

The corrected Equation 23 coefficient is pinned by a literal 3-4-5 opponent
vector case. Curve tests pin selected endpoints and monotonic behavior, while
the generated-artifact check requires exact schemas and finite values. The
figure is regenerated from the current command output rather than from a
hand-maintained table.

## CIE94 implementation

The shared colorimetry layer exposes directional CIE94 with the reference color
first. The reference chroma determines the chroma and hue weighting terms. A
separately named historical variant uses the geometric mean of the two chromas
for those weights; the API does not silently switch conventions.

The historical fixture carries rounded Lab pairs. The tests recompute all
patches under the declared variants and compare their summaries with the printed
course result. Agreement shows consistency with the retained rounded table; it
does not recover the missing original tool settings or full-precision input.

## Source and tests

- Equation types and API:
  [`cam16_equation_audit.hpp`](../../include/camera_iq/cam16_equation_audit.hpp)
- Equation evaluation and serializers:
  [`cam16_equation_audit.cpp`](../../src/cam16_equation_audit.cpp)
- CIE94 and supporting colorimetry:
  [`colorimetry.cpp`](../../src/colorimetry.cpp)
- Command: [`cmd_cam16_equation_audit.cpp`](../../src/cmd_cam16_equation_audit.cpp)
- Tests: [`test_cam16_equation_audit.cpp`](../../tests/test_cam16_equation_audit.cpp),
  [`test_colorimetry.cpp`](../../tests/test_colorimetry.cpp), and
  [`test_cmd_cam16_equation_audit.cpp`](../../tests/test_cmd_cam16_equation_audit.cpp)
- Artifact generator:
  [`generate_cam16_equation_audit.py`](../../tools/generate_cam16_equation_audit.py)
