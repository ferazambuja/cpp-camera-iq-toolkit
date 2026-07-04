# ColorChecker-SG Reference Provenance (dataset: clrs589_project_camera)

Date: 2026-07-04
Purpose: establish what colorimetric ground truth exists for the SG color slice
(white balance → CCM → ΔE) BEFORE any of it is built. All source data is private
and gitignored (`data/private/datasets/clrs589_project_camera/`); this report
records provenance and numbers only, not the data.

## Summary

The local spectroradiometry is **neutral-only**: a grayscale luminance ramp
(`Old/`, 15 steps) and a Perfect Reflecting Diffuser (`PRD measurments/`, 45
readings), under two distinct illuminant conditions. **There is no measured
colored-patch reference anywhere in this dataset** (all 134 `.mat` measurements
verified near-neutral). The 140-patch camera side (`Images/ccsg_matlab.csv`) is
colored and complete, but it has no measured colored reference to pair with. A
colored-patch ΔE / CCM therefore requires an external, edition-matched
**manufacturer** SG reference; the local measured data anchors **white point and
neutral tone/OECF only**, not chroma.

(Open question, unresolved: whether the SG *colored* patches were ever spectrally
measured on a separate drive. If so those files are outside this repo snapshot.)

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

Build pipeline confirmed by `Old/load_all.m` + `PRD measurments/create_single_file.m`
(per-folder averaging/merge) and `Images/patch_extract.m`
(`checker2colors(..., [10,14], roisize 70)`).

## Verified this session (machine-precision)

| Claim | Method | Result |
|---|---|---|
| All measured spectra are neutral | chromaticity (x,y) of all 134 `.mat` | max chroma radius **0.011** (ramp) / **0.004** (PRD); Y varies 163→679 on ramp → grayscale, not color |
| Reference XYZ is a derived view of the SPD | recompute `683.017·∫ SPD·CMF₂°·2nm` | scale k = **683.017** across all 16 rows × 3 channels, **zero variance** (683 = Km) |
| Ramp rows 1–15 = trial averages | mean of `patch_Ntrail_M.mat` XYZ vs xlsx | max\|Δ\| ≈ **4e-13** every row |
| Row 16 = mean(prd_1, prd_2), not a chart patch | mean of `prd_{1,2}.mat` XYZ vs xlsx row 16 | max\|Δ\| = **4.55e-13**; no `patch_16*` file exists |
| Camera order ≠ reference order | corr(camera green[:16], reference Y[:16]) | **−0.07**, ratio CV 126% |

Illuminant conditions (verified): ramp trials (n=42) **CCT mean 5984 K
[5845–6157], Duv mean −0.0023**; PRD (n=45) **CCT mean ~5510 K, Duv ~−0.0009**
(the `Old/prd` pair reads ~5612 K). PRD is a distinct condition from the ramp.
Absolute radiance XYZ (Y in cd/m²) — needs a white point to reach Lab/ΔE. **Not
D50.**

## Consequences

1. **Source of truth for neutrals is the SPD, not the XYZ file.** Regenerate XYZ
   in-code from the SPD; the xlsx XYZ carries no independent information.
2. **No colored measured reference exists here.** Local measured data supports
   white balance, neutral tone/OECF, and the illuminant white point — not a
   colored CCM fit or colored ΔE.
3. **PRD = white/illuminant reference** (candidate white point), not a color
   chart; the `Old/` ramp row 16 is a PRD average, not a 16th patch.
4. **Patch identity map is missing.** The camera CSV uses `checker2colors`
   extraction order; a naive `csv_row[N] == reference_cell[N]` join is invalid
   (corr −0.07 against the neutral rows).

## Recommendation for the color slice

**R0 — reference ingestion + typed provenance (before any CCM/ΔE):**
`ColorReference { source, illuminant, observer, patch_count, numbering_order,
unit, white_reference }`. No hardcoded reference table.

- **Manufacturer SG reference, edition-specific.** The SG pigment set changed in
  Nov 2014, so the file must be tagged `Before_Nov2014` or `After_Nov2014` and
  matched to the physical chart used for the 2020 capture. Public files are
  **Lab / cell-reference tables, not spectral 140-patch data** — do not describe
  them as measured spectra.
- **Do not assume cell identity.** `checker2colors([10,14])` yields a
  deterministic extraction *order* for a given chart orientation and corner-click
  sequence — it does **not** by itself establish X-Rite A1…J14 identity. R0 must
  gate manufacturer-patch pairing on a **proven** row order (cross-check against
  the RawDigger export's A1… labels, chart-orientation evidence, or a saved
  mapping file) before computing any per-cell ΔE.
- **White point** from the PRD readings and/or the `Sphere` illuminant SPD;
  needed to normalize absolute radiance to relative colorimetry / Lab.
- **Neutral validation (buildable now):** the 15-step ramp is an
  instrument-traceable **grayscale tone / neutrality** anchor — usable for OECF /
  white-balance / gray-axis ΔE without any colored reference.

**ΔE reporting:**
- **Coverage (colored):** fit CCM (3×3 and root-polynomial) on the 140
  manufacturer patches, once the order is proven; report full-chart ΔE labeled
  "vs manufacturer nominal (edition X); includes per-chart manufacturing +
  illuminant-adaptation error."
- **Neutral spot-check (traceable):** gray-axis ΔE + neutrality against the
  measured ramp — this is the only measured-reference ΔE the dataset supports.

**Do not claim** a colored measured-reference ΔE from this dataset. It supports a
neutral (grayscale/white) measured anchor plus a colored manufacturer-nominal
comparison.
