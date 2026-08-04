# CFA flat-field response implementation

[Implementation index](README.md) ·
[case study](../case-studies/cfa-flat-field-response.md) ·
[scientific report](../reports/FLAT_FIELD_RESPONSE.md) ·
[screening data](../data/flat_field_summary.csv) ·
[response maps](../data/flat_field_response.csv)

## Software boundary

The flat-field pipeline measures spatial response on the black-subtracted Bayer
mosaic before demosaic. Red, both green positions, and blue remain independent
through screening and field measurement. The implementation can add dark-frame
controls and compare two accepted fields, but it does not currently apply a
correction to an independent scene.

## Code-level data flow

```text
dataset ID + flat RAW + options
  -> read_raw_cfa_image()
  -> signal-ceiling validation
  -> CFA-balanced whole-frame and center-gate screening
  -> block-grid measurement for R, G1, G2, B
  -> per-plane center normalization
  -> relative response and chromatic-ratio maps
  -> center-block and four corner-block scalars
  -> green corner-field asymmetry A
  -> optional dark-control and second-field comparison
  -> ShadingAnalysis / ShadingComparison
  -> JSON and long-form CSV
```

## Screening before normalization

`measure_cfa_near_ceiling()` counts finite samples and samples above the
declared fraction of the sensor signal ceiling in both the full frame and a
centered gate. Fractions are kept per CFA position. A frame is accepted only
when finite coverage and headroom pass for every position; one green plane
cannot be rescued by averaging it with the other.

This gate is shared by flat-field correction used in patch extraction. The
shared helper ensures the same source mosaic, CFA-balanced geometry, and
per-position rule are used by both consumers.

## Field calculation

`measure_shading_field()` divides the active area into a declared Cartesian
grid. Each valid bin stores the median for each CFA position. For plane `c`, the
relative field is:

```text
response_c(x,y) = median_c(x,y) / center_median_c
```

The scientific report defines the exact center and corner summaries. The code
keeps missing or invalid bins absent rather than filling them with zero.

Chromatic maps are derived only after spatial normalization:

```text
C_RG(x,y)   = response_R(x,y)  / response_G(x,y)
C_BG(x,y)   = response_B(x,y)  / response_G(x,y)
C_G1G2(x,y) = response_G1(x,y) / response_G2(x,y)
```

Completeness is an explicit result because a valid luminance-like map does not
guarantee that every chromatic ratio has a nonzero denominator.

## Asymmetry and dark controls

`ShadingBlocks` holds one median per CFA position for the center block and for
each of the four corner blocks, plus each corner's center-normalized value. The
corner blocks are `corner_block_px` squares inset by `corner_inset_px`; they are
deliberately not the grid's corner bins, which a 16x12 grid centers at 1/32 of
the frame width and which therefore never reach the corner.

`ShadingChromatic::green_asymmetry` reduces those four corner values to the
single scalar the scientific report defines, using the mean of the two green
planes at each corner. Keeping four named corners rather than a radial average
is what makes the statistic informative: the four blocks sit at equal radius, so
a centered radially symmetric field drives the spread to zero analytically, and
a nonzero value is a departure that a radial average would have erased.

Equal radius is a geometric precondition, so the code enforces it rather than
assuming it. Odd block and inset requests round inward to even values, and the
effective rectangles travel with the result in `ShadingGeometry`. An odd active
width or height is refused outright: mirrored corners cannot be exact on an odd
mosaic, and refusing is preferable to publishing an equal-radius claim the
geometry does not support.

Optional dark controls verify camera and exposure compatibility, finite
coverage, global pedestal residual, and center/corner residual before a dark
field can be used as supporting evidence. `compare_shading_fields()` requires
matching geometry and reports maximum and RMS corner changes; it does not infer
a cause from the difference.

## Serialization and refusal behavior

`ShadingAnalysis` separates geometry, gates, response blocks, chromatic blocks,
dark evidence, and interpretation scope. The serializers preserve three states:

- a measurement that exists and is finite;
- a scientifically undefined value, written as JSON `null` or blank CSV; and
- a stage not reached because an earlier gate failed.

Absolute input paths are reduced to dataset-relative labels. Output schemas
carry the effective thresholds and geometry so an aggregate cannot be detached
from the policy that produced it.

## Source and tests

- Public types and analysis API: [`shading.hpp`](../../include/camera_iq/shading.hpp)
- Shared source-frame gate: [`flat_field_gate.hpp`](../../include/camera_iq/flat_field_gate.hpp),
  [`flat_field_gate.cpp`](../../src/flat_field_gate.cpp)
- Field analysis and serializers: [`shading.cpp`](../../src/shading.cpp)
- Command orchestration: [`cmd_shading.cpp`](../../src/cmd_shading.cpp)
- Core and command tests: [`test_shading.cpp`](../../tests/test_shading.cpp),
  [`test_flat_field_gate.cpp`](../../tests/test_flat_field_gate.cpp),
  [`test_cmd_shading.cpp`](../../tests/test_cmd_shading.cpp)
- Producer-to-consumer contract fixture:
  [`emit_shading_contract.cpp`](../../tests/emit_shading_contract.cpp)

Synthetic tests exercise symmetric and asymmetric fields, CFA independence,
coverage and clipping gates, zero denominators, dark controls, comparison
geometry, privacy, and serialization. The scientific report remains the source
for the archive measurements and capture-system interpretation.
