# Relative CFA Flat-Field Response

Dataset: `clrs589_project_camera` Fujifilm X-T100 sphere and dark captures  
Command: `camera_iq shading`  
Result type: source–lens–sensor characterization in black-subtracted DN

[Case study](../case-studies/cfa-flat-field-response.md) ·
[response table](../data/flat_field_response.csv) ·
[frame screening table](../data/flat_field_summary.csv) ·
[implementation](../../src/shading.cpp) ·
[CLI and serialization](../../src/cmd_shading.cpp)

## Purpose and conclusion

This command measures how a uniform-field RAW capture changes across the
sensor. It retains the four CFA positions independently, applies explicit
quality gates, normalizes each plane to a central block, and derives chromatic
ratios from the normalized maps.

The CLRS-589 archive yielded three usable f/8 frames from 52 sphere captures.
The primary green response reached 0.4801 at the lowest bin relative to its
center, while `C_RG` remained within −2.27 pp and `C_BG` within +4.47 pp of
unity. Green quadrant asymmetry was 19.65%, well above the 5% project policy.
The intensity field is consequently reported as a source–lens–sensor composite;
the capture does not isolate optical vignetting.

## Reproduction

Source RAFs and dark frames stay outside Git. Configure the dataset root in the
gitignored `configs/datasets.local.json`, then run:

```bash
./build/camera_iq shading \
  'Images/Sphere/Sphere_f8.0_1:1000_DSCF0368.RAF' \
  --dataset clrs589_project_camera \
  --config configs/datasets.local.json \
  --dark 'Images/Dark Frame/Dark_Frame_f8.0_1:1000_DSCF0437.RAF' \
  --compare 'Images/Sphere/Sphere_f8.0_1:1000_DSCF0387.RAF' \
  --compare-dark 'Images/Dark Frame/Dark_Frame_f8.0_1:1000_DSCF0437.RAF' \
  --out out/shading/f8_pair.json \
  --csv-out out/shading/f8_pair.csv
```

The publication-safe tables are exported from the ignored result files with:

```bash
python3 tools/export_shading_portfolio.py \
  --inventory-dir out/shading/inventory \
  --detailed out/shading/f8_pair.json out/shading/f8_1600.json \
             out/shading/f8_clipped.json \
  --response out/shading/f8_pair.json out/shading/f8_1600.json \
  --summary-out docs/data/flat_field_summary.csv \
  --response-out docs/data/flat_field_response.csv

python3 tools/generate_portfolio_figures.py
python3 tools/generate_portfolio_figures.py --check
```

## Input and ceiling semantics

`read_raw_cfa_image()` returns the active Bayer mosaic as signed,
black-subtracted residuals. The shading path never subtracts black again. Its
production entry point accepts a `RawCfaImage` and derives the signal ceiling
internally:

```text
ceiling[p]       = white_level - black_per_channel[p]
near_threshold  = near_ceiling_level * ceiling[p]
```

For these X-T100 captures, raw white is 16,383 DN, effective black is 1,024 DN
at all four positions, and the signal-referred ceiling is 15,359 DN. At the
default 0.98 level, samples at or above 15,051.82 DN count as near ceiling.
This distinction matters because the input buffer is already black-subtracted.

## Three geometries

The analyzer reports the effective CFA-balanced rectangles in JSON. The
primary frame used:

| Region | Mosaic rectangle | Purpose |
|---|---|---|
| Center gate | x=2406, y=1606, 1202 × 802 px | near-ceiling verdict |
| Center block | x=2808, y=1806, 400 × 400 px | normalizer and low-signal anchor |
| TL / TR corners | x=120 / 5496, y=120, 400 × 400 px | corner response |
| BL / BR corners | x=120 / 5496, y=3494, 400 × 400 px | corner response |
| Map grid | 16 × 12 bins per CFA plane | spatial response |

Geometry that cannot fit is rejected before allocation. Origins and dimensions
are even so every region contains a balanced count of all four Bayer positions.
The gate, normalizer, and map bins are separate because they answer different
questions.

## Binning and normalization

Each mosaic position is treated as its own plane. Bins use the upper median for
even populations; the convention is deterministic, and the bins contain many
thousands of samples.

For plane `X` and its center-block median `X_c`:

```text
R_X(i) = median_X(i) / X_c
G(i)   = [R_G1(i) + R_G2(i)] / 2
C_RG(i)   = R_R(i) / G(i)
C_BG(i)   = R_B(i) / G(i)
C_G1G2(i) = R_G1(i) / R_G2(i)
```

The center-block median normalizes to one; this does not force every center
sample or center-overlapping grid bin to one. The chromatic maps are ratios of
independently normalized response fields, not raw R/G or B/G ratios.

`C_G1G2` is a spatial-consistency diagnostic. A constant G1/G2 gain difference
cancels during center normalization. A spatially varying mismatch does not.

## Quality gates

All thresholds below are project policies and every measured diagnostic remains
in JSON even when a frame is rejected.

| Check | Default | Region | Failure behavior |
|---|---:|---|---|
| Near ceiling | >1% of samples at ≥98% ceiling | center gate | reject; omit derived maps |
| Center signal | median <5% ceiling | center block | reject denominator |
| Negative residual | >1% | full plane | reject pedestal/black anomaly |
| Bin coverage | <90% finite samples | each map bin | reject incomplete map |
| Finiteness | any non-finite input | full plane | reject invalid buffer |

The f/8, 1/500 s capture shows why the central and whole-frame fractions are
both reported. The worst green plane measured 11.6319% near ceiling in the
center gate and 0.4964% across the full frame. The center is the normalization
anchor, so the frame is rejected even though a 1% whole-frame test would pass.

## Archive screening

All 52 sphere files were reprocessed with the same command and thresholds:

| Aperture | Frames | Accepted | Rejected |
|---|---:|---:|---:|
| f/5.6 | 18 | 0 | 18 |
| f/8 | 21 | 3 | 18 |
| f/9 | 13 | 0 | 13 |
| Total | 52 | 3 | 49 |

All 49 rejected frames failed the near-ceiling check. The accepted frames were
the two f/8, 1/1000 s captures and one f/8, 1/1600 s capture. Because the other
apertures have no usable frame, the archive provides no aperture comparison.

## Primary response

| Map | Minimum | Maximum | Interpretation |
|---|---:|---:|---|
| Green relative response | 0.480104 | 1.000534 | combined intensity field |
| `C_RG` | 0.977316 | 0.999956 | red response up to 2.268 pp below normalized green |
| `C_BG` | 0.999718 | 1.044729 | blue response up to 4.473 pp above normalized green |
| `C_G1G2` | 0.998943 | 1.002342 | greens agree within 0.234 pp |

The green map has strong horizontal and vertical imbalance. Its four-corner
asymmetry statistic is:

```text
A = (max corner G - min corner G) / mean corner G = 0.196484
```

`A` exceeds the 0.05 project policy. This is evidence that a centered,
radially symmetric explanation is inadequate; it does not identify the source
of the asymmetry.

The policy is not an industry standard, but it is not arbitrary either. The
repeat pair below measures `A` at 0.196484 and 0.199964, so the statistic
reproduces to 0.00348 on this rig across two captures. The 0.05 policy sits
about 14x above that spread — far enough that a tripped policy is not
reproducibility noise, and close enough to stay meaningful. Two frames bound
short-term behavior; they do not establish a reproducibility distribution.

## Pedestal and repeat checks

The matching f/8, 1/1000 s dark was compatible in dimensions and CFA layout,
matched aperture/shutter/ISO metadata, and had a per-plane median residual of
`[0, 0, 0, 0]` DN. It passed the declared 1 DN tolerance. The dark verifies the
pedestal but is not subtracted from or otherwise applied to the sphere samples.

For the two f/8, 1/1000 s sphere frames, the absolute corner-response
difference over four corners × four CFA positions measured:

| Statistic | Result |
|---|---:|
| Maximum | 0.378748 percentage points |
| RMS | 0.181309 percentage points |
| Primary `A` | 0.196484 |
| Repeat `A` | 0.199964 |

This pair bounds short-term repeat behavior for these captures. It is not a
population estimate or a substitute for a larger repeatability study.

## JSON and CSV behavior

- JSON records dataset-relative filenames, the signal ceilings, effective
  geometry, all gate diagnostics, response maps, chromatic completeness,
  pedestal evidence, asymmetry, and interpretation scope.
- A rejected frame retains its measured gates and center/corner medians while
  relative and chromatic maps become `null`.
- Undefined ratio bins become JSON `null`; `chromatic_complete` and
  `missing_chromatic_bin_count` make the condition explicit.
- CSV uses long-form measurement rows. Comparison mode appends both frames and
  the maximum/RMS corner deltas.
- Absolute input and dark paths are reduced to publication-safe labels.

## Validation

The shading tests cover:

- production ceiling derivation and invalid RAW buffer sizes;
- CFA-balanced geometry and impossible-region rejection;
- finite/range validation and grid allocation bounds;
- central versus full-frame near-ceiling fractions;
- low signal, negative residuals, incomplete bins, and zero denominators;
- independent CFA planes, unequal gains, and row/column orientation;
- exact synthetic `C_RG`/`C_BG`, spatial `C_G1G2`, and missing bins;
- radial and asymmetric green fields;
- dark compatibility, metadata matching, residual tolerance, and verification;
- multiplicative repeat invariance and additive-offset detection;
- JSON/CSV rejection, privacy, pedestal, completeness, and comparison output.

The deterministic figure test also validates the exact 52-row screening table,
three complete 16 × 12 accepted maps, numeric ranges, acceptance count, and
repeat-pair record.

## Limitations and extensions

The integrating-sphere field is visibly nonuniform and was not independently
mapped with a radiometer. Only one aperture has usable frames, and there is no
camera/source rotation pair. The current results cannot separate illumination,
lens, alignment, mechanical shading, or sensor/microlens angular response.

Separating those terms would require multiple unsaturated apertures, source and
camera rotations, an independently characterized source, and more repeated
frames. A correction workflow would additionally require computing a bounded
gain surface, applying it to an independent capture, and remeasuring the field.
