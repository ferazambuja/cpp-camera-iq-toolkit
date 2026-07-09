# Dataset Handling

RAW captures and generated outputs are private working data. They are not part
of the public repository.

## Local Private Cache

Use this gitignored layout for local mirrors:

```text
data/private/datasets/
  clrs589_project_camera/
  spectral_sensitivity_2016_2017/
  canon_5d2_repro/
  d800_oecf_2016/
data/private/references/
  ccsg_2019_workbook/
  sg_2016_archive/
```

Copy `configs/datasets.example.json` to `configs/datasets.local.json` and point
each dataset ID at the matching local mirror. The checked-in example uses
relative `data/private/...` roots so public docs never need machine-specific
paths.

## Public References

Public commands and reports should use dataset IDs:

```bash
./build/camera_iq manifest clrs589_project_camera --out out/clrs589_manifest.json
./build/camera_iq raw-stats --dataset clrs589_project_camera \
  "Images/CCSG/CCSG_f9.0_1:100_ISO200_DSCF0299.RAF"
```

The JSON output labels those sources as `dataset:<id>` or
`dataset:<id>/<relative-path>`. Absolute local paths belong only in the
gitignored `configs/datasets.local.json`, shell history, or private notes.

## Color References

Color-reference selection is explicit in `configs/datasets.local.json`; it is
never inferred from camera EXIF dates. Use project/archive provenance:

- CLRS-589 / newer ColorChecker-SG work uses the local 2019 workbook export:
  `data/private/references/ccsg_2019_workbook/ccsg_2_FIXED_ref.csv`, generated
  from `ccsg.xlsx` sheet `ccsg_2_FIXED_ref`.
- 2016/2017 camera-characterization archives use the contemporaneous 2016 SG
  measurements under `data/private/references/sg_2016_archive/`. The checked
  example records the PatchTool file order (`A1..A10`, then `B1..B10`, ...);
  color consumers must respect or remap that order before pairing it to camera
  patch tables.
- Manufacturer X-Rite/Zenodo Lab tables are optional public nominal comparisons,
  not replacements for dataset-specific reference provenance.

The `color_reference` block carries typed provenance (`source`, `illuminant`,
`observer`, `unit`, `numbering_order`) plus validation constraints
(`expected_patch_count`, `expected_band_count`, wavelength endpoints, and
reflectance range). `reference-info` rejects references that violate those
constraints before a later CCM/DeltaE step can consume them.

For CLRS-589, `reference-info` also runs a camera/reference pairing gate when
`pairing_rgb_path` is configured. This is a coarse order sanity check, not a
color-accuracy metric: it correlates camera green against a reference green-band
luminance proxy, plus normalized red-green and blue-green chroma proxies. The
gate exists because an internally well-formed A1..N10 reference can still be
paired to the wrong camera row order.

Export the private workbook to the C++ text format with an explicit wavelength
axis. The workbook sheet stores data rows only, so the axis is supplied by the
export command and then written into the CSV header:

```bash
python3 tools/export_ccsg_xlsx.py \
  data/private/references/ccsg_2019_workbook/ccsg.xlsx \
  --out data/private/references/ccsg_2019_workbook/ccsg_2_FIXED_ref.csv
```

Inspect the configured reference with:

```bash
./build/camera_iq reference-info clrs589_project_camera
```

`spectral_sensitivity_2016_2017` is a camera spectral-sensitivity sweep
dataset, not a ColorChecker-SG reference dataset, so it intentionally does not
carry a `color_reference` block in the checked-in example. Keep SG reflectance
references under `data/private/references/sg_2016_archive/`; do not conflate
`sg_2016_archive/monochromator_color_checker` with the camera monochromator /
camSPECS RAW sweep archive.

For the spectral-sensitivity archive, start by manifesting a scoped local subset
rather than bulk-copying the full RAW archive:

```bash
./build/camera_iq manifest spectral_sensitivity_2016_2017 \
  --out out/spectral_sensitivity_manifest.json
```

For the Nikon D800 OECF / Stepchart archive, mirror the scoped
`2016_12_10_D800_OECF` subset under `data/private/datasets/d800_oecf_2016/`,
including its `Results/*_summary.csv` Imatest Stepchart summaries. Those
summaries are private oracle files and may contain local-directory metadata in
the metadata tail; command outputs must omit or sanitize those paths. The
Stepchart summaries are rendered luminance-oracle data, not raw CFA OECF data.
The current evidence report is `docs/reports/OECF_STEPCHART.md`.

Fit the first linear CCM with an explicit local illuminant SPD:

```bash
./build/camera_iq ccm-fit clrs589_project_camera \
  --illuminant-spd "data/private/datasets/clrs589_project_camera/Sphere measurments/fernando_ff2.csv"
```

Extract RAW-space SG patch means with RawDigger's local coordinate export:

```bash
./build/camera_iq patches \
  "Images/CCSG_f8/CCSG_f8.0_1:10_DSCF0402.RAF" \
  --dataset clrs589_project_camera \
  --rawdigger-csv Images/CCSG_rawdigger.csv
```

Extract a corrected RAW-derived RGB table for `ccm-fit`:

```bash
./build/camera_iq patches \
  "Images/CCSG_f8/CCSG_f8.0_1:10_DSCF0402.RAF" \
  --dataset clrs589_project_camera \
  --rawdigger-csv Images/CCSG_rawdigger.csv \
  --flat-field-raw "Images/Sphere/Sphere_f8.0_1:1000_DSCF0387.RAF" \
  --wb-from-flat-field \
  --rgb-csv-out out/raw-flat-wb-patches.csv \
  --out out/raw-flat-wb-patches.json

./build/camera_iq ccm-fit clrs589_project_camera \
  --illuminant-spd "data/private/datasets/clrs589_project_camera/Sphere measurments/fernando_ff2.csv" \
  --camera-rgb out/raw-flat-wb-patches.csv
```

Do not use saturated sphere/flat captures for vignetting correction. For the
current CLRS-589 f/8 SG validation run, the `1:10` through roughly `1:200` sphere
frames sit too close to the flat maximum; `Sphere_f8.0_1:1000_DSCF0387.RAF`
preserves usable spatial variation. The command rejects flats when more than
1% of demosaiced channel samples sit above 98% of the black-subtracted sensor
ceiling. The correction scale is recorded in JSON as
`normalization: "per_channel_mean_valid_samples"`: each RGB channel uses the
mean of flat samples above the denominator floor, not the flat image maximum.

Flat-field coverage is aperture-limited in the local CLRS-589 cache. The f/8
sphere set has usable same-aperture candidates (`1:500`, two `1:1000` frames,
and `1:1600`; the validation commands use `Sphere_f8.0_1:1000_DSCF0387.RAF`). The
f/9 sphere set has 13 frames from `1:10` through `1:180`, and every one is
rejected by the near-ceiling guard. Therefore the flat-fielded RAW color path is
scoped to the f/8 CCSG series unless a cross-aperture approximation is chosen
and labeled explicitly.

## Guardrail

`check_public_paths` runs in CTest and fails if tracked files contain private
absolute path prefixes such as local home or mounted-share paths.

`check_sample_fixtures` also runs in CTest. It keeps public samples small and
requires RAW-like fixtures under `data/samples/` to be tiny text placeholders
with an explicit synthetic/not-real marker. Real RAW captures belong in
`data/private/` or another local dataset root, not in tracked sample folders.
