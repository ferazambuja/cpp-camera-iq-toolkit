# Spectroradiometer recovery and analysis implementation

[Implementation index](README.md) ·
[case study](../case-studies/spectroradiometer-ingest.md) ·
[scientific report](../reports/SPECTRORADIOMETER_INGEST.md) ·
[aggregate data](../data/spectro_group_summary.csv)

## Software boundary

This pipeline reads the subset of MATLAB v5 needed by the retained
spectroradiometer files, converts each measurement struct to a typed record,
resolves aliases by exact content identity, joins readings to a declared ledger,
and computes level, normalized-shape, chromaticity, and same-record XYZ closure
as separate outputs.

## Code-level data flow

```text
configured archive root + identity ledger
  -> bounded file discovery and exact-byte digest
  -> read_mat_struct()
  -> spectro_measurement_from_mat()
  -> alias collapse and ledger group join
  -> IngestedSpectroGroup[]
  -> analyze_spectro_group()
  -> compute_spectro_closure()
  -> JSON + aggregate CSV + figure
```

## MATLAB parser and typed measurement

`read_mat_struct()` parses little-endian MATLAB v5 elements, including compressed
elements, numeric arrays, character values, and nested structs needed by this
archive. Parsing uses checked dimensions, bounded decompression, depth limits,
and exact element widths before allocation.

`spectro_measurement_from_mat()` then enforces the measurement contract:

- wavelength and spectral-radiance vectors have equal length;
- the wavelength axis is finite and strictly increasing;
- spectrum and recorded XYZ values are finite;
- scalar/vector field shapes match the archive schema; and
- acquisition flags keep their declared types.

Negative spectral samples remain valid because instrument-floor noise can cross
zero. A group must share one exact wavelength grid before summary statistics are
calculated.

## Separating level, shape, and color

For reading `r` on a uniform wavelength grid with step `Delta lambda`, the
level calculation is:

```text
level_r = Delta lambda * sum_i radiance_r[i]
```

The implementation uses compensated summation and exponent scaling so a finite,
representable answer is preserved across a large numeric range. A non-positive
integral is refused because it cannot normalize a spectrum.

Shape is calculated from:

```text
normalized_r[i] = radiance_r[i] / level_r

shape_residual_r =
  ||normalized_r - mean_normalized||_2 / ||mean_normalized||_2
```

For repeated groups, level variation uses sample standard deviation (`n - 1`)
and coefficient of variation `s / mean`. Singletons have no repeatability
statistics rather than a zero standard deviation.

Recorded XYZ is converted to chromaticity with:

```text
x  = X / (X + Y + Z)
y  = Y / (X + Y + Z)
u' = 4X / (X + 15Y + 3Z)
v' = 9Y / (X + 15Y + 3Z)
```

The group records the maximum pairwise distance in `u'v'` space. Recorded CCT
and Duv pass through as metadata because the source does not retain enough
information to identify their original conventions.

## Same-record XYZ closure

`compute_spectro_closure()` integrates the recorded spectrum against a public
CIE observer table using the same equal sample weights. One proportional scale
is fitted across every reading and XYZ channel, then signed relative residuals
are emitted. The scale is explicitly archive-derived; it is not labeled as a
standard luminous-efficacy constant or proof of undocumented instrument
software behavior.

## Identity, failure, and serialization

- The ledger, not filename similarity, assigns measurement groups.
- Exact-byte aliases are retained as provenance but analyzed once.
- Paths must remain below the configured root and serialize as relative labels.
- A ledger mismatch, ambiguous source, nonuniform grid, shape mismatch, or
  non-finite derived value is a refusal, not a partial group.
- Typed outputs distinguish a singleton from repeated-data statistics that
  happen to be numerically small.

## Source and tests

- MATLAB parser: [`mat_file.hpp`](../../include/camera_iq/mat_file.hpp),
  [`mat_file.cpp`](../../src/mat_file.cpp)
- Measurement schema and grouping:
  [`spectro_measurement.hpp`](../../include/camera_iq/spectro_measurement.hpp),
  [`spectro_measurement.cpp`](../../src/spectro_measurement.cpp),
  [`spectro_ingest.cpp`](../../src/spectro_ingest.cpp),
  [`spectro_ledger.cpp`](../../src/spectro_ledger.cpp)
- Analysis and closure: [`spectro_analysis.cpp`](../../src/spectro_analysis.cpp),
  [`spectro_colorimetry.cpp`](../../src/spectro_colorimetry.cpp)
- Focused tests: [`test_mat_file.cpp`](../../tests/test_mat_file.cpp),
  [`test_spectro_measurement.cpp`](../../tests/test_spectro_measurement.cpp),
  [`test_spectro_ingest.cpp`](../../tests/test_spectro_ingest.cpp),
  [`test_spectro_analysis.cpp`](../../tests/test_spectro_analysis.cpp), and
  [`test_spectro_colorimetry.cpp`](../../tests/test_spectro_colorimetry.cpp)
