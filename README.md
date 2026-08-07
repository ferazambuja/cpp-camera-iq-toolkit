# Camera IQ Toolkit

[![CI](https://github.com/ferazambuja/cpp-camera-iq-toolkit/actions/workflows/ci.yml/badge.svg)](https://github.com/ferazambuja/cpp-camera-iq-toolkit/actions/workflows/ci.yml)

This C++20 toolkit turns RAW camera captures and measured references into
inspectable image-quality results. It covers the measurement chain from
LibRaw/CFA handling through color, spectral, tone, noise, and slanted-edge
sharpness analysis. Validation combines synthetic edge cases, archive-backed
measurements, and independent reference comparisons.

**C++20 · LibRaw · CMake · numerical methods · color science · JSON/CSV**

**Start here:** [case studies and reports](docs/README.md) ·
[source](src/) · [public headers](include/camera_iq/) ·
[implementation companions](docs/implementation/README.md) ·
[validation decisions](docs/README.md#validation-decisions)

New to the project? The
[SFR aperture and field study](docs/case-studies/sfr-mtf-aperture-field.md) is
the best single entry point: it measures two camera bodies with the same lens
model and shows why one center-of-frame sharpness number does not describe
either of them.

![Nikon D800 and D810 SFR aperture and field summary](docs/figures/sfr_aperture_field.svg)

*The flagship measurement. Left: centre MTF50 in cycles/pixel against aperture
for both bodies — the D810 peaks cleanly at f/5.6, while the D800 does not
reproduce that shape and sits below its own f/16 result wide open. Right:
centre minus strongest physical corner, where a negative bar means the corner
outresolved the centre. The two nominally matched systems disagree, which is
why one acceptance criterion cannot cover both.*

## Featured case studies

| Case study | Methods | Result |
|---|---|---|
| [Nikon D800/D810 + 50 mm f/1.4G SFR aperture and field analysis](docs/case-studies/sfr-mtf-aperture-field.md) | Slanted-edge algorithm, field behavior, advisory cross-checks, failure transfer | 299 accepted field ROIs; capture-system-specific trend and field findings |
| [Spectral sensitivity and color fidelity](docs/case-studies/spectral-color-fidelity.md) | RAW monochromator extraction, physical closure, Luther/SMI comparison | Four-camera closure; stable five-camera endpoint ordering |
| [Spectroradiometer archive ingest](docs/case-studies/spectroradiometer-ingest.md) | Exact-byte identity, MATLAB v5 parsing, absolute/normalized group analysis, XYZ closure | 89 readings; level variation separated from shape and chromaticity |
| [Spectral measurement and reference-data cross-check](docs/case-studies/spectral-archive-crosscheck.md) | Native/common-grid spectral analysis, residual localization, CGATS identity, explicit observers | 4.327% HID-series difference; 75.9% of squared residual at 530/540 nm; observer conflict resolved numerically |
| [ColorChecker extraction and CCM validation](docs/case-studies/colorchecker-ccm.md) | RAW patch extraction, flat field/WB, linear CCM, held-out Delta E | 140-patch pipeline with explicit dark-patch diagnostics |
| [CFA flat-field response](docs/case-studies/cfa-flat-field-response.md) | Black-subtracted Bayer grids, center normalization, near-ceiling/dark/pair checks | 3/52 usable sphere frames; green-field asymmetry separated from smaller R/G and B/G variation |
| [Display-P3 to sRGB gamut mapping](docs/case-studies/gamut-mapping.md) | D65 RGB/XYZ/Lab/OkLab transforms, analytic radial boundaries, dated Local MINDE, soft-knee experiment | P3-yellow overcompression reduced; coordinate and algorithm effects separated |
| [Color-model equation audit](docs/case-studies/color-model-equation-audit.md) | Published CAM16 equation behavior, corrected Hellwig coefficient, directional CIE94 | Model sensitivities reproduced without claiming full-model or observer validation |

The [technical documentation index](docs/README.md) connects these case studies
to the OECF, noise, demosaic, localization, dataset, and provenance reports.

## Selected results

- **Sharpness is not one number.** Slanted-edge SFR/MTF across **299 chart
  regions** on two Nikon bodies sharing a 50 mm lens model: the D810 peaked at
  f/5.6, the D800 peaked later and lower and put its field maximum off-axis.
  The D800 was manually focused with unverified accuracy, so that result is
  scoped to the capture session rather than the body.
- **Walking away from a good-looking number.** A ColorChecker grid was rejected
  despite RGB correlations above 0.999, because its centre error reached
  **16.449 px** against a declared 5 px limit — correlation cannot outvote
  geometry. An invalid Stepchart strip model was rejected the same way.
- **Spectral colour fidelity.** A Canon 5D2 spectral-sensitivity function was
  extracted from monochromator RAW sweeps and closed against same-session chart
  captures at **9.5–13.8% RMS per channel**. Five cameras compared on Luther
  residual and ISO 17321-style SMI span **90.7 to 88.3** — a stable ordering
  inside a practically small spread.
- **A colour matrix evaluated honestly.** A linear 3×3 RGB-to-XYZ fit on 140
  patches reached **4.134 mean held-out CIEDE2000** against **4.099** training
  error — a 0.035 gap, so the matrix generalizes. Restricting the fit to
  lighter patches produced a better-looking 3.221 that left all-patch error
  unchanged; the honest number is reported instead.
- **A shading model falsified.** Of **52 integrating-sphere captures**, 49 were
  too near the sensor ceiling to measure. In the usable frames, four corner
  blocks at equal distance from centre spread by **19.65%** of their average —
  a field depending only on radius must give all four the same value, so the
  centred radial model is excluded for this capture system.
- **Provenance from content, not filenames.** **89 spectroradiometer readings**
  stored under filenames that numbered acquisitions rather than scenes were
  recovered by content hash into 40 measurement groups, then characterized on
  level, spectral shape, and chromaticity separately — because level moves
  independently of the other two.
- **Localized spectral disagreement.** Two eight-reading
  HID series differ by **4.327% directional relative L2**, with **75.9%** of the
  squared residual at 530 and 540 nm. On the fixed 35-band sweep support,
  fitting a reference-axis offset to those same spectra lowers the objective by
  **28.7%**. It is a sensitivity result, not evidence of a registration error
  or other physical cause.

Two further studies are controlled algorithm and equation work rather than
camera measurements: the [gamut-mapping
comparison](docs/case-studies/gamut-mapping.md) and the [CAM16 equation
audit](docs/case-studies/color-model-equation-audit.md).

![Camera IQ toolkit measurement architecture](docs/figures/architecture.svg)

*Configured datasets enter through a thin command layer, while the reusable
C++ core keeps RAW decoding, scientific calculations, and typed results
separate from JSON/CSV serialization. Archive-backed reports, safe aggregate
tables, and deterministic figures are the public outputs; private source
captures remain outside the repository.*

## Capability map

| Area | Implemented work |
|---|---|
| RAW/CFA | LibRaw unpack, active-area handling, tiled black subtraction, Bayer-plane statistics, bilinear demosaic |
| Color | ColorChecker-SG extraction, flat-field and WB policies, RGB-to-XYZ CCM fitting, Delta E 76/2000, directional CIE94 plus a separately named historical variant, held-out diagnostics |
| Color management | sRGB/Display-P3 transfer and matrix transforms, D65 CIELAB and OkLab/OkLCh, analytic gamut boundaries, radial, dated Local-MINDE, and soft-knee methods; bounded CAM16 equation audit |
| Spectral | Monochromator RAW extraction, physical closure, Luther-condition residuals, ISO 17321-style SMI approximation, explicit-observer reflectance colorimetry |
| Spectroradiometry | Exact-byte MATLAB v5 ingest, measurement-group absolute/normalized spectra, cross-grid repeated-series comparison, residual localization, chromaticity, and same-record XYZ closure |
| Tone and noise | Exposure grouping, relative OECF, Stepchart oracle/ring extraction, dark temporal noise, DSNU, DN-referred variance |
| Sharpness | Green-linear slanted-edge SFR, MTF50/MTF50P, aperture sweeps, 23-ROI field maps |
| Spatial response | Per-CFA flat-field maps, center-normalized R/G and B/G fields, corner-field asymmetry, bounded dark-control checks, and one capture-pair delta |
| Validation and reporting | CLI validation, JSON/CSV reporting, synthetic and negative-path tests, archive-backed checks, privacy-safe dataset IDs |

Implemented commands:

`manifest`, `raw-stats`, `demosaic`, `dark-calibration`, `noise`, `sfr`,
`exposure-response`, `oecf-fit`, `oecf-stepchart`, `reference-info`,
`ccm-fit`, `patches`, `spectral-response`, `spectral-closure`,
`spectral-quality`, `spectral-smi`, `spectro-ingest`, `spectro-compare`,
`spectral-reference-audit`, `shading`, `gamut-map`, and
`cam16-equation-audit`.

## Reproducibility and data access

Bulk source RAW datasets are intentionally outside Git. Those studies are
supported by aggregate result tables, deterministic figures, parser and CLI
fixtures, and archive receipts; re-measuring them still requires the private
captures. The compact spectral coursework tables used by the cross-check are
committed with source hashes, so that study regenerates from its public inputs.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure

# Rebuild figures and verify the synthetic gamut results.
python3 tools/generate_portfolio_figures.py
python3 tools/generate_portfolio_figures.py --check
python3 tools/generate_spectro_report_figure.py --check
python3 tools/generate_2017_spectral_portfolio.py \
  --camera-iq build/camera_iq --check
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
`configs/datasets.local.json` to configure local roots. Bulk RAW captures and
some measured references remain outside the public repository. The small 2017
spectral text set is committed with source hashes because it is required to
reproduce that cross-check; the corresponding report identifies its incomplete
acquisition metadata.

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
