# Dataset Handling

RAW captures and generated outputs are private working data. They are not part
of the public repository.

## Local Private Cache

Use this gitignored layout for local mirrors:

```text
data/private/datasets/
  clrs589_project_camera/
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
  measurements under `data/private/references/sg_2016_archive/`.
- Manufacturer X-Rite/Zenodo Lab tables are optional public nominal comparisons,
  not replacements for dataset-specific reference provenance.

Export the private workbook to the C++ text format with:

```bash
python3 tools/export_ccsg_xlsx.py \
  data/private/references/ccsg_2019_workbook/ccsg.xlsx \
  --out data/private/references/ccsg_2019_workbook/ccsg_2_FIXED_ref.csv
```

Inspect the configured reference with:

```bash
./build/camera_iq reference-info clrs589_project_camera
./build/camera_iq reference-info monochromator_2016_color_checker
```

## Guardrail

`check_public_paths` runs in CTest and fails if tracked files contain private
absolute path prefixes such as local home or mounted-share paths.

`check_sample_fixtures` also runs in CTest. It keeps public samples small and
requires RAW-like fixtures under `data/samples/` to be tiny text placeholders
with an explicit synthetic/not-real marker. Real RAW captures belong in
`data/private/` or another local dataset root, not in tracked sample folders.
