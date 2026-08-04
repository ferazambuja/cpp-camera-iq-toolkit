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

## Evidence binding and admitted record

Each ledger row names one dataset-relative MAT file and its SHA-256 digest. The
same verified bytes feed the numerical analysis, so identity cannot change
between the ledger check and the calculations. Only the named `measurements`
record is admitted. Its wavelength axis must be ordered, its spectrum and XYZ
values must have the declared shapes, and every numeric value used below must
be finite. File-format and path-safety mechanics belong to the
[implementation companion](../implementation/spectroradiometer.md).

Optional alias verification hashes the 45 declared copies. Aliases can confirm
identity and preserve descriptive names, but they do not increase sample size.

## Absolute level and normalized shape

For reading `i` with spectral samples `S_i(λ_j)` on uniform spacing `Δλ = 2
nm`, the absolute spectral integral is

```text
I_i = Δλ Σ_j S_i(λ_j)
```

Every sample receives equal weight, including the endpoints, so this is a
rectangular sum rather than a trapezoidal integral.

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

The primary analysis recomputes an unscaled tristimulus vector from each spectrum and
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
it does not reveal its physical definition. Each value is retained but not
aggregated.

CCT and Duv are also retained without recomputation because the files do not
identify the locus and distance conventions used to produce them.

## Independent file-reading cross-check

MATLAB R2026a independently read the same 89 ledger-bound files. It reproduced
both numeric vectors in every reading exactly by SHA-256 and matched the
retained numeric fields within the declared `1e-12` absolute-or-relative
tolerance; the largest absolute difference was `4.55e-12` for CCT. This makes
the observed variation unlikely to be a primary-reader artifact. It does not
provide an independent reference for instrument accuracy because both paths
read the same archived measurements. Public aggregate and cross-check receipts
preserve the comparison identities without publishing the private readings.

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

## Engineering companion

The [spectroradiometer implementation companion](../implementation/spectroradiometer.md)
explains how the analysis is realized in C++ and routes readers to the public
source and tests. Public aggregate results remain in the
[group summary](../data/spectro_group_summary.csv); the report above is
canonical for their scientific meaning and archive limitations.
