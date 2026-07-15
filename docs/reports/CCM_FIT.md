# Evidence CCM Fit

Date: 2026-07-04
Dataset: `clrs589_project_camera`
Command: `camera_iq ccm-fit`

## Scope

This slice renders the configured ColorChecker-SG spectral reflectance reference
under an explicit illuminant SPD, then fits a first linear 3x3 camera-RGB to XYZ
color-correction matrix. It is a demonstration of the color pipeline mechanics,
not an exact per-unit chart characterization.

The reference is the local 2019 compatible SG workbook export:

```text
data/private/references/ccsg_2019_workbook/ccsg_2_FIXED_ref.csv
```

The camera RGB input is the existing 140-row MATLAB patch table:

```text
data/private/datasets/clrs589_project_camera/Images/ccsg_matlab.csv
```

`camera_iq patches` can now emit a corrected RAW-derived 140-row RGB table via
`--rgb-csv-out`; the MATLAB table remains the historical baseline input.

The illuminant is supplied explicitly from the local copied sphere measurements;
it is not inferred from EXIF or camera dates.

The reference and capture are cross-timeline by design: the colored SG
reflectance comes from the compatible 2019 CLRS-601 workbook, while the Fuji
CLRS-589 capture is a separate 2020 project. The JSON emits this as
`timeline_provenance` and keeps `reference_scope` as
`compatible_sg_spectral_not_exact_per_unit`.

## Implemented

- `read_spectrum_csv_interpolated()` reads two-column illuminant spectra, skips
  text headers, interpolates to the reference wavelength axis, and rejects
  negative values on the target axis. Negative instrument noise outside the
  target axis does not poison the fit.
- `render_reference_xyz()` integrates reflectance x illuminant x CIE 1931 2
  degree CMFs, normalizing the perfect diffuser white to `Y=100`.
- `fit_rgb_to_xyz_ccm()` solves a least-squares 3x3 RGB to XYZ matrix and
  reports mean, RMS, and max DeltaE76 and CIEDE2000 against the rendered
  reference. The CIEDE2000 implementation is locked by Sharma/Wu/Dalal
  reference pairs.
- `cross_validate_rgb_to_xyz_ccm()` reports deterministic row-index k-fold
  held-out DeltaE76 and CIEDE2000 for the same linear model. This is not an
  independent physical chart capture, but it prevents training-only model
  comparisons from being presented as color improvement.
- `diagnose_dark_patches()` reports the subset of target reference patches with
  `L* < 25`, including the worst patch id. This makes the current dark-axis
  error visible instead of hiding it inside the overall mean.
- `--exclude-ref-lightness-below LSTAR` is an explicit opt-in fit/evaluation
  policy for flare-suspect dark SG patches. The command still reports the
  all-patch baseline, kept-set metrics, all-patches-with-kept-fit metrics, and
  excluded-patch metrics, plus baseline and kept-set held-out summaries, so
  excluded data remains visible.
- `camera_iq ccm-fit` reuses the configured reference validation and
  camera/reference pairing gate before fitting.
- `camera_iq patches --flat-field-raw ... --wb-from-flat-field
  --rgb-csv-out ...` now provides a corrected RAW-derived camera RGB input path.

## Real-Data Result

Command used:

```bash
./build/camera_iq ccm-fit clrs589_project_camera \
  --illuminant-spd "data/private/datasets/clrs589_project_camera/Sphere measurments/sphere_ff2.csv" \
  --out /tmp/clrs589_ccm_fit_ff2.json
```

Pairing gate:

| Metric | Value | Gate |
|---|---:|---:|
| luminance correlation | 0.9775 | >= 0.90 |
| red-green proxy correlation | 0.9498 | >= 0.80 |
| blue-green proxy correlation | 0.9617 | >= 0.90 |

FF2 fitted white:

```json
{"x":94.52503424535965,"y":100,"z":83.50355241583816}
```

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

The three copied sphere SPDs give stable first-slice DeltaE76 results:

| Illuminant file | White Z | Mean DeltaE76 | RMS DeltaE76 | Max DeltaE76 |
|---|---:|---:|---:|---:|
| `sphere_ff1.csv` | 84.180 | 7.044 | 9.661 | 39.317 |
| `sphere_ff2.csv` | 83.504 | 7.028 | 9.643 | 39.312 |
| `sphere_ff3.csv` | 83.358 | 7.025 | 9.640 | 39.311 |

`sphere_ff1.csv` contains negative spectrometer noise beyond the SG reference
axis, around 991 nm. The reader ignores that unused tail and still rejects any
negative interpolated value on the actual 380-730 nm target axis.

## Corrected RAW Patch Input Validation

Patch table command:

```bash
./build/camera_iq patches \
  "Images/CCSG_f8/CCSG_f8.0_1:10_DSCF0402.RAF" \
  --dataset clrs589_project_camera \
  --rawdigger-csv Images/CCSG_rawdigger.csv \
  --flat-field-raw "Images/Sphere/Sphere_f8.0_1:1000_DSCF0387.RAF" \
  --wb-from-flat-field \
  --rgb-csv-out /tmp/clrs589_raw_flat_wb_patches.csv \
  --out /tmp/clrs589_raw_flat_wb_patches.json
```

CCM command:

```bash
./build/camera_iq ccm-fit clrs589_project_camera \
  --illuminant-spd "data/private/datasets/clrs589_project_camera/Sphere measurments/sphere_ff2.csv" \
  --camera-rgb /tmp/clrs589_raw_flat_wb_patches.csv \
  --out /tmp/clrs589_raw_flat_wb_ccm.json
```

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

Opt-in dark-patch exclusion command:

```bash
./build/camera_iq ccm-fit clrs589_project_camera \
  --illuminant-spd "data/private/datasets/clrs589_project_camera/Sphere measurments/sphere_ff2.csv" \
  --camera-rgb /tmp/clrs589_raw_flat_wb_patches.csv \
  --exclude-ref-lightness-below 25 \
  --out /tmp/clrs589_raw_flat_wb_ccm_exclude_l25.json
```

Corrected RAW-patch result with `--exclude-ref-lightness-below 25`:

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

The near-identical DeltaE under `--wb-from-flat-field` versus no WB is expected
for a free 3x3 CCM: per-channel white-balance gains are absorbed by the fitted
matrix. The value of this slice is not the final DeltaE number; it is that the
RAW patch extraction path now has explicit flat-field/WB provenance and can feed
the same CCM fitter as the historical MATLAB table.

The corrected RAW-patch validation is intentionally scoped to the f/8 CCSG capture.
The local f/9 sphere set has 13 frames from `1:10` through `1:180`, and every
frame is rejected by the flat-field near-ceiling guard. There is no usable
same-aperture f/9 sphere flat in the local cache. Applying the f/8 flat to f/9
CCSG frames would be a labeled cross-aperture approximation, not the evidence
used for this result.

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
- The command consumes a patch RGB table. `camera_iq patches` can now produce a
  corrected RAW-derived table, but RawDigger coordinates are still an external
  dependency.
- The command uses the supplied illuminant SPD and cannot verify illumination
  stability during the chart capture.
- Corrected RAW-patch CCM evidence currently covers the f/8 CCSG series only;
  f/9 lacks a usable same-aperture flat in the local dataset cache.

## Next Risks

1. Replace RawDigger-coordinate dependency with automatic RAW-space chart
   localization.
2. Diagnose the dark-patch axis against the measured neutral ramp and RAW
   black/flat-field floor before claiming a model-side color improvement.
3. Add root-polynomial CCM variants only behind held-out/CV evidence that they
   improve rather than overfit the compatible reference.
