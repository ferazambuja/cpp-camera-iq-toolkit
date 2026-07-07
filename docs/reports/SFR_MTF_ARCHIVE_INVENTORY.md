# SFR / MTF Archive Inventory

Date: 2026-07-07
Scope: filename-level inventory of the available slanted-edge SFR/MTF archive
inputs. No RAW files were copied.

## Verdict

Slanted-edge SFR/MTF is **not globally data-blocked**. The CLRS-589 Fujifilm
X-T100 dataset does not contain a slanted-edge / resolution target, but the
mounted 2016 esensi archive contains Nikon D800 and D810 SFR captures plus
Imatest-derived SFR/MTF result CSVs.

This means the next SFR/MTF slice should start from the 2016 Nikon archive, not
from a new capture by default. A new capture is only needed for Fuji X-T100 or
for a camera/lens condition not covered by the archive.

## Archive Labels

Use archive labels in public docs, not absolute mount paths:

| Role | Archive label |
|---|---|
| D800 SFR RAW + per-folder results | `archive:2016_esensi_images/2016_12_09_D800_SFR/` |
| D810 SFR RAW + per-folder results | `archive:2016_esensi_images/2016_12_09_D810_SFR/` |
| Consolidated SFR results | `archive:2016_esensi_images/SFR_Results/` |

Recommended local cache id when a slice stages files:
`d800_d810_sfr_2016`.

## Filename Inventory

| Camera | RAW files | Result files | SFR/MTF CSV candidates | Result PNGs |
|---|---:|---:|---:|---:|
| Nikon D800 | 13 NEF | 233 | 12 | 220 |
| Nikon D810 | 89 NEF | 238 | 13 | 223 |

Representative RAW files:

- D800: `NIKON D800_50mm_f1.4_.NEF`, `NIKON D800_50mm_f8_.NEF`,
  `2016_12_09_SFR_D800_f11_0032.NEF`.
- D810: `NIKON D810_50mm_f1.4_.NEF`, `NIKON D810_50mm_f8_.NEF`,
  `NIKON D810_i100_s1-40_1.NEF`.

Representative result files:

- `Results/SFR_cypx.csv`
- `Results/SFR_lwph.csv`
- `Results/NIKON D800_50mm_f8__Y_multi.csv`
- `Results/NIKON D810_50mm_f8__Y_multi.csv`
- `SFR_Results/SFR_cypx_all.csv`

The result CSVs contain Imatest SFR fields including `MTF50`, `MTF50P`,
`R1090`, edge angle, ROI channel, lens, aperture, ISO, shutter, and MTF
readouts. These are comparison/fidelity references for a fresh C++
implementation, not correctness oracles by themselves.

## Correct Next Slice

1. Stage a scoped subset only: one D800 and one D810 NEF around a known aperture,
   plus the matching Imatest CSV rows.
2. Implement a slanted-edge parser/detector with ISO 12233-compatible reporting
   where possible.
3. Compare toolkit output to Imatest CSVs as tier-1 fidelity evidence.
4. Keep the CLRS-589 Fuji dataset status separate: it remains blocked for
   SFR/MTF because it has no slanted-edge target.
