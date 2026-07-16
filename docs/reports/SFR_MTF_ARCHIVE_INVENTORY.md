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
`d800_d810_sfr_2016`. Its root should mirror the `2016_esensi_images`
umbrella, so RAW/oracle paths remain subdirectory-relative
(`2016_12_09_D810_SFR/...`, `2016_12_09_D800_SFR/...`) instead of flattening
one camera folder into the dataset root.

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

The per-file tables also expose 23 ROIs per aperture. The center-ROI sweep and
the 23-ROI field maps are reported separately so the single-edge aperture trend
and field behavior do not get conflated.

Toolkit orientation convention: the center ROI is processed as a near-vertical
edge (edge-position x as a function of y), with measured edge angles around
-6.3 degrees. Describing this ROI as "horizontal" mismatches the actual
green-plane detector convention.

## Field-MTF Slice

Center SFR and field MTF are complete for the D810 50 mm sweep.
`camera_iq sfr --field-map` processes all 23 per-aperture ROIs from a single
per-file `_Y_multi.csv` table:

1. The `_Y_multi.csv` parser reads all 23 rows, preserving the row number,
   region (`Center`, `Corner`, `Pt Way`), direction label, edge ID, full-frame
   ROI, matching MTF50/MTF50P, R1090, peak MTF, field offsets, and the per-ROI
   CSV-summary filename as a basename.
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

## D800 Oracle Contract

The D800 counterpart sweep is oracled by the same per-file `_Y_multi.csv`
pattern under `archive:2016_esensi_images/2016_12_09_D800_SFR/Results/`. All 9
files carry the identical run date `10-Dec-2016 12:47:10` (single Imatest 4.5.7
batch, Build 2016-11-22). The folder's `Results/SFR_cypx.csv` concatenates
THREE batches (2016-12-09 22:55:41, 2016-12-09 23:14:11, 2016-12-10 12:47:10;
207 rows each) — only the 12:47:10 rows match the `_Y_multi` files; never mix.

| Aperture | Filename | Center ROI L,T,R,B | Center MTF50 | Oracle corner max | Argmax N |
|---|---|---|---:|---:|---:|
| f/1.4 | `NIKON D800_50mm_f1.4_.NEF` | `3841,2204,4057,2543` | 0.1029 | 0.0866 | 1 |
| f/1.8 | `NIKON D800_50mm_f1.8_.NEF` | `3841,2204,4057,2543` | 0.1204 | 0.0890 | 1 |
| f/2 | `NIKON D800_50mm_f2_.NEF` | `3841,2204,4057,2542` | 0.1377 | 0.0904 | 1 |
| f/2.8 | `NIKON D800_50mm_f2.8_.NEF` | `3840,2204,4056,2542` | 0.1395 | 0.1364 | 12 |
| f/4 | `NIKON D800_50mm_f4_.NEF` | `3840,2204,4056,2542` | 0.1385 | 0.1647 | 12 |
| f/5.6 | `NIKON D800_50mm_f5.6_.NEF` | `3839,2203,4055,2542` | 0.1649 | 0.1711 | 12 |
| f/8 | `NIKON D800_50mm_f8_.NEF` | `3839,2203,4055,2542` | 0.1831 | 0.1618 | 12 |
| f/11 | `NIKON D800_50mm_f11_.NEF` | `3839,2204,4055,2542` | 0.1707 | 0.1536 | 12 |
| f/16 | `NIKON D800_50mm_f16_.NEF` | `3839,2204,4055,2542` | 0.1583 | 0.1310 | 1 |

D800-specific contract notes (all verified on the real files, 2026-07-08):

- **Label trap is harsher than D810:** the `Region` column labels ALL FOUR
  physical corners (`-4_-2_L_C`, `4_-2_R_C`, `-4_2_L_C`, `4_2_R_C`) as
  `Pt Way`, and the Regions summary line reads `0,Corner`. Physical corners
  come from the `_C` edge-ID suffix only.
- **The D810 aperture-trend gate does not transfer:** oracle plateau minimum
  (f/4 = 0.1385) is below f/16 (0.1583), so
  `min(f/4..f/11) > f/16 > max(f/1.4..f/2)` correctly FAILS. The failure is
  unit-pinned; do not redefine the gate to force a pass.
- **Center-above-corner gates: f/8 and f/11 only.** f/4 and f/5.6 have the
  corner max above center (both oracle and toolkit); f/2.8 is marginal
  (+0.0031) and unpinned.
- Oracle field argmax is N=12 (`0_-2_R_E`, top-center) for f/2.8-f/11. The
  toolkit agrees at f/4-f/11, while f/2.8 is diagnostic-only (toolkit max N=8)
  and f/16 diverges (oracle max N=1, toolkit max N=13). N=13 on D800 is edge ID
  `0_2_L_E` (left-edge; the D810 counterpart is `0_2_R_E`).
- Unoracled RAW files in the folder (no per-file `_Y_multi.csv`): the
  tethered `2016_12_09_SFR_D800_f11_0032.NEF` and three
  `2016_12_09_SFR_D800_test_*.NEF` frames — diagnostic only.

Natural follow-ons are a multi-aperture summary command or an
Imatest-replication A2 path with demosaic/luma/gamma handling.
