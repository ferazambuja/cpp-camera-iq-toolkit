# Spectroradiometer archive ingest and measurement-group analysis

## Overview

This study resolves 89 distinct spectroradiometer readings into 40 declared
measurement groups and reports absolute level, normalized spectral shape, and
recorded-XYZ chromaticity as separate quantities. Thirty-seven groups contain
two or three measurements; three are singletons.

The retained archive includes spectra, recorded XYZ, `totalRadiance`, CCT, Duv,
and acquisition fields, but not the complete physical setup, geometry,
integration time, or instrument configuration. Rather than infer those missing
conditions, the implementation resolves duplicate aliases by content hash and
reports level, shape, chromaticity, and numerical closure separately. This
makes the surviving measurement record useful without assigning a cause that
the archive cannot support.

[Documentation index](../README.md) ·
[detailed report](../reports/SPECTRORADIOMETER_INGEST.md) ·
[aggregate CSV](../data/spectro_group_summary.csv) ·
[result receipt](../data/spectro_result_receipt.json) ·
[MATLAB cross-check receipt](../data/spectro_matlab_crosscheck_receipt.json) ·
[implementation](../../src/spectro_ingest.cpp) ·
[tests](../../tests/test_spectro_ingest.cpp)

![Measurement-group level and chromaticity variation](../figures/spectro_group_variation.svg)

## Technical approach

`camera_iq spectro-ingest` resolves a dataset root or configured ID, reads the
committed identity ledger, and verifies the SHA-256 of the exact bytes passed to
the MAT parser. Canonical paths must remain below the dataset root and cannot
use symlinks. Optional alias verification confirms that the 45 descriptive
copies are byte-identical without counting them as additional measurements.
The scoped C++ reader supports the compressed MATLAB Level-5 structures used
by this archive, so the analysis does not require MATLAB at runtime. MATLAB is
reserved for the separate parser check below.

The command emits schema-versioned JSON plus separate CSVs for readings, group
summaries, and spectra. Each spectrum is retained in absolute form and also
normalized by its computed equal-weight spectral integral. Recorded XYZ,
`totalRadiance`, CCT, and Duv remain visible as source metadata.

## Independent parser verification

An independent MATLAB R2026a export matched all 89 readings. Both paths verified
each source MAT file against the committed identity ledger, produced identical
hashes for both numeric vectors in every reading — 178 exact comparisons — and
met the declared `1e-12` absolute-or-relative tolerance in all 623 numeric-field
comparisons. This establishes agreement between two parser implementations on
this archive; it is not an instrument-accuracy test.

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
