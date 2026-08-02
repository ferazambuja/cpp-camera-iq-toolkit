# Tool index

## Documentation and repository-safety checks

- [`generate_portfolio_figures.py`](generate_portfolio_figures.py) regenerates
  the deterministic SVGs from committed aggregate CSVs; `--check` verifies
  freshness.
- [`export_shading_portfolio.py`](export_shading_portfolio.py) converts ignored
  `camera_iq shading` JSON results into the committed 52-frame screening
  and 16 × 12 response tables.
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
  repeat-group ledger; `--check` validates the committed ledger without claiming
  to re-read the unavailable source archive.
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
- [`matlab/export_spectro_crosscheck.m`](matlab/export_spectro_crosscheck.m)
  makes MATLAB emit its own reading of every spectroradiometer file, so the
  subset MAT parser can be checked against the reference implementation of the
  format rather than only against its own tests. Nothing on the ingest path
  runs it, and no published number depends on it.
