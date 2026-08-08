# Nikon D800/D810 + 50 mm f/1.4G slanted-edge SFR and field analysis

Slanted-edge spatial frequency response measures how much contrast a complete
capture system preserves as image detail becomes finer. This report examines
both aperture and position in the frame, because a center-only sharpness number
can hide field asymmetry. The D810 system peaks at f/5.6 in the center; the D800
system follows a different aperture trend and moves its strongest response away
from center at several apertures.

The sensor-linear green analysis accepted all 299 measured chart regions. SFR
includes lens, aperture, focus and alignment state, optical low-pass filtering,
sensor sampling, and processing together, so every value below is a
capture-system result rather than a camera-body or lens property.

[Case study](../case-studies/sfr-mtf-aperture-field.md) ·
[aggregate results CSV](../data/sfr_aperture_summary.csv)

![D800 and D810 SFR aperture and field summary](../figures/sfr_aperture_field.svg)

*Panel A: center MTF50 in cycles per pixel against aperture. Solid lines are
the sensor-linear green measurement; dashed lines are the advisory Imatest
values from a different luma/gamma path. The D810 peaks at f/5.6 while the D800
does not reproduce that trend. Panel B: paired bars for f/4–f/11, blue for D810
and orange for D800. Each bar is center minus strongest physical corner, so a
negative value means the corner measured sharper than the center.*

## Recorded capture configuration

All 18 sweep files record the same AF-S Nikkor 50mm f/1.4G lens model at 50 mm,
an approximate 0.84 m focus distance, and ISO 100. Archive observations and
manufacturer specifications remain separate evidence layers.

| Property | D810 set | D800 set |
|---|---|---|
| Files audited | 9/9 | 9/9 |
| Lens (EXIF) | AF-S Nikkor 50mm f/1.4G | AF-S Nikkor 50mm f/1.4G |
| Focal length / approximate focus distance | 50 mm / 0.84 m | 50 mm / 0.84 m |
| Focus mode | AF-S | Manual |
| `FocusPosition` raw code | `0x11` in all files | `0x11` in all files |
| Camera-clock window | 2016-12-09 17:53–17:54 | 2016-12-09 18:44–18:48 |
| Optical low-pass filter | absent | present |
| Lens serial | not recorded | not recorded |

The constant `FocusPosition` byte records the same opaque maker-note value; it
does not prove unchanged focus or focus accuracy. No lens serial is recorded,
so the archive establishes the same lens model, not the same physical sample.
The windows come from different camera bodies, and no clock-synchronization
record survives. They support ordering within each sweep but do not establish
elapsed time between sweeps or a shared physical lens.

Nikon specifies the D800 with an enhanced OLPF and the D810 without an OLPF.
Both are specified at 35.9 x 24.0 mm and 7360 x 4912 pixels, so their nominal
sampling pitch matches. Cycles/pixel is therefore not confounded by a nominal
pixel-pitch difference, although the measurement remains capture-system-specific.

## Measurement model

Each region uses black-subtracted green samples at their native Bayer
positions. A tilted edge is important because successive sensor rows cross it
at different sub-pixel offsets; projecting those samples perpendicular to the
edge reconstructs an edge-spread function (ESF) on a 0.25 px grid without
demosaic, gamma, or sharpening becoming part of the measured response.

```text
RAW Bayer mosaic
  -> black-subtracted native green samples
  -> per-line edge positions and fitted edge
  -> 0.25 px edge-spread function
  -> two-sided support gate and symmetric line-spread interval
  -> line-spread function and window
  -> Fourier magnitude and sampling correction
  -> MTF50, MTF at Nyquist, and 10–90% rise distance
```

For ESF samples `E_i` at spacing `Delta x = 0.25 px`, the line-spread function
(LSF), Hamming window, frequency axis, and adjacent-difference correction are:

```text
L_i     = E_(i+1) - E_i
w_i     = 0.54 - 0.46 cos(2 pi i / (N - 1))
f_k     = k / (N Delta x)                    [cycles/pixel]
A(f_k)  = sin(pi f_k Delta x) / (pi f_k Delta x)

MTF(f_k) = |sum_i L_i w_i exp(-j 2 pi k i / N)|
           / |sum_i L_i w_i| / A(f_k)
```

Here `E_i` is the normalized ESF, `L_i` is its adjacent-difference LSF, `w_i`
is the Hamming weight, `N` is the number of LSF samples, `k` is the
frequency-bin index, `j` is the imaginary unit, and `A` is the attenuation
introduced by adjacent differencing. `A(0) = 1` by continuity. MTF50 is the
first falling crossing of `MTF = 0.5`; MTF at sensor Nyquist is interpolated at
`0.5 cycles/pixel`. The 10–90% rise distance is the pixel distance between the
ESF's 0.1 and 0.9 crossings.

The edge position is estimated independently on each scan line and fitted by
least squares before projection. After the 10–90% crossings are found, the
analysis keeps the largest interval with equal measured support on both sides
of the transition. A region is refused if its shorter side has less than half
the support of its longer side; a window cannot reconstruct the missing side of
a badly placed ROI. This is a declared screening rule for this implementation,
not an ISO-conformance threshold. Regions with invalid geometry, non-finite
samples, weak contrast, excessive near-saturation, incomplete ESF support, or
missing MTF crossings are rejected rather than converted into
plausible-looking sharpness values.

For field analysis, the same estimator is applied to all 23 regions of interest
from one coherent per-file advisory table. Each row retains its grid position,
physical edge identity, field offset, rectangle, and independent reference
value. Invalid geometry, non-finite samples, weak edges, and incomplete
measurements are refused rather than converted into plausible-looking SFR
values.

## D810 50 mm center sweep

Input set: nine D810 aperture-sweep RAW captures with their matched advisory
tables.

The advisory rows come from one 10-Dec-2016 per-file batch; that is the Imatest
run date, not the capture date, which is 09-Dec-2016 for both sweeps. The center
edge is near-vertical in the analysis convention.

| Aperture | Accepted | Angle deg | Sensor-linear MTF50 | Advisory MTF50 | Delta | MTF@Nyq | R1090 px |
|---|---:|---:|---:|---:|---:|---:|---:|
| f/1.4 | true | -6.320 | 0.1075 | 0.1158 | -0.0083 | 0.0226 | 5.142 |
| f/1.8 | true | -6.320 | 0.0840 | 0.0899 | -0.0059 | 0.0806 | 6.852 |
| f/2 | true | -6.326 | 0.1081 | 0.1121 | -0.0040 | 0.0269 | 5.311 |
| f/2.8 | true | -6.428 | 0.1992 | 0.1707 | +0.0285 | 0.0971 | 2.727 |
| f/4 | true | -6.528 | 0.1997 | 0.1949 | +0.0048 | 0.0898 | 2.639 |
| f/5.6 | true | -6.423 | 0.2713 | 0.2400 | +0.0313 | 0.1730 | 2.249 |
| f/8 | true | -6.438 | 0.2202 | 0.2388 | -0.0186 | 0.1715 | 2.703 |
| f/11 | true | -6.419 | 0.2048 | 0.1989 | +0.0059 | 0.0819 | 3.060 |
| f/16 | true | -6.431 | 0.1668 | 0.1735 | -0.0067 | 0.0242 | 3.560 |

The predeclared D810 center trend passed:

```text
min(f/4,f/5.6,f/8,f/11) = 0.1997
f/16                    = 0.1668
max(f/1.4,f/1.8,f/2)    = 0.1081
argmax                  = f/5.6 at 0.2713
```

## D810 field result

All 92 ROIs across the four field apertures were accepted. Detected orientation
was near-vertical for every ROI; direction labels from the advisory table were
not used as a substitute for pixel measurement.

| Aperture | ROIs | Center MTF50 | Physical-corner max | Center − corner |
|---|---:|---:|---:|---:|
| f/4 | 23 | 0.1997 | 0.2008 | -0.0011 |
| f/5.6 | 23 | 0.2713 | 0.1998 | +0.0715 |
| f/8 | 23 | 0.2202 | 0.1958 | +0.0244 |
| f/11 | 23 | 0.2048 | 0.1823 | +0.0225 |

The f/4 near tie is retained. A strict center-above-corner rule would
misrepresent both the pixels and the advisory comparison.

## D800 replication and non-transfer finding

The D800 sweep used nine matching per-file advisory tables from one
10-Dec-2016 batch. All **207/207 ROIs** were accepted and detected as
near-vertical.

| Aperture | Advisory center | Sensor-linear center | Delta | Sensor-linear corner max | Center > corner (advisory / sensor-linear) | Argmax N (advisory / sensor-linear) |
|---|---:|---:|---:|---:|---|---|
| f/1.4 | 0.1029 | 0.1082 | +0.0053 | 0.0971 | true / true | 1 / 1 |
| f/1.8 | 0.1204 | 0.1307 | +0.0103 | 0.1056 | true / true | 1 / 1 |
| f/2 | 0.1377 | 0.1445 | +0.0068 | 0.1109 | true / true | 1 / 1 |
| f/2.8 | 0.1395 | 0.1443 | +0.0048 | 0.1529 | true / false | 12 / 8 |
| f/4 | 0.1385 | 0.1426 | +0.0041 | 0.1883 | false / false | 12 / 12 |
| f/5.6 | 0.1649 | 0.1648 | -0.0001 | 0.1886 | false / false | 12 / 12 |
| f/8 | 0.1831 | 0.1684 | -0.0147 | 0.1786 | true / false | 12 / 12 |
| f/11 | 0.1707 | 0.1674 | -0.0033 | 0.1592 | true / true | 12 / 12 |
| f/16 | 0.1583 | 0.1477 | -0.0106 | 0.1364 | true / true | 1 / 13 |

Load-bearing findings:

- **The D810 aperture gate correctly fails on D800.** D800 f/4 center is below
  f/16 in both the sensor-linear and advisory results.
- **The field maximum moves off center.** The advisory maximum is grid point
  N=12 at f/2.8 through f/11; the sensor-linear path agrees from f/4 through
  f/11.
- **Center/corner behavior is not monotonic.** At f/4 and f/5.6 the strongest
  physical corner exceeds center in both paths. At f/4 the sensor-linear corner is
  32% above its center. The advisory table for that file labels no ROI
  `Corner` — its regions are `Center` and `Pt Way` — so the comparable advisory
  figure is its most-peripheral ROI at 0.1647 against 0.1385 at center, +19%.
  The mid-aperture maximum is N=12 at 244 px horizontally and 1414 px above
  center, reading 0.2211 in the advisory path: +60% over center.
- **Strong evidence against a centered rotationally symmetric field, short of
  formal exclusion; the mechanism is unresolved.** An off-axis maximum does not
  exclude a centered radial response — it may peak on an annulus. Arbitrary
  near-radius pairs do not close it either: every ROI here uses a fixed-axis,
  near-vertical edge, so changing azimuth changes the sagittal/tangential
  mixture even for a centered rotationally symmetric lens.

  Near-mirror partners constrain it much harder. For an exactly vertical edge
  the S/T mixture depends on `|x| / r`, so an exact reflection through the
  horizontal axis (`y → -y`) would preserve both the radius and the mixture,
  and any centered rotationally symmetric system — astigmatic ones included —
  would have to return the same MTF50. The available ROI grid offers three
  near-reflections rather than exact ones:

  | Pair (upper / lower) | Position (x, y) px | Radius px | Radius mismatch | `\|x\|/r` mismatch | f/4 MTF50 | f/8 MTF50 |
  |---|---|---|---:|---:|---|---|
  | N=14 / N=16 | (-2797, -706) / (-2804, +608) | 2885 / 2869 | 0.54% | 0.80% | 0.1701 / 0.1175 (1.45x) | 0.1736 / 0.1536 (1.13x) |
  | N=2 / N=4 | (-2788, -1354) / (-2801, +1270) | 3099 / 3075 | 0.76% | 1.24% | 0.1883 / 0.1018 (1.85x) | 0.1737 / 0.1448 (1.20x) |
  | N=18 / N=20 | (-1504, -1387) / (-1499, +1266) | 2046 / 1963 | 4.06% | 3.78% | 0.1947 / 0.1102 (1.77x) | 0.1910 / 0.1529 (1.25x) |

  All three favor the upper field, at three radii and two apertures. The
  tightest pair — 0.54% in radius and 0.80% in mixture — still differs by 45% in
  MTF50. The residual mismatch also runs *against* the observed sign: in every
  pair the upper site sits at the larger radius, so a centered profile that
  falls with radius predicts the opposite ordering.

  This is strong evidence, not a formal exclusion. At the tightest pair, MTF50
  changes by 45% while nominal radius and `|x|/r` differ by only 0.54% and
  0.80%. But the archive records the edges only as near-vertical, so a centered
  radius-plus-orientation response could distribute the MTF50 difference across
  both variables; the data cannot assign all 45% to radial slope. A formal
  exclusion needs controlled radial/tangential edge orientations or a fitted
  radial-plus-orientation baseline, which this archive does not contain.

  The cause is separately unresolved. Target, sensor or focus-plane tilt,
  decentering, and capture alignment all produce an upper/lower imbalance, and
  the imbalance falling from 1.77x to 1.25x between f/4 and f/8 does not
  separate them: stopping down reduces coma, astigmatism, spherical aberration
  and decentering signatures as well as widening depth-of-field tolerance.
- **Off-axis reference deltas are larger.** Green-linear CFA SFR and
  rendered-luma processing diverge more away from center. The disagreement is
  reported rather than used as evidence of equivalence.
- **The low D800 center response is not analysis-path-only.** Center agreement
  within ±0.015 cycles/pixel across the sensor-linear and advisory paths makes a
  gross sensor-linear-path numerical artifact less likely. It does not identify the
  physical cause because both paths operate on the same captures.
- D800 center values remain below D810 at matched plateau apertures, but the
  difference cannot be attributed to optical low-pass filtering alone because
  focus mode, capture/alignment state, and focus accuracy also differ. OLPF
  design is a plausible body-side contributor, but its magnitude is not isolated
  here. A common lens-model label with no recorded lens serial does not establish
  a shared lens sample or authorize ranking the lens, body, and setup
  contributions.

## Measurement cross-check

Archive verification retained all 207 D800 and 92 D810 accepted regions. Two D810
corner values differ by 0.0001 cycles/pixel at four-decimal reporting precision
without changing acceptance, aperture ordering, or the interpretations above.

## Interpretation limits

- The sensor-linear analysis does not claim absolute Imatest equivalence. Repeated advisory
  batches disagree with each other at some apertures.
- The path is linear green CFA, not demosaiced/luma/gamma parity.
- MTF is reported in cycles/pixel; no lp/mm or LW/PH conversion is claimed.
- Field maps sample the provided slanted edges. They are not a complete
  sagittal/tangential lens model.
- This is a system SFR study, not a lens characterization. Lens-sample identity,
  controlled refocusing, and repeat captures are all absent from the archive.

## Engineering companion

The [SFR implementation companion](../implementation/sfr-mtf.md) explains how
the measurement model is realized in C++ and routes readers to the public
source, tests, and figure generation.
