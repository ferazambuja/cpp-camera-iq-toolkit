# Spectroradiometer archive ingest and measurement-group analysis

## Overview

This study implements an executable C++ path from MATLAB v5 measurement files
to verified spectra, measurement-group statistics, chromaticity diagnostics,
and an XYZ closure check. The source archive contains 89 distinct readings in
40 declared groups; 37 groups contain two or three measurements and three are
singletons.

[Documentation index](../README.md) ·
[detailed report](../reports/SPECTRORADIOMETER_INGEST.md) ·
[aggregate CSV](../data/spectro_group_summary.csv) ·
[result receipt](../data/spectro_result_receipt.json) ·
[implementation](../../src/spectro_ingest.cpp) ·
[tests](../../tests/test_spectro_ingest.cpp)

![Measurement-group level and chromaticity variation](../figures/spectro_group_variation.svg)

## What the code does

`camera_iq spectro-ingest` resolves a dataset root or configured ID, reads the
committed identity ledger, and verifies the SHA-256 of the exact bytes passed to
the MAT parser. Canonical paths must remain below the dataset root and cannot
use symlinks. Optional alias verification confirms that the 45 descriptive
copies are byte-identical without counting them as additional measurements.

The command emits authoritative JSON plus separate CSVs for readings, group
summaries, and spectra. Each spectrum is retained in absolute form and also
normalized by its computed equal-weight spectral integral. Recorded XYZ,
`totalRadiance`, CCT, and Duv remain visible as source metadata.

## Result

Across the 37 groups with at least two readings, the spectral-integral
coefficient of variation is **7.17% median** and **41.65% maximum**. The maximum
per-group normalized-shape relative L2 residual is **0.518% median** and
**1.076% maximum**. Recorded-XYZ
chromaticity also changes: maximum pairwise Δu′v′ is **0.000703 median** and
**0.002852 maximum**.

The spectra reproduce recorded XYZ with one archive-derived proportional scale
of **683.016758** under equal 2 nm sample weights. With that fitted scale, the
maximum relative residual is below `2e-13%`. This is numerical closure between
fields in the same files, not an independent instrument-accuracy test and not
evidence that the scale is a standard luminous-efficacy constant.

## Interpretation

The measurements support a narrow conclusion: absolute level, normalized
shape, and chromaticity all show nonzero within-group variation under their
respective metrics. They do not identify why. Source output, geometry,
acquisition settings, re-aiming, and instrument behavior are not separable from
the retained records, so the result is labeled **within-group observed
variation**, not source drift, instrument noise, or repeatability.

Three singletons remain in the output with null variation metrics. A single
measurement establishes a level and shape, not a spread.
