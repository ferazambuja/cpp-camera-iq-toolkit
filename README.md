# camera-iq-toolkit

A C++ toolkit for camera image-quality analysis from RAW and reference-chart data:
RAW/CFA handling, ColorChecker patch statistics, color-correction (CCM) and ΔE,
noise diagnostics, OECF/linearity, and reproducible CSV/JSON/Markdown reports.

> **Status: early development.** This is a portfolio research project, not a
> production ISP. Interfaces and outputs will change.

## What it does (planned tracks)

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

# Estimate DN-only temporal noise and DSNU from matched dark-frame pairs:
#   this is not gain/PTC/electron read noise/dynamic range.
./build/camera_iq noise clrs589_project_camera \
  --subdir "Images/Dark Frame" \
  --out out/noise.json

# Group black-subtracted CFA stats by detected exposure series:
#   this is a readiness/response summary, not a final ISO OECF/PTC metric.
./build/camera_iq exposure-response clrs589_project_camera --series-min 3 \
  --roi 1000,1000,500,500 \
  --out out/exposure-response.json

# Fit relative-exposure sensor linearity over usable OECF points:
#   assumes constant illumination; not ISO 14524; no PTC/noise/DR.
./build/camera_iq oecf-fit clrs589_project_camera --series-min 3 \
  --subdir "Images/Sphere" --series-limit 3 \
  --roi 1000,1000,500,500 \
  --out out/oecf-fit.json

# Inspect the configured ColorChecker-SG spectral reference and pairing gate:
./build/camera_iq reference-info clrs589_project_camera --out out/reference-info.json

# Fit a first linear RGB-to-XYZ CCM under an explicit illuminant SPD:
./build/camera_iq ccm-fit clrs589_project_camera \
  --illuminant-spd "data/private/datasets/clrs589_project_camera/Sphere measurments/fernando_ff2.csv" \
  --out out/ccm-fit.json

# Extract RAW-space SG patch means from a RawDigger coordinate export:
./build/camera_iq patches \
  "Images/CCSG_f8/CCSG_f8.0_1:10_DSCF0402.RAF" \
  --dataset clrs589_project_camera \
  --rawdigger-csv Images/CCSG_rawdigger.csv \
  --out out/patches.json

# Extract corrected RAW patch means and feed them into the CCM fitter:
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
  --camera-rgb out/raw-flat-wb-patches.csv \
  --out out/raw-flat-wb-ccm.json

# Optional, explicit dark-patch exclusion for flare-suspect SG patches:
./build/camera_iq ccm-fit clrs589_project_camera \
  --illuminant-spd "data/private/datasets/clrs589_project_camera/Sphere measurments/fernando_ff2.csv" \
  --camera-rgb out/raw-flat-wb-patches.csv \
  --exclude-ref-lightness-below 25 \
  --out out/raw-flat-wb-ccm-kept-l25.json

# Public no-private-data demo:
./build/camera_iq manifest data/samples/manifest_fixture --no-exif
```

Implemented commands: `manifest`, `raw-stats`, `demosaic`,
`dark-calibration`, `noise`, `exposure-response`, `oecf-fit`,
`oecf-stepchart`, `reference-info`, `ccm-fit`, `patches`, `spectral-response`,
`spectral-closure`, `spectral-quality`, and `spectral-smi`.
Evidence reports for completed phases live under
[docs/reports/](docs/reports/), with the consolidated coverage map at
[docs/reports/CAMERA_IQ_COVERAGE.md](docs/reports/CAMERA_IQ_COVERAGE.md).

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
