# ColorChecker-SG Reference Provenance (dataset: clrs589_project_camera)

Date: 2026-07-04
Purpose: establish what colorimetric ground truth exists for the SG color slice
(white balance -> CCM -> DeltaE) before broadening the color pipeline beyond the
first linear CCM. All source data is private and gitignored
(`data/private/datasets/clrs589_project_camera/`); this report records
provenance and numbers only, not the data.

## Summary

The CLRS-589 spectroradiometry inside the project folder is **neutral-only**: a
grayscale luminance ramp (`Old/`, 15 steps) and a Perfect Reflecting Diffuser
(`PRD measurments/`, 45 readings), under two distinct illuminant conditions. That
project folder still has no measured colored-patch SG reference. However, local
course documents contain a compatible 2019 ColorChecker-SG workbook:
`ccsg.xlsx`, sheet `ccsg_2_FIXED_ref`, with 140 cell-labeled A1..N10 spectral
reflectances (380-730 nm @ 10 nm). Its native order aligns strongly with
`Images/ccsg_matlab.csv` (`corr(camera green, luminance proxy)=0.915`). R0 exports
that workbook to `ccsg_2_FIXED_ref.csv`, the stable text format consumed by the
C++ toolkit.

Honest scope: `ccsg.xlsx` is a compatible/standard full-gamut SG reflectance
reference, not yet proven as a per-unit measurement of the exact physical SG
chart used in the CLRS-589 Fuji capture. Do not call it exact chart ground truth
unless that identity is proven.

## Inventory

| Role | File(s) | Coverage | Chroma | Content |
|---|---|---|---|---|
| Neutral ramp reference (SPD) | `Old/Patches_SPD.xlsx`, `Old/SPD_all.csv` | 15 steps + 1 PRD row | neutral | radiance SPD, 380–780 nm @ 2 nm |
| Neutral ramp reference (XYZ, derived) | `Old/Patches_XYZ.xlsx`, `Old/XYZ_all.csv` | same 16 rows | neutral | absolute XYZ, CIE 1931 2° |
| Ramp trials | `Old/{1 to 6,7 to 9,10 to 15}/patch_Ntrail_M.mat` | steps **1–15** | neutral | spectroradiometer `measurements{wl,radiance,XYZ,totalRadiance,CCT,Duv}` |
| PRD (white ref) | `Old/prd/prd_{1,2}.mat`; `PRD measurments/PRD_NN.mat` (+`PRD_SPD_all.csv`,`XYZ_all.csv`) | 2 + 45 readings | neutral | Perfect Reflecting Diffuser radiance/XYZ |
| Illuminant SPD | `Sphere measurments/fernando_ff{1,2,3}.csv` | 3 | — | integrating-sphere spectral radiance (W/m²·µm·sr) |
| Camera measurement | `Images/ccsg_matlab.csv` | **140 patches** | colored | linear camera RGB (dark-subtracted + sphere vignette-corrected) |
| Camera measurement (alt) | `Images/CCSG_rawdigger.csv` (A1… labels), `ccsg_matlab_dark_frame_corrected.csv` | 140 | colored | RawDigger export / dark-corrected |
| Compatible colored SG reference | `data/private/references/ccsg_2019_workbook/ccsg.xlsx`, sheet `ccsg_2_FIXED_ref`; exported to `ccsg_2_FIXED_ref.csv` | **140 patches** | colored | spectral reflectance, 380-730 nm @ 10 nm, cell-labeled A1..N10 |
| Worked color-pipeline precedent | `data/private/references/clrs601_hw12_ccsg_pipeline/{Xopt.mat,mask_ccsg.tif,*.png,ImageOutput_sRGB.jpg}` | 140-patch workflow artifacts | colored | prior MATLAB CCM / ΔE-style pipeline artifacts for validation precedent |

Build pipeline confirmed by `Old/load_all.m` + `PRD measurments/create_single_file.m`
(per-folder averaging/merge) and `Images/patch_extract.m`
(`checker2colors(..., [10,14], roisize 70)`).

## Verified this session (machine-precision)

| Claim | Method | Result |
|---|---|---|
| All measured spectra are neutral | chromaticity (x,y) of all 150 `.mat` (134 raw `measurements` structs + 16 legacy averaged in `Old/Old code/`) | max chroma radius **0.011** (ramp) / **0.004** (PRD); averaged rows Y 166→679, raw trials Y 131→679, x,y ≈ const → grayscale, not color |
| Reference XYZ is a derived view of the SPD | recompute `683.017·∫ SPD·CMF₂°·2nm` | scale k = **683.017** across all 16 rows × 3 channels, **zero variance** (683 = Km) |
| Ramp rows 1–15 = trial averages | mean of `patch_Ntrail_M.mat` XYZ vs xlsx | max\|Δ\| ≈ **4e-13** every row |
| Row 16 = mean(prd_1, prd_2), not a chart patch | mean of `prd_{1,2}.mat` XYZ vs xlsx row 16 | max\|Δ\| = **4.55e-13**; no `patch_16*` file exists |
| Camera order ≠ reference order | corr(camera green[:16], reference Y[:16]) | **−0.07**, ratio CV 126% |
| `ccsg.xlsx` copies are identical | SHA-256 across 2020 HW12, 2020 HW13, 2022 CSCI-631 copies | **8c067562f16f8340b4d980e787703e250915d6c8d7b0f769c7e6154c3998a52a** |
| `ccsg.xlsx` shape/order | openpyxl read of `ccsg_2_FIXED_ref` | 140 rows × 40 columns; labels A1..N10; 36 spectral bands, 380-730 nm @ 10 nm |
| `ccsg.xlsx` order matches camera extraction | corr(`ccsg_matlab.csv` green, workbook luminance proxies) | L*-proxy **0.915**, Y-proxy **0.972**, 550/560-nm proxy **0.963** |
| C++ ingestion path | `tools/export_ccsg_xlsx.py` → `camera_iq reference-info clrs589_project_camera` | validates 140 patches × 36 bands; A1..N10; 380-730 nm; emits typed provenance |
| C++ pairing gate | `reference-info` broadband proxy correlations against `Images/ccsg_matlab.csv` | luminance **0.9775**, red-green **0.9498**, blue-green **0.9617**; configured gate passes |

Illuminant conditions (verified): ramp trials (n=42) **CCT mean 5984 K
[5845–6157], Duv mean −0.0023**; PRD (n=45) **CCT mean ~5510 K, Duv ~−0.0009**
(the `Old/prd` pair reads ~5612 K). PRD is a distinct condition from the ramp.
Absolute radiance XYZ (Y in cd/m²) — needs a white point to reach Lab/ΔE. **Not
D50.**

## Consequences

1. **Source of truth for neutrals is the SPD, not the XYZ file.** Regenerate XYZ
   in-code from the SPD; the xlsx XYZ carries no independent information.
2. **The project spectroradiometry is neutral-only, but R0 now has a compatible
   colored spectral reference.** Use `ccsg.xlsx` for the colored CCM/ΔE demo and
   label it as compatible/standard until physical chart identity is proven.
3. **PRD = white/illuminant reference** (candidate white point), not a color
   chart; the `Old/` ramp row 16 is a PRD average, not a 16th patch.
4. **Patch identity is no longer blocked for `ccsg.xlsx`.** The neutral ramp is
   not a 140-patch chart reference (corr −0.07 against the first 16 camera rows),
   but the workbook is A1..N10 and aligns with the camera extraction order.

## Recommendation for the color slice

**R0 — reference ingestion + typed provenance (before any CCM/ΔE):**
`ColorReference { source, illuminant, observer, patch_count, numbering_order,
unit, white_reference }`. No hardcoded reference table.

Status: R0 is implemented by `reference-info`; the first bounded linear CCM
slice is implemented by `ccm-fit` and reported in `CCM_FIT.md`.

- **Primary colored spectral demo reference:** ingest local
  `ccsg_2019_workbook/ccsg_2_FIXED_ref.csv`, exported from `ccsg.xlsx` sheet
  `ccsg_2_FIXED_ref`. It has cell labels, full spectral reflectance, and
  verified native-order alignment to `ccsg_matlab.csv`. Report ΔE/CCM as
  **vs compatible SG spectral reference**, not exact per-unit chart truth.
- **Pairing acceptance gate:** do not rely only on the reference's internal
  A1..N10 label order. The configured CLRS reference must also pass the
  `reference-info` camera/reference pairing gate against `ccsg_matlab.csv`.
  The gate uses coarse broadband luminance and red-green / blue-green chroma
  proxy correlations, so it validates row pairing only; it is not a substitute
  for CCM residuals or DeltaE.
- **Old archive reference policy:** 2016/2017 camera-characterization datasets
  should select the 2016 SG measurement bundle by project provenance. Do not use
  the 2019 CLRS workbook for old archives unless a specific mapping proves that
  is the correct chart reference. The 2016 PatchTool file order is
  `A1..A10`, then `B1..B10`, ...; any future old-archive color consumer must
  respect or remap that order before pairing it to camera patches.
- **Manufacturer SG reference, edition-specific.** The SG pigment set changed in
  Nov 2014, so the file must be tagged `Before_Nov2014` or `After_Nov2014` and
  matched to the physical chart used for the capture when known. Public files are
  **Lab / cell-reference tables, not spectral 140-patch data** — do not describe
  them as measured spectra.
- **White point — condition-matched only.** The PRD is the standard reflectance
  reference, but the local PRD readings sit at ~5510 K (numbered/copy) / ~5612 K
  (`Old/prd`) while the ramp is ~5984 K — different illuminant conditions. Do not
  treat "PRD = white point" blindly. The white point for the SG camera color
  slice must come from the illuminant **under the same capture condition** —
  prefer the `Sphere` illuminant SPD (the flat-field/vignetting source actually
  used on the CCSG capture) or a proven neutral/white measured under that
  condition. Use PRD only when paired to a matching condition.
- **Neutral validation (buildable now):** the 15-step ramp is an
  instrument-traceable **grayscale tone / neutrality** anchor — usable for OECF /
  white-balance / gray-axis ΔE without any colored reference.

**ΔE reporting:**
- **Coverage (colored):** fit CCM (3×3 and root-polynomial) on the 140
  exported `ccsg.xlsx` spectral patches rendered under the selected illuminant;
  report full-chart ΔE labeled "vs compatible SG spectral reference; not exact
  per-unit measured chart."
- **Public-standard comparison:** optionally repeat against the edition-matched
  manufacturer Lab table and label it "vs manufacturer nominal (edition X);
  includes per-chart manufacturing + illuminant-adaptation error."
- **Neutral spot-check (traceable):** gray-axis ΔE + neutrality against the
  measured ramp — this is the only measured-reference ΔE the dataset supports.

**Do not claim** exact measured-reference ΔE for the physical CLRS-589 chart
unless chart identity is proven or the chart is remeasured. The current honest
claim is stronger than before but still bounded: neutral local measurement
anchor, compatible colored spectral SG reference, and optional manufacturer
nominal comparison.
