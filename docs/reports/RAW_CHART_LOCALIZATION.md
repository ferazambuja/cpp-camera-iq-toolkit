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
RawDigger rectangles yet. The next slice should diagnose the geometry mismatch
before any threshold change:

- verify whether RawDigger rectangles include manual per-patch jitter that a
  single homography cannot reproduce;
- compare an all-center least-squares or robust homography as a diagnostic only,
  without using it to relax the committed gate post hoc;
- decide, before another real-data validation run, whether the production path
  needs lens-distortion compensation, a different corner-estimation method, or a
  separately justified pre-run gate revision.

## Limitations

- Validation is scoped to the f/8 CCSG `1:10` RAW and uncorrected RGB means.
- The f/9 CCSG series still lacks a usable same-aperture flat-field frame.
- The color reference remains a compatible 2019 SG spectral reference, not a
  proven exact per-unit measurement of the 2020 capture chart.
- This is not blind chart localization; corners trace back to RawDigger.
