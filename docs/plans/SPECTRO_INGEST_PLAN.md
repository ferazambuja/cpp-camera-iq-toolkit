# PR-655 Spectroradiometer Ingest — Implementation Record

## Measurement objective

`camera_iq spectro-ingest` reads primary spectroradiometer measurement files,
averages declared repeat groups, and checks the derived colorimetry against the
values the instrument recorded alongside each spectrum. It reports measured
spectra and repeatability; it does not calibrate the instrument, assign an
absolute radiometric scale, or establish traceability to a standard.

## Source material

The CLRS-589 archive holds the only MATLAB sources in this project. Two of them
describe this ingest and neither has a C++ equivalent:

| Script | Lines | Behavior |
|---|---:|---|
| `load_all.m` | 149 | Reads four folders of `.mat` files, averages each folder in fixed-size groups, writes `SPD_all.csv` and `XYZ_all.csv` |
| `create_single_file.m` | 30 | Reads one folder, writes every measurement without averaging |

The remaining scripts are already covered: `patch_extract.m` corresponds to
`patches --flat-field-raw`, `sphere_test.m` to `shading`, and `patchmask.m` and
`PatchMaskS.m` are third-party interactive tools (Lawrence Taplin, 2007)
superseded by `patches --sg-corners`.

## Data

Measurement counts and group sizes as the scripts declare them:

| Folder | Files | Group size | Result |
|---|---:|---:|---:|
| `Old/1 to 6` | 18 | 3 | 6 scenes |
| `Old/7 to 9` | 6 | 2 | 3 scenes |
| `Old/10 to 15` | 18 | 3 | 6 scenes |
| `Old/prd` | 2 | 2 | 1 scene |
| `PRD measurments` | 45 | 3 | 15 patches |

Each file is a MATLAB v5 MAT-file of about 2.3 KB holding one `measurements`
struct:

| Field | Shape | Note |
|---|---|---|
| `radiance` | 1 x 201 | 380-780 nm at 2 nm |
| `wl` | 1 x 201 | wavelength axis |
| `XYZ` | 1 x 3 | instrument-reported tristimulus |
| `totalRadiance` | 1 x 1 | |
| `CCT` | 1 x 1 | instrument-reported correlated color temperature |
| `Duv` | 1 x 1 | |
| `repeatOnError` | 1 x 1 | |
| `numCurrentRepetitions` | 1 x 1 | |

The whole payload of each file is a single `miCOMPRESSED` element, so reading
one requires inflating a zlib stream before any MAT element can be parsed.

## Why the instrument values are usable as a check

Each file carries a spectrum and the instrument's own `XYZ`, `CCT`, and `Duv`
for that same spectrum. Integrating the spectrum against color-matching
functions and comparing to the recorded tristimulus is therefore a closure test
with a measured reference rather than a self-consistency check.

The comparison has a limit the implementation reports rather than absorbs. The
committed CMF table (`data/cie1931_2deg_cmf.csv`) is a 10 nm grid over
380-730 nm; these measurements are a 2 nm grid over 380-780 nm. Reconciling the
two requires resampling, and the direction chosen changes the answer:

| Method | X | Y | Z |
|---|---:|---:|---:|
| Interpolate the CMF up to the 2 nm measurement grid | -0.136% | -0.048% | -0.453% |
| Resample the spectrum down to the CMF's 10 nm nodes | +0.241% | +0.116% | -0.284% |

Measured on `PRD_01.mat` against its recorded tristimulus, scaling by 683 lm/W.

Neither method is the correct one; both are bounded by the table. Reporting a
single figure would imply a precision the grid does not support, so the command
computes both and publishes the pair. Their spread — 0.38% on X, 0.16% on Y,
0.17% on Z — is the grid-induced uncertainty on any closure figure derived here.

Two components of that residual are separable and were measured rather than
assumed:

- **Truncation** at 730 nm is bounded at 0.040% of X and 0.014% of Y, and is
  exactly zero for Z, because the committed table's `z` value at 730 nm is
  0.0000000 and the lobe has ended well before it. Truncation cannot explain the
  Z residual.
- **Sampling** of the `z` lobe accounts for the rest. Z reads low under both
  methods rather than bracketing zero, which is the signature of a peak whose
  area a 10 nm grid loses regardless of integration direction.

The existing spectral commands are built on that same 10 nm grid and their
published results depend on it. Changing the shared table is out of scope here.

## Planned command

```bash
camera_iq spectro-ingest \
  --dataset clrs589_project_camera \
  --group "Old/1 to 6":3 \
  --group "Old/7 to 9":2 \
  --group "Old/10 to 15":3 \
  --group "Old/prd":2 \
  --out out/clrs589_spd.json \
  --spd-csv-out out/clrs589_spd.csv
```

`--group PATH:N` names a folder and its repeat-group size, replacing the
hard-coded folder paths and `stepSize` values in `load_all.m`. A folder whose
file count is not a multiple of its group size is an error rather than a
truncation, which is the one behavior of the original scripts that will not be
reproduced: MATLAB's `i:i+stepSize-1` indexing reads past the end of a short
final group.

## Reporting

- Per group: averaged spectrum, member file names, per-wavelength dispersion.
- Repeatability: coefficient of variation across each group's members, since the
  repeats are the only uncertainty evidence the archive provides.
- Closure: computed against recorded `XYZ` and `CCT` per measurement, with the
  CMF grid and the photometric constant recorded beside the result.
- Wavelength axes are checked for agreement across every file in a group; a
  disagreement rejects rather than interpolates.

## Acceptance rules

- A file whose `measurements` struct lacks a required field is an error naming
  the field, not a silently defaulted zero.
- Group size, file count, and wavelength axis are validated before any averaging.
- JSON records the effective CMF path, wavelength grid, and luminous efficacy
  constant, so a reported closure figure can be reproduced.
- Instrument-reported values are recorded as measured and never overwritten by
  computed ones.

## Implementation cycles

1. MATLAB v5 element reader: inflate, parse elements, read a struct of numeric
   arrays. Hermetic tests build their own MAT bytes.
2. Repeat-group averaging with wavelength-axis and count validation.
3. Colorimetry closure against the recorded `XYZ` and `CCT`.
4. Command, JSON and CSV output, report, and archive validation run.

## Out of scope

Absolute radiometric calibration, instrument traceability, and any comparison
against the separate 2016 monochromator archive's `PR655_HID_avg.txt`, which is
a different session with a different instrument record.
