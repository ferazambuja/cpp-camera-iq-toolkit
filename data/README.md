# Data

## Standard reference tables

Colorimetric reference tables used by the analysis commands. These are
third-party standard data, not measurements produced by this project, and they
carry their own terms — the repository's MIT license covers this project's own
source and documentation, not the contents of the tables below.

| File | Contents | Derivation from official CIE source |
|---|---|---|
| `cie1931_2deg_cmf_1nm.csv` | CIE 1931 2-degree colour-matching functions, 360–830 nm at 1 nm | complete observer grid, header added, reduced decimal formatting; maximum numeric difference `5.0e-13` |
| `cie1931_2deg_cmf.csv` | same observer, 380–730 nm at 10 nm | wavelength subset with legacy reduced precision; maximum absolute numeric difference `4.0e-5` |
| `cie_d50.csv` | CIE D50 relative SPD, 380–730 nm at 10 nm | subset of the official 300–830 nm, 1 nm table, rounded to three decimals; maximum absolute difference `0.0005` |
| `cie_d55.csv` | CIE D55 relative SPD, 380–730 nm at 10 nm | subset of the official 300–780 nm, 5 nm table; values unchanged |

The exact official tables are committed under `data/third_party/` with line
endings normalized to LF. DOI, published and committed-copy SHA-256 values,
attribution, transformations, and CC BY-SA 4.0 terms are recorded in the
[third-party data notices](../THIRD_PARTY_NOTICES.md).

## Which observer to use

`cie1931_2deg_cmf.csv` is the observer for the chart-reflectance work, whose
spectral references are themselves on a 10 nm grid.

`cie1931_2deg_cmf_1nm.csv` is for measurements sampled finer than 10 nm.
Interpolating the 10 nm table up to a 2 nm spectroradiometer axis under-resolves
the short-wavelength `z` lobe, which is a property of the table rather than of
the measurement.

## Verification

`tools/check_cie_cmf_1nm.py` runs in CTest and pins the three official source
copies and four derived tables by SHA-256. It also verifies each declared grid,
selection, and decimal-rounding bound, plus the observer's 555 nm peak and
equal-energy white point.

`tools/gen_cie_d50.py` and `tools/gen_cie_d55.py` each assert that the
illuminant they emit lands on its published white point when integrated against
`cie1931_2deg_cmf.csv`.

## Spectroradiometer identity ledger

[`spectro_identity_ledger.csv`](spectro_identity_ledger.csv) records the 89
distinct neutral-ramp and diffuser readings in the spectroradiometer archive,
their 40 measurement groups (37 repeated and three singleton), and 45
byte-identical aliases. Paths are relative to the private CLRS-589 project root;
the ledger contains no spectra or machine-specific paths.

`tools/generate_spectro_identity_ledger.py` derives the file identities by
SHA-256 and obtains the scene/repeat labels from the descriptive alias names.
This avoids treating directory order as measurement identity. The CTest check
validates the committed ledger's schema, counts, unique declared digests,
source-relative path roles, and group membership. Re-hashing the source files
and confirming byte identity requires the private archive.
