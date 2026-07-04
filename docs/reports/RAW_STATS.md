# Evidence Report - Raw CFA Statistics

Date: 2026-07-02
Tool: `camera_iq raw-stats` (this repository, v0.1.0)
Dataset: private local RAW captures: CLRS-589 "Project Camera" for the Fuji
validation run, plus local Canon CR2 and Nikon NEF files for cross-maker regression.
Source RAW files are not distributed with this repository.

## Scope

This slice implements RAW unpack plus per-CFA-position statistics over the
visible active Bayer mosaic. It does not implement demosaic, OECF, PTC, noise
modeling, or color correction yet.

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
- `raw-stats` reads black level and pitch after `unpack()`. The `manifest`
  command is intentionally metadata-only and may report maker-dependent
  open-file black/pitch values; it must not be used as the authority for
  scientific black subtraction.
- Reported `min`, `max`, `mean`, and `stddev` are signed black-subtracted
  residuals. Values below black are preserved, not clamped, so dark/noise
  analysis is not biased upward.
- Saturation is counted on the raw value before black subtraction:
  `raw >= white_level`.

## Real-Data Validation Run

Command:

```bash
./build/camera_iq raw-stats \
  --dataset clrs589_project_camera \
  "Images/CCSG/CCSG_f9.0_1:100_ISO200_DSCF0299.RAF" \
  --out out/ccsg_f9_1_100_raw_stats.json
```

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
| Saturated fraction | 0 on all four CFA positions |

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

## Cross-Maker Regression Checks

### Canon CR2 post-unpack black

Command:

```bash
./build/camera_iq raw-stats \
  --dataset canon_5d2_repro \
  "Capture/DSLR_White.CR2" \
  --out out/canon_5d2_white_raw_stats.json
```

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

| Channel | Mean | Min | Max | Saturated fraction |
|---|---:|---:|---:|---:|
| G1 | 12455.1633 | 4130 | 14740 | 0.0782861 |
| B | 5292.7913 | 2634 | 8113 | 0 |
| R | 7442.6776 | 3138 | 12624 | 0 |
| G2 | 12471.9586 | 5308 | 14740 | 0.0838083 |

### Nikon active-area crop

Command:

```bash
./build/camera_iq raw-stats \
  --dataset d800_oecf_2016 \
  "NIKON D800_i100_s1-40_8.NEF" \
  --out out/nikon_d800_i100_s1_40_8_raw_stats.json
```

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

| Channel | Mean | Min | Max | Saturated fraction |
|---|---:|---:|---:|---:|
| R | 646.6386 | 0 | 10203 | 0 |
| G1 | 901.0370 | 0 | 14043 | 0 |
| G2 | 902.6827 | 0 | 14091 | 0 |
| B | 437.2619 | 0 | 7042 | 0 |

## Validation

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Current repository gate after later Evidence and data-privacy hardening:
16/16 CTest tests passed, including public-path and sample-fixture guards.

## Not Claimed

- No demosaiced image quality claim.
- No OECF/PTC/noise-model claim yet.
- No claim that raw full-frame masked pixels are part of the active image stats.
- No support yet for X-Trans or other non-2x2 Bayer mosaics.

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

## Manifest Note

`manifest` remains an open-file metadata inventory command. For makers that
finalize black level or pitch during `unpack()`, such as the Canon CR2
regression above, its `black_level` can be an open-stage placeholder; use
`raw-stats` for pixel-correct black subtraction.
