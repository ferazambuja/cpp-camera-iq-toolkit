# ColorChecker extraction and CCM validation

## What this is about

A camera's RAW RGB values are not colorimetry. Two cameras photographing the
same chart under the same light record different numbers, and neither set
matches CIE XYZ, because each sensor's spectral sensitivities differ from the
human observer's. This study closes part of that gap with a color-correction
matrix: a linear 3×3 fit from one camera's RGB into XYZ for a declared capture
condition.

The difficulty is that such a matrix is easy to make look good. Judging it only
on the patches used for fitting can hide poor generalization, while a chart-order
mistake, a mislocated patch grid, a flat frame near clipping, or flare in the
dark patches can each distort the result. This study therefore reports error on
patches excluded from the fit, rather than optimizing a single color-difference
number.

On this corrected 140-patch workflow, five-fold held-out mean CIEDE2000 was
**4.134** against a compatible spectral reference. CIEDE2000 is a perceptual
color-difference measure in which lower values indicate a closer match and
roughly 1 is a just-noticeable difference, so 4.134 is a visible error — which
is what a single linear 3×3 matrix can deliver across a 140-patch chart
spanning the full gamut, since no such matrix can undo a sensor's departure
from the human observer.

Two numbers matter more than the headline. Training error was **4.099**, so the
held-out result is worse by only **0.035**: the matrix is not overfit to the
patches it was fitted on, which is what the five-fold design existed to test.
And the reference itself carries **mean ΔE76 1.34** against manufacturer
nominal values across all 140 patches, which bounds how much of the 4.134 can
be attributed to the camera at all. The result demonstrates the workflow and
its validation controls; it is not a per-unit chart calibration or a claim
about every scene and illuminant.

[Documentation index](../README.md) ·
[CCM report](../reports/CCM_FIT.md) ·
[patch report](../reports/PATCH_EXTRACTION.md) ·
[reference notes](../reports/SG_REFERENCE_PROVENANCE.md) ·
[aggregate CSV](../data/ccm_validation_summary.csv) ·
[implementation companion](../implementation/color-characterization.md)

The retained study material includes the RAW, flat-field, and dark captures but
not an exact per-unit spectral measurement of the photographed chart. The
analysis therefore uses a compatible SG spectral
reference verified against manufacturer nominal values. This preserves a
physically specified 140-patch target for the fit and held-out validation
while bounding the result to compatible-reference, not per-unit, color
difference.

![ColorChecker CCM validation summary](../figures/ccm_validation.svg)

*Lower CIEDE2000 is better. The first two groups compare fit/evaluation mean
with five-fold held-out mean for all 140 patches and for the 112-patch
`L* >= 25` kept set, where `L*` is perceptual lightness. The last two bars
evaluate that kept-set fit on all 140 patches and on the 28 excluded dark
patches. Keeping those evaluations visible
is why the lower kept-set number is reported as a flare-handling decision, not
as a better camera model.*

![Reduced crop of the ColorChecker-SG patch grid used for the physical capture](../images/colorchecker-sg-patch-grid.jpg)

*Illustrative crop from the source test capture. Numerical analysis samples
rectangular patch interiors after RAW unpacking, black subtraction, and bilinear
demosaic; this reduced image is not a calibration reference.*

## Method

Each of the 140 chart patches is sampled as a rectangle in the RAW frame after
black subtraction and demosaic. Before any color fit, two corrections are
applied and both are measured rather than assumed: the available flat
compensates its measured capture-system field, and white balance sets the
neutral axis. The flat cannot separate sphere, lens, and sensor contributions,
so this is same-aperture correction rather than a component calibration. A flat
frame that is itself near sensor clipping would silently distort the correction,
so candidate flats are screened and rejected on that basis.

The reference side comes from the chart's spectral reflectances rendered under
an explicitly measured sphere spectrum — the illuminant is taken from the
measurement sidecars, not inferred from EXIF or capture dates — which yields
the XYZ each patch should produce. Fitting
a linear 3×3 matrix by least squares from camera RGB to those XYZ values gives
the color-correction matrix. Error is reported as CIEDE2000, and the fit is
repeated over five deterministic patch partitions so the quoted error comes from
patches the matrix never saw. Dark patches are labeled separately, because they
are where the two known error sources concentrate: flare in a bright-surround
capture lands there first, and a compatible rather than per-unit reference is
proportionally least reliable where reflectance is lowest. These data cannot
separate the two.

## Cross-checks

Uncorrected patch extraction matched the reference-tool averages with
correlations above **0.99999998** and direct RMSE of **0.352 / 0.041 / 0.381
DN** for R/G/B. The direct-vs-flipped orientation controls separated clearly.

The corner-seeded grid was not used for the reported CCM result: although RGB
correlations remained above 0.999, generated centers missed the manually checked
reference positions by up to 16.449 px. RawDigger rectangles therefore remain
the coordinate source for the reported CCM result.

## Findings

The corrected 140-patch RAW path produced:

- training mean CIEDE2000: **4.099**;
- deterministic five-fold held-out mean CIEDE2000: **4.134**;
- dark-patch mean CIEDE2000 for `L* < 25`: **7.890**.

Held-out error exceeds training error by 0.035, so the matrix generalizes to
patches it never saw; that near-zero gap is the payoff of the five-fold design.

An explicit `L* >= 25` kept-set fit lowered held-out mean CIEDE2000 to 3.221.
That looks like a 22% improvement, and it is not one: under that fit all
patches still measured 4.126 — statistically unchanged from 4.134 — and the
excluded dark subset measured 7.952, slightly worse than before. Nothing about
the camera model improved; the error was relocated out of the reported average.
The reduction is therefore recorded as a flare-handling choice, and 4.134
remains the quoted result.

## What the correction flat contributes

The flat used to correct these patches is the accepted f/8, 1/1000 s frame
characterized in the
[CFA flat-field case study](cfa-flat-field-response.md), so it is measured
rather than assumed. It is the repeat frame of the matched 1/1000 s pair, so its
figures differ slightly from the primary frame quoted there: green falls to
0.4816 of center, `C_BG` reaches 1.0447,
`C_RG` falls to 0.9773, and it is 0% near ceiling in both the whole frame and
the centered gate. Dividing by it therefore removes a strong intensity gradient
and a smaller chromatic one across the chart area. What it cannot do is separate
sphere nonuniformity from camera response, so this path is
same-aperture-corrected, not shading-calibrated.

Flat-field admission uses the same per-sensor-position limits as the dedicated
flat-field analysis, including separate whole-frame and bright-center
measurements. This rejects a frame whose center is near the sensor ceiling even
when the whole-frame statistic looks safe. The selected 1/1000 s flat passes
both tests; its mean valid signal supplies the correction normalization.

## What the result does not establish

The spectral reference is a compatible SG reference verified against
manufacturer nominal values; it is not proven to be the exact per-unit chart
used for the capture. Held-out folds are deterministic patch partitions, not a
second physical chart session. The corrected evidence is scoped to the f/8
capture because the available f/9 same-aperture sphere flats were too close to
sensor ceiling.
