# Recovering and analyzing archived spectroradiometer measurements

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

The archive kept the spectra, recorded XYZ, radiance, and acquisition fields —
but not the physical setup, geometry, integration time, or instrument
configuration. Those missing conditions are exactly what would let someone
attribute a difference between repeat readings to a cause. Since they are gone,
this study measures the differences precisely and stops there, rather than
naming a cause the record cannot support.

The analysis resolves 89 distinct readings plus 45 byte-identical aliases into
40 declared groups. Among the 37 groups holding repeated measurements, the
median coefficient of variation in total light level — each group's standard
deviation divided by its mean, across the two or three readings in that group —
was **7.17%**, while the most variable group reached **41.65%**.

Level moved independently of the other two. The group with the largest level
variation is not the group with the largest shape or chromaticity variation:
those two peak together on one group, and the level maximum lands on a
different one. So a source can hold its spectral shape and its color while its
output drifts, and no single “stability” score can describe the archive
honestly.

[Documentation index](../README.md) ·
[detailed report](../reports/SPECTRORADIOMETER_INGEST.md) ·
[aggregate CSV](../data/spectro_group_summary.csv) ·
[implementation companion](../implementation/spectroradiometer.md)

![Measurement-group level and chromaticity variation](../figures/spectro_group_variation.svg)

*Both panels cover the same 37 groups — every set of readings the archive
treats as repeats of one target. Left: total light level, one bar per group,
sorted, colored by whether the group is a ramp or reference measurement or a
scene; the single tall bar is the 41.65% maximum. Right: the same groups as
circles, with horizontal position again the level variation, vertical position
the largest chromaticity separation inside the group — note that axis is scaled
×1000, so the 0.002852 maximum in the text appears near 3 — and circle size the
normalized spectral-shape residual, readable as ordering rather than as values.
Level, shape, and color stay on separate axes rather than collapsing into one
score because their maxima land on different groups: the group that varied most
in brightness is not the group that varied most in color.*

## Method

Byte identity and spectrum content distinguish the 89 retained readings from 45
descriptive aliases without treating the aliases as new measurements. The
retained grouping record then identifies distinct readings of the same target;
target identity is not inferred from spectral similarity. That yields 40
measurement groups — 37 holding two or three readings, and three singletons.
Each multi-reading group is characterized on three axes. They are not
independent — chromaticity is computed from the spectrum through the CIE
observer, so it is a functional of the normalized shape — but they isolate
different failure modes, and a source can fail on one while holding the others:

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

An independent MATLAB reading reproduced all 89 spectra and their retained
numeric fields within the declared numerical tolerance. That confirms the
variation reported below is in the archived measurements rather than a
disagreement between two file readers; it is not a test of instrument accuracy.

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

## What the result does not establish

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
