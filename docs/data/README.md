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
| [`hid_spectral_comparison.json`](hid_spectral_comparison.json) and [`hid_spectral_comparison.csv`](hid_spectral_comparison.csv) | [spectral cross-check](../reports/SPECTRAL_CROSSCHECK_2017.md) | Repeated-series summaries, common-grid residual, diagnostic exclusions, signed offset sensitivity, and per-band evidence before and after the best offset |
| [`spectral_reference_audit.json`](spectral_reference_audit.json) | [spectral cross-check](../reports/SPECTRAL_CROSSCHECK_2017.md) | Per-export CGATS schema diagnostics, stable-identity interchange checks, and explicit-observer colorimetry |
| [`spectral_reference_repeat.csv`](spectral_reference_repeat.csv) | [spectral cross-check](../reports/SPECTRAL_CROSSCHECK_2017.md) | Per-patch reflectance RMS and D55/2° ΔE76 for the candidate paired chart series |
| [`ccm_validation_summary.csv`](ccm_validation_summary.csv) | [CCM fit](../reports/CCM_FIT.md) | [CCM validation](../figures/ccm_validation.svg) |
| [`ccsg_f8_flat_wb_patches.csv`](ccsg_f8_flat_wb_patches.csv) | [patch extraction](../reports/PATCH_EXTRACTION.md) | Accepted-flat 140-patch RGB result |
| [`flat_field_summary.csv`](flat_field_summary.csv) | [flat-field response](../reports/FLAT_FIELD_RESPONSE.md) | [CFA flat-field response](../figures/flat_field_response.svg) |
| [`flat_field_response.csv`](flat_field_response.csv) | [flat-field response](../reports/FLAT_FIELD_RESPONSE.md) | [CFA flat-field response](../figures/flat_field_response.svg) |
| [`gamut_synthetic_input.csv`](gamut_synthetic_input.csv) | [gamut mapping](../reports/GAMUT_MAPPING.md) | Deterministic 5 × 5 × 5 encoded Display-P3 stress grid |
| [`gamut_synthetic_radial.csv`](gamut_synthetic_radial.csv) and [`gamut_synthetic_radial.json`](gamut_synthetic_radial.json) | [gamut mapping](../reports/GAMUT_MAPPING.md) | Schema-v3 CIELAB-radial result with typed boundary and IPT evidence |
| [`gamut_synthetic_oklch_radial.csv`](gamut_synthetic_oklch_radial.csv) and [`gamut_synthetic_oklch_radial.json`](gamut_synthetic_oklch_radial.json) | [gamut mapping](../reports/GAMUT_MAPPING.md) | Schema-v3 OkLCh-radial result isolating the coordinate-space change |
| [`gamut_synthetic_css_local_minde.csv`](gamut_synthetic_css_local_minde.csv) and [`gamut_synthetic_css_local_minde.json`](gamut_synthetic_css_local_minde.json) | [gamut mapping](../reports/GAMUT_MAPPING.md) | Schema-v3 dated CSS Local-MINDE result with typed search evidence |
| [`gamut_synthetic_soft.csv`](gamut_synthetic_soft.csv) and [`gamut_synthetic_soft.json`](gamut_synthetic_soft.json) | [gamut mapping](../reports/GAMUT_MAPPING.md) | Schema-v3 soft-knee result with boundary and IPT evidence |
| [`cam16_equation_audit.csv`](cam16_equation_audit.csv) and [`cam16_equation_audit.json`](cam16_equation_audit.json) | [CAM16 equation audit](../reports/CAM16_EQUATION_AUDIT.md) | [normalized brightness, isolated background factor, and fixed-response complete chroma-expression sweep](../figures/cam16_equation_audit.svg) |
| [`cie94_historical_24patch.csv`](cie94_historical_24patch.csv) | [CAM16 equation audit and CIE94 continuity check](../reports/CAM16_EQUATION_AUDIT.md) | Rounded Lab pairs and printed CIE94 values from the Color Pony 24-patch table in the color-management course project report *Color Matching Workflow for Art Reproduction*; supports both directional conventions and the historical variant |

The corrected-patch table retains the 140-row R/G/B extraction order for the
published 1/1000 s result without exposing RAW samples or RawDigger
coordinates. Its canonical-LF SHA-256 is
`4b8429cdacbb982d33ef56a76e09cc46d8c7aadde927e805e52bc5feec2c8f92`.
Reproducing the measurement still requires the private RAWs.

Toolkit SFR values retain eight-decimal measurement precision so derived field
margins do not depend on rounded operands; narrative tables round them to four
decimals. Aggregate figures are deterministic. Their regeneration commands,
numeric tolerances, and freshness checks are documented in the
[implementation companion index](../implementation/README.md#artifact-generation-and-freshness-checks).

These tables make the published results inspectable and plottable without
making the source capture datasets public. Reproducing the measurements from
RAW still requires a configured local dataset root as described in
[dataset handling](../DATASETS.md).
