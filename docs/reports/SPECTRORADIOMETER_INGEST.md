# Spectroradiometer ingest and measurement-group analysis

## Measurement objective

The archive stores spectroradiometer readings in MATLAB v5 files. The project
objective is to turn those files into an inspectable and reproducible analysis
without treating filenames, duplicate copies, or undocumented metadata as more
evidence than they are.

The committed identity ledger describes **89 distinct readings**, **45
byte-identical aliases**, and **40 measurement groups**. Thirty-seven groups
have `n >= 2`; three have one reading. The group IDs cover 15 neutral-ramp
positions, one reference group, and 24 scene groups. The MAT records contain a
201-sample radiance vector from 380 to 780 nm at 2 nm, recorded XYZ,
`totalRadiance`, CCT, Duv, and acquisition fields.

Related native project records identify a PR-655 in the broader project
archive, but the MAT payload does not serialize a complete physical setup,
geometry, integration time, or instrument configuration. The calculations
therefore characterize the retained records rather than reconstructing an
unrecorded laboratory procedure.

## Evidence binding and parser boundary

The command accepts a dataset root or configured dataset ID. For every ledger
row it:

1. Resolves the canonical dataset root.
2. Rejects absolute, parent-relative, empty, or malformed ledger paths.
3. Rejects symlinks along each declared path and confirms the resolved regular
   file remains below the root.
4. Reads the file once, verifies its SHA-256, and passes that same byte buffer
   to the MAT parser.
5. Selects the named scalar `measurements` struct and validates field shapes,
   finite values, logical flags, and the wavelength axis.

The reader implements the Level-5 subset the archive uses: compressed
elements, compact tags, and mixed numeric widths. Inflate and nesting limits
bound malformed input. Legacy workspace saves with many unrelated variables
remain outside the named-struct contract.

Optional alias verification hashes the 45 declared copies. Aliases can confirm
identity and preserve descriptive names, but they do not increase sample size.

## Absolute level and normalized shape

For reading `i` with spectral samples `S_i(λ_j)` on uniform spacing `Δλ = 2
nm`, the absolute spectral integral is

```text
I_i = Δλ Σ_j S_i(λ_j)
```

Every sample receives equal weight, including the endpoints. This is
serialized as `sample_weighting = uniform_equal_weight`; it is not a
trapezoidal integral.

The normalized spectrum is computed before group averaging:

```text
ŝ_i(λ_j) = S_i(λ_j) / I_i
```

This separates a scale change from a shape change. For a group of `n >= 2`,
absolute level variation is reported with the sample standard deviation and
coefficient of variation:

```text
CV_I = sample_sd(I_1 ... I_n) / mean(I_1 ... I_n)
```

Normalized-shape variation is the maximum relative L2 distance from the group
mean normalized spectrum:

```text
r_shape = max_i ||ŝ_i - mean(ŝ)||₂ / ||mean(ŝ)||₂
```

The public group table reports **7.17% median / 41.65% maximum** spectral-
integral CV and **0.518% median / 1.076% maximum** normalized-shape residual
across the 37 multi-reading groups.

## Chromaticity

Recorded XYZ is converted to CIE 1976 `u′,v′` without using the recorded CCT or
Duv conventions:

```text
u′ = 4X / (X + 15Y + 3Z)
v′ = 9Y / (X + 15Y + 3Z)
```

Each group reports the maximum Euclidean separation between reading pairs in
`u′,v′`. The measured maximum-pair Δu′v′ is **0.000703 median** and **0.002852
maximum**. Δu′v′ and level CV have different units and are not ranked against
one another. The nonzero chromaticity values do show that “color does not
change” is not supported.

## Same-record XYZ closure

The C++ path recomputes an unscaled tristimulus vector from each spectrum and
the committed 1 nm CIE 1931 2-degree observer:

```text
p_i,c = Δλ Σ_j S_i(λ_j) c̄_c(λ_j)
```

One proportional scale is fitted across all readings and XYZ channels:

```text
k = Σ_i,c recorded_i,c p_i,c / Σ_i,c p_i,c²
```

The archive gives `k = 683.0167582353`. With that fitted value, maximum
relative residual is below `2e-13%`. The result shows that recorded XYZ and the
stored spectrum are numerically consistent with equal sample weighting up to
one archive-derived scale.

The fit does not establish the instrument's undocumented software, observer
normalization, or physical accuracy. The scale is not labeled `Kcd`, `Km`, or a
standard luminous-efficacy constant. Both spectrum and XYZ originate in the
same record, so this is a same-record numerical closure check rather than an
independent reference measurement.

## Recorded metadata boundary

`totalRadiance` equals the rectangular spectral integral in the 45 numbered
PRD files but has different, nonconstant ratios in the older capture families.
That proves it is not one fixed multiplier of this integral across the archive;
it does not reveal its physical definition. The command retains each value but
does not aggregate it.

CCT and Duv are also retained without recomputation because the files do not
identify the locus and distance conventions used to produce them.

## Outputs and cross-check

The command can emit:

- JSON with dataset/ledger/observer identities, method fields, absolute and
  normalized summaries, closure, and singleton reasons.
- A group CSV used by the public figure.
- A spectra CSV with absolute and normalized mean/sample-SD columns.
- A per-reading CSV with identities, recorded metadata, computed closure, and
  SHA-256 hashes of the wavelength and radiance vectors as little-endian
  IEEE-754 binary64.
- A compact [result receipt](../data/spectro_result_receipt.json) recording the
  archive-run JSON and reading-table hashes alongside the committed
  identity-ledger, observer, and aggregate hashes. Public verification covers
  the committed inputs, aggregate, receipt structure, and value domains;
  reproducing archive-only closure values requires the private measurements.

The committed receipt is reproduced by this invocation and no other:

```sh
camera_iq spectro-ingest clrs589_project_camera \
  --config configs/datasets.local.json \
  --ledger data/spectro_identity_ledger.csv \
  --cmf data/cie1931_2deg_cmf_1nm.csv \
  --verify-aliases \
  --out run.json --groups-csv groups.csv --readings-csv readings.csv
python3 tools/generate_spectro_receipt.py --result run.json \
  --groups-csv groups.csv --readings-csv readings.csv \
  --out docs/data/spectro_result_receipt.json
```

Passing the archive root instead of the dataset ID reads the same files and
produces the same aggregate, but records `dataset-root:` rather than `dataset:`
in the run JSON. That is a different byte stream and therefore a different
`archive_run_result` hash. The measurements are unchanged; only the recorded
identity of the run differs. A reader who reproduces the receipt the other way
and finds a hash mismatch has not found stale evidence.

`tools/matlab/export_spectro_crosscheck.m` reads the same 89 ledger rows through
MATLAB. `tools/compare_spectro_crosscheck.py` compares vector hashes exactly and
numeric metadata within declared tolerances. This keeps MATLAB as an
independent parser check rather than an ingest dependency or second source of
truth.

That comparison has not been run against this archive, and no published number
here should be read as MATLAB-confirmed. CI cannot run it, because MATLAB is not
available there. The gated `test_compare_spectro_crosscheck` exercises the
comparator's own behaviour against constructed inputs, which establishes that
the comparator would detect a disagreement, not that the two parsers agree.

## Interpretation limits

- Group membership comes from the archive-derived, CTest-gated ledger.
- The 37 multi-reading groups permit observed within-group dispersion; they do
  not establish repeatability under a documented fixed procedure.
- The three singletons carry null spread, CV, shape-residual, and pairwise
  chromaticity metrics.
- Absolute-level, normalized-shape, and recorded-XYZ-derived chromaticity
  differences are reported separately. The records do not distinguish physical
  change, acquisition variation, or measurement uncertainty.
- The source MAT files remain private; public inspection uses implementation,
  hermetic fixtures, the identity ledger, aggregate results, and deterministic
  figures.

## Implementation and verification

- [`src/mat_file.cpp`](../../src/mat_file.cpp)
- [`src/spectro_ingest.cpp`](../../src/spectro_ingest.cpp)
- [`src/spectro_analysis.cpp`](../../src/spectro_analysis.cpp)
- [`src/spectro_colorimetry.cpp`](../../src/spectro_colorimetry.cpp)
- [`tests/test_spectro_ingest.cpp`](../../tests/test_spectro_ingest.cpp)
- [`tests/test_spectro_analysis.cpp`](../../tests/test_spectro_analysis.cpp)
- [`tests/test_spectro_colorimetry.cpp`](../../tests/test_spectro_colorimetry.cpp)
- [`docs/data/spectro_group_summary.csv`](../data/spectro_group_summary.csv)
- [`docs/data/spectro_result_receipt.json`](../data/spectro_result_receipt.json)
