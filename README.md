# camera-iq-toolkit

[![CI](https://github.com/ferazambuja/cpp-camera-iq-toolkit/actions/workflows/ci.yml/badge.svg)](https://github.com/ferazambuja/cpp-camera-iq-toolkit/actions/workflows/ci.yml)

A C++ toolkit for camera image-quality analysis from RAW and reference-chart data:
RAW/CFA handling, ColorChecker patch statistics, color-correction (CCM) and ΔE,
noise diagnostics, OECF/linearity, and reproducible CSV/JSON/Markdown reports.

> **Status: research toolkit.** The implemented slices are tested and
> documented, but this is not a production ISP or certified ISO lab suite.
> Interfaces and outputs may still change.

## What it covers

- **RAW front-end** — LibRaw unpack, black-level subtraction, hand-written demosaic,
  raw-CFA channel statistics.
- **Color characterization** — ColorChecker patch extraction, white balance,
  linear color-correction matrices, held-out diagnostics, and ΔE
  (76/2000) against compatible or measured references. Root-polynomial models
  are deferred until they prove held-out improvement.
- **Spectral sensitivity** — archived monochromator / camSPECS camera sweeps,
  parsed as camera spectral-sensitivity datasets rather than ColorChecker
  references. Legacy script outputs are fidelity checks, not correctness
  oracles.
- **Objective IQ metrics** — dark-frame temporal noise / DSNU diagnostics;
  OECF/linearity (CLRS exposure-response fits plus a D800 Imatest Stepchart
  oracle path); slanted-edge SFR/MTF as green-linear center-ROI and 23-ROI
  field maps for the D810 and D800 archives, hard-gated on offset-independent
  aperture trends with per-file Imatest `_Y_multi.csv` values as advisory
  references; and seeded Stepchart raw-zone paths (strip corners or ring
  center/radius/angle) guarded by an empirical oracle-ladder gate. The D800
  OECF chart turned out to be an ISO 14524-style ring layout, so the 20x1
  strip model refuses on that archive;
  the measured ring seed produces accepted raw-DN zone summaries plus
  DN-referred per-pixel temporal variance diagnostics. Electron-calibrated
  gain/read noise, full well, engineering dynamic range, measured ISO speed,
  and PRNU still need additional calibration or capture support.
- **Reporting** — CSV/JSON export, phase evidence reports, and deeper technical
  notes with explicit claim boundaries.

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

# Public no-private-data demo using tiny synthetic fixtures:
./build/camera_iq manifest data/samples/manifest_fixture --no-exif \
  --out out/public_manifest_fixture.json
```

The full command surface is exercised by the test suite and documented in the
coverage report. Real RAW validation examples use local dataset IDs configured
outside git; large RAW captures and measured references are not committed to
this repository.

```bash
# Private-data examples use dataset IDs rather than absolute paths:
./build/camera_iq raw-stats --dataset clrs589_project_camera \
  "<relative/raw/file.RAF>" --out out/raw-stats.json
./build/camera_iq sfr d800_d810_sfr_2016 \
  --raw "<relative/slanted-edge/file.NEF>" \
  --oracle-y-multi "<relative/imatest/file_Y_multi.csv>" \
  --out out/sfr.json
./build/camera_iq spectral-smi \
  --ssf-csv "<camera-ssf.csv>" \
  --reflectance-csv "<chart-reflectance.csv>" \
  --illuminant-spd data/cie_d55.csv \
  --out out/smi.json
```

Implemented commands: `manifest`, `raw-stats`, `demosaic`,
`dark-calibration`, `noise`, `exposure-response`, `oecf-fit`,
`oecf-stepchart`, `reference-info`, `ccm-fit`, `patches`, `spectral-response`,
`spectral-closure`, `spectral-quality`, and `spectral-smi`.
Evidence reports for completed phases live under
[docs/reports/](docs/reports/), with the consolidated coverage map at
[docs/reports/CAMERA_IQ_COVERAGE.md](docs/reports/CAMERA_IQ_COVERAGE.md).
Selected technical deep dives with plots live under [docs/reports/](docs/reports/).

## Data

This repository contains **code and tiny public fixtures** —
**not** the source image datasets. Datasets (RAW captures, measured references) live
in a gitignored private cache or outside the repo and are referenced by a local
config:

- Copy `configs/datasets.example.json` to `configs/datasets.local.json` (gitignored)
  and set the paths to your own dataset roots or local mirrors under
  `data/private/datasets/<dataset_id>/`.
- Public docs and JSON labels use stable dataset IDs such as
  `clrs589_project_camera` and `spectral_sensitivity_2016_2017`, never
  machine-specific absolute paths.
- All captures used by the author were shot by the author on DSLR, mirrorless,
  and medium-format camera systems; large RAW sets are kept out of git for size
  and reproducibility, not licensing. A small sample under `data/samples/` lets
  `build → test` run with no private data.
- CTest runs public-path and sample-fixture guards so tracked docs avoid local
  absolute paths and tracked samples stay tiny synthetic placeholders.
- See [docs/DATASETS.md](docs/DATASETS.md) for the local-cache policy.

## Method references

ISO 12233 (SFR/MTF), ISO 14524 (OECF),
ISO 15739 (noise/DR); EMVA 1288 (PTC / read noise / PRNU / DSNU); Finlayson et al.
root-polynomial color correction (2015); CIE ΔE2000.

## License

MIT — see [LICENSE](LICENSE).
