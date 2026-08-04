# Auditing inherited color-model equations

[Detailed report](../reports/CAM16_EQUATION_AUDIT.md) ·
[figure](../figures/cam16_equation_audit.svg) ·
[data](../data/cam16_equation_audit.csv) ·
[historical CIE94 fixture](../data/cie94_historical_24patch.csv) ·
[implementation](../../src/cam16_equation_audit.cpp) ·
[tests](../../tests/test_cam16_equation_audit.cpp)

## What this is about

A color appearance model predicts how a color will *look* under stated viewing
conditions, not merely what it measures. Brightness, lightness, colorfulness,
and chroma all shift with the surround and background even when the stimulus is
unchanged, which is why appearance models matter to display rendering and
cross-condition color work. CAM16 is a CIE-derived model of that kind. This
study traces a declared subset of published equations and revisions rather
than treating an inherited formula as self-validating.

Inherited equations carry inherited surprises: coefficients that were corrected
after publication, terms that behave unexpectedly toward the edges of their
range, and tradeoffs that a summary can quietly drop. This study turns a small,
declared set of those equations into inspectable C++ with numeric tests, so the
behavior can be checked rather than assumed. It audits specific equations; it
does not implement the full model or validate it against observers.

## Engineering question

Can published color-appearance equations be turned into small, inspectable
tests that expose their behavior without overstating equation agreement as
perceptual validation?

## Result

The C++ audit reproduces two numerical consequences discussed by Hellwig and
Fairchild. CAM16's normalized brightness relation places half brightness at
`J = 25`, while half lightness is `J = 50`. A second diagnostic isolates the
background-dependent `N_cb^0.9` contribution: relative to `Y_background = 20`,
it reaches `1.283` at 5, `1.715` at 1, and `2.595` at 0.1.

The paper-derived reference-`J = 10…90` sweep then evaluates the complete CAM16
chroma expression while adapted responses are held fixed. At
`Y_background = 0.1`, the coupled result spans `2.120–2.687`, crossing both
sides of the isolated `2.595` value.
The isolated factor is therefore neither an upper nor a lower bound. This is a
declared equation contract, not a general CAM16 forward transform or a failure
assigned to a display technology.

The audit also pins the corrected Equation 23 coefficient `43` and retains the
paper's unfavorable result: its proposed colorfulness relation reduced the
reported LUTCHI `R²` from `0.81` to `0.71`, even while brightness and Munsell
chroma improved. This makes the comparison useful as engineering evidence
rather than a one-sided summary.

## Connection to earlier work

A prior color-management study reported CIE94 results but did not retain the
formula convention used by its third-party tool. All 24 rounded Lab pairs are
retained as a compact public fixture and exercised by the C++ tests. The new
API makes standard CIE94 directionality explicit and gives the historical
geometric-mean-chroma variant a separate name. Recomputing the rounded
24-patch table produces a worst-two value of `6.919` with the historical
variant, close to the printed
`6.92`; the standard directional conventions bracket it at `6.879` and
`6.959`.

That supports a bounded conclusion: the retained table is internally
consistent. Because only rounded inputs and no tool settings survive, the audit
compares both standard directions and a separately named historical variant
rather than claiming exact reproduction. The improvement is the explicit
contract—reference direction, application weights, numeric tests, and
limits—rather than a claim that the earlier arithmetic was wrong.

The equation audit uses the retained rounded table and adds no new capture
measurement. CIE94 appears here to preserve and test that prior result;
CIEDE2000 appears in the current CCM and gamut studies under their own declared
contracts. Their formulas and weighting conventions differ, so the values are
method-specific and are not compared numerically across studies.

## What this demonstrates

- translating published equations into typed, testable C++;
- separating one equation factor from the complete chroma expression and
  measuring how the coupled result changes with reference lightness;
- checking corrections and unfavorable results instead of selecting only
  confirming evidence; and
- tracing an ambiguous historical metric to a testable convention without
  overstating what the retained record proves.

![CAM16 equation audit](../figures/cam16_equation_audit.svg)

*Left: normalized lightness and brightness plotted against `J`. Lightness is the
straight line, so half lightness sits at `J = 50`; CAM16 brightness follows the
square-root curve, so half brightness sits at `J = 25`. The gap between the two
curves is the disagreement the audit reproduces. Right: how the chroma
expression responds as the background darkens, with the isolated `N_cb^0.9`
factor shown against the complete expression evaluated across reference
lightness — the band crosses the isolated line, which is what makes the isolated
term neither a floor nor a ceiling.*

The curves are equation-level diagnostics with fixed adapted responses. They do
not replace a general CAM16 forward model, viewing-condition validation,
observer data, or a validated image-rendering study.
