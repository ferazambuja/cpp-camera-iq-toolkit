# Aggregate result data

These result artifacts support the technical reports without exposing source
captures. They contain no RAW samples, absolute paths, commercial-tool exports,
or editable copies of private references.

| Table | Source report | Figure |
|---|---|---|
| [`sfr_aperture_summary.csv`](sfr_aperture_summary.csv) | [SFR/MTF](../reports/SFR_MTF.md) | [aperture and field](../figures/sfr_aperture_field.svg) |
| [`spectral_color_fidelity.csv`](spectral_color_fidelity.csv) | [spectral sensitivity](../reports/SPECTRAL_SENSITIVITY.md) | [five-camera comparison](../figures/spectral_color_fidelity.svg) |
| [`spectro_group_summary.csv`](spectro_group_summary.csv) | [spectroradiometer ingest](../reports/SPECTRORADIOMETER_INGEST.md) | [measurement-group variation](../figures/spectro_group_variation.svg) |
| [`spectro_result_receipt.json`](spectro_result_receipt.json) | [spectroradiometer ingest](../reports/SPECTRORADIOMETER_INGEST.md) | Hashes the archive-run result/readings and committed ledger, observer, and aggregate; includes checkable metrics and value domains |
| [`spectro_matlab_crosscheck_receipt.json`](spectro_matlab_crosscheck_receipt.json) | [spectroradiometer ingest](../reports/SPECTRORADIOMETER_INGEST.md) | Records the independent MATLAB/C++ parser comparison without exposing per-reading measurements or local paths |
| [`ccm_validation_summary.csv`](ccm_validation_summary.csv) | [CCM fit](../reports/CCM_FIT.md) | [CCM validation](../figures/ccm_validation.svg) |
| [`ccsg_f8_flat_wb_patches.csv`](ccsg_f8_flat_wb_patches.csv) | [patch extraction](../reports/PATCH_EXTRACTION.md) | accepted-flat 140-patch RGB regression table, enforced by `check_patch_baseline` |
| [`flat_field_summary.csv`](flat_field_summary.csv) | [flat-field response](../reports/FLAT_FIELD_RESPONSE.md) | [CFA flat-field response](../figures/flat_field_response.svg) |
| [`flat_field_response.csv`](flat_field_response.csv) | [flat-field response](../reports/FLAT_FIELD_RESPONSE.md) | [CFA flat-field response](../figures/flat_field_response.svg) |
| [`gamut_synthetic_input.csv`](gamut_synthetic_input.csv) | [gamut mapping](../reports/GAMUT_MAPPING.md) | Deterministic 5 × 5 × 5 encoded Display-P3 stress grid |
| [`gamut_synthetic_radial.csv`](gamut_synthetic_radial.csv) and [`gamut_synthetic_radial.json`](gamut_synthetic_radial.json) | [gamut mapping](../reports/GAMUT_MAPPING.md) | Schema-v3 CIELAB-radial result with typed boundary and IPT evidence |
| [`gamut_synthetic_oklch_radial.csv`](gamut_synthetic_oklch_radial.csv) and [`gamut_synthetic_oklch_radial.json`](gamut_synthetic_oklch_radial.json) | [gamut mapping](../reports/GAMUT_MAPPING.md) | Schema-v3 OkLCh-radial result isolating the coordinate-space change |
| [`gamut_synthetic_css_local_minde.csv`](gamut_synthetic_css_local_minde.csv) and [`gamut_synthetic_css_local_minde.json`](gamut_synthetic_css_local_minde.json) | [gamut mapping](../reports/GAMUT_MAPPING.md) | Schema-v3 dated CSS Local-MINDE result with typed search evidence |
| [`gamut_synthetic_soft.csv`](gamut_synthetic_soft.csv) and [`gamut_synthetic_soft.json`](gamut_synthetic_soft.json) | [gamut mapping](../reports/GAMUT_MAPPING.md) | Schema-v3 soft-knee result with boundary and IPT evidence |
| [`cam16_equation_audit.csv`](cam16_equation_audit.csv) and [`cam16_equation_audit.json`](cam16_equation_audit.json) | [CAM16 equation audit](../reports/CAM16_EQUATION_AUDIT.md) | [normalized brightness, isolated background factor, and fixed-response complete chroma-expression sweep](../figures/cam16_equation_audit.svg) |
| [`cie94_historical_24patch.csv`](cie94_historical_24patch.csv) | [CAM16 equation audit and CIE94 continuity check](../reports/CAM16_EQUATION_AUDIT.md) | Rounded Lab pairs and printed CIE94 values from the Color Pony 24-patch table in the color-management course project report *Color Matching Workflow for Art Reproduction*; the C++ tests recalculate both directional conventions and the historical variant |

The corrected-patch table is the headerless R/G/B output of the documented
`camera_iq patches --rgb-csv-out` command, in its 140-row extraction order. It
pins the published 1/1000 s result as an exact comparison baseline without
publishing RAW samples or RawDigger coordinates. CI protects this artifact's
integrity; reproducing implementation output still requires the private RAWs.

Toolkit SFR values retain eight-decimal measurement precision so derived field
margins do not depend on rounded operands; narrative tables round them to four
decimals. The aggregate-table figure generator is dependency-free and
deterministic. The gamut study additionally runs the built C++ command:

```bash
python3 tools/generate_portfolio_figures.py
python3 tools/generate_portfolio_figures.py --check
python3 tools/generate_gamut_portfolio.py --camera-iq build/camera_iq --check
python3 tools/generate_cam16_equation_audit.py \
  --camera-iq build/camera_iq --check
```

`check_portfolio_figures` runs through CTest and protects the aggregate-table
SVGs generated by `generate_portfolio_figures.py`. The separate
`check_gamut_portfolio` and `check_cam16_equation_audit` tests rerun their
compiled commands and fail when the committed gamut or equation-audit data and
SVGs no longer match. Both retain exact schemas. The equation audit allows
only `1e-12` relative or absolute numeric roundoff. The gamut check uses that
same tolerance except for angular diagnostics, where platform math libraries
can differ by up to `1e-5` degrees; it also reconciles every JSON sample with
its CSV row and recomputes the published aggregates.
The measurement architecture SVG is generated from the current repository
structure by `generate_portfolio_figures.py`.

These tables make the published results inspectable and plottable without
making the source capture datasets public. Reproducing the measurements from
RAW still requires a configured local dataset root as described in
[dataset handling](../DATASETS.md).
