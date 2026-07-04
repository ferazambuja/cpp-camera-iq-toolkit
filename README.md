# camera-iq-toolkit

A C++ toolkit for camera image-quality analysis from RAW and reference-chart data:
RAW/CFA handling, ColorChecker patch statistics, color-correction (CCM) and ΔE,
noise/SNR, OECF/linearity, and reproducible CSV/JSON/Markdown reports.

> **Status: early development.** This is a portfolio research project, not a
> production ISP. Interfaces and outputs will change.

## What it does (planned tracks)

- **RAW front-end** — LibRaw unpack, black-level subtraction, hand-written demosaic,
  raw-CFA channel statistics.
- **Color characterization** — ColorChecker patch extraction, white balance, linear
  and root-polynomial color-correction matrices, ΔE (76/94/2000) against a measured
  reference.
- **Objective IQ metrics** — read noise / DSNU / PRNU, photon-transfer-curve summaries,
  dynamic range, slanted-edge SFR/MTF, OECF/linearity.
- **Reporting** — batch runner, threshold checks, CSV/JSON export, Markdown reports.

## Build

Requires a C++20 compiler, CMake ≥ 3.20, and LibRaw
(`brew install libraw` / `apt-get install libraw-dev`).

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

## Run

```bash
./build/camera_iq --help

# Enumerate a dataset folder into a JSON manifest:
#   file inventory, filename-encoded exposure metadata, LibRaw EXIF + CFA
#   pattern, CSV shape probes, and candidate exposure series. This is a
#   metadata-only pass; some makers finalize black/pitch only during unpack().
./build/camera_iq manifest clrs589_project_camera --out out/manifest.json

# Compute signed black-subtracted per-CFA-position stats for one RAW file:
./build/camera_iq raw-stats --dataset clrs589_project_camera \
  "Images/CCSG/CCSG_f9.0_1:100_ISO200_DSCF0299.RAF" \
  --out out/raw-stats.json

# Compute hand-written bilinear demosaic summary stats for one RAW file:
./build/camera_iq demosaic --dataset clrs589_project_camera \
  "Images/CCSG/CCSG_f9.0_1:100_ISO200_DSCF0299.RAF" \
  --out out/demosaic.json

# Reconcile metadata black against measured dark frames:
./build/camera_iq dark-calibration clrs589_project_camera \
  --subdir "Images/Dark Frame" \
  --out out/dark-calibration.json

# Group black-subtracted CFA stats by detected exposure series:
#   this is a readiness/response summary, not a final ISO OECF/PTC metric.
./build/camera_iq exposure-response clrs589_project_camera --series-min 3 \
  --roi 1000,1000,500,500 \
  --out out/exposure-response.json

# Public no-private-data demo:
./build/camera_iq manifest data/samples/manifest_fixture --no-exif
```

Implemented commands: `manifest`, `raw-stats`, `demosaic`,
`dark-calibration`, and `exposure-response`. Evidence reports for completed
phases live under [docs/reports/](docs/reports/).

## Data

This repository contains **code and tiny public fixtures** —
**not** the source image datasets. Datasets (RAW captures, measured references) live
in a gitignored private cache or outside the repo and are referenced by a local
config:

- Copy `configs/datasets.example.json` to `configs/datasets.local.json` (gitignored)
  and set the paths to your own dataset roots or local mirrors under
  `data/private/datasets/<dataset_id>/`.
- Public docs and JSON labels use stable dataset IDs such as
  `clrs589_project_camera`, never machine-specific absolute paths.
- All captures used by the author were shot by the author on standard DSLR / camera
  bodies; large RAW sets are kept out of git for size and reproducibility, not
  licensing. A small sample under `data/samples/` lets `build → test` run with no
  private data.
- CTest runs public-path and sample-fixture guards so tracked docs avoid local
  absolute paths and tracked samples stay tiny synthetic placeholders.
- See [docs/DATASETS.md](docs/DATASETS.md) for the local-cache policy.

## Method references

ISO 12233 (SFR/MTF), ISO 14524 (OECF),
ISO 15739 (noise/DR); EMVA 1288 (PTC / read noise / PRNU / DSNU); Finlayson et al.
root-polynomial color correction (2015); CIE ΔE2000.

## License

MIT — see [LICENSE](LICENSE).
