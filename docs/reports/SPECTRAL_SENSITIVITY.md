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

Only one scoped camera subset was copied locally:

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
3. **Physical closure:** if an independently measured target or illuminant SPD
   and a camera capture are present, integrate SPD through the reconstructed
   response and compare predicted RGB against actual camera RGB.

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

Next, add either the independent Canon 5D Mark II public-SSF comparison (tier 2)
or a physical-closure test against an independently measured target/illuminant
capture (tier 3). Do not tune the RAW extraction to the legacy curve.

## Not Claimed

- No independently validated camera spectral-sensitivity function is claimed
  here; the toolkit-derived RAW response is tier-1 legacy-fidelity evidence.
- No assertion that the legacy `*_mono.csv` is scientifically correct.
- No public-SSF comparison yet.
- No physical-closure result yet.
