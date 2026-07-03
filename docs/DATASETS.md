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

## Guardrail

`check_public_paths` runs in CTest and fails if tracked files contain private
absolute path prefixes such as local home or mounted-share paths.
