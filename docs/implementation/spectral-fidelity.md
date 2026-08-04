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
wavelength in the validated axis and refuses a broken count or non-contiguous
map before extraction. `extract_raw_spectral_response()` samples a
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

- The legacy parser validates its wavelength grid, finite response values, and
  positive line SPD. Closure validates aligned shapes and patch counts; the
  Luther fit validates minimum size and basis rank; the SMI-style path validates
  aligned lengths, enough colors, and positive white responses.
- A legacy spectral curve is labeled as a comparison reference, not silently
  promoted to a toolkit measurement.
- Same-session physical closure, cross-camera Luther residual, and SMI-style
  simulation remain separate result types.
- An incomplete camera set can participate in SSF-only comparison without being
  forced through unavailable chart closure.

## Verification evidence

At the library/unit layer, the spectral-response assertions are in
[`test_spectral_response.cpp`](../../tests/test_spectral_response.cpp).

The legacy parser fixture requires 48 samples from `360` through `830 nm` in
`10 nm` steps and pins the normalized green peak at `1.0` to `1e-12`. It
refuses wrong row counts, an axis gap, misaligned SPD, non-finite response, and
nonpositive SPD. A synthetic RAW sweep recovers normalized R/G/B of
`0.5/1.0/0.25` to `1e-12`, preserves basename-only provenance, and refuses a
fully clipped CFA channel. Partial clipping has a different, explicit contract:
one clipped red sample out of four is excluded, recorded as
`0.25 ± 1e-12`, its affected normalized red response remains `11/96` to
`1e-12`, and extraction continues. A red sample exactly at the measured dark
residual is separately classified as nonpositive signal, flagged at fraction
`1.0`, clamped to zero, and included in the one-sample diagnostic rollup.

The closure fixtures in
[`test_spectral_closure.cpp`](../../tests/test_spectral_closure.cpp) use
measured RGB exactly ten times the predicted RGB. They
must recover one global `k = 10`, zero white-ratio error, and zero per-channel
relative RMS to `1e-9`. A white mismatch fails before patch emission, and a
doubled red channel remains visible rather than being hidden by per-channel
scales. At the command/integration layer,
[`test_cmd_spectral_closure.cpp`](../../tests/test_cmd_spectral_closure.cpp)
also pins that saturation is evaluated before dark subtraction and that invalid
inputs produce no output.

For the Luther calculation,
[`test_spectral_quality.cpp`](../../tests/test_spectral_quality.cpp) gives an
overdetermined basis that produces residuals
`0, 1, 0` and combined residual `sqrt(1/3)` to `1e-9`; a rank-deficient basis
is refused. In that finite, nonzero, full-rank synthetic basis, multiplying all
SSF channels by the positive factor `1e-8` preserves every component residual
and the combined residual to `1e-12`; multiplying the channels independently by
the positive factors `1e-6`, `1e3`, and `7` preserves the combined residual to
`1e-12`. These fixtures pin invariance of this normalized subspace metric under
those rescalings; they do not make extracted SSF amplitudes or physical closure
scale-invariant.
The ideal fixture in
[`test_spectral_smi.cpp`](../../tests/test_spectral_smi.cpp) retains six colors,
produces mean Delta E 76 near zero and both scores near `100` under their
declared tolerances, while a wavelength-shifted metameric fixture must score
below `100`. The equation
`100 - 5.5 × mean Delta E 76` is pinned to `1e-9`, and the separate
white-preserving fit must keep white error at zero to `1e-9`.

At the generated-artifact layer, the registered CIE table guard and its
mutation test—
[`check_cie_cmf_1nm.py`](../../tools/check_cie_cmf_1nm.py) and
[`test_check_cie_cmf_1nm.py`](../../tools/test_check_cie_cmf_1nm.py)—pin all
seven official and derived hashes, the 360–830 nm
observer extent, the `ȳ` peak of `1.0` at 555 nm to `1e-9`, and declared
observer/illuminant subset tolerances; each registered table is mutated in turn
and required to fail. At the command/integration layer, command tests exercise
all four typed stages and pin their stage identities and selected output fields.
The SMI command additionally pins its
arbitrary-test-set, Annex-B, and white-preserving limitations; equivalent
interpretive text is not claimed as serialized by the other three commands.

The library fixtures establish the numerical stages, command tests establish
their orchestration and serialization, and the table guard establishes the
committed reference-data contract. These checks do not establish that an
archived sensitivity curve is physically correct, that two archive sessions
form a valid closure pair, or that the SMI-style approximation is bit-exact ISO
17321; those questions remain explicit in the scientific report.

## Source and tests

- SSF extraction: [`spectral_response.hpp`](../../include/camera_iq/spectral_response.hpp),
  [`spectral_response.cpp`](../../src/spectral_response.cpp)
- Physical closure: [`spectral_closure.hpp`](../../include/camera_iq/spectral_closure.hpp),
  [`spectral_closure.cpp`](../../src/spectral_closure.cpp)
- Luther fit: [`spectral_quality.hpp`](../../include/camera_iq/spectral_quality.hpp),
  [`spectral_quality.cpp`](../../src/spectral_quality.cpp)
- SMI-style analysis: [`spectral_smi.hpp`](../../include/camera_iq/spectral_smi.hpp),
  [`spectral_smi.cpp`](../../src/spectral_smi.cpp)
- Command adapters: [`cmd_spectral_response.cpp`](../../src/cmd_spectral_response.cpp),
  [`cmd_spectral_closure.cpp`](../../src/cmd_spectral_closure.cpp),
  [`cmd_spectral_quality.cpp`](../../src/cmd_spectral_quality.cpp), and
  [`cmd_spectral_smi.cpp`](../../src/cmd_spectral_smi.cpp)
- Focused tests: [`test_spectral_response.cpp`](../../tests/test_spectral_response.cpp),
  [`test_spectral_closure.cpp`](../../tests/test_spectral_closure.cpp),
  [`test_spectral_quality.cpp`](../../tests/test_spectral_quality.cpp), and
  [`test_spectral_smi.cpp`](../../tests/test_spectral_smi.cpp)
- Command and serialization tests:
  [`test_cmd_spectral_response.cpp`](../../tests/test_cmd_spectral_response.cpp),
  [`test_cmd_spectral_closure.cpp`](../../tests/test_cmd_spectral_closure.cpp),
  [`test_cmd_spectral_quality.cpp`](../../tests/test_cmd_spectral_quality.cpp), and
  [`test_cmd_spectral_smi.cpp`](../../tests/test_cmd_spectral_smi.cpp)
- Reference-table integrity:
  [`check_cie_cmf_1nm.py`](../../tools/check_cie_cmf_1nm.py) and
  [`test_check_cie_cmf_1nm.py`](../../tools/test_check_cie_cmf_1nm.py)
