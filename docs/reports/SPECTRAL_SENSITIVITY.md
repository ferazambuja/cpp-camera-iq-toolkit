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

- The camera captures are Fernando's archive captures.
- The original DPMI-era analysis was a team effort; do not claim Fernando alone
  performed that original analysis.
- `spectral_v2_1.py` is legacy Gold legacy method context. A new C++ pipeline
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
evidence only: the comparison target is legacy Gold's legacy `*_mono.csv`, not an
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

Predeclare these implementation constraints before running closure:

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
  phrasing; the measured spread is ~16-53% for these proxies.) The closure
  slice must still encode this as an automated gate rather than relying on this
  manual check;
- use the strict three-way spectral overlap, **380-730 nm**, because the SG
  reflectance file ends at 730 nm; do not extrapolate reflectance to the PR-655
  780 nm endpoint;
- resample the PR-655 illuminant from 4 nm to the 10 nm closure grid and
  restrict the extracted SSF from 360-830 nm to the same 380-730 nm grid;
- fit one global exposure scale `k` across all channels. The target capture is
  1/200 s while the monochromator sweep is 1/160 s, so a global exposure scale
  is expected; per-channel scales are diagnostics only, not the closure fit;
- subtract the same-session dark frame, count saturated patches, and report
  exclusions as top-level rollups;
- language should be "consistent with physical closure" until residuals are
  computed and reviewed. Do not tune the RAW extraction to improve closure.

The separate `canon_5d2_repro` / `2016_IS_Reproduction` captures remain real
archive material, but they are not the closure evidence for this slice because
they are a different session with no paired capture illuminant SPD.

## Per-Camera Coverage and Multi-Camera Closure Plan

The archive is a five-camera set across two sessions. Tier-3 closure needs, per
camera: an SSF source, a broadband ColorChecker/Target capture, a measured
illuminant SPD, and a measured chart reflectance. Coverage (verified 2026-07-07,
read-only):

| Camera | Session | SSF source | Target capture | Illuminant SPD | Chart reflectance | Tier-3 |
|---|---|---|---|---|---|---|
| Canon 5D2 | 2016 | sweeps + `mono.csv` | `_Target` (5 sets) | HID (PR655) | SGMeasurements | ready, pairing verified |
| Nikon D810 | 2016 | sweeps + `mono.csv` | `_Target` (5 sets) | HID (shared) | SGMeasurements (shared) | ready, not yet run |
| Sony A7RII | 2016 | sweeps + `mono.csv` | `_Target` (5 sets) | HID (shared) | SGMeasurements (shared) | ready, not yet run |
| Sony A7SII | 2016 | sweeps + `mono.csv` | `_Target` (5 sets) | HID (shared) | SGMeasurements (shared) | ready, not yet run |
| Phase One IQ3 | 2017 | sweeps + `Spectral_Sensitivity_Data.csv` | none | `Lamp_SPD` xlsx (camSPECS) | none | blocked |

Only the Phase One IQ3 is missing measurements: its 2017 camSPECS session has
spectral sweeps and a lamp SPD but no broadband Target capture and no chart
reflectance, so it is SSF-only (tier-1/tier-2), not physically closable. All
four 2016 cameras are complete and share the single measured HID illuminant and
the single measured proven-identity SG reflectance.

Multi-camera closure plan (fires after the 5D2 closure slice lands, not before):
the `spectral-closure` command is camera-agnostic, so extending it to D810,
A7RII, and A7SII is mechanical — same command, per-camera `_Target` +
SSF inputs, the shared illuminant and reflectance, and the same white-card
pairing gate run first for each (same 2016-11-21 session and HID, so each is
expected to pass its own gate). This yields a four-camera cross-manufacturer
closure suite (Canon / Nikon / Sony x2) rather than a single-camera result. Copy
each camera's scoped Target subset only when its slice runs; do not bulk-copy.

## Not Claimed

- No independently validated camera spectral-sensitivity function is claimed
  here; the toolkit-derived RAW response is tier-1 legacy-fidelity evidence.
- No assertion that the legacy `*_mono.csv` is scientifically correct.
- No public-SSF comparison yet.
- No physical-closure result yet; the same-session Canon 5D2 target, dark, SG
  reflectance, and PR-655 illuminant inputs are staged locally, but the closure
  computation has not been implemented or run.
