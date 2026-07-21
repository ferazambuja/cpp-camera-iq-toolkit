# Dataset Handling

RAW captures and generated outputs are private working data. They are not part
of the public repository.

## Private Dataset Roots

Private data can live in a local gitignored cache or in mounted archive folders.
The checked-in example uses relative `data/private/...` roots because they are
safe placeholders, not because the heavy captures must be copied into this repo.

Typical local-cache layout:

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
each dataset ID at the matching private root. On this machine, the heavy RAW /
TIFF datasets can point at mounted archive roots while small CSV/XLSX/reference
sidecars remain under `data/private/...`.

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

## Reference and Archive Policy

Reference selection is explicit in `configs/datasets.local.json`; it is never
inferred from camera EXIF dates. The checked-in example documents the expected
shape of each dataset entry, while the detailed evidence and caveats for each
archive live in `docs/reports/`.

General rules:

- Use dataset IDs and relative paths in commands and reports.
- Keep measured references under `data/private/references/`.
- Keep RAW captures, TIFFs, Imatest exports, generated JSON, and generated CSVs
  out of git.
- Treat historical scripts and CSVs as method/fidelity context unless a report
  explicitly identifies them as independent measurement evidence.
- Use `reference-info` to validate configured ColorChecker references before a
  later CCM/DeltaE step consumes them.

Typical private-data command shape:

```bash
./build/camera_iq reference-info clrs589_project_camera
./build/camera_iq manifest spectral_sensitivity_2016_2017 \
  --out out/spectral_sensitivity_manifest.json
./build/camera_iq patches "<relative/raw/file.RAF>" \
  --dataset clrs589_project_camera \
  --rawdigger-csv "<relative/coordinates.csv>" \
  --out out/patches.json
```

The consolidated evidence map is
`docs/reports/CAMERA_IQ_COVERAGE.md`. Dataset-specific details are intentionally
kept in reports rather than in this public data-policy page.

## Public Repository Checks

`check_public_paths` runs in CTest and fails if tracked files contain private
absolute path prefixes such as local home or mounted-share paths.

`check_sample_fixtures` also runs in CTest. It keeps public samples small and
requires RAW-like fixtures under `data/samples/` to be tiny text placeholders.
with an explicit synthetic/not-real marker. Real RAW captures belong in
`data/private/` or another local dataset root, not in tracked sample folders.
