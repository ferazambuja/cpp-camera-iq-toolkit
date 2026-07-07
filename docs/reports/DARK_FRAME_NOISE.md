# Evidence Report - Dark-Frame Noise Diagnostics

Date: 2026-07-07
Tool: `camera_iq noise` (this repository, v0.1.0)
Dataset: private CLRS-589 Fujifilm X-T100 dark-frame captures. Source RAW files
are not distributed with this repository.

## Scope

This slice adds the first sensor-noise command, scoped only to what the local
CLRS-589 dark-frame data supports:

- Reuse the existing post-`unpack()` RAW/CFA path and the
  `dark-calibration` residual gate.
- Exclude dark-calibration outliers before any pair differencing.
- Estimate temporal dark-frame noise from matched setting pairs in DN:
  `sigma_temporal = stddev(frame1 - frame2) / sqrt(2)`.
- Estimate DSNU from the pair mean after subtracting the temporal contribution:
  `DSNU^2 = var(mean_pair) - sigma_temporal^2 / N`, with `N=2` for a pair.
- Emit a robust MAD-based DSNU companion because the moment estimate is
  defect-pixel-inclusive.
- Fit an expected-null dark-current diagnostic over in-tolerance dark frames.

This is deliberately not photon-transfer, electron read noise, full well, or
dynamic range.

## Real-Data Run

```bash
./build/camera_iq noise \
  clrs589_project_camera \
  --subdir "Images/Dark Frame" \
  --out out/noise.json
```

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

The only clean matched pair is the f/8, 1/60 s, ISO 200 pair:

- `Dark_Frame_f8.0_1:60_ISO200_DSCF0269.RAF`
- `Dark_Frame_f8.0_1:60_ISO200_DSCF0276.RAF`

The apparent 1/1000 s pair is intentionally not used:
`Dark_Frame_f8.0_1:1000_DSCF0434.RAF` is the dark-calibration outlier already
reported in `DARK_CALIBRATION.md` (`max_abs_mean_residual` =
81.2448 DN), so it is excluded before pairing.

## Temporal Noise And DSNU

All values are black-subtracted DN. Channel labels are CFA-position labels from
LibRaw's active Bayer phase; the two green positions are kept separate.

| Plane | Temporal noise DN | Moment DSNU DN | Robust MAD DSNU DN |
|---|---:|---:|---:|
| R | 2.4397 | 3.0662 | 1.4826 |
| G1 | 2.0584 | 0.4133 | 1.4826 |
| G2 | 2.0825 | 0.8927 | 1.4826 |
| B | 2.0669 | 0.5307 | 1.4826 |

Interpretation:

- This is a single-pair estimate, not a pair-level reproducibility study.
- `sigma_temporal` approximates read noise only because the clean dark residual
  is near zero over this short shutter ladder.
- The moment DSNU is an upper-bound style estimate because no hot-pixel or
  defect-pixel rejection is performed.
- The robust MAD companion exposes how much the moment estimate depends on
  sparse defects or tails.

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

## Validation

Targeted red/green tests were added for:

- Pair temporal noise: `stddev(frame1-frame2)/sqrt(2)`.
- DSNU moment correction with a material temporal-noise subtraction.
- Negative DSNU variance clamp to `null` with
  `dsnu_below_temporal_floor`.
- Hot-pixel sensitivity: moment DSNU inflates while robust MAD remains stable.
- Dimension and CFA-phase mismatch rejection before differencing.
- Dark-current expected-null diagnostic.
- JSON honesty contract: DN units, single-pair status, and gain/PTC/DR
  non-support flags.
- Command parsing and routing.

Local validation:

```bash
cmake -S . -B build
cmake --build build --parallel
ctest --test-dir build --output-on-failure
bash tools/check_public_paths.sh
```

## Not Claimed

- No electron read noise. The command reports DN only because no system gain is
  measured here.
- No PRNU. The local `Flat Image` folder has two different exposures, not a
  same-level flat pair.
- No PTC. Photon-transfer needs repeated uniform flat-field data over a signal
  ladder, not this single clean dark pair.
- No engineering dynamic range. DR requires gain, read noise in electrons, and
  a defensible full-well/saturation model.
- No independent pair cross-check. Only one clean matched dark-frame pair
  survives the dark-calibration gate.
