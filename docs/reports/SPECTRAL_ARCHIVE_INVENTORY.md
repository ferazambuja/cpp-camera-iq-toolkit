# Spectral Sensitivity Archive Inventory

Date: 2026-07-07
Scope: the camera monochromator / camSPECS archive feeding the `spectral-response`,
`spectral-closure`, `spectral-quality`, and `spectral-smi` slices.

This is the **canonical file->role map** for the spectral track. It exists so the
correct per-camera reading is chosen once and never re-searched, and so no file
type is silently ignored. When a slice needs a camera SSF, illuminant, or chart
reflectance, take it from the row below — do not re-walk the archive.

All paths are archive labels or repo-relative private labels; no absolute mount
paths are recorded (public-path guard). Read-only: nothing here is bulk-copied.

## Archive roots

| Label | Meaning |
|---|---|
| `archive:2016_Monochromator/` | 2016-11-18..22 monochromator + camSPECS + Target sessions for the four 35 mm cameras |
| `archive:2016_Monochromator/Data_Collected/` | **Curated, authoritative** per-camera outputs + shared illuminant + chart reflectances |
| `archive:2017_camspec/` | 2017-04-16 Phase One IQ3 camSPECS session (separate rig/timeline) |
| `private:` | `data/private/datasets/spectral_sensitivity_2016_2017/` (gitignored local cache) |

**Authority rule:** `Data_Collected/` is the source of truth. Each camera was
measured on several days (see below); the loose per-day session folders are raw
inputs. Prefer the curated `Data_Collected/<camera>/Monochromator/` CSV.

## Per-camera monochromator SSF (the reading each slice uses)

Each camera has **five** monochromator measurements (11-18 through 11-21, some
with a second "II" run the same day). The **canonical choice is 2016-11-21**,
because the broadband Target captures used for closure are also 2016-11-21 —
same-session SSF+capture pairing is the scientifically correct choice. Using
11-21 also avoids the "II" second-run ambiguity (11-21 has exactly one file per
camera).

| Camera | Canonical SSF (authoritative) | Toolkit file actually used | Byte-identical? |
|---|---|---|---|
| Canon 5D2 | `Data_Collected/Canon 5D Mk II/Monochromator/2016_11_21_5D2_mono.csv` | `private:canon_5d2/.../2016_11_21_5D2_mono.csv` (legacy) **and** `out/spectral_response_5d2_toolkit_ssf.csv` (toolkit RAW extraction of the 11-21 sweeps) | legacy = YES (verified 2026-07-07) |
| Nikon D810 | `Data_Collected/Nikon D810/Monochromator/2016_11_21_D810_mono.csv` | `private:d810/target_closure_20161121/2016_11_21_D810_mono.csv` | YES |
| Sony A7RII | `Data_Collected/Sony A7RII/Monochromator/2016_11_21_A7R2_mono.csv` | `private:a7r2/target_closure_20161121/2016_11_21_A7R2_mono.csv` | YES |
| Sony A7SII | `Data_Collected/Sony A7SII/Monochromator/2016_11_21_A7S2_mono.csv` | `private:a7s2/target_closure_20161121/2016_11_21_A7S2_mono.csv` | YES |
| Phase One IQ3 100 | `archive:2017_camspec/Capture/IQ3_100_1st/IQ3 100_Spectral_Sensitivity_Data.csv` | `private:iq3_100_2017camspec/IQ3_100_1st_mono.csv` | copy of source (run 2 also staged) |

**Verification (2026-07-07):** every 2016 SSF the toolkit uses was md5-compared
against the authoritative `Data_Collected` 11-21 file and is byte-identical. No
wrong-camera or wrong-day file is in use.

## Shared analysis inputs

| Role | Canonical file | Used by | Verified |
|---|---|---|---|
| Illuminant (closure) | `Data_Collected/Light Source/PR655_HID_avg.txt` (PR-655 spectroradiometer, HID lamp, 380-780 nm) | `spectral-closure` | md5 MATCH to `private:.../PR655_HID_avg.txt` |
| Illuminant (SMI primary) | `data/cie_d55.csv` (committed standard CIE D55) | `spectral-smi` | official CIE D55 dataset subset, white point x=0.33242 y=0.34756 checked by `tools/gen_cie_d55.py` |
| Illuminant (SMI cross-check) | `data/cie_d50.csv` (committed standard CIE D50) | `spectral-smi` | white point x=0.34566 y=0.35863 checked by `tools/gen_cie_d50.py` |
| Chart reflectance (SG-140) | `Data_Collected/Color Checker/SGMeasurements_CGATS.txt` (i1Pro, 140 patches, 380-730 nm) | `spectral-closure`, `spectral-smi` | md5 MATCH to staged copy |
| Chart reflectance (CC-24) | `Data_Collected/Color Checker/CC24Patch_CGATS.txt` (i1Pro, classic 24-patch) | `spectral-smi` (full 24 + the ISO 18-chromatic subset) | converted to `private:...monochromator_color_checker/cc24_reflectance_canonical.csv`; the 18-chromatic subset is `cc18_chromatic_reflectance_canonical.csv` (neutrals A4/B4/C4/D4/E4/F4 excluded, verified as the flattest, monotonic white->black ramp) |

## Target capture sessions (closure broadband captures)

Per camera, `Data_Collected/<camera>/Target/` holds RawDigger ROI sidecars for
**two** Target sessions, `2016-11-21` and `2016-11-22`, each with numbered
Target / WhiteCard / DarkFrame captures and two sidecar flavors:

- `*_CC.txt` — ColorChecker 24-patch ROI RGB
- `*_SG.txt` — ColorChecker SG 140-patch ROI RGB

Closure uses the **2016-11-21 set 1** (Target_1 / WhiteCard_1 / DarkFrame_1,
`_SG.txt`), pairing the 11-21 SSF with the 11-21 capture. The 11-22 set is an
additional same-chart session; it must carry its own white-card/dark pairing if
used later.

## Correctness verdict

The monochromator SSF chosen for every camera is the correct camera, correct day
(11-21), and byte-identical to the curated authoritative file. The closure
illuminant and SG reflectance are likewise byte-identical to the authoritative
`Data_Collected` copies. No incorrect file is in use in any spectral slice.

## Hazards (do not trip on these)

- **Mis-filed A7RII CSV in the A7SII folder.** `Data_Collected/Sony A7SII/
  Monochromator/` contains `2016_11_19_A7RII_mono_II.csv` — a Sony **A7RII** file
  sitting in the A7SII folder. Always select the `2016_11_21_A7S2_mono.csv` file
  by exact name; never "grab the A7SII folder's CSVs" blindly.
- **Second-run "II" files** exist for 11-19 and 11-20 (e.g. `..._mono_II.csv`).
  The canonical 11-21 has no II variant, which is the reason to standardize on it.
- **CamSpec vs Monochromator.** `*_CamSpec_*` / `*_camspec_*` session folders are a
  different measurement method (camSPECS express) than the monochromator sweeps.
  The SSF track uses the **Monochromator** outputs. Do not mix them.

## Available but unused data (cataloged so it is not "ignored")

| Data | Location | Why unused / potential use |
|---|---|---|
| CC ROI sidecars (target RGB) | `<camera>/Target/*_CC.txt` | **Now used** by the CC-24 `spectral-closure` (all four cameras, Target set 1); staged under `private:.../<camera>/target_closure_20161121/cc24/`. The 11-22 Target set 2 `_CC.txt` remain available for a repeat. |
| i1Pro illuminant | `Data_Collected/Light Source/i1Pro_HID_avg.txt` | A second-instrument measurement of the same HID lamp; usable as a cross-check against PR-655. |
| Other-day SSFs (11-18/19/20 + II) | `Data_Collected/<camera>/Monochromator/*.csv` | Repeatability set; could quantify day-to-day SSF stability. Not needed for closure (11-21 is canonical). |
| Excel spectral workbooks | `2016_11_21_<cam>.xlsx` (per mono session); `2017_camspec/.../spectral.xlsx` | Excel form of data already available as CSV; no new information. |
| 2017 lamp SPD | `archive:2017_camspec/Capture/Lamp_SPD_Data_Run{1,2}.xlsx` | The only measured illuminant for the IQ3 session. Would be required for any IQ3 closure, but the 2017 session has **no** broadband Target capture or chart reflectance, so IQ3 stays SSF-only. |
| `.spectrashop` | `Data_Collected/Light Source/`, `Color Checker/` | SpectraShop native measurement projects; the `_CGATS`/`_avg.txt` exports carry the same numbers in text form. |
| `.pgm`, `.pdf`, `.py`, `.tiff`, `converted/`, Capture One `.cos/.cop/.cof/.cot/.cosessiondb` | various sessions | Previews, plots, legacy scripts (method context only, not a correctness oracle), demosaiced intermediates, and capture-software session state. None are analysis inputs. |

## 2017 Phase One IQ3 (separate session)

`archive:2017_camspec/` is a distinct rig and timeline (2017-04-16 camSPECS), not
part of the 2016 monochromator run. It has IIQ sweeps, `IQ3 100*_Spectral_
Sensitivity_Data.csv` (SSF, used for the Luther + SMI ranking), `spd.csv`, and
`Lamp_SPD_Data_Run{1,2}.xlsx`, but **no** broadband target and **no** chart
reflectance -> SSF-only, cannot be tier-3 closed. There is no `Data_Collected`
curation for 2017; the two capture runs (`IQ3_100_1st`, `IQ3_100_2nd`) are both
staged. Luther/SMI are per-camera SSF properties, so ranking the 2017 IQ3
alongside the 2016 cameras is valid; a closure comparison would not be.

## Follow-ups

1. **[DONE 2026-07-07] Adopt CC-24 for the ISO-style SMI.** `CC24Patch_CGATS.txt`
   is converted to canonical CSV and `spectral-smi` runs over the 18 chromatic
   patches (ISO), the full 24, and the SG-140. The primary SMI run uses D55, as
   ISO DSC/SMI specifies by default; D50 is retained as a cross-check. The three
   sets agree on the endpoints (Canon best, IQ3 worst) and on A7RII second; A7SII
   is slightly ahead of D810 under D55, while D50 makes them a practical tie.
   The command also reports a white-preserving constrained-fit sensitivity check
   to bound one plausible Annex-B normalization variant. See the SMI ranking
   section of `SPECTRAL_SENSITIVITY.md`.
2. **[DONE 2026-07-07] CC-24 closure.** All four 2016 cameras were closed against
   the classic 24-patch ColorChecker using the `<camera>/Target/*_CC.txt` ROI RGB
   (Target set 1) + `CC24Patch_CGATS.txt` reflectance + PR-655 HID + legacy SSF.
   All gate-PASS, 24/24 matched, >0.997 correlation; residuals ~5-7% (roughly half
   the SG-140 ~9-14%, because the 24 matte patches are better-behaved than the
   dense SG-140). See the CC-24 closure section of `SPECTRAL_SENSITIVITY.md`.
   Sidecars staged under `private:.../<camera>/target_closure_20161121/cc24/`.
3. **PR-655 vs i1Pro illuminant cross-check** for the closure illuminant.
4. **SSF day-to-day stability** (optional): rank the same camera across its 11-18..21
   monochromator runs to bound repeatability of the Luther/SMI numbers.
