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

## D810 SFR Oracle Contract

The first implementation slice should use the D810 50 mm aperture sweep because
it has exact filename-keyed NEFs, per-file ROI tables, and a complete f/1.4..f/16
series. The actual filenames include the `NIKON ` prefix and a space:
`NIKON D810_50mm_f<aperture>_.NEF`. Do not split these names with whitespace-
delimited shell tools.

Use a **single Imatest batch** for any advisory comparison. The per-folder
`Results/SFR_cypx.csv` concatenates two batches; the per-file
`Results/NIKON D810_50mm_f...__Y_multi.csv` files are the 10-Dec-2016 batch and
carry both the exact center ROI and the matching center MTF50 values. Do not mix
the first `SFR_cypx.csv` batch with the per-file ROI tables.

For the green-linear SFR trend gate, these 10-Dec center rows are the
coherent advisory oracle:

| Aperture | Filename | Center ROI L,T,R,B | Imatest MTF50 cy/px | Edge angle deg |
|---|---|---|---:|---:|
| f/1.4 | `NIKON D810_50mm_f1.4_.NEF` | `3483,2231,3699,2568` | 0.1158 | -6.314 |
| f/1.8 | `NIKON D810_50mm_f1.8_.NEF` | `3483,2232,3699,2567` | 0.0899 | -6.323 |
| f/2 | `NIKON D810_50mm_f2_.NEF` | `3483,2232,3699,2568` | 0.1121 | -6.321 |
| f/2.8 | `NIKON D810_50mm_f2.8_.NEF` | `3483,2231,3699,2568` | 0.1707 | -6.318 |
| f/4 | `NIKON D810_50mm_f4_.NEF` | `3483,2232,3699,2567` | 0.1949 | -6.314 |
| f/5.6 | `NIKON D810_50mm_f5.6_.NEF` | `3483,2231,3699,2568` | 0.2400 | -6.326 |
| f/8 | `NIKON D810_50mm_f8_.NEF` | `3485,2232,3701,2568` | 0.2388 | -6.321 |
| f/11 | `NIKON D810_50mm_f11_.NEF` | `3483,2231,3699,2567` | 0.1989 | -6.297 |
| f/16 | `NIKON D810_50mm_f16_.NEF` | `3482,2231,3698,2567` | 0.1735 | -6.331 |

The aperture trend gate survives this coherent batch:
`min(f/4,f/5.6,f/8,f/11) > f/16 > max(f/1.4,f/1.8,f/2)`, with the argmax inside
the f/4..f/11 plateau. Absolute MTF50 agreement with Imatest remains advisory
because Imatest's own repeated batch differs materially on some apertures
(for example f/1.8 center is 0.1215 in the first batch and 0.0899 in the
10-Dec per-file batch).

The per-file tables also expose 23 ROIs per aperture. SFR should use only
the center ROI above; the field-sweep ROIs are useful for a later field-MTF
phase, not for the first single-edge implementation gate.

SFR toolkit orientation convention: the center ROI is processed as a
near-vertical edge (edge-position x as a function of y), with measured edge
angles around -6.3 degrees. Avoid describing this ROI as "horizontal" in future
prompts; that shorthand mismatches the actual green-plane detector convention.

## Field-MTF Slice

SFR center SFR and the follow-on field-MTF slice are complete for the D810
50 mm sweep. `camera_iq sfr --field-map` now processes all 23 per-aperture ROIs
from a single per-file `_Y_multi.csv` table:

1. The `_Y_multi.csv` parser reads all 23 rows, preserving the row number,
   region (`Center`, `Corner`, `Pt Way`), direction label, edge ID, full-frame
   ROI, matching MTF50/MTF50P, R1090, peak MTF, and field offsets.
2. The command reuses the existing green-linear `sfr` core for every ROI and
   emits a per-aperture field map. The command remains claim-scoped as
   green-linear and advisory-vs-Imatest.
3. The measured physics gates are:
   - all 23 ROIs parse and run for each verified plateau aperture;
   - per-ROI filename/run provenance remains single-batch;
   - f/5.6, f/8, and f/11 show center MTF50 above the physical-corner maximum;
   - f/4 is a near-tie/slight corner win in both Imatest and toolkit probes, so
     strict center > corner is intentionally **not** a universal plateau gate.

   Pin the corner set explicitly: the `Region` column marks only the two LEFT
   corners (`-4_-2_L_C`, `-4_2_L_C`) as `Corner`; the right-side corner
   positions (`4_-2_R_C`, `4_2_R_C`) are labeled `Pt Way` despite the `_C`
   edge-ID suffix. Both gate definitions were probed on real pixels (2026-07-08)
   and the f/5.6-f/11 center-above / f/4 near-tie results hold under either;
   prefer deriving field position from the edge-ID grid offsets (or the ROI
   center coordinates), not the `Region` strings — same label-trust trap as the
   direction column.
4. Do not treat Imatest direction labels (`L`, `R`, `AL`, etc.) as proof of
   mixed horizontal/vertical edge orientation. A real f/5.6 probe classified all
   23 ROIs as near-vertical in the toolkit convention; field-MTF should report
   actual detected orientation per ROI instead of assuming it from labels.
5. Keep the CLRS-589 Fuji dataset status separate: it remains blocked for
   SFR/MTF because it has no slanted-edge target.

Verified D810 field-map plateau probe:

| Aperture | ROIs | Accepted | Detected orientations | Center MTF50 | Physical-corner max | Center > corner |
|---|---:|---:|---|---:|---:|---|
| f/4 | 23 | 23 | vertical | 0.2000 | 0.2005 | false |
| f/5.6 | 23 | 23 | vertical | 0.2714 | 0.2002 | true |
| f/8 | 23 | 23 | vertical | 0.2218 | 0.1955 | true |
| f/11 | 23 | 23 | vertical | 0.2049 | 0.1831 | true |

Natural follow-ons are a D800 replication run, a multi-aperture summary command,
or an Imatest-replication A2 path with demosaic/luma/gamma handling.
