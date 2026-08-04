# Slanted-edge SFR and field-summary implementation

[Implementation index](README.md) ·
[case study](../case-studies/sfr-mtf-aperture-field.md) ·
[scientific report](../reports/SFR_MTF.md) ·
[archive contract](../reports/SFR_MTF_ARCHIVE_INVENTORY.md)

## Software boundary

The SFR core measures a slanted edge directly from the black-subtracted Bayer
mosaic. It uses only green CFA samples, avoiding demosaic, gamma, sharpening,
and other spatial processing that would otherwise become part of the measured
response. A separate command layer resolves datasets, reads retained advisory
tables, converts full-frame coordinates to active-area coordinates, and writes
single-region or field-map JSON.

## Code-level data flow

```text
dataset ID + RAW + ROI or retained field table
  -> read_raw_cfa_image()
  -> full_frame_roi_to_active_area()
  -> analyze_green_sfr()
       -> quality gates
       -> per-scan-line edge centers
       -> fitted edge line
       -> oversampled ESF bins
       -> adjacent-difference LSF
       -> DFT magnitude and correction
       -> MTF50, MTF50P, MTF at Nyquist, R10-90
  -> SfrResult or field collection
  -> JSON
  -> aggregate CSV and SVG generator
```

## Edge estimation

`analyze_green_sfr()` first converts the requested rectangle to a CFA-balanced
ROI. It refuses invalid options, undersized regions, non-finite buffers, weak
contrast, excessive near-saturation, and edges outside the declared angle
range.

For each scan line, the implementation:

1. collects green samples in spatial order;
2. estimates the two plateau levels from the first and last quarters;
3. finds the strongest crossing of their midpoint;
4. refines the edge position with an absolute-gradient centroid in an 8 px
   window; and
5. fits edge position against scan-line position by least squares.

The fitted slope establishes edge orientation and angle. Each sample is then
projected to signed distance from that line and accumulated into 0.25 px bins by
default, producing four-times oversampling.

## ESF to MTF mapping

The canonical equations and symbol definitions are in the scientific report's
[measurement model](../reports/SFR_MTF.md#measurement-model). The code realizes
that model as:

```text
LSF[i] = ESF[i + 1] - ESF[i]

Hamming[i] = 0.54 - 0.46 cos(2 pi i / (N - 1))

MTF_raw[k] = abs(DFT(LSF[i] * Hamming[i])[k])

adjacent_difference_response(f, dx)
  = sin(pi * f * dx) / (pi * f * dx)

MTF_corrected(f) = normalized_MTF_raw(f) /
                   adjacent_difference_response(f, dx)
```

Here `dx` is the ESF bin spacing in pixels and `f` is cycles per pixel. The DFT
is an explicit deterministic implementation, making the transform easy to
inspect. Crossing frequencies are linearly interpolated. MTF50 is the first
falling crossing of 0.5; MTF at Nyquist is interpolated at 0.5 cycles per pixel.

## Typed results and field summaries

`SfrResult` carries both measurement values and rejection diagnostics. Metrics
that exist only after a successful analysis serialize as `null` on rejection;
contrast, saturation fraction, ROI, and the rejection reason remain available.

Retained Imatest tables are parsed into `ImatestYMultiFile` and
`ImatestYMultiRoi`. Parsed edge IDs preserve physical-corner and physical-edge
labels rather than inferring them from array order. `summarize_imatest_field_mtf()`
then reports the center, field maximum, and physical-corner maximum. Aperture
trend logic is kept in a separate `SfrTrendResult` so field and center
conclusions cannot be merged accidentally.

## Failure behavior

- The algorithm refuses unsupported or malformed RAW buffers before sampling.
- Orientation is derived from the stronger gradient direction, then checked
  against the angle contract.
- Missing crossings do not become plausible-looking frequencies.
- Full-frame and active-area coordinate systems are converted explicitly.
- Advisory-tool values remain separate fields and are not substituted for the
  toolkit measurement.

## Verification evidence

Synthetic tests cover known edge orientations and blur, saturation and contrast
refusals, coordinate conversion, MTF summaries, and field labels. The archive
result still depends on the capture and pairing evidence documented in the
scientific report and inventory.

## Source and tests

- Public API: [`sfr.hpp`](../../include/camera_iq/sfr.hpp)
- Core analysis and advisory parsers: [`sfr.cpp`](../../src/sfr.cpp)
- Dataset, field-map, and JSON layer: [`cmd_sfr.cpp`](../../src/cmd_sfr.cpp)
- Core numeric tests: [`test_sfr.cpp`](../../tests/test_sfr.cpp)
- Command and output tests: [`test_cmd_sfr.cpp`](../../tests/test_cmd_sfr.cpp)
- Aggregate generation: [`generate_portfolio_figures.py`](../../tools/generate_portfolio_figures.py)
