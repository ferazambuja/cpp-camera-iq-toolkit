# Spectral Sensitivity and Camera Color Fidelity

The shapes of a camera's red, green, and blue spectral sensitivities constrain
how closely a linear transform can reproduce standard human-observer color
coordinates. This report measures and checks those spectral-sensitivity
functions (SSFs) before comparing five cameras. Four evidence-complete
camera/chart sets reached minimum channel correlation above 0.992 in
same-session physical closure; the Canon 5D2 and Phase One IQ3 form the stable
endpoints of the separate colorimetric-fit comparison, while the middle ordering
remains method-sensitive.

Analysis date: 2026-07-06
Dataset: `spectral_sensitivity_2016_2017`

## Result summary

The completed path runs from monochromator data to a RAW-extracted camera
spectral-sensitivity function, same-session physical closure, Luther-condition
residuals, and an ISO 17321-style SMI approximation.

[Case study](../case-studies/spectral-color-fidelity.md) ·
[aggregate results CSV](../data/spectral_color_fidelity.csv) ·
[archive role map](SPECTRAL_ARCHIVE_INVENTORY.md) ·
[implementation companion](../implementation/spectral-fidelity.md)

The source archive was read only. The tracked repository records relative
dataset labels; private RAW files, workbooks, and generated manifests remain
under ignored paths.

## Inputs and conditions

The evidence-complete portion is a four-camera laboratory run in which each
camera has a monochromator sweep, a same-session broadband chart capture, a
measured illuminant, and measured chart reflectance. A Phase One IQ3 sweep from
a separate rig has no matching broadband target and therefore enters only the
spectral-sensitivity comparison, not physical closure. Exact file roles and
session pairings are recorded in the
[spectral archive inventory](SPECTRAL_ARCHIVE_INVENTORY.md).

The Canon 5D2 is the retained end-to-end RAW extraction case. Its sweep contains
48 wavelengths from 360 to 830 nm in 10 nm steps, one matched dark frame, and a
line-source spectrum sampled on the same rounded axis. The camera settings are
ISO 100, 1/160 s, f/5.6, and 50 mm. The physical-closure calculation later uses
only 380–730 nm, the strict overlap shared by the camera sensitivities,
illuminant, and chart reflectance.

All source captures remain private. The retained legacy response curves are
comparison references for reimplementation fidelity, not independent truth.
Black levels used for sample calculations are read after RAW unpacking because
maker metadata can change between initial file inspection and pixel decoding;
camera clocks and file
modification times do not determine session pairing.

## Recovering and checking the sensitivity functions

A monochromator presents a narrow band of wavelengths to the camera, one band
at a time. For each wavelength `lambda_i`, the response of channel `c` is the
mean sensor signal above the matched dark measurement divided by the measured
line-source power at that wavelength:

```text
S_c(lambda_i) = max(0, mean_light,c(lambda_i) - mean_dark,c)
                / line_power(lambda_i)
```

The three curves are then divided by the maximum green response, fixing their
shared scale while preserving their relative shapes. Samples near saturation
are excluded before the mean is formed; below-dark tails become zero response
and remain counted as diagnostics. The Canon extraction uses the central 50%
of the active mosaic, rounded to complete Bayer blocks, so red, both greens,
and blue are sampled without demosaic.

Canon 5D2 extraction diagnostics:

| Field | Value |
|---|---:|
| Metadata black by CFA position | `[1022, 1024, 1023, 1023]` |
| Dark residual mean by CFA position | `[0.7014, -0.0404, 0.9930, 1.2593]` |
| Maximum saturated fraction | `0.005576` |
| Samples with any saturation flag | `3 / 48` |
| Maximum below-dark fraction | `1.0` |
| Samples with any below-dark tail flag | `12 / 48` |

Agreement with the retained legacy curve, after the same green-peak
normalization:

| Channel | RMS vs legacy | Pearson correlation |
|---|---:|---:|
| R | `0.0063000` | `0.9993665` |
| G | `0.0068747` | `0.9997911` |
| B | `0.0037980` | `0.9999076` |

This close match shows that the new extraction recovers the retained curve; it
does not establish that either curve is physically correct. That stronger test
is closure: use the recovered sensitivities to predict an independent
broadband chart capture from measured illuminant and reflectance data. The
legacy workflow differs materially—it could omit dark subtraction, selected a
region on a downscaled TIFF, sampled a rectangular border, and operated after
demosaic—so it remains method context rather than the measurement definition.

## Physical closure against an independent chart capture

The `2016_Monochromator` archive contains a same-session Canon 5D2 broadband
target set. The RAW frames live in the top-level
`2016_11_21_5D2_Target/` session folder, their per-frame patch-extraction
sidecars are mirrored under
`Data_Collected/Canon 5D Mk II/Target/`, and the shared illuminant and chart
reflectance files sit under `Data_Collected/Light Source/` and
`Data_Collected/Color Checker/`. The analysis reads these inputs through the
configured dataset root.

| Input | Archive file | Verified role |
|---|---|---|
| Target RAW | `2016_11_21_5D2_Target_1_Target_0116.CR2` | Canon EOS 5D Mark II, EF50mm f/2.5 Compact Macro, ISO 100, 1/200 s, f/5.6, 50 mm, 5616 x 3744 |
| White RAW | `2016_11_21_5D2_Target_1_WhiteCard_0117.CR2` | Same camera/lens/exposure metadata as target |
| Dark RAW | `2016_11_21_5D2_Target_1_DarkFrame_0118.CR2` | Same camera/lens/exposure metadata as target |
| Patch coordinates | `*_CR2_SG.txt` sidecars | RawDigger SG exports for the target, white, and dark frames |
| Illuminant SPD | `PR655_HID_avg.txt` | PR-655 HID average, 101 samples, 380-780 nm at 4 nm |
| SG reflectance | `SGMeasurements_CGATS.txt` | i1Pro / SpectraShop SG measurement, 140 patches A1..N10, 380-730 nm at 10 nm |

The text illuminant export has no header, but its paired native
`PR655_HID.spectrashop` project records `PR-655`. The SG and CC24 native projects
both record `i1Pro` and probable unit identifier `1001351`; their CGATS exports
record the shared 45:0/source-A/2-degree/D50 conditions. No archive file names
the monochromator. Unknown wavelength, bandpass, and stray-light behavior can
affect relative curves and rankings as well as absolute SSF values; the 2017 IQ3
session below is a separate rig and timeline.

The chart response predicted for patch `p` and camera channel `c` is

```text
P[p,c] = sum_lambda S[c,lambda] E[lambda] R[p,lambda] delta_lambda
```

where `S` is the camera sensitivity, `E` the measured HID illuminant, and `R`
the measured patch reflectance. All three are aligned to 380–730 nm. A single
global scale `k` is fitted across every patch and channel because exposure
differs between the monochromator sweep and chart capture; separate channel
scales would conceal a spectral mismatch.

The illuminant label is supported by the session context but not written into
the target capture itself, so a white-card check comes first. The measured
dark-subtracted white ratios, R/G `0.589` and B/G `0.459`, agree with the
SSF-times-HID prediction, `0.591` and `0.462`, within 0.4% and 0.8% on that
calculation. Equal-energy and 2856 K, 5000 K, and 6500 K proxy illuminants miss
the same ratios by roughly 16–53%. This makes the HID pairing specific among
the tested broad alternatives, although it cannot rule out every engineered
spectrum. The chart result is reported only after that pairing check, matched
dark subtraction, and saturated/below-dark patch screening.

Canon 5D2 Target set 1 closure uses the RAW-derived SSF.

Result:

| Quantity | Value |
|---|---:|
| White-card gate max ratio error | 1.3510% |
| Common wavelength grid | 380-730 nm, 36 bands |
| Closure patches | 140/140 matched |
| Target dark-subtracted patches | 140 |
| Target saturated / below-dark exclusions | 0 / 0 |
| Global exposure scale `k` | 13503.990 |
| R/G/B relative RMS | 9.539% / 9.840% / 11.618% |
| R/G/B correlation | 0.994688 / 0.994328 / 0.994999 |

Per-channel scale values are retained only as diagnostics; the fitted closure
uses the single global `k` above.

The four-camera Target set 1 comparison uses the shared PR-655 HID illuminant,
SG reflectance, and per-camera dark measurements. This retained closure table is a
mixed-source baseline: the Canon row uses the toolkit RAW-derived SSF; the other
three rows use their legacy `*_mono.csv` SSFs. Toolkit-SSF closure artifacts
for the Nikon D810, Sony A7RII, and Sony A7SII are not retained in this table,
so it remains a mixed-source comparison:

| Camera | SSF source | Gate-1 max ratio error | Patches | Target saturated / below-dark exclusions | R/G/B relative RMS | Minimum channel correlation |
|---|---|---:|---:|---:|---:|---:|
| Canon 5D2 | toolkit RAW extraction | 1.351% | 140/140 | 0 / 0 | 9.539% / 9.840% / 11.618% | 0.994328 |
| Nikon D810 | legacy `mono.csv` | 2.949% | 140/140 | 0 / 0 | 10.802% / 11.069% / 13.802% | 0.992676 |
| Sony A7RII | legacy `mono.csv` | 2.103% | 140/140 | 0 / 0 | 10.803% / 11.149% / 13.349% | 0.992517 |
| Sony A7SII | legacy `mono.csv` | 1.284% | 140/140 | 0 / 0 | 9.901% / 9.917% / 11.252% | 0.993567 |

All four 2016 cameras pass the illuminant-pairing gate and close with
high patch-order correlation (minimum channel correlation >0.992). This is a
cross-manufacturer method validation: independently measured SSF, illuminant,
and chart reflectance predict the same-session camera target captures with a
single global exposure scale.

### CC-24 closure: a complementary standard chart

The classic 24-patch ColorChecker provides a smaller complementary closure test
for all four cameras. It uses the same-session patch measurements, PR-655 HID
illuminant, legacy SSFs, and per-patch dark subtraction as the SG comparison.

| Camera | Gate-1 max ratio error | Patches | Target saturated / below-dark exclusions | R/G/B relative RMS | Minimum channel correlation |
|---|---:|---:|---:|---:|---:|
| Canon 5D2 | 1.012% | 24/24 | 0 / 0 | 5.153% / 4.439% / 5.403% | 0.998234 |
| Nikon D810 | 2.581% | 24/24 | 0 / 0 | 5.583% / 4.271% / 7.575% | 0.998095 |
| Sony A7RII | 1.747% | 24/24 | 0 / 0 | 5.707% / 4.755% / 6.627% | 0.997728 |
| Sony A7SII | 1.473% | 24/24 | 0 / 0 | 5.610% / 5.377% / 5.803% | 0.997400 |

The CC-24 residuals are roughly half the SG-140 residuals (~5-8% vs ~9-14%).
This is a **chart-set difference, not a stronger camera result**: the classic
24-patch ColorChecker is a smaller set of matte, well-behaved colours, while
SG-140 includes many more dark, extreme, and near-glossy patches where relative
error on small RGB values is larger. Both charts hold high patch-order
correlation, so both validate the same SSF physics; CC-24 is the standard-chart
complement to the denser SG-140 closure, not a replacement.

As a method-sensitivity check, all four monochromator sweeps were also
re-extracted directly from RAW. Only the Canon extraction is retained as a
local end-to-end artifact; the other three comparisons were read from the
mounted archive. The near-identical combined residuals below show that the
reported ordering is not an artifact of choosing the retained legacy curve over
the new extraction.

| Camera | Combined residual, toolkit extraction | Combined residual, legacy SSF |
|---|---:|---:|
| Canon 5D2 | 0.2218 | 0.2221 |
| Nikon D810 | 0.2972 | 0.2989 |
| Sony A7RII | 0.2970 | 0.2991 |
| Sony A7SII | 0.3087 | 0.3102 |

The stability of the ranking across legacy and toolkit SSFs is the real result:
it confirms the color-fidelity ordering is a genuine SSF property, not an
artifact of which extraction is trusted.

This residual spread measures per-camera session and optical-path closure
consistency—including lens, capture, patch extraction, SSF, and shared
illuminant/reference pairing—not camera quality.

### Luther-condition residual

The cross-camera comparison uses a property of the SSF itself rather than a
closure residual. For each CIE 1931 2-degree color-matching function `CMF_j`, it
fits the best linear combination of the camera sensitivities:

```text
CMF_j(lambda) approximately equals
  a_jR S_R(lambda) + a_jG S_G(lambda) + a_jB S_B(lambda)
```

The relative residual is the remaining error divided by the norm of the CIE
function. Lower is better, and the measure is scale-invariant, so green-peak
normalization does not bias it. Results use the 380–730 nm common grid:

| Rank | Camera | SSF source | xbar residual | ybar residual | zbar residual | Combined residual | Quality index |
|---|---|---|---:|---:|---:|---:|---:|
| 1 | Canon 5D2 | toolkit RAW extraction | 0.173 | 0.211 | 0.270 | 0.222 | 0.778 |
| 2 (tie) | Nikon D810 | legacy `mono.csv` | 0.348 | 0.225 | 0.311 | 0.299 | 0.701 |
| 2 (tie) | Sony A7RII | legacy `mono.csv` | 0.342 | 0.198 | 0.335 | 0.299 | 0.701 |
| 4 | Sony A7SII | legacy `mono.csv` | 0.353 | 0.196 | 0.355 | 0.310 | 0.690 |
| 5 | Phase One IQ3 100 | legacy `Spectral_Sensitivity_Data.csv` (2017 camSPECS) | 0.358 | 0.304 | 0.377 | 0.348 | 0.652 |

Canon 5D2 has the lowest residual in this comparison; the medium-format Phase One
IQ3 100 has the highest. Nikon D810 and Sony A7RII are effectively tied at the
reported precision, and Sony A7SII sits between that pair and the IQ3. The IQ3
row is its first 2017 capture run; the second run gives a combined residual of
0.336, so the IQ3 figure carries ~0.01 run-to-run uncertainty — an order of
magnitude larger than the 35 mm cameras' spreads — but it ranks last under either
run (both well above the A7SII's 0.310).

IQ3 provenance caveats (distinct from the four 2016 cameras):
- **Different session and rig**: the IQ3 SSF is from the 2017 camSPECS session,
  not the 2016 monochromator run. This is legitimate for the Luther metric, which
  is a pure SSF-vs-CMF geometry — session-, illuminant-, and capture-independent —
  but it would be invalid to pool the IQ3 into any closure comparison.
- **Legacy SSF, SSF-only**: the IQ3 uses its retained
  `Spectral_Sensitivity_Data.csv`. Its session has no broadband target capture
  or measured chart reflectance, so it cannot participate in physical closure.

Caveats shared with the four-camera table: this is a Luther-condition CMF-fit
residual (a metamerism proxy), not the official CIE Sensitivity Metamerism Index
(which fixes test colors + a reference illuminant); the differences among the
middle cameras are modest; and the component-residual sources are mixed (Canon
toolkit, the rest legacy). The all-toolkit combined-residual validation table above
records that the four-camera ranking is stable when all four 2016 cameras use
toolkit-extracted SSFs.

The separate `canon_5d2_repro` / `2016_IS_Reproduction` captures remain real
archive material, but they are not the closure evidence for this report because
they are a different session with no paired capture illuminant SPD.

### ISO 17321-style Sensitivity Metamerism Index approximation

The Luther residual is an unweighted geometric fit. The complementary
Sensitivity Metamerism Index (SMI) approximation synthesizes each camera's RGB
response to real test colors under a reference illuminant, fits the best 3 by 3
RGB-to-XYZ transform, and scores the remaining CIELAB error as
`SMI = 100 - 5.5 * mean Delta E*ab`. Higher is better; 100 describes a camera
whose sensitivities satisfy the Luther condition for this calculation. The
primary comparison uses CIE D55, the default illuminant in the ISO 17321
DSC/SMI annex; D50 is retained as a sensitivity check.

The ISO-recommended test set is the **18 chromatic patches** of the classic
ColorChecker 24. The 6 bottom-row neutrals (`A4`/`B4`/`C4`/`D4`/`E4`/`F4`) are
excluded; they were verified as the flattest spectra and a monotonic white-to-
black ramp. All three measured 2016 test sets were run for cross-checking: the
18 chromatic CC-24 patches (ISO-style), the full 24, and the 140-patch
ColorChecker SG.

**CC-18 (ISO-recommended chromatic patches), CIE D55 — primary result:**

| Rank | Camera | SSF source | mean dE*ab (1976) | mean dE2000 | SMI |
|---|---|---|---:|---:|---:|
| 1 | Canon 5D2 | toolkit RAW extraction | 1.69 | 0.93 | 90.7 |
| 2 | Sony A7RII | legacy `mono.csv` | 1.81 | 0.97 | 90.0 |
| 3 | Sony A7SII | legacy `mono.csv` | 1.86 | 0.88 | 89.8 |
| 4 | Nikon D810 | legacy `mono.csv` | 1.93 | 1.07 | 89.4 |
| 5 | Phase One IQ3 100 | legacy `Spectral_Sensitivity_Data.csv` (2017) | 2.13 | 1.10 | 88.3 |

**SMI across all three test sets under D55 (stability check):**

| Camera | SMI, SG-140 | SMI, CC-24 | SMI, CC-18 (ISO) |
|---|---:|---:|---:|
| Canon 5D2 | 93.3 | 93.2 | 90.7 |
| Sony A7RII | 91.7 | 92.4 | 90.0 |
| Sony A7SII | 91.4 | 92.2 | 89.8 |
| Nikon D810 | 91.0 | 91.7 | 89.4 |
| Phase One IQ3 100 | 90.4 | 90.6 | 88.3 |

What is robust and what is not:
- **Endpoint ordering is stable across the tested sets.** Canon 5D2 is best and the Phase One IQ3 is worst
  under every test set and under the Luther residual. (IQ3 run 2 gives SMI 88.4 on
  CC-18, still last.)
- **Sony A7RII is clearly second** under all three sets.
- **Sony A7SII and Nikon D810 are close enough to report conservatively.** Under
  the ISO-default D55 CC-18 run A7SII leads D810 by about 0.36 SMI, while D50
  made them an exact practical tie (89.20 vs 89.21) and the broader SG/CC-24
  sets move the gap by only a few tenths. Treat A7SII as slightly ahead in this
  D55 run, not as a large-margin result.
- **CC-18 SMI is about 2-3 points below CC-24 / SG-140** because dropping the 6
  neutrals removes the trivially-fit flat patches and leaves only the harder
  chromatic ones. This is expected: the neutrals inflate SMI without testing
  colour fidelity, which is exactly why ISO specifies the 18 chromatic set.
  CC-18 is the more discriminating metric.
- **dE2000 is a companion diagnostic, not the SMI ranking metric.** Under dE2000
  A7SII has the lowest CC-18 mean in the D55 run, while SMI is defined on dE*ab
  1976 and keeps Canon clearly ahead. The two orderings answer different
  questions and are therefore kept separate.

White-preserving optimization sensitivity (same CC-18 / D55 inputs):

| Camera | default SMI | white-preserving SMI | delta |
|---|---:|---:|---:|
| Canon 5D2 | 90.70 | 91.73 | +1.03 |
| Sony A7RII | 90.03 | 90.03 | +0.01 |
| Sony A7SII | 89.76 | 89.71 | -0.05 |
| Nikon D810 | 89.40 | 89.76 | +0.36 |
| Phase One IQ3 100 | 88.29 | 86.64 | -1.65 |

The white-preserving variant refits the 3 by 3 matrix while forcing the camera's
perfect-diffuser RGB response to map exactly to the CIE illuminant white. It is
a plausible normalization variant, not a claim that ISO Annex B uses this exact
optimizer. The comparison turns the optimizer caveat into a measured range:
the endpoints remain stable while the middle group can shift by a few tenths of
an SMI point.

SMI limitations:
- **Close to ISO, not bit-exact.** The test set now matches the ISO 17321 shape
  (18 chromatic ColorChecker patches) and the primary illuminant now follows the
  ISO DSC/SMI default (D55). The metric uses `SMI = 100 - 5.5*dE*ab` after a 3x3
  RGB-to-XYZ fit. The remaining gaps to a citable absolute ISO SMI are the exact
  slope constant and Annex B optimizer/normalization details. Changing the
  positive slope rescales absolute SMI values but not their ordering.
- **Data access.** The five-camera values depend on private measured
  reflectances and SSFs, so they cannot be regenerated from the public tree
  alone. The implementation and synthetic numerical contracts remain public.
- **Mixed SSF sources / cross-timeline**, exactly as the Luther table: Canon uses
  the toolkit extraction, the rest legacy; the IQ3 is the 2017 camSPECS SSF. SMI,
  like Luther, is a per-camera SSF property, so this is valid for ranking.

## Per-camera coverage and closure status

The archive is a five-camera set across two sessions. Physical closure needs, per
camera: an SSF source, a broadband ColorChecker/Target capture, a measured
illuminant SPD, and a measured chart reflectance. Coverage (verified 2026-07-07,
read-only):

| Camera | Session | SSF source | Target capture | Illuminant SPD | Chart reflectance | Physical closure |
|---|---|---|---|---|---|---|
| Canon 5D2 | 2016 | sweeps + `mono.csv` | `_Target` (5 sets) | HID (PR655) | SGMeasurements | Target set 1 closure run; gate PASS |
| Nikon D810 | 2016 | sweeps + `mono.csv` | `_Target` (5 sets) | HID (shared) | SGMeasurements (shared) | Target set 1 closure run; gate PASS |
| Sony A7RII | 2016 | sweeps + `mono.csv` | `_Target` (5 sets) | HID (shared) | SGMeasurements (shared) | Target set 1 closure run; gate PASS |
| Sony A7SII | 2016 | sweeps + `mono.csv` | `_Target` (5 sets) | HID (shared) | SGMeasurements (shared) | Target set 1 closure run; gate PASS |
| Phase One IQ3 | 2017 | sweeps + `Spectral_Sensitivity_Data.csv` | none | `Lamp_SPD` xlsx (camSPECS) | none | blocked |

Only the Phase One IQ3 is missing measurements: its 2017 camSPECS session has
spectral sweeps and a lamp SPD but no broadband Target capture and no chart
reflectance, so it is SSF-only, not physically closable. The
four 2016 cameras are archive input-complete and share the single measured HID
illuminant and the single measured proven-identity SG reflectance. Target set 1
has now been gate-checked for all four 2016 cameras. The additional
target sets span 2016-11-21 and 2016-11-22; interpreting either requires its
own white-card and dark-frame pairing. Additional camera or target subsets
remain outside the reported analysis because their session pairing is not yet
verified.

## Interpretation limits

- The toolkit-derived RAW response is legacy-fidelity evidence rather than an
  independently validated absolute SSF; the legacy CSV is a comparison source,
  not a correctness oracle.
- Closure is a same-session physical consistency check under one global
  exposure scale, not a uniqueness proof or public-SSF validation.
- Closure residuals do not rank camera color quality; that question is handled
  separately by the SSF-vs-CMF metrics.
- A stronger comparison would remeasure all five cameras on one characterized
  rig, repeat each spectral sweep, and capture matching white, dark, and chart
  targets for every camera. That would turn the IQ3 from SSF-only evidence into
  a closure case and quantify session-to-session uncertainty for the full set.

## Engineering companion

The [spectral implementation companion](../implementation/spectral-fidelity.md)
explains how the scientific method is realized in C++ and routes readers to
the public source, tests, and aggregate generation. The report above remains
canonical for input pairing, method conditions, results, and limitations.
