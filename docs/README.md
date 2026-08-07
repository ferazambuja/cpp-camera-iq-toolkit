# Camera IQ documentation and case studies

This index connects concise case studies to scientific reports, implementation
companions, aggregate tables, dataset notes, and provenance records. The
[project overview](../README.md) summarizes the implemented measurement areas
and principal results. The
[public documentation standard](PUBLIC_DOCUMENTATION_STANDARD.md) defines how
those layers stay readable and technically complete without mixing scientific
narrative with source-code detail.

**Jump to:** [featured case studies](#featured-case-studies) ·
[implementation architecture](#implementation-architecture) ·
[validation decisions](#validation-decisions) ·
[technical reports](#technical-reports) ·
[reproducibility and data access](#reproducibility-and-data-access)

Looking for a specific report rather than a study? The
[technical reports](#technical-reports) section indexes them by subject.

## Featured case studies

### Nikon D800/D810 + 50 mm f/1.4G SFR aperture and field analysis

[Case study](case-studies/sfr-mtf-aperture-field.md) ·
[aggregate data](data/sfr_aperture_summary.csv) ·
[detailed report](reports/SFR_MTF.md) ·
[implementation companion](implementation/sfr-mtf.md)

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
[implementation companion](implementation/spectral-fidelity.md)

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
[implementation companion](implementation/spectroradiometer.md)

Repeat measurements of the same light source rarely agree exactly, and the
interesting question is what changed: the amount of light, the shape of its
spectrum, or its color. This study recovers 89 archived spectroradiometer
readings — stored under filenames that numbered acquisitions rather than
describing what was measured — and reports those three quantities separately for
each group of repeats. They disagree by different amounts in different groups,
which is why collapsing them into a single stability figure would be
misleading. Typical within-group level variation was 7.17%; the most variable
group reached 41.65%, without establishing why the source or measurement changed.

### Spectral measurement and reference-data cross-check

[Case study](case-studies/spectral-archive-crosscheck.md) ·
[HID comparison data](data/hid_spectral_comparison.json) ·
[reference audit data](data/spectral_reference_audit.json) ·
[detailed report](reports/SPECTRAL_CROSSCHECK_2017.md) ·
[implementation companion](implementation/spectral-crosscheck.md)

Comparing spectra from different instruments requires more than placing two
curves on one graph. This study separates native-grid repeat variation from a
shared-grid comparison, localizes the wavelengths driving the difference, and
audits the colorimetric metadata embedded in ColorChecker exports. The HID
series differ by 4.327% relative L2 while their within-series shape residuals
stay below 0.307%; 530 and 540 nm carry 75.9% of the squared discrepancy. A
separate observer check reproduces the retained SpectraShop Lab values at
0.0119 mean ΔE76 under D65/10°, versus 3.909 under the conflicting 2° reading.

### ColorChecker extraction and CCM validation

[Case study](case-studies/colorchecker-ccm.md) ·
[aggregate data](data/ccm_validation_summary.csv) ·
[CCM report](reports/CCM_FIT.md) ·
[patch report](reports/PATCH_EXTRACTION.md) ·
[implementation companion](implementation/color-characterization.md)

A camera's raw values are not colorimetry: each sensor responds to light
differently from the human eye, so its RGB values must be transformed before
they can estimate standard color coordinates. This study builds that transform
— a color-correction matrix — from a photographed 140-patch chart, then grades
it on patches deliberately withheld from the fit so training error cannot
masquerade as generalization. A chart-locating shortcut that looked accurate by
correlation was rejected after it missed patch centers by 16.449 px.
The corrected workflow reached a five-fold held-out mean CIEDE2000 of 4.134
against a compatible spectral chart reference.

### CFA flat-field response

[Case study](case-studies/cfa-flat-field-response.md) ·
[aggregate maps](data/flat_field_response.csv) ·
[frame screening](data/flat_field_summary.csv) ·
[detailed report](reports/FLAT_FIELD_RESPONSE.md) ·
[implementation companion](implementation/flat-field.md)

Even under nominally uniform illumination, a camera can record less signal at
the edges than at the center, with different falloff in each color channel.
Correcting that field often starts from the assumption that it is symmetric
about the image center. In this integrating-sphere capture set, it was not:
the four corners spread by 19.65% of their own average where the project
allowed 5%, so a simple
centered radial model does not describe the measured field. The missing rotation
controls prevent assigning the asymmetry to the source, lens, or camera alone.

### Display-P3 to sRGB gamut mapping

[Case study](case-studies/gamut-mapping.md) ·
[CIELAB-radial data](data/gamut_synthetic_radial.csv) ·
[OkLCh-radial data](data/gamut_synthetic_oklch_radial.csv) ·
[CSS Local-MINDE data](data/gamut_synthetic_css_local_minde.csv) ·
[soft-compression data](data/gamut_synthetic_soft.csv) ·
[detailed report](reports/GAMUT_MAPPING.md) ·
[implementation companion](implementation/gamut-mapping.md)

A wide-gamut image holds colors an ordinary sRGB screen cannot show, and
something has to decide where those colors land instead — often an ICC profile
or rendering engine whose mapping is not exposed to the user. This study makes
that decision inspectable in four different ways and changes one variable at a
time to find out what each choice
costs. Switching the coordinate space alone rescued a badly overcompressed
saturated yellow, cutting its error from 23.928 to 5.523, while very slightly
worsening the average across the rest — a targeted trade, not a free
improvement.

### Color-model equation audit

[Case study](case-studies/color-model-equation-audit.md) ·
[data](data/cam16_equation_audit.csv) ·
[figure](figures/cam16_equation_audit.svg) ·
[detailed report](reports/CAM16_EQUATION_AUDIT.md) ·
[implementation companion](implementation/color-model-audit.md)

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

## Implementation architecture

The [implementation companion index](implementation/README.md) explains how
the scientific methods map to C++ types, algorithms, numerical conventions,
data flow, serializers, and tests. These pages begin with the complete pipeline
and formula-to-code mapping before linking to individual source files. The
scientific reports remain the canonical source for equations, measurement
conditions, results, and limitations.

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
it supports; later sections retain equations, input selection, result
conditions, and interpretation for readers who need to audit the work. The
separate implementation companions carry software operation and reproduction
details.

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
| [Spectroradiometer ingest](reports/SPECTRORADIOMETER_INGEST.md) | Content-bound measurement recovery, group analysis, chromaticity, and same-record XYZ closure |
| [Spectral measurement and reference-data cross-check](reports/SPECTRAL_CROSSCHECK_2017.md) | Repeated HID series, cross-grid residual localization, CGATS interchange, and observer selection |

### Exposure, tone, and noise

| Report | Status and purpose |
|---|---|
| [Exposure response](reports/EXPOSURE_RESPONSE.md) | Series grouping and readiness gates |
| [Relative OECF fit](reports/OECF_FIT.md) | Sensor-DN linearity over a controlled relative-exposure span |
| [OECF Stepchart](reports/OECF_STEPCHART.md) | Advisory response-table comparison, ring-zone RAW measurement, and temporal-variance fits |

### Sharpness and field behavior

| Report | Status and purpose |
|---|---|
| [SFR/MTF result](reports/SFR_MTF.md) | D800/D810 center, aperture, and field analysis |
| [SFR/MTF archive inventory](reports/SFR_MTF_ARCHIVE_INVENTORY.md) | Input/advisory batch selection, field identity, and metadata limits |

### Dataset and provenance references

| Report | Status and purpose |
|---|---|
| [Fujifilm X-T100 ColorChecker-SG manifest](reports/FUJI_XT100_CCSG_MANIFEST.md) | Dataset enumeration, CFA/black verification, and caveats |
| [Dataset handling](DATASETS.md) | Public/private data boundary and local configuration |
| [Aggregate result tables](data/README.md) | Figure inputs and regeneration |

## Reproducibility and data access

Bulk source captures and some measured references remain outside Git. Public
inspection is supported by aggregate CSVs, deterministic figures, method
reports, and the implementation. The compact spectral coursework tables used
by the cross-check are committed with source hashes and regenerate completely
in CI. Build and artifact-regeneration instructions are kept in the
[implementation companion index](implementation/README.md#artifact-generation-and-freshness-checks)
so this scientific index remains focused on the studies.

Selected method pages also include
[reduced illustrative crops](images/README.md) from the source test captures.
They are metadata-stripped visual guides, not calibration references or
analysis inputs.
