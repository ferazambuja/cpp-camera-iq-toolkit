# Evidence OECF Stepchart Oracle

Date: 2026-07-09

Dataset: `d800_oecf_2016`

Command:

```bash
./build/camera_iq oecf-stepchart \
  d800_oecf_2016 \
  --oracle-dir Results \
  --out out/d800_oecf_stepchart_oracle.json
```

## Result

The Nikon D800 OECF archive is now handled as an Imatest Stepchart oracle
dataset, not as a raw fixed-ISO exposure-response ladder.

Real output summary from the local private cache:

| Field | Value |
|---|---:|
| Oracled Stepchart summaries | 8 |
| Oracled ISO range | 100-12800 |
| Combined files per summary | 10 |
| Zones per summary | 20 |
| Run-date window | 11-Dec-2016 03:19:31 to 03:39:54 |
| Run-date span | 1223 s |
| ISO25600 unoracled files | 11 |
| Test/unmatched NEFs | 3 |

`camera_iq exposure-response d800_oecf_2016 --series-min 3` selecting zero
series is correct: the capture changes ISO and shutter together, so each ISO
group has one shutter token. The Stepchart's 20 printed density zones provide
the rendered-luminance oracle axis.

## Archive Shape

The private cache mirrors `2016_12_10_D800_OECF` as:

- 94 NEF files.
- 8 `Results/*_summary.csv` Imatest Stepchart summaries.
- 10 listed NEFs per summary, all joined at the dataset root.
- 11 ISO25600 NEFs with no summary.
- 3 `2016_12_09_OECF_D800_test_0148..0150.NEF` unmatched test files.

ISO25600 is diagnostic-only. It reuses `s1-5000`, the same shutter as ISO12800,
because the D800 capture set has no `s1-10000` frame; it is one stop brighter
than the compensated ISO100-12800 ladder.

## Oracle Format

The summaries are Imatest 4.5.7 Stepchart CSVs. The parser is deliberately
table-scoped because the files contain several traps:

- The primary header has 10 split cells, ending in empty `Lux (patch),`, while
  every primary data row has 8 fields.
- Secondary density/noise/SNR tables also contain numeric zone-like rows and
  `Inf` values.
- `Directory` metadata in the tail contains private absolute paths and is not
  emitted.
- `File Name`, `File Size`, and `File Source` metadata rows are not combined
  file-list rows.
- Run dates are one sequential run per ISO, not one identical batch timestamp.

The command emits only sanitized dataset labels and dataset-relative filenames.
The live JSON was checked for zero private absolute-path markers.

## Advisory Spread

The command reports rendered-luma spread across the 8 ISO summaries as advisory
only. It is a join/provenance sanity check, not a hard physics gate.

Observed envelope:

| Zone | Pixel min | Pixel max | Spread |
|---:|---:|---:|---:|
| 1 | 42.4 | 48.5 | 6.1 |
| 14 | 1.9 | 2.4 | 0.5 |
| 15 | 1.2 | 1.6 | 0.4 |
| 16 | 0.9 | 1.2 | 0.3 |
| 17 | 0.5 | 0.8 | 0.3 |
| 18 | 0.1 | 0.4 | 0.3 |
| 19 | 0.1 | 0.4 | 0.3 |
| 20 | 0.0 | 0.4 | 0.4 |

## Not Claimed

This slice does not claim:

- ISO 14524 OECF conformance.
- Raw-DN OECF or raw Stepchart zone extraction unless `--zone-corners` is
  provided; the default oracle-only command stays rendered-luma only.
- PTC or dynamic range.
- Chart-density traceability. The `Lux (patch)` column is empty in every
  summary, so the log-exposure axis is nominal chart density.
- Measured ISO speed. ISO tokens are exposure-index settings from filenames.

## Raw Zone Extraction

The command now has an optional corner-seeded raw-zone path:

```bash
./build/camera_iq oecf-stepchart \
  d800_oecf_2016 \
  --oracle-dir Results \
  --zone-corners "2240,1830;6260,1830;6260,2122;2240,2122" \
  --zone-inner-fraction 1 \
  --out out/d800_oecf_stepchart_raw_zone.json
```

The corner seed is an audited active-image OECF-20 strip rectangle selected from
the same static D800 scene. `--zone-inner-fraction 1` uses the full 20 equal
zones, matching the Imatest primary table geometry (`201x292` px for zones
1-19 and `200x292` px for zone 20).

Real raw-zone output summary from the local private cache:

| ISO | Frames | Green zone 1 mean DN | Green zone 20 mean DN | Max green repeat-mean stddev DN |
|---:|---:|---:|---:|---:|
| 100 | 10 | 4760.1 | 577.1 | 26.06 |
| 200 | 10 | 4792.8 | 581.0 | 26.07 |
| 400 | 10 | 4809.5 | 583.0 | 33.27 |
| 800 | 10 | 4857.7 | 588.4 | 24.50 |
| 1600 | 10 | 4886.2 | 597.1 | 21.59 |
| 3200 | 10 | 5126.1 | 630.9 | 36.66 |
| 6400 | 10 | 5285.7 | 652.6 | 26.13 |
| 12800 | 10 | 5526.4 | 675.0 | 144.35 |

Scope boundaries for the raw-zone path:

- It is corner-seeded, not automatic Stepchart detection.
- It reuses the projective homography pattern from ColorChecker-SG work, but
  the Stepchart strip has its own 20x1 geometry.
- It reports black-subtracted raw-CFA DN means per ISO/zone/channel and the
  repeat-frame spread of ROI means.
- It does **not** claim ISO 14524 conformance, electron-calibrated gain, PTC,
  engineering dynamic range, measured ISO speed, or PRNU. True PTC still needs
  per-pixel temporal variance and calibration evidence, not just repeat-mean
  spread over corner-seeded chart zones.

## Validation

Local verification after implementation:

```bash
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/camera_iq oecf-stepchart d800_oecf_2016 \
  --oracle-dir Results \
  --out out/d800_oecf_stepchart_oracle.json
./build/camera_iq oecf-stepchart d800_oecf_2016 \
  --oracle-dir Results \
  --zone-corners "2240,1830;6260,1830;6260,2122;2240,2122" \
  --zone-inner-fraction 1 \
  --out out/d800_oecf_stepchart_raw_zone.json
bash tools/check_public_paths.sh
git diff --check
```

CI passed on `3b904bd` before the final hardening challenge and on the follow-up
fix commit recorded in git history.
