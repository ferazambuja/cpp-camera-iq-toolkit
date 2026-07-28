#!/usr/bin/env python3
"""Generate deterministic, dependency-free SVG figures for the portfolio docs."""

from __future__ import annotations

import argparse
import csv
import html
import math
import sys
from pathlib import Path


INK = "#172033"
MUTED = "#5f6b7a"
GRID = "#d8dee8"
PANEL = "#f7f9fc"
BLUE = "#2563eb"
BLUE_LIGHT = "#60a5fa"
ORANGE = "#c2410c"
ORANGE_LIGHT = "#fb923c"
TEAL = "#0f766e"
VIOLET = "#7c3aed"


def esc(value: object) -> str:
    return html.escape(str(value), quote=True)


def read_rows(
    path: Path, expected_headers: list[str], expected_count: int
) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle)
        if reader.fieldnames != expected_headers:
            raise ValueError(
                f"{path.name}: expected headers {expected_headers}, "
                f"got {reader.fieldnames}"
            )
        rows = list(reader)
    if len(rows) != expected_count:
        raise ValueError(
            f"{path.name}: expected {expected_count} rows, got {len(rows)}"
        )
    return rows


def number(
    row: dict[str, str],
    field: str,
    *,
    minimum: float,
    maximum: float,
    allow_empty: bool = False,
) -> float | None:
    text = row[field]
    if allow_empty and text == "":
        return None
    try:
        value = float(text)
    except ValueError as error:
        raise ValueError(f"{field}: expected a number, got {text!r}") from error
    if not math.isfinite(value) or not minimum <= value <= maximum:
        raise ValueError(
            f"{field}: expected a finite value in [{minimum}, {maximum}], "
            f"got {text!r}"
        )
    return value


def svg_start(width: int, height: int, title: str, description: str) -> list[str]:
    return [
        '<?xml version="1.0" encoding="UTF-8"?>',
        (
            f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" '
            f'height="{height}" viewBox="0 0 {width} {height}" role="img" '
            'aria-labelledby="title desc">'
        ),
        f"  <title id=\"title\">{esc(title)}</title>",
        f"  <desc id=\"desc\">{esc(description)}</desc>",
        "  <style>",
        "    text { font-family: Helvetica; fill: #172033; }",
        "    .title { font-size: 25px; font-weight: 700; }",
        "    .subtitle { font-size: 14px; fill: #5f6b7a; }",
        "    .panel-title { font-size: 16px; font-weight: 700; }",
        "    .axis { font-size: 12px; fill: #5f6b7a; }",
        "    .legend { font-size: 12px; }",
        "    .value { font-size: 11px; font-weight: 600; }",
        "    .node-title { font-size: 15px; font-weight: 700; }",
        "    .node-detail { font-size: 12px; fill: #5f6b7a; }",
        "  </style>",
    ]


def line_path(
    points: list[tuple[float, float]], color: str, dashed: bool = False
) -> str:
    coords = " ".join(
        ("M" if index == 0 else "L") + f"{x:.2f},{y:.2f}"
        for index, (x, y) in enumerate(points)
    )
    dash = ' stroke-dasharray="7 5"' if dashed else ""
    return (
        f'  <path d="{coords}" fill="none" stroke="{color}" stroke-width="2.5"'
        f'{dash} stroke-linejoin="round" stroke-linecap="round"/>'
    )


def generate_sfr(data_dir: Path) -> str:
    rows = read_rows(
        data_dir / "sfr_aperture_summary.csv",
        [
            "camera",
            "aperture",
            "toolkit_center_mtf50",
            "advisory_center_mtf50",
            "toolkit_corner_max_mtf50",
        ],
        18,
    )
    apertures = ["1.4", "1.8", "2", "2.8", "4", "5.6", "8", "11", "16"]
    d810_field_apertures = {"4", "5.6", "8", "11"}
    corner_keys = {
        ("D810", aperture) for aperture in d810_field_apertures
    } | {("D800", aperture) for aperture in apertures}
    expected_keys = {
        (camera, aperture)
        for camera in ("D810", "D800")
        for aperture in apertures
    }
    keys = [(row["camera"], row["aperture"]) for row in rows]
    if len(set(keys)) != len(keys) or set(keys) != expected_keys:
        raise ValueError("sfr_aperture_summary.csv: camera/aperture keys invalid")
    center_axis_min, center_axis_max = 0.0, 0.30
    min_margin, max_margin = -0.06, 0.08
    for row in rows:
        toolkit_center = number(
            row,
            "toolkit_center_mtf50",
            minimum=center_axis_min,
            maximum=center_axis_max,
        )
        number(
            row,
            "advisory_center_mtf50",
            minimum=center_axis_min,
            maximum=center_axis_max,
        )
        corner = number(
            row,
            "toolkit_corner_max_mtf50",
            minimum=0.0,
            maximum=0.5,
            allow_empty=True,
        )
        if ((row["camera"], row["aperture"]) in corner_keys) != (
            corner is not None
        ):
            raise ValueError(
                "sfr_aperture_summary.csv: corner values must cover field apertures"
            )
        if corner is not None:
            margin = toolkit_center - corner
            if not min_margin <= margin <= max_margin:
                raise ValueError(
                    "sfr_aperture_summary.csv: center-minus-corner margin "
                    f"{margin} is outside displayed axis "
                    f"[{min_margin}, {max_margin}]"
                )
    width, height = 1000, 650
    out = svg_start(
        width,
        height,
        "D800 and D810 slanted-edge SFR summary",
        (
            "Panel A compares toolkit green-linear center MTF50 with advisory "
            "Imatest values across aperture. Panel B shows toolkit center minus "
            "physical-corner maximum for the four field-map apertures."
        ),
    )
    out += [
        '  <rect width="1000" height="650" rx="16" fill="#ffffff"/>',
        '  <text x="44" y="46" class="title">D800 / D810 slanted-edge SFR</text>',
        (
            '  <text x="44" y="72" class="subtitle">Solid: toolkit green-linear '
            'MTF50 · dashed: advisory Imatest center values</text>'
        ),
        '  <rect x="36" y="94" width="596" height="500" rx="12" fill="#f7f9fc"/>',
        '  <rect x="650" y="94" width="314" height="500" rx="12" fill="#f7f9fc"/>',
        '  <text x="58" y="124" class="panel-title">A · Center MTF50 vs aperture</text>',
        '  <text x="672" y="124" class="panel-title">B · Center − corner margin</text>',
    ]

    x0, y0, plot_w, plot_h = 74, 154, 530, 365
    center_tick_step = 0.05
    center_tick_count = round(
        (center_axis_max - center_axis_min) / center_tick_step
    )

    def center_y(value: float) -> float:
        return (
            y0
            + plot_h
            - (value - center_axis_min)
            / (center_axis_max - center_axis_min)
            * plot_h
        )

    for tick in range(center_tick_count + 1):
        value = center_axis_min + tick * center_tick_step
        y = center_y(value)
        out.append(
            f'  <line x1="{x0}" y1="{y:.2f}" x2="{x0 + plot_w}" y2="{y:.2f}" '
            f'stroke="{GRID}" stroke-width="1"/>'
        )
        out.append(
            f'  <text x="{x0 - 10}" y="{y + 4:.2f}" text-anchor="end" '
            f'class="axis">{value:.2f}</text>'
        )
    for index, aperture in enumerate(apertures):
        x = x0 + index * plot_w / (len(apertures) - 1)
        out.append(
            f'  <text x="{x:.2f}" y="{y0 + plot_h + 24}" text-anchor="middle" '
            f'class="axis">f/{aperture}</text>'
        )
    out.append(
        f'  <text x="{x0 - 52}" y="{y0 + plot_h / 2}" text-anchor="middle" '
        'class="axis" transform="rotate(-90 22 336)">MTF50 (cycles/pixel)</text>'
    )

    series = [
        ("D810", "toolkit_center_mtf50", BLUE, False),
        ("D810", "advisory_center_mtf50", BLUE_LIGHT, True),
        ("D800", "toolkit_center_mtf50", ORANGE, False),
        ("D800", "advisory_center_mtf50", ORANGE_LIGHT, True),
    ]
    by_camera = {
        camera: {row["aperture"]: row for row in rows if row["camera"] == camera}
        for camera in ("D810", "D800")
    }
    for camera, key, color, dashed in series:
        points = []
        for index, aperture in enumerate(apertures):
            value = float(by_camera[camera][aperture][key])
            x = x0 + index * plot_w / (len(apertures) - 1)
            y = center_y(value)
            points.append((x, y))
        out.append(line_path(points, color, dashed))
        if not dashed:
            for x, y in points:
                out.append(
                    f'  <circle cx="{x:.2f}" cy="{y:.2f}" r="3.5" fill="{color}"/>'
                )

    legend = [
        (BLUE, False, "D810 toolkit"),
        (BLUE_LIGHT, True, "D810 advisory"),
        (ORANGE, False, "D800 toolkit"),
        (ORANGE_LIGHT, True, "D800 advisory"),
    ]
    for index, (color, dashed, label) in enumerate(legend):
        x = 82 + (index % 2) * 190
        y = 557 + (index // 2) * 22
        dash = ' stroke-dasharray="7 5"' if dashed else ""
        out.append(
            f'  <line x1="{x}" y1="{y}" x2="{x + 28}" y2="{y}" '
            f'stroke="{color}" stroke-width="2.5"{dash}/>'
        )
        out.append(f'  <text x="{x + 36}" y="{y + 4}" class="legend">{label}</text>')

    field_apertures = ["4", "5.6", "8", "11"]
    fx0, fy0, fplot_w, fplot_h = 684, 164, 246, 345

    def field_y(value: float) -> float:
        return fy0 + fplot_h - (value - min_margin) / (max_margin - min_margin) * fplot_h

    for value in (-0.06, -0.04, -0.02, 0.0, 0.02, 0.04, 0.06, 0.08):
        y = field_y(value)
        stroke = MUTED if value == 0 else GRID
        stroke_width = 1.5 if value == 0 else 1
        out.append(
            f'  <line x1="{fx0}" y1="{y:.2f}" x2="{fx0 + fplot_w}" '
            f'y2="{y:.2f}" stroke="{stroke}" stroke-width="{stroke_width}"/>'
        )
        out.append(
            f'  <text x="{fx0 - 8}" y="{y + 4:.2f}" text-anchor="end" '
            f'class="axis">{value:+.02f}</text>'
        )
    group_w = fplot_w / len(field_apertures)
    bar_w = 18
    for index, aperture in enumerate(field_apertures):
        center = fx0 + group_w * (index + 0.5)
        out.append(
            f'  <text x="{center:.2f}" y="{fy0 + fplot_h + 22}" '
            f'text-anchor="middle" class="axis">f/{aperture}</text>'
        )
        for camera, offset, color in (("D810", -11, BLUE), ("D800", 11, ORANGE)):
            row = by_camera[camera][aperture]
            margin = float(row["toolkit_center_mtf50"]) - float(
                row["toolkit_corner_max_mtf50"]
            )
            zero_y = field_y(0)
            value_y = field_y(margin)
            top = min(zero_y, value_y)
            height_value = abs(zero_y - value_y)
            out.append(
                f'  <rect x="{center + offset - bar_w / 2:.2f}" y="{top:.2f}" '
                f'width="{bar_w}" height="{height_value:.2f}" rx="2" fill="{color}"/>'
            )
    out += [
        '  <rect x="704" y="548" width="10" height="10" rx="2" fill="#2563eb"/>',
        '  <text x="720" y="557" class="legend">D810</text>',
        '  <rect x="790" y="548" width="10" height="10" rx="2" fill="#c2410c"/>',
        '  <text x="806" y="557" class="legend">D800</text>',
        '  <text x="672" y="580" class="subtitle">Positive means the center exceeds</text>',
        '  <text x="672" y="598" class="subtitle">the strongest physical corner.</text>',
        (
            '  <text x="44" y="626" class="subtitle">Aggregate measurements from '
            'the publication-safe table in docs/data/sfr_aperture_summary.csv.</text>'
        ),
        "</svg>",
    ]
    return "\n".join(out) + "\n"


def generate_spectral(data_dir: Path) -> str:
    rows = read_rows(
        data_dir / "spectral_color_fidelity.csv",
        [
            "camera",
            "ssf_source",
            "smi_cc18",
            "smi_cc24",
            "smi_sg140",
            "luther_quality_index",
        ],
        5,
    )
    if len({row["camera"] for row in rows}) != len(rows):
        raise ValueError("spectral_color_fidelity.csv: duplicate camera")
    for row in rows:
        if not row["camera"] or not row["ssf_source"]:
            raise ValueError("spectral_color_fidelity.csv: blank identity field")
        for field in ("smi_cc18", "smi_cc24", "smi_sg140"):
            number(row, field, minimum=86.0, maximum=94.0)
        number(row, "luther_quality_index", minimum=0.0, maximum=1.0)
    width, height = 960, 560
    out = svg_start(
        width,
        height,
        "Five-camera spectral color-fidelity comparison",
        (
            "Grouped markers compare ISO 17321-style SMI approximations over the "
            "18 chromatic ColorChecker patches, the full 24-patch chart, and "
            "the 140-patch ColorChecker SG."
        ),
    )
    out += [
        '  <rect width="960" height="560" rx="16" fill="#ffffff"/>',
        '  <text x="44" y="48" class="title">Spectral color-fidelity comparison</text>',
        (
            '  <text x="44" y="74" class="subtitle">Higher is better · CIE D55 · '
            'ISO 17321-style approximation, not certified Annex-B equivalence</text>'
        ),
        '  <rect x="36" y="98" width="888" height="402" rx="12" fill="#f7f9fc"/>',
    ]
    x0, y0, plot_w, plot_h = 92, 128, 794, 292
    min_y, max_y = 86.0, 94.0
    for value in range(86, 95, 2):
        y = y0 + plot_h - (value - min_y) / (max_y - min_y) * plot_h
        out.append(
            f'  <line x1="{x0}" y1="{y:.2f}" x2="{x0 + plot_w}" y2="{y:.2f}" '
            f'stroke="{GRID}"/>'
        )
        out.append(
            f'  <text x="{x0 - 10}" y="{y + 4:.2f}" text-anchor="end" '
            f'class="axis">{value}</text>'
        )
    colors = [BLUE, TEAL, VIOLET]
    fields = ["smi_cc18", "smi_cc24", "smi_sg140"]
    labels = ["CC-18 chromatic", "CC-24", "SG-140"]
    group_w = plot_w / len(rows)
    for row_index, row in enumerate(rows):
        group_center = x0 + group_w * (row_index + 0.5)
        for series_index, (field, color) in enumerate(zip(fields, colors)):
            value = float(row[field])
            y = y0 + plot_h - (value - min_y) / (max_y - min_y) * plot_h
            x = group_center + (series_index - 1) * 30
            out.append(
                f'  <circle class="score-marker" cx="{x:.2f}" cy="{y:.2f}" '
                f'r="7" fill="{color}" stroke="#ffffff" stroke-width="2"/>'
            )
        camera = row["camera"].replace(" ", "\u00a0")
        out.append(
            f'  <text x="{group_center:.2f}" y="{y0 + plot_h + 24}" '
            f'text-anchor="middle" class="axis">{esc(camera)}</text>'
        )
        out.append(
            f'  <text x="{group_center:.2f}" y="{y0 + plot_h + 42}" '
            f'text-anchor="middle" class="axis">QI {float(row["luther_quality_index"]):.3f}</text>'
        )
    for index, (label, color) in enumerate(zip(labels, colors)):
        x = 210 + index * 190
        out.append(
            f'  <circle cx="{x + 6}" cy="478" r="6" fill="{color}"/>'
        )
        out.append(f'  <text x="{x + 19}" y="482" class="legend">{label}</text>')
    out += [
        (
            '  <text x="44" y="530" class="subtitle">Canon uses toolkit RAW '
            'extraction; the other rows use measured legacy SSFs. QI is the '
            'separate Luther-fit quality index.</text>'
        ),
        "</svg>",
    ]
    return "\n".join(out) + "\n"


def generate_ccm(data_dir: Path) -> str:
    rows = read_rows(
        data_dir / "ccm_validation_summary.csv",
        [
            "evaluation",
            "patches",
            "mean_ciede2000",
            "heldout_mean_ciede2000",
        ],
        4,
    )
    if len({row["evaluation"] for row in rows}) != len(rows):
        raise ValueError("ccm_validation_summary.csv: duplicate evaluation")
    for row in rows:
        try:
            patches = int(row["patches"])
        except ValueError as error:
            raise ValueError(
                "ccm_validation_summary.csv: invalid patch count"
            ) from error
        if not row["evaluation"] or not 1 <= patches <= 1000:
            raise ValueError("ccm_validation_summary.csv: invalid row identity")
        number(row, "mean_ciede2000", minimum=0.0, maximum=9.0)
        number(
            row,
            "heldout_mean_ciede2000",
            minimum=0.0,
            maximum=9.0,
            allow_empty=True,
        )
    width, height = 920, 560
    out = svg_start(
        width,
        height,
        "ColorChecker CCM validation summary",
        (
            "Bars compare mean CIEDE2000 for a corrected RAW patch table, a "
            "lightness-filtered kept set, all patches under the kept-set fit, "
            "and the excluded dark patches. Held-out values are shown where defined."
        ),
    )
    out += [
        '  <rect width="920" height="560" rx="16" fill="#ffffff"/>',
        '  <text x="44" y="48" class="title">ColorChecker / CCM validation</text>',
        (
            '  <text x="44" y="74" class="subtitle">Corrected RAW patches · '
            'linear 3×3 RGB→XYZ model · lower CIEDE2000 is better</text>'
        ),
        '  <rect x="36" y="98" width="848" height="390" rx="12" fill="#f7f9fc"/>',
    ]
    x0, y0, plot_w, plot_h = 88, 126, 756, 276
    max_y = 9.0
    for value in range(0, 10):
        y = y0 + plot_h - value / max_y * plot_h
        out.append(
            f'  <line x1="{x0}" y1="{y:.2f}" x2="{x0 + plot_w}" y2="{y:.2f}" '
            f'stroke="{GRID}"/>'
        )
        if value % 2 == 0:
            out.append(
                f'  <text x="{x0 - 10}" y="{y + 4:.2f}" text-anchor="end" '
                f'class="axis">{value}</text>'
            )
    group_w = plot_w / len(rows)
    bar_w = 42
    for index, row in enumerate(rows):
        center = x0 + group_w * (index + 0.5)
        mean = float(row["mean_ciede2000"])
        values = [(mean, TEAL)]
        if row["heldout_mean_ciede2000"]:
            values.append((float(row["heldout_mean_ciede2000"]), VIOLET))
        offsets = [0] if len(values) == 1 else [-24, 24]
        for (value, color), offset in zip(values, offsets):
            y = y0 + plot_h - value / max_y * plot_h
            out.append(
                f'  <rect x="{center + offset - bar_w / 2:.2f}" y="{y:.2f}" '
                f'width="{bar_w}" height="{y0 + plot_h - y:.2f}" rx="3" fill="{color}"/>'
            )
            out.append(
                f'  <text x="{center + offset:.2f}" y="{y - 7:.2f}" '
                f'text-anchor="middle" class="value">{value:.3f}</text>'
            )
        words = row["evaluation"].split()
        midpoint = (len(words) + 1) // 2
        first = " ".join(words[:midpoint])
        second = " ".join(words[midpoint:])
        out.append(
            f'  <text x="{center:.2f}" y="{y0 + plot_h + 24}" text-anchor="middle" '
            f'class="axis">{esc(first)}</text>'
        )
        if second:
            out.append(
                f'  <text x="{center:.2f}" y="{y0 + plot_h + 41}" text-anchor="middle" '
                f'class="axis">{esc(second)}</text>'
            )
        out.append(
            f'  <text x="{center:.2f}" y="{y0 + plot_h + 59}" text-anchor="middle" '
            f'class="axis">n={row["patches"]}</text>'
        )
    out += [
        '  <rect x="286" y="467" width="12" height="12" rx="2" fill="#0f766e"/>',
        '  <text x="305" y="477" class="legend">fit/evaluation mean</text>',
        '  <rect x="470" y="467" width="12" height="12" rx="2" fill="#7c3aed"/>',
        '  <text x="489" y="477" class="legend">5-fold held-out mean</text>',
        (
            '  <text x="44" y="528" class="subtitle">The kept-set reduction is '
            'reported as a flare-handling policy; excluded patches remain visible '
            'and are not evidence of a better camera model.</text>'
        ),
        "</svg>",
    ]
    return "\n".join(out) + "\n"


def generate_architecture() -> str:
    width, height = 1000, 470
    out = svg_start(
        width,
        height,
        "Camera IQ toolkit processing architecture",
        (
            "Dataset IDs and public fixtures enter a thin command-line launcher. "
            "A single static core contains command parsing, measurement algorithms, "
            "validation and serialization, backed by LibRaw and spectral references."
        ),
    )
    out += [
        '  <defs><marker id="arrow" viewBox="0 0 10 10" refX="9" refY="5" '
        'markerWidth="7" markerHeight="7" orient="auto-start-reverse">'
        '<path d="M 0 0 L 10 5 L 0 10 z" fill="#5f6b7a"/></marker></defs>',
        '  <rect width="1000" height="470" rx="16" fill="#ffffff"/>',
        '  <text x="44" y="48" class="title">Measurement architecture</text>',
        (
            '  <text x="44" y="74" class="subtitle">The executable is a thin '
            'launcher; command and algorithm layers currently share one C++20 static core.</text>'
        ),
    ]
    nodes = [
        (42, 120, 190, 96, "#eff6ff", "Inputs", "RAW via dataset IDs", "CSV / spectra / fixtures"),
        (278, 120, 170, 96, "#f5f3ff", "CLI launcher", "camera_iq", "argument routing"),
        (494, 104, 292, 128, "#ecfdf5", "camera_iq_core", "commands + validation", "algorithms + JSON/CSV"),
        (832, 120, 126, 96, "#fff7ed", "Outputs", "JSON / CSV", "evidence reports"),
    ]
    for x, y, w, h, fill, title, detail1, detail2 in nodes:
        out.append(
            f'  <rect x="{x}" y="{y}" width="{w}" height="{h}" rx="12" '
            f'fill="{fill}" stroke="{GRID}"/>'
        )
        out.append(f'  <text x="{x + 18}" y="{y + 31}" class="node-title">{title}</text>')
        out.append(f'  <text x="{x + 18}" y="{y + 57}" class="node-detail">{detail1}</text>')
        out.append(f'  <text x="{x + 18}" y="{y + 77}" class="node-detail">{detail2}</text>')
    for x1, x2 in ((232, 278), (448, 494), (786, 832)):
        out.append(
            f'  <line x1="{x1}" y1="168" x2="{x2 - 8}" y2="168" '
            f'stroke="{MUTED}" stroke-width="2" marker-end="url(#arrow)"/>'
        )
    subnodes = [
        ("#eff6ff", "LibRaw front end", "unpack · active CFA", "black handling"),
        (
            "#f0fdfa",
            "Measurement methods",
            "patches · CCM · spectral",
            "OECF · noise · SFR",
        ),
        (
            "#f5f3ff",
            "Verification",
            "synthetic · fixture",
            "archive · negative paths",
        ),
    ]
    for index, (fill, title, detail1, detail2) in enumerate(subnodes):
        x = 162 + index * 238
        y = 278
        out.append(
            f'  <rect x="{x}" y="{y}" width="202" height="92" rx="10" '
            f'fill="{fill}" stroke="{GRID}"/>'
        )
        out.append(
            f'  <text x="{x + 14}" y="{y + 29}" class="node-title">{title}</text>'
        )
        out.append(
            f'  <text x="{x + 14}" y="{y + 52}" class="node-detail">{detail1}</text>'
        )
        out.append(
            f'  <text x="{x + 14}" y="{y + 70}" class="node-detail">{detail2}</text>'
        )
        out.append(
            f'  <line x1="{640}" y1="232" x2="{x + 101}" y2="{y - 8}" '
            f'stroke="{GRID}" stroke-width="1.5"/>'
        )
    out += [
        (
            '  <text x="44" y="445" class="subtitle">Bulk capture data remains '
            'outside Git; committed public artifacts are code, tests, safe '
            'aggregates, figures and technical reports.</text>'
        ),
        "</svg>",
    ]
    return "\n".join(out) + "\n"


def outputs(repo_root: Path) -> dict[Path, str]:
    data_dir = repo_root / "docs" / "data"
    figure_dir = repo_root / "docs" / "figures"
    return {
        figure_dir / "architecture.svg": generate_architecture(),
        figure_dir / "sfr_aperture_field.svg": generate_sfr(data_dir),
        figure_dir / "spectral_color_fidelity.svg": generate_spectral(data_dir),
        figure_dir / "ccm_validation.svg": generate_ccm(data_dir),
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo-root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    repo_root = args.repo_root.resolve()
    generated = outputs(repo_root)
    stale: list[str] = []
    for path, content in generated.items():
        if args.check:
            if not path.exists() or path.read_text(encoding="utf-8") != content:
                stale.append(str(path.relative_to(repo_root)))
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content, encoding="utf-8")
            print(f"wrote {path.relative_to(repo_root)}")
    if stale:
        print("portfolio figures are missing or stale:", file=sys.stderr)
        for path in stale:
            print(f"  {path}", file=sys.stderr)
        print("run tools/generate_portfolio_figures.py", file=sys.stderr)
        return 1
    if args.check:
        print(f"portfolio figures current: {len(generated)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
