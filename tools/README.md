# Tool index

## Portfolio and publication checks

- [`generate_portfolio_figures.py`](generate_portfolio_figures.py) regenerates
  the deterministic SVGs from committed aggregate CSVs; `--check` verifies
  freshness.
- [`check_portfolio_docs.py`](check_portfolio_docs.py) validates the public
  Markdown link graph, report-index coverage, and stale/internal-language
  publication markers.
- [`check_public_paths.sh`](check_public_paths.sh) scans tracked public files
  for machine-specific path leakage.
- [`check_sample_fixtures.sh`](check_sample_fixtures.sh) keeps committed fixtures
  small and visibly synthetic.

## Reference preparation and verification

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
