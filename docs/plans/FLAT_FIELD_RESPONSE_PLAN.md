# Relative CFA Flat-Field Response — Implementation Record

## Measurement objective

`camera_iq shading` characterizes spatial response in a black-subtracted Bayer
mosaic. It reports a center-normalized capture-system response; it does not
fit a correction surface or isolate optical vignetting from illumination,
alignment, microlens response, or mechanical shading.

The public result consists of:

- a 16 × 12 median response map for each CFA position;
- center-normalized `C_RG`, `C_BG`, and `C_G1G2` maps;
- center and four-corner block statistics;
- a green quadrant-asymmetry statistic;
- quality-gate, pedestal, and repeat-capture diagnostics;
- JSON, long-form CSV, aggregate tables, and a deterministic SVG.

## Data and current result

The configured `clrs589_project_camera` archive was reachable on 2026-07-31.
The analysis reprocessed all 52 RAF files under `Images/Sphere` with one
ceiling definition:

```text
signal_ceiling[p] = white_level - black_per_channel[p]
near_ceiling[p]   = 0.98 * signal_ceiling[p]
```

The X-T100 metadata yields a 15,359 DN signal-referred ceiling from a 16,383 DN
raw white level and 1,024 DN effective black level. The command accepted three
f/8 frames and rejected 49 frames at the near-ceiling gate. All f/5.6 and f/9
frames were rejected; this archive therefore cannot support an aperture trend.

The primary and repeat f/8, 1/1000 s frames produced a maximum corner/plane
difference of 0.378748 percentage points and an RMS difference of 0.181309
percentage points. The primary green quadrant asymmetry was 0.196484, above the
declared 0.05 project policy. `A` diagnoses departure from a centered radial
scalar model; missing source/rotation controls require capture-system framing
regardless of its verdict.

The f/8, 1/500 s discriminator demonstrates why the gate region differs from a
whole-frame check: the worst green plane was 11.6319% near ceiling inside the
central gate but only 0.4964% over the full frame.

## Geometry contract

Three CFA-balanced mosaic regions have separate jobs:

| Region | Default | Purpose |
|---|---:|---|
| Gate | central 20% by linear dimension | near-ceiling verdict |
| Center/corners | 400 px square, 120 px corner inset | normalizer, low-signal check, corner statistics |
| Grid | 16 × 12 per CFA plane | spatial response maps |

The implementation reports the effective rectangles, requires an even mosaic,
requires the gate to contain the normalizer, and rejects overlapping or
non-fitting blocks. It never silently clips a requested block into a different
test.

## Response definitions

For CFA plane `X`, with `X_c` the center-block median:

```text
R_X(x,y) = X(x,y) / X_c
G(x,y)   = [R_G1(x,y) + R_G2(x,y)] / 2
C_RG     = R_R / G
C_BG     = R_B / G
C_G1G2   = R_G1 / R_G2
```

The center-block median normalizes to one; individual center samples and grid
bins need not equal one. `C_G1G2` equals one when the independently normalized
green planes have the same spatial response. A uniform green gain difference
cancels and is not diagnosed by this map.

Green quadrant asymmetry is computed over the four corner blocks:

```text
A = (max corner G - min corner G) / mean corner G
```

The 0.05 threshold is a declared project policy, not an industry standard or a
value derived from the two-frame pair. It flags departure from a centered radial
scalar model and never grants or refuses attribution to a physical component.

## Acceptance and evidence rules

- Near-ceiling fractions are reported for both the center gate and full frame;
  the center-gate fraction controls the verdict.
- Low signal is evaluated at the center block because that block is the
  denominator for every map.
- Negative residual, aggregate-finiteness, and independently binding finite-bin
  coverage checks remain explicit.
- Rejected frames retain diagnostics and omit derived maps.
- A dark control is marked verified only when every sample is finite, dimensions
  and CFA match, camera make/model and positive aperture/shutter/ISO metadata
  match, and full-frame plus center/four-corner medians pass a 1 DN tolerance.
- Dark measurements verify only those declared checks; they are never applied
  to the source samples.
- `--compare` writes both measurements and the corner-delta statistics in one
  JSON document and appends both result sets to the CSV.

## Public artifacts

- `docs/case-studies/cfa-flat-field-response.md`
- `docs/reports/FLAT_FIELD_RESPONSE.md`
- `docs/data/flat_field_summary.csv`
- `docs/data/flat_field_response.csv`
- `docs/figures/flat_field_response.svg`
- `docs/images/flat-field-sphere.jpg`
- `tools/export_shading_portfolio.py`
- `tools/generate_portfolio_figures.py`

The RAW files remain outside Git. Dataset IDs and relative filenames provide
reproducibility without exposing local archive paths.

## Future measurements

Separating source nonuniformity, lens response, and sensor angular response
would require additional captures such as source rotation, camera rotation,
multiple unsaturated apertures, and an independently characterized uniform
source. A correction path would additionally require applying a bounded gain
map and remeasuring an independent capture.

## Verification record

A test count on its own is not auditable. Every claim below names the commit,
toolchain, command, and result so it can be re-run or contradicted.

| Item | Value |
|---|---|
| Commit under test | see `git rev-parse HEAD` at the time of the run |
| Branch | `feature/cfa-flat-field-response` |
| Local toolchain | Apple clang 21.0.0, CMake 4.4.2, macOS arm64 |
| Configure | `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release` |
| Build | `cmake --build build --parallel` — no errors, no warnings |
| Test | `ctest --test-dir build` — `100% tests passed out of 51` |
| Docs guard | `python3 tools/check_portfolio_docs.py --repo-root .` — 30 Markdown files, 18 reports |

Linux CI on the pushed branch, run `30723351641`:

| Job | Result |
|---|---|
| `build-and-test (gcc)` | pass |
| `build-and-test (clang)` | pass |
| `undefined-behavior-sanitizer (clang)` | pass |

Known history worth keeping rather than smoothing over:

- An earlier pushed revision failed GCC, Clang and UBSan on the same overload
  ambiguity: a `long long` cast against `JsonWriter`'s `double`/`int64_t`/`int`
  overloads, whose resolution differs by platform. Fixed in `f1a353d` by
  serializing the count as `std::int64_t`.
- Implementation and tests landed together in `f958df9`, so the repository does
  not contain independent red/green commits for the first two cycles. The cycles
  happened, but the commit history is not evidence of them. Later cycles should
  commit the failing test separately if that evidence is wanted.
- `build/compile_commands.json` is now generated and symlinked at the repository
  root. Editor diagnostics that disagree with the build are now falsifiable
  rather than dismissible.
