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

Sharpness typically changes with aperture: residual aberrations often dominate
wide open, while diffraction becomes important when the lens is stopped down.
Reviews usually summarize that balance with one measurement at the center of
the frame. This study asks whether one center number describes a camera and lens
at all. Across 299 regions on two systems using the same 50 mm lens model, the
D810 system peaked cleanly at f/5.6, while the D800 system followed a different
trend and was sharper off-center at some apertures. The two systems needed
separate conclusions.

### Spectral sensitivity and camera color fidelity

[Case study](case-studies/spectral-color-fidelity.md) ·
[aggregate data](data/spectral_color_fidelity.csv) ·
[detailed report](reports/SPECTRAL_SENSITIVITY.md) ·
[implementation](../src/spectral_response.cpp) ·
[tests](../tests/test_spectral_response.cpp)

How faithfully a camera can reproduce color is constrained before any
processing, by how its red, green, and blue channels respond to each wavelength
of light. Measuring those response curves with a monochromator makes that limit
visible: the closer a sensor comes to a linear transform of the human observer's
sensitivities, the better its theoretical colorimetric fit. Five cameras were
compared this way. Where the archive retained enough evidence, the response
curves were first checked against separate chart captures from the same lab run.
The Canon 5D2 and Phase One IQ3 formed the stable endpoints of the comparison;
small differences among the middle cameras were treated as method-sensitive.

### Recovering and analyzing archived spectroradiometer measurements

[Case study](case-studies/spectroradiometer-ingest.md) ·
[aggregate data](data/spectro_group_summary.csv) ·
[detailed report](reports/SPECTRORADIOMETER_INGEST.md) ·
[implementation](../src/spectro_ingest.cpp) ·
[tests](../tests/test_spectro_ingest.cpp)

Repeat measurements of the same light source rarely agree exactly, and the
interesting question is what changed: the amount of light, the shape of its
spectrum, or its color. This study recovers 89 archived spectroradiometer
readings — stored under filenames that numbered acquisitions rather than
describing what was measured — and reports those three quantities separately for
each group of repeats. They disagree by different amounts in different groups,
which is why collapsing them into a single stability figure would be
misleading. Typical within-group level variation was 7.17%; the most variable
group reached 41.65%, without establishing why the source or measurement changed.

### ColorChecker extraction and CCM validation

[Case study](case-studies/colorchecker-ccm.md) ·
[aggregate data](data/ccm_validation_summary.csv) ·
[CCM report](reports/CCM_FIT.md) ·
[patch report](reports/PATCH_EXTRACTION.md) ·
[implementation](../src/colorimetry.cpp) ·
[tests](../tests/test_colorimetry.cpp)

A camera's raw values are not colorimetry: each sensor responds to light
differently from the human eye, so its RGB values must be transformed before
they can estimate standard color coordinates. This study builds that transform
— a color-correction matrix — from a photographed 140-patch chart, then grades
it on patches deliberately withheld from the fit so training error cannot
masquerade as generalization. A chart-locating shortcut that looked accurate by
correlation was rejected after it missed patch centers by 16 px.
The corrected workflow reached a five-fold held-out mean CIEDE2000 of 4.134
against a compatible spectral chart reference.

### CFA flat-field response

[Case study](case-studies/cfa-flat-field-response.md) ·
[aggregate maps](data/flat_field_response.csv) ·
[frame screening](data/flat_field_summary.csv) ·
[detailed report](reports/FLAT_FIELD_RESPONSE.md) ·
[implementation](../src/shading.cpp) ·
[tests](../tests/test_shading.cpp)

Even under nominally uniform illumination, a camera can record less signal at
the edges than at the center, with different falloff in each color channel.
Correcting that field often starts from the assumption that it is symmetric
about the image center. In this integrating-sphere capture set, it was not:
opposite quadrants differed by 19.65% where the project allowed 5%, so a simple
centered radial model does not describe the measured field. The missing rotation
controls prevent assigning the asymmetry to the source, lens, or camera alone.

### Display-P3 to sRGB gamut mapping

[Case study](case-studies/gamut-mapping.md) ·
[CIELAB-radial data](data/gamut_synthetic_radial.csv) ·
[OkLCh-radial data](data/gamut_synthetic_oklch_radial.csv) ·
[CSS Local-MINDE data](data/gamut_synthetic_css_local_minde.csv) ·
[soft-compression data](data/gamut_synthetic_soft.csv) ·
[detailed report](reports/GAMUT_MAPPING.md) ·
[implementation](../src/gamut_mapping.cpp) ·
[tests](../tests/test_gamut_mapping.cpp)

A wide-gamut image holds colors an ordinary sRGB screen cannot show, and
something has to decide where those colors land instead — often an ICC profile
or rendering engine whose mapping is not exposed to the user. This study makes
that decision inspectable in four different ways and changes one variable at a
time to find out what each choice
costs. Switching the coordinate space alone rescued a badly overcompressed
saturated yellow, cutting its error from 23.9 to 5.5, while very slightly
worsening the average across the rest — a targeted trade, not a free
improvement.

### Color-model equation audit

[Case study](case-studies/color-model-equation-audit.md) ·
[data](data/cam16_equation_audit.csv) ·
[figure](figures/cam16_equation_audit.svg) ·
[detailed report](reports/CAM16_EQUATION_AUDIT.md) ·
[implementation](../src/cam16_equation_audit.cpp) ·
[tests](../tests/test_cam16_equation_audit.cpp)

Color appearance models predict how a color will look rather than only what its
colorimetric coordinates measure. Implementing one means translating equations
from published work, including any later corrections and known tradeoffs. Those
equations can carry surprises:
coefficients corrected after publication, terms that behave strangely at the
edges of their range, and tradeoffs that summaries quietly drop. This study
turns a small set of those equations into tested code to see how they actually
behave — including a published change that improved two attributes while making
a third measurably worse. The audit also shows why an isolated background term
cannot be read as a bound on the complete chroma expression.

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

These are the deeper method, result, and measurement-identity appendices behind
the featured studies. Each opens with the scientific purpose and the conclusion
it supports; later sections retain equations, input selection, and reproduction
details for readers who need to audit the work.

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
