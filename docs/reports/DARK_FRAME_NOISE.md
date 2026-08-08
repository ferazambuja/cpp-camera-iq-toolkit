# Dark-Frame Noise Diagnostics

Two dark frames made under matched settings can separate frame-to-frame noise
from spatial pattern that stays fixed on the sensor. This archive contains only
one clean matched pair after black-level screening, so the report gives a
bounded DN-space diagnostic—not a camera specification, photon-transfer curve,
or electron-calibrated read-noise result.

Analysis date: 2026-07-07
Dataset: private CLRS-589 Fujifilm X-T100 dark-frame captures. Source RAW files
are not distributed with this repository.

## Scope

The calculation is scoped to what the local CLRS-589 dark-frame data supports:

- Start with decoded, active-area, black-subtracted CFA samples and exclude the
  outlier identified by the dark-calibration screen.
- Estimate temporal dark-frame noise from matched setting pairs in DN:
  `sigma_temporal = stddev(frame1 - frame2) / sqrt(2)`.
- Estimate dark-signal non-uniformity (DSNU), the fixed spatial component, from
  the pair mean after subtracting the temporal contribution:
  `DSNU^2 = var(mean_pair) - sigma_temporal^2 / N`, with `N=2` for a pair.
- Report a robust MAD-based DSNU companion because the moment estimate includes
  defect pixels. The companion subtracts the same temporal floor
  (`robust^2 = mad(mean_pair)^2 - sigma_temporal^2 / N`) so both DSNU columns
  are on one scale. When the robust variance does not exceed that floor, the
  result remains undefined rather than being forced to zero or an imaginary
  value.
- Fit an expected-null dark-current diagnostic over in-tolerance dark frames.

This is deliberately not photon-transfer, electron read noise, full well, or
dynamic range.

Subtracting the two matched frames cancels spatial structure that is fixed in
place and leaves frame-to-frame noise. The variance of two independent noise
samples adds, so division by `sqrt(2)` returns the single-frame standard
deviation. Averaging the same pair does the opposite: it retains fixed pattern
while reducing the temporal contribution, which is why that contribution is
subtracted before DSNU is reported.

## Real-Data Run

Result summary:

| Field | Value |
|---|---:|
| Candidate frames | 21 |
| Readable frames | 21 |
| In-tolerance frames | 20 |
| Matched clean pairs | 1 |
| Excluded frames | 19 |
| Single-pair only | true |
| Gain candidate | false |
| PTC candidate | false |
| DR candidate | false |

The only clean matched pair is the f/8, 1/60 s, ISO 200 pair, published as
`dark_pair_1-60_01` and `dark_pair_1-60_02`.

The apparent 1/1000 s pair is intentionally not used: its first candidate,
published as `dark_outlier_1-1000_01`, is the dark-calibration outlier already
reported in `DARK_CALIBRATION.md` (`max_abs_mean_residual` =
81.2448 DN), so it is excluded before pairing.

## Temporal Noise And DSNU

All values are black-subtracted DN. Channel labels follow the decoded active
Bayer phase; the two green positions are kept separate.

| Plane | Temporal noise DN | Moment DSNU DN | Robust MAD DSNU DN |
|---|---:|---:|---:|
| R | 2.4397 | 3.0662 | null (below floor) |
| G1 | 2.0584 | 0.4133 | 0.2821 |
| G2 | 2.0825 | 0.8927 | 0.1721 |
| B | 2.0669 | 0.5307 | 0.2492 |

Interpretation:

- This is a single-pair estimate, not a pair-level reproducibility study.
- `sigma_temporal` approximates read noise only because the clean dark residual
  is near zero over this short shutter ladder.
- The moment DSNU is an upper-bound style estimate because no hot-pixel or
  defect-pixel rejection is performed.
- The temporal-corrected robust MAD companion estimates fixed-pattern that
  survives tail rejection. R clamps to null: its robust bulk spread sits below
  the temporal floor while its 3.07 DN moment DSNU remains high, so the R-plane
  moment estimate is tail-sensitive rather than supported by the robust bulk
  estimate. G1/G2/B carry a small resolvable fixed-pattern (0.17-0.28 DN), below
  their moment values and on the same scale.

## Dark-Current Diagnostic

Dark-current slope is fit over the in-tolerance frames that also carry usable
filename exposure metadata. The result is the expected null: slopes are small
and R² is tiny, so no dark-current rate is claimed.

| Plane | Points | Slope DN/s | Intercept DN | R² | Measurable |
|---|---:|---:|---:|---:|---|
| R | 19 | 0.0982 | 0.0218 | 0.0057 | false |
| G1 | 19 | 0.0702 | 0.1767 | 0.0029 | false |
| G2 | 19 | 0.1678 | 0.1841 | 0.0131 | false |
| B | 19 | 0.1425 | 0.2234 | 0.0129 | false |

This diagnostic is reported so a future longer-exposure dark set has a stable
output contract. For this dataset it confirms that dark current is not
measurable from the available short-exposure ladder.

## Interpretation limits

- Results remain DN-referred because system gain was not measured; electron
  read noise, full well, and engineering dynamic range therefore remain open.
- The available flats are not a repeated same-level signal ladder, so they do
  not support PRNU or photon-transfer claims.
- Only one clean matched dark pair survives calibration, without an independent
  pair cross-check.

## Engineering companion

The [RAW implementation companion](../implementation/raw-foundation.md)
explains how this measurement is realized in C++ and routes readers to the
public source and tests. Input black-level screening is documented in
[Dark calibration](DARK_CALIBRATION.md).
