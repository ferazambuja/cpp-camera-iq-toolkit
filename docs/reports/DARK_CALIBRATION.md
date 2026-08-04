# Dark Calibration Reconciliation

RAW code values include a black pedestal even when no light reaches the sensor.
Subtracting the wrong pedestal biases shadows and contaminates every later noise
or color calculation. This report compares the post-unpack metadata black level
with 21 dark captures: 20 support the recovered 1024 DN pedestal, while one
outlier is retained and rejected rather than allowed to move the consensus.

The input is the retained CLRS-589 Fujifilm X-T100 dark-frame set; the source
RAW captures remain outside the public repository.

## Scope

The analysis reconciles post-unpack metadata black against measured
dark-frame RAW data:

- Analyze the 21 declared dark-frame candidates as one bounded set.
- Measure post-unpack RAW-CFA statistics for each candidate.
- Report each frame's signed mean residual after metadata black subtraction.
- Report measured dark raw means as `metadata_black + residual_mean`.
- Count frames inside and outside a configurable residual tolerance.

This is a black-level reconciliation diagnostic. It is not a dark-current model,
DSNU/PRNU result, temporal-noise result, PTC, or dynamic-range metric.

## Scientific Handling

- Black and pitch are read after unpacking and use the same active-area crop and
  repeating black-tile handling as the RAW-statistics and demosaic baselines.
- A dark-frame residual near zero means the metadata black subtraction agrees
  with measured dark RAW values for that frame. The reported measured raw dark
  level is the metadata black plus the signed residual mean.
- Dark frames can include dark current, light leaks, capture mistakes, or
  mislabeled files. Therefore this tool does **not** replace metadata black with
  a dark-frame mean. It reports agreement and outliers before later
  noise/dynamic-range work decides which frames are scientifically usable.
- The default tolerance is 2 DN. It is a dataset-specific diagnostic guard, not
  an ISO/EMVA threshold.
- Aggregate means are reported two ways: all readable frames, and only frames
  whose per-plane mean residuals stay within tolerance.
- Two verdicts are kept separate: whether every selected dark frame stays
  within tolerance, and whether the clean-frame consensus supports the
  metadata black level. A contaminated frame can fail the first question
  without erasing the evidence provided by the clean subset.

## Results

Result summary:

| Field | Value |
|---|---:|
| Candidate frames | 21 |
| Readable frames | 21 |
| Missing reports | 0 |
| Frames within 2 DN | 20 |
| Outlier frames | 1 |
| All selected frames within tolerance | no |
| Clean subset supports metadata black | yes |
| Metadata black by plane | [1024, 1024, 1024, 1024] DN |
| In-tolerance metadata black mean | [1024, 1024, 1024, 1024] DN |
| In-tolerance residual mean | [0.0207, 0.1749, 0.1841, 0.2235] DN |
| In-tolerance measured dark raw mean | [1024.0207, 1024.1749, 1024.1841, 1024.2235] DN |
| All-frame residual mean | [2.1516, 4.0292, 4.0441, 2.5923] DN |

Interpretation: 20 of the 21 dark-frame candidates independently support the
1024 DN pedestal recovered from the repeating metadata black tile. The
all-frame mean is dominated by one outlier, so the complete set fails the
tolerance while the clean subset supports the metadata value.

Outlier:

| File | Shutter | Max abs residual | Residuals by plane |
|---|---:|---:|---:|
| `Dark_Frame_f8.0_1:1000_DSCF0434.RAF` | 1:1000 | 81.2448 DN | [44.770, 81.115, 81.245, 49.969] DN |

The outlier should not be used as a black/noise/dynamic-range calibration frame
until the capture provenance is resolved. The analysis keeps it visible instead of
silently discarding it.

## Interpretation limits

- The comparison does not estimate dark-current slope, DSNU/PRNU, read noise,
  PTC, or dynamic range.
- The outlier is identified as inconsistent at the configured tolerance, not
  automatically classified as a bad capture. The result is scoped to the
  CLRS-589 Fujifilm X-T100 dark-frame set.

## Engineering companion

The [RAW implementation companion](../implementation/raw-foundation.md)
explains how the analysis is realized in C++ and routes readers to the public
source and tests. The downstream matched-pair calculation is reported
separately in [Dark-frame noise](DARK_FRAME_NOISE.md).
