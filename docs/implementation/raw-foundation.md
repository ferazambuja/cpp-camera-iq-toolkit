# RAW foundation, calibration, exposure, and noise implementation

[Implementation index](README.md) ·
[RAW statistics report](../reports/RAW_STATS.md) ·
[demosaic report](../reports/BILINEAR_DEMOSAIC.md) ·
[dark calibration](../reports/DARK_CALIBRATION.md) ·
[dark noise](../reports/DARK_FRAME_NOISE.md) ·
[exposure response](../reports/EXPOSURE_RESPONSE.md) ·
[relative OECF](../reports/OECF_FIT.md) ·
[Stepchart report](../reports/OECF_STEPCHART.md) ·
[dataset manifest](../reports/FUJI_XT100_CCSG_MANIFEST.md)

## Software boundary

This is the common sensor-linear foundation for the measurement commands. The
implementation opens RAW files with LibRaw, resolves the visible active Bayer
area, preserves the recorded color-filter-array (CFA) phase, derives effective
black levels, and copies signed black-subtracted samples into a camera-neutral
`RawCfaImage`. Downstream methods receive that typed object rather than touching
LibRaw state directly.

## Code-level data flow

```text
dataset ID + relative RAW path
  -> dataset_config / manifest path resolution
  -> LibRaw open_file() and unpack()
  -> raw_meta_from_processor()
  -> effective_black_levels()
  -> RawCfaImage { metadata, CFA phase, signed samples }
  -> one of:
       raw CFA statistics
       bilinear demosaic
       dark calibration and matched-pair noise
       exposure-series summary and relative OECF
       Stepchart zone extraction and DN-space PTC diagnostic
  -> typed result -> JSON serializer
```

The `manifest` path stops before pixel analysis: it enumerates files, opens RAW
metadata, classifies filename fields, derives CFA and exposure-series records,
and writes a privacy-safe dataset summary. The scientific reports consume that
identity layer; they do not infer capture roles from filenames on their own.

The dataset layer keeps absolute roots out of generated public labels. Input
files are resolved below the configured root. The direct `raw-stats` and
`demosaic` commands also refuse an output that resolves to their input before
opening the destination, so a report request cannot truncate the RAW it reads.

## Effective black subtraction

LibRaw represents black level as a scalar, per-color additions, and sometimes a
repeating tile. For each of the four active-area CFA positions the code evaluates
the scientific report's canonical relation as:

```text
effective_black(r,c)
  = black
  + cblack[color(r,c)]
  + cblack[6 + (r mod tile_height) * tile_width + (c mod tile_width)]

signed_sample(r,c) = unpacked_RAW(r,c) - effective_black(r,c)
```

`effective_black_levels()` bounds every tile access and evaluates tile phase in
active-area coordinates. `read_raw_metadata()` is sufficient for identity, but
scientific sample work uses `read_raw_cfa_image()` or `read_raw_cfa_stats()`
after `unpack()`, because some makers finalize black metadata during unpack.
Measurement paths accept a repeating black tile larger than `2 × 2` only when
every entry with the same CFA parity has the same value over the complete
repeat. Otherwise the four-position representation would lose spatial pedestal
structure, so post-unpack measurement metadata is refused. Metadata-only
inventory may still expose preliminary values without making that measurement
claim.

Core types and mapping:

- `RawMeta` stores active dimensions, margins, CFA descriptor and phase, maker
  metadata, black levels, and sensor white level.
- `RawCfaImage` owns the active, row-major signed sample array and explicit row
  stride.
- `RawCfaReport` stores per-position statistics and the effective measurement
  ROI.
- `cfa_balanced_roi()` clips and rounds regions inward so all four Bayer
  positions remain equally represented.

## Bilinear demosaic baseline

`demosaic_bilinear()` preserves the known component at each pixel. Each missing
component is the arithmetic mean of matching component samples in the local
3 by 3 neighborhood; boundary pixels use only neighbors that exist. Channel
identity comes from LibRaw `COLOR()` indices plus `cdesc`, so the algorithm does
not assume one fixed RGGB origin.

The implementation intentionally stays in black-subtracted sensor DN. It does
not apply white balance, a color matrix, gamma, tone mapping, or output-space
encoding. The full RGB image is currently materialized before summary
statistics are calculated, which makes the algorithm transparent but costs
substantial memory on high-resolution files.

## Dark calibration and matched-pair noise

`summarize_dark_calibration()` joins manifest entries with `RawCfaReport`
objects, computes the residual mean for every CFA position, and classifies each
frame against the declared DN tolerance. Rejected frames remain in the typed
summary with their measurements and reason. The serialized summary keeps
`all_dark_frames_within_tolerance` separate from
`in_tolerance_supports_metadata_black`, so one contaminated frame cannot erase
the clean-subset verdict.

For a compatible dark pair, `compute_noise_pair_estimate()` evaluates:

```text
temporal_noise = stddev(frame_1 - frame_2) / sqrt(2)

pair_mean = (frame_1 + frame_2) / 2

DSNU_variance = variance(pair_mean) - temporal_noise^2 / 2
```

The robust companion replaces ordinary pair-mean spread with a MAD-derived
spread but subtracts the same temporal floor. A negative remainder becomes an
absent estimate with an explicit reason; it is never converted into a physical
zero. Pair selection uses filename-derived aperture and shutter plus effective
ISO; only ISO is reconciled with RAW metadata. Before differencing, the typed
pair check refuses mismatched dimensions, stride, CFA phase, or sample count.
That keeps incompatible arrays apart without implying that the archive proves
camera identity, physical setup, or synchronized exposure controls.

## Exposure response and relative OECF

Filename metadata and manifest entries are grouped conservatively by directory,
series key, aperture, and ISO. For each shutter point,
`summarize_exposure_response()` joins readable RAW reports and evaluates signal,
headroom, saturation, control consistency, and optional ROI uniformity. It emits
an `ExposureResponseSummary`; it does not fit a response curve.

`fit_oecf_series()` accepts only points marked usable by that summary. For each
CFA position it fits an ordinary least-squares line:

```text
relative_exposure_i = shutter_i / fastest_usable_shutter
signal_i = slope * relative_exposure_i + intercept + residual_i

max_nonlinearity_percent
  = max(abs(residual_i)) / fitted_signal_range * 100
```

The intercept is free so a black-subtraction offset remains visible. A minimum
of three usable points is required. Readiness and fit verdicts are serialized
separately so consumers cannot confuse “enough data to attempt a fit” with a
valid fitted result.

## Stepchart path

The Stepchart implementation treats the chart's retained log-exposure zones as
the reference axis instead of forcing the files into a shutter ladder. The
parser reads retained Imatest summaries, localization generates zone geometry,
and `summarize_stepchart_raw_iso()` aggregates per-zone CFA data across matched
frames. A gate checks monotonic green response and correlation with
`10^log_exposure` before any DN-referred variance fit is accepted.

The PTC diagnostic fits temporal variance in DN squared against mean signal in
DN. No conversion to electrons is performed because no independently supported
conversion gain is available.

## Failure and output contracts

- Unsupported non-Bayer layouts, invalid strides, incomplete buffers, and
  unreadable RAW files are refused before measurement.
- Signed residuals below black remain data; they are not clipped to zero.
- Undefined scientific results are represented as optional values or JSON
  `null`, with a reason where the distinction matters.
- Spatial standard deviation and temporal variance use different fields and
  names so one cannot silently substitute for the other.

## Verification evidence

At the library/unit layer, the camera-neutral RAW invariants begin in
[`test_raw_meta.cpp`](../../tests/test_raw_meta.cpp) and
[`test_cfa_stats.cpp`](../../tests/test_cfa_stats.cpp).

The RAW bridge in [`test_raw_meta.cpp`](../../tests/test_raw_meta.cpp) is tested
at the representation boundaries that can change the meaning of every later
result. A `12032`-byte row pitch is interpreted as
`6016` `uint16` samples, while an odd `12033`-byte pitch is refused. Synthetic
black metadata recovers a four-position `2 × 2` tile of `1024` without reading
past a short tile buffer. Separate unit fixtures accept a larger repeat only
when all same-CFA-parity entries agree and reject incomplete, odd-period
nonconstant, or same-parity-varying repeats at the post-unpack measurement
gate.
<!-- test-evidence: raw-foundation.black-repeat-periodicity -->

Sampling and ceiling behavior is a separate boundary, tested in
[`test_cfa_stats.cpp`](../../tests/test_cfa_stats.cpp). A strided active-area
fixture proves that border samples do not enter the statistics. With black
`1024`, white `16383`, and a `0.98` policy level, raw code `16075` is below the
first flagged integer and `16076` is included; the threshold is recomputed from
each plane's own white-minus-black range, so unequal per-position black still
flags the same boundary sample.

The demosaic assertions in
[`test_demosaic.cpp`](../../tests/test_demosaic.cpp) use constant fields, a
hand-computed `5 × 5` RGGB mosaic,
edge-only `3 × 3` neighborhoods, and a BGGR phase fixture. The selected
interpolated values are pinned to `1e-9`, and a missing RAW argument is verified
to return usage status `2`. These checks establish the stated local averaging
and phase behavior; they are not a claim of bit-exact agreement with every
LibRaw interpolation path.

Dark/noise fixtures in [`test_noise.cpp`](../../tests/test_noise.cpp) pin the
equations as well as their failure states. An
eight-sample pair with difference standard deviation `4 DN` must produce
temporal noise `2 sqrt(2) DN` to `1e-12`; the corresponding moment-DSNU result
is `sqrt(1.25) DN` to `1e-12`. When the temporal floor exceeds the spatial
spread, both DSNU estimates are absent with reason
`dsnu_below_temporal_floor`. Dimension, CFA-phase, and buffer mismatches are
refused before differencing, and serialized DN-space results keep gain, PTC,
and dynamic-range support false.

The exposure and tone-response fixtures in
[`test_exposure_response.cpp`](../../tests/test_exposure_response.cpp),
[`test_oecf_fit.cpp`](../../tests/test_oecf_fit.cpp), and
[`test_stepchart_raw.cpp`](../../tests/test_stepchart_raw.cpp) keep acceptance
and fitting separate. A
four-frame fixture with three distinct shutters groups the duplicate exposure,
while missing reports, changed ISO, below-black signal, heavy clipping, and a
nonuniform ROI each exercise a different refusal. The exact linear OECF fixture
recovers slope `100`, intercept `0`, `R² = 1`, and `0%` maximum nonlinearity to
`1e-12`; a separate injected-knee fixture pins the nonzero residual path. The
Stepchart tests distinguish strip and ring geometry, require 20 ordered zones,
pin the green-ladder correlation floor at `0.98`, and keep a DN-space PTC fit
separate from unsupported electron gain or dynamic range.

At the library and serialization layer,
[`test_manifest_scan.cpp`](../../tests/test_manifest_scan.cpp) and
[`test_manifest_json.cpp`](../../tests/test_manifest_json.cpp) exclude
AppleDouble and `.DS_Store` files, preserve relative public paths, and
serialize unavailable EXIF as `null`. At the command/integration layer,
[`test_cmd_dataset_labels.cpp`](../../tests/test_cmd_dataset_labels.cpp)
reduces direct dataset roots and their selected subdirectories to sanitized
`dataset-root:<basename[/subdir]>` labels. Its command fixtures reject absolute
subdirectory arguments, leading or embedded `..` components, and a configured
subdirectory symlink that resolves outside the dataset root.

Configured scan subdirectories are resolved canonically before the manifest,
dark-calibration, noise, exposure-response, or OECF-fit scanners run, and the
manifest excludes symlinked file entries rather than inheriting their targets.
The Stepchart command applies the same rule to its oracle directory, summary
files, and listed RAW files. Single-file RAW-statistics, demosaic, shading,
patch-extraction, and SFR inputs likewise keep dataset attribution only after
canonical containment succeeds. Direct-directory mode remains available
without dataset attribution. The shared output tests in
[`test_output_file.cpp`](../../tests/test_output_file.cpp), plus the
RAW-statistics, demosaic, patch-extraction, and SFR command fixtures, verify that
identical, normalized-equivalent, and hard-linked output identities are refused
before an input can be truncated; the two patch outputs must also remain
distinct. Together these tests establish numeric, attribution, and output
contracts. The archive-backed reports, not the fixtures, remain the authority
for the physical captures and conclusions.

## Source and tests

- RAW bridge: [`raw_meta.hpp`](../../include/camera_iq/raw_meta.hpp),
  [`raw_meta.cpp`](../../src/raw_meta.cpp),
  [`cfa_stats.cpp`](../../src/cfa_stats.cpp)
- Demosaic: [`demosaic.hpp`](../../include/camera_iq/demosaic.hpp),
  [`demosaic.cpp`](../../src/demosaic.cpp)
- Calibration and noise: [`dark_calibration.cpp`](../../src/dark_calibration.cpp),
  [`noise.cpp`](../../src/noise.cpp)
- Exposure and OECF: [`exposure_response.cpp`](../../src/exposure_response.cpp),
  [`oecf_fit.cpp`](../../src/oecf_fit.cpp),
  [`stepchart_raw.cpp`](../../src/stepchart_raw.cpp)
- Manifest enumeration and serialization:
  [`manifest.cpp`](../../src/manifest.cpp),
  [`cmd_manifest.cpp`](../../src/cmd_manifest.cpp)
- Shared ROI, dataset, filename, and output-path contracts:
  [`output_file.hpp`](../../include/camera_iq/output_file.hpp),
  [`output_file.cpp`](../../src/output_file.cpp),
  [`test_roi.cpp`](../../tests/test_roi.cpp),
  [`test_exposure_series.cpp`](../../tests/test_exposure_series.cpp),
  [`test_filename_meta.cpp`](../../tests/test_filename_meta.cpp),
  [`test_dataset_config.cpp`](../../tests/test_dataset_config.cpp), and
  [`test_output_file.cpp`](../../tests/test_output_file.cpp)
- Focused tests: [`test_raw_meta.cpp`](../../tests/test_raw_meta.cpp),
  [`test_cfa_stats.cpp`](../../tests/test_cfa_stats.cpp),
  [`test_demosaic.cpp`](../../tests/test_demosaic.cpp),
  [`test_dark_calibration.cpp`](../../tests/test_dark_calibration.cpp),
  [`test_noise.cpp`](../../tests/test_noise.cpp),
  [`test_exposure_response.cpp`](../../tests/test_exposure_response.cpp),
  [`test_oecf_fit.cpp`](../../tests/test_oecf_fit.cpp), and
  [`test_imatest_stepchart.cpp`](../../tests/test_imatest_stepchart.cpp),
  [`test_stepchart_localization.cpp`](../../tests/test_stepchart_localization.cpp),
  [`test_stepchart_raw.cpp`](../../tests/test_stepchart_raw.cpp), plus
  [`test_manifest_scan.cpp`](../../tests/test_manifest_scan.cpp) and
  [`test_manifest_json.cpp`](../../tests/test_manifest_json.cpp)
- Command and public-label tests:
  [`test_cmd_noise.cpp`](../../tests/test_cmd_noise.cpp),
  [`test_cmd_exposure_response.cpp`](../../tests/test_cmd_exposure_response.cpp),
  [`test_cmd_oecf_stepchart.cpp`](../../tests/test_cmd_oecf_stepchart.cpp), and
  [`test_cmd_dataset_labels.cpp`](../../tests/test_cmd_dataset_labels.cpp)
