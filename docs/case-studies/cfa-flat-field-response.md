# CFA Flat-Field Response in a Uniform-Field Capture

## What this is about

A flat-field measurement asks a simple question: if a camera photographs an
evenly illuminated surface, how even is the recorded image? Signal commonly
falls toward the corners and can fall by different amounts in the red, green,
and blue samples. Cameras compensate for this with shading correction, often
starting from a simplified model in which response changes smoothly with
distance from the image center. This study tests that assumption on an archived
integrating-sphere capture set. The sphere provides a controlled luminous field,
and the measurement works directly on the colour filter array — the mosaic of
red, green, and blue filters laid over the sensor — so each colour's response
stays separately visible instead of being blended together first.

The integrating-sphere port provided the controlled field, but residual source
nonuniformity was not independently isolated from the camera/lens response by
source or camera rotation. The result therefore stays at capture-system scope.
Most frames were also too bright to use: near clipping, the apparent falloff
flattens and understates response variation, so each frame was screened for
headroom before measurement.

[Detailed method report](../reports/FLAT_FIELD_RESPONSE.md) ·
[aggregate response maps](../data/flat_field_response.csv) ·
[52-frame screening table](../data/flat_field_summary.csv) ·
[implementation companion](../implementation/flat-field.md)

## Headline finding

Only three of the 52 sphere frames were usable. In the other 49 the brightest
parts of the frame sat close to the sensor's ceiling, where clipping flattens
the falloff and makes the field look more even than it is — so those frames
would have understated the very thing being measured.

In the primary usable frame the green response is uneven between corners:
comparing the brightest and darkest of the four corner blocks, and scaling that
spread by their average, gives 19.65% — nearly four times the 5% the project
treats as acceptable. Those four blocks are positioned at equal distance from
the frame centre, which is what makes the number an argument rather than an
observation: a response that depends only on distance from the centre must give
all four the same value, so any spread at all falsifies that model. A spread of
19.65% is not a marginal failure of it. What the measurement cannot say is which
component is responsible, because these captures do not separate the sphere's
own unevenness from the lens, the alignment, or the sensor's response to
off-axis light.

The retained Fujifilm X-T100 and Fujinon XF 14 mm f/2.8 R sphere and dark
captures are sufficient to detect and quantify composite-field asymmetry.
Isolating its source would require an independent map of the sphere port and
repeat captures with the source or camera rotated. The study therefore reports
what the complete capture system did rather than assigning a lens correction.

![CFA flat-field response summary](../figures/flat_field_response.svg)

*The accepted f/8, 1/1000 s primary frame. Across the top, the four measurement
stages. Below, three maps of the frame: each divides the image into a 16 × 12
grid and shows every cell's median relative to a 400 × 400 px block at the
centre. **The three maps use different colour scales** — the green map spans
0.45–1.05, the two colour-ratio maps only 0.97–1.03 and 0.97–1.05 — so a strong
colour in the left map means a change roughly ten times larger than the same
colour on the right. That contrast is the finding: brightness falls to about
half at the worst corner while colour balance moves by a few percent. Four
separately measured 400 × 400 px corner blocks, inset to sit at equal distance
from the centre, spread by 19.65% of their own average — which a centred radial
model cannot produce, since it would assign all four the same value. The
matched repeat measured 19.996%, closely
repeating the large asymmetry; two frames do not validate the 5% threshold, and
nothing here identifies which component caused it.*

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
frame is less asymmetric: at 1/1600 s the corner-field statistic measures 16.09%,
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

## What to take from this

The opening asked whether a camera's response to an evenly illuminated surface
is symmetric about the image centre, because that assumption is where shading
correction usually starts. For this capture system it is not. Four blocks at
equal distance from the centre spread by 19.65% of their average, and a field
that depends only on radius must give all four the same value, so the model is
excluded rather than merely strained.

The practical consequence is that a centred radial correction cannot be fitted
to this system without leaving a known residual — a full per-position map is
required, which is what the ColorChecker workflow uses. The measurement does
not say which component caused the asymmetry, and the experiment that would is
specific and cheap: repeat the capture with the source or camera rotated. If
the pattern rotates with the sphere it is the source; if it stays with the
frame it is the camera and lens.
