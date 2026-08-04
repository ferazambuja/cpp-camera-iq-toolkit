# Camera IQ Toolkit

[![CI](https://github.com/ferazambuja/cpp-camera-iq-toolkit/actions/workflows/ci.yml/badge.svg)](https://github.com/ferazambuja/cpp-camera-iq-toolkit/actions/workflows/ci.yml)

This C++20 toolkit turns RAW camera captures and measured references into
inspectable image-quality results. It covers the measurement chain from
LibRaw/CFA handling through color, spectral, tone, noise, and slanted-edge
sharpness analysis. Validation combines synthetic edge cases, archive-backed
measurements, and independent reference comparisons.

**C++20 · LibRaw · CMake · numerical methods · color science · JSON/CSV**

## Selected results and engineering decisions

- **SFR/MTF:** processed **299 field ROIs** across Nikon D800 and D810 aperture
  sweeps. The D810 peak occurred at f/5.6. That trend did not hold on the D800,
  which showed asymmetric off-axis behavior, so the two capture systems are
  reported under separate acceptance criteria rather than one.
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
- **CFA flat-field response:** screened **52 sphere captures** from a Fujifilm
  X-T100 and Fujinon XF 14 mm f/2.8 R integrating-sphere set, retained three
  usable f/8 frames, and measured center-normalized green and chromatic fields.
  A **19.65% quadrant asymmetry** exceeded the declared 5% criterion and was
  inconsistent with a centered radial scalar model for the measured composite
  field; missing capture controls prevent isolated lens attribution.
- **Flat-field input screening:** `patches` and `shading` evaluate source-CFA
  samples with the same per-position near-ceiling limits over both the full
  frame and a centered region. The test rejects a 1/500 s frame whose worst CFA
  position is **11.63% near ceiling** in the centered region, while retaining
  the 1/1000 s correction flat. The accepted flat uses the full-frame
  valid-sample mean for correction normalization.
- **Spectroradiometer ingest:** parsed **89 distinct MATLAB v5 readings** into
  40 measurement groups, resolved by content hash rather than by filenames that
  number acquisitions instead of scenes. Across the 37 multi-reading groups,
  spectral-integral CV was **7.17% median** and **41.65% maximum**; maximum
  normalized-shape residual was **1.076%**; and maximum recorded-XYZ pair
  separation was **0.002852 Δu′v′**. These metrics describe different
  properties, their maxima occur in different groups, and they do not identify
  a cause. An independent MATLAB R2026a export matched all 89 readings,
  including source-file identities and exact hashes for **178 numeric vectors**.
- **Gamut mapping:** on a 125-point synthetic Display-P3 grid, **94 points**
  began outside sRGB. Changing only the radial coordinates from CIELAB to
  OkLCh cut P3 yellow, the CIELAB worst case, from **23.928 to 5.523**
  CIEDE2000 with far more chroma retained, lowering the grid maximum to
  **9.956** at a new worst point in red, while the grid mean rose from
  **2.857 to 2.947**: a targeted trade, not a uniform gain. Changing only the
  OkLCh algorithm to Local MINDE then reduced both the grid mean (**2.947 to
  2.323**) and the maximum (**9.956 to 7.602**), at a wider IPT-hue 90th
  percentile. The C++ implementation includes CIELAB and OkLCh
  mapping, analytic channel-boundary searches, and a dated CSS Color 4
  Local-MINDE method. No one method wins every statistic; this is a controlled
  algorithm study, not observer validation.
- **Color-model equation audit:** reproduced half normalized brightness at
  `J = 25` versus half lightness at `J = 50` and isolated the
  background-dependent `N_cb^0.9` factor, which reaches **2.595×** its
  `Y_background = 20` value at `Y_background = 0.1`. Including the paper's
  coupled `z` and `n` terms under fixed adapted responses produces
  **2.120–2.687×** across reference `J = 10…90`, showing that the isolated term
  is neither an upper nor lower bound. The audit keeps the source paper's
  unfavorable outcome, a LUTCHI colorfulness `R²` drop from **0.81 to 0.71**,
  alongside its gains. The bounded C++ tests pin the corrected 2022 Equation
  23 coefficient `43` and make CIE94 reference direction explicit.

## Featured case studies

| Case study | Methods | Result |
|---|---|---|
| [Nikon D800/D810 + 50 mm f/1.4G SFR aperture and field analysis](docs/case-studies/sfr-mtf-aperture-field.md) | Slanted-edge algorithm, field behavior, advisory cross-checks, failure transfer | 299 accepted field ROIs; capture-system-specific trend and field findings |
| [Spectral sensitivity and color fidelity](docs/case-studies/spectral-color-fidelity.md) | RAW monochromator extraction, physical closure, Luther/SMI comparison | Four-camera closure; stable five-camera endpoint ordering |
| [Spectroradiometer archive ingest](docs/case-studies/spectroradiometer-ingest.md) | Exact-byte identity, MATLAB v5 parsing, absolute/normalized group analysis, XYZ closure | 89 readings; level variation separated from shape and chromaticity |
| [ColorChecker extraction and CCM validation](docs/case-studies/colorchecker-ccm.md) | RAW patch extraction, flat field/WB, linear CCM, held-out Delta E | 140-patch pipeline with explicit dark-patch diagnostics |
| [CFA flat-field response](docs/case-studies/cfa-flat-field-response.md) | Black-subtracted Bayer grids, center normalization, near-ceiling/dark/pair checks | 3/52 usable sphere frames; green-field asymmetry separated from smaller R/G and B/G variation |
| [Display-P3 to sRGB gamut mapping](docs/case-studies/gamut-mapping.md) | D65 RGB/XYZ/Lab/OkLab transforms, analytic radial boundaries, dated Local MINDE, soft-knee experiment | P3-yellow overcompression reduced; coordinate and algorithm effects separated |
| [Color-model equation audit](docs/case-studies/color-model-equation-audit.md) | Published CAM16 equation behavior, corrected Hellwig coefficient, directional CIE94 | Model sensitivities reproduced without claiming full-model or observer validation |

The [technical documentation index](docs/README.md) connects these case studies
to the OECF, noise, demosaic, localization, dataset, and provenance reports.

![Camera IQ toolkit measurement architecture](docs/figures/architecture.svg)

A thin command-line interface over a reusable C++ core keeps input validation,
measurement algorithms, and result serialization independently testable.

## Capability map

| Area | Implemented work |
|---|---|
| RAW/CFA | LibRaw unpack, active-area handling, tiled black subtraction, Bayer-plane statistics, bilinear demosaic |
| Color | ColorChecker-SG extraction, flat-field and WB policies, RGB-to-XYZ CCM fitting, Delta E 76/2000, directional CIE94 plus a separately named historical variant, held-out diagnostics |
| Color management | sRGB/Display-P3 transfer and matrix transforms, D65 CIELAB and OkLab/OkLCh, analytic gamut boundaries, radial, dated Local-MINDE, and soft-knee methods; bounded CAM16 equation audit |
| Spectral | Monochromator RAW extraction, physical closure, Luther-condition residuals, ISO 17321-style SMI approximation |
| Spectroradiometry | Exact-byte MATLAB v5 ingest, measurement-group absolute/normalized spectra, chromaticity, and same-record XYZ closure |
| Tone and noise | Exposure grouping, relative OECF, Stepchart oracle/ring extraction, dark temporal noise, DSNU, DN-referred variance |
| Sharpness | Green-linear slanted-edge SFR, MTF50/MTF50P, aperture sweeps, 23-ROI field maps |
| Spatial response | Per-CFA flat-field maps, center-normalized R/G and B/G fields, quadrant asymmetry, bounded dark-control checks, and one capture-pair delta |
| Validation and reporting | CLI validation, JSON/CSV reporting, synthetic and negative-path tests, archive-backed checks, privacy-safe dataset IDs |

Implemented commands:

`manifest`, `raw-stats`, `demosaic`, `dark-calibration`, `noise`, `sfr`,
`exposure-response`, `oecf-fit`, `oecf-stepchart`, `reference-info`,
`ccm-fit`, `patches`, `spectral-response`, `spectral-closure`,
`spectral-quality`, `spectral-smi`, `spectro-ingest`, `shading`, `gamut-map`,
and `cam16-equation-audit`.

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

# Rebuild figures and verify the synthetic gamut results.
python3 tools/generate_portfolio_figures.py
python3 tools/generate_portfolio_figures.py --check
python3 tools/generate_spectro_report_figure.py --check
python3 tools/generate_gamut_portfolio.py --camera-iq build/camera_iq --check
python3 tools/generate_cam16_equation_audit.py \
  --camera-iq build/camera_iq --check
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
The optional LittleCMS reference test uses `little-cms2` on macOS or
`liblcms2-dev` on Debian/Ubuntu and is enabled with
`-DCAMERA_IQ_ENABLE_LCMS=ON`.

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

./build/camera_iq gamut-map docs/data/gamut_synthetic_input.csv \
  --out-json out/gamut-map.json \
  --out-csv out/gamut-map.csv

./build/camera_iq cam16-equation-audit \
  --out-json out/cam16-equation-audit.json \
  --out-csv out/cam16-equation-audit.csv
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
