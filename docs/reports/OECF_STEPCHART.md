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

The command has an optional corner-seeded raw-zone path (`--zone-corners`,
`--zone-inner-fraction`), modeled as a 20x1 contiguous strip. **On this
archive it correctly refuses**: the physical chart is NOT a linear 20-zone
strip.

Raw-mosaic analysis of the actual scene (2026-07-09, `NIKON
D800_i100_s1-40_2.NEF` dumped via `unprocessed_raw` and scanned for uniform
patches) shows an ISO 14524-style layout: ~300x300 px gray patches arranged
in a RING at roughly 1200-1400 px radius around the chart center, in
scrambled density order, plus a continuous V-shaped sweep and auxiliary patch
rows. The ring-patch green medians match the oracle's relative-exposure
ladder within ~4-8% when scaled from the brightest patch (13476 DN at
ISO100): predicted 11470/9762/8311/5618/4567/3627/2881/2186/1658 vs found
11716/9978/8564/5802/4760/3794/3061/2351/1787. The sensor tracks the ladder
linearly; the zones are just not where a strip model looks for them.

A strip-rectangle seed cuts a chord through that ring: it clips two ring
patches (oracle zones 7 and 8) and otherwise samples scene background, which
produces non-monotone means, mid-zone spatial stddevs of 800-1300 DN
(ROIs straddling structured content), and step-free deep zones — plausible
endpoint numbers, garbage in between. An earlier revision of this report
published exactly such endpoint numbers as zone data; they are withdrawn.

The command now enforces an empirical oracle-ladder gate
(`validate_stepchart_raw_iso_against_oracle`): per ISO group, green zone
means must be non-increasing in oracle zone order (deep-shadow ties allowed)
and correlate with `10^log_exposure` at r >= 0.98. The strip seed on this
archive fails it immediately:

```text
Stepchart raw gate: green zone means are not monotone with the oracle ladder
(zone 12 -> 13 rises); corner seed or chart-layout model is wrong
```

Scope boundaries for the raw-zone path:

- It is corner-seeded, not automatic Stepchart detection.
- The 20x1 strip geometry applies to linear step wedges only. This archive
  needs a ring-layout model. In this capture the zone order matches a
  deterministic ISO 14524-style alternating pattern — no external zone-order
  map was needed; see the plan's "Measured Ring Geometry" section for the
  verified 4-parameter seed and two-frame validation.
- When the gate passes, it reports black-subtracted raw-CFA DN means per
  ISO/zone/channel and the repeat-frame spread of ROI means. On the D800
  (which stores black already subtracted, effective black 0) DN values are
  pedestal-free by construction.
- It does **not** claim ISO 14524 conformance, electron-calibrated gain, PTC,
  engineering dynamic range, measured ISO speed, or PRNU.

## Validation

Local verification after implementation:

```bash
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/camera_iq oecf-stepchart d800_oecf_2016 \
  --oracle-dir Results \
  --out out/d800_oecf_stepchart_oracle.json
# The strip seed refuses on this ring-layout archive (exit 1, gate message):
./build/camera_iq oecf-stepchart d800_oecf_2016 \
  --oracle-dir Results \
  --zone-corners "2240,1830;6260,1830;6260,2122;2240,2122" \
  --zone-inner-fraction 1 \
  --out out/d800_oecf_stepchart_raw_zone.json
bash tools/check_public_paths.sh
git diff --check
```

The strip-seed failure is intentional and must remain fail-closed; GitHub CI
tracks the Stepchart hardening, raw-zone gate, and ring-contract commits.
