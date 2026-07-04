# ColorChecker-SG Reference Provenance (dataset: clrs589_project_camera)

Date: 2026-07-04
Purpose: establish what colorimetric ground truth exists for the SG color slice
(white balance → CCM → ΔE) BEFORE any of it is built. All source data is private
and gitignored (`data/private/datasets/clrs589_project_camera/`); this report
records provenance and numbers only, not the data.

## Summary

There is **no complete 140-patch measured colorimetric reference** in this
dataset. The local spectroradiometry covers **15 chart patches** (patch rows 1–15,
2–3 trials each) plus **one PRD-average row**. The 140-patch camera side
(`Images/ccsg_matlab.csv`) is complete but its patch order does **not** align with
the 15 measured patches, and the measured-patch → physical-SG-cell identity is
undocumented. A full-chart ΔE therefore requires an external manufacturer
reference; a measured-reference ΔE is limited to 15 patches and blocked on an
identity map.

## Inventory

| Role | File | Coverage | Content |
|---|---|---|---|
| Reference SPD (source of truth) | `Old/Patches_SPD.xlsx`, `Old/SPD_all.csv` | 15 patches + 1 PRD row | radiance SPD, 380–780 nm @ 2 nm (201 pts) |
| Reference XYZ (derived view) | `Old/Patches_XYZ.xlsx`, `Old/XYZ_all.csv` | same 16 rows | absolute XYZ, CIE 1931 2° |
| Reference trials | `Old/{1 to 6,7 to 9,10 to 15}/patch_Ntrail_M.mat` | patches **1–15** | spectroradiometer `measurements{wl,radiance,XYZ,totalRadiance,CCT,Duv}` |
| PRD | `Old/prd/prd_1.mat`, `prd_2.mat` | — | 2 spectroradiometer measurements; averaged into reference row 16 |
| Camera measurement | `Images/ccsg_matlab.csv` | **140 patches** | linear camera RGB (dark-subtracted + integrating-sphere vignette-corrected) |
| Camera measurement (alt) | `Images/CCSG_rawdigger.csv`, `Images/ccsg_matlab_dark_frame_corrected.csv` | 140 | RawDigger export / dark-corrected |

Build pipeline confirmed by `Old/load_all.m` (per-folder trial averaging) and
`Images/patch_extract.m` (`checker2colors(..., [10,14], roisize 70)`).

## Verified this session (machine-precision)

| Claim | Method | Result |
|---|---|---|
| Reference XYZ is a derived view of the SPD | recompute `683.017·∫ SPD·CMF₂°·2nm` per patch | scale k = **683.017** across all 16 rows × 3 channels, **zero variance** (683 = Km, photometric) |
| Rows 1–15 = trial averages | mean of `patch_Ntrail_M.mat` XYZ vs xlsx | max\|Δ\| ≈ **4e-13** every patch |
| **Row 16 = mean(prd_1, prd_2)**, not a chart patch | mean of `prd_{1,2}.mat` XYZ vs xlsx row 16 | max\|Δ\| = **4.55e-13**; no `patch_16*` file exists |
| Camera order ≠ reference order | corr(camera green[:16], reference Y[:16]) | **−0.07**, ratio CV 126% |

Illuminant: chart-patch measurements ≈ **6003 K, Duv −0.003**; PRD ≈ **5612 K,
Duv −0.001** (distinct condition — further reason row 16 is not a chart patch).
Absolute radiance XYZ (Y in cd/m²) — needs a white point to reach Lab/ΔE. **Not
D50.**

## Consequences

1. **Source of truth is the SPD, not the XYZ file.** Regenerate XYZ in-code from
   `Patches_SPD.xlsx`; the xlsx XYZ carries no independent information.
2. **Reference is 15 chart patches + 1 PRD row.** Row 16 is a PRD average; its
   chart/white identity is unproven. Do not treat it as a 16th patch or as the
   white reference without evidence.
3. **No trusted 140-patch measured reference exists here.**
4. **Patch identity map is missing.** Camera CSV uses `checker2colors` order; the
   measured set is numbered 1–15 in session order; the `.mat`/xlsx carry no
   row/col field. A naive `csv_row[N] == reference_patch[N]` join is invalid.

## Recommendation for the color slice

**R0 — reference ingestion + typed provenance (before any CCM/ΔE):**
`ColorReference { source, illuminant, observer, patch_count, numbering_order,
unit, white_reference }`. No hardcoded reference table.

- Ingest `Patches_SPD.xlsx`, integrate `683·∫SPD·CMF₂°·2nm` in-code; gate the
  recomputed XYZ to <1e-3 vs the xlsx (already verified true → permanent
  regression lock).
- Ingest a **manufacturer SG reference, edition-specific**: the SG pigment set
  changed in Nov 2014, so the file must be tagged `Before_Nov2014` or
  `After_Nov2014` and matched to the physical chart used for the capture. These
  public files are **Lab / cell-reference tables, not spectral 140-patch data** —
  do not describe them as measured spectra. Cell identity is deterministic from
  `checker2colors([10,14])`.

**Two ΔE reports, both labeled honestly:**
- **Coverage (primary):** fit CCM (3×3 and root-polynomial) on the 140
  manufacturer patches; report full-chart ΔE as "vs manufacturer nominal
  (edition X); includes per-chart manufacturing + illuminant-adaptation error."
- **Traceable spot-check:** the 15 measured chart-patch spectra as an
  instrument-traceable anchor — **gated on building the identity map** (recover
  the operator's measurement order from the study notes / lab report, or
  chromatically fingerprint-match after a rough WB; SG has near-duplicate pastels
  so documented order is far safer). Do not ship a measured ΔE until the map is
  proven.

**White point:** absolute radiance XYZ needs the illuminant white to normalize —
source it from the integrating-sphere (`Sphere` series) or a measured
white/near-neutral target, only after that target's identity is proven. The PRD
row is a candidate only if `prd_{1,2}` are shown to measure a diffuser/white.

**Do not claim** a 140-patch measured-reference ΔE. The dataset supports at most a
15-patch measured anchor plus a 140-patch manufacturer-nominal comparison.
