# Camera IQ Toolkit

[![CI](https://github.com/ferazambuja/cpp-camera-iq-toolkit/actions/workflows/ci.yml/badge.svg)](https://github.com/ferazambuja/cpp-camera-iq-toolkit/actions/workflows/ci.yml)

This C++20 toolkit turns RAW camera captures and measured references into
inspectable image-quality results. It covers the measurement chain from
LibRaw/CFA handling through color, spectral, tone, noise, and slanted-edge
sharpness analysis, with structured JSON/CSV outputs and CTest-backed
validation.

**C++20 · LibRaw · CMake/CTest · numerical methods · color science · JSON/CSV**

## Engineering results at a glance

- **SFR/MTF:** processed **299 field ROIs** across Nikon D800 and D810 aperture
  sweeps. The D810 peak occurred at f/5.6. That trend did not hold on the D800,
  whose field is asymmetric, so the two bodies are reported under separate
  acceptance criteria rather than one.
- **Spectral characterization:** extracted a Canon 5D2 spectral-sensitivity
  function from monochromator RAW sweeps, closed four same-session camera/chart
  datasets with minimum channel correlation above **0.992**, and compared five
  cameras with Luther and ISO 17321-style color-fidelity metrics.
- **ColorChecker / CCM:** matched uncorrected 140-patch RAW extraction to a
  reference tool at correlation above **0.99999998** with sub-0.4 DN RMSE, then
  ran a corrected RAW-to-linear-CCM path with **4.134 mean held-out
  CIEDE2000**.
- **Measurement judgment:** rejected a ColorChecker grid despite correlations
  above 0.999 because its center error reached **16.449 px**, and rejected an
  invalid Stepchart strip model before accepting the measured ring geometry.
- **CFA flat-field response:** screened **52 sphere captures** from a Fujifilm X-T100 +
  Fujinon XF 14 mm f/2.8 R integrating-sphere set, retained three usable f/8 frames, and
  measured center-normalized green and chromatic fields. A **19.65% quadrant
  asymmetry** diagnoses departure from a centered radial scalar model; missing
  capture controls prevent isolated lens attribution regardless of the
  diagnostic verdict.
- **Flat-field input screening:** `patches` and `shading` evaluate source-CFA
  samples with the same per-position near-ceiling limits over both the full
  frame and a centered region. The test rejects a 1/500 s frame whose worst CFA
  position is **11.63% near ceiling** in the centered region, while retaining
  the 1/1000 s correction flat. The correction calculation after admission is
  unchanged.
- **Spectroradiometer ingest:** verified **89 distinct MATLAB readings** across
  40 measurement groups, separated absolute level from normalized spectral
  shape, and reproduced recorded XYZ with one archive-derived scale. Across the
  37 groups with multiple readings, level CV is **7.17% median**;
  normalized-shape and chromaticity diagnostics are reported separately and
  are nonzero.

## Featured case studies

| Case study | Methods | Result |
|---|---|---|
| [Nikon D800/D810 + 50 mm f/1.4G SFR aperture and field analysis](docs/case-studies/sfr-mtf-aperture-field.md) | Slanted-edge algorithm, field behavior, advisory cross-checks, failure transfer | 299 accepted field ROIs; capture-system-specific trend and field findings |
| [Spectral sensitivity and color fidelity](docs/case-studies/spectral-color-fidelity.md) | RAW monochromator extraction, physical closure, Luther/SMI comparison | Four-camera closure; stable five-camera endpoint ordering |
| [Spectroradiometer archive ingest](docs/case-studies/spectroradiometer-ingest.md) | Exact-byte identity, MATLAB v5 parsing, absolute/normalized group analysis, XYZ closure | 89 readings; level variation separated from shape and chromaticity |
| [ColorChecker extraction and CCM validation](docs/case-studies/colorchecker-ccm.md) | RAW patch extraction, flat field/WB, linear CCM, held-out Delta E | 140-patch pipeline with explicit dark-patch diagnostics |
| [CFA flat-field response](docs/case-studies/cfa-flat-field-response.md) | Black-subtracted Bayer grids, center normalization, near-ceiling/dark/pair checks | 3/52 usable sphere frames; green-field asymmetry separated from smaller R/G and B/G variation |

The [technical documentation index](docs/README.md) connects these case studies
to the OECF, noise, demosaic, localization, dataset, and provenance reports.

![Camera IQ toolkit measurement architecture](docs/figures/architecture.svg)

The current architecture uses a thin executable over one static C++ core that
contains command handling, validation, measurement algorithms, and
serialization. The diagram describes the implementation as it exists; it does
not imply a separate production service or ISP.

## Capability map

| Area | Implemented work |
|---|---|
| RAW/CFA | LibRaw unpack, active-area handling, tiled black subtraction, Bayer-plane statistics, bilinear demosaic |
| Color | ColorChecker-SG extraction, flat-field and WB policies, RGB-to-XYZ CCM fitting, Delta E 76/2000, held-out diagnostics |
| Spectral | Monochromator RAW extraction, physical closure, Luther-condition residuals, ISO 17321-style SMI approximation |
| Spectroradiometry | Exact-byte MATLAB v5 ingest, measurement-group absolute/normalized spectra, chromaticity, and same-record XYZ closure |
| Tone and noise | Exposure grouping, relative OECF, Stepchart oracle/ring extraction, dark temporal noise, DSNU, DN-referred variance |
| Sharpness | Green-linear slanted-edge SFR, MTF50/MTF50P, aperture sweeps, 23-ROI field maps |
| Spatial response | Per-CFA flat-field maps, center-normalized R/G and B/G fields, quadrant asymmetry, bounded dark-control checks, and one capture-pair delta |
| Engineering systems | CLI validation, JSON/CSV reporting, synthetic and negative-path tests, archive-backed checks, privacy-safe dataset IDs |

Implemented commands:

`manifest`, `raw-stats`, `demosaic`, `dark-calibration`, `noise`, `sfr`,
`exposure-response`, `oecf-fit`, `oecf-stepchart`, `reference-info`,
`ccm-fit`, `patches`, `spectral-response`, `spectral-closure`,
`spectral-quality`, `spectral-smi`, `spectro-ingest`, and `shading`.

## Reproducibility and data access

The source RAW datasets are intentionally outside Git. What is committed
therefore supports checking results rather than re-deriving them: aggregate
result tables, deterministic SVG generation from those tables, fixtures covering
the parser and CLI paths, and the test suite all run without the archives.
Re-measuring from RAW, or reproducing an archive-backed before/after comparison,
requires the private captures.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure

# Rebuild the figures from committed aggregate CSVs.
python3 tools/generate_portfolio_figures.py
python3 tools/generate_portfolio_figures.py --check
python3 tools/generate_spectro_report_figure.py --check
```

A small synthetic fixture exercises the dataset and manifest path end to end. It
carries no image-quality content and is not a substitute for a real capture:

```bash
./build/camera_iq manifest data/samples/manifest_fixture --no-exif \
  --out out/public_manifest_fixture.json
```

See [dataset handling](docs/DATASETS.md), the
[aggregate-data notes](docs/data/README.md), and the
[tool index](tools/README.md) for the public/private boundary and regeneration
commands.

## Build and run

Requirements: a C++20 compiler, CMake 3.20 or newer, and LibRaw
(`brew install libraw` or `apt-get install libraw-dev`).

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/camera_iq --help
```

Real-data commands use a dataset ID and paths relative to that dataset:

```bash
./build/camera_iq raw-stats --dataset clrs589_project_camera \
  "<relative/raw/file.RAF>" --out out/raw-stats.json

./build/camera_iq sfr d800_d810_sfr_2016 \
  --raw "<relative/slanted-edge/file.NEF>" \
  --oracle-y-multi "<relative/advisory-table.csv>" \
  --out out/sfr.json

./build/camera_iq spectro-ingest clrs589_project_camera \
  --ledger data/spectro_identity_ledger.csv \
  --verify-aliases \
  --out out/spectro-ingest.json \
  --groups-csv out/spectro-groups.csv \
  --spectra-csv out/spectro-spectra.csv \
  --readings-csv out/spectro-readings.csv
```

Copy `configs/datasets.example.json` to the gitignored
`configs/datasets.local.json` to configure local roots. Bulk RAW captures,
measured references, and commercial-tool exports remain outside the public
repository; the reports retain safe aggregates and enough provenance to
interpret them.

## Interpretation boundaries

This is a research toolkit, not a certified ISO laboratory suite or a
production ISP. The reports distinguish sensor-linear measurements from
rendered-luma comparisons, DN-domain diagnostics from electron-calibrated
quantities, compatible chart references from exact per-unit measurements, and
advisory reference-tool comparisons from equivalence claims.

Primary method families include ISO 12233-style slanted-edge SFR, ISO
14524-informed OECF work, ISO 15739/EMVA-inspired noise diagnostics, CIE
Delta E, and an ISO 17321-style SMI approximation. Exact conformance is claimed
only where the implementation and available calibration evidence support it.

## About

More projects and contact context:
[Imaging Engineering & Color Science profile](https://github.com/ferazambuja).

Project code and original documentation: [MIT](LICENSE). Named standard datasets
retain the terms recorded in [Third-party data notices](THIRD_PARTY_NOTICES.md).
