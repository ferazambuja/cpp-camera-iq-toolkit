#!/usr/bin/env python3
"""Generate the spectroradiometer measurement-group summary figure."""

from __future__ import annotations

import argparse
import csv
import html
import pathlib
import statistics
import sys


def load_rows(path: pathlib.Path) -> tuple[list[dict[str, str]], int]:
    with path.open(newline="", encoding="utf-8") as stream:
        rows = list(csv.DictReader(stream))
    repeated = [row for row in rows if int(row["count"]) >= 2]
    singletons = len(rows) - len(repeated)
    if len(repeated) != 37 or singletons != 3:
        raise ValueError("expected 37 repeated groups and three singletons")
    return repeated, singletons


def render(rows: list[dict[str, str]], singletons: int) -> str:
    width, height = 1200, 650
    cv = [100.0 * float(row["coefficient_of_variation"]) for row in rows]
    duv = [
        1000.0 * float(row["max_pair_delta_u_prime_v_prime"]) for row in rows
    ]
    shape = [100.0 * float(row["max_shape_relative_l2"]) for row in rows]
    left = (70, 110, 650, 430)
    right = (800, 110, 330, 430)

    lines = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
        '<rect width="1200" height="650" fill="#f8fafc"/>',
        '<style>text{font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif;fill:#172033}.title{font-size:25px;font-weight:700}.subtitle{font-size:14px;fill:#4b5563}.axis{font-size:12px;fill:#4b5563}.label{font-size:10px;fill:#4b5563}.note{font-size:13px;fill:#374151}</style>',
        '<text x="70" y="45" class="title">Spectroradiometer measurement-group variation</text>',
        '<text x="70" y="72" class="subtitle">Absolute level, normalized spectral shape, and recorded-XYZ chromaticity are reported separately</text>',
    ]

    x0, y0, plot_w, plot_h = left
    for tick in range(0, 46, 10):
        y = y0 + plot_h - tick / 45.0 * plot_h
        lines.append(f'<line x1="{x0}" y1="{y:.1f}" x2="{x0+plot_w}" y2="{y:.1f}" stroke="#dbe3ec"/>')
        lines.append(f'<text x="{x0-12}" y="{y+4:.1f}" text-anchor="end" class="axis">{tick}%</text>')
    bar_w = plot_w / len(rows)
    for index, (row, value) in enumerate(zip(rows, cv)):
        x = x0 + index * bar_w + 1
        h = value / 45.0 * plot_h
        color = "#2563eb" if row["group_id"].startswith("scene_") else "#0f766e"
        lines.append(f'<rect x="{x:.1f}" y="{y0+plot_h-h:.1f}" width="{max(2.0, bar_w-2):.1f}" height="{h:.1f}" fill="{color}" opacity="0.85"/>')
    lines.extend([
        f'<line x1="{x0}" y1="{y0+plot_h}" x2="{x0+plot_w}" y2="{y0+plot_h}" stroke="#64748b"/>',
        f'<text x="{x0+plot_w/2}" y="{y0+plot_h+42}" text-anchor="middle" class="axis">37 groups with n ≥ 2</text>',
        f'<text x="25" y="{y0+plot_h/2}" transform="rotate(-90 25 {y0+plot_h/2})" text-anchor="middle" class="axis">Spectral-integral CV</text>',
        f'<text x="{x0}" y="{y0-18}" class="note">Median {statistics.median(cv):.2f}% · maximum {max(cv):.2f}%</text>',
        f'<rect x="{x0+440}" y="{y0-31}" width="12" height="12" fill="#0f766e"/><text x="{x0+458}" y="{y0-20}" class="axis">ramp/reference</text>',
        f'<rect x="{x0+560}" y="{y0-31}" width="12" height="12" fill="#2563eb"/><text x="{x0+578}" y="{y0-20}" class="axis">scene</text>',
    ])

    x0, y0, plot_w, plot_h = right
    max_x, max_y = 45.0, 3.0
    for tick in (0, 10, 20, 30, 40):
        x = x0 + tick / max_x * plot_w
        lines.append(f'<line x1="{x:.1f}" y1="{y0}" x2="{x:.1f}" y2="{y0+plot_h}" stroke="#e2e8f0"/>')
        lines.append(f'<text x="{x:.1f}" y="{y0+plot_h+20}" text-anchor="middle" class="axis">{tick}%</text>')
    for tick in (0, 1, 2, 3):
        y = y0 + plot_h - tick / max_y * plot_h
        lines.append(f'<line x1="{x0}" y1="{y:.1f}" x2="{x0+plot_w}" y2="{y:.1f}" stroke="#e2e8f0"/>')
        lines.append(f'<text x="{x0-10}" y="{y+4:.1f}" text-anchor="end" class="axis">{tick}</text>')
    for row, x_value, y_value, shape_value in zip(rows, cv, duv, shape):
        x = x0 + x_value / max_x * plot_w
        y = y0 + plot_h - y_value / max_y * plot_h
        radius = 4.0 + min(shape_value, 1.2) * 4.0
        lines.append(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="{radius:.1f}" fill="#ea580c" opacity="0.70"><title>{html.escape(row["group_id"])}: CV {x_value:.2f}%, Δu′v′ {y_value/1000:.6f}, shape {shape_value:.3f}%</title></circle>')
    lines.extend([
        f'<line x1="{x0}" y1="{y0+plot_h}" x2="{x0+plot_w}" y2="{y0+plot_h}" stroke="#64748b"/>',
        f'<line x1="{x0}" y1="{y0}" x2="{x0}" y2="{y0+plot_h}" stroke="#64748b"/>',
        f'<text x="{x0+plot_w/2}" y="{y0+plot_h+45}" text-anchor="middle" class="axis">Spectral-integral CV</text>',
        f'<text x="{x0-48}" y="{y0+plot_h/2}" transform="rotate(-90 {x0-48} {y0+plot_h/2})" text-anchor="middle" class="axis">Maximum pair Δu′v′ × 1000</text>',
        f'<text x="{x0}" y="{y0-18}" class="note">Circle size = normalized-shape residual</text>',
        f'<text x="70" y="603" class="note">Three singleton groups are retained but excluded from variation statistics.</text>',
        f'<text x="70" y="626" class="note">Cause of the observed within-group variation is unresolved.</text>',
        '</svg>',
    ])
    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo-root", type=pathlib.Path, default=pathlib.Path(__file__).resolve().parents[1])
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    source = args.repo_root / "docs/data/spectro_group_summary.csv"
    target = args.repo_root / "docs/figures/spectro_group_variation.svg"
    try:
        rows, singletons = load_rows(source)
        expected = render(rows, singletons)
        if args.check:
            if not target.exists() or target.read_text(encoding="utf-8") != expected:
                print(f"stale spectro figure: {target}", file=sys.stderr)
                return 1
        else:
            target.write_text(expected, encoding="utf-8")
        return 0
    except (KeyError, OSError, ValueError) as error:
        print(f"spectro figure: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
