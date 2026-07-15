# Evidence Report — Spectral Sensitivity Archive

Date: 2026-07-06
Tool: `camera_iq manifest` (this repository, v0.1.0)
Dataset: `spectral_sensitivity_2016_2017`

## Scope

This starts the camera spectral-sensitivity track as a separate dataset from the
CLRS-589 ColorChecker-SG track. Evidence inventories the first scoped Canon 5D
Mark II monochromator subset, records what was copied into the local private
cache, and defines the next analysis slice. It does not claim a new spectral
sensitivity function yet.

The source archive was read only. The tracked repository records relative
dataset labels; private RAW files, workbooks, and generated manifests remain
under ignored paths.

## Local Copy

For the spectral-sensitivity dataset itself, only one scoped camera subset was
copied locally:

| Role | Location |
|---|---|
| Archive label | `archive:2016_Monochromator/2016_11_21_5D2_Monochromator_OK` |
| Local private cache | `data/private/datasets/spectral_sensitivity_2016_2017/canon_5d2/2016_11_21_5D2_Monochromator_OK/` |
| Derived manifest | `out/spectral_sensitivity_5d2_20161121_manifest.json` |

Copy result: 57 files, 1.09 GB transferred. This is intentionally not a bulk
mirror of the full archive.

The private cache and derived manifest are gitignored (`data/private/` and
`out/`). No RAW, workbook, PDF, or generated manifest is intended for commit.

## Manifest Run

```bash
./build/camera_iq manifest spectral_sensitivity_2016_2017 \
  --config configs/datasets.local.json \
  --subdir canon_5d2/2016_11_21_5D2_Monochromator_OK \
  --out out/spectral_sensitivity_5d2_20161121_manifest.json
# scanned 57 files; exif read for 50/50 raw files; 0 exposure-series candidates
```

File census from the manifest:

| Extension | Count |
|---|---:|
| `.CR2` | 50 |
| `.csv` | 2 |
| `.pdf` | 1 |
| `.pgm` | 1 |
| `.py` | 2 |
| `.xlsx` | 1 |

Directory census:

| Directory | Files |
|---|---:|
| root | 9 |
| `raw/` | 48 |

## Canon 5D2 Subset Contents

The subset is a complete first target for parser and normalizer work:

- 48 sweep RAW files under `raw/`, from
  `raw/2016_11_21_5D2_mono_0592.CR2` through
  `raw/2016_11_21_5D2_mono_0639.CR2`.
- 1 root-level dark frame:
  `2016_11_21_5D2_mono_DARK_FRAME_0640.CR2`.
- 1 root-level test RAW: `2016_11_21_5D2_monotest_0591.CR2`.
- `spd.csv` line-SPD sidecar, 48 samples.
- `2016_11_21_5D2_mono.csv` legacy spectral-response output, 48 samples.
- `2016_11_21_5D2.xlsx`, `2016_11_21_5D2_mono.pdf`,
  `2016_11_21_5D2_mono_DARK_FRAME_0640.pgm`.
- Legacy scripts: `spectral_v2_1.py` and `raw2tiff.py`.

The two legacy scripts are method context only. They are not a correctness
oracle and should not be copied into new implementation logic.

## Wavelength and Sidecar Checks

`2016_11_21_5D2_mono.csv`:

- 48 rows.
- Header: `Wavelength (nm),Red,Green,Blue`.
- Axis: 360 nm to 830 nm in 10 nm steps.
- Channel ranges: R 0.00017047 to 0.54720, G 0.00016838 to 1.0,
  B 0.00015089 to 0.80003.

`spd.csv`:

- 48 rows.
- Axis: 359.993 nm to 830.037 nm, rounding to the same 360 to 830 nm,
  10 nm grid.
- Voltage range: 4.00357e-07 to 6.97176e-05.

These two CSVs share the same rounded wavelength grid, so the next slice can
start with strict axis validation before any RAW pixel extraction.

## Camera Metadata

Representative sweep RAW (`raw/2016_11_21_5D2_mono_0592.CR2`) and dark frame
both parse as:

- Camera: Canon EOS 5D Mark II.
- ISO 100, 1/160 s, f/5.6, 50 mm.
- CFA: GBRG.
- Raw frame: 5792 x 3804.
- Visible image: 5634 x 3752.
- Sensor margins: top 52, left 158.
- Raw pitch: 11584 bytes.
- LibRaw white level: 15600.

Manifest-time black level is `0` for this Canon subset because the manifest
path uses metadata available at file-open time. That is a manifest provenance
field, not a calibration truth. Canon black can populate after `unpack()`; the
analysis paths must use the post-unpack metadata path already used by
`raw-stats`, not this open-time manifest value.

Camera-clock timestamps and filesystem mtimes are provenance clues only. They
must not drive dataset pairing or reference selection.

## Provenance Boundaries

- The camera captures are author-captured archive data.
- The original DPMI-era analysis was a team effort; do not claim one author alone
  performed that original analysis.
- `spectral_v2_1.py` is legacy method context. A new C++ pipeline
  should reimplement the method fresh.
- `2016_Monochromator` in this report means camera spectral-sensitivity sweeps.
  It is not the SG chart reflectance reference directory named
  `sg_2016_archive/monochromator_color_checker`.

## Validation Hierarchy for the Next Slice

Legacy reproduction is a fidelity check, not a scientific correctness proof:

1. **Implementation fidelity:** parse the 48-row response CSV and SPD sidecar,
   then re-extract Canon 5D2 response from the RAW sweep and compare to
   `2016_11_21_5D2_mono.csv`.
2. **Independent oracle:** verify whether a published Canon EOS 5D Mark II
   spectral-sensitivity function exists in a suitable public database before
   asserting an external comparison.
3. **Physical closure:** integrate the same-session measured illuminant SPD and
   measured SG reflectance through the reconstructed response, then compare
   predicted RGB against the Canon 5D2 broadband target capture.

Do not promote a conclusion from tier 1 alone.

## Parser / Normalizer Slice

The first follow-up implementation slice adds a spectral-response
parser/normalizer before RAW extraction:

- read `spd.csv` and `*_mono.csv`;
- validate 48 samples, 360-830 nm rounded axis, 10 nm spacing, finite numeric
  values, and nonzero SPD;
- emit a normalized response JSON/CSV with explicit provenance fields:
  `camera_model`, `dataset_id`, `archive_subset`, `axis_nm`, `response_rgb`,
  `line_spd`, and `validation_tier`;
- keep legacy CSV comparison labeled as `legacy_fidelity_only`.

The command shape is:

```bash
./build/camera_iq spectral-response \
  --response-csv "<local-subset>/2016_11_21_5D2_mono.csv" \
  --spd-csv "<local-subset>/spd.csv" \
  --camera-model "Canon EOS 5D Mark II" \
  --dataset-id spectral_sensitivity_2016_2017 \
  --archive-subset canon_5d2/2016_11_21_5D2_Monochromator_OK \
  --out out/spectral_response_5d2_20161121.json
```

On the local Canon 5D2 subset this emits 48 samples, axis 360-830 nm, a
positive 48-sample line SPD, the original legacy RGB response, normalization
`legacy_peak_channel_normalized_green_1_no_rescale`, and
`validation_tier: "legacy_fidelity_only"`.

## RAW Extraction Slice

The second implementation slice adds toolkit-derived RAW extraction over the
same 48 sweep CR2s plus the matched dark frame. This remains tier-1 fidelity
evidence only: the comparison target is the legacy `*_mono.csv`, not an
independent camera spectral-sensitivity oracle.

The command shape is:

```bash
./build/camera_iq spectral-response \
  --response-csv "<local-subset>/2016_11_21_5D2_mono.csv" \
  --spd-csv "<local-subset>/spd.csv" \
  --camera-model "Canon EOS 5D Mark II" \
  --dataset-id spectral_sensitivity_2016_2017 \
  --archive-subset canon_5d2/2016_11_21_5D2_Monochromator_OK \
  --raw-dir "<local-subset>/raw" \
  --dark-raw "<local-subset>/2016_11_21_5D2_mono_DARK_FRAME_0640.CR2" \
  --ssf-csv-out out/spectral_response_5d2_toolkit_ssf.csv \
  --out out/spectral_response_5d2_raw_20161121.json
```

Implementation details:

- RAW decoding reuses `read_raw_cfa_image`, so Canon black is read after
  `unpack()` through the same `cblack`/active-area path used by `raw-stats`.
  It does not use the manifest's open-time black value.
- The empirical dark frame is subtracted per CFA position over the same
  measurement ROI, so the extracted response uses measured dark residuals while
  still logging the post-unpack metadata black for sanity checking.
- With no explicit `--roi`, extraction uses the central 50% of the active image,
  CFA-balanced to preserve Bayer phase. For this Canon 5D2 subset the emitted
  ROI is `{x: 1408, y: 938, width: 2816, height: 1876}`.
- Near-saturated pixels are excluded from the channel mean and reported per
  sample; a channel only fails if no unsaturated samples remain. Tails at or
  below the dark-frame mean are flagged and clamped to zero response rather than
  hidden or treated as negative physical responsivity.

Local Canon 5D2 run:

| Field | Value |
|---|---:|
| Metadata black by CFA position | `[1022, 1024, 1023, 1023]` |
| Dark residual mean by CFA position | `[0.7014, -0.0404, 0.9930, 1.2593]` |
| Maximum saturated fraction | `0.005576` |
| Samples with any saturation flag | `3 / 48` |
| Maximum below-dark fraction | `1.0` |
| Samples with any below-dark tail flag | `12 / 48` |

The saturation and below-dark flags are diagnostics, not silent corrections:
near-saturated pixels are omitted from the channel mean, while below-dark tails
are clamped to zero physical response and remain visible in the emitted JSON.
The four values above are emitted as top-level `extraction` rollups
(`saturated_sample_count`, `max_saturated_fraction`, `below_dark_sample_count`,
`max_below_dark_fraction`) so a consumer can read whether the guards fired
without iterating the per-sample array — a mis-named per-sample field otherwise
defaults to zero and makes a run look deceptively clean.

Tier-1 legacy fidelity, normalized to the extracted green peak:

| Channel | RMS vs legacy | Pearson correlation |
|---|---:|---:|
| R | `0.0063000` | `0.9993665` |
| G | `0.0068747` | `0.9997911` |
| B | `0.0037980` | `0.9999076` |

This is a strong reimplementation-fidelity result. It is not a proof that the
legacy curve is scientifically correct.

The command now also emits the toolkit-derived SSF as `Wavelength,R,G,B` CSV via
`--ssf-csv-out`, so downstream closure and quality slices can consume the C++
extraction directly instead of the legacy `*_mono.csv`. The legacy CSV stays
as a tier-1 fidelity comparison target only. This distinction matters because the
legacy `spectral_v2_1.py` path is method context, not a scientific oracle:

- its dark-frame path is commented out, so the historical TIFF workflow can run
  without dark subtraction;
- its ROI is selected manually on a downscaled TIFF preview;
- its `meanrgb()` mask uses `cv2.rectangle(..., thickness=10)`, which samples a
  rectangular border rather than a filled interior;
- it consumes demosaiced TIFFs, while the toolkit extraction uses CFA-direct
  per-channel means plus saturation and below-dark rollups.

## Tier-3 Feasibility Check

The earlier blocked conclusion was too broad. The `2016_Monochromator` archive
also contains a same-session Canon 5D2 broadband target set under the
`2016_11_21_5D2_Target` session, and the scoped local private cache now contains
only the closure inputs needed for the next slice:

`data/private/datasets/spectral_sensitivity_2016_2017/canon_5d2/target_closure_20161121/`

| Input | Local file | Verified role |
|---|---|---|
| Target RAW | `2016_11_21_5D2_Target_1_Target_0116.CR2` | Canon EOS 5D Mark II, EF50mm f/2.5 Compact Macro, ISO 100, 1/200 s, f/5.6, 50 mm, 5616 x 3744 |
| White RAW | `2016_11_21_5D2_Target_1_WhiteCard_0117.CR2` | Same camera/lens/exposure metadata as target |
| Dark RAW | `2016_11_21_5D2_Target_1_DarkFrame_0118.CR2` | Same camera/lens/exposure metadata as target |
| Patch coordinates | `*_CR2_SG.txt` sidecars | RawDigger SG exports for the target, white, and dark frames |
| Illuminant SPD | `PR655_HID_avg.txt` | PR-655 HID average, 101 samples, 380-780 nm at 4 nm |
| SG reflectance | `SGMeasurements_CGATS.txt` | i1Pro / SpectraShop SG measurement, 140 patches A1..N10, 380-730 nm at 10 nm |

The session `readme.rtf` states that the 5D2 target files were normalized to the
same naming convention and that the CC/SG patch readings were reordered to match
RawDigger. This makes the tier-3 physical-closure slice feasible without mixing
the separate `canon_5d2_repro` / `2016_IS_Reproduction` capture.

The implemented `spectral-closure` command follows these constraints:

- **confirm the illuminant pairing first (gate 1), do not assume it**: this
  report treats `PR655_HID` as the Target capture's illuminant on strong but
  circumstantial grounds (same session, only measured broadband source in the
  session, standard SSF-plus-known-light design). That the `Target` ColorChecker
  frames were actually shot under that HID lamp is inferred, not documented. The
  closure slice must verify it via a white-card cross-check before using the
  target patches. Because no measured white-card reflectance is staged here,
  this is an illuminant-pairing / chromaticity sanity gate, not the physical
  closure result: the `WhiteCard` frame's dark-subtracted channel ratios should
  be consistent with an SSF-times-HID neutral prediction under one global scale.
  If the chromaticity check fails, stop and report the pairing as unconfirmed
  rather than emitting a closure residual that could rest on the wrong
  illuminant. **Coordinator pre-check (2026-07-07): this gate PASSES.** The
  `WhiteCard` dark-subtracted channel ratios (R/G 0.589, B/G 0.459, mean over
  140 sampled points, RawDigger `_SG` export; dark frame verified ~0 DN) match
  the SSF-times-HID neutral prediction (R/G 0.591, B/G 0.462) to 0.4% and 0.8%.
  This confirms the staged Target/WhiteCard/DarkFrame set is chromatically
  consistent with the PR-655-measured HID lamp. Discriminating power (same SSF,
  reproducible from the local files): the predicted white ratios differ from the
  HID result by roughly 16-53% under the tested broad proxy set (equal energy
  plus 2856K, 5000K, and 6500K blackbodies; examples: tungsten ~2856K gives
  R/G +26%, B/G -25%, daylight ~6500K gives R/G -26%, B/G +53%). Thus the
  sub-1% HID match is specific relative to these proxies, not a generic result
  produced by any broad illuminant assumption. This does not rule out every
  possible engineered spectrum. (Superseded an earlier uncomputed "30-100%+"
  phrasing; the measured spread is ~16-53% for these proxies.) The command
  encodes this as the `white_card_gate` and exits nonzero if the pairing check
  fails;
- use the strict three-way spectral overlap, **380-730 nm**, because the SG
  reflectance file ends at 730 nm; do not extrapolate reflectance to the PR-655
  780 nm endpoint;
- resample the PR-655 illuminant from 4 nm to the 10 nm closure grid and
  restrict the extracted SSF from 360-830 nm to the same 380-730 nm grid;
- fit one global exposure scale `k` across all channels. The target capture is
  1/200 s while the monochromator sweep is 1/160 s, so a global exposure scale
  is expected; per-channel scales are diagnostics only, not the closure fit;
- subtract the same-session dark sidecar when `--dark-rgb` is supplied, exclude
  saturated or not-above-dark target patches, and report extraction rollups as
  top-level JSON fields;
- language should be "consistent with physical closure" until residuals are
  computed and reviewed. Do not tune the RAW extraction to improve closure.

Canon 5D2 Target set 1 tier-3 closure result using the toolkit-derived SSF
(`out/spectral_response_5d2_toolkit_ssf.csv`, fresh challenged run, with
same-session dark sidecar subtraction):

```bash
./build/camera_iq spectral-closure \
  --ssf-csv out/spectral_response_5d2_toolkit_ssf.csv \
  --illuminant data/private/datasets/spectral_sensitivity_2016_2017/canon_5d2/target_closure_20161121/PR655_HID_avg.txt \
  --reflectance data/private/datasets/spectral_sensitivity_2016_2017/canon_5d2/target_closure_20161121/SGMeasurements_CGATS.txt \
  --target-rgb data/private/datasets/spectral_sensitivity_2016_2017/canon_5d2/target_closure_20161121/2016_11_21_5D2_Target_1_Target_0116_CR2_SG.txt \
  --white-rgb data/private/datasets/spectral_sensitivity_2016_2017/canon_5d2/target_closure_20161121/2016_11_21_5D2_Target_1_WhiteCard_0117_CR2_SG.txt \
  --dark-rgb data/private/datasets/spectral_sensitivity_2016_2017/canon_5d2/target_closure_20161121/2016_11_21_5D2_Target_1_DarkFrame_0118_CR2_SG.txt \
  --camera-model "Canon EOS 5D Mark II" \
  --dataset-id spectral_sensitivity_2016_2017 \
  --archive-subset canon_5d2/target_closure_20161121 \
  --out out/spectral_closure_5d2_20161121.json
```

Result:

| Quantity | Value |
|---|---:|
| White-card gate max ratio error | 1.3510% |
| Common wavelength grid | 380-730 nm, 36 bands |
| Closure patches | 140/140 matched |
| Target dark-subtracted patches | 140 |
| Target saturated / below-dark exclusions | 0 / 0 |
| Global exposure scale `k` | 13503.990 |
| R/G/B relative RMS | 9.539% / 9.840% / 11.618% |
| R/G/B correlation | 0.994688 / 0.994328 / 0.994999 |

The JSON also emits per-patch measured, predicted, and residual RGB rows, plus
per-channel diagnostic `k` values. Those per-channel values stay diagnostic only;
the fitted closure uses the single global `k` above.

Four-camera Target set 1 fan-out (`--dark-rgb` supplied for every camera, shared
PR-655 HID illuminant and SG reflectance). This retained closure table is a
mixed-source baseline: the Canon row uses the toolkit RAW-derived SSF; the other
three rows still use their legacy `*_mono.csv` SSFs. Regenerate and retain the
D810, A7RII, and A7SII toolkit-SSF closure artifacts before claiming the closure
table itself is fully migrated off the legacy curves:

| Camera | SSF source | Gate-1 max ratio error | Patches | Target saturated / below-dark exclusions | R/G/B relative RMS | Minimum channel correlation |
|---|---|---:|---:|---:|---:|---:|
| Canon 5D2 | toolkit RAW extraction | 1.351% | 140/140 | 0 / 0 | 9.539% / 9.840% / 11.618% | 0.994328 |
| Nikon D810 | legacy `mono.csv` | 2.949% | 140/140 | 0 / 0 | 10.802% / 11.069% / 13.802% | 0.992676 |
| Sony A7RII | legacy `mono.csv` | 2.103% | 140/140 | 0 / 0 | 10.803% / 11.149% / 13.349% | 0.992517 |
| Sony A7SII | legacy `mono.csv` | 1.284% | 140/140 | 0 / 0 | 9.901% / 9.917% / 11.252% | 0.993567 |

All four 2016 cameras pass the illuminant-pairing gate and close with
high patch-order correlation (minimum channel correlation >0.992). This is a
cross-manufacturer method validation: independently measured SSF, illuminant,
and chart reflectance predict the same-session camera target captures with a
single global exposure scale.

#### CC-24 closure (classic ColorChecker, complementary to SG-140)

The same `spectral-closure` command also closes the **classic 24-patch
ColorChecker** for all four 2016 cameras. This run uses the original paired-column
`CC24Patch_CGATS.txt` SpectraShop export, not the canonical
`patch_id,380,390,...` CSV used by `spectral-smi`; `spectral-closure` reads
paired `SPECTRAL_NM` / `SPECTRAL_DEC` CGATS rows and matches by patch name
(`A1`..`F4`). Inputs are Target set 1 (2016-11-21) `_CC.txt` RawDigger ROI
sidecars, the same PR-655 HID illuminant, legacy `*_mono.csv` SSFs for the four
rows, and per-patch dark subtraction.

| Camera | Gate-1 max ratio error | Patches | Target saturated / below-dark exclusions | R/G/B relative RMS | Minimum channel correlation |
|---|---:|---:|---:|---:|---:|
| Canon 5D2 | 1.012% | 24/24 | 0 / 0 | 5.153% / 4.439% / 5.403% | 0.998234 |
| Nikon D810 | 2.581% | 24/24 | 0 / 0 | 5.583% / 4.271% / 7.575% | 0.998095 |
| Sony A7RII | 1.747% | 24/24 | 0 / 0 | 5.707% / 4.755% / 6.627% | 0.997728 |
| Sony A7SII | 1.473% | 24/24 | 0 / 0 | 5.610% / 5.377% / 5.803% | 0.997400 |

The CC-24 residuals are roughly half the SG-140 residuals (~5-8% vs ~9-14%).
This is a **chart-set difference, not a stronger camera result**: the classic
24-patch ColorChecker is a smaller set of matte, well-behaved colours, while
SG-140 includes many more dark, extreme, and near-glossy patches where relative
error on small RGB values is larger. Both charts hold high patch-order
correlation, so both validate the same SSF physics; CC-24 is the standard-chart
complement to the denser SG-140 closure, not a replacement.

The suite was also re-run on the toolkit's **own** RAW extractions via
`spectral-response --raw-dir --ssf-csv-out` (CR2/NEF/ARW sweeps discovered by
the generalized `discover_spectral_sweep_files`). Distinguish reproducibility:

- **Canon 5D2 is fully self-contained** — its 48 sweep RAW are in the local
  private cache, so closure and quality run on our extraction end-to-end and
  reproduce from the committed repo.
- **D810 / A7RII / A7SII were extracted by reading the mounted archive
  read-only** (their RAW are not scoped-copied locally, ~1-4 GB each). Those
  runs confirm the ranking is stable across legacy-vs-toolkit SSFs (combined
  residuals 0.297 / 0.297 / 0.309, unchanged at the reported precision from the
  legacy `mono.csv` values), but those toolkit SSF artifacts are **not retained
  in the local cache**. The committed local closure pipeline for these three
  still uses legacy SSFs until the archive-backed toolkit CSVs and closure JSONs
  are regenerated and kept under ignored `out/`.

| Camera | Combined residual, toolkit extraction | Combined residual, legacy SSF |
|---|---:|---:|
| Canon 5D2 | 0.2218 | 0.2221 |
| Nikon D810 | 0.2972 | 0.2989 |
| Sony A7RII | 0.2970 | 0.2991 |
| Sony A7SII | 0.3087 | 0.3102 |

The stability of the ranking across legacy and toolkit SSFs is the real result:
it confirms the color-fidelity ordering is a genuine SSF property, not an
artifact of which extraction is trusted.

Do **not** read the residual spread as a camera-quality ranking. These numbers
measure per-camera session and optical-path closure consistency, including lens,
capture, RawDigger sidecar, SSF, and shared illuminant/reference pairing.

### Camera color-fidelity ranking (`spectral-quality`, Luther metric)

The fair cross-camera color ranking is a per-camera SSF property, not a closure
number. The `spectral-quality` command fits each camera's SSFs to the CIE 1931
2-degree color-matching functions (`data/cie1931_2deg_cmf.csv`, the same
10-nm Wyszecki/Stiles table already used by `colorimetry.cpp`) with one 3x3
transform and reports the relative residual (Luther condition). Lower residual
is better, and the metric is scale-invariant so the peak-G SSF normalization does
not bias it. Result over the current CMF grid, 380-730 nm:

| Rank | Camera | SSF source | xbar residual | ybar residual | zbar residual | Combined residual | Quality index |
|---|---|---|---:|---:|---:|---:|---:|
| 1 | Canon 5D2 | toolkit RAW extraction | 0.173 | 0.211 | 0.270 | 0.222 | 0.778 |
| 2 (tie) | Nikon D810 | legacy `mono.csv` | 0.348 | 0.225 | 0.311 | 0.299 | 0.701 |
| 2 (tie) | Sony A7RII | legacy `mono.csv` | 0.342 | 0.198 | 0.335 | 0.299 | 0.701 |
| 4 | Sony A7SII | legacy `mono.csv` | 0.353 | 0.196 | 0.355 | 0.310 | 0.690 |
| 5 | Phase One IQ3 100 | legacy `Spectral_Sensitivity_Data.csv` (2017 camSPECS) | 0.358 | 0.304 | 0.377 | 0.348 | 0.652 |

Canon 5D2 has the lowest residual in this slice; the medium-format Phase One
IQ3 100 has the highest. Nikon D810 and Sony A7RII are effectively tied at the
reported precision, and Sony A7SII sits between that pair and the IQ3. The IQ3
row is its first 2017 capture run; the second run gives a combined residual of
0.336, so the IQ3 figure carries ~0.01 run-to-run uncertainty — an order of
magnitude larger than the 35 mm cameras' spreads — but it ranks last under either
run (both well above the A7SII's 0.310).

IQ3 provenance caveats (distinct from the four 2016 cameras):
- **Different session and rig**: the IQ3 SSF is from the 2017 camSPECS session,
  not the 2016 monochromator run. This is legitimate for the Luther metric, which
  is a pure SSF-vs-CMF geometry — session-, illuminant-, and capture-independent —
  but it would be invalid to pool the IQ3 into any closure comparison.
- **Legacy SSF, SSF-only**: the IQ3 uses its legacy `Spectral_Sensitivity_Data.csv`
  (already in `Wavelength,Red,Green,Blue` form — no conversion needed; the earlier
  "once converted" note was wrong). A toolkit dark-subtracted variant is possible
  from the 2017 IIQ sweeps but is not extracted or retained. The IQ3 cannot be
  tier-3 closed: the 2017 session has no broadband target capture and no measured
  chart reflectance.

Caveats shared with the four-camera table: this is a Luther-condition CMF-fit
residual (a metamerism proxy), not the official CIE Sensitivity Metamerism Index
(which fixes test colors + a reference illuminant); the differences among the
middle cameras are modest; and the component-residual sources are mixed (Canon
toolkit, the rest legacy). The all-toolkit combined-residual validation table above
records that the four-camera ranking is stable when all four 2016 cameras use
toolkit-extracted SSFs.

The separate `canon_5d2_repro` / `2016_IS_Reproduction` captures remain real
archive material, but they are not the closure evidence for this slice because
they are a different session with no paired capture illuminant SPD.

### Camera color-fidelity ranking (`spectral-smi`, perceptual CIE-SMI style)

The Luther residual above is an unweighted geometric CMF-fit. The `spectral-smi`
command upgrades this to the ISO 17321 Sensitivity Metamerism Index method:
synthesize each camera's linear RGB response to a set of real test colours under
a reference illuminant, fit the optimal 3x3 RGB->XYZ transform, and score the
residual **perceptual** CIELAB error as `SMI = 100 - 5.5 * meanDE*ab`. Higher is
better; 100 is a Luther-condition camera. The primary run below uses **CIE D55**
(`data/cie_d55.csv`, generated from the official CIE D55 dataset and white-point-
checked by `tools/gen_cie_d55.py`), because ISO 17321's DSC/SMI annex uses D55 as
the default illuminant. D50 is retained as a cross-check.

The ISO-recommended test set is the **18 chromatic patches** of the classic
ColorChecker 24. The 6 bottom-row neutrals (`A4`/`B4`/`C4`/`D4`/`E4`/`F4`) are
excluded; they were verified as the flattest spectra and a monotonic white-to-
black ramp. All three measured 2016 test sets were run for cross-checking: the
18 chromatic CC-24 patches (ISO-style), the full 24, and the 140-patch
ColorChecker SG.

**CC-18 (ISO-recommended chromatic patches), CIE D55 — primary result:**

| Rank | Camera | SSF source | mean dE*ab (1976) | mean dE2000 | SMI |
|---|---|---|---:|---:|---:|
| 1 | Canon 5D2 | toolkit RAW extraction | 1.69 | 0.93 | 90.7 |
| 2 | Sony A7RII | legacy `mono.csv` | 1.81 | 0.97 | 90.0 |
| 3 | Sony A7SII | legacy `mono.csv` | 1.86 | 0.88 | 89.8 |
| 4 | Nikon D810 | legacy `mono.csv` | 1.93 | 1.07 | 89.4 |
| 5 | Phase One IQ3 100 | legacy `Spectral_Sensitivity_Data.csv` (2017) | 2.13 | 1.10 | 88.3 |

**SMI across all three test sets under D55 (stability check):**

| Camera | SMI, SG-140 | SMI, CC-24 | SMI, CC-18 (ISO) |
|---|---:|---:|---:|
| Canon 5D2 | 93.3 | 93.2 | 90.7 |
| Sony A7RII | 91.7 | 92.4 | 90.0 |
| Sony A7SII | 91.4 | 92.2 | 89.8 |
| Nikon D810 | 91.0 | 91.7 | 89.4 |
| Phase One IQ3 100 | 90.4 | 90.6 | 88.3 |

What is robust and what is not:
- **Endpoints are rock-solid.** Canon 5D2 is best and the Phase One IQ3 is worst
  under every test set and under the Luther residual. (IQ3 run 2 gives SMI 88.4 on
  CC-18, still last.)
- **Sony A7RII is clearly second** under all three sets.
- **Sony A7SII and Nikon D810 are close enough to report conservatively.** Under
  the ISO-default D55 CC-18 run A7SII leads D810 by about 0.36 SMI, while D50
  made them an exact practical tie (89.20 vs 89.21) and the broader SG/CC-24
  sets move the gap by only a few tenths. Treat A7SII as slightly ahead in this
  D55 run, not as a large-margin result.
- **CC-18 SMI is about 2-3 points below CC-24 / SG-140** because dropping the 6
  neutrals removes the trivially-fit flat patches and leaves only the harder
  chromatic ones. This is expected: the neutrals inflate SMI without testing
  colour fidelity, which is exactly why ISO specifies the 18 chromatic set.
  CC-18 is the more discriminating and more honest number.
- **dE2000 is a companion diagnostic, not the SMI ranking metric.** Under dE2000
  A7SII has the lowest CC-18 mean in the D55 run, while SMI is defined on dE*ab
  1976 and keeps Canon clearly ahead. Report both, but do not reinterpret SMI
  using the dE2000 ordering.

White-preserving optimization sensitivity (same CC-18 / D55 inputs):

| Camera | default SMI | white-preserving SMI | delta |
|---|---:|---:|---:|
| Canon 5D2 | 90.70 | 91.73 | +1.03 |
| Sony A7RII | 90.03 | 90.03 | +0.01 |
| Sony A7SII | 89.76 | 89.71 | -0.05 |
| Nikon D810 | 89.40 | 89.76 | +0.36 |
| Phase One IQ3 100 | 88.29 | 86.64 | -1.65 |

The `spectral-smi` command now reports this as a sensitivity bound:
`white_preserving_*` refits the 3x3 while forcing the camera's perfect-diffuser
RGB response to map exactly to the CIE illuminant white. It is a plausible
normalization variant, not a claim that ISO Annex B uses this exact optimizer.
The check narrows the residual optimizer caveat from a pure prose warning to a
measured range: the endpoints (Canon best, IQ3 worst) remain stable, while the
middle pack can shift by a few tenths of an SMI point.

SMI caveats (honest scope):
- **Close to ISO, not bit-exact.** The test set now matches the ISO 17321 shape
  (18 chromatic ColorChecker patches) and the primary illuminant now follows the
  ISO DSC/SMI default (D55). The metric uses `SMI = 100 - 5.5*dE*ab` after a 3x3
  RGB->XYZ fit. The command also emits a white-preserving constrained-fit
  sensitivity check, but the remaining gaps to a citable absolute ISO SMI are
  still the exact slope constant and the paywalled Annex B optimizer /
  normalization details. The command exposes `--smi-slope`; slope changes the
  absolute SMI scale but not the ranking (a positive affine map).
- **Reproducibility.** The `spectral-smi` command and its unit tests are fully
  reproducible from the committed repo (synthetic fixtures + committed D50/D55).
  The five-camera *numbers* depend on the private measured reflectances (CC-24 /
  SG) and per-camera SSFs under `data/private`, so they are not regenerable from
  the public tree alone. Both committed daylight illuminants are white-point-
  verified by generator scripts.
- **Mixed SSF sources / cross-timeline**, exactly as the Luther table: Canon uses
  the toolkit extraction, the rest legacy; the IQ3 is the 2017 camSPECS SSF. SMI,
  like Luther, is a per-camera SSF property, so this is valid for ranking.

## Per-Camera Coverage and Multi-Camera Closure Plan

The archive is a five-camera set across two sessions. Tier-3 closure needs, per
camera: an SSF source, a broadband ColorChecker/Target capture, a measured
illuminant SPD, and a measured chart reflectance. Coverage (verified 2026-07-07,
read-only):

| Camera | Session | SSF source | Target capture | Illuminant SPD | Chart reflectance | Tier-3 |
|---|---|---|---|---|---|---|
| Canon 5D2 | 2016 | sweeps + `mono.csv` | `_Target` (5 sets) | HID (PR655) | SGMeasurements | Target set 1 closure run; gate PASS |
| Nikon D810 | 2016 | sweeps + `mono.csv` | `_Target` (5 sets) | HID (shared) | SGMeasurements (shared) | Target set 1 closure run; gate PASS |
| Sony A7RII | 2016 | sweeps + `mono.csv` | `_Target` (5 sets) | HID (shared) | SGMeasurements (shared) | Target set 1 closure run; gate PASS |
| Sony A7SII | 2016 | sweeps + `mono.csv` | `_Target` (5 sets) | HID (shared) | SGMeasurements (shared) | Target set 1 closure run; gate PASS |
| Phase One IQ3 | 2017 | sweeps + `Spectral_Sensitivity_Data.csv` | none | `Lamp_SPD` xlsx (camSPECS) | none | blocked |

Only the Phase One IQ3 is missing measurements: its 2017 camSPECS session has
spectral sweeps and a lamp SPD but no broadband Target capture and no chart
reflectance, so it is SSF-only (tier-1/tier-2), not physically closable. The
four 2016 cameras are archive input-complete and share the single measured HID
illuminant and the single measured proven-identity SG reflectance. Target set 1
has now been staged and gate-checked for all four 2016 cameras. The additional
target sets span 2016-11-21 and 2016-11-22 and must carry their own
white-card/dark pairing if used later. Copy each additional camera or target
subset only when its slice runs; do not bulk-copy.

## Follow-on TODO

1. **[DONE 2026-07-07]** **Add the Phase One IQ3 to the color-fidelity ranking.**
   The IQ3 `Spectral_Sensitivity_Data.csv` was already in `Wavelength,Red,Green,
   Blue` form — no conversion needed — so `spectral-quality` runs on it directly
   (the SSF is scoped-copied to `data/private/.../iq3_100_2017camspec/`). Added as
   rank 5 above (combined residual 0.348 run 1 / 0.336 run 2, worst of the five).
   The medium-format back has the highest CMF-fit residual of the set.
2. **[DONE 2026-07-07]** **Upgrade from the Luther CMF-fit proxy to the CIE
   Sensitivity Metamerism Index (SMI) method.** The `spectral-smi` command
   (optimal 3x3 + mean CIELAB error, `SMI = 100 - 5.5*dE`) scores all five cameras
   under CIE D55 over three measured test sets — the ISO-recommended 18 chromatic
   ColorChecker patches, the full CC-24, and the SG-140 — see the SMI ranking
   section above (Canon best, IQ3 worst on all three; A7RII second; A7SII slightly
   ahead of D810 in the D55 run). The command also reports a white-preserving
   constrained-fit sensitivity check to bound one plausible normalization variant.
   The remaining gaps to a *bit-exact* citable SMI are the `5.5` slope (paywalled
   ISO constant) and the exact Annex B optimizer / normalization convention;
   slope does not change the ranking.

## Not Claimed

- No independently validated camera spectral-sensitivity function is claimed
  here; the toolkit-derived RAW response is tier-1 legacy-fidelity evidence.
- No assertion that the legacy `*_mono.csv` is scientifically correct.
- No public-SSF comparison yet.
- No claim that these closures are uniqueness proofs or public-SSF validations;
  they are same-session physical consistency checks under one global exposure
  scale.
- No claim that the four-camera closure residuals rank camera color quality.
  They validate the spectral-response method and session pairing; color-quality
  ranking requires a separate SSF-vs-CMF spectral-quality metric.
