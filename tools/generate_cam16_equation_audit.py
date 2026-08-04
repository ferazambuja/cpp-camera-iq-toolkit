#!/usr/bin/env python3
"""Generate and verify the bounded CAM16 equation-audit portfolio artifacts."""

from __future__ import annotations

import argparse
import csv
import json
import math
import subprocess
import tempfile
from pathlib import Path


NUMERIC_ABS_TOLERANCE = 1e-12
NUMERIC_REL_TOLERANCE = 1e-12


def _numbers_close(left: float, right: float) -> bool:
    return math.isfinite(left) and math.isfinite(right) and math.isclose(
        left, right, rel_tol=NUMERIC_REL_TOLERANCE,
        abs_tol=NUMERIC_ABS_TOLERANCE,
    )


def _json_equivalent(left: object, right: object) -> bool:
    if isinstance(left, bool) or isinstance(right, bool):
        return type(left) is type(right) and left == right
    if isinstance(left, (int, float)) and isinstance(right, (int, float)):
        if type(left) is not type(right):
            return False
        return left == right if isinstance(left, int) else _numbers_close(left, right)
    if type(left) is not type(right):
        return False
    if isinstance(left, dict):
        return left.keys() == right.keys() and all(
            _json_equivalent(left[key], right[key]) for key in left
        )
    if isinstance(left, list):
        return len(left) == len(right) and all(
            _json_equivalent(a, b) for a, b in zip(left, right)
        )
    return left == right


def _csv_equivalent(left: Path, right: Path) -> bool:
    try:
        with left.open(newline="", encoding="utf-8") as handle:
            left_reader = csv.DictReader(handle)
            left_fields = left_reader.fieldnames
            left_rows = list(left_reader)
        with right.open(newline="", encoding="utf-8") as handle:
            right_reader = csv.DictReader(handle)
            right_fields = right_reader.fieldnames
            right_rows = list(right_reader)
    except OSError:
        return False
    if left_fields != right_fields or left_fields is None or len(left_rows) != len(right_rows):
        return False
    for left_row, right_row in zip(left_rows, right_rows):
        for field in left_fields:
            left_value = left_row[field]
            right_value = right_row[field]
            if field == "series" or not left_value or not right_value:
                if left_value != right_value:
                    return False
                continue
            try:
                if not _numbers_close(float(left_value), float(right_value)):
                    return False
            except (TypeError, ValueError):
                if left_value != right_value:
                    return False
    return True


def artifacts_equivalent(generated: Path, committed: Path) -> bool:
    if not committed.is_file():
        return False
    if generated.suffix == ".json":
        try:
            left = json.loads(generated.read_text(encoding="utf-8"))
            right = json.loads(committed.read_text(encoding="utf-8"))
        except (OSError, ValueError):
            return False
        return _json_equivalent(left, right)
    if generated.suffix == ".csv":
        return _csv_equivalent(generated, committed)
    return generated.read_bytes() == committed.read_bytes()


def _load(
    csv_path: Path, json_path: Path
) -> tuple[
    list[dict[str, float]],
    list[dict[str, float]],
    list[dict[str, float]],
    dict[str, float],
]:
    with csv_path.open(newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle)
        if reader.fieldnames != [
            "series",
            "x",
            "reference_j",
            "value",
            "comparison_value",
        ]:
            raise ValueError("unexpected CAM16 equation-audit CSV header")
        brightness: list[dict[str, float]] = []
        background: list[dict[str, float]] = []
        coupled: list[dict[str, float]] = []
        for row in reader:
            try:
                item = {
                    "x": float(row["x"]),
                    "value": float(row["value"]),
                    "reference_j": (
                        float(row["reference_j"])
                        if row["reference_j"]
                        else math.nan
                    ),
                    "comparison": (
                        float(row["comparison_value"])
                        if row["comparison_value"]
                        else math.nan
                    ),
                }
            except (TypeError, ValueError) as error:
                raise ValueError("invalid CAM16 equation-audit numeric value") from error
            if not math.isfinite(item["x"]) or not math.isfinite(item["value"]):
                raise ValueError("non-finite CAM16 equation-audit value")
            if row["series"] == "normalized_brightness":
                if math.isfinite(item["reference_j"]) or not math.isfinite(
                    item["comparison"]
                ):
                    raise ValueError("brightness row lacks the proposed comparison")
                brightness.append(item)
            elif row["series"] == "isolated_ncb_factor":
                if math.isfinite(item["reference_j"]) or math.isfinite(
                    item["comparison"]
                ):
                    raise ValueError("isolated-factor row carries an extra value")
                background.append(item)
            elif row["series"] == "cam16_chroma_expression":
                if not math.isfinite(item["reference_j"]) or math.isfinite(
                    item["comparison"]
                ):
                    raise ValueError("invalid coupled chroma-expression row")
                coupled.append(item)
            else:
                raise ValueError(f"unknown CAM16 equation-audit series {row['series']!r}")
    expected_brightness_grid = [float(value) for value in range(0, 101, 5)]
    if [row["x"] for row in brightness] != expected_brightness_grid:
        raise ValueError("brightness curve must span J=0 through J=100 in steps of 5")
    expected_background_grid = [20.0, 10.0, 5.0, 2.0, 1.0, 0.5, 0.2, 0.1]
    if [row["x"] for row in background] != expected_background_grid:
        raise ValueError("background curve must use the declared Yb grid")
    reference_js = {row["reference_j"] for row in coupled}
    if reference_js != {float(value) for value in range(10, 100, 10)}:
        raise ValueError("coupled chroma expression must span reference J=10 through 90")
    background_values = set(expected_background_grid)
    coupled_pairs = {(row["x"], row["reference_j"]) for row in coupled}
    if len(coupled) != 72 or len(coupled_pairs) != len(coupled):
        raise ValueError("coupled chroma expression has duplicate or extra points")
    for reference_j in reference_js:
        if {
            row["x"] for row in coupled if row["reference_j"] == reference_j
        } != background_values:
            raise ValueError("coupled chroma expression has incomplete backgrounds")

    document = json.loads(json_path.read_text(encoding="utf-8"))
    if type(document.get("schema_version")) is not int or document["schema_version"] != 2:
        raise ValueError("unexpected CAM16 equation-audit JSON schema")
    if document.get("scope") != "equation_level_not_perceptual_validation":
        raise ValueError("CAM16 equation-audit scope boundary is missing")
    if document.get("study") != "Hellwig_Fairchild_2022_CAM16_equation_audit":
        raise ValueError("CAM16 equation-audit study identity differs")
    if document.get("source_doi") != "10.1002/col.22792":
        raise ValueError("CAM16 equation-audit source DOI differs")
    if document.get("equation_23_correction_date") != "2022-04-22":
        raise ValueError("CAM16 equation-audit correction date differs")
    if type(document.get("equation_23_coefficient")) is not int or document["equation_23_coefficient"] != 43:
        raise ValueError("CAM16 equation-audit corrected coefficient differs")
    brightness_contract = document.get("brightness")
    if not isinstance(brightness_contract, dict) or brightness_contract.get("contract") != "normalized_Q_over_Q_white_under_fixed_viewing_conditions":
        raise ValueError("CAM16 equation-audit brightness contract differs")
    background_contract = document.get("background_dependence")
    if (
        not isinstance(background_contract, dict)
        or background_contract.get("contract") != "isolated_Ncb_to_the_0_9_contribution"
        or background_contract.get("not_full_cam16_chroma") is not True
        or background_contract.get("reference_Y_b") != 20
    ):
        raise ValueError("CAM16 equation-audit background contract differs")
    coupled_contract = document.get("complete_chroma_expression")
    if (
        not isinstance(coupled_contract, dict)
        or coupled_contract.get("contract")
        != "fixed_adapted_response_reference_J_sweep"
        or coupled_contract.get("not_full_cam16_forward") is not True
        or coupled_contract.get("relative_Y_white") != 100
        or coupled_contract.get("reference_Y_b") != 20
        or coupled_contract.get("reference_J_min") != 10
        or coupled_contract.get("reference_J_max") != 90
        or coupled_contract.get("reference_J_step") != 10
        or not isinstance(coupled_contract.get("points"), list)
        or len(coupled_contract["points"]) != len(coupled)
    ):
        raise ValueError("CAM16 equation-audit coupled-expression contract differs")

    def crosscheck_json_curve(
        json_points: object,
        csv_points: list[dict[str, float]],
        fields: tuple[tuple[str, str], ...],
    ) -> None:
        if not isinstance(json_points, list) or len(json_points) != len(csv_points):
            raise ValueError("CAM16 equation-audit JSON and CSV curves differ")
        expected_keys = {json_field for json_field, _ in fields}
        for json_point, csv_point in zip(json_points, csv_points, strict=True):
            if not isinstance(json_point, dict) or json_point.keys() != expected_keys:
                raise ValueError("CAM16 equation-audit JSON curve point differs")
            for json_field, csv_field in fields:
                json_value = json_point[json_field]
                csv_value = csv_point[csv_field]
                if (
                    isinstance(json_value, bool)
                    or not isinstance(json_value, (int, float))
                    or not math.isfinite(json_value)
                ):
                    raise ValueError("invalid CAM16 equation-audit JSON curve value")
                if not math.isclose(
                    float(json_value), csv_value, rel_tol=1e-11, abs_tol=1e-11
                ):
                    raise ValueError("CAM16 equation-audit JSON and CSV curves differ")

    crosscheck_json_curve(
        brightness_contract.get("points") if isinstance(brightness_contract, dict) else None,
        brightness,
        (
            ("J", "x"),
            ("cam16_Q_over_Q_white", "value"),
            ("hellwig_2022_Q_over_Q_white", "comparison"),
        ),
    )
    crosscheck_json_curve(
        background_contract.get("points") if isinstance(background_contract, dict) else None,
        background,
        (("Y_b", "x"), ("relative_factor", "value")),
    )
    crosscheck_json_curve(
        coupled_contract["points"],
        coupled,
        (
            ("Y_b", "x"),
            ("reference_J", "reference_j"),
            ("relative_chroma", "value"),
        ),
    )
    performance = document.get("published_performance_R_squared")
    required = {
        "brightness_CAM16",
        "brightness_proposed",
        "Munsell_chroma_CAM16",
        "Munsell_chroma_proposed",
        "LUTCHI_colorfulness_CAM16",
        "LUTCHI_colorfulness_proposed",
    }
    if not isinstance(performance, dict) or performance.keys() != required:
        raise ValueError("CAM16 equation-audit performance fields differ")
    typed_performance: dict[str, float] = {}
    for key, value in performance.items():
        if (
            isinstance(value, bool)
            or not isinstance(value, (int, float))
            or not math.isfinite(value)
        ):
            raise ValueError("invalid CAM16 equation-audit performance value")
        if not 0.0 <= float(value) <= 1.0:
            raise ValueError("CAM16 equation-audit R-squared outside [0,1]")
        typed_performance[key] = float(value)
    return brightness, background, coupled, typed_performance


def _polyline(points: list[tuple[float, float]], class_name: str) -> str:
    coords = " ".join(f"{x:.2f},{y:.2f}" for x, y in points)
    return f'<polyline class="{class_name}" points="{coords}"/>'


def render_svg(csv_path: Path, json_path: Path) -> str:
    brightness, background, coupled, performance = _load(csv_path, json_path)
    bx0, by0, bw, bh = 82.0, 126.0, 438.0, 330.0
    nx0, ny0, nw, nh = 650.0, 126.0, 438.0, 330.0
    brightness_cam16 = [
        (bx0 + bw * row["x"] / 100.0, by0 + bh * (1.0 - row["value"]))
        for row in brightness
    ]
    brightness_proposed = [
        (bx0 + bw * row["x"] / 100.0, by0 + bh * (1.0 - row["comparison"]))
        for row in brightness
    ]
    log_min, log_max = math.log10(0.1), math.log10(20.0)
    factor_max = 2.8
    background_points = [
        (
            nx0 + nw * (math.log10(row["x"]) - log_min) / (log_max - log_min),
            ny0 + nh * (1.0 - (row["value"] - 1.0) / (factor_max - 1.0)),
        )
        for row in sorted(background, key=lambda item: item["x"])
    ]
    coupled_by_background: dict[float, list[float]] = {}
    coupled_by_j: dict[float, list[dict[str, float]]] = {}
    for row in coupled:
        coupled_by_background.setdefault(row["x"], []).append(row["value"])
        coupled_by_j.setdefault(row["reference_j"], []).append(row)

    def coupled_coords(rows: list[dict[str, float]]) -> list[tuple[float, float]]:
        return [
            (
                nx0
                + nw
                * (math.log10(row["x"]) - log_min)
                / (log_max - log_min),
                ny0
                + nh
                * (1.0 - (row["value"] - 1.0) / (factor_max - 1.0)),
            )
            for row in sorted(rows, key=lambda item: item["x"])
        ]

    envelope_low: list[tuple[float, float]] = []
    envelope_high: list[tuple[float, float]] = []
    for x in sorted(coupled_by_background):
        values = coupled_by_background[x]
        x_coord = nx0 + nw * (math.log10(x) - log_min) / (log_max - log_min)
        envelope_low.append(
            (x_coord, ny0 + nh * (1.0 - (min(values) - 1.0) / (factor_max - 1.0)))
        )
        envelope_high.append(
            (x_coord, ny0 + nh * (1.0 - (max(values) - 1.0) / (factor_max - 1.0)))
        )
    envelope = " ".join(
        f"{x:.2f},{y:.2f}"
        for x, y in envelope_high + list(reversed(envelope_low))
    )
    j10_points = coupled_coords(coupled_by_j[10.0])
    j90_points = coupled_coords(coupled_by_j[90.0])
    perf = performance
    return "\n".join(
        [
            '<svg xmlns="http://www.w3.org/2000/svg" width="1200" height="760" viewBox="0 0 1200 760" role="img" aria-labelledby="audit-title audit-description">',
            '<title id="audit-title">CAM16 equation audit</title>',
            '<desc id="audit-description">Equation-level diagnostics from Hellwig and Fairchild 2022: normalized CAM16 brightness against the proposed linear relation, and the isolated Ncb factor compared with the CAM16 chroma expression while adapted responses are held fixed over the paper’s reference-lightness sweep. A published performance table retains both improvements and the colorfulness regression.</desc>',
            '<style>text{font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif;fill:#172033}.title{font-size:28px;font-weight:700}.subtitle{font-size:15px;fill:#506077}.panel-title{font-size:17px;font-weight:650}.axis{stroke:#9aa8bc;stroke-width:1}.grid{stroke:#dce3ec;stroke-width:1}.cam16{fill:none;stroke:#c74b50;stroke-width:3}.proposed{fill:none;stroke:#176f85;stroke-width:3}.ncb{fill:none;stroke:#7566b3;stroke-width:3}.j10{fill:none;stroke:#b06b15;stroke-width:2}.j90{fill:none;stroke:#1f8291;stroke-width:2}.envelope{fill:#dbeef1;stroke:none;opacity:.85}.legend{font-size:13px}.metric{font-size:14px}.metric-head{font-size:14px;font-weight:650}.warn{font-size:14px;font-weight:650;fill:#873b40}.foot{font-size:13px;fill:#59697f}</style>',
            '<rect width="1200" height="760" fill="#f7f9fc"/>',
            '<text x="60" y="50" class="title">CAM16 equation audit</text>',
            '<text x="60" y="78" class="subtitle">Published equations reproduced as bounded numerical diagnostics</text>',
            f'<rect x="{bx0}" y="{by0}" width="{bw}" height="{bh}" fill="#fff" stroke="#cbd5e1"/>',
            f'<rect x="{nx0}" y="{ny0}" width="{nw}" height="{nh}" fill="#fff" stroke="#cbd5e1"/>',
            '<text x="82" y="112" class="panel-title">Normalized lightness and brightness</text>',
            '<text x="650" y="112" class="panel-title">Fixed-response CAM16 chroma (J₀ at Yb=20)</text>',
            f'<line x1="{bx0}" y1="{by0+bh}" x2="{bx0+bw}" y2="{by0+bh}" class="axis"/>',
            f'<line x1="{bx0}" y1="{by0}" x2="{bx0}" y2="{by0+bh}" class="axis"/>',
            f'<line x1="{nx0}" y1="{ny0+nh}" x2="{nx0+nw}" y2="{ny0+nh}" class="axis"/>',
            f'<line x1="{nx0}" y1="{ny0}" x2="{nx0}" y2="{ny0+nh}" class="axis"/>',
            '<text x="76" y="477" class="legend">0</text><text x="183" y="477" class="legend">25</text><text x="293" y="477" class="legend">50</text><text x="402" y="477" class="legend">75</text><text x="506" y="477" class="legend">100</text>',
            '<text x="62" y="461" class="legend">0</text><text x="53" y="296" class="legend">0.5</text><text x="62" y="131" class="legend">1</text>',
            '<text x="638" y="477" class="legend">0.1</text><text x="835" y="477" class="legend">1</text><text x="968" y="477" class="legend">5</text><text x="1077" y="477" class="legend">20</text>',
            '<text x="625" y="461" class="legend">1</text><text x="625" y="277" class="legend">2</text><text x="616" y="167" class="legend">2.6</text>',
            _polyline(brightness_cam16, "cam16"),
            _polyline(brightness_proposed, "proposed"),
            f'<polygon class="envelope" points="{envelope}"/>',
            _polyline(j10_points, "j10"),
            _polyline(j90_points, "j90"),
            _polyline(background_points, "ncb"),
            '<line x1="350" y1="94" x2="378" y2="94" class="cam16"/><text x="386" y="99" class="legend">CAM16 sqrt(J/100)</text>',
            '<line x1="350" y1="116" x2="378" y2="116" class="proposed"/><text x="386" y="121" class="legend">proposed J/100</text>',
            '<text x="276" y="487" class="legend">J</text><text x="676" y="487" class="legend">relative background Yb (log scale)</text>',
            '<text x="95" y="146" class="legend">Q/Qw</text>',
            '<rect x="663" y="135" width="14" height="9" class="envelope"/><text x="682" y="145" class="legend">J₀=10–90</text>',
            '<line x1="772" y1="140" x2="794" y2="140" class="j10"/><text x="800" y="145" class="legend">J₀=10</text>',
            '<line x1="854" y1="140" x2="876" y2="140" class="j90"/><text x="882" y="145" class="legend">J₀=90</text>',
            '<line x1="936" y1="140" x2="958" y2="140" class="ncb"/><text x="964" y="145" class="legend">isolated Ncb^0.9</text>',
            '<text x="82" y="525" class="metric-head">Published comparison (R²)</text>',
            '<text x="82" y="554" class="metric">Brightness, LUTCHI</text>',
            f'<text x="300" y="554" class="metric">{perf["brightness_CAM16"]:.2f} → {perf["brightness_proposed"]:.2f}</text>',
            '<text x="82" y="581" class="metric">Chroma, Munsell</text>',
            f'<text x="300" y="581" class="metric">{perf["Munsell_chroma_CAM16"]:.2f} → {perf["Munsell_chroma_proposed"]:.2f}</text>',
            '<text x="82" y="608" class="metric">Colorfulness, LUTCHI</text>',
            f'<text x="300" y="608" class="warn">{perf["LUTCHI_colorfulness_CAM16"]:.2f} → {perf["LUTCHI_colorfulness_proposed"]:.2f}</text>',
            '<text x="650" y="525" class="metric-head">Interpretation boundary</text>',
            '<text x="650" y="554" class="metric">J=25 gives half normalized CAM16 brightness;</text>',
            '<text x="650" y="577" class="metric">J=50 gives half normalized lightness.</text>',
            '<text x="650" y="608" class="metric">The isolated factor is neither an upper nor lower bound;</text>',
            '<text x="650" y="631" class="metric">the relation depends on Yb and reference J₀.</text>',
            '<text x="60" y="684" class="foot">Equation behavior, not observer validation.</text>',
            '<text x="60" y="708" class="foot">Paper equations and literal regression tests validate transcription, not perceptual accuracy.</text>',
            '<text x="60" y="732" class="foot">Source: Hellwig &amp; Fairchild (2022), doi:10.1002/col.22792; corrected Equation 23 coefficient 43.</text>',
            '</svg>',
            '',
        ]
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--camera-iq", type=Path, required=True)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    data = root / "docs" / "data"
    figures = root / "docs" / "figures"
    expected_json = data / "cam16_equation_audit.json"
    expected_csv = data / "cam16_equation_audit.csv"
    expected_svg = figures / "cam16_equation_audit.svg"

    with tempfile.TemporaryDirectory() as temp:
        tmp = Path(temp)
        generated_json = tmp / expected_json.name
        generated_csv = tmp / expected_csv.name
        subprocess.run(
            [
                str(args.camera_iq),
                "cam16-equation-audit",
                "--out-json",
                str(generated_json),
                "--out-csv",
                str(generated_csv),
            ],
            check=True,
        )
        generated_svg = render_svg(generated_csv, generated_json)
        if args.check:
            stale: list[str] = []
            generated_svg_path = tmp / expected_svg.name
            generated_svg_path.write_text(generated_svg, encoding="utf-8")
            for generated, expected in (
                (generated_json, expected_json),
                (generated_csv, expected_csv),
                (generated_svg_path, expected_svg),
            ):
                if not artifacts_equivalent(generated, expected):
                    stale.append(expected.name)
            if stale:
                raise SystemExit("stale CAM16 equation-audit artifacts: " + ", ".join(stale))
            print("CAM16 equation-audit artifacts current: 2 data files, 1 figure")
            return 0
        data.mkdir(parents=True, exist_ok=True)
        figures.mkdir(parents=True, exist_ok=True)
        expected_json.write_bytes(generated_json.read_bytes())
        expected_csv.write_bytes(generated_csv.read_bytes())
        expected_svg.write_text(generated_svg, encoding="utf-8")
        print("generated CAM16 equation-audit artifacts: 2 data files, 1 figure")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
