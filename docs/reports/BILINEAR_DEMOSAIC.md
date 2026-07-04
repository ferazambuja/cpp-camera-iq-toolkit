# Evidence Report - Bilinear Demosaic

Date: 2026-07-02
Tool: `camera_iq demosaic` (this repository, v0.1.0)
Dataset: private local RAW captures used only for validation. Source RAW files
are not distributed with this repository.

## Scope

This slice adds the first hand-written demosaic:

- `demosaic_bilinear()` over the active, black-subtracted Bayer mosaic.
- RGGB-family phase support via LibRaw `COLOR()` positions plus `cdesc`; the
  implementation does not assume one fixed RGGB origin.
- Edge pixels average only same-color neighbors that exist inside bounds.
- `camera_iq demosaic <raw> --out <json>` emits RGB summary statistics only;
  it does not write full images yet.

## Scientific Handling

- Input CFA samples are copied after LibRaw `unpack()`, cropped to
  `sizes.width` / `sizes.height`, and black-subtracted with the same effective
  per-position black levels used by `raw-stats`.
- **Black-level source (deviation from the Evidence plan).** Evidence anticipated
  deriving black from the 21 dark frames after finding LibRaw's *scalar* `black`
  reports 0 for the X-T100. This slice instead reads the LibRaw `cblack` **tile**
  via `effective_black_levels()`, which correctly recovers the ~1024 DN pedestal
  the scalar hides (Fuji matches the Evidence dark-frame mean of 1023.99). This is
  adequate for a DN-space demosaic preview, and the later dark-calibration slice
  reconciles it against the CLRS-589 X-T100 dark frames. It is still not a
  substitute for camera-by-camera dark-current/noise modeling: the Nikon D800
  above reports black `[0,0,0,0]`, which is not validated by this CLRS-only dark
  reconciliation.
- Demosaic operates in sensor DN residual space. There is no white balance,
  color matrix, gamma, exposure scaling, clipping, or output color-space
  conversion.
- Each missing RGB component is the arithmetic mean of same-component samples
  in the local 3x3 neighborhood. Known components are preserved.
- Negative residuals are preserved in this tool's output statistics. LibRaw's
  interpolated `image` buffer is unsigned, so LibRaw comparisons clip this
  tool's signed values to zero only for comparison.

## Synthetic Validation

`tests/test_demosaic.cpp` covers:

- Constant per-channel fields reconstruct to constant RGB at interior and edge
  pixels.
- Hand-computed 5x5 RGGB interpolation at red, blue, and both green positions.
- Edge handling on a 3x3 mosaic.
- Non-RGGB phase handling using BGGR.
- RGB summary-statistics labels/counts/means/stddev.
- CLI argument validation for `cmd_demosaic`.

## Real-Data Validation Runs

### Fujifilm X-T100 RAF

```bash
./build/camera_iq demosaic \
  --dataset clrs589_project_camera \
  "Images/CCSG/CCSG_f9.0_1:100_ISO200_DSCF0299.RAF" \
  --out out/ccsg_f9_1_100_demosaic.json
```

| Field | Value |
|---|---:|
| Camera | Fujifilm X-T100 |
| CFA | RGGB |
| Active area | 6016 x 4014 |
| Pixels | 24,148,224 |
| Black per CFA position | [1024, 1024, 1024, 1024] DN |

| Channel | Mean | Min | Max | Stddev |
|---|---:|---:|---:|---:|
| R | 70.6738 | -22 | 7970 | 100.8351 |
| G | 118.0117 | -16 | 1380 | 163.6354 |
| B | 68.9906 | -25 | 805 | 96.4263 |

### Canon EOS 5D Mark II CR2

```bash
./build/camera_iq demosaic \
  --dataset canon_5d2_repro \
  "Capture/DSLR_White.CR2" \
  --out out/canon_5d2_white_demosaic.json
```

| Field | Value |
|---|---:|
| Camera | Canon EOS 5D Mark II |
| CFA | GBRG |
| Active area | 5634 x 3752 |
| Pixels | 21,138,768 |
| Top / left margin | 52 / 158 |
| Black per CFA position | [1024, 1024, 1024, 1023] DN |

| Channel | Mean | Min | Max | Stddev |
|---|---:|---:|---:|---:|
| R | 7443.0862 | 3138 | 12624 | 946.0173 |
| G | 12463.5602 | 4130 | 14740 | 1607.5545 |
| B | 5292.5029 | 2634 | 8113 | 680.3678 |

### Nikon D800 NEF

```bash
./build/camera_iq demosaic \
  --dataset d800_oecf_2016 \
  "NIKON D800_i100_s1-40_8.NEF" \
  --out out/nikon_d800_i100_s1_40_8_demosaic.json
```

| Field | Value |
|---|---:|
| Camera | Nikon D800 |
| CFA | RGGB |
| Active area | 7378 x 4924 |
| Pixels | 36,329,272 |
| Raw frame | 7424 x 4924 |
| Black per CFA position | [0, 0, 0, 0] DN |

| Channel | Mean | Min | Max | Stddev |
|---|---:|---:|---:|---:|
| R | 646.6387 | 0 | 10203 | 1594.2109 |
| G | 901.8599 | 0 | 14091 | 2222.7044 |
| B | 437.2618 | 0 | 7042 | 1086.6763 |

## LibRaw Comparison

`tools/libraw_bilinear_compare.cpp` compared this implementation to LibRaw
`raw2image_ex(1)` followed by `lin_interpolate()`, with matching dimensions on
all three files above. Because LibRaw's image buffer is unsigned, this tool's
signed residuals are clipped to zero for the comparison only.

Build command used locally:

```bash
c++ -std=c++20 -Iinclude $(pkg-config --cflags libraw) \
  tools/libraw_bilinear_compare.cpp build/libcamera_iq_core.a \
  $(pkg-config --libs libraw) -o /tmp/libraw_bilinear_compare
```

On this macOS/Homebrew machine, the compile also needed the local libomp include
and library paths from `brew --prefix libomp`.

| File | Mean abs diff R/G/B | Max abs diff R/G/B |
|---|---:|---:|
| Fuji X-T100 RAF | 0.2188 / 0.1874 / 0.2187 DN | 10 / 3.5 / 9.5 DN |
| Canon 5D2 CR2 | 0.2187 / 0.1854 / 0.2187 DN | 0.75 / 0.75 / 0.75 DN |
| Nikon D800 NEF | 0.2171 / 0.1875 / 0.2132 DN | 0.75 / 0.75 / 0.75 DN |

Interpretation: the core bilinear interpolation agrees with LibRaw to sub-DN
mean error on all three makers. Canon and Nikon are within rounding tolerance
at every checked pixel. Fuji has a few larger differences, so this report does
not claim bit-exact LibRaw equivalence for every camera; it claims a transparent
hand-written bilinear demosaic whose behavior is defined by the synthetic tests
above and sanity-checked against LibRaw. The consistent ~0.19–0.22 DN mean
offset is systematic uint16 truncation in LibRaw's unsigned `image` buffer, not
random disagreement.

Scope of this agreement: because LibRaw's buffer is unsigned, the comparison
clips this tool's signed residuals to zero. It therefore validates only the
non-negative region — it does **not** exercise the negative-residual behavior
that is this tool's actual point of difference from LibRaw. Negative residual
handling is covered by the synthetic tests, not by the LibRaw comparison.

## Validation

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Current repository gate after later Evidence and data-privacy hardening:
16/16 CTest tests passed, including public-path and sample-fixture guards.

## Not Claimed

- No full image export yet.
- No colorimetric output claim.
- No white balance, CCM, gamma, tone curve, or perceptual image quality claim.
- No X-Trans or non-2x2 Bayer demosaic support.

## Known Limitations (carried forward)

- **Memory.** `camera_iq demosaic` materializes the entire RGB image
  (~872 MB for a 36 MP Nikon frame, plus the CFA sample copy) only to emit
  three channel summaries. Acceptable one-shot; the batch runner in a later
  phase should stream per-pixel statistics instead of holding the full image.
- **Black-level provenance.** See `DARK_CALIBRATION.md` for the CLRS-589
  dark-frame reconciliation. Camera-by-camera dark-current/noise modeling still
  belongs to later objective-IQ phases.
