# Camera IQ documentation and case studies

This index connects concise case studies to detailed method reports, C++ source,
tests, aggregate tables, dataset notes, and provenance records. The
[project overview](../README.md) summarizes the implemented measurement areas
and principal results.

## Featured case studies

### Nikon D800/D810 + 50 mm f/1.4G SFR aperture and field analysis

[Case study](case-studies/sfr-mtf-aperture-field.md) ·
[aggregate data](data/sfr_aperture_summary.csv) ·
[detailed report](reports/SFR_MTF.md) ·
[implementation](../src/sfr.cpp) ·
[tests](../tests/test_sfr.cpp)

The toolkit accepted 299 field ROIs across 13 aperture conditions. It captured a
clear f/5.6 peak on the D810 capture system; the D800 showed a different
aperture trend and asymmetric off-axis behavior. Both sweeps record the same 50 mm
f/1.4G lens model, so the findings describe capture systems, not camera bodies
alone; physical lens-sample identity remains unverified.

### Spectral sensitivity and camera color fidelity

[Case study](case-studies/spectral-color-fidelity.md) ·
[aggregate data](data/spectral_color_fidelity.csv) ·
[detailed report](reports/SPECTRAL_SENSITIVITY.md) ·
[implementation](../src/spectral_response.cpp) ·
[tests](../tests/test_spectral_response.cpp)

Four same-session camera/chart datasets reached minimum channel correlation
above 0.992. A five-camera comparison then evaluated Luther-condition residuals
and ISO 17321-style fidelity metrics while retaining the mixed SSF provenance
needed to interpret the ordering.

### Spectroradiometer archive ingest and measurement-group analysis

[Case study](case-studies/spectroradiometer-ingest.md) ·
[aggregate data](data/spectro_group_summary.csv) ·
[result receipt](data/spectro_result_receipt.json) ·
[MATLAB cross-check receipt](data/spectro_matlab_crosscheck_receipt.json) ·
[detailed report](reports/SPECTRORADIOMETER_INGEST.md) ·
[implementation](../src/spectro_ingest.cpp) ·
[tests](../tests/test_spectro_ingest.cpp)

Across the 37 multi-reading groups, spectral-integral CV was `7.17%` median and
`41.65%` maximum, while normalized-shape and chromaticity variation remained
separate results. The command verifies exact file identities and parses 89
distinct readings without assigning an unsupported physical cause to the
variation. An independent MATLAB R2026a export matched all 89 readings,
including ledger-bound source-file identities and exact hashes for 178 numeric
vectors.

### ColorChecker extraction and CCM validation

[Case study](case-studies/colorchecker-ccm.md) ·
[aggregate data](data/ccm_validation_summary.csv) ·
[CCM report](reports/CCM_FIT.md) ·
[patch report](reports/PATCH_EXTRACTION.md) ·
[implementation](../src/colorimetry.cpp) ·
[tests](../tests/test_colorimetry.cpp)

The 140-patch workflow matched an independent extraction above 0.99999998
correlation with sub-0.4 DN RMSE, rejected a 16.449 px localization error, and
reached 4.134 mean held-out CIEDE2000 on the corrected RAW-to-CCM path.

### CFA flat-field response

[Case study](case-studies/cfa-flat-field-response.md) ·
[aggregate maps](data/flat_field_response.csv) ·
[frame screening](data/flat_field_summary.csv) ·
[detailed report](reports/FLAT_FIELD_RESPONSE.md) ·
[implementation](../src/shading.cpp) ·
[tests](../tests/test_shading.cpp)

The study retained three usable frames from 52 Fujifilm X-T100 and Fujinon XF
14 mm f/2.8 R sphere captures. A 19.65% green-field quadrant asymmetry exceeded
the declared 5% criterion and was inconsistent with a centered radial scalar
model for the measured composite field, while missing source- and
camera-rotation controls prevented lens-only attribution. The
[CCM path](reports/CCM_FIT.md) applies the same source-CFA,
per-position screening to its correction flat.

### Display-P3 to sRGB gamut mapping

[Case study](case-studies/gamut-mapping.md) ·
[CIELAB-radial data](data/gamut_synthetic_radial.csv) ·
[OkLCh-radial data](data/gamut_synthetic_oklch_radial.csv) ·
[CSS Local-MINDE data](data/gamut_synthetic_css_local_minde.csv) ·
[soft-compression data](data/gamut_synthetic_soft.csv) ·
[detailed report](reports/GAMUT_MAPPING.md) ·
[implementation](../src/gamut_mapping.cpp) ·
[tests](../tests/test_gamut_mapping.cpp)

This study uses deterministic synthetic input rather than camera or display
measurements. On a 125-point cube, changing only the radial coordinates from
CIELAB to OkLCh reduced P3-yellow CIEDE2000 from `23.928` to `5.523` and the
grid maximum from `23.928` to `9.956`, while raising the grid mean from `2.857`
to `2.947` and moving the worst point to red. Changing only the OkLCh algorithm
to Local MINDE then reduced the grid mean to `2.323` and the maximum to
`7.602`. The C++ transform compares CIELAB and OkLCh radial
mapping, a dated CSS Color 4 Local-MINDE method, and an experimental soft knee.

### Color-model equation audit

[Case study](case-studies/color-model-equation-audit.md) ·
[data](data/cam16_equation_audit.csv) ·
[figure](figures/cam16_equation_audit.svg) ·
[detailed report](reports/CAM16_EQUATION_AUDIT.md) ·
[implementation](../src/cam16_equation_audit.cpp) ·
[tests](../tests/test_cam16_equation_audit.cpp)

The audit reproduced half normalized brightness at `J = 25` versus half
lightness at `J = 50`. At `Y_background = 0.1`, the isolated `N_cb^0.9` factor
is `2.595`, while the complete chroma expression spans `2.120–2.687` across
reference `J = 10…90`; the isolated term is not a bound. The audit also retains
a published colorfulness `R²` decrease from `0.81` to `0.71`, pins the corrected
2022 Equation 23 coefficient, and makes CIE94 directionality explicit. It is
not presented as a general appearance-model implementation.

## Validation decisions

- [RAW chart localization](reports/RAW_CHART_LOCALIZATION.md) is a retained
  negative result: high RGB correlation did not compensate for a 16.449 px
  coordinate error.
- [OECF Stepchart analysis](reports/OECF_STEPCHART.md) rejects the wrong strip
  geometry, accepts the measured ring layout, and keeps DN-referred variance
  separate from electron-calibrated read-noise or dynamic-range claims.
- [Dark-frame noise](reports/DARK_FRAME_NOISE.md) preserves the one rejected
  dark capture and limits the result to the single clean matched pair.

## Technical reports

### Camera measurement methods

| Report | Status and purpose |
|---|---|
| [Camera IQ coverage map](reports/CAMERA_IQ_COVERAGE.md) | Cross-domain implementation and limitation matrix |
| [RAW CFA statistics](reports/RAW_STATS.md) | Active-area Bayer statistics and maker-specific metadata timing |
| [Bilinear demosaic](reports/BILINEAR_DEMOSAIC.md) | Transparent sensor-DN baseline with synthetic and LibRaw comparisons |
| [Dark calibration](reports/DARK_CALIBRATION.md) | Metadata-black reconciliation against 21 dark candidates |
| [Dark-frame noise](reports/DARK_FRAME_NOISE.md) | Temporal noise, DSNU, and dark-current diagnostics in DN |
| [Relative CFA flat-field response](reports/FLAT_FIELD_RESPONSE.md) | Per-CFA spatial response, chromatic ratios, near-ceiling screening, bounded dark-control checks, one capture-pair delta, and asymmetry |

### Color and chart analysis

| Report | Status and purpose |
|---|---|
| [ColorChecker-SG reference provenance](reports/SG_REFERENCE_PROVENANCE.md) | Compatible reference identity, layout, and manufacturer comparison |
| [RAW patch extraction](reports/PATCH_EXTRACTION.md) | 140-patch extraction, flat-field/WB policy, and reference-tool comparison |
| [RAW chart localization](reports/RAW_CHART_LOCALIZATION.md) | Retained negative result and model-comparison diagnostics |
| [CCM fit](reports/CCM_FIT.md) | Linear CCM, held-out Delta E, and dark-patch policy |
| [Display-P3 to sRGB gamut mapping](reports/GAMUT_MAPPING.md) | Typed RGB/XYZ/Lab/OkLab transforms, analytic radial boundaries, dated Local MINDE, and an experimental soft knee |
| [CAM16 equation audit and CIE94 continuity check](reports/CAM16_EQUATION_AUDIT.md) | Bounded equation behavior, corrected coefficient, published tradeoff, and explicit CIE94 conventions |

### Spectral characterization

| Report | Status and purpose |
|---|---|
| [Spectral sensitivity](reports/SPECTRAL_SENSITIVITY.md) | RAW extraction, physical closure, Luther residuals, and SMI-style analysis |
| [Spectral archive inventory](reports/SPECTRAL_ARCHIVE_INVENTORY.md) | Camera/session/file-role map and measurement hazards |
| [Spectroradiometer ingest](reports/SPECTRORADIOMETER_INGEST.md) | Exact-byte MAT ingestion, measurement-group analysis, chromaticity, and same-record XYZ closure |

### Exposure, tone, and noise

| Report | Status and purpose |
|---|---|
| [Exposure response](reports/EXPOSURE_RESPONSE.md) | Series grouping and readiness gates |
| [Relative OECF fit](reports/OECF_FIT.md) | Sensor-DN linearity over a controlled relative-exposure span |
| [OECF Stepchart](reports/OECF_STEPCHART.md) | Oracle parsing, ring-zone RAW extraction, and temporal-variance fits |

### Sharpness and field behavior

| Report | Status and purpose |
|---|---|
| [SFR/MTF result](reports/SFR_MTF.md) | D800/D810 center, aperture, and field analysis |
| [SFR/MTF archive inventory](reports/SFR_MTF_ARCHIVE_INVENTORY.md) | Input/oracle batch selection and field-label traps |

### Dataset and provenance references

| Report | Status and purpose |
|---|---|
| [Fujifilm X-T100 ColorChecker-SG manifest](reports/FUJI_XT100_CCSG_MANIFEST.md) | Dataset enumeration, CFA/black verification, and caveats |
| [Dataset handling](DATASETS.md) | Public/private data boundary and local configuration |
| [Aggregate result tables](data/README.md) | Figure inputs and regeneration |

## Reproducibility and data access

Source captures and measured references remain outside Git. Public inspection is
supported by C++ source, tests, safe aggregate CSVs, deterministic figures, and
method reports. The aggregate figures can be rebuilt with:

```bash
python3 tools/generate_portfolio_figures.py
python3 tools/generate_portfolio_figures.py --check
python3 tools/generate_gamut_portfolio.py --camera-iq build/camera_iq --check
python3 tools/generate_cam16_equation_audit.py \
  --camera-iq build/camera_iq --check
```

Selected method pages also include
[reduced illustrative crops](images/README.md) from the source test captures.
They are metadata-stripped visual guides, not calibration references or
analysis inputs.
