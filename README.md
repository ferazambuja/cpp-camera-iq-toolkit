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
#   pattern, CSV shape probes, and candidate exposure series.
./build/camera_iq manifest /path/to/dataset --out out/manifest.json
```

Implemented commands: `manifest`. Evidence reports for completed phases live
under [docs/reports/](docs/reports/).

## Data

This repository contains **code, a tiny sample fixture, and generated results** —
**not** the source image datasets. Datasets (RAW captures, measured references) live
outside the repo and are referenced by a local config:

- Copy `configs/datasets.example.json` to `configs/datasets.local.json` (gitignored)
  and set the paths to your own dataset roots.
- All captures used by the author were shot by the author on standard DSLR / camera
  bodies; large RAW sets are kept out of git for size and reproducibility, not
  licensing. A small sample under `data/samples/` lets `build → test` run with no
  private data.

## Method references

ISO 12233 (SFR/MTF), ISO 14524 (OECF),
ISO 15739 (noise/DR); EMVA 1288 (PTC / read noise / PRNU / DSNU); Finlayson et al.
root-polynomial color correction (2015); CIE ΔE2000.

## License

MIT — see [LICENSE](LICENSE).
