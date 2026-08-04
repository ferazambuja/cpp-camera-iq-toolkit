# OECF Stepchart Oracle

A Stepchart relates known printed density zones to the signal recorded by a
camera. The retained Nikon archive is a chart sequence across ISO settings, not
a fixed-ISO shutter ladder, so treating it as an ordinary exposure series would
erase the physical reference axis. This report recovers eight matched Imatest
summaries and preserves the 20-zone chart structure for later RAW comparison.
Here “oracle” means the retained third-party reference table used for
comparison; it is not treated as proof that either analysis is correct.

Analysis date: 2026-07-09

Dataset: `d800_oecf_2016`

## Result

The Nikon D800 OECF archive is now handled as an Imatest Stepchart oracle
dataset, not as a raw fixed-ISO exposure-response ladder.

Real output summary from the configured private dataset:

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

An ordinary fixed-ISO shutter-series search correctly selects no series: ISO
and shutter change together, leaving one shutter value per ISO group. The
Stepchart's 20 printed density zones, rather than shutter time, provide the
rendered-luminance reference axis.

## Archive Shape

The `d800_oecf_2016` dataset points at the `2016_12_10_D800_OECF` archive
directory and contains:

- 94 NEF files.
- 8 `Results/*_summary.csv` Imatest Stepchart summaries.
- 10 listed NEFs per summary, all joined at the dataset root.
- 11 ISO25600 NEFs with no summary.
- 3 `2016_12_09_OECF_D800_test_0148..0150.NEF` unmatched test files.

ISO25600 is diagnostic-only. It reuses `s1-5000`, the same shutter as ISO12800,
because the D800 capture set has no `s1-10000` frame; it is one stop brighter
than the compensated ISO100-12800 ladder.

## Retained advisory data

The reference summaries come from Imatest 4.5.7 Stepchart analyses. Each file
contains several tables, but only the primary zone-response table answers this
study's question. Density, noise, and signal-to-noise rows are excluded rather
than mistaken for additional chart zones. Run dates identify one sequential
analysis per ISO, not one shared batch timestamp. The public record retains the
dataset identity and response values needed for this comparison without
reproducing archive-location metadata.

## Advisory Spread

Rendered-luma spread across the eight ISO summaries is advisory only. It is a
join and provenance check, not a hard physics gate.

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

## Interpretation limits

- ISO 14524 OECF conformance.
- Raw-DN OECF or raw Stepchart zone extraction without declared zone geometry;
  the reference-table-only path stays rendered-luma only. On this archive only
  the ring geometry yields accepted raw-DN output—the strip geometry fails the
  reference-ladder gate.
- Electron-calibrated PTC, full well, PRNU, or dynamic range.
- Chart-density traceability. The `Lux (patch)` column is empty in every
  summary, so the log-exposure axis is nominal chart density.
- Measured ISO speed. ISO tokens are exposure-index settings from filenames.

## Raw zone extraction

Two candidate geometries were tested. A 20-by-1 contiguous strip is rejected
because the physical chart is not a linear step wedge. A measured ring layout
is the accepted RAW-DN extraction path for this archive.

![Reduced crop of the Stepchart ring-zone layout](../images/oecf-stepchart-zones.jpg)

*Illustrative crop from the source D800 test capture. The accepted raw-DN path
samples the discrete tonal zones around the measured ring; the reduced image is
not used for measurement.*

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
(ROIs straddling structured content), and step-free deep zones. The strip seed
is therefore an invalid extraction model for this archive.

The empirical reference-ladder gate requires the green mean in each ISO group
to be non-increasing in zone order, allowing ties in the deepest shadows, and
to correlate with `10^log_exposure` at `r >= 0.98`. The strip seed fails
immediately because its regions do not follow the physical zones.

```text
Stepchart raw gate: green zone means are not monotone with the oracle ladder
(zone 12 -> 13 rises); corner seed or chart-layout model is wrong
```

Scope boundaries for the raw-zone path:

- It is corner-seeded, not automatic Stepchart detection.
- The 20x1 strip geometry applies to linear step wedges only. This archive
  needs a ring-layout model. In this capture the zone order matches a
  deterministic ISO 14524-style alternating pattern — no external zone-order
  map was needed; the accepted 4-parameter seed is verified by the two-frame
  validation described below.
- The accepted ring has center `(3633, 2582)`, radius `1341 px`, angular offset
  `-97.8 degrees`, and `150 px` square sampling regions. It passes the
  reference-ladder gate on all eight matched ISO groups. Green-channel
  correlation with `10^log_exposure` is 0.999795-0.999938 across ISO 100-12800.
- When the gate passes, it reports black-subtracted raw-CFA DN means per
  ISO/zone/channel, the repeat-frame spread of ROI means, and aligned same-pixel
  temporal variance over the 10 repeated frames. On the D800 (which stores
  black already subtracted, effective black 0) DN values are pedestal-free by
  construction.
- The DN-referred variance-vs-mean diagnostic fits the signal-dominated zones
  (`Log(exp) >= -1.6`) per ISO and CFA plane, excluding saturated zones at high
  ISO and the flare/noise-floor tail.
- It does **not** claim ISO 14524 conformance, electron-calibrated gain/read
  noise, full well, engineering dynamic range, measured ISO speed, or PRNU.

DN-referred per-pixel temporal-variance result from that accepted ring geometry
(G1 shown for compactness):

| ISO | min R^2 across planes | G1 slope (DN^2/DN) | G1 intercept (DN^2) | fit zones per plane |
|---:|---:|---:|---:|---:|
| 100 | 0.9844 | 0.656 | -570.7 | 15 |
| 200 | 0.9892 | 1.013 | -708.3 | 15 |
| 400 | 0.9904 | 1.900 | -1243.1 | 15 |
| 800 | 0.9985 | 2.759 | -681.3 | 15 |
| 1600 | 0.9996 | 5.023 | -480.2 | 15 |
| 3200 | 0.9995 | 10.203 | -1103.3 | 14-15 |
| 6400 | 0.9996 | 19.274 | 607.2 | 14-15 |
| 12800 | 0.9954 | 45.277 | -11665.9 | 13-15 |

These slopes are useful DN-domain ISO-dependent variance diagnostics. The
intercepts are not reported as read noise: the chart density axis is nominal,
deep patches are flare/noise-floor dominated, and no electron calibration or
full-well evidence is present.

The strip-seed failure is intentional and remains fail-closed; the ring seed is
the accepted raw-DN path. The implementation companion documents the two
geometry branches, refusal behavior, and validation fixtures.

## Engineering companion

The [RAW implementation companion](../implementation/raw-foundation.md)
explains how the Stepchart analysis is realized in C++ and routes readers to
the public source and tests.
