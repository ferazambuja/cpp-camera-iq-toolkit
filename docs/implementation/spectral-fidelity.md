# Spectral sensitivity and color-fidelity implementation

[Implementation index](README.md) ·
[case study](../case-studies/spectral-color-fidelity.md) ·
[scientific report](../reports/SPECTRAL_SENSITIVITY.md) ·
[archive contract](../reports/SPECTRAL_ARCHIVE_INVENTORY.md)

## Software boundary

The spectral pipeline has four distinct calculations: recover a camera spectral
sensitivity function (SSF), check it against same-session chart captures,
measure its Luther-condition residual against CIE color-matching functions, and
run an ISO 17321-style sensitivity-metamerism simulation. The stages share
spectral inputs but produce different evidence and are not collapsed into one
score.

## Code-level data flow

```text
validated wavelength axis + RAW sweep + dark RAW + ROI
  -> discover_spectral_sweep_files()
  -> read_raw_cfa_image() for every wavelength
  -> per-position dark residual subtraction
  -> CFA-direct R, G, B means with saturation/below-dark diagnostics
  -> green-peak normalization
  -> SpectralRawExtraction and SSF CSV

SSF + illuminant + chart reflectances + measured chart RGB
  -> common-grid alignment
  -> white-card pairing gate
  -> compute_spectral_closure()

SSF + CIE observer
  -> compute_spectral_quality()

SSF + observer + illuminant + test reflectances
  -> compute_spectral_smi()
```

## RAW sweep extraction

`discover_spectral_sweep_files()` requires one sorted RAW file for each
wavelength in the validated axis. Missing, duplicated, or extra positions are
refused before extraction. `extract_raw_spectral_response()` samples a
CFA-balanced ROI directly from the mosaic, subtracts a measured dark residual
per CFA position, combines the two greens only at the channel-summary stage,
and records saturation and below-dark fractions for every wavelength.

The response curves are normalized by a declared scalar convention. The
normalization changes scale, not shape; the original diagnostic values remain
available in the extraction object.

## Physical closure

For channel `c` and chart patch `p`, the core predicts relative camera response
from the measured SSF, illuminant, and reflectance:

```text
predicted[p,c] = sum_lambda
  SSF[c,lambda] * illuminant[lambda] * reflectance[p,lambda]
```

All vectors reach `compute_spectral_closure()` already aligned to one wavelength
grid. A white-card gate first compares predicted and measured `R/G` and `B/G`
ratios. If it passes, one global scale `k`—not one scale per channel—is fitted
across every patch and channel:

```text
k = arg min sum_p,c (measured[p,c] - k * predicted[p,c])^2
```

Per-channel correlation and relative RMS are then reported. Diagnostic
per-channel scale values remain visible but do not replace the global closure
fit.

## Luther-condition residual

`compute_spectral_quality()` asks how closely the CIE `x_bar`, `y_bar`, and
`z_bar` functions can each be written as a linear combination of the camera's
three SSFs. It solves three least-squares fits:

```text
CMF_j(lambda) approximately equals
  a_jR * SSF_R(lambda) + a_jG * SSF_G(lambda) + a_jB * SSF_B(lambda)
```

Each relative RMS residual is divided by the norm of the corresponding CMF.
The combined residual is the RMS across the three functions, and the companion
quality index is `1 - combined_residual`. Degenerate SSF bases are refused.

## SMI-style simulation

`compute_spectral_smi()` integrates reference XYZ and camera RGB for every test
reflectance under the same illuminant. It fits the best 3 by 3 camera-RGB-to-XYZ
matrix, converts reference and predicted XYZ to Lab, and reports Delta E 76 and
CIEDE2000 summaries. The declared approximation is:

```text
SMI = 100 - smi_slope * mean_DeltaE76
```

A second fit constrains the perfect-diffuser camera response to map to the
illuminant white. Both results are serialized so the optimization choice is
visible rather than hidden inside one rank.

## Invariants and evidence separation

- Every stage validates grid length, monotonicity, finite values, and aligned
  vector shapes.
- A legacy spectral curve is labeled as a comparison reference, not silently
  promoted to a toolkit measurement.
- Same-session physical closure, cross-camera Luther residual, and SMI-style
  simulation remain separate result types.
- An incomplete camera set can participate in SSF-only comparison without being
  forced through unavailable chart closure.

## Source and tests

- SSF extraction: [`spectral_response.hpp`](../../include/camera_iq/spectral_response.hpp),
  [`spectral_response.cpp`](../../src/spectral_response.cpp)
- Physical closure: [`spectral_closure.hpp`](../../include/camera_iq/spectral_closure.hpp),
  [`spectral_closure.cpp`](../../src/spectral_closure.cpp)
- Luther fit: [`spectral_quality.hpp`](../../include/camera_iq/spectral_quality.hpp),
  [`spectral_quality.cpp`](../../src/spectral_quality.cpp)
- SMI-style analysis: [`spectral_smi.hpp`](../../include/camera_iq/spectral_smi.hpp),
  [`spectral_smi.cpp`](../../src/spectral_smi.cpp)
- Focused tests: [`test_spectral_response.cpp`](../../tests/test_spectral_response.cpp),
  [`test_spectral_closure.cpp`](../../tests/test_spectral_closure.cpp),
  [`test_spectral_quality.cpp`](../../tests/test_spectral_quality.cpp), and
  [`test_spectral_smi.cpp`](../../tests/test_spectral_smi.cpp)
