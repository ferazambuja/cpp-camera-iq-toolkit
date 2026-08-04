# Raw CFA Statistics

Every later RAW measurement depends on reading the visible Bayer mosaic with
the correct active-area crop, row stride, color phase, black pedestal, and white
level. This report defines that common sensor-linear baseline and checks it on
Fujifilm, Canon, and Nikon files. Negative residuals remain visible and headroom
is measured against the signal range above black, preventing dark and
near-clipping behavior from being silently distorted.

Analysis date: 2026-07-02
Dataset: private local RAW captures: CLRS-589 "Project Camera" for the Fuji
validation run, plus local Canon CR2 and Nikon NEF files for cross-maker regression.
Source RAW files are not distributed with this repository.

## Scope

The measurement covers RAW unpack and per-CFA-position statistics over the
visible active Bayer mosaic. It deliberately stops before demosaic, response
fitting, photon-transfer analysis, noise modeling, or color correction so the
sensor-domain baseline remains observable.

## Scientific Handling

- Input pixels come from LibRaw `rawdata.raw_image` after `unpack()`.
- Statistics use LibRaw `sizes.width` / `sizes.height` as the visible active
  area and start at `sizes.top_margin` / `sizes.left_margin`.
- Row stepping uses `sizes.raw_pitch / 2` when LibRaw reports a pitch, otherwise
  falls back to `raw_width` for tightly packed `uint16_t` raw buffers.
- Non-ordinary Bayer layouts are rejected for this phase (`filters < 1000`,
  including X-Trans, monochrome/full-color, and other special masks).
- Black subtraction uses the effective LibRaw pedestal:
  `black + cblack[color] + cblack tile`. The repeating `cblack[6..]` tile is
  indexed in active-area-local coordinates; margins move the raw pointer to the
  visible image, but do not shift the black-tile phase.
- Black level and pitch are read after pixel unpacking. A metadata-only archive
  inventory may see maker-dependent pre-unpack values and is therefore not the
  authority for scientific black subtraction.
- Reported `min`, `max`, `mean`, and `stddev` are signed black-subtracted
  residuals. Values below black are preserved, not clamped, so dark/noise
  analysis is not biased upward.
- Saturation is counted on the raw value before black subtraction:
  `raw >= white_level`.
- `near_ceiling_fraction` counts residuals at or above the shared project
  default of 98% of the signal-referred ceiling
  `white_level - black[p]`. A recorded response can plateau below the reported
  white level, so `saturated_fraction` can remain zero despite no measurable
  within-frame variation; the measured Fuji case is in
  [Near-ceiling plateau contrast](#near-ceiling-plateau-contrast). Use
  `near_ceiling_fraction` for headroom decisions and `saturated_fraction` only
  for exact-white accounting.
- The two statistics are nested rather than independent. The near-ceiling
  threshold is `black[p] + level * (white_level - black[p])`, which lies at or
  below `white_level` for any `level` at or under 1, and levels above 1 are
  rejected as undefined. A saturated pixel therefore always counts as
  near-ceiling, so `near_ceiling_fraction >= saturated_fraction` per plane
  wherever the threshold is defined. Their difference is a conservative
  near-ceiling margin band: pixels below `white_level` but above 98% of the
  signal-referred ceiling. That count is not, by itself, a measurement of a
  sensor plateau, clipping, or response compression; those interpretations
  require response-series or other independent evidence.
- The declared near-ceiling level is retained beside the derived plane
  fractions. Full-frame and CFA-balanced ROI measurements apply the same
  per-plane `white_level - black[p]` definition.

## Real-Data Validation Run

Result summary:

| Field | Value |
|---|---:|
| Camera | Fujifilm X-T100 |
| CFA | RGGB |
| ISO / aperture / shutter | ISO 200 / f9 / 0.01 s |
| Black level | 1024 DN |
| Black per CFA position | [1024, 1024, 1024, 1024] DN |
| White level | 16383 DN |
| Active area | 6016 x 4014 |
| Raw pitch | 12032 bytes |
| Total active pixels assigned | 24,148,224 |
| Signal-referred ceiling `white_level - black` | 15,359 DN |
| Saturated fraction | 0 on all four CFA positions |
| Near-ceiling fraction | 0 on all four CFA positions |

Per-position signed residual statistics:

| Channel | Count | Min | Max | Mean | Stddev | Below-black fraction |
|---|---:|---:|---:|---:|---:|---:|
| R | 6,037,056 | -22 | 7970 | 70.6725 | 101.0625 | 0.0002808 |
| G1 | 6,037,056 | -16 | 1380 | 117.9136 | 163.6677 | 0.0000182 |
| G2 | 6,037,056 | -13 | 1360 | 118.1097 | 163.9528 | 0.0000162 |
| B | 6,037,056 | -25 | 805 | 68.9922 | 96.6360 | 0.0001415 |

The two green positions agree closely (`G1` mean 117.9136 vs `G2` mean
118.1097), which is a basic sanity check that the RGGB demultiplexing phase is
correct for this zero-margin X-T100 capture.

This frame has real headroom — the largest residual on any position is 7970 DN
against a 15,359 DN ceiling — so both headroom statistics read zero and it does
not exercise the case `near_ceiling_fraction` exists for. The frame below does.

## Near-ceiling plateau contrast

Same camera, same black and white levels, and ISO 200, but a different target
and illumination. This integrating-sphere frame uses a 10× longer shutter
(0.1 s versus 0.01 s) and f/8 rather than f/9. It is not a controlled exposure
ratio against the CCSG frame above.

| Channel | Count | Min | Max | Mean | Stddev | Saturated fraction | Near-ceiling fraction |
|---|---:|---:|---:|---:|---:|---:|---:|
| R | 6,037,056 | 15357 | 15357 | 15357 | 0 | 0 | 1 |
| G1 | 6,037,056 | 15357 | 15357 | 15357 | 0 | 0 | 1 |
| G2 | 6,037,056 | 15357 | 15357 | 15357 | 0 | 0 | 1 |
| B | 6,037,056 | 15357 | 15357 | 15357 | 0 | 0 | 1 |

All 24,148,224 active pixels carry the identical residual 15357 DN, so the
per-position standard deviation is exactly zero: the recorded plateau retains
no within-frame spatial or tonal variation. `saturated_fraction` still reads 0
on every position, because the X-T100 pins at raw 16381 and `white_level` is
16383.

The plateau sits inside the band the two statistics bracket. With
`black = 1024` and `level = 0.98`, near-ceiling begins at raw
`1024 + 0.98 x 15359 = 16075.8`, and saturation begins at raw 16383:

```
  16075.8            16381          16383
  near-ceiling  <->  plateau   <->  white_level
       |---------------|--------------|
       |<-- 305.2 DN ->|<-- 2 DN ---->|
```

A headroom gate written against `saturated_fraction` accepts this frame; one
written against `near_ceiling_fraction` rejects it. That is the whole reason
the second statistic is reported.

## Cross-Maker Regression Checks

### Canon CR2 post-unpack black

Result summary:

| Field | Value |
|---|---:|
| Camera | Canon EOS 5D Mark II |
| CFA | GBRG |
| ISO / aperture / shutter | ISO 2000 / f8 / 0.002 s |
| Black level | 1023.75 DN |
| Black per CFA position | [1024, 1024, 1024, 1023] DN |
| White level | 15600 DN |
| Raw frame | 5792 x 3804 |
| Active area | 5634 x 3752 |
| Top / left margin | 52 / 158 |
| Raw pitch | 11584 bytes |
| Pixels per CFA position | 5,284,692 |

This file is the regression for maker metadata timing: reading metadata before
`unpack()` reported `black_level = 0` on this CR2, while the patched stats path
reads post-`unpack()` and subtracts the 1023.75 DN effective pedestal.

Per-position signed residual means after the fix:

| Channel | Mean | Min | Max | Saturated fraction | Near-ceiling fraction |
|---|---:|---:|---:|---:|---:|
| G1 | 12455.1633 | 4130 | 14740 | 0.0782861 | 0.1406362 |
| B | 5292.7913 | 2634 | 8113 | 0 | 0 |
| R | 7442.6776 | 3138 | 12624 | 0 | 0 |
| G2 | 12471.9586 | 5308 | 14740 | 0.0838083 | 0.1470595 |

This body is the counterexample to reading the Fuji result as a general rule.
The 5D Mark II does reach `white_level` exactly, so `saturated_fraction` is
informative here. The green planes report about 7.8–8.4% at exact white and
14.1–14.7% in the conservative near-ceiling band, but the difference is only a
count of samples in the top 2% of the signal-referred range. These frame
statistics alone cannot decide whether those samples are clipped, compressed,
or still response-bearing. The two statistics diverge on both bodies; only on
the Fuji does exact-white accounting collapse to zero.

### Nikon active-area crop

Result summary:

| Field | Value |
|---|---:|
| Camera | Nikon D800 |
| CFA | RGGB |
| ISO / aperture / shutter | ISO 100 / f5.6 / 0.025 s |
| Black level | 0 DN |
| Black per CFA position | [0, 0, 0, 0] DN |
| White level | 16383 DN |
| Raw frame | 7424 x 4924 |
| Active area | 7378 x 4924 |
| Raw pitch | 14848 bytes |
| Pixels per CFA position | 9,082,318 |

This file exercises the cropped-width path: the stats iterate `7378 x 4924`
active pixels, not the full `7424 x 4924` raw frame.

Per-position signed residual means:

| Channel | Mean | Min | Max | Saturated fraction | Near-ceiling fraction |
|---|---:|---:|---:|---:|---:|
| R | 646.6386 | 0 | 10203 | 0 | 0 |
| G1 | 901.0370 | 0 | 14043 | 0 | 0 |
| G2 | 902.6827 | 0 | 14091 | 0 | 0 |
| B | 437.2619 | 0 | 7042 | 0 | 0 |

This body reports `black_level = 0`, so the signal-referred ceiling equals
`white_level` at 16383 DN and near-ceiling begins at 16055.3 DN. The brightest
green residual is 14091, which is why both fractions read zero despite the
frame sitting within 15% of the ceiling.

## Interpretation limits

- Results are active-area CFA statistics, not demosaiced image quality,
  OECF/PTC, or a noise model.
- Full-frame masked pixels are excluded from active-image statistics.
- Only 2×2 Bayer mosaics are supported; X-Trans and other layouts require a
  separate sampling model.

## References

- LibRaw data-structure docs:
  <https://www.libraw.org/docs/API-datastruct-eng.html>. `raw_width` /
  `raw_height` describe the full RAW frame, `width` / `height` the visible
  area, and some fields are finalized during unpack.
- LibRaw `raw_image` forum guidance:
  <https://www.libraw.org/node/2504>. `raw_image` keeps masked pixels and
  should be cropped with `top_margin`, `left_margin`, `width`, `height`, and
  row pitch.
- LibRaw black-level forum guidance:
  <https://www.libraw.org/node/2565>. Effective black is additive across
  `black`, `cblack[color]`, and the optional `cblack[6..]` pattern.

## Metadata-stage boundary

The archive inventory reads metadata at file-open time. For makers that
finalize black level or pitch during unpacking, such as the Canon CR2 case
above, its recorded black level can be an open-stage placeholder. Scientific
pixel calculations therefore use the post-unpack value.

## Engineering companion

The [RAW implementation companion](../implementation/raw-foundation.md)
explains how this sensor-linear baseline is realized in C++ and routes readers
to the public source and tests.
