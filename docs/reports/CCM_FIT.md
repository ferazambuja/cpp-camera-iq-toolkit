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

The illuminant is supplied explicitly from the local copied sphere measurements;
it is not inferred from EXIF or camera dates.

## Implemented

- `read_spectrum_csv_interpolated()` reads two-column illuminant spectra, skips
  text headers, interpolates to the reference wavelength axis, and rejects
  negative values on the target axis. Negative instrument noise outside the
  target axis does not poison the fit.
- `render_reference_xyz()` integrates reflectance x illuminant x CIE 1931 2
  degree CMFs, normalizing the perfect diffuser white to `Y=100`.
- `fit_rgb_to_xyz_ccm()` solves a least-squares 3x3 RGB to XYZ matrix and
  reports mean, RMS, and max DeltaE76 against the rendered reference.
- `camera_iq ccm-fit` reuses the configured reference validation and
  camera/reference pairing gate before fitting.

## Real-Data Result

Command used:

```bash
./build/camera_iq ccm-fit clrs589_project_camera \
  --illuminant-spd "data/private/datasets/clrs589_project_camera/Sphere measurments/fernando_ff2.csv" \
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

FF2 DeltaE76 summary, 140 patches:

| Mean | RMS | Max |
|---:|---:|---:|
| 7.028 | 9.643 | 39.312 |

The three copied sphere SPDs give stable first-slice results:

| Illuminant file | White Z | Mean DeltaE76 | RMS DeltaE76 | Max DeltaE76 |
|---|---:|---:|---:|---:|
| `fernando_ff1.csv` | 84.180 | 7.044 | 9.661 | 39.317 |
| `fernando_ff2.csv` | 83.504 | 7.028 | 9.643 | 39.312 |
| `fernando_ff3.csv` | 83.358 | 7.025 | 9.640 | 39.311 |

`fernando_ff1.csv` contains negative spectrometer noise beyond the SG reference
axis, around 991 nm. The reader ignores that unused tail and still rejects any
negative interpolated value on the actual 380-730 nm target axis.

## Scientific Boundaries

- The result is labeled **vs compatible SG spectral reference**, not exact
  measured-reference DeltaE for the physical CLRS-589 chart.
- This is DeltaE76 only, not CIEDE2000.
- This is a linear 3x3 CCM only; root-polynomial and exposure-normalized color
  models are future work.
- The command consumes an existing patch RGB table. `camera_iq patches` now
  provides RAW-space patch means, but flat-field and white-balance policy still
  need to be made explicit before those means replace `ccsg_matlab.csv` as the
  CCM input.
- The command uses the supplied illuminant SPD and cannot verify illumination
  stability during the chart capture.

## Next Risks

1. Use `camera_iq patches` plus RawDigger coordinates as the RAW-space patch
   source, then add flat-field/white-balance policy before replacing
   `ccsg_matlab.csv` as the CCM input.
2. Add root-polynomial CCM variants before treating the
   DeltaE value as the best achievable color result.
3. Add CIEDE2000 once the color appearance/reference scope is locked.
