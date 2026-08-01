# D800/D810 + 50 mm f/1.4G slanted-edge SFR and field analysis

Evidence runs: 2026-07-08<br>
Implementation validation refreshed: 2026-07-28

**Result:** the C++ green-linear SFR path accepted all 299 field ROIs in the
published D800/D810 sweeps. The D810 center response peaked at f/5.6 and showed
positive center-to-corner margin from f/5.6 through f/11. The same trend did not
transfer to the D800; that capture system retained a different center curve and
field maximum. Slanted-edge SFR measures lens, aperture, focus and alignment
state, OLPF, sampling, and processing together, so every value below is a
capture-system result rather than a camera-body or lens property.

[Case study](../case-studies/sfr-mtf-aperture-field.md) ·
[publication-safe aggregate CSV](../data/sfr_aperture_summary.csv) ·
[archive and oracle contract](SFR_MTF_ARCHIVE_INVENTORY.md)

![D800 and D810 SFR aperture and field summary](../figures/sfr_aperture_field.svg)

## Recorded capture configuration

All 18 sweep files record the same AF-S Nikkor 50mm f/1.4G lens model at 50 mm,
an approximate 0.84 m focus distance, and ISO 100. The
[capture-metadata audit](SFR_MTF_ARCHIVE_INVENTORY.md#capture-metadata-audit)
separates archive observations from manufacturer specifications.

| Property | D810 set | D800 set |
|---|---|---|
| Files audited | 9/9 | 9/9 |
| Lens (EXIF) | AF-S Nikkor 50mm f/1.4G | AF-S Nikkor 50mm f/1.4G |
| Focal length / approximate focus distance | 50 mm / 0.84 m | 50 mm / 0.84 m |
| Focus mode | AF-S | Manual |
| `FocusPosition` raw code | `0x11` in all files | `0x11` in all files |
| Capture time | 2016-12-09 17:53–17:54 | 2016-12-09 18:44–18:48 |
| Optical low-pass filter | absent | present |
| Lens serial | not recorded | not recorded |

The constant `FocusPosition` byte records the same opaque maker-note value; it
does not prove unchanged focus or focus accuracy. No lens serial is recorded,
so the archive establishes the same lens model, not the same physical sample.
Captures about 50 minutes apart make a single copy plausible but unverified.

Nikon specifies the D800 with an enhanced OLPF and the D810 without an OLPF.
Both are specified at 35.9 x 24.0 mm and 7360 x 4912 pixels, so their nominal
sampling pitch matches. Cycles/pixel is therefore not confounded by a nominal
pixel-pitch difference, although the measurement remains capture-system-specific.

## Measurement model

The `sfr` command:

- reads LibRaw active-area, black-subtracted Bayer samples;
- uses green CFA positions in native sensor coordinates;
- converts a full-frame advisory ROI into active-area coordinates;
- estimates the slanted edge from per-line derivative centroids;
- projects samples into a 0.25 px ESF and bounds missing-bin interpolation;
- differentiates to an LSF, applies a Hamming window, and runs an in-repo DFT;
- applies adjacent-difference attenuation correction;
- reports MTF50, MTF50P, MTF at sensor Nyquist, R1090, edge angle, saturation,
  rejection diagnostics, and filename/EXIF checks.

`--field-map` applies the same estimator to all 23 ROIs in one coherent
per-file advisory table. The parser preserves row, grid/edge identity,
field offset, ROI, and reference values, but emits only basenames for any
referenced summary file.

The implementation validates finite/ranged options at CLI and public-library
boundaries, rejects non-finite RAW samples and metadata, bounds ESF allocation,
uses the configured minimum scan-line sample count, validates oracle geometry
and duplicate rows, and emits measurement-only values as JSON `null` when a
center is rejected.

## D810 50 mm center sweep

Dataset label: `archive:2016_esensi_images/2016_12_09_D810_SFR/`

The advisory rows come from one 10-Dec-2016 per-file batch; that is the Imatest
run date, not the capture date, which is 09-Dec-2016 for both sweeps. The center
edge is near-vertical in the toolkit convention.

| Aperture | Accepted | Angle deg | Toolkit MTF50 | Advisory MTF50 | Delta | MTF@Nyq | R1090 px |
|---|---:|---:|---:|---:|---:|---:|---:|
| f/1.4 | true | -6.320 | 0.1074 | 0.1158 | -0.0084 | 0.0210 | 5.142 |
| f/1.8 | true | -6.320 | 0.0837 | 0.0899 | -0.0062 | 0.0802 | 6.852 |
| f/2 | true | -6.326 | 0.1085 | 0.1121 | -0.0036 | 0.0271 | 5.311 |
| f/2.8 | true | -6.428 | 0.1992 | 0.1707 | +0.0285 | 0.0928 | 2.727 |
| f/4 | true | -6.528 | 0.2000 | 0.1949 | +0.0051 | 0.0894 | 2.639 |
| f/5.6 | true | -6.423 | 0.2714 | 0.2400 | +0.0314 | 0.1727 | 2.249 |
| f/8 | true | -6.438 | 0.2218 | 0.2388 | -0.0170 | 0.1682 | 2.703 |
| f/11 | true | -6.419 | 0.2049 | 0.1989 | +0.0060 | 0.0862 | 3.060 |
| f/16 | true | -6.431 | 0.1666 | 0.1735 | -0.0069 | 0.0237 | 3.560 |

The predeclared D810 center trend passed:

```text
min(f/4,f/5.6,f/8,f/11) = 0.2000
f/16                    = 0.1666
max(f/1.4,f/1.8,f/2)    = 0.1085
argmax                  = f/5.6 at 0.2714
```

## D810 field result

All 92 ROIs across the four field apertures were accepted. Detected orientation
was near-vertical for every ROI; direction labels from the advisory table were
not used as a substitute for pixel measurement.

| Aperture | ROIs | Center MTF50 | Physical-corner max | Center − corner |
|---|---:|---:|---:|---:|
| f/4 | 23 | 0.2000 | 0.2005 | -0.0005 |
| f/5.6 | 23 | 0.2714 | 0.2001 | +0.0712 |
| f/8 | 23 | 0.2218 | 0.1955 | +0.0263 |
| f/11 | 23 | 0.2049 | 0.1830 | +0.0219 |

The f/4 near tie is retained. A strict center-above-corner rule would
misrepresent both the pixels and the advisory comparison.

## D800 replication and non-transfer finding

The D800 sweep used nine matching per-file advisory tables from one
10-Dec-2016 batch. All **207/207 ROIs** were accepted and detected as
near-vertical.

| Aperture | Advisory center | Toolkit center | Delta | Toolkit corner max | Center > corner (advisory / toolkit) | Argmax N (advisory / toolkit) |
|---|---:|---:|---:|---:|---|---|
| f/1.4 | 0.1029 | 0.1082 | +0.0053 | 0.0978 | true / true | 1 / 1 |
| f/1.8 | 0.1204 | 0.1304 | +0.0100 | 0.1058 | true / true | 1 / 1 |
| f/2 | 0.1377 | 0.1439 | +0.0062 | 0.1113 | true / true | 1 / 1 |
| f/2.8 | 0.1395 | 0.1447 | +0.0052 | 0.1535 | true / false | 12 / 8 |
| f/4 | 0.1385 | 0.1428 | +0.0043 | 0.1885 | false / false | 12 / 12 |
| f/5.6 | 0.1649 | 0.1647 | -0.0002 | 0.1886 | false / false | 12 / 12 |
| f/8 | 0.1831 | 0.1684 | -0.0147 | 0.1849 | true / false | 12 / 12 |
| f/11 | 0.1707 | 0.1674 | -0.0033 | 0.1592 | true / true | 12 / 12 |
| f/16 | 0.1583 | 0.1478 | -0.0105 | 0.1367 | true / true | 1 / 13 |

Load-bearing findings:

- **The D810 aperture gate correctly fails on D800.** D800 f/4 center is below
  f/16 in both toolkit and advisory results.
- **The field maximum moves off center.** The advisory maximum is grid point
  N=12 at f/2.8 through f/11; the toolkit agrees from f/4 through f/11.
- **Center/corner behavior is not monotonic.** At f/4 and f/5.6 the strongest
  physical corner exceeds center in both paths. At f/4 the toolkit corner is
  32% above its center. The advisory table for that file labels no ROI
  `Corner` — its regions are `Center` and `Pt Way` — so the comparable advisory
  figure is its most-peripheral ROI at 0.1647 against 0.1385 at center, +19%.
  The mid-aperture maximum is N=12 at 244 px horizontally and 1414 px above
  center, reading 0.2211 in the advisory path: +60% over center.
- **The field is not centered rotationally symmetric; the mechanism is still
  unresolved.** An off-axis maximum does not exclude a centered radial
  response — it may peak on an annulus. Arbitrary near-radius pairs do not close
  it either: every ROI here uses a fixed-axis, near-vertical edge, so changing
  azimuth changes the sagittal/tangential mixture even for a centered
  rotationally symmetric lens.

  Near-mirror partners constrain it much harder. For an exactly vertical edge
  the S/T mixture depends on `|x| / r`, so an exact reflection through the
  horizontal axis (`y → -y`) would preserve both the radius and the mixture,
  and any centered rotationally symmetric system — astigmatic ones included —
  would have to return the same MTF50. The available ROI grid offers three
  near-reflections rather than exact ones:

  | Pair (upper / lower) | Position (x, y) px | Radius px | Radius mismatch | `\|x\|/r` mismatch | f/4 MTF50 | f/8 MTF50 |
  |---|---|---|---:|---:|---|---|
  | N=14 / N=16 | (-2797, -706) / (-2804, +608) | 2885 / 2869 | 0.54% | 0.80% | 0.1403 / 0.1054 (1.33x) | 0.1566 / 0.1399 (1.12x) |
  | N=2 / N=4 | (-2788, -1354) / (-2801, +1270) | 3099 / 3075 | 0.76% | 1.24% | 0.1647 / 0.0945 (1.74x) | 0.1618 / 0.1365 (1.19x) |
  | N=18 / N=20 | (-1504, -1387) / (-1499, +1266) | 2046 / 1963 | 4.06% | 3.78% | 0.1878 / 0.1055 (1.78x) | 0.1889 / 0.1507 (1.25x) |

  All three favor the upper field, at three radii and two apertures. The
  tightest pair — 0.54% in radius and 0.80% in mixture — still differs by 33% in
  MTF50. The residual mismatch also runs *against* the observed sign: in every
  pair the upper site sits at the larger radius, so a centered profile that
  falls with radius predicts the opposite ordering.

  This is strong evidence, not a formal exclusion. Reproducing it with a
  centered rotationally symmetric response would require a radial profile that
  rises by 33% across a 0.54% step in radius, plus edges vertical enough for
  `|x|/r` to describe the mixture — the archive only records them as
  near-vertical. Both are implausible; neither is ruled out by arithmetic here.
  A formal exclusion needs controlled radial/tangential edge orientations or a
  fitted radial-plus-orientation baseline, which this archive does not contain.

  The cause is separately unresolved. Target, sensor or focus-plane tilt,
  decentering, and capture alignment all produce an upper/lower imbalance, and
  the imbalance falling from 1.78x to 1.25x between f/4 and f/8 does not
  separate them: stopping down reduces coma, astigmatism, spherical aberration
  and decentering signatures as well as widening depth-of-field tolerance.
- **Off-axis reference deltas are larger.** Green-linear CFA SFR and
  rendered-luma processing diverge more away from center. The disagreement is
  reported rather than used as evidence of equivalence.
- **The low D800 center response is not toolkit-only.** Center agreement within
  ±0.015 cycles/pixel across the toolkit and advisory paths makes a gross
  toolkit-specific numerical artifact less likely. It does not identify the
  physical cause because both paths operate on the same captures.
- D800 center values remain below D810 at matched plateau apertures, but the
  difference cannot be attributed to optical low-pass filtering alone because
  focus mode, capture/alignment state, and focus accuracy also differ. OLPF
  design is a plausible body-side contributor, but its magnitude is not isolated
  here. A common lens model and a plausible single copy do not authorize ranking
  the lens, body, and setup contributions.

## Validation

Automated coverage includes:

- synthetic vertical and horizontal edges;
- MTF50/MTF50P, R1090, Nyquist, sinc correction, and DFT behavior;
- CFA-balanced ROI conversion and saturation rejection;
- missing/oversized ESF grids and bounded gap interpolation;
- non-finite inputs, option ranges, unknown options, and conflicting ROI modes;
- center/field advisory parsing, duplicate rows, geometry overflow, and missing
  required rows;
- rejected-result JSON nullability with retained diagnostics;
- D810 and D800 trend/field fixture pins, including the intentional D800 gate
  failure.

Local archive verification after validation hardening retained all 207 D800 and
92 D810 accepted ROIs. Acceptance, aperture ordering, and the engineering
interpretations above were unchanged; two D810 corner values moved by 0.0001
cycles/pixel at four-decimal reporting precision.

## Interpretation limits

- The toolkit does not claim absolute Imatest equivalence. Repeated advisory
  batches disagree with each other at some apertures.
- The path is linear green CFA, not demosaiced/luma/gamma parity.
- MTF is reported in cycles/pixel; no lp/mm or LW/PH conversion is claimed.
- Field maps sample the provided slanted edges. They are not a complete
  sagittal/tangential lens model.
- This is a system SFR study, not a lens characterization. Lens-sample identity,
  controlled refocusing, and repeat captures are all absent from the archive.

## Implementation and tests

- [`include/camera_iq/sfr.hpp`](../../include/camera_iq/sfr.hpp)
- [`src/sfr.cpp`](../../src/sfr.cpp)
- [`src/cmd_sfr.cpp`](../../src/cmd_sfr.cpp)
- [`tests/test_sfr.cpp`](../../tests/test_sfr.cpp)
- [`tests/test_cmd_sfr.cpp`](../../tests/test_cmd_sfr.cpp)
- [`tools/generate_portfolio_figures.py`](../../tools/generate_portfolio_figures.py)
