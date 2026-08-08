# RAW Patch Extraction

A color-correction matrix is only as trustworthy as the RGB values extracted
from its chart. This report checks the full path from RAW samples to 140
ColorChecker-SG patch means, including black subtraction, field correction,
white balance, chart orientation, and patch placement. A corner-generated grid
retained high RGB correlation but missed checked patch centers by as much as
16.449 px, so the reported CCM continues to use the retained RawDigger
rectangles rather than promoting the inaccurate shortcut.

Analysis date: 2026-07-04
Dataset: `clrs589_project_camera`

[Case study](../case-studies/colorchecker-ccm.md) ·
[implementation companion](../implementation/color-characterization.md)

## Method

The RAW capture is opened in its active Bayer area, black-subtracted, and
demosaiced with a transparent bilinear baseline. Patch rectangles then sample
mean linear-DN RGB in the chart's physical 14 × 10 order. The reported result
uses retained RawDigger rectangles because they belong to the same RAW
coordinate domain. A second path generates rectangles from four chart corners
with a projective transform; it is evaluated as a localization experiment, not
silently substituted into the color result.

When a flat field is applied, each demosaiced channel is corrected by:

```text
corrected_channel(x,y)
  = target_channel(x,y) × mean_valid_flat_channel / flat_channel(x,y)
```

The flat is screened on the source Bayer mosaic before demosaic. Every CFA
position must retain at least 90% finite coverage and keep the fraction of
samples above 98% of its signal range below 1%, both over the complete frame and
over a centered 20% gate. This prevents a bright central plateau from being
diluted by darker surroundings. A valid flat's full-frame channel means set the
normalization; optional white balance then scales red and blue to the flat's
green mean.

Extraction is checked in two independent dimensions. RGB agreement against the
retained RawDigger means tests sample values, while patch-center distance tests
geometry. Correlation alone is not accepted because a shifted grid can preserve
the chart's overall tonal order while sampling the wrong pixels.

## Uncorrected extraction cross-check

Result against RawDigger's own exported patch averages:

| Channel | Correlation | Mean direct error | Direct RMSE | Max direct abs error | Affine slope | RMSE after affine |
|---|---:|---:|---:|---:|---:|---:|
| R | 0.999999982 | +0.015 DN | 0.352 DN | 1.224 DN | 0.999982371 | 0.350 DN |
| G | 1.000000000 | -0.003 DN | 0.041 DN | 0.259 DN | 1.000001748 | 0.040 DN |
| B | 0.999999980 | -0.028 DN | 0.381 DN | 1.415 DN | 1.000013457 | 0.379 DN |

The first patch (`A1`) measured
`RGB = (4139.594, 7602.262, 4651.185) DN`.

RawDigger reports `4139.45, 7602.30, 4651.25`. The direct (pre-affine) RMSE is
within 1% of the after-affine RMSE and the signed channel bias is below `0.03`
DN, so the agreement is absolute-DN agreement, not a scale/offset artifact.

## Corner-Seeded Orientation Gate Validation

The orientation gate was evaluated on the f/8 CCSG RAW using a
RawDigger-derived four-corner seed. The seed was computed from the A1, A14, J14,
and J1 patch centers and then projected back to the SG outer chart corners, so
this is **not** an independent localization result; it tests the physical order
against direct, row-flip, column-flip, and 180-degree alternatives.

Result:

| Orientation | Aggregate min corr | Luminance corr | R-G proxy corr | B-G proxy corr | Passes thresholds |
|---|---:|---:|---:|---:|---|
| direct | 0.960203 | 0.982774 | 0.960203 | 0.960970 | true |
| column flip | 0.052004 | 0.376739 | 0.248921 | 0.052004 | false |
| row flip | 0.080909 | 0.491728 | 0.140243 | 0.080909 | false |
| 180 rotation | -0.280493 | 0.396087 | -0.211692 | -0.280493 | false |

The direct physical orientation is the only candidate that passes the declared
correlation gate.

## RawDigger oracle localization result

The f/8 `1:10` corner-seeded model **does not pass** the declared geometry
criterion. With corners
derived from RawDigger A1/A14/J14/J1 centers, it fails because:

- patch count: 140, pass
- max center error: **16.449 px**, fail against the 5 px gate
- per-channel correlations: all >= 0.999, pass
- max absolute per-patch RGB error: R 12.169 DN, G 20.482 DN, B 11.554 DN, pass
- orientation: direct, pass

Those three figures are the worst single patch in each channel, not a mean. The
gate they are compared against is a per-patch maximum, so `G 20.482 DN` against
the `25 DN` limit describes one outlying patch and not the typical agreement —
the signed channel bias over all 140 patches stays below `0.03 DN`.

This confirms the orientation and mean extraction are close, but the generated
projective grid fails the 5 px center-error gate and is not used as a
replacement for RawDigger rectangles. See
`docs/reports/RAW_CHART_LOCALIZATION.md`. That report records
the per-patch residuals: the four corner patches are pinned near zero, while
the middle columns bow by about 15 px relative to RawDigger. The
miss is therefore a systematic geometry/model mismatch, not a coordinate-origin
artifact or simple global offset.

## Important Negative Finding

The retained rendered-pipeline coordinate table is valid for the historical
MATLAB TIFF workflow,
but it is **not** a RAW-space coordinate source for LibRaw patch extraction. On
the same RAW series, using the rendered-pipeline coordinate table against the
RAW image gives only about `0.30 / 0.31 / 0.36` correlation against the
historical rendered camera table. This is not a color failure; it is a
coordinate-domain mismatch.

Use RawDigger coordinates for RAW-space validation. Treat the historical camera
table as a rendered/TIFF pipeline target until the C++ tool has an explicit
TIFF/flat-field parity path or an automatic chart-localization step.

RawDigger's `Sample_Name` labels are coordinate-grid labels, not the same label
axis as the compatible SG workbook. For the f/8 `1:10` capture, RawDigger row
order starts `A1,A2,...A14,B1...`; the reference workbook order starts
`A1,B1,...N1,A2...`. The current row-order pairing is still the correct
physical sweep: RawDigger green vs MATLAB green corr is **0.99984**, and
RawDigger green vs the reference 560-nm proxy is **0.958** in the current
orientation versus **0.327 / 0.433 / 0.353** for reference-grid column flip,
reference-grid row flip, and 180-degree rotation. Literal RawDigger-label
matching is wrong for this
chart pairing (shared-label corr only **0.407**). Downstream reports therefore
name excluded patches by **reference patch ID**, not RawDigger grid label.
Standalone `spectral-diversity-toolkit` columns named `patch_row` and
`patch_col` are parsed from reference label text and are not authoritative
physical SG geometry.

## Corrected RAW patch result

Result:

| Field | Value |
|---|---:|
| patch rows | 140 |
| flat normalization | per-channel mean of valid samples |
| flat normalizer R/G/B | 3240.165 / 5979.162 / 3199.320 DN |
| flat clamped samples | 0 |
| flat near-ceiling fractions, R/G1/G2/B, full frame | 0 / 0 / 0 / 0 |
| flat near-ceiling fractions, R/G1/G2/B, centered 20% gate | 0 / 0 / 0 / 0 |
| flat-derived WB gains R/G/B | 1.845327 / 1.000000 / 1.868886 |
| first patch A1 corrected RGB | 7677.11 / 7639.68 / 8712.55 |

`comparison` is intentionally `null` in this corrected mode unless an explicit
corrected reference RGB table is supplied. RawDigger's `Ravg/Gavg/Bavg` values
are uncorrected RAW rectangle means, so they are valid as a geometry/extraction
oracle only for the uncorrected mode above.

The same-aperture f/8 sphere frames from `1:10` through `1:500` are too near the
signal ceiling for meaningful flat-field correction. The validation run
uses `sphere_f8.0_1-1000_02`, whose CFA means are well below the
ceiling and preserve spatial variation. The shorter `1:10` flat is rejected
because it is too close to the sensor ceiling for correction.

The guard measures two regions, not one. A whole-frame near-ceiling fraction
cannot protect a flat whose brightest region is central: the darker surround
keeps the frame-wide fraction small. `sphere_f8.0_1-500_01` is the
measured case —
[11.6319% of its center gate near ceiling against 0.4964% frame-wide](FLAT_FIELD_RESPONSE.md#the-shared-gate-protects-correction-inputs) —
and the old pooled post-demosaic whole-frame implementation accepted it.

The screening is performed on the source mosaic before demosaic, using the same
effective rectangle and per-position decision in both the patch and flat-field
analyses. On the `1:500` frame both report:

| Region | R | G1 | G2 | B | Policy |
|---|---:|---:|---:|---:|---:|
| Whole frame | 0% | 0.3664% | 0.4964% | 0% | 1% |
| Centered gate (`x=2406, y=1606, w=1202, h=802`) | 0% | 8.6908% | 11.6319% | 0% | 1% |

The second green position exceeds the 1% policy and rejects the frame. Keeping
the decision per CFA position matters: pooling the two greens would dilute the
worst local failure.

The 1/1000 s flat measures 0% near ceiling in every CFA position in both
regions and passes the screening criteria. Its full-frame valid-sample mean
supplies the correction normalization. The accepted 140-patch output is
published as
[`ccsg_f8_flat_wb_patches.csv`](../data/ccsg_f8_flat_wb_patches.csv).

This verifies the accepted correction output, not a correction result for the
rejected 1/500 s flat.

Remeasurement requires the private RAW files; the public table preserves the
140 accepted patch values and reported A1 value of
`7677.11 / 7639.68 / 8712.55`.

Same-aperture flat coverage is not available for the f/9 CCSG series in the
private dataset. The f/9 sphere folder contains 13 frames (`1:10` through `1:180`);
all 13 are rejected by the near-ceiling guard, including the shortest
exposure, `sphere_f9.0_1-180_01`. The f/8 group has four
same-aperture candidates: `1:500`, two `1:1000` frames, and `1:1600`. The
shared gate rejects `1:500`, leaving the other three usable. The current
flat-fielded RAW patch extraction is therefore scoped to the f/8 series; using
an f/8 flat on the f/9 CCSG series would be a
cross-aperture approximation, not a measured same-aperture correction.

## Scientific Boundaries

- The extraction uses bilinear demosaic only.
- Flat-field correction is multiplicative image-domain correction, not a full
  ISP shading model.
- Flat-field validity assumes a suitable flat for the target optical state; the
  local CLRS-589 cache has usable same-aperture sphere flats for f/8 CCSG only.
- White balance is explicit: either caller-provided gains or the documented
  flat-field green-anchor policy.
- The retained RawDigger rectangles validate RAW-space extraction against
  RawDigger patch means only when no correction is applied; corrected patch
  tables need a corrected reference or downstream CCM/DeltaE evaluation.
- MATLAB-style rectangle files remain usable only when the coordinates belong
  to the same image domain as the RAW being read.
- Four-corner projective geometry removes the 140-rectangle dependency but
  still depends on caller-supplied chart corners; there is no blind chart
  detection yet. The orientation gate confirms the direct physical sweep beats flip
  controls, but the first RawDigger-oracle run fails the predeclared 5 px
  center gate, so the corner-seeded path is not used in the reported analysis.

## Measurement limitations

- The historical TIFF workflow and RAW-space extraction use different
  coordinate domains; this result uses RawDigger rectangles in RAW space.
- The corner-seeded projective grid misses the RawDigger oracle centers by up
  to 16.449 px. The interior-column bow constrains the failure pattern but does
  not isolate lens distortion, chart geometry, or non-projective placement.
- Dark-patch and neutral-axis errors remain unresolved. Higher-order color
  models would require held-out evidence before they could be treated as an
  improvement.

## Engineering companion

The [color-characterization implementation companion](../implementation/color-characterization.md)
explains how the measurement is realized in C++ and routes readers to the
public source and tests. The concise scientific narrative is the
[ColorChecker/CCM case study](../case-studies/colorchecker-ccm.md).
