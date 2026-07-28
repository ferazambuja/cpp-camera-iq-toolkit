# Camera IQ Technical Coverage Map

Portfolio audit: 2026-07-28<br>
Source basis: the public implementation, tests, reports, aggregate tables, and
archive inventories in this repository.

[Portfolio and report index](../README.md)

## Executive Verdict

The toolkit implements a broad set of objective still-image IQ analyses
supported by the available image archives:

- RAW front-end and CFA statistics.
- Patch extraction, chart reference provenance, CCM fitting, and Delta E color
  accuracy.
- Camera spectral sensitivity, physical closure, Luther/SMI color-fidelity
  ranking, and archive provenance.
- Tone/OECF/linearity from CLRS exposure series and D800 Stepchart oracle data.
- Dark-frame noise, DSNU, and DN-referred per-pixel temporal variance
  diagnostics.
- Slanted-edge SFR/MTF, including center ROI and 23-ROI field maps on two Nikon
  archives.

The remaining gaps are mostly outside the current archive evidence rather than
unimplemented parser loops: electron-calibrated gain/read noise, full well,
engineering dynamic range, PRNU, exact ISO standard conformance, automatic
chart localization for every target type, and dedicated vignetting/distortion/
chromatic-aberration/flare metrics.

## Command Surface

Current CLI verbs:

| Area | Commands |
|---|---|
| Dataset and RAW front-end | `manifest`, `raw-stats`, `demosaic` |
| Dark/noise/tone | `dark-calibration`, `noise`, `exposure-response`, `oecf-fit`, `oecf-stepchart` |
| Color chart workflow | `reference-info`, `patches`, `ccm-fit` |
| Spectral workflow | `spectral-response`, `spectral-closure`, `spectral-quality`, `spectral-smi` |
| Sharpness | `sfr` |

The implementation is accompanied by CTest coverage and repository privacy
checks; the large RAW data and generated outputs stay in private or ignored
locations.

## Coverage Matrix

| IQ dimension | Status | Evidence reports | What is supported | Main boundaries |
|---|---|---|---|---|
| RAW file inventory and metadata | Covered | [Fuji manifest](FUJI_XT100_CCSG_MANIFEST.md), [spectral report](SPECTRAL_SENSITIVITY.md) | Dataset scans, filename/EXIF checks, candidate exposure series, private-data labeling. | `manifest` is metadata/open-file oriented; maker black and pitch are authoritative only after unpack where needed. |
| RAW CFA statistics | Covered | [RAW stats](RAW_STATS.md) | Black-subtracted per-CFA-position stats over full frames or ROIs, cross-maker regression fixtures. | Not a full ISP or rendered-image analysis. |
| Demosaic | Covered as transparent baseline | [Bilinear demosaic](BILINEAR_DEMOSAIC.md) | Hand-written bilinear demosaic with synthetic and real validation. | Not bit-exact LibRaw parity or production demosaic quality. |
| ColorChecker-SG reference provenance | Covered | [SG provenance](SG_REFERENCE_PROVENANCE.md) | Spectral reference inventory, X-Rite verification, orientation/layout checks. | Not a measured per-unit CLRS-589 SG reference. |
| RAW patch extraction | Covered | [Patch extraction](PATCH_EXTRACTION.md) | RawDigger coordinate extraction, flat-field/WB correction, CSV handoff to CCM, orientation checks. | RawDigger-independent replacement remains constrained by localization diagnostics. |
| RAW chart localization | Partial but bounded | [Localization](RAW_CHART_LOCALIZATION.md) | Projective grid geometry, CLI corner input, residual diagnostics, de-biased detector arbitration. | Final RawDigger replacement stayed unresolved for the centered capture; detector was too unstable to arbitrate. |
| Color accuracy / CCM / Delta E | Covered | [CCM fit](CCM_FIT.md), [patch extraction](PATCH_EXTRACTION.md) | Linear RGB-to-XYZ CCM, held-out diagnostics, dark-patch exclusion experiments, Delta E 76/2000 handling. | Root-polynomial or more flexible models deferred until held-out improvement is proven. |
| Spectral sensitivity extraction | Covered deeply | [Spectral report](SPECTRAL_SENSITIVITY.md), [archive map](SPECTRAL_ARCHIVE_INVENTORY.md) | C++ RAW extraction from monochromator sweeps, legacy-fidelity comparison, five-camera SSF inventory. | Legacy CSVs are fidelity checks, not correctness oracles. |
| Spectral physical closure | Covered | [Spectral report](SPECTRAL_SENSITIVITY.md), [archive map](SPECTRAL_ARCHIVE_INVENTORY.md) | SG-140 and CC-24 physical closure for Canon/Nikon/Sony 2016 cameras using measured illuminant and reflectance. | Phase One IQ3 has SSF but no same-session broadband closure target. |
| Spectral color-fidelity ranking | Covered | [Spectral report](SPECTRAL_SENSITIVITY.md) | Luther residuals and ISO-style SMI over SG-140, CC-24, and CC-18; D55 primary; white-preserving sensitivity bound. | Not claimed bit-exact to paywalled ISO Annex B. |
| Exposure response readiness | Covered | [Exposure response](EXPOSURE_RESPONSE.md) | Exposure-series grouping and black-subtracted CFA response summaries. | Readiness/response summary, not final ISO OECF/PTC. |
| Relative OECF / linearity | Covered | [OECF fit](OECF_FIT.md) | Relative-exposure linearity over usable OECF points. | Assumes constant illumination; not ISO 14524. |
| Stepchart OECF oracle | Covered | [OECF Stepchart](OECF_STEPCHART.md) | Imatest Stepchart parser, archive joins, run-window gates, D800 oracle summaries, advisory cross-ISO luma spread. | Rendered-luma oracle path; no chart-density traceability or measured ISO speed. |
| Stepchart raw-DN ring extraction | Covered with explicit geometry seed | [OECF Stepchart](OECF_STEPCHART.md) | D800 ISO 14524-style ring seed, 20 zone ROIs, oracle-ladder gate, raw-CFA DN summaries. | Manual seed, not automatic detection; strip model correctly refuses this archive. |
| DN-referred PTC-style variance | Covered as diagnostic | [OECF Stepchart](OECF_STEPCHART.md) | Aligned per-pixel temporal variance over 10 repeats, variance-vs-mean fits per ISO/CFA plane, saturated/deep-tail exclusions. | DN-domain diagnostic only; no electron gain/read noise, full well, PRNU, or engineering DR. |
| Dark-frame temporal noise and DSNU | Covered | [Noise](DARK_FRAME_NOISE.md), [dark calibration](DARK_CALIBRATION.md) | Dark-pair temporal noise, moment/robust DSNU, dark-current diagnostic, outlier gating. | DN diagnostics; gain/PTC/DR refused where data does not support them. |
| SFR / MTF center ROI | Covered | [SFR result](SFR_MTF.md), [archive map](SFR_MTF_ARCHIVE_INVENTORY.md) | Green-linear slanted-edge MTF50P, sinc correction, D810 aperture trend, Imatest advisory comparison. | Not luma/gamma Imatest parity, lp/mm, or rendered Y-channel equivalence. |
| SFR / MTF field map | Covered | [SFR result](SFR_MTF.md), [archive map](SFR_MTF_ARCHIVE_INVENTORY.md) | 23-ROI field maps for D810 and D800, per-ROI oracle parsing, field/corner gates, D800 negative trend finding. | Still green-linear CFA SFR; no full sagittal/tangential lens model. |
| Vignetting/shading | Partial | [Patch extraction](PATCH_EXTRACTION.md) | Flat-field correction is used in patch extraction. | No dedicated shading-map or lens-vignetting metric. |
| Distortion / chromatic aberration / flare | Partial / diagnostic only | [Localization](RAW_CHART_LOCALIZATION.md), [CCM fit](CCM_FIT.md) | Localization residuals and dark-patch flare evidence. | No standalone distortion, lateral CA, flare, or veiling-glare metric. |
| Texture, autofocus, rolling shutter, HDR/video | Not covered | none | Out of current still-image archive scope. | Would need new target captures or different data. |

## Dataset Coverage

| Dataset family | Covered outputs |
|---|---|
| CLRS-589 Project Camera | Manifest, RAW stats, demosaic, dark calibration, dark-frame noise/DSNU, exposure-response readiness, OECF fit, SG reference handling, patch extraction, CCM fit, raw chart localization diagnostics. |
| 2016 monochromator / ColorChecker target sessions | Canon/Nikon/Sony SSF extraction, physical closure, SMI/Luther ranking, CC-24 and SG-140 target-set evidence. |
| 2017 camSPECS / Phase One IQ3 | IQ3 SSF and color-fidelity ranking; closure blocked by missing same-session target/reflectance. |
| 2016 esensi D810/D800 SFR | Center and field SFR/MTF, aperture trend gates, Imatest `_Y_multi.csv` oracle comparisons. |
| 2016 D800 OECF Stepchart | Imatest oracle parsing, raw ring-zone extraction, DN-referred per-pixel temporal variance diagnostics. |

## Public Summary

Defensible summary:

> This toolkit implements a C++ still-camera IQ analysis pipeline over archived
> RAW datasets: RAW/CFA statistics, color chart extraction and CCM/Delta E,
> spectral sensitivity with physical closure and SMI-style ranking, OECF and
> Stepchart analysis, dark-frame noise/DSNU, DN-referred temporal-variance
> diagnostics, and slanted-edge SFR/MTF center/field maps. Large private RAW
> datasets stay out of git; every public result is tied to an evidence report,
> parser/fixture tests, and explicit non-claim boundaries.

Do not compress that into "complete ISO camera certification." The project is a
technical reimplementation and validation toolkit, not a certified ISO lab
suite.

## Known Gaps

1. **Calibration-backed electron PTC/DR** — highest scientific gap, but requires
   calibration evidence beyond the current DN-domain Stepchart fits: electron
   gain/read noise, full well, and a defensible DR definition.
2. **Dedicated shading/vignetting and CA/distortion metrics** — feasible only if
   target captures support them; current code has partial ingredients, not final
   metrics.
3. **Automatic Stepchart and SG localization** — useful engineering polish, but
   less scientifically important than the already-guarded seeded workflows.
4. **Spectral archive follow-ups** — PR-655 vs i1Pro illuminant cross-check and
   SSF day-to-day stability are small provenance-strengthening tasks.
5. **Rendered-luma Imatest parity modes** — lower priority because the current
   green-linear metrics already state their scope and Imatest absolute parity is
   advisory.

## Bottom Line

The available archives have been mined for the major objective still-camera IQ
families they can support. The toolkit is now broad enough to present as a
camera-IQ toolkit: color/spectral is especially deep, OECF/noise/MTF are
covered with honest scope boundaries, and the remaining gaps require either
additional calibration evidence or new target-specific captures rather than just
more parser work.

## Implementation and verification

- [`CMakeLists.txt`](../../CMakeLists.txt)
- [CI workflow](../../.github/workflows/ci.yml)
- [Complete portfolio/report index](../README.md)
