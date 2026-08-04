# Bilinear demosaic: method and validation

A Bayer sensor records only one color value at each pixel; demosaicing estimates
the two missing color components. This report documents a deliberately simple
bilinear baseline so later image-quality measurements can distinguish sensor
data from interpolation behavior. The goal is transparency and cross-camera
correctness, not production rendering quality.

Analysis date: 2026-07-02
Dataset: private local RAW captures used only for validation. Source RAW files
are not distributed with this repository.
## Scope

The analysis uses a transparent hand-written demosaic:

- Bilinear interpolation over the active, black-subtracted Bayer mosaic.
- RGGB-family phase support from the decoded CFA layout; the measurement does
  not assume one fixed Bayer origin.
- Edge pixels average only same-color neighbors that exist inside bounds.
- The reported output is RGB summary statistics rather than a rendered image.

## Scientific Handling

- Input CFA samples are decoded, cropped to the visible active area, and
  black-subtracted with the same effective per-position pedestal used by the
  common RAW measurement pipeline.
- **Black-level source.** An earlier manifest pass anticipated deriving black
  from the 21 dark frames after the decoder's scalar metadata reported zero for
  the Fujifilm X-T100. The effective repeating per-position metadata instead
  recovers the approximately 1024 DN pedestal and agrees with the X-T100
  dark-frame mean of 1023.99. This is adequate for a DN-space demosaic preview,
  and the implemented
  [dark-calibration analysis](DARK_CALIBRATION.md) reconciles it against the
  CLRS-589 X-T100 dark frames. It is still not a
  substitute for camera-by-camera dark-current/noise modeling: the Nikon D800
  above reports black `[0,0,0,0]`, which is not validated by this CLRS-only dark
  reconciliation.
- Demosaic operates in sensor DN residual space. There is no white balance,
  color matrix, gamma, exposure scaling, clipping, or output color-space
  conversion.
- Each missing RGB component is the arithmetic mean of same-component samples
  in the local 3x3 neighborhood. Known components are preserved.
- Negative residuals are preserved in the reported statistics. The independent
  LibRaw comparison uses an unsigned image representation, so signed residuals
  are clipped to zero for that comparison only.

## Real-Data Validation Runs

### Fujifilm X-T100 RAF

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

A separate LibRaw bilinear result provides an independent numerical
cross-check on all three files. Because LibRaw represents the interpolated
image as unsigned values, the signed residuals in this analysis are clipped to
zero for the comparison only.

| File | Mean abs diff R/G/B | Max abs diff R/G/B |
|---|---:|---:|
| Fuji X-T100 RAF | 0.2188 / 0.1874 / 0.2187 DN | 10 / 3.5 / 9.5 DN |
| Canon 5D2 CR2 | 0.2187 / 0.1854 / 0.2187 DN | 0.75 / 0.75 / 0.75 DN |
| Nikon D800 NEF | 0.2171 / 0.1875 / 0.2132 DN | 0.75 / 0.75 / 0.75 DN |

Interpretation: the core bilinear interpolation agrees with LibRaw to sub-DN
mean error on all three makers. Canon and Nikon are within rounding tolerance
at every checked pixel. Fuji has a few larger differences, so this report does
not claim bit-exact LibRaw equivalence for every camera; it claims a transparent
hand-written bilinear demosaic whose measured non-negative output is
cross-checked against LibRaw under the comparison boundary above. The
consistent ~0.19–0.22 DN mean offset follows systematic unsigned-integer
truncation in LibRaw's comparison buffer, not random disagreement.

Scope of this agreement: because LibRaw's buffer is unsigned, the comparison
clips this tool's signed residuals to zero. It therefore validates only the
non-negative region — it does **not** exercise the negative-residual behavior
that is this tool's actual point of difference from LibRaw. The
[implementation companion](../implementation/raw-foundation.md) documents the
separate numerical verification of signed residual handling.

## Interpretation limits

- The result covers channel summaries rather than a complete rendered image;
  white balance, a color-correction matrix, tone/gamma, and perceptual quality
  are outside its scope. Only 2×2 Bayer mosaics are supported.
- **Black-level provenance.** See `DARK_CALIBRATION.md` for the CLRS-589
  dark-frame reconciliation. Camera-by-camera dark-current/noise modeling still
  requires matched calibration captures.

## Engineering companion

The [RAW implementation companion](../implementation/raw-foundation.md)
explains how this baseline is realized in C++ and routes readers to the public
source and tests. The scientific relationship to the black pedestal is
documented in the [dark-calibration report](DARK_CALIBRATION.md).
