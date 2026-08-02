# Tool index

## Documentation and repository-safety checks

- [`generate_portfolio_figures.py`](generate_portfolio_figures.py) regenerates
  the deterministic SVGs from committed aggregate CSVs; `--check` verifies
  freshness.
- [`generate_spectro_report_figure.py`](generate_spectro_report_figure.py)
  regenerates the measurement-group level/chromaticity figure from the
  committed spectroradiometer aggregate; `--check` verifies freshness.
- [`export_shading_portfolio.py`](export_shading_portfolio.py) converts ignored
  schema-3 `camera_iq shading` JSON results into the committed 52-frame
  screening and 16 × 12 response tables, validating measured per-position
  headroom and finite-coverage evidence before publication.
- [`check_portfolio_docs.py`](check_portfolio_docs.py) validates the public
  Markdown link graph, report-index coverage, and project-centered language
  rules.
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
- [`matlab/export_spectro_crosscheck.m`](matlab/export_spectro_crosscheck.m)
  reads the same ledger through MATLAB and exports binary64 vector hashes plus
  recorded metadata. [`compare_spectro_crosscheck.py`](compare_spectro_crosscheck.py)
  compares that artifact with `spectro-ingest --readings-csv`; its mismatch
  behavior is covered by CTest.
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

`check_schema_contract.py` pins cross-language result schemas. It compares the
shading producer with its portfolio exporter and fixture, and the
spectroradiometer producer with its receipt generator, checker, and fixture.
Registered as the `check_schema_contract` test.
