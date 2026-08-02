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

| Folder | Files | Naming | Grouping |
|---|---:|---|---|
| `Old/1 to 6` | 18 | `patch_<N>trail_<M>` | patches 1-6, triplicate |
| `Old/7 to 9` | 6 | `patch_<N>trail_<M>` | patches 7-9, duplicate |
| `Old/10 to 15` | 18 | `patch_<N>trail_<M>` | patches 10-15, triplicate |
| `Old/prd` | 2 | `prd_<M>` | one scene, duplicate |
| `PRD measurments` | 45 | `PRD_<K>` | 24 scenes, two readings each |

`load_all.m` averages the four `Old/` folders with its `stepSize` of 3 or 2.
`PRD measurments` is not one of them: `create_single_file.m` reads that folder
and writes every reading without averaging, and declares no group size.

Its 45 files are 24 scenes with two readings each, less the three missing second
readings. `PRD measurments copy/` holds the same 45 measurements under their
original scene names, and radiance matching in
[the capture manifest](../reports/FUJI_XT100_CCSG_MANIFEST.md) established
`PRD1sceneK -> PRD_(2K-1)` and `PRD2sceneK -> PRD_2K`. The filenames carry the
same evidence: the sequence runs `PRD_01` through `PRD_43`, then `PRD_45` and
`PRD_47`, with 44, 46 and 48 absent — the second readings of scenes 22, 23
and 24. Grouping this folder in threes would average across scene boundaries.

`Old/Old code/patch_data.m` is part of this ledger rather than scratch: it
averages the two `prd` readings, which is the provenance of the 16th row of
`Old/XYZ_all.csv`.

Each file is a MATLAB v5 MAT-file of about 2.3 KB holding one `measurements`
struct:

| Field | Shape | Note |
|---|---|---|
| `radiance` | 1 x 201 | 380-780 nm at 2 nm |
| `wl` | 1 x 201 | wavelength axis |
| `XYZ` | 1 x 3 | instrument-reported tristimulus; `PRD_01` records 291.736027 / 297.603271 / 290.922327 |
| `totalRadiance` | 1 x 1 | |
| `CCT` | 1 x 1 | instrument-reported correlated color temperature |
| `Duv` | 1 x 1 | |
| `repeatOnError` | 1 x 1 | |
| `numCurrentRepetitions` | 1 x 1 | |

Each of the 134 raw `measurements` files stores its whole payload as a single
`miCOMPRESSED` element, so reading one requires inflating a zlib stream before
any MAT element can be parsed. The 16 derived workspace saves under
`Old/Old code/` hold several elements instead, so the reader scans a stream
rather than assuming one element per file.

## What reproducing the recorded XYZ does and does not establish

Each file carries a spectrum and the instrument's `XYZ`, `CCT` and `Duv`. Those
are derived by the instrument from that same spectrum, not measured
independently of it, so reproducing them is a closure test on this pipeline
rather than a check against an independent reference. It validates MAT
ingestion, wavelength ordering, the integration convention, the observer data,
and the luminous-efficacy constant. It does not validate instrument accuracy,
radiometric calibration, or traceability.

[SG reference provenance](../reports/SG_REFERENCE_PROVENANCE.md) already
established the relationship: recomputing `683.017 * integral(SPD * CMF_2deg * 2nm)`
reproduced the recorded tristimulus with the scale constant 683.017 at zero
variance across 16 rows and three channels.

That result is reproduced here across every raw `measurements` struct in the
archive. Integrating on the measurements' own 2 nm axis against the committed
1 nm observer table (`data/cie1931_2deg_cmf_1nm.csv`) and scaling by
683.017 lm/W agrees with the recorded tristimulus to a maximum of **0.0000354%**
across all **134** files, on every channel.

The observer table is the whole of the difference. The same computation against
the 10 nm table (`data/cie1931_2deg_cmf.csv`) leaves residuals of -0.136% on X,
-0.048% on Y and -0.453% on Z, because interpolating a 10 nm grid up to a 2 nm
axis under-resolves the short-wavelength `z` lobe. Resampling the spectrum down
to 10 nm instead gives +0.241%, +0.116% and -0.284%. Neither is a bound on the
other: both are lossy, and the interval they span on Z excludes the correct
result entirely. The 10 nm table stays the observer for the chart-reflectance
work, whose references are themselves on a 10 nm grid; this command uses the
1 nm table because its input is finer than 10 nm.

`tools/check_cie_cmf_1nm.py` gates the new table on the 360-830 nm grid being
complete, `y` peaking at exactly 1.0 at 555 nm, the equal-energy stimulus
landing on x = y = 1/3, and agreement with the committed 10 nm table at every
shared wavelength.

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

`PRD measurments` is deliberately absent from that list. A fixed stride cannot
express it: 45 files over 24 scenes means three scenes carry a single reading,
so no constant group size divides the folder. Its repeats are identified by
index parity through `PRD1sceneK -> PRD_(2K-1)` and `PRD2sceneK -> PRD_2K`, which
is a different grouping rule and needs its own option rather than a stride that
would silently pair `PRD_43` with `PRD_45` across a scene boundary. Reproducing
`create_single_file.m`, which averages nothing, needs no grouping at all.

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

## Scope

This command reads primary spectroradiometer files, averages declared repeat
groups, and reports the closure above. It does not calibrate the instrument,
assign an absolute radiometric scale, establish traceability, or compare against
the separate 2016 monochromator archive's `PR655_HID_avg.txt`, which is a
different session with a different instrument record.
