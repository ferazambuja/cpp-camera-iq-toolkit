# Camera IQ Technical Coverage Map

This is the navigation map for the measurement work, not a standalone camera
test. It shows which image-quality questions the toolkit can currently answer,
where the supporting reports and data live, and which conclusions remain
blocked by missing captures or calibration evidence.

Evidence basis: the scientific reports, aggregate tables, and archive
inventories in this repository.

[Documentation index](../README.md)

## Technical scope

The toolkit implements a broad set of objective still-image IQ analyses
supported by the available image archives:

- RAW front-end and CFA statistics.
- Patch extraction, chart reference provenance, CCM fitting, and Delta E color
  accuracy.
- Ideal Display-P3/sRGB conversion, CIELAB and OkLCh destination-boundary
  analysis, dated Local MINDE, and an experimental protected-core method.
- Directional CIE94 plus a separately named historical convention, and a
  bounded CIECAM02/CAM16 equation audit.
- Camera spectral sensitivity, physical closure, Luther/SMI color-fidelity
  ranking, and archive provenance.
- Tone/OECF/linearity from CLRS exposure series and Nikon D800 Stepchart oracle
  data.
- Dark-frame noise, DSNU, and DN-referred per-pixel temporal variance
  diagnostics.
- Slanted-edge SFR/MTF, including center ROI and 23-ROI field maps on two Nikon
  archives.
- Relative CFA flat-field response with central near-ceiling screening,
  bounded dark-control, one capture-pair-difference, chromatic-ratio, and
  corner-field-asymmetry diagnostics.

The current archives do not provide the calibration or target captures needed
for electron-calibrated gain/read noise, full well, engineering dynamic range,
PRNU, exact ISO standard conformance, or dedicated vignetting, distortion,
chromatic-aberration, and flare measurements. Automatic chart localization is
also incomplete for some target types.

The [implementation companion index](../implementation/README.md) maps these
measurement areas to the command layer, C++ data flow, source, and tests without
turning this scientific coverage map into a software inventory.

## Coverage matrix

| IQ dimension | Scientific status | Reports | Supported method or result | Limitations |
|---|---|---|---|---|
| RAW file inventory and metadata | Covered | [Fuji manifest](FUJI_XT100_CCSG_MANIFEST.md), [spectral report](SPECTRAL_SENSITIVITY.md) | Dataset scans, filename/EXIF checks, candidate exposure series, private-data labeling. | `manifest` is metadata/open-file oriented; maker black and pitch are authoritative only after unpack where needed. |
| RAW CFA statistics | Covered | [RAW stats](RAW_STATS.md) | Black-subtracted per-CFA-position statistics over full frames or regions, with controlled cross-maker comparisons. | Not a full ISP or rendered-image analysis. |
| Demosaic | Covered as transparent baseline | [Bilinear demosaic](BILINEAR_DEMOSAIC.md) | Hand-written bilinear demosaic with synthetic and real validation. | Not bit-exact LibRaw parity or production demosaic quality. |
| ColorChecker-SG reference provenance | Covered | [SG provenance](SG_REFERENCE_PROVENANCE.md) | Spectral reference inventory, X-Rite verification, orientation/layout checks. | Not a measured per-unit CLRS-589 SG reference. |
| RAW patch extraction | Covered | [Patch extraction](PATCH_EXTRACTION.md) | RawDigger coordinate extraction, flat-field/WB correction, CSV handoff to CCM, orientation checks. | RawDigger-independent replacement remains constrained by localization diagnostics. |
| RAW chart localization | Partial but bounded | [Localization](RAW_CHART_LOCALIZATION.md) | Corner-seeded projective grid, held-out residual diagnostics, and dual-seeded detector arbitration. | Final RawDigger replacement stayed unresolved for the centered capture; detector was too unstable to arbitrate. |
| Color accuracy / CCM / Delta E | Covered | [CCM fit](CCM_FIT.md), [patch extraction](PATCH_EXTRACTION.md), [equation audit](CAM16_EQUATION_AUDIT.md) | Linear RGB-to-XYZ CCM, held-out diagnostics, dark-patch exclusion experiments, Delta E 76/2000, directional CIE94, and a separately named geometric-mean-chroma historical variant. | Root-polynomial or more flexible models deferred until held-out improvement is proven; the historical CIE94 tool and full-precision inputs were not retained. |
| RGB encoding-gamut mapping | Covered for ideal sRGB and Display-P3 encodings | [Gamut mapping](GAMUT_MAPPING.md) | D65 transforms, analytic CIELAB/OkLCh boundaries, radial clipping, dated CSS Local-MINDE, and experimental protected-core compression. | Not a measured display/printer gamut, spatial image-rendering study, or observer validation; the CSS draft is work in progress; arbitrary ICC profiles remain outside this method. |
| CAM16 equation behavior | Covered as a bounded numerical audit | [CAM16 equation audit](CAM16_EQUATION_AUDIT.md) | Normalized brightness, isolated and coupled background factors, corrected Hellwig/Fairchild Equation 23 coefficient, and the published performance tradeoff. | Not a general CAM16 forward transform, complete Hellwig implementation, CIE 248:2022 conformance test, appearance prediction, or observer validation. |
| Spectral sensitivity extraction | Covered deeply | [Spectral report](SPECTRAL_SENSITIVITY.md), [archive map](SPECTRAL_ARCHIVE_INVENTORY.md) | RAW-derived recovery from monochromator sweeps, legacy-fidelity comparison, five-camera SSF inventory. | Legacy CSVs are fidelity checks, not correctness oracles. |
| Spectral physical closure | Covered | [Spectral report](SPECTRAL_SENSITIVITY.md), [archive map](SPECTRAL_ARCHIVE_INVENTORY.md) | SG-140 and CC-24 physical closure for Canon/Nikon/Sony 2016 cameras using measured illuminant and reflectance. | Phase One IQ3 has SSF but no same-session broadband closure target. |
| Spectral color-fidelity ranking | Covered | [Spectral report](SPECTRAL_SENSITIVITY.md) | Luther residuals and ISO-style SMI over SG-140, CC-24, and CC-18; D55 primary; white-preserving sensitivity bound. | Not claimed bit-exact to ISO 17321 Annex B. |
| Spectroradiometer archive ingest | Covered as record characterization | [Spectroradiometer ingest](SPECTRORADIOMETER_INGEST.md), [SG provenance](SG_REFERENCE_PROVENANCE.md) | Content-identity binding, bounded MATLAB v5 recovery, 40 measurement groups, absolute/normalized spectra, recorded-XYZ chromaticity, and same-record closure. | Source files do not record enough physical controls to assign within-group variation to source, geometry, settings, or instrument repeatability. |
| Exposure response readiness | Covered | [Exposure response](EXPOSURE_RESPONSE.md) | Exposure-series grouping and black-subtracted CFA response summaries. | Readiness/response summary, not final ISO OECF/PTC. |
| Relative OECF / linearity | Covered | [OECF fit](OECF_FIT.md) | Relative-exposure linearity over usable OECF points. | Assumes constant illumination; not ISO 14524. |
| Stepchart OECF oracle | Covered | [OECF Stepchart](OECF_STEPCHART.md) | Primary Imatest response tables, archive joins, run-window gates, D800 advisory summaries, and cross-ISO luma spread. | Rendered-luma advisory path; no chart-density traceability or measured ISO speed. |
| Stepchart raw-DN ring extraction | Covered with explicit geometry seed | [OECF Stepchart](OECF_STEPCHART.md) | D800 ISO 14524-style ring seed, 20 zone ROIs, oracle-ladder gate, raw-CFA DN summaries. | Manual seed, not automatic detection; strip model correctly refuses this archive. |
| DN-referred PTC-style variance | Covered as diagnostic | [OECF Stepchart](OECF_STEPCHART.md) | Aligned per-pixel temporal variance over 10 repeats, variance-vs-mean fits per ISO/CFA plane, saturated/deep-tail exclusions. | DN-domain diagnostic only; no electron gain/read noise, full well, PRNU, or engineering DR. |
| Dark-frame temporal noise and DSNU | Covered | [Noise](DARK_FRAME_NOISE.md), [dark calibration](DARK_CALIBRATION.md) | Dark-pair temporal noise, moment/robust DSNU, dark-current diagnostic, outlier gating. | DN diagnostics; gain/PTC/DR refused where data does not support them. |
| SFR / MTF center ROI | Covered | [SFR result](SFR_MTF.md), [archive map](SFR_MTF_ARCHIVE_INVENTORY.md) | Green-linear slanted-edge MTF50P, sinc correction, D810 aperture trend, Imatest advisory comparison. | Not luma/gamma Imatest parity, lp/mm, or rendered Y-channel equivalence. |
| SFR / MTF field map | Covered | [SFR result](SFR_MTF.md), [archive map](SFR_MTF_ARCHIVE_INVENTORY.md) | 23-region field maps for D810 and D800, per-region advisory comparisons, field/corner gates, and the D800 negative trend finding. | Still green-linear CFA SFR; no full sagittal/tangential lens model. |
| CFA flat-field response | Covered as composite characterization; optical attribution remains partial | [Flat-field response](FLAT_FIELD_RESPONSE.md), [patch extraction](PATCH_EXTRACTION.md) | Per-CFA median maps, center-normalized R/G and B/G, center/full-frame near-ceiling gates, bounded dark-control checks, one observed capture-pair delta, and corner-field asymmetry. | Available captures do not separate source, lens, alignment, mechanical shading, or sensor angular response; the single pair is not a repeatability estimate; not an isolated lens-vignetting metric. |
| Vignetting/shading (optical attribution) | Partial | [Flat-field response](FLAT_FIELD_RESPONSE.md), [patch extraction](PATCH_EXTRACTION.md) | The composite field response above, plus flat-field correction inside patch extraction. | No isolated lens-vignetting metric. Attribution needs camera-rotation pairs and multi-aperture headroom-safe captures, neither of which the archive provides. |
| Distortion / chromatic aberration / flare | Partial / diagnostic only | [Localization](RAW_CHART_LOCALIZATION.md), [CCM fit](CCM_FIT.md) | Localization residuals and dark-patch flare evidence. | No standalone distortion, lateral CA, flare, or veiling-glare metric. |
| Texture, autofocus, rolling shutter, HDR/video | Not covered | none | Out of current still-image archive scope. | Would need new target captures or different data. |

## Dataset coverage

| Dataset family | Covered outputs |
|---|---|
| CLRS-589 Project Camera | Manifest, RAW stats, demosaic, dark calibration, dark-frame noise/DSNU, exposure-response readiness, OECF fit, SG reference handling, patch extraction, CCM fit, raw chart localization diagnostics, and relative CFA flat-field response. |
| 2016 monochromator / ColorChecker target sessions | Canon/Nikon/Sony SSF extraction, physical closure, SMI/Luther ranking, CC-24 and SG-140 target-set evidence. |
| 2017 camSPECS / Phase One IQ3 | IQ3 SSF and color-fidelity ranking; closure blocked by missing same-session target/reflectance. |
| 2016 esensi D810/D800 SFR | Center and field SFR/MTF, aperture trend gates, Imatest `_Y_multi.csv` oracle comparisons. |
| 2016 D800 OECF Stepchart | Imatest oracle parsing, raw ring-zone extraction, DN-referred per-pixel temporal variance diagnostics. |

## Scope and limitations

The toolkit implements a C++ still-camera IQ analysis pipeline over archived
RAW datasets: RAW/CFA statistics, color chart extraction and CCM/Delta E,
spectral sensitivity with physical closure and SMI-style ranking, OECF and
Stepchart analysis, dark-frame noise/DSNU, DN-referred temporal-variance
diagnostics, and slanted-edge SFR/MTF center/field maps. Large source datasets
stay outside Git; the repository provides method reports, aggregate results,
and public implementation companions. The toolkit is a technical
implementation and validation project, not a certified ISO laboratory suite.

## Known gaps

1. **Calibration-backed electron PTC/DR** requires calibrated system gain,
   electron-referred read noise, full-well evidence, and a defined engineering
   dynamic-range threshold. The current Stepchart fits remain DN-referred.
2. **Optical vignetting decomposition and CA/distortion metrics** require
   additional target captures. The implemented CFA result remains a
   capture-system field response because sphere mapping, camera rotation, and
   multi-aperture controls are insufficient for component attribution.
3. **Automatic Stepchart and SG localization** is incomplete. The validated
   workflows use measured ring geometry or supplied chart coordinates.
4. **Spectral repeatability and instrument comparison** would require a
   PR-655/i1Pro illuminant comparison and analysis of the repeated
   monochromator sessions.
5. **Rendered-luma Imatest parity** is not implemented. Current SFR results use
   sensor-linear green measurements and treat Imatest values as advisory.

## Current coverage

The available archives support the major objective still-camera IQ families
listed in the matrix. Color and spectral analysis has the deepest coverage;
OECF, noise, and MTF are implemented within the stated measurement limits.
The remaining gaps require additional calibration evidence or new
target-specific captures rather than additional file parsing alone.

## Engineering companion

The [documentation index](../README.md) routes each scientific result to its
report, aggregate data, and implementation companion. The
[implementation index](../implementation/README.md) provides the cross-domain
software map.
