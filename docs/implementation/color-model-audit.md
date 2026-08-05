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
vector case. Curve tests pin selected values and grid sizes, while
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

## Verification evidence

At the library/unit layer, the published curves are test oracles rather than
unchecked outputs. The eleven numeric assertions in
[`test_cam16_equation_audit.cpp`](../../tests/test_cam16_equation_audit.cpp)
pin both brightness relations at their midpoint consequences to `1e-15`:
CAM16's square-root relation reaches half normalized brightness at `J = 25`,
and the proposed linear relation reaches it at `J = 50`. The background factor
is pinned at the reference condition and at selected darker backgrounds
`Y_b = 5`, `1`, and `0.1`. At
`Y_b = 0.1`, the coupled expression is pinned to
`2.6865933941337503` for reference `J₀ = 10` and
`2.1198928552563943` for `J₀ = 90`, both to `1e-12`; those values establish
that the isolated `2.595287047166021` term is neither a floor nor a ceiling. A
literal 3-4-5 opponent vector with `N_c = e_t = 1` must return `215` to `1e-12`,
which pins the corrected Equation 23 coefficient `43`.
<!-- test-evidence: color_model_audit_numeric_oracles -->

The implementation enforces these domains: normalized brightness accepts `J`
only in `[0,100]`; relative and reference backgrounds only in `(0,100]`; the
coupled expression's reference `J` only in `(0,100]`; and serialized published
`R²` only in `[0,1]`. The current test fixture directly exercises brightness
values just above `100`, at `150`, just below zero, `NaN`, and infinity. For the
isolated background factor it exercises actual and reference backgrounds above
`100`, non-finite actual backgrounds, and zero actual background. Zero is an
asymptote of the relation, so returning a finite number there would invent a
value the equation does not have. The remaining enforced domain edges are not
claimed as directly exercised by this fixture.

The six published `R²` values are compared with tolerance `0.0` — exact
equality. They are transcribed from the source paper, so any drift is a
transcription error rather than numerical noise, and that includes the
unfavorable `0.81 → 0.71` colorfulness result. Curve lengths are pinned at
`21`, `8`, and `72` points. The generator and its mutation tests in
[`test_generate_cam16_equation_audit.py`](../../tools/test_generate_cam16_equation_audit.py)
require the correction date
`2022-04-22`, coefficient `43`, scope marker, JSON and CSV schemas, and finite
values; regenerated JSON/CSV numerics compare to `1e-12`, while the SVG is
byte-exact. CLI argument refusals are covered separately.

For CIE94, [`test_colorimetry.cpp`](../../tests/test_colorimetry.cpp) recomputes
all 24 retained patches under both directional
conventions and the separately named geometric-mean variant. The nine summary
values are pinned to `1e-6`, every printed patch remains within `0.015` of the
geometric-mean result, and non-finite Lab input is refused.

This evidence establishes that the declared equations are implemented as
written and that their behavior is reproducible. It does not validate CAM16
against observers, establish CIE 248:2022 conformance, implement the full
forward model, or recover the missing tool settings behind the historical CIE94
table; the scientific report keeps those boundaries explicit.

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
  [`generate_cam16_equation_audit.py`](../../tools/generate_cam16_equation_audit.py),
  [`test_generate_cam16_equation_audit.py`](../../tools/test_generate_cam16_equation_audit.py)
