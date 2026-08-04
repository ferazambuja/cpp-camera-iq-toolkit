# Relative CFA Flat-Field Response

A nominally uniform field reveals how much recorded signal changes across the
image and whether the individual color-filter-array (CFA) samples fall off
together. This report tests whether a centered radial model describes the
retained sphere captures. It does not: the primary accepted frame shows a
19.65% spread between its brightest and darkest green corner blocks, scaled by
their average, while the available controls cannot isolate the
source, lens, alignment, or sensor contribution.

Dataset: `clrs589_project_camera` Fujifilm X-T100 sphere and dark captures  
Result type: capture-system field characterization in black-subtracted DN

[Case study](../case-studies/cfa-flat-field-response.md) ·
[response table](../data/flat_field_response.csv) ·
[frame screening table](../data/flat_field_summary.csv) ·
[implementation companion](../implementation/flat-field.md)

## Purpose and conclusion

The measurement retains the four CFA positions independently, applies explicit
quality gates, normalizes each plane to a central block, and derives chromatic
ratios from the normalized maps.

The CLRS-589 archive yielded three usable f/8 frames from 52 sphere captures.
The primary green response reached 0.4801 at the lowest bin relative to its
center, while `C_RG` remained within −2.27 pp and `C_BG` within +4.47 pp of
unity. The brightest-to-darkest corner-block spread was 19.65% of their
average, well above the 5% project policy.

"Primary" here means the first frame of the documented pair run
(`Sphere_f8.0_1:1000_DSCF0368.RAF`) and nothing more. The label carries no
evidential weight: both 1/1000 s frames support the same qualitative result, and
the repeat carries marginally more signal (green center 0.4942 against 0.4876 of
ceiling). Both frames' statistics are published, including both `A` values. No
qualitative conclusion or policy verdict depends on which one is called
primary, although the reported numeric ranges change slightly.
The green-CFA field is consequently reported as a capture-system response; the
capture does not isolate optical vignetting regardless of the `A` verdict.

## Recorded capture configuration

An ExifTool audit of the CLRS-589 `Images` tree records one body/lens-sample,
focal-length, focus-mode, and ISO identity across all 165 RAF files, including
the 52 sphere and 21 dark frames this study uses. It does not record one optical
configuration: aperture varies, and focus distance is absent.

| Property | Recorded value |
|---|---|
| Camera body | Fujifilm X-T100 (APS-C) |
| Lens | Fujinon XF 14 mm f/2.8 R |
| Lens serial number | `56A00213` |
| Focal length | 14.0 mm (21 mm 35 mm-equivalent, 1.5x) |
| ISO | 200 |
| Focus mode / distance | Manual / not recorded |
| Aperture census | f/5.6: 18; f/8: 92; f/9: 54; f/10: 1 |

The lens is an ultra-wide on APS-C. That is the context required to read the
falloff magnitude below, where green response reaches roughly one-half of its
center value. It also fixes a provenance asymmetry with the
[SFR archive](SFR_MTF_ARCHIVE_INVENTORY.md#capture-metadata-audit), whose
captures record no lens serial: here the serial is present and identical in
every audited file, so lens *sample* identity is verified rather than inferred
from a matching model name.

Naming the lens does not attribute the measured field to it. The
integrating-sphere field is itself visibly nonuniform, and the archive contains
no source- or camera-rotation control, so the result remains a capture-system
characterization however well the optical state is documented. The value of the
record is narrower: within a matched-aperture comparison, recorded body, lens
sample, focal length, focus mode, and ISO do not change. Unrecorded focus
distance, alignment, illumination, and other capture state remain uncontrolled.

## Input and ceiling semantics

The RAW front end supplies the active Bayer mosaic as signed, black-subtracted
residuals. The spatial-response analysis never subtracts black again and derives
the signal ceiling from the same per-position metadata:

```text
ceiling[p]       = white_level - black_per_channel[p]
near_threshold  = near_ceiling_level * ceiling[p]
```

For these X-T100 captures, raw white is 16,383 DN, effective black is 1,024 DN
at all four positions, and the signal-referred ceiling is 15,359 DN. At the
default 0.98 level, samples at or above 15,051.82 DN count as near ceiling.
This distinction matters because the input buffer is already black-subtracted.

## Three geometries

The primary frame used these effective CFA-balanced rectangles:

| Region | Mosaic rectangle | Purpose |
|---|---|---|
| Center gate | x=2406, y=1606, 1202 × 802 px | near-ceiling verdict |
| Center block | x=2808, y=1806, 400 × 400 px | normalizer and low-signal anchor |
| TL / TR corners | x=120 / 5496, y=120, 400 × 400 px | corner response |
| BL / BR corners | x=120 / 5496, y=3494, 400 × 400 px | corner response |
| Map grid | 16 × 12 bins per CFA plane | spatial response |

Geometry that cannot fit is rejected before allocation. Origins and dimensions
are even so every region contains a balanced count of all four Bayer positions.
The gate, normalizer, and map bins are separate because they answer different
questions.

## Binning and normalization

Each mosaic position is treated as its own plane. Bins use the upper median for
even populations; the convention is deterministic, and the bins contain many
thousands of samples.

For plane `X` and its center-block median `X_c`:

```text
R_X(i) = median_X(i) / X_c
G(i)   = [R_G1(i) + R_G2(i)] / 2
C_RG(i)   = R_R(i) / G(i)
C_BG(i)   = R_B(i) / G(i)
C_G1G2(i) = R_G1(i) / R_G2(i)
```

The center-block median normalizes to one; this does not force every center
sample or center-overlapping grid bin to one. The chromatic maps are ratios of
independently normalized response fields, not raw R/G or B/G ratios.

`C_G1G2` is a spatial-consistency diagnostic. A constant G1/G2 gain difference
cancels during center normalization. A spatially varying mismatch does not.

## Quality gates

All thresholds below are project policies. A rejected frame retains the
diagnostics that caused the rejection rather than collapsing to a bare failure.

| Check | Default | Region | Failure behavior |
|---|---:|---|---|
| Near ceiling | >1% of samples at ≥98% ceiling | full plane and center gate, per CFA position | reject; omit derived maps |
| Screening finite coverage | <90% finite samples | full plane and center gate, per CFA position | reject an untrustworthy near-ceiling ratio |
| Center signal | median <5% ceiling | center block | reject denominator |
| Negative residual | >1% | full plane | reject pedestal/black anomaly |
| Bin coverage | <90% finite samples | each map bin | reject incomplete map |
| Aggregate finiteness | empty/non-finite derived region | gate/center/corners/bins | reject undefined result |

The f/8, 1/500 s capture shows why the central and whole-frame fractions are
both gated and reported. The worst green plane measured 11.6319% near ceiling in the
center gate and 0.4964% across the full frame. That bright gate surrounds the
separate 400 x 400 px normalization block, so the frame is rejected even though
a 1% whole-frame test would pass.

## Archive screening

All 52 sphere files were screened with the same 16 by 12 geometry and the
quality gates defined above. All eight per-position gate/frame finite-coverage
values are 1.0 in this integer-RAW archive rerun:

| Aperture | Frames | Accepted | Rejected |
|---|---:|---:|---:|
| f/5.6 | 18 | 0 | 18 |
| f/8 | 21 | 3 | 18 |
| f/9 | 13 | 0 | 13 |
| Total | 52 | 3 | 49 |

All 49 rejected frames failed the near-ceiling check. The accepted frames were
the two f/8, 1/1000 s captures and one f/8, 1/1600 s capture. Because the other
apertures have no usable frame, the archive provides no aperture comparison.

## Primary response

| Map | Minimum | Maximum | Interpretation |
|---|---:|---:|---|
| Green relative response | 0.480104 | 1.000534 | green-CFA intensity proxy |
| `C_RG` | 0.977316 | 0.999956 | red response up to 2.268 pp below normalized green |
| `C_BG` | 0.999718 | 1.044729 | blue response up to 4.473 pp above normalized green |
| `C_G1G2` | 0.998943 | 1.002342 | greens agree within 0.234 pp |

The green map has strong horizontal and vertical imbalance, and the imbalance
keeps the same orientation in every accepted frame. The minimum green bin is the
same bottom-left grid cell in all three, and the top-right bin is always the
brightest corner:

| Frame | TL | TR | BL | BR |
|---|---:|---:|---:|---:|
| f/8, 1/1000 s primary | 0.5118 | 0.6276 | 0.4801 | 0.5174 |
| f/8, 1/1000 s repeat | 0.5117 | 0.6331 | 0.4816 | 0.5165 |
| f/8, 1/1600 s | 0.5141 | 0.6022 | 0.4859 | 0.5309 |

These are corner cells of the 16 × 12 map, not the 400 × 400 px corner blocks
that feed `A`; the two geometries differ by design and the values are not
interchangeable. A gradient that keeps its direction across three frames is
fixed in the capture geometry rather than frame-specific, which still does not
separate sphere, lens, alignment, or sensor angular terms.

Its four-corner asymmetry statistic is:

```text
A = (max corner G - min corner G) / mean corner G = 0.196484
```

`A` exceeds the declared 0.05 project policy and is inconsistent with a
centered radial scalar model for the measured composite. It neither identifies
the source of the asymmetry nor decides whether a lens contribution is present.

The threshold is a declared project policy, not an industry standard and not a
value derived from this pair. The two frames measure `A` at 0.196484 and
0.199964, an observed difference of 0.00348. That supports stability of the
high-`A` observation for this pair only; it does not establish a null
distribution, calibrate the 0.05 policy, or estimate repeatability.

The third accepted frame is outside that pair and does not reproduce its value.
The f/8, 1/1600 s capture measures `A` = 0.160875, which is 0.0356 below the
primary and 0.0391 below the repeat — roughly ten times the 0.00348 within-pair
difference, and driven mainly by its lower top-right corner in the table above.
All three exceed the
0.05 policy, so the verdict does not change, but `A` is not constant across
exposure here and the within-pair difference must not be quoted as a general
repeatability figure. All three values are in the
[frame screening table](../data/flat_field_summary.csv).

## Bounded dark-control and capture-pair checks

The f/8, 1/1000 s dark control was compatible in dimensions, CFA layout and
camera make/model; aperture/shutter/ISO metadata was present and matched. Its
full-frame per-plane median was `[0, 0, 0, 0]` DN, every sample was finite, and
the center plus four corner blocks were also checked against the declared 1 DN
tolerance. This verifies the stated dark-control checks, not all possible
spatial pedestal structure. The dark is never applied to the sphere samples.
The metadata audit treats make/model compatibility and physical-body identity
as separate questions; a one-sided or unequal body serial blocks the latter
check. No body serial was available in these RAFs, so physical-body identity
remains unverified even though the bounded dark-control checks pass.

For the two f/8, 1/1000 s sphere frames, the absolute corner-response
difference over four corners × four CFA positions measured:

| Statistic | Result |
|---|---:|
| Maximum | 0.378748 percentage points |
| RMS | 0.181309 percentage points |
| Primary `A` | 0.196484 |
| Repeat `A` | 0.199964 |

This pair bounds short-term repeat behavior for these captures. It is not a
population estimate or a substitute for a larger repeatability study.

## Transfer to the flat-field-corrected CCM path

The repeat frame of this pair, `Sphere_f8.0_1:1000_DSCF0387.RAF`, is also the
flat that corrects the ColorChecker-SG patches in the [CCM fit](CCM_FIT.md) and
[patch extraction](PATCH_EXTRACTION.md) reports. The numbers above therefore
characterize the exact frame that path divides by. Two of them bound the
corrected color result:

- green falls to 0.4816 of center, so the correction gain applied to a patch
  depends strongly on where the patch sits. The chart occupies a subregion of
  the frame, so the gain range actually applied is narrower than the full-field
  range and is not measured here.
- the chromatic maps are not flat. `C_BG` reaches 1.0447 and `C_RG` falls to
  0.9773, so a per-plane division also imposes an inverse chromatic gradient
  across the chart area.

The correction is a full per-position image-domain division rather than a radial
model, so it removes the measured gradient instead of approximating it. What it
cannot separate is the part of the flat that belongs to the sphere rather than
the camera: correcting with this frame divides out source nonuniformity too.
That is correct for flattening a chart capture and wrong for any camera-only
shading claim, which is why the CCM evidence is labeled same-aperture-corrected
rather than shading-calibrated.

### The shared gate protects correction inputs

The patch-extraction correction path applies the same source-CFA,
per-position admission test as the flat-field measurement, including both
full-frame and centered-region measurements. The
[f/8, 1/500 s frame](#quality-gates) shows why each dimension is needed. Its worst CFA position
measured 11.6319% near ceiling in the gate against 0.4964% frame-wide, while the
full-frame result alone remains below the 1% limit.

Testing before demosaic avoids averaging high samples with lower neighbors,
and retaining all four CFA positions prevents one green position from being
hidden by a pooled color fraction. The center of this field-falloff flat is also
the brightest region, so a frame-wide denominator alone is insensitive to the
local headroom loss. The correction normalizer itself is not this center region:
it uses the full-frame valid-sample mean of each demosaiced channel.

The correction input is evaluated on the source mosaic before demosaic with
the same declared 20% geometry, 98% level, 1%
near-ceiling policy, 90% screening-coverage policy, and per-position decision
rule as the flat-field analysis. Screening coverage is a fixed policy over the
full-plane and center-gate populations; it is distinct from the 90%
per-map-bin coverage gate used to accept a spatial map. The `1:500` result is
therefore identical in both analyses:

| Region | R | G1 | G2 | B | Policy |
|---|---:|---:|---:|---:|---:|
| Whole frame | 0% | 0.3664% | 0.4964% | 0% | 1% |
| Centered gate (`x=2406, y=1606, w=1202, h=802`) | 0% | 8.6908% | 11.6319% | 0% | 1% |

G2 rejects. Accepted and rejected correction inputs retain the two
near-ceiling arrays, the two finite-coverage arrays, the effective rectangles,
and the per-position verdict, so a rejection is reported together with the
measurements that caused it rather than as a bare failure. The shared screening
rule makes the
[52-frame screening table](../data/flat_field_summary.csv), with all eight
per-position frame/gate near-ceiling fractions, the shared gate ledger for both
consumers rather than two independently implemented tests that happen to agree.
Its remaining columns describe map-specific coverage, dark-control, and
asymmetry analysis.

The selected `Sphere_f8.0_1:1000_DSCF0387.RAF` flat is accepted because it
measures 0% near ceiling at every CFA position in both regions. Its full-frame
valid-sample mean supplies the correction normalization. The available
aggregate evidence does not support a quantitative correction comparison with
the rejected 1/500 s flat, so no correction-error magnitude is claimed.

## Limitations and extensions

The integrating-sphere field is visibly nonuniform and was not independently
mapped with a radiometer. Only one aperture has usable frames, and there is no
camera/source rotation pair. The current results cannot separate illumination,
lens, alignment, mechanical shading, or sensor/microlens angular response.

Separating those terms would require multiple unsaturated apertures, source and
camera rotations, an independently characterized source, and more repeated
frames. A correction workflow would additionally require computing a bounded
gain surface, applying it to an independent capture, and remeasuring the field.

## Engineering companion

The [flat-field implementation companion](../implementation/flat-field.md)
explains how the measurement is realized in C++ and routes readers to the
public source and tests.
