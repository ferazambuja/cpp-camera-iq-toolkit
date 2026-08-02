# Third-party data notices

The project source code and original documentation are licensed under the MIT
License. The CIE standard datasets listed here are not relicensed under MIT.
They are published by the International Commission on Illumination (CIE) under
the Creative Commons Attribution-ShareAlike 4.0 International license. The
committed source copies and the four project tables derived from them retain
those terms.

License: <https://creativecommons.org/licenses/by-sa/4.0/>

## CIE 1931 colour-matching functions, 2-degree observer

- Publisher: International Commission on Illumination (CIE), Vienna, Austria
- DOI: <https://doi.org/10.25039/CIE.DS.xvudnb9b>
- Source file: <https://files.cie.co.at/CIE_xyz_1931_2deg.csv>
- Published source SHA-256: `fa663e3535a7e0763a745993a1f0a192eb0275ac46ad2d1befd7626841e713c1`
- Committed source copy: `data/third_party/CIE_xyz_1931_2deg.csv`
- Modification: line endings normalized from CRLF to LF
- Committed-copy SHA-256: `bd7973e895a97e543815614b19c51ceff552ae9910a424724ae04ed89bd863a3`

The project tables `data/cie1931_2deg_cmf_1nm.csv` and
`data/cie1931_2deg_cmf.csv` are derived from this dataset. The 1 nm table adds a
header and uses reduced decimal formatting over the complete 360–830 nm source
grid; its maximum numeric difference from the source is `5.0e-13`. The 10 nm
table selects 380–730 nm at 10 nm and uses the older reduced-precision decimal
format; its maximum absolute numeric difference is `4.0e-5`.

## CIE standard illuminant D50

- Publisher: International Commission on Illumination (CIE), Vienna, Austria
- DOI: <https://doi.org/10.25039/CIE.DS.etgmuqt5>
- Source file: <https://files.cie.co.at/CIE_std_illum_D50.csv>
- Published source SHA-256: `b23049c6f7b266c1c1fbe147aa271e8930ca02d6e569c5ae1804c036faea4193`
- Committed source copy: `data/third_party/CIE_std_illum_D50.csv`
- Modification: line endings normalized from CRLF to LF
- Committed-copy SHA-256: `1f0ce0e7261c2ac2901d5ac286e7d656400b357a52315f334df4b7548b98632a`

`data/cie_d50.csv` selects 380–730 nm at 10 nm from the official 300–830 nm,
1 nm table and rounds relative power to three decimals. The maximum absolute
rounding difference is `0.0005`.

## CIE illuminant D55

- Publisher: International Commission on Illumination (CIE), Vienna, Austria
- DOI: <https://doi.org/10.25039/CIE.DS.qewfb3kp>
- Source file: <https://files.cie.co.at/CIE_illum_D55.csv>
- Published source SHA-256: `3e5aa1a8d5514df1928effef1615ab90e16c9a368ccfe513372cd8556c37bf4b`
- Committed source copy: `data/third_party/CIE_illum_D55.csv`
- Modification: line endings normalized from CRLF to LF
- Committed-copy SHA-256: `89d72e9ce57afb504f5a6de20608f1a713562519dbde248f12e54cb14011518d`

`data/cie_d55.csv` selects 380–730 nm at 10 nm from the official 300–780 nm,
5 nm table without interpolation or numeric modification.
