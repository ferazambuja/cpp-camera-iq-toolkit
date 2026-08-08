# Implementation companions

These documents explain how the C++20 toolkit realizes the measurement methods
described in the [scientific reports](../README.md#technical-reports). They are
written for readers who want software architecture, formula-to-code mapping,
typed data flow, validation behavior, and direct source links without forcing
those details into the main scientific narrative.

| Companion | Software scope |
|---|---|
| [RAW foundation, calibration, exposure, and noise](raw-foundation.md) | LibRaw boundary, active Bayer data, black subtraction, demosaic, dark/noise calculations, exposure response, OECF, and Stepchart flow |
| [Slanted-edge SFR and field summaries](sfr-mtf.md) | Edge estimation, oversampled ESF/LSF, DFT, MTF summaries, archive-oracle parsing, and field aggregation |
| [Color chart extraction and CCM](color-characterization.md) | Chart coordinates, patch sampling, field correction, white balance, spectral reference rendering, matrix fit, held-out evaluation, and localization diagnostics |
| [CFA flat-field response](flat-field.md) | Frame gates, block maps, normalization, chromatic ratios, asymmetry, dark controls, comparison mode, and serialization |
| [Spectral sensitivity and color fidelity](spectral-fidelity.md) | Monochromator sweep extraction, physical closure, Luther-condition fit, and SMI-style color simulation |
| [Spectroradiometer recovery and analysis](spectroradiometer.md) | MATLAB v5 parsing, content identity, grouping, level/shape/chromaticity statistics, and XYZ closure |
| [Spectral measurement cross-check](spectral-crosscheck.md) | Generic repeated-spectrum analysis, common-grid comparison, CGATS interchange diagnostics, and explicit-observer colorimetry |
| [Gamut mapping](gamut-mapping.md) | Typed RGB/XYZ/perceptual transforms, analytic boundary search, four mapping intents, diagnostics, and artifacts |
| [Color-model equation audit](color-model-audit.md) | Bounded CAM16/Hellwig equation sweeps, CIE94 conventions, serialization, and generated figure |

The companions describe the current public implementation. Scientific meaning,
measurement conditions, results, and evidence limits remain canonical in the
linked reports.

## Build and validation entry point

The repository-wide build exercises every companion's public source and tests:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

## Artifact generation and freshness checks

```bash
python3 tools/generate_portfolio_figures.py
python3 tools/generate_portfolio_figures.py --check
python3 tools/generate_spectro_report_figure.py --check
python3 tools/generate_2017_spectral_portfolio.py \
  --camera-iq build/camera_iq --check
python3 tools/generate_gamut_portfolio.py --camera-iq build/camera_iq --check
python3 tools/generate_cam16_equation_audit.py \
  --camera-iq build/camera_iq --check
```

`check_portfolio_figures` protects the aggregate-table SVGs. The spectral
cross-check, gamut, and equation-audit checks execute the compiled C++ producer
and compare the complete committed JSON, CSV, and SVG artifacts. Numeric checks
use `1e-12` relative or absolute tolerance except for gamut angular diagnostics,
which allow `1e-5` degrees for platform math-library variation while still
reconciling every JSON sample with its CSV row.

Large archive-backed measurements additionally require locally configured
dataset roots. Their reports retain the scientific input selection and result;
command, serialization, and regeneration details belong here and in the linked
source.
