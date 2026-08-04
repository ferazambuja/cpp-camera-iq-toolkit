# CFA Flat-Field Response in a Uniform-Field Capture

[Detailed method report](../reports/FLAT_FIELD_RESPONSE.md) ·
[aggregate response maps](../data/flat_field_response.csv) ·
[52-frame screening table](../data/flat_field_summary.csv) ·
[implementation](../../src/shading.cpp) ·
[tests](../../tests/test_shading.cpp)

## What this is about

A flat-field measurement asks a simple question: if a camera photographs an
evenly illuminated surface, how even is the recorded image? Signal commonly
falls toward the corners and can fall by different amounts in the red, green,
and blue samples. Cameras compensate for this with shading correction, often
starting from a simplified model in which response changes smoothly with
distance from the image center. This study tests that assumption on an archived
integrating-sphere capture set. The sphere provides a controlled luminous field;
the RAW sensor mosaic keeps the individual color responses visible.

The integrating-sphere port provided the controlled field, but residual source
nonuniformity was not independently isolated from the camera/lens response by
source or camera rotation. The result therefore stays at capture-system scope.
Most frames were also too bright to use: near clipping, the apparent falloff
flattens and understates response variation, so each frame was screened for
headroom before measurement.

## Headline finding

Three of 52 integrating-sphere captures retained usable headroom; the primary
f/8 frame showed 19.65% green-field quadrant asymmetry, exceeding the declared
5% criterion and conflicting with a centered radial scalar model for the
measured field. The other 49 frames were too close to the sensor ceiling for a
trustworthy falloff measurement: clipping would make the field look flatter
than it was. The result remains a capture-system characterization because the
available captures do not separate illumination nonuniformity from lens,
alignment, mechanical shading, or sensor-angular effects.

The retained Fujifilm X-T100 and Fujinon XF 14 mm f/2.8 R sphere and dark
captures are sufficient to detect and quantify composite-field asymmetry.
Isolating its source would require an independent map of the sphere port and
repeat captures with the source or camera rotated. The study therefore reports
what the complete capture system did rather than assigning a lens correction.

![CFA flat-field response summary](../figures/flat_field_response.svg)

*The figure shows the accepted f/8, 1/1000 s primary frame. Each heatmap divides
the image into a 16 × 12 grid and expresses every cell relative to a central
reference area. The green map shows brightness response; the red-to-green and
blue-to-green maps show how color balance changes across the field. The 19.65%
quadrant asymmetry exceeds the declared 5% project policy and is inconsistent
with a centered radial scalar model for the measured composite. It does not
identify the responsible component; the missing source and rotation controls
preclude isolated lens attribution. The matched repeat measured 19.996%, only 0.348
percentage points away. That supports stability of the large observed
asymmetry, but two frames do not derive or validate the 5% policy threshold.*

## What the measurement found

The archive contains 52 sphere frames spanning f/5.6, f/8, and f/9. Screening
for samples near the sensor ceiling accepted three f/8 frames and rejected 49.
Every f/5.6 and f/9 frame was too close to that ceiling, so the dataset cannot
support an aperture trend.

For the primary accepted frame:

- green relative response ranged from 0.4801 to 1.0005;
- the center-normalized red-to-green ratio ranged from 0.9773 to 1.0000;
- the center-normalized blue-to-green ratio ranged from 0.9997 to 1.0447;
- the ratio between the two green sensor positions stayed between 0.9989 and
  1.0023;
- the matched repeat frame differed by at most 0.3787 percentage points over
  the 16 corner/plane comparisons, with 0.1813 pp RMS;
- a dark frame taken under compatible exposure metadata produced a 0 DN
  full-frame median residual at all four sensor positions after black
  subtraction.

The strong green-response falloff is accompanied by much smaller
chromatic variation and close agreement between the two green positions. The
left/right and top/bottom imbalance remains the controlling interpretation:
one uniform-field capture cannot determine which physical component caused it.

The imbalance keeps its orientation. In all three accepted frames the minimum
green bin is the same bottom-left grid cell and the top-right bin is the
brightest corner, so the gradient is fixed in the capture geometry rather than
frame-specific. That constrains it without attributing it. The third accepted
frame is less asymmetric: at 1/1600 s the quadrant statistic measures 16.09%,
compared with 19.65% and 20.00% for the 1/1000 s pair. That spread is roughly
ten times the difference within the pair. All three exceed the 5% policy, so the
model verdict holds, but the pair difference is not a general repeatability
figure.

## Why the bright center is screened separately

The f/8, 1/500 s frame is a useful negative case. Its worst green plane was
11.6319% near ceiling inside the central region but only 0.4964% near ceiling
over the whole frame. A whole-frame 1% test would accept it even though the
bright central region was already near ceiling. The frame is rejected before
any response map is produced, rather than corrected for afterwards.

The same frame is also a negative case for the flat used in the ColorChecker
correction. Both analysis paths inspect the source mosaic before demosaic, use
the same centered region, and decide separately for each of the four sensor
positions. On the 1/500 s frame, no position exceeds 0.50% near ceiling over
the whole image, but the two green positions reach 8.69% and 11.63% in the
center. The second green position therefore rejects the frame. The 1/1000 s
flat used by the ColorChecker correction measures 0% near ceiling in both
regions at every position and remains accepted. The available evidence does not
quantify the correction error that the rejected flat would introduce, so no
magnitude is claimed.

## How it was measured

![Reduced view of the integrating-sphere capture](../images/flat-field-sphere.jpg)

*This metadata-stripped, reduced JPEG illustrates the physical sphere field and
its visible asymmetry. It is a rendered guide only. Numerical measurements use
the source RAF's active Bayer mosaic, not this preview.*

The measurement proceeds in four stages:

1. The active 2 × 2 Bayer mosaic is read and the sensor's per-position black
   pedestal is subtracted once, so every later value is signal above black
   rather than raw code.
2. Each position's usable signal range is its white level minus that pedestal.
   Measuring headroom against this range, rather than against the raw code
   ceiling, is what makes the near-ceiling screening meaningful.
3. Median response is computed per color position over a 16 × 12 spatial grid.
   A 400 × 400 px center block sets the normalizer so the maps read as response
   relative to center; four inset corner blocks supply the asymmetry statistics.
4. Normalizing R, G1, G2, and B independently and then taking their ratios
   separates chromatic falloff from the much larger overall falloff — otherwise
   the two are indistinguishable in a single luminance map.

Medians rather than means are used per bin so that isolated defective pixels
cannot shift a whole spatial cell. Synthetic fields with known shapes — radial,
asymmetric, and channel-mismatched — confirm the measurement recovers the field
it is given.

## What the result does not establish

The result does not provide a correction gain map, source-uniformity
calibration, or camera-only color-shading measurement. Those require additional
capture controls. Applying a correction would also require a separate
remeasurement loop before the workflow could be called calibration.
