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

The library accepts the caller's screening rectangle only when it is exactly
equal to `cfa_balanced_roi(gate, width, height)`. Because that helper clips to
the image and rounds inward to complete CFA blocks, equality proves both
in-frame containment and CFA balance; there is no separate containment path.
The exporter's later center-block-inside-gate check is a distinct document-
geometry contract and does not rerun this source-image proof.

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
response_G(x,y) = (response_G1(x,y) + response_G2(x,y)) / 2
C_RG(x,y)   = response_R(x,y)  / response_G(x,y)
C_BG(x,y)   = response_B(x,y)  / response_G(x,y)
C_G1G2(x,y) = response_G1(x,y) / response_G2(x,y)
```

Completeness is an explicit result because a valid luminance-like map does not
guarantee that every chromatic ratio has a nonzero denominator.

## Asymmetry and dark controls

`ShadingField::center_block_median` holds one center median per CFA position.
`ShadingBlocks` holds the four corner medians and their center-normalized
values. The
corner blocks are `corner_block_px` squares inset by `corner_inset_px`; they are
deliberately not the grid's corner bins, which a 16x12 grid centers at 1/32 of
the frame width and which therefore never reach the corner.

`ShadingChromatic::green_asymmetry` reduces those four corner values to the
single scalar the scientific report defines, using the mean of the two green
planes at each corner. Keeping four named corners rather than a radial average
is what makes the statistic informative: in a continuous centered radial field,
the four equal-radius locations have identical response and therefore zero
spread. The finite CFA/block estimator has a small fixture-dependent sampling
residual, so a nonzero measurement by itself does not establish a physical
departure. The four-corner statistic becomes informative when its magnitude is
materially above that estimator residual; a radial average would erase the
directional pattern either way.

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
dark evidence, and interpretation scope. JSON preserves undefined measured
values as `null` and carries the effective rectangles. The long-form CSV uses
blank diagnostic fields when a pre-measurement gate was not reached, omits
undefined map-bin rows, and emits no response or chromatic rows for a rejected
frame. It carries effective options and gate diagnostics, but not the JSON
geometry object.

Existing relative labels pass through; absolute primary and dark inputs are
reduced to basename-only publication labels. Effective thresholds remain in the
outputs so an aggregate cannot be detached from the policy that produced it.

## Verification evidence

At the library/unit layer, the primary scalar and geometry assertions are in
[`test_shading.cpp`](../../tests/test_shading.cpp).

Other synthetic fields in
[`test_shading.cpp`](../../tests/test_shading.cpp) distinguish a centered radial
response from a four-corner asymmetry and keep R, G1, G2, and B independent.

For the sampled centered-radial fixture and its declared CFA/block geometry,
[`test_shading.cpp`](../../tests/test_shading.cpp) requires green asymmetry to
remain below `1e-3`; that is a fixture-specific discretization bound, not a
universal sampling floor, and the centered field must not trip the `0.05`
policy.
<!-- test-evidence: flat_field_radial_asymmetry -->

Gate fixtures in
[`test_flat_field_gate.cpp`](../../tests/test_flat_field_gate.cpp) require the
declared rectangle to equal its clipped, CFA-balanced form. Odd active-image
dimensions, an odd-origin gate, and a gate escaping the frame are refused
rather than silently trimmed or re-aligned.
<!-- test-evidence: flat_field_cfa_balanced_roi -->

Separate boundary fixtures in
[`test_flat_field_gate.cpp`](../../tests/test_flat_field_gate.cpp) verify that
exactly `1%` near-ceiling samples and exactly `90%` finite coverage pass, while
values beyond either inclusive boundary fail per CFA position.
<!-- test-evidence: flat_field_threshold_boundaries -->

Other fixtures cover zero chromatic denominators, dark metadata and pedestal
controls, and matched comparison geometry.

At the command/serialization layer,
[`test_cmd_shading.cpp`](../../tests/test_cmd_shading.cpp) checks
source-relative labels, output collisions, pedestal fields, and the JSON/CSV
rejection states described above. Those checks exercise the adapter and output
contract; they do not rerun an archive capture.

At the producer-to-consumer integration layer, the live check in
[`emit_shading_contract.cpp`](../../tests/emit_shading_contract.cpp) and
[`test_export_shading_portfolio.py`](../../tools/test_export_shading_portfolio.py)
executes the compiled C++ serializer and feeds its output to the Python
exporter. The exporter independently requires measured gates, even
CFA-balanced rectangles, a center contained in the gate, positive center
medians, four finite corner rows, and complete chromatic maps for every accepted
document. Detailed and response exports additionally require verified pedestal
evidence; screening inventory entries may defer that control to their matching
detailed evidence. The synthetic exporter corpus pins 52 unique inventory rows,
the `18/21/13` aperture census, three accepted frames, a comparison record that
must declare measured evidence, and one complete `16 × 12` response grid for
each accepted file. The archive-backed report establishes that the retained
comparison actually came from measured captures.
Mutation tests break the contract when producer or consumer semantics drift.

These library, command, and integration checks establish gate, schema,
join, and calculation behavior at their respective layers. The archive-backed
report supplies the physical-input evidence; none of these fixtures remeasure
the private RAW archive, prove the integrating sphere was uniform, or isolate a
physical cause for the measured field.

## Source and tests

- Public types and analysis API: [`shading.hpp`](../../include/camera_iq/shading.hpp)
- Shared source-frame gate: [`flat_field_gate.hpp`](../../include/camera_iq/flat_field_gate.hpp),
  [`flat_field_gate.cpp`](../../src/flat_field_gate.cpp),
  shared correction use in [`patches.cpp`](../../src/patches.cpp), and
  [`test_patches.cpp`](../../tests/test_patches.cpp)
- Field analysis: [`shading.cpp`](../../src/shading.cpp)
- Command orchestration and serializers:
  [`cmd_shading.cpp`](../../src/cmd_shading.cpp)
- Core and command tests: [`test_shading.cpp`](../../tests/test_shading.cpp),
  [`test_flat_field_gate.cpp`](../../tests/test_flat_field_gate.cpp),
  [`test_cmd_shading.cpp`](../../tests/test_cmd_shading.cpp)
- Producer, exporter, and live contract checks:
  [`emit_shading_contract.cpp`](../../tests/emit_shading_contract.cpp),
  [`export_shading_portfolio.py`](../../tools/export_shading_portfolio.py),
  [`check_schema_contract.py`](../../tools/check_schema_contract.py),
  [`test_check_schema_contract.py`](../../tools/test_check_schema_contract.py), and
  [`test_export_shading_portfolio.py`](../../tools/test_export_shading_portfolio.py)
