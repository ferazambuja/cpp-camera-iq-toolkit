# Spectroradiometer archive ingest and measurement-group analysis

## What this is about

A spectroradiometer measures how much light arrives at each wavelength. That
spectrum is the physical evidence underneath every downstream color number — a
white point, a chromaticity, a correlated color temperature — so an archive of
readings is only worth as much as the certainty about which reading is which.
This archive arrived as MATLAB `.mat` files whose names count acquisitions
rather than describing scenes, with duplicates saved under several names. Before
any variation could be interpreted, each reading had to be identified by its
contents rather than its filename, and the readings of the same target had to be
grouped correctly.

The analysis then keeps three things apart that are easy to conflate: how much
light there was, what shape the spectrum had, and where the color landed. A
group can vary in one without varying in the others, and lumping them together
would invent a cause the archive cannot support.

The archive kept the spectra, the recorded XYZ, `totalRadiance`, CCT, Duv, and
acquisition fields — but not the physical setup, geometry, integration time, or
instrument configuration. Those missing conditions are exactly what would let
someone attribute a difference between repeat readings to a cause. Since they
are gone, this study measures the differences precisely and stops there, rather
than naming a cause the record cannot support.

The pipeline resolves 89 distinct readings plus 45 byte-identical aliases into
40 declared groups. Across the 37 multi-reading groups, level CV was **7.17%
median / 41.65% maximum**, normalized-shape residual was **0.518% / 1.076%**,
and maximum-pair Δu′v′ was **0.000703 / 0.002852**. The maxima belong to
different groups, so they are not collapsed into one quality score.

[Documentation index](../README.md) ·
[detailed report](../reports/SPECTRORADIOMETER_INGEST.md) ·
[aggregate CSV](../data/spectro_group_summary.csv) ·
[result receipt](../data/spectro_result_receipt.json) ·
[MATLAB cross-check receipt](../data/spectro_matlab_crosscheck_receipt.json) ·
[implementation](../../src/spectro_ingest.cpp) ·
[tests](../../tests/test_spectro_ingest.cpp)

![Measurement-group level and chromaticity variation](../figures/spectro_group_variation.svg)

*One circle per multi-reading group — 37 of them, each a set of readings the
archive treats as repeats of the same target — showing how far those repeats
disagreed. Horizontal position is variation in total level (spectral-integral
CV), vertical position is the largest chromaticity separation within the group
(Δu′v′), and circle size is the normalized spectral-shape residual. The three
stay on separate axes rather than collapsing into one score because their
maxima land on different groups: the group that varied most in brightness is
not the group that varied most in color.*

## Method

Byte identity and spectrum content distinguish the 89 retained readings from 45
descriptive aliases without treating the aliases as new measurements. Declared
archive provenance then groups distinct readings of the same target; the target
identity is not inferred from spectral similarity. That yields 40 measurement
groups — 37 holding two or three readings, and three singletons. Each
multi-reading group is characterized on three independent axes:

- **Level** — the coefficient of variation of the equal-weight spectral
  integral, that is, how much total radiance differed between repeats.
- **Shape** — the relative L2 residual between spectra after normalizing each
  one by its own integral, isolating changes in spectral distribution from
  changes in overall level.
- **Chromaticity** — the largest pairwise Δu′v′ within the group, computed from
  the recorded XYZ, giving the color separation in a roughly uniform space.

A fourth check tests internal consistency: integrating each spectrum against
the CIE observer should reproduce the XYZ the instrument recorded in the same
file. Agreement there means the spectral and colorimetric fields describe the
same measurement.

An independent MATLAB R2026a export matched all 89 readings: both numeric
vectors matched exactly by hash, and all 623 numeric-field comparisons met a
declared `1e-12` absolute-or-relative tolerance. That confirms the variation
reported below is in the parsed measurements rather than a disagreement between
the two file readers; it is not a test of instrument accuracy.

## Result

Across the 37 groups with at least two readings, the spectral-integral
coefficient of variation is **7.17% median** and **41.65% maximum**. The maximum
per-group normalized-shape relative L2 residual is **0.518% median** and
**1.076% maximum**. Recorded-XYZ-derived chromaticity values also differ within
groups: maximum pairwise Δu′v′ is **0.000703 median** and **0.002852 maximum**.
The level maximum occurs in a different group from the shape and chromaticity
maxima; the three values do not describe one measurement condition.

The spectra reproduce recorded XYZ with one archive-derived proportional scale
of **683.016758** under equal 2 nm sample weights. With that fitted scale, the
maximum relative residual is below `2e-13%`. This is numerical closure between
fields in the same files, not an independent instrument-accuracy test and not
evidence that the scale is a standard luminous-efficacy constant.

## Interpretation

The measurements support a narrow conclusion: absolute level, normalized
shape, and recorded-XYZ-derived chromaticity values differ within groups under
their respective metrics. The retained records do not establish whether those
differences represent physical change, acquisition variation, or measurement
uncertainty. Source output, geometry, acquisition settings, re-aiming, and
instrument behavior are not separable, so the result is labeled
**within-group observed variation**, not source drift, instrument noise, or
repeatability.

Three singletons remain in the output with null variation metrics. A single
measurement establishes a level and shape, not a spread.
