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
project folder still has no measured colored-patch SG reference. However,
archived reference workbooks include a compatible 2019 ColorChecker-SG workbook:
`ccsg.xlsx`, sheet `ccsg_2_FIXED_ref`, with 140 cell-labeled spectral
reflectances (380-730 nm @ 10 nm). Its native workbook order is
`A1, B1, ... N1, A2, ... N10` and aligns strongly with the CLRS-589 camera
extraction order. R0 exports that workbook to `ccsg_2_FIXED_ref.csv`, the stable
text format consumed by the C++ toolkit.

Honest scope: `ccsg.xlsx` is a compatible/standard full-gamut SG reflectance
reference. As of 2026-07-05 it is **verified accurate to the X-Rite manufacturer
nominal at mean ΔE76 1.34** (see "Verification vs X-Rite manufacturer reference"
below), but that proves manufacturer accuracy, **not** a per-unit measurement of
the exact physical SG chart used in the CLRS-589 Fuji capture. Do not call it
exact per-unit chart ground truth unless that identity is proven.

## Inventory

| Role | File(s) | Coverage | Chroma | Content |
|---|---|---|---|---|
| Neutral ramp reference (SPD) | `Old/Patches_SPD.xlsx`, `Old/SPD_all.csv` | 15 steps + 1 PRD row | neutral | radiance SPD, 380–780 nm @ 2 nm |
| Neutral ramp reference (XYZ, derived) | `Old/Patches_XYZ.xlsx`, `Old/XYZ_all.csv` | same 16 rows | neutral | absolute XYZ, CIE 1931 2° |
| Ramp trials | `Old/{1 to 6,7 to 9,10 to 15}/patch_Ntrail_M.mat` | steps **1–15** | neutral | spectroradiometer `measurements{wl,radiance,XYZ,totalRadiance,CCT,Duv}` |
| PRD (white ref) | `Old/prd/prd_{1,2}.mat`; `PRD measurments/PRD_NN.mat` (+`PRD_SPD_all.csv`,`XYZ_all.csv`) | 2 + 45 readings | neutral | Perfect Reflecting Diffuser radiance/XYZ |
| Illuminant SPD | `Sphere measurments/sphere_ff{1,2,3}.csv` | 3 | — | integrating-sphere spectral radiance (W/m²·µm·sr) |
| Camera measurement | `Images/ccsg_matlab.csv` | **140 patches** | colored | linear camera RGB (dark-subtracted + sphere vignette-corrected) |
| Camera measurement (alt) | `Images/CCSG_rawdigger.csv` (A1… labels), `ccsg_matlab_dark_frame_corrected.csv` | 140 | colored | RawDigger export / dark-corrected |
| Compatible colored SG reference | `data/private/references/ccsg_2019_workbook/ccsg.xlsx`, sheet `ccsg_2_FIXED_ref`; exported to `ccsg_2_FIXED_ref.csv` | **140 patches** | colored | spectral reflectance, 380-730 nm @ 10 nm, cell-labeled A1..N10 |
| Derived compatible-reference exports checked | local `spectral-diversity-toolkit/data/ccsg_*_spectral.csv` | 24 / 96 / 140 / multi-measure subsets | colored | same `ccsg.xlsx` source, sometimes reordered/subsetted; not a newer or independent chart measurement |
| Worked color-pipeline precedent | `data/private/references/clrs601_hw12_ccsg_pipeline/{Xopt.mat,mask_ccsg.tif,*.png,ImageOutput_sRGB.jpg}` | 140-patch workflow artifacts | colored | prior MATLAB CCM / ΔE-style pipeline artifacts for validation precedent |
| Manufacturer nominal SG reference (edition-split) | `data/private/references/xrite_colorchecker_sg_2016_zenodo/extracted/ColorCheckerSG_{Before,After}_Nov2014.txt`; layout key `.../sg_2016_archive/color_management_color/ColorChecker SG by rows.txt` | **140 patches** each | colored | X-Rite official Lab (i1Pro 2, M0); SpectraShop geometry key proves letter=column, number=row |

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
| Additional reference workbooks checked | original `all_1nm_data.xlsx` copies in related archive folders | CMF/illuminant support data; not a 140-patch SG reference and not a replacement for `ccsg.xlsx` |
| `ccsg.xlsx` shape/order | openpyxl read of `ccsg_2_FIXED_ref` | 140 rows × 40 columns; labels `A1,B1,...N1,A2,...N10`; 36 spectral bands, 380-730 nm @ 10 nm |
| `ccsg.xlsx` order matches camera extraction | corr(`ccsg_matlab.csv` green, workbook luminance proxies) | L*-proxy **0.915**, Y-proxy **0.972**, 550/560-nm proxy **0.963** |
| RawDigger and MATLAB extraction agree | corr(`CCSG_rawdigger.csv` Gavg, `ccsg_matlab.csv` green) in current row order | **0.99984** for f/8 `1:10`; confirms RawDigger values are faithful to the MATLAB patch order |
| RawDigger label convention is transposed relative to reference IDs | compare RawDigger labels and workbook labels | RawDigger row order `A1,A2,...A14,B1...` maps to reference order `A1,B1,...N1,A2...`; literal label matching over shared IDs gives only **0.407** corr, while current physical order gives **0.958** corr against 560-nm proxy |
| SG orientation sanity check | current physical order vs reference-grid column flip, row flip, and 180° rotation, using RawDigger green vs 560-nm proxy | current **0.958**; reference-grid column/row/180 flips **0.327 / 0.433 / 0.353** — the current sweep direction is the only plausible orientation |
| Local `spectral-diversity-toolkit` exports checked | inspect CSV headers and source fields | `ccsg_measured_140patch_spectral.csv` and subsets cite `source_file=ccsg.xlsx`, `source_sheet=ccsg_2_FIXED_ref`; they are derivative order/subset exports, not missed newer measurements |
| C++ ingestion path | `tools/export_ccsg_xlsx.py` → `camera_iq reference-info clrs589_project_camera` | validates 140 patches × 36 bands; A1..N10; 380-730 nm; emits typed provenance |
| C++ pairing gate | `reference-info` broadband proxy correlations against `Images/ccsg_matlab.csv` | luminance **0.9775**, red-green **0.9498**, blue-green **0.9617**; configured gate passes |

Illuminant conditions (verified): ramp trials (n=42) **CCT mean 5984 K
[5845–6157], Duv mean −0.0023**; PRD (n=45) **CCT mean ~5510 K, Duv ~−0.0009**
(the `Old/prd` pair reads ~5612 K). PRD is a distinct condition from the ramp.
Absolute radiance XYZ (Y in cd/m²) — needs a white point to reach Lab/ΔE. **Not
D50.**

## Verification vs X-Rite manufacturer reference (2026-07-05)

The compatible-scope claim was independently tested against the X-Rite official
ColorChecker-SG nominal Lab tables (Zenodo mirror, i1Pro 2 / M0), which ship both
the `Before_Nov2014` and `After_Nov2014` pigment editions. Our
`ccsg_2_FIXED_ref.csv` reflectance was rendered to CIELAB (D50 / CIE 1931 2°,
perfect-diffuser white Y=100) and compared **by label** to the manufacturer Lab.
Reproduce with `tools/verify_ccsg_vs_xrite.py` (self-contained; hardcoded CMF/D50;
no external deps).

**Render self-check (validates the tables before trusting any chromatic number):**
the column-A neutral border renders L = 96.5 / 8.9 / 50.8 (A1/A2/A3) vs X-Rite
96.7 / 8.1 / 49.8, a,b within ~1 unit of zero. The render pipeline is sound.

| Comparison | mean ΔE76 | median | max (worst patch) |
|---|---|---|---|
| ours vs X-Rite **After_Nov2014** | **1.34** | 1.03 | 11.35 (B4) |
| ours vs X-Rite **Before_Nov2014** | **1.36** | 1.09 | 7.84 (B4) |
| ours vs X-Rite **column-mirrored** (orientation control) | **47.49** | — | — |

**Findings:**

1. **Data is manufacturer-accurate.** Mean ΔE76 1.34 across all 140 patches vs the
   manufacturer nominal sits at the floor set by lot + measurement + 10-nm sampling
   noise. `ccsg.xlsx` is genuinely accurate SG reference data, not merely
   "compatible." Only the saturated violet B4 misses (11.4 After / 7.8 Before) —
   the one patch where the two editions diverge, i.e. edition/lot sensitivity, not
   a data error; every other patch is < 5.6, median ~1.
2. **Labels are physically correct and correctly oriented** (letter = column,
   number = row). Proven two independent ways, **both regenerated by
   `tools/verify_ccsg_vs_xrite.py`**: (a) the SpectraShop geometry key
   `ColorChecker SG by rows.txt` — the tool asserts `PATCH_LEFT` is strictly
   monotonic in the letter (A..N, 0.00→22.75 cm) and `PATCH_TOP` strictly
   monotonic in the number (1..10, 0.00→15.75 cm), i.e. letter = column,
   number = row; (b) the column-mirror control scores ΔE76 47.49, 35× the
   direct-align 1.34. A transposed or mis-oriented label set could not produce
   that separation.
3. **Edition indeterminate at this precision.** After (1.34) ≈ Before (1.36); B4
   leans Before but weakly. The physical CLRS-589 chart is one of these editions;
   this comparison cannot split them.

**Caveat preserved.** 1.34 ΔE76 to the manufacturer **nominal** proves our data is
accurate SG reference data; it does **not** prove it is the exact 2020 physical
CLRS-589 chart per-unit (lot variation ~1-2 ΔE is plausible). The
`compatible_sg_spectral_not_exact_per_unit` scope stays — but "compatible" now
means "manufacturer-accurate," not "unverified." This also strengthens the
dark-patch finding in `CCM_FIT.md`: the reference darks (A2/A5, L≈8) are
now verified accurate to ~1 ΔE, so the camera reading them 2.5–17× brighter is
**unambiguously capture-side** — the reference is ruled out as the cause. The
mechanism is **consistent with veiling glare / scene-capture physics** (bright-
surround scatter into dark cells; both independent extractions agree), not proven
to be the sole mechanism, and not a bad reference.

## Consequences

1. **Source of truth for neutrals is the SPD, not the XYZ file.** Regenerate XYZ
   in-code from the SPD; the xlsx XYZ carries no independent information.
2. **The project spectroradiometry is neutral-only, but R0 now has a compatible
   colored spectral reference.** Use `ccsg.xlsx` for the colored CCM/ΔE demo and
   label it as compatible/standard until physical chart identity is proven.
3. **PRD = white/illuminant reference** (candidate white point), not a color
   chart; the `Old/` ramp row 16 is a PRD average, not a 16th patch.
4. **Patch identity is no longer blocked for `ccsg.xlsx`, but labels must be
   read carefully.** The neutral ramp is not a 140-patch chart reference (corr
   −0.07 against the first 16 camera rows). The workbook order aligns with the
   camera extraction order, but RawDigger's grid labels are transposed relative
   to workbook/reference patch IDs. Report `ccm-fit` exclusions as
   **reference patch IDs**, not RawDigger grid labels.
   Standalone `spectral-diversity-toolkit` columns named `patch_row` and
   `patch_col` are parsed from the reference label text and should not be treated
   as authoritative physical SG geometry.

## Recommendation for the color slice

**R0 — reference ingestion + typed provenance (before any CCM/ΔE):**
`ColorReference { source, illuminant, observer, patch_count, numbering_order,
unit, white_reference }`. No hardcoded reference table.

Status: R0 is implemented by `reference-info`; the first bounded linear CCM
slice is implemented by `ccm-fit` and reported in `CCM_FIT.md`.

- **Primary colored spectral demo reference:** ingest local
  `ccsg_2019_workbook/ccsg_2_FIXED_ref.csv`, exported from `ccsg.xlsx` sheet
  `ccsg_2_FIXED_ref`. It has cell labels, full spectral reflectance, and
  verified native-order alignment to `ccsg_matlab.csv` and RawDigger row order.
  Report ΔE/CCM as
  **vs compatible SG spectral reference**, not exact per-unit chart truth.
- **Pairing acceptance gate:** do not rely only on the reference's internal
  workbook label order. The configured CLRS reference must also pass the
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
- **Coverage (colored):** fit the linear 3×3 CCM on the 140 exported
  `ccsg.xlsx` spectral patches rendered under the selected illuminant; report
  full-chart ΔE labeled "vs compatible SG spectral reference; not exact per-unit
  measured chart."
- **Model expansion:** add root-polynomial or exposure-normalized color models
  only when deterministic held-out / cross-validation metrics show improvement;
  training-only ΔE reductions against a compatible-not-exact reference are not
  sufficient evidence.
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
