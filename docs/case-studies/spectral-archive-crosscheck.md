# Separating spectral disagreement from metadata error

## What this is about

Two spectral files can look comparable while answering different questions.
Their wavelength grids may differ, overall light level can move independently
of spectral shape, and a single wrong observer setting can dominate the final
color difference. This study revisits retained HID-lamp and ColorChecker
measurements with those variables separated explicitly.

The question is how much a disagreement between two archived spectral series
can be made to say when the conditions that produced it were never recorded.
Answering it directly would mean interleaving both instruments on a monitored
source with calibration state, geometry, wavelength accuracy, and bandpass
preserved. The archive holds the spectra and their headers and none of those
controls. The work is therefore to narrow the disagreement as far as the files
allow, and to stop where a cause would have to be assumed.

## Headline results

The two eight-reading HID series differ by **4.327% directional relative L2**
after both are placed on the same 380–730 nm grid. Their own maximum
within-series shape residuals are only **0.307%** and **0.207%**. Most of the
cross-series difference is localized: 530 and 540 nm account for **75.9% of
the squared residual**, and omitting those two diagnostic bands reduces the
comparison to **2.276%**.

That pattern narrows the investigation without naming a cause. On the fixed
35-band sweep support, a fitted reference-axis offset reduces directional
relative L2 from **4.327416%** at zero offset to **3.084143%** at −0.95 nm, a
**28.7% reduction**. Because the offset is selected from the same spectra, it
does not identify a registration error, prove either instrument is
miscalibrated, or exclude spectral bandwidth, source change, and unrecorded
acquisition differences.

[Documentation index](../README.md) ·
[detailed report](../reports/SPECTRAL_CROSSCHECK_2017.md) ·
[HID comparison data](../data/hid_spectral_comparison.json) ·
[reference audit data](../data/spectral_reference_audit.json) ·
[paired-series data](../data/spectral_reference_repeat.csv) ·
[implementation companion](../implementation/spectral-crosscheck.md)

![Repeated spectra, residual localization, and ColorChecker reference audit](../figures/spectral_archive_crosscheck.svg)

*The upper-left plot compares the two mean normalized HID spectra; the bars
below show each wavelength's share of squared residual. The right panels keep
three separate checks visible: observer selection, exact interchange of one
24-patch measurement, and variation in a candidate paired chart series.*

## Why the metadata check matters

One ColorChecker export declares a 2-degree observer in one field and a
10-degree observer in another. Recomputing its embedded Lab values resolves the
conflict: D65 with the CIE 1964 10-degree observer gives **0.0119 mean ΔE76**,
whereas the CIE 1931 2-degree alternative gives **3.909**. A second application's
embedded XYZ agrees with the 2-degree calculation to **0.0469% mean relative
L2**, confirming that the observer must be selected for each declared output
rather than inferred from a filename or a single header field.

Four CGATS exports preserve all 24 spectra exactly by stable sample identity,
even though their layout labels differ. The two SpectraShop exports declare 38
fields while carrying 41; the PatchTool exports correctly declare their 41-
and 38-field layouts. This is strong interoperability evidence, but it is still
one measurement serialized four ways—not four independent measurements.

## What changed from the earlier method

A retained D800 workflow shows how easily a plausible spectral curve could be
produced from fragile assumptions: it sampled a rectangle outline, relied on
unsorted file order for wavelength pairing, and measured pixels after dcraw
interpolation. The private legacy source and surviving outputs are
[hash-bound](../../data/samples/spectral_2017/d800_legacy_method_receipt.json)
without redistributing code for which no redistribution license was identified.
The current C++ path instead binds a sorted file set to a validated wavelength
axis, samples filled CFA-balanced regions before demosaic, subtracts measured
dark residuals per CFA position, and reports saturation and below-dark evidence.

Only two NEFs from that D800 exercise survive, so the earlier curve cannot be
recomputed. The comparison is therefore a code-level method audit, not a claim
that a new implementation corrected the historical result.

## What the evidence supports

The work demonstrates how to compare repeated spectra across different grids,
localize disagreement, audit contradictory observer metadata, and distinguish
stable sample identity from layout labels. It does not establish instrument
accuracy or explain the physical cause of the HID difference. A controlled
follow-up would interleave both instruments on a monitored source and preserve
settings, calibration state, geometry, wavelength accuracy, and bandpass
characterization.

## What to take from this

The opening asked how much an undocumented disagreement can be made to say. The
answer is a great deal about *where* and nothing about *why*. Two bands at 530
and 540 nm carry **75.9%** of the squared residual, and dropping them takes the
comparison from **4.327%** to **2.276%** — so the difference is a localized
feature rather than a broad scale or shape mismatch, and any explanation has to
account for those two bands specifically.

The tempting next step is the one this study declines. Fitting a reference-axis
offset lowers the objective by **28.7%** at −0.95 nm, which reads like a
wavelength registration error located and measured. It is not. The offset was
fitted to the same spectra it is then scored against, so it establishes that the
comparison is sensitive to the axis, not that either axis is wrong. Spectral
bandwidth, source change, and unrecorded acquisition differences remain equally
consistent with the data. Naming the cause needs the interleaved capture; no
amount of reanalysis substitutes for a control that was never measured.

The metadata half generalizes further than the HID half. One export declared two
different observers in two fields, and recomputing the embedded Lab values
settled it numerically — **0.0119** mean ΔE76 under the 10-degree observer
against **3.909** under the 2-degree. One of those is agreement and the other is
a plainly visible error, from the same file, decided by a field the file
contradicts itself about. The observer therefore has to be recomputed against
each declared output rather than inferred from a filename or a single header,
and the same check applies to any reference table whose observer is asserted
rather than demonstrated.
