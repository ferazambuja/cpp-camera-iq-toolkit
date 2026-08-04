# Display-P3 to sRGB gamut mapping

## Overview

Display-P3 can encode colors that an sRGB destination cannot reproduce. A gamut
mapper must move those colors somewhere, balancing color difference, hue
behavior, preserved distinctions, and unnecessary changes to colors that
already fit. This report compares four fully specified methods on the same
deterministic input, changing coordinates and mapping rules separately so their
effects can be interpreted rather than blended together.

[Documentation index](../README.md) ·
[case study](../case-studies/gamut-mapping.md) ·
[synthetic input](../data/gamut_synthetic_input.csv) ·
[CIELAB-radial result](../data/gamut_synthetic_radial.csv) ·
[OkLCh-radial result](../data/gamut_synthetic_oklch_radial.csv) ·
[CSS Local-MINDE result](../data/gamut_synthetic_css_local_minde.csv) ·
[soft-compression result](../data/gamut_synthetic_soft.csv) ·
[figure](../figures/gamut_mapping_synthetic.svg)

The numerical path converts encoded Display-P3 or sRGB samples through linear
RGB and relative D65 XYZ before applying a declared method in CIELAB or OkLCh.
It rejects non-finite or out-of-domain input, tests destination membership in
linear RGB, and verifies every accepted output against the destination cube.

Four methods form two controlled comparisons:

- **Fixed-L\*, Lab-hue radial clip** preserves every destination-in-gamut color
  and moves an out-of-gamut color to the first destination boundary connected
  to the neutral axis.
- **Experimental protected-core soft compression** preserves chroma below a
  declared knee and compresses the shoulder with a continuous, monotone curve
  that approaches the destination boundary without reaching it at finite input
  chroma.
- **Fixed-L, OkLCh-hue radial clip** applies the same first-connected-boundary
  rule in OkLCh, isolating the effect of changing coordinates.
- **CSS Color 4 Local MINDE**, pinned to the dated W3C draft, keeps
  destination-in-gamut colors unchanged and uses an OkLCh minimum-color-
  difference search with local clipping, isolating the effect of changing the
  algorithm.

The CIELAB radial method is the reference baseline. CIELAB radial versus OkLCh
radial tests coordinate choice under the same mapping rule. OkLCh radial versus
CSS Local MINDE tests algorithm choice in the same coordinate space. The soft
method remains an explicit design experiment illustrating a protected core,
smoother onset, additional modification of in-gamut colors, and unused
destination-boundary headroom.

## Relationship to the prior color-management workflow

The earlier color-management course report made three narrower claims that are
relevant here:

- its 24-patch evaluation table explicitly labeled its RGB values as Adobe RGB;
- it recommended conversion to sRGB for web delivery and described the
  resulting out-of-gamut fidelity tradeoff; and
- its separate print branch selected a perceptual intent and the `LOGO Classic`
  gamut option in ProfileMaker.

Those are rendering configuration and evaluation decisions, not evidence that
the mapping algorithm was implemented in that project. The current study
specifies and tests the color encodings, boundary definition, mapping curves,
numerical failures, and displacement tradeoffs directly.

The Display-P3-to-sRGB study is intentionally independent of the historical
Adobe-RGB and printer-profile paths. It does not reproduce the proprietary
rendering or re-evaluate the paper's 24-patch result. The ideal RGB pair keeps
the algorithm inspectable while the historical work supplies only the
motivating wide-to-narrow-gamut problem.

## Color contract

The v1 transform is an ideal RGB encoding-gamut study:

- source and destination encodings are sRGB or Display-P3;
- both use D65 and the sRGB piecewise transfer function;
- no chromatic adaptation is required because the white point is unchanged;
- RGB matrices operate on relative XYZ with `Ywhite = 1`;
- CIELAB and OkLab use the same normalized D65 reference white;
- OkLCh uses the W3C sample code's signed-cube-root transform and powerless-hue
  threshold; conversion of a missing polar hue sets the Cartesian opponent
  components to zero, bounding the discarded near-neutral chroma at `4e-6`; and
- legal encoded input components are finite values in `[0,1]`.

The RGB and OkLab matrix values were transcribed from the non-normative sample
conversion code in the
[28 July 2026 CSS Color Module Level 4 Candidate Recommendation Draft](https://www.w3.org/TR/2026/CRD-css-color-4-20260728/#color-conversion-code).
The specification's sRGB and Display-P3 definitions provide the chromaticity,
D65-white, and transfer-function contract. Independent calculations evaluate
the sample-code rationals over the shared-gamut points used in the cross-check.

CSS Color 4 is a work-in-progress draft and currently permits three SDR
single-color gamut-mapping algorithms. This study implements one named,
dated option: Binary Search Gamut Mapping with Local MINDE from the 28 July
2026 draft. Its `deltaEOK` JND (`0.02`), binary-search epsilon (`0.0001`),
component-clipping rule, and relative-colorimetric identity behavior are
treated as properties of that dated draft rather than generalized as a
timeless "CSS algorithm."

This contract does not describe the measured gamut of a physical display.
That would require device characterization, viewing conditions, and a separate
appearance-validation design.

## Boundary solver

At fixed L\* and Lab hue, `fy` and Y are constant while `fx` and `fz` are
affine functions of chroma. The inverse CIELAB function is linear below its
breakpoint and cubic above it, so each destination linear-RGB channel is a
piecewise cubic function of chroma.

The solver uses that structure directly:

1. partition the chroma axis at the CIELAB inverse breakpoints;
2. form the cubic polynomial for each destination channel in each partition;
3. use the polynomial derivatives to divide each channel into monotone
   intervals;
4. enumerate crossings of the tolerated `0` and `1` channel surfaces; and
5. inspect the resulting intervals and refine the first in-gamut to
   out-of-gamut transition.

This matters because constant-Lab-hue rays are not necessarily star-convex.
One constructed counterexample is a legal Display-P3 color at `L*=96.23856` and Lab hue
`1.80124` radians. Its ray first leaves sRGB at approximately `C*=57.64096`,
re-enters shortly afterward, and leaves again much later. A fixed 0.25-C\*
membership scan samples both sides of the narrow first excursion as in-gamut
and selects the wrong boundary. Channel-root enumeration detects the first
transition and maps the color successfully.

The solver returns the conservative in-gamut side of its final bracket. The
default chroma tolerance is `1e-10`; mapped results are independently checked
in unclipped destination-linear RGB before encoding.

At fixed OkLab lightness and hue, the inverse OkLab LMS terms are affine in
OkLCh chroma. Cubing those terms makes each destination linear-RGB channel a
cubic polynomial, without CIELAB's piecewise breakpoint. The OkLCh radial
solver enumerates the same channel-surface crossings and refines the first
neutral-connected exit. CSS Local MINDE uses its own specified binary search;
its evidence is not mislabeled as a radial boundary result.

## Mapping intents

### Fixed-L\*, Lab-hue radial clip

The baseline performs a direct destination-gamut test. An in-gamut color keeps
its XYZ and Lab values. An out-of-gamut color keeps L\* and the CIELAB hue angle
while its chroma is reduced to the neutral-connected destination boundary.
This is radial clipping, not nearest-point projection and not RGB component
clipping.

### Fixed-L, OkLCh-hue radial clip

This method keeps the radial rule unchanged and changes only the coordinates.
Destination-in-gamut inputs retain colorimetric identity. Out-of-gamut inputs
keep OkLab L and OkLCh hue while chroma moves to the first neutral-connected
destination boundary. Comparing it with the CIELAB radial result isolates the
coordinate-space effect.

### Dated CSS Color 4 Binary Local MINDE

The Local-MINDE method is a separate relative-colorimetric algorithm. It keeps
destination-in-gamut colors unchanged, searches OkLCh chroma, and at each step
compares the candidate with its destination-RGB component-clipped version in
`deltaEOK`. When the local clip is below the `0.02` JND threshold, the clipped
color can be returned, allowing a small lightness or hue-coordinate change
instead of forcing further chroma reduction. The implementation matches
ColorAide 5.1 for the Display-P3-yellow reference within the draft algorithm's
search tolerance.

This method is for individual SDR colors. It is not a spatial image-rendering
intent and does not establish observer preference.

### Experimental protected-core compression

For destination boundary `D`, knee `K`, and input chroma `C`, the experimental
curve above the knee is:

```text
C' = K + (D - K)(C - K) / ((D - K) + (C - K))
```

It is continuous with unit slope at `K`, is strictly monotone, and remains
below `D` for every finite `C`. A knee fraction of `1` is rejected because it
would leave no compression span. The default `K = 0.75 D` deliberately changes
the in-gamut shoulder between `K` and `D`; only the protected core is an
identity region. Results report destination-boundary utilization so the
headroom cost remains visible.

### Secondary IPT hue audit

Holding a hue coordinate numerically does not guarantee constant perceived
hue. To measure that model dependence consistently, each modified input and
output was also
expressed in IPT and compared by its IPT hue angle. The transform follows the
[Ebner-Fairchild IPT model](https://library.imaging.org/cic/articles/6/1/art00003)
and is checked against independent MATLAB reference vectors for D65 white and
Display-P3 yellow.

Hue is marked undefined when opponent chroma is at most `0.001` of `|I|`, so
nominal grays do not acquire a meaningless angle from rounded transform
coefficients.

| Method | Modified chromatic samples | Median | 90th percentile | Maximum | Above 3° |
|---|---:|---:|---:|---:|---:|
| CIELAB radial | 94 | 0.722° | 2.781° | 12.692° | 8 |
| OkLCh radial | 94 | 0.409° | 3.368° | 10.260° | 10 |
| CSS Local MINDE | 94 | 1.637° | 4.806° | 9.220° | 23 |
| Experimental CIELAB soft knee | 108 | 1.086° | 5.720° | 12.961° | 29 |

Changing the radial coordinates from CIELAB to OkLCh reduced the median and
worst IPT-hue differences, but slightly increased the 90th percentile and the
count above 3°. Changing from OkLCh radial to Local MINDE reduced overall
CIEDE2000 displacement and the worst IPT-hue difference while increasing the
IPT-hue tail. No single scalar establishes a universally better result.
These are coordinate-model diagnostics on a synthetic grid, not observer
validation or image-quality preference data.

The separate [CAM16 equation audit](CAM16_EQUATION_AUDIT.md) now reproduces two
bounded model behaviors and a corrected Hellwig/Fairchild coefficient. It does
not provide the complete appearance model or a gamut-mapping rule. Any future
CAM-family mapper still needs a declared viewing-condition contract and an
independently specified compression algorithm; a uniform color-difference
space alone does not supply either one.

The soft method remains an experimental baseline rather than a completed
appearance model.

## Independent transform cross-check

Independently constructed LittleCMS sRGB and Display-P3 profiles agree with the
toolkit's common-gamut conversions to `1e-6` per encoded channel in both
directions. The comparison includes primaries, secondaries, neutrals, a sample
below the transfer-function breakpoint, and a 216-point sRGB cube. This supports
the RGB/XYZ transform path; LittleCMS is not used by the mapping calculation and
the comparison does not validate the boundary search or rendering intents.

## Synthetic study

The committed demonstrator samples a five-level cube in encoded Display-P3:
`5 × 5 × 5 = 125` points. It is a deterministic stress grid, not a probability
distribution of image colors.

| Metric | CIELAB radial | OkLCh radial | CSS Local MINDE | CIELAB soft knee |
|---|---:|---:|---:|---:|
| Input points outside sRGB | 94 / 125 | 94 / 125 | 94 / 125 | 94 / 125 |
| Points modified | 94 / 125 | 94 / 125 | 94 / 125 | 108 / 125 |
| Mean CIEDE2000 over all 125 points | 2.857 | 2.947 | 2.323 | 4.195 |
| Maximum CIEDE2000 | 23.928 | 9.956 | 7.602 | 24.026 |
| Worst grid point | P3 yellow | P3 red | P3 red | P3 yellow |
| P3-yellow output OkLCh chroma | 0.058 | 0.211 | 0.211 | 0.057 |
| Median radial-boundary utilization among modified points | 1.000 | 1.000 | not applicable | 0.913 |

Changing only the radial coordinate space from CIELAB to OkLCh retained much
more P3-yellow chroma (`0.211` instead of `0.058`) and reduced that sample's
CIEDE2000 from `23.928` to `5.523`. The grid mean increased slightly and the
worst point moved to red, so this is a targeted improvement rather than a
universal win.

Changing only the OkLCh algorithm from radial clipping to dated Local MINDE
reduced the grid mean from `2.947` to `2.323` and the maximum from `9.956` to
`7.602`. Its IPT-hue 90th percentile increased from `3.368°` to `4.806°`.
This is the intended controlled comparison: coordinate choice and algorithm
choice affect different parts of the result, and no one displacement scalar
establishes appearance preference.

The soft method changes 14 in-gamut shoulder points, increases average
displacement on this grid, and retains visible headroom. A smooth onset is not
automatically a better rendering result; its knee and perceptual coordinates
need application-specific evaluation.

P3 yellow also exposes a limitation of the radial baseline itself. Its input
chroma is `127.63`, but the ray's first neutral-connected Display-P3 boundary
is only `28.48` and the sRGB boundary is `22.74`; the legal source color lies
in a disconnected high-chroma region. Mapping to the first connected boundary
therefore removes `104.89 C*` and produces the largest displacement in the
study. This is useful for testing concave or re-entering gamut geometry, but it
also shows why fixed-L\*, fixed-Lab-hue radial clipping is a numerical baseline
rather than a generally acceptable rendering intent. The OkLCh methods reduce
that concrete failure without claiming that every color or image is improved.
[CSS Color 4 documents the related risk](https://www.w3.org/TR/2026/CRD-css-color-4-20260728/#excessive-chroma-reduction)
of excessive chroma reduction for light yellow in simple LCH mapping.

![Synthetic Display-P3 to sRGB mapping](../figures/gamut_mapping_synthetic.svg)

*Left: the CIELAB `a*b*` plane under the fixed-lightness, fixed-hue radial
baseline; each segment joins input to mapped chroma, so longer segments mean
more chroma was removed. Upper right: a paired 12-bin histogram of modified-
sample CIEDE2000 values for the radial and protected-core methods, with counts
normalized to the tallest bin. Lower right: all four methods compared by
modified-sample count, grid-mean CIEDE2000, and 90th-percentile IPT hue shift.*

## Limitations

- The gamut is defined by ideal encoding primaries, not a measured display or
  printer profile.
- Fixed CIELAB or OkLCh hue is a numeric constraint, not a perceptual-hue
  guarantee.
- IPT hue differences are a secondary coordinate-space diagnostic, not
  observer validation.
- The dated Local-MINDE implementation is one option in a work-in-progress CSS
  draft for individual SDR colors, not a complete spatial image-rendering
  intent or a promise of future browser behavior.
- The soft knee is a declared design choice rather than a standard rendering
  intent.
- The radial method can substantially overcompress colors on disconnected or
  shallow high-lightness rays; P3 yellow is the concrete counterexample in
  this study.
- The 125-point cube is a synthetic stress grid and does not estimate the
  frequency of out-of-gamut colors in photographs.
- The LittleCMS reference check covers the ideal common-gamut RGB transform;
  loading arbitrary ICC profiles and validating printer rendering remain out of
  scope.

## Engineering companion

The [gamut-mapping implementation companion](../implementation/gamut-mapping.md)
explains how the four scientific methods map to C++ and routes readers to the
public source, tests, and deterministic artifact generation.
