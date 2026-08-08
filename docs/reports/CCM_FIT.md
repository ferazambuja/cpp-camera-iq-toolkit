# CCM fitting: method and validation

A camera's RAW RGB values depend on its own spectral sensitivities; they are not
standard color coordinates. This report follows a 140-patch ColorChecker-SG
capture through field correction, white balance, and a linear 3×3 RGB-to-XYZ
fit, then measures color difference on patches withheld from that fit. The
principal held-out result is **4.134 mean CIEDE2000**, a perceptual
color-difference metric in which lower is better, against a compatible spectral
chart reference. It is not an exact per-unit measurement of the photographed
chart.

Analysis date: 2026-07-04
Dataset: `clrs589_project_camera`

[Case study](../case-studies/colorchecker-ccm.md) ·
[aggregate results CSV](../data/ccm_validation_summary.csv) ·
[implementation companion](../implementation/color-characterization.md)

## Scope

The principal result retains dark-patch and reference-provenance diagnostics so
that a lower aggregate cannot hide where the fit fails. The study also keeps the
compatible-reference limitation attached to every color-difference result.

The reference XYZ values are rendered from a compatible 2019 ColorChecker-SG
spectral workbook under an explicitly measured sphere spectrum. The principal
camera RGB table comes from the corrected RAW extraction documented in the
[patch report](PATCH_EXTRACTION.md); a retained 140-row MATLAB table remains a
historical comparison only.

The illuminant is supplied explicitly from the private sphere-measurement
sidecars; it is not inferred from EXIF or camera dates.

The reference and capture are cross-timeline by design: the colored SG
reflectance comes from the compatible 2019 CLRS-601 workbook, while the Fuji
CLRS-589 capture is a separate 2020 project. The workbook is therefore a
compatible spectral reference, not a measurement of the physical chart unit
in the capture. This study evaluates the fitting method under that bounded
reference relationship; it does not present the result as per-unit calibration.

## Method

The measured chart RGB and rendered reference XYZ are paired in the same
140-patch physical order. For each patch `i`, the linear matrix `M` is fitted by
least squares:

```text
M = arg min sum_i ||M RGB_i - XYZ_i||²
```

Reference XYZ is the wavelength integral of chart reflectance, measured
illuminant, and the CIE 1931 2° color-matching functions, normalized so a
perfect diffuser has `Y = 100`. Camera predictions and references are converted
to CIELAB under that same white before DeltaE76 and CIEDE2000 are calculated.

Five-fold evaluation assigns every fifth chart row to the same held-out fold,
fits on the other four folds, and scores only the omitted patches. This is not
an independent second capture, but it prevents training error from being
presented as generalization. A separate `L* < 25` summary exposes the dark-axis
error, and an explicit lightness-based exclusion experiment reports both the
kept and excluded patches rather than silently deleting difficult data.

## Real-Data Result

Pairing gate:

| Metric | Value | Gate |
|---|---:|---:|
| luminance correlation | 0.9775 | >= 0.90 |
| red-green proxy correlation | 0.9498 | >= 0.80 |
| blue-green proxy correlation | 0.9617 | >= 0.90 |

The FF2 fitted reference white is `XYZ = (94.5250, 100.0000, 83.5036)`.

FF2 RGB to XYZ matrix:

```text
[ 496.093260,  231.290621,   10.439111 ]
[ 136.862458,  551.340524, -135.007767 ]
[ -15.952276,  -80.834525,  890.432263 ]
```

FF2 training summary, 140 patches:

| Metric | Mean | RMS | Max |
|---|---:|---:|---:|
| DeltaE76 | 7.028 | 9.643 | 39.312 |
| CIEDE2000 | 4.374 | 6.156 | 29.797 |

FF2 deterministic 5-fold held-out summary, 140 patches:

| Metric | Mean | RMS | Max |
|---|---:|---:|---:|
| DeltaE76 | 7.078 | 9.713 | 39.444 |
| CIEDE2000 | 4.409 | 6.194 | 29.919 |

FF2 dark-patch diagnostics (`L* < 25`):

| Count | Worst patch | Mean DeltaE76 | Mean CIEDE2000 |
|---:|---|---:|---:|
| 28 | `A5` | 11.100 | 7.549 |

The three measured sphere SPDs give stable DeltaE76 sensitivity results:

| Illuminant file | White Z | Mean DeltaE76 | RMS DeltaE76 | Max DeltaE76 |
|---|---:|---:|---:|---:|
| sphere SPD 1 | 84.180 | 7.044 | 9.661 | 39.317 |
| sphere SPD 2 | 83.504 | 7.028 | 9.643 | 39.312 |
| sphere SPD 3 | 83.358 | 7.025 | 9.640 | 39.311 |

Sphere SPD 1 contains negative spectrometer noise beyond the SG reference
axis, around 991 nm. The reader ignores that unused tail and still rejects any
negative interpolated value on the actual 380-730 nm target axis.

## Corrected RAW Patch Input Validation

Pairing gate:

| Metric | Value | Gate |
|---|---:|---:|
| luminance correlation | 0.9828 | >= 0.90 |
| red-green proxy correlation | 0.9603 | >= 0.80 |
| blue-green proxy correlation | 0.9611 | >= 0.90 |

Corrected RAW-patch training summary, 140 patches:

| Metric | Mean | RMS | Max |
|---|---:|---:|---:|
| DeltaE76 | 6.501 | 9.457 | 39.911 |
| CIEDE2000 | 4.099 | 6.199 | 30.350 |

Corrected RAW-patch deterministic 5-fold held-out summary, 140 patches:

| Metric | Mean | RMS | Max |
|---|---:|---:|---:|
| DeltaE76 | 6.579 | 9.533 | 39.936 |
| CIEDE2000 | 4.134 | 6.230 | 30.373 |

Corrected RAW-patch dark-patch diagnostics (`L* < 25`):

| Count | Worst patch | Mean DeltaE76 | Mean CIEDE2000 |
|---:|---|---:|---:|
| 28 | `A5` | 11.484 | 7.890 |

`A5` here is the **reference workbook patch ID**. RawDigger's displayed
`Sample_Name` grid is transposed relative to the workbook labels; the current
row order is nevertheless the correct physical sweep (RawDigger-vs-MATLAB green
corr **0.99984**, current orientation vs 560-nm proxy **0.958**, literal
label-match corr only **0.407**).

Corrected RAW-patch result when reference patches below `L* = 25` are excluded
from the fit and kept-set evaluation:

| Evaluation | Patches | Mean DeltaE76 | Mean CIEDE2000 | Held-out Mean DeltaE76 | Held-out Mean CIEDE2000 |
|---|---:|---:|---:|---:|---:|
| all-patch baseline fit | 140 | 6.501 | 4.099 | 6.579 | 4.134 |
| kept-set fit/eval (`L* >= 25`) | 112 | 5.283 | 3.170 | 5.427 | 3.221 |
| all patches evaluated with kept-set fit | 140 | 6.544 | 4.126 | — | — |
| excluded patches with kept-set fit | 28 | 11.589 | 7.952 | — | — |

The kept-set ΔE drop is material, but it is not a better camera model claim.
It is a labeled flare-handling policy for patches where the camera capture and
the contact/spectro reference are measuring different physical light. The
excluded patches remain reported separately and are not used to claim final
chart accuracy.

The near-identical DeltaE with versus without the flat-derived white-balance
gains is expected
for a free 3x3 CCM: per-channel white-balance gains are absorbed by the fitted
matrix. The RAW patch extraction path retains explicit flat-field and
white-balance provenance while using the same CCM fitter as the historical
MATLAB baseline.

The corrected RAW-patch validation is intentionally scoped to the f/8 CCSG capture.
The private f/9 sphere set has 13 frames from `1:10` through `1:180`, and every
frame is rejected by the flat-field near-ceiling guard. There is no usable
same-aperture f/9 sphere flat in the private dataset. Applying the f/8 flat to f/9
CCSG frames would be a labeled cross-aperture approximation, not the evidence
used for this result.

### What the correction frame itself measures

`sphere_f8.0_1-1000_02` is not only an input here. It is also the
repeat frame of the pair characterized in the
[flat-field response report](FLAT_FIELD_RESPONSE.md#transfer-to-the-flat-field-corrected-ccm-path),
so its own field is measured rather than assumed:

| Measured property of the correction frame | Value |
|---|---:|
| Green relative response, minimum bin | 0.4816 |
| `C_RG` range | 0.9773 – 1.0000 |
| `C_BG` range | 0.9997 – 1.0447 |
| Green corner-block spread `A` | 0.199964 |
| Near-ceiling fraction, frame and centered gate | 0 |

The dividing flat therefore carries both a strong intensity gradient and a
smaller chromatic one, so the correction imposes an inverse chromatic gradient
across the chart area in addition to flattening intensity. Because the flat is
divided per position rather than fitted to a radial model, the measured
asymmetry is removed rather than approximated — but the correction cannot
separate sphere nonuniformity from camera response, so this path is
same-aperture-corrected, not shading-calibrated.

The near-ceiling guard measures the source CFA with the same centered,
per-position limits as `shading`. The selected 1/1000 s flat passes both limits,
and its full-frame valid-sample mean supplies the correction normalization; a
flat with a near-ceiling central region is rejected. The archive-backed comparison
produced byte-identical 140-row corrected RGB tables (0 DN difference across
all 420 channel values); the supporting output is included with the
[patch-extraction evidence](PATCH_EXTRACTION.md#corrected-raw-patch-result),
which also records the rejected measured case.

## Scientific Boundaries

- The result is labeled **vs compatible SG spectral reference**, not exact
  measured-reference DeltaE for the physical CLRS-589 chart.
- This reports DeltaE76 and CIEDE2000 side by side. CIEDE2000 changes the
  perceptual interpretation but does not prove the camera model improved.
- This is a linear 3x3 CCM only. Root-polynomial and other higher-order models
  are intentionally deferred until they show held-out improvement; a
  training-only DeltaE reduction is not acceptable evidence here.
- Held-out metrics are deterministic row-index k-fold diagnostics, not an
  independent validation on a second physical chart capture.
- Dark-patch diagnostics show the current worst errors are concentrated in
  `L* < 25` reference patches. RawDigger and MATLAB agree on those lifted dark
  patch signals, so the likely issue is veiling glare / scene-capture physics
  relative to the flare-free reference, not a RawDigger import bug.
- Dark-patch exclusion is explicit and reference-lightness based. It is a
  reporting/fit policy, not silent data deletion, and not proof that the
  compatible 2019 reference is the exact 2020 physical chart.
- The fit consumes a corrected RAW-derived patch table, but RawDigger
  coordinates remain an external dependency.
- The calculation uses the supplied illuminant SPD and cannot verify illumination
  stability during the chart capture.
- Corrected RAW-patch CCM evidence covers the f/8 CCSG series only;
  f/9 lacks a usable same-aperture flat in the local dataset cache.

## Engineering companion

The [color-characterization implementation companion](../implementation/color-characterization.md)
explains how the scientific method maps to C++ and routes readers to the public
source and tests. The report above remains canonical for the capture/reference
conditions and color-difference result.
