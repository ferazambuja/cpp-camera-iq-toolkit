# Tool index

## Documentation and repository-safety checks

- [`generate_portfolio_figures.py`](generate_portfolio_figures.py) regenerates
  the deterministic SVGs from committed aggregate CSVs; `--check` verifies
  freshness.
- [`generate_spectro_report_figure.py`](generate_spectro_report_figure.py)
  regenerates the measurement-group level/chromaticity figure from the
  committed spectroradiometer aggregate; `--check` verifies freshness.
- [`generate_gamut_portfolio.py`](generate_gamut_portfolio.py) creates the
  deterministic 125-point Display-P3 input, runs all four `gamut-map` methods,
  and regenerates the controlled-comparison SVG. `--check` byte-compares the
  input and figure, and compares result schemas exactly. Finite numerics use
  `1e-12` relative/absolute tolerance except angular diagnostics, which allow
  `1e-5` degrees for platform math-library roundoff.
- [`generate_cam16_equation_audit.py`](generate_cam16_equation_audit.py) runs
  the compiled `cam16-equation-audit` command and regenerates its JSON, CSV,
  and SVG. `--check` keeps schemas and non-numeric fields exact, compares
  finite JSON/CSV numerics within `1e-12`, and byte-compares the SVG so
  platform-level math-library roundoff cannot create false staleness.
- [`export_shading_portfolio.py`](export_shading_portfolio.py) converts ignored
  schema-3 `camera_iq shading` JSON results into the committed 52-frame
  screening and 16 × 12 response tables, validating measured per-position
  headroom and finite-coverage evidence before publication.
- [`check_portfolio_docs.py`](check_portfolio_docs.py) validates the public
  Markdown link graph, report-index coverage, project-centered language, and
  the report-layer provenance relationships needed to interpret public case
  studies without forcing capture dates into their summaries.
- [`check_public_paths.sh`](check_public_paths.sh) scans tracked public files
  for machine-specific path leakage.
- [`check_sample_fixtures.sh`](check_sample_fixtures.sh) keeps committed fixtures
  small and visibly synthetic.

## Reference preparation and verification

- [`check_cie_cmf_1nm.py`](check_cie_cmf_1nm.py) verifies the official CIE
  source-copy hashes and the declared selection/rounding transformations in all
  four project tables.
- [`generate_spectro_identity_ledger.py`](generate_spectro_identity_ledger.py)
  hashes the private CLRS-589 MAT files and derives an explicit, source-relative
  measurement-group ledger; `--check` validates the committed ledger without claiming
  to re-read the unavailable source archive.
- [`generate_spectro_receipt.py`](generate_spectro_receipt.py) cross-checks one
  archive run's JSON, group CSV, and reading CSV before producing the compact
  receipt. [`check_spectro_receipt.py`](check_spectro_receipt.py) binds that
  receipt to the committed ledger, observer, and public aggregate and validates
  the public metrics and value domains. With the private dataset configured,
  regenerate and compare the archive-backed artifacts with:

  ```bash
  ./build/camera_iq spectro-ingest clrs589_project_camera \
    --ledger data/spectro_identity_ledger.csv --verify-aliases \
    --out out/spectro-ingest.json \
    --groups-csv out/spectro-groups.csv \
    --spectra-csv out/spectro-spectra.csv \
    --readings-csv out/spectro-readings.csv
  python3 tools/generate_spectro_receipt.py \
    --result out/spectro-ingest.json \
    --groups-csv out/spectro-groups.csv \
    --readings-csv out/spectro-readings.csv \
    --out out/spectro-receipt.json
  cmp out/spectro-groups.csv docs/data/spectro_group_summary.csv
  cmp out/spectro-receipt.json docs/data/spectro_result_receipt.json
  ```

  The public receipt checker validates the committed side of this boundary; it
  cannot rerun archive-only closure or metadata values without the private MAT
  files.
- [`matlab/export_spectro_crosscheck.m`](matlab/export_spectro_crosscheck.m)
  verifies each MAT file against its ledger digest, reads the same ledger
  through MATLAB, and exports source identity, binary64 vector hashes, and
  recorded metadata. [`compare_spectro_crosscheck.py`](compare_spectro_crosscheck.py)
  compares that artifact with `spectro-ingest --readings-csv`. With MATLAB and
  the private archive available, retain a privacy-safe receipt with:

  ```bash
  python3 tools/compare_spectro_crosscheck.py \
    out/spectro-readings.csv out/spectro-matlab-readings.csv \
    --receipt out/spectro-matlab-crosscheck-receipt.json \
    --dataset-id clrs589_project_camera --matlab-release R2026a \
    --ledger data/spectro_identity_ledger.csv \
    --matlab-exporter tools/matlab/export_spectro_crosscheck.m
  python3 tools/check_spectro_matlab_crosscheck_receipt.py \
    --receipt out/spectro-matlab-crosscheck-receipt.json \
    --cpp-csv out/spectro-readings.csv \
    --matlab-csv out/spectro-matlab-readings.csv
  ```

  The receipt records artifact/source hashes, comparison counts, tolerances,
  and maximum differences rather than per-reading measurements or local paths.
  The checker rehashes both supplied CSVs and requires the C++ artifact to match
  the readings hash in `spectro_result_receipt.json`; it also cross-checks the
  dataset, reading count, and ledger identity against that result receipt.
  Without the optional CSV arguments it checks the retained public receipt and
  source bindings only. `--repo-root` supports manual invocation from any
  working directory.
  Generation, mismatch behavior, source binding, and privacy checks are covered
  by CTest.
- [`gen_cie_d50.py`](gen_cie_d50.py) and
  [`gen_cie_d55.py`](gen_cie_d55.py) regenerate the committed daylight SPDs and
  verify their white points.
- [`sg_cgats_to_reflectance_csv.py`](sg_cgats_to_reflectance_csv.py) converts
  paired CGATS wavelength/reflectance fields.
- [`verify_ccsg_vs_xrite.py`](verify_ccsg_vs_xrite.py) and
  [`check_verify_ccsg_vs_xrite_behavior.sh`](check_verify_ccsg_vs_xrite_behavior.sh)
  verify the compatible ColorChecker-SG reference mapping against the
  manufacturer nominal archive.
- [`export_ccsg_xlsx.py`](export_ccsg_xlsx.py) is the local bridge for a
  configured reference workbook; its output belongs under a gitignored private
  reference root.

## Advisory comparison

- [`libraw_bilinear_compare.cpp`](libraw_bilinear_compare.cpp) compares the
  toolkit's transparent bilinear path with LibRaw interpolation for local
  validation. It is not part of the default CMake build.

`check_schema_contract.py` runs a synthetic accepted field through the real C++
shading JSON serializer and then through the portfolio exporter's live
validators. It also validates the exporter's independently literal canonical
fixture. This protects the cross-language case the component suites cannot:
the producer and its direct JSON/CSV assertions can advance together while the
Python exporter remains stale. Comments and intentional legacy-schema fixtures
are not treated as contract authorities. No private RAW input is required.
The same test reads the compiled C++ spectroradiometer schema authority and
behavior-probes the live receipt generator and committed-receipt checker to
prove their exported version controls their production admission paths. Source
comments, strings, docstrings, dead constants, and stale bytecode cannot satisfy
either contract; the independent receipt tests still exercise their fixtures.
Registered as the `check_schema_contract` test.
