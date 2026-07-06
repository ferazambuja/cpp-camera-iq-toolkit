# Evidence RAW Chart Localization

Date: 2026-07-06
Dataset: `clrs589_project_camera`
Command: `camera_iq patches --sg-corners --rawdigger-oracle-csv`

## Scope

This slice validates the corner-seeded ColorChecker-SG grid against RawDigger as
an oracle. RawDigger is not used as the coordinate source for extraction; it is
used only after extraction to compare generated ROI centers and uncorrected RGB
means.

This is still corner-seeded, not blind detection. The four corners in this run
are RawDigger-derived, so the run validates grid math and extraction behavior,
not independence from RawDigger.

## Predeclared Gates

These gates were committed in the plan before this real-data run:

| Gate | Hard threshold |
|---|---:|
| Matched patches | 140 |
| Max generated-vs-RawDigger ROI center error | 5 px |
| Per-channel correlation | >= 0.999 |
| Per-channel max absolute mean error | < 25 DN |

Correlation is necessary but not sufficient. A shifted or oversized grid can
preserve the SG tonal ordering while biasing every patch mean, so the center and
absolute-DN gates are the load-bearing coordinate-replacement checks.

## Command

The RawDigger oracle exports uncorrected means, so this command intentionally
does not apply flat-field or white-balance corrections:

```bash
./build/camera_iq patches \
  "Images/CCSG_f8/CCSG_f8.0_1:10_DSCF0402.RAF" \
  --dataset clrs589_project_camera \
  --sg-corners "1242.489159,707.131935;4835.468326,692.253409;4816.545845,3254.656481;1252.609404,3220.163201" \
  --sg-corner-source "RawDigger A1/A14/J14/J1 center-derived outer corners; zero-based active-image coordinates" \
  --rawdigger-oracle-csv Images/CCSG_rawdigger.csv \
  --out /tmp/camera_iq_rawdigger_oracle_rawdigger_oracle.json
```

Exit status: `1` by design, because the hard localization gate fails. The JSON
artifact is still written for inspection.

## Corner Source

Corner source: RawDigger A1, A14, J14, and J1 patch centers, projected back to
the ColorChecker-SG outer chart corners using the verified SG physical layout.
Coordinates are zero-based active-image coordinates consumed by
`--sg-corners`.

| Corner | x | y |
|---|---:|---:|
| top-left | 1242.489159 | 707.131935 |
| top-right | 4835.468326 | 692.253409 |
| bottom-right | 4816.545845 | 3254.656481 |
| bottom-left | 1252.609404 | 3220.163201 |

This corner source is not independent of RawDigger. The blind detector follow-on
is the first slice that can remove the RawDigger dependency.

## Geometry Summary

Generated chart model: `ColorChecker-SG 14x10`
Generated method: `corner_seeded_projective_grid`
Generated patch count: 140

Example generated rectangles:

| Reference patch | One-based source x | One-based source y | width | height |
|---|---:|---:|---:|---:|
| A1 | 1280.587 | 744.814 | 137.847 | 139.288 |
| N1 | 4654.307 | 731.513 | 143.474 | 141.888 |
| A10 | 1289.590 | 3048.002 | 136.842 | 138.017 |
| N10 | 4638.350 | 3077.227 | 142.385 | 140.569 |

Patch IDs here are reference-grid IDs (`A1..N10`), not RawDigger's exported
`Sample_Name` grid labels.

## Oracle Validation Result

Overall result: **FAIL**. The generated grid passes count, correlation, absolute
mean-error, and orientation gates, but fails the predeclared 5 px center gate.

| Gate | Result | Pass |
|---|---:|---|
| Matched patches | 140 | true |
| Max center error | 16.449 px | false |
| RMS center error | 11.237 px | diagnostic |
| Correlation gate | all channels >= 0.999 | true |
| Absolute mean-error gate | all channels < 25 DN | true |

Per-channel uncorrected RGB mean comparison:

| Channel | Correlation | RMSE | Max abs error | Pass |
|---|---:|---:|---:|---|
| R | 0.999997619 | 4.035 DN | 12.169 DN | true |
| G | 0.999997742 | 6.574 DN | 20.482 DN | true |
| B | 0.999998012 | 3.813 DN | 11.554 DN | true |

The JSON now serializes every generated-vs-oracle ROI-center residual under
`localization_validation.center_residuals`, including reference patch ID,
reference-grid row/column, generated center, oracle center, `dx_px`, `dy_px`,
and Euclidean distance. This makes the negative verdict diagnosable instead of
only reporting max/RMS aggregates.

Worst residuals from the f/8 `1:10` run:

| Reference patch | Row | Column | dx px | dy px | Distance px |
|---|---:|---:|---:|---:|---:|
| H6 | 5 | 7 | -15.921 | 4.133 | 16.449 |
| H4 | 3 | 7 | -15.868 | 3.504 | 16.251 |
| G6 | 5 | 6 | -15.559 | 4.228 | 16.123 |
| H5 | 4 | 7 | -15.395 | 4.782 | 16.121 |
| H2 | 1 | 7 | -15.812 | 2.113 | 15.952 |

Corner residuals are near zero because the seed was derived from the corner
patches:

| Reference patch | dx px | dy px | Distance px |
|---|---:|---:|---:|
| A1 | 0.011 | -0.042 | 0.043 |
| N1 | 0.044 | -0.043 | 0.061 |
| A10 | 0.011 | 0.011 | 0.015 |
| N10 | 0.043 | 0.011 | 0.044 |

Column summaries show the failure is a systematic interior bow rather than a
global shift. Mean `dx_px` is near zero at columns A/N, but reaches about
`-15 px` around columns G/H; mean `dy_px` stays near `+2.6 px`.

| Column | Mean dx px | Mean dy px | Mean distance px | Max distance px |
|---|---:|---:|---:|---:|
| A | 0.07 | 2.75 | 2.76 | 4.53 |
| D | -10.86 | 2.68 | 11.33 | 12.37 |
| G | -15.05 | 2.67 | 15.36 | 16.12 |
| H | -15.25 | 2.54 | 15.55 | 16.45 |
| K | -10.97 | 2.66 | 11.40 | 12.00 |
| N | 0.03 | 2.69 | 2.72 | 5.12 |

## Model-Comparison Diagnostics

The toolkit now emits a diagnostics-only `model_comparison` block under
`localization_validation`. It does not revise the predeclared 5 px gate and it
does not promote any candidate to the production coordinate path. The comparison
is run against regenerated `center_residuals` from the same f/8 `1:10` command
above, using spatial holdouts rather than fitted RMS alone.

Summary from `/tmp/camera_iq_model_comparison.json`:

| Model | DOF | In-sample RMS px | Checkerboard held-out RMS px | Row-block held-out RMS px | Column-block held-out RMS px |
|---|---:|---:|---:|---:|---:|
| corner-seeded homography baseline | 0 | 11.237 | 11.237 | 11.237 | 11.237 |
| all-center LSQ homography ceiling | 8 | 3.759 | 3.766 | 4.569 | 5.844 |
| isotropic radial k1 baseline | 1 | 11.235 | 11.235 | 11.241 | 11.382 |
| isotropic radial k1+k2 baseline | 2 | 11.234 | 11.234 | 11.249 | 11.750 |
| affine / linear-pitch baseline | 6 | 5.503 | 5.506 | 6.150 | 8.514 |
| tangential/decentering candidate | 2 | 10.376 | 10.381 | 10.449 | 11.047 |
| chart cylindrical-bow candidate | 3 | 2.143 | 2.150 | 2.359 | 2.169 |
| smooth polynomial degree-2 warp candidate | 12 | 0.453 | 0.502 | 0.498 | 0.483 |

Interpretation:

- The residual is still x-dominant: observed `dx/dy` RMS anisotropy is `3.460`.
- Isotropic radial is judged against the chart-geometry-predicted radial
  anisotropy, not `1.0`; the emitted radial basis predicts `1.620`, still far
  below the observed `3.460`.
- Simple radial terms barely move the residual, and affine/linear pitch remains
  above the 5 px target on held-out row/column blocks. They stay in the table as
  baselines, not skipped dead ends.
- The parsimonious chart-space cylindrical-bow candidate reduces held-out RMS to
  about `2.2 px`, while the 12-DOF polynomial warp reaches about `0.5 px`.
  That is consistent with a second-order smooth bow, but the higher-DOF warp is
  a flexibility ceiling, not a physical explanation.

The command also emits an `independent_center_check` using a color-similarity
centroid from the bilinear RAW RGB image as a third source. On this capture it
finds 140 valid centroids, but the centroids are far from both coordinate
sources (`69.555 px` RMS to the generated grid, `71.438 px` RMS to RawDigger).
Because the separation is small relative to the detector error, the emitted
verdict is `unresolved`. This prevents a circular RawDigger-only conclusion:
the current data support "smooth second-order bow" but do not independently
prove whether the bow is chart geometry, optical distortion, or RawDigger
placement.

Orientation control table:

| Orientation | Aggregate min corr |
|---|---:|
| direct | 0.949165 |
| column flip | 0.045996 |
| row flip | 0.081431 |
| 180 rotation | -0.298113 |

The orientation gate passes (`best_orientation: "direct"`), so the failure is
not a flipped or rotated SG order. It is an absolute geometry agreement failure.

## Interpretation

The result is scientifically useful because it shows exactly why correlation is
not enough: the generated grid keeps the SG tonal ordering and the patch means
remain close, but the ROI centers are too far from the RawDigger oracle to claim
coordinate replacement under the predeclared 5 px gate.

Do not mark the corner-seeded SG grid as the production replacement for
RawDigger rectangles yet. The residual evidence narrows the mismatch: it is not
a coordinate-origin convention error, not a simple global shift, and not
uncorrelated manual jitter. The corner-pinned homography agrees at the four
seeded corner patches but bows away from RawDigger in the chart interior,
especially the middle columns. That is consistent with a model mismatch such as
lens distortion, a different chart/cell geometry assumption, or RawDigger's
rectangle placement following a non-projective warp.

The next slice should diagnose that systematic interior bow before any
threshold change:

- verify whether RawDigger rectangles include manual per-patch jitter that a
  single homography cannot reproduce;
- compare all-center least-squares, robust, or low-order distortion-corrected
  models as diagnostics only, without using them to relax the committed gate
  post hoc;
- decide, before another real-data validation run, whether the production path
  needs lens-distortion compensation, a different corner-estimation method, or a
  separately justified pre-run gate revision.

## Limitations

- Validation is scoped to the f/8 CCSG `1:10` RAW and uncorrected RGB means.
- The f/9 CCSG series still lacks a usable same-aperture flat-field frame.
- The color reference remains a compatible 2019 SG spectral reference, not a
  proven exact per-unit measurement of the 2020 capture chart.
- This is not blind chart localization; corners trace back to RawDigger.
