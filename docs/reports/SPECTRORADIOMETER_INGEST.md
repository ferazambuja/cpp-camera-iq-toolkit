# Spectroradiometer ingest and measurement-group analysis

Repeated spectra can differ in total light level, spectral shape, or
chromaticity, and those differences do not necessarily peak in the same
measurement group. This report recovers 89 distinct archived readings, groups
them by retained measurement identity rather than filename similarity, and
reports the three forms of variation separately. Missing setup and instrument
conditions prevent assigning the observed variation to a physical cause.

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
The level maximum occurs in `ramp_patch_05`; the shape maximum occurs in
`ramp_patch_01`. They do not describe one measurement condition.

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
one another. The chromaticity maximum occurs in `ramp_patch_01`, not in the
group with the maximum level CV. The chromaticity separations are nonzero, so
the readings within a group are not colorimetrically identical. Treating a
group as one repeated colour would discard observed numerical variation in
recorded-XYZ-derived chromaticity. The available records do not determine
whether that variation is physical, acquisition-related, or measurement
uncertainty.

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

`tools/matlab/export_spectro_crosscheck.m` verifies each source MAT file against
the ledger digest, then reads the same 89 rows through MATLAB.
`tools/compare_spectro_crosscheck.py` requires the C++ and MATLAB source-file
identities to agree, compares vector hashes exactly, and compares numeric
metadata within declared tolerances. This keeps MATLAB as an independent parser
check rather than an ingest dependency or second source of truth.

The archive comparison with MATLAB R2026a matched all 89 readings. Both paths
verified the source MAT file digest for every reading against the identity
ledger. The two parsers produced identical SHA-256 hashes for both numeric
vectors in every reading (178 exact comparisons), and all 623 numeric-field
comparisons met the declared `1e-12` absolute-or-relative tolerance. The
largest absolute difference was `4.55e-12` for CCT; the largest relative
difference across the seven numeric fields was `4.21e-15`. A compact
[cross-check receipt](../data/spectro_matlab_crosscheck_receipt.json) binds the
two private comparison artifacts and the public ledger/exporter/comparator by
hash without publishing per-reading values or local paths. This establishes
agreement between two parser implementations on this archive; it does not
provide an independent reference for instrument accuracy.

## Interpretation limits

- Group membership comes from the identity ledger, which is derived from the
  archive by content hash rather than asserted by hand.
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

## Reproducibility

- [`src/mat_file.cpp`](../../src/mat_file.cpp)
- [`src/spectro_ingest.cpp`](../../src/spectro_ingest.cpp)
- [`src/spectro_analysis.cpp`](../../src/spectro_analysis.cpp)
- [`src/spectro_colorimetry.cpp`](../../src/spectro_colorimetry.cpp)
- [`tests/test_spectro_ingest.cpp`](../../tests/test_spectro_ingest.cpp)
- [`tests/test_spectro_analysis.cpp`](../../tests/test_spectro_analysis.cpp)
- [`tests/test_spectro_colorimetry.cpp`](../../tests/test_spectro_colorimetry.cpp)
- [`docs/data/spectro_group_summary.csv`](../data/spectro_group_summary.csv)
- [`docs/data/spectro_result_receipt.json`](../data/spectro_result_receipt.json)
- [`docs/data/spectro_matlab_crosscheck_receipt.json`](../data/spectro_matlab_crosscheck_receipt.json)
