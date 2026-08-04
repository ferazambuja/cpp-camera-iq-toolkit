# Gamut-mapping implementation

[Implementation index](README.md) ·
[case study](../case-studies/gamut-mapping.md) ·
[scientific report](../reports/GAMUT_MAPPING.md) ·
[synthetic input](../data/gamut_synthetic_input.csv)

## Software boundary

The gamut engine uses distinct types for encoded RGB, linear RGB, XYZ, CIELAB,
OkLab, and OkLCh. A mapping request names the source and destination RGB spaces,
the mapping intent, and its numeric options. The core returns both the mapped
color and evidence about the selected branch and destination boundary.

## Code-level data flow

```text
RFC 4180 input RGB rows + source/destination + intent
  -> EncodedRgb validation
  -> transfer-function decode
  -> source linear RGB -> relative D65 XYZ
  -> CIELAB or OkLCh coordinates
  -> destination-gamut membership test
  -> selected boundary search and mapping rule
  -> destination XYZ -> linear RGB -> encoded RGB
  -> GamutMappingResult + diagnostics
  -> JSON/CSV report -> deterministic comparison figure
```

## Color transforms

sRGB and Display-P3 share the same piecewise encoding curve in this
implementation:

```text
decode(v) = v / 12.92                         when v <= 0.04045
          = ((v + 0.055) / 1.055)^2.4        otherwise

encode(v) = 12.92 v                           when v <= 0.0031308
          = 1.055 v^(1/2.4) - 0.055          otherwise
```

Rational D65 RGB/XYZ matrices are stored as constants from the cited CSS Color
4 conversion sample. CIELAB conversion uses the D65 white for the mapping
comparison. OkLab conversion uses explicit XYZ/LMS matrices and signed cube
roots. Destination membership is evaluated in linear RGB against the unit cube
with a declared tolerance.

## Analytic boundary search

For a fixed lightness and hue, radial mapping varies only chroma. In CIELAB:

```text
a(C) = C cos(h)
b(C) = C sin(h)
```

Within each branch of the Lab inverse function, XYZ and therefore every linear
RGB channel are cubic polynomials in `C`. The implementation partitions the ray
at Lab branch transitions, derives each channel polynomial, finds its derivative
critical points, and brackets every crossing of the destination surfaces
`channel = 0` and `channel = 1`. This is why a ray that leaves and later
re-enters the cube is not mistaken for one continuous in-gamut interval.

The OkLCh boundary uses the same idea: OkLab-to-LMS is affine in chroma at fixed
lightness and hue; cubing LMS makes each destination RGB channel a cubic in
chroma. Roots are refined to the declared chroma tolerance and intervals are
classified by testing their interiors.

## Mapping intents

- **CIELAB radial** selects the first neutral-connected destination interval at
  fixed `L*` and Lab hue.
- **OkLCh radial** selects the analogous boundary at fixed OkLab lightness and
  OkLCh hue.
- **CSS Local MINDE** follows the dated binary-search algorithm and uses
  CIEDE2000 to decide when clipped and candidate colors are locally close.
- **Protected-core soft compression** leaves a declared core untouched and
  applies a smooth shoulder toward the radial boundary. It is labeled as an
  experimental design choice.

Already-in-gamut colors follow an explicit identity branch for the hard mapping
intents. The soft method can modify in-gamut shoulder colors by design, so that
branch is recorded separately.

## Diagnostics and output

`GamutMappingResult` stores input and output coordinates, the boundary result,
selected algorithm branch, chroma utilization, and Local-MINDE evidence where
applicable. The report layer recomputes CIEDE2000, OkLab distance, Lab/OkLCh hue
change, IPT hue change, gamut margin, and percentile summaries from typed
results.

Non-finite inputs, encoded components outside `[0,1]`, invalid search options,
unresolved boundaries, or out-of-gamut final outputs are refused. Final linear
RGB clamping is limited to tolerance-scale numerical cleanup after membership
has been established; it is not the mapping algorithm.

## Source and tests

- Types and public API: [`gamut_mapping.hpp`](../../include/camera_iq/gamut_mapping.hpp)
- Transforms, boundary search, and mapping intents:
  [`gamut_mapping.cpp`](../../src/gamut_mapping.cpp)
- Report aggregation and serialization:
  [`gamut_mapping_report.cpp`](../../src/gamut_mapping_report.cpp)
- Command layer: [`cmd_gamut_map.cpp`](../../src/cmd_gamut_map.cpp)
- Core and report tests: [`test_gamut_mapping.cpp`](../../tests/test_gamut_mapping.cpp),
  [`test_gamut_mapping_report.cpp`](../../tests/test_gamut_mapping_report.cpp),
  [`test_cmd_gamut_map.cpp`](../../tests/test_cmd_gamut_map.cpp)
- Optional independent transform reference:
  [`test_gamut_lcms.cpp`](../../tests/test_gamut_lcms.cpp)
- Artifact check: [`generate_gamut_portfolio.py`](../../tools/generate_gamut_portfolio.py)
