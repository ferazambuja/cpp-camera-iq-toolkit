# Camera IQ documentation and case studies

This index connects concise case studies to detailed method reports, C++ source,
tests, aggregate tables, dataset notes, and provenance records. The
[project overview](../README.md) summarizes the implemented measurement areas
and principal results.

## Featured case studies

### D800/D810 + 50 mm f/1.4G SFR aperture and field analysis

[Case study](case-studies/sfr-mtf-aperture-field.md) ·
[aggregate data](data/sfr_aperture_summary.csv) ·
[detailed report](reports/SFR_MTF.md) ·
[implementation](../src/sfr.cpp) ·
[tests](../tests/test_sfr.cpp)

The toolkit accepted 299 field ROIs across 13 aperture conditions. It captured a
clear f/5.6 peak on the D810 capture system while retaining the D800 system's
non-transfer result and off-axis behavior. Both sweeps record the same 50 mm
f/1.4G lens model, so the findings describe capture systems, not camera bodies
alone; physical lens-sample identity remains unverified.

### Spectral sensitivity and camera color fidelity

[Case study](case-studies/spectral-color-fidelity.md) ·
[aggregate data](data/spectral_color_fidelity.csv) ·
[detailed report](reports/SPECTRAL_SENSITIVITY.md) ·
[implementation](../src/spectral_response.cpp) ·
[tests](../tests/test_spectral_response.cpp)

The study connects RAW monochromator extraction to same-session physical
closure and a five-camera color-fidelity comparison, with mixed SSF provenance
shown directly beside the result.

### ColorChecker extraction and CCM validation

[Case study](case-studies/colorchecker-ccm.md) ·
[aggregate data](data/ccm_validation_summary.csv) ·
[CCM report](reports/CCM_FIT.md) ·
[patch report](reports/PATCH_EXTRACTION.md) ·
[implementation](../src/colorimetry.cpp) ·
[tests](../tests/test_colorimetry.cpp)

The pipeline moves from RAW patch extraction through flat-field/WB handling to
a linear RGB-to-XYZ fit and deterministic held-out Delta E diagnostics.

### CFA flat-field response

[Case study](case-studies/cfa-flat-field-response.md) ·
[aggregate maps](data/flat_field_response.csv) ·
[frame screening](data/flat_field_summary.csv) ·
[detailed report](reports/FLAT_FIELD_RESPONSE.md) ·
[implementation](../src/shading.cpp) ·
[tests](../tests/test_shading.cpp)

The analyzer separates four Bayer positions, rejects a clipped normalizing
center, verifies the pedestal with paired dark metadata, and reports
source–lens–sensor intensity and chromatic fields with repeatability and
quadrant-asymmetry diagnostics.

## Validation decisions

- [RAW chart localization](reports/RAW_CHART_LOCALIZATION.md) correctly remains
  a **FAIL**: high RGB correlation did not override a 16.449 px coordinate
  error.
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
| [Relative CFA flat-field response](reports/FLAT_FIELD_RESPONSE.md) | Per-CFA spatial response, chromatic ratios, clipping, pedestal, repeatability, and asymmetry |

### Color and chart analysis

| Report | Status and purpose |
|---|---|
| [ColorChecker-SG reference provenance](reports/SG_REFERENCE_PROVENANCE.md) | Compatible reference identity, layout, and manufacturer comparison |
| [RAW patch extraction](reports/PATCH_EXTRACTION.md) | 140-patch extraction, flat-field/WB policy, and reference-tool comparison |
| [RAW chart localization](reports/RAW_CHART_LOCALIZATION.md) | Retained negative result and model-comparison diagnostics |
| [CCM fit](reports/CCM_FIT.md) | Linear CCM, held-out Delta E, and dark-patch policy |

### Spectral characterization

| Report | Status and purpose |
|---|---|
| [Spectral sensitivity](reports/SPECTRAL_SENSITIVITY.md) | RAW extraction, physical closure, Luther residuals, and SMI-style analysis |
| [Spectral archive inventory](reports/SPECTRAL_ARCHIVE_INVENTORY.md) | Camera/session/file-role map and measurement hazards |

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
| [Publication-safe aggregate tables](data/README.md) | Figure inputs and regeneration |

## Reproducibility and data access

Source captures and measured references remain outside Git. Public inspection is
supported by C++ source, tests, safe aggregate CSVs, deterministic figures, and
method reports. The aggregate figures can be rebuilt with:

```bash
python3 tools/generate_portfolio_figures.py
python3 tools/generate_portfolio_figures.py --check
```

Selected method pages also include
[reduced illustrative crops](images/README.md) from the source test captures.
They are metadata-stripped visual guides, not calibration references or
analysis inputs.
