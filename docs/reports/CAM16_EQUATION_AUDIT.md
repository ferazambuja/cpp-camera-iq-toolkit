# CAM16 equation audit and CIE94 continuity check

## Overview

A color appearance model predicts perceived attributes such as brightness and
colorfulness under stated viewing conditions. This report audits a small subset
of the published CIECAM02/CAM16 equations by varying their inputs and comparing
isolated terms with the complete expressions they enter. It reproduces two
bounded numerical consequences discussed by Hellwig and Fairchild, pins a
corrected colorfulness coefficient, and retains an unfavorable published
tradeoff instead of reporting only improvements.

This is an equation-level audit, not an implementation of full CAM16, a test of
CIE 248:2022 conformance, or observer validation. That narrower scope makes the
result self-contained: every plotted value follows from a declared equation or
from values printed in the source paper.

The colorimetry library also adds directional CIE94 and a separately named
geometric-mean-chroma variant. That distinction resolves an ambiguity in a
prior color-management study without rewriting its historical result as a
modern validation claim.

[Documentation index](../README.md) ·
[case study](../case-studies/color-model-equation-audit.md) ·
[data](../data/cam16_equation_audit.csv) ·
[historical CIE94 fixture](../data/cie94_historical_24patch.csv) ·
[figure](../figures/cam16_equation_audit.svg) ·
[implementation companion](../implementation/color-model-audit.md)

## Normalized lightness and brightness

Under one fixed viewing-condition contract, CAM16 brightness normalized to the
white is:

```text
Q / Q_white = sqrt(J / 100)
```

Half normalized CAM16 brightness therefore occurs at `J = 25`, while half
normalized lightness occurs at `J = 50`. The proposed 2022 relation is linear:

```text
Q / Q_white = J / 100
```

The equations predict different interior neutral-scale midpoints. They still
agree at black and white, and the calculation alone does not establish which
relation better predicts an observer.

## Background-dependent factor

CAM16 includes:

```text
N_cb = 0.725 (Y_white / Y_background)^0.2
C contains a factor proportional to N_cb^0.9
```

Holding the other terms fixed and normalizing to `Y_background = 20` isolates:

```text
relative factor = (20 / Y_background)^0.18
```

| Relative background `Y_background` | Isolated factor |
|---:|---:|
| 20 | 1.000 |
| 5 | 1.283 |
| 1 | 1.715 |
| 0.1 | 2.595 |

This factor diverges as the relative background approaches zero. It is not full
CAM16 chroma: the complete expression contains additional lightness,
adaptation, and nonlinear-response terms. The result therefore identifies one
equation-level sensitivity without assigning it to a specific display or
viewing application.

## Complete chroma-expression sweep

The source paper's Figure 3 varies reference lightness from `J = 10` through
`90`. Holding adapted responses fixed isolates the background-dependent terms
without choosing arbitrary RGB or XYZ stimuli. With
`n = Y_background / 100`, `n₀ = 0.2`, and
`z(n) = 1.48 + sqrt(n)`, the relative CAM16 chroma expression is:

```text
C(Y_background) / C(20) =
    (n₀ / n)^0.18
  × [(1.64 - 0.29^n) / (1.64 - 0.29^n₀)]^0.73
  × (J₀ / 100)^[(z(n) - z(n₀)) / (2 z(n₀))]
```

`J₀` is the stimulus lightness at the `Y_background = 20` reference. The first
term is the isolated `N_cb^0.9` contribution; the other two carry the coupled
`n` and `z` dependencies from the complete CAM16 chroma expression.

| Relative background | Isolated factor | Complete-expression range, `J₀ = 10…90` |
|---:|---:|---:|
| 5 | 1.283 | 1.112–1.263 |
| 1 | 1.715 | 1.416–1.725 |
| 0.1 | 2.595 | 2.120–2.687 |

Across the declared grid, the isolated factor is neither an upper nor a lower
bound. At `Y_background = 5`, the complete-expression sweep remains below the
isolated factor; at `Y_background = 1` and `0.1`, it crosses from above to
below as reference lightness increases. The relationship therefore depends
jointly on background and reference lightness. This replaces a single-factor
disclaimer with a quantified result while remaining an equation audit. It is
not a general CAM16 forward transform: adapted cone responses are held fixed,
and no XYZ adaptation or appearance prediction is performed.

## Corrected coefficient and published tradeoff

The implementation pins the corrected 22 April 2022 form of Equation 23:

```text
M = 43 N_c e_t sqrt(a^2 + b^2)
```

A literal test uses a 3-4-5 opponent vector and requires the corrected result
`215` when `N_c = e_t = 1`.

The paper reports the following squared correlations. Preserving the
colorfulness regression is important because the proposed equations do not
improve every evaluated attribute.

| Dataset / correlate | CAM16 | Proposed 2022 relation |
|---|---:|---:|
| LUTCHI brightness | 0.86 | 0.95 |
| Munsell chroma | 0.87 | 0.96 |
| LUTCHI colorfulness | 0.81 | 0.71 |

The source datasets are not used here, so these published correlations are
context, not independently reproduced observer results.

![CAM16 equation audit](../figures/cam16_equation_audit.svg)

*Left: normalized brightness against CAM16 lightness `J`; the square-root CAM16
relation reaches half brightness at `J = 25`, while the proposed linear
relation reaches it at `J = 50`. Center: the isolated background factor and the
range of the complete chroma expression across reference lightness
`J₀ = 10…90`; the band crossing the isolated line shows that the single term is
neither a floor nor a ceiling. Right: the paper's reported fits, including the
colorfulness regression from `0.81` to `0.71`.*

## CIE94 and the prior study

CIE94 is directional: the chroma and hue weighting terms use the reference
sample's chroma. The public API therefore requires the reference first and
does not present CIE94 as a symmetric distance. A separately named historical
variant uses the geometric mean of the two chromas for the weighting terms.

The [retained 24-patch table](../data/cie94_historical_24patch.csv) comes from
the Color Pony result in the color-management course project report *Color
Matching Workflow for Art Reproduction*. It printed a summary of `3.10` mean,
`2.75` for its
“best 90%,” and `6.92` for its “worst 10%.” Those labels correspond to 22 and 2
patches, or 91.7% and 8.3% of the set. Recalculation from the rounded Lab values
gives:

| Convention | Mean | Best 22 | Worst 2 |
|---|---:|---:|---:|
| Chart as CIE94 reference | 3.095 | 2.751 | 6.879 |
| Image as CIE94 reference | 3.102 | 2.751 | 6.959 |
| Geometric-mean-chroma weighting | 3.098 | 2.751 | 6.919 |
| Printed historical summary | 3.10 | 2.75 | 6.92 |

Recalculation across all 24 retained pairs reproduces the three summaries and
patch-level rounding agreement. The printed table is internally consistent and
closely follows the geometric-mean-chroma variant. The original tool,
full-precision inputs, and its convention were not retained, so this is neither
proof that the historical workflow used that formula nor an exact independent
reproduction. The custom-ICC arm remains aggregate-only and cannot be
recalculated per patch.

## Limitations

- The implementation covers selected equations and the complete CAM16 chroma
  expression under a fixed-adapted-response sweep, not a general CAM16 forward
  transform or the revised Hellwig/Fairchild appearance model.
- The audit does not establish CIE 248:2022 conformance.
- The paper and its authors' reference code share an author lineage; agreement
  can check transcription but is not independent perceptual evidence.
- LUTCHI observer records were not available for independent analysis. A local
  Munsell workbook was not treated as the paper's dataset without provenance.
- The CIE94 historical recalculation uses rounded values printed in the prior
  report and cannot recover missing full-precision inputs or tool settings.

## Source

Luke Hellwig and Mark D. Fairchild, “Brightness, Lightness, Colorfulness, and
Chroma in CIECAM02 and CAM16,” *Color Research & Application* 47 (2022),
[doi:10.1002/col.22792](https://doi.org/10.1002/col.22792).

## Engineering companion

The [color-model implementation companion](../implementation/color-model-audit.md)
explains how the audited equations map to the C++ implementation and routes
readers to the public source, tests, and artifact generation.
