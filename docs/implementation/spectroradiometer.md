# Spectroradiometer recovery and analysis implementation

[Implementation index](README.md) ·
[case study](../case-studies/spectroradiometer-ingest.md) ·
[scientific report](../reports/SPECTRORADIOMETER_INGEST.md) ·
[aggregate data](../data/spectro_group_summary.csv) ·
[result receipt](../data/spectro_result_receipt.json) ·
[MATLAB cross-check receipt](../data/spectro_matlab_crosscheck_receipt.json)

## Software boundary

This pipeline reads the subset of MATLAB v5 needed by the retained
spectroradiometer files, converts each measurement struct to a typed record,
joins readings and declared aliases to an identity ledger, optionally verifies
alias bytes against their canonical source,
and computes level, normalized-shape, chromaticity, and same-record XYZ closure
as separate outputs.

## Code-level data flow

```text
configured archive root + identity ledger
  -> resolve declared relative paths below the root + exact-byte digest
  -> read_mat_struct()
  -> spectro_measurement_from_mat()
  -> alias collapse and ledger group join
  -> IngestedSpectroGroup[]
  -> analyze_spectro_group()
  -> compute_spectro_closure()
  -> JSON + aggregate CSV + figure
```

## MATLAB parser and typed measurement

`read_mat_struct()` parses the flat little-endian MATLAB v5 numeric and logical
fields used by this archive, including compressed elements and mixed numeric
storage widths. Character arrays and nested structs are outside this parser's
public subset and are refused. Parsing uses checked dimensions, bounded
decompression and cumulative inflation, nesting limits, and exact element
widths before allocation.

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
- Declared aliases are retained as provenance but analyzed once; exact-byte
  equality is additionally checked when alias verification is enabled.
- Paths must remain below the configured root and serialize as relative labels.
- A ledger mismatch, ambiguous source, nonuniform grid, shape mismatch, or
  non-finite derived value is a refusal, not a partial group.
- Typed outputs distinguish a singleton from repeated-data statistics that
  happen to be numerically small.

## Verification evidence

The MAT-ingest assertions begin in
[`test_mat_file.cpp`](../../tests/test_mat_file.cpp).

Byte-built MATLAB v5 fixtures exercise compressed and uncompressed numeric
structs, padding, mixed numeric widths, compact tags, logical identity, and
bounded cumulative inflation. Malformed headers, versions, dimensions, lossy
integer widening, complex values, duplicate fields, character arrays, nested
structs, and excessive depth are refused.

Ledger tests in [`test_spectro_ledger.cpp`](../../tests/test_spectro_ledger.cpp)
preserve declared repeat-index order rather than filename order and refuse
duplicate canonical paths, duplicate content digests, ambiguous aliases,
traversal, malformed hashes, gaps, and silent regrouping. Ingest tests in
[`test_spectro_ingest.cpp`](../../tests/test_spectro_ingest.cpp) bind canonical
bytes to SHA-256, distinguish readings from aliases, refuse symlinks and
byte-limit violations, and verify alias bytes when requested.

At the library/unit layer, a two-reading fixture in
[`test_spectro_analysis.cpp`](../../tests/test_spectro_analysis.cpp) pins
spectral integral `8`, mean level `12`, and
coefficient of variation `sqrt(32) / 12`, each to `1e-12`; a pure scale change
must produce zero normalized-shape and chromaticity separation. Together these
assertions show that level, normalized spectral shape, and chromaticity remain
separate outputs rather than collapsing into one score.
<!-- test-evidence: spectroradiometer_scale_separation -->

The closure fixture in
[`test_spectro_colorimetry.cpp`](../../tests/test_spectro_colorimetry.cpp)
recovers one global scale `10` and zero maximum relative residual to
`1e-12`. High-range, cancellation, subnormal, and overflow-refusal cases keep
those numerical paths explicit. Command tests in
[`test_cmd_spectro_ingest.cpp`](../../tests/test_cmd_spectro_ingest.cpp) cover
JSON plus three CSV
outputs, privacy-safe labels, CSV quoting, vector hashes, source identities,
output collisions, and the ban on writing output inside the source root.

The retained runtime receipt and its public checker—
[`spectro_matlab_crosscheck_receipt.json`](../data/spectro_matlab_crosscheck_receipt.json)
and
[`check_spectro_matlab_crosscheck_receipt.py`](../../tools/check_spectro_matlab_crosscheck_receipt.py)—
record a MATLAB R2026a/C++ comparison over 89
private readings: 89 source-file SHA-256 comparisons bind identity, two
binary64 vector hashes per reading give 178 exact vector comparisons, and seven
numeric fields per reading give 623 comparisons at `1e-12`
absolute-or-relative tolerance. Public checks recompute the
`2 × reading_count` and `7 × reading_count` relationships, bind the receipt to
the 89-row ledger and public artifacts, and reject changed identities, hashes,
counts, or tolerance outcomes. Public checks cannot rerun the private export or
the MATLAB/C++ comparison. When both retained CSVs are supplied, the receipt
checker additionally verifies their hashes.

This evidence supports parser and calculation agreement on the retained
archive. It does not prove physical scene stability, instrument accuracy, or
the undocumented CCT and Duv conventions.

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
  [`test_spectro_ledger.cpp`](../../tests/test_spectro_ledger.cpp),
  [`test_spectro_analysis.cpp`](../../tests/test_spectro_analysis.cpp), and
  [`test_spectro_colorimetry.cpp`](../../tests/test_spectro_colorimetry.cpp)
- Command and serialization test:
  [`test_cmd_spectro_ingest.cpp`](../../tests/test_cmd_spectro_ingest.cpp)
- Independent MATLAB comparison and record checks:
  [`compare_spectro_crosscheck.py`](../../tools/compare_spectro_crosscheck.py),
  [`check_spectro_matlab_crosscheck_receipt.py`](../../tools/check_spectro_matlab_crosscheck_receipt.py),
  [`test_compare_spectro_crosscheck.py`](../../tools/test_compare_spectro_crosscheck.py), and
  [`test_check_spectro_matlab_crosscheck_receipt.py`](../../tools/test_check_spectro_matlab_crosscheck_receipt.py)
- Public result-receipt validation:
  [`check_spectro_receipt.py`](../../tools/check_spectro_receipt.py),
  [`test_generate_spectro_receipt.py`](../../tools/test_generate_spectro_receipt.py), and
  [`test_check_spectro_receipt.py`](../../tools/test_check_spectro_receipt.py)
