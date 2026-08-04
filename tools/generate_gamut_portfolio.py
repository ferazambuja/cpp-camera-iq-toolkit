#!/usr/bin/env python3
"""Generate and verify the synthetic Display-P3 to sRGB gamut study."""

from __future__ import annotations

import argparse
import csv
import html
import io
import json
import math
import shutil
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DATA = ROOT / "docs" / "data"
FIGURES = ROOT / "docs" / "figures"
REPORT_FIELDS = [
    "id", "input_r", "input_g", "input_b", "input_in_destination",
    "modified", "branch", "output_r", "output_g", "output_b",
    "mapping_coordinate_space", "input_Lstar", "input_Cstar",
    "input_Lab_hue_degrees", "output_Lstar", "output_Cstar",
    "output_Lab_hue_degrees", "input_OkLab_L", "input_OkLab_a",
    "input_OkLab_b", "input_OkLCh_C", "input_OkLCh_h_degrees",
    "input_OkLCh_hue_defined", "output_OkLab_L", "output_OkLab_a",
    "output_OkLab_b", "output_OkLCh_C", "output_OkLCh_h_degrees",
    "output_OkLCh_hue_defined", "input_mapping_chroma",
    "output_mapping_chroma", "boundary_evidence_applicable",
    "source_connected_boundary_mapping_chroma",
    "destination_connected_boundary_mapping_chroma", "knee_mapping_chroma",
    "destination_boundary_utilization", "local_minde_applicable",
    "local_minde_iterations", "local_minde_final_delta_e_ok",
    "local_minde_returned_clipped_color", "delta_e_2000", "delta_e_ok",
    "delta_Lstar", "delta_Cstar", "delta_Lab_hue_degrees",
    "input_IPT_hue_degrees", "output_IPT_hue_degrees",
    "delta_IPT_hue_degrees", "IPT_hue_defined",
    "destination_margin_before", "destination_margin_after",
    "output_in_destination",
]
REPORT_HEADER = ",".join(REPORT_FIELDS)
BOOLEAN_FIELDS = {
    "input_in_destination", "modified", "input_OkLCh_hue_defined",
    "output_OkLCh_hue_defined", "boundary_evidence_applicable",
    "local_minde_applicable", "local_minde_returned_clipped_color",
    "IPT_hue_defined", "output_in_destination",
}
STRING_FIELDS = {"id", "branch", "mapping_coordinate_space"}
NUMERIC_FIELDS = {
    field for field in REPORT_FIELDS
    if field not in {*STRING_FIELDS, *BOOLEAN_FIELDS}
}
NUMERIC_ABS_TOLERANCE = 1e-12
NUMERIC_REL_TOLERANCE = 1e-12
ANGULAR_ABS_TOLERANCE_DEGREES = 1e-5
ANGULAR_FIELDS = {
    "input_Lab_hue_degrees", "output_Lab_hue_degrees",
    "input_OkLCh_h_degrees", "output_OkLCh_h_degrees",
    "delta_Lab_hue_degrees", "input_IPT_hue_degrees",
    "output_IPT_hue_degrees", "delta_IPT_hue_degrees",
    "h_degrees", "max_abs_delta_Lab_hue_degrees",
    "median_abs_delta_degrees", "p90_abs_delta_degrees",
    "max_abs_delta_degrees",
}
INTEGER_JSON_FIELDS = {
    "schema_version", "sample_count", "out_of_gamut_count", "modified_count",
    "iterations", "segments_examined", "refinement_iterations",
    "boundary_refinement_iterations",
}


def _numbers_close(left: float, right: float, field: str = "") -> bool:
    absolute_tolerance = (
        ANGULAR_ABS_TOLERANCE_DEGREES
        if field in ANGULAR_FIELDS else NUMERIC_ABS_TOLERANCE
    )
    return math.isfinite(left) and math.isfinite(right) and math.isclose(
        left,
        right,
        rel_tol=NUMERIC_REL_TOLERANCE,
        abs_tol=absolute_tolerance,
    )


def _json_equivalent(left: object, right: object, field: str = "") -> bool:
    if isinstance(left, bool) or isinstance(right, bool):
        return type(left) is type(right) and left == right
    if isinstance(left, (int, float)) and isinstance(right, (int, float)):
        if field in INTEGER_JSON_FIELDS:
            return type(left) is int and type(right) is int and left == right
        return _numbers_close(float(left), float(right), field)
    if type(left) is not type(right):
        return False
    if isinstance(left, dict):
        return left.keys() == right.keys() and all(
            _json_equivalent(left[key], right[key], key) for key in left
        )
    if isinstance(left, list):
        return len(left) == len(right) and all(
            _json_equivalent(a, b, field) for a, b in zip(left, right)
        )
    return left == right


def _json_difference(
    generated: object,
    committed: object,
    path: str = "$",
    field: str = "",
) -> str | None:
    if isinstance(generated, bool) or isinstance(committed, bool):
        if type(generated) is type(committed) and generated == committed:
            return None
        return f"{path}: generated {generated!r}, committed {committed!r}"
    if isinstance(generated, (int, float)) and isinstance(committed, (int, float)):
        if field in INTEGER_JSON_FIELDS and (
            type(generated) is not int or type(committed) is not int
        ):
            return (
                f"{path}: generated type {type(generated).__name__}, "
                f"committed type {type(committed).__name__}"
            )
        if field in INTEGER_JSON_FIELDS:
            if generated == committed:
                return None
            return f"{path}: generated {generated!r}, committed {committed!r}"
        elif _numbers_close(float(generated), float(committed), field):
            return None
        return (
            f"{path}: generated {generated:.17g}, committed {committed:.17g}, "
            f"absolute difference {abs(generated - committed):.17g}"
        )
    if type(generated) is not type(committed):
        return (
            f"{path}: generated type {type(generated).__name__}, "
            f"committed type {type(committed).__name__}"
        )
    if isinstance(generated, dict):
        if generated.keys() != committed.keys():
            missing = sorted(set(committed) - set(generated))
            extra = sorted(set(generated) - set(committed))
            return f"{path}: missing keys {missing}, extra keys {extra}"
        for key in generated:
            difference = _json_difference(
                generated[key], committed[key], f"{path}.{key}", key
            )
            if difference is not None:
                return difference
        return None
    if isinstance(generated, list):
        if len(generated) != len(committed):
            return f"{path}: generated length {len(generated)}, committed {len(committed)}"
        for index, (left, right) in enumerate(zip(generated, committed)):
            difference = _json_difference(
                left, right, f"{path}[{index}]", field
            )
            if difference is not None:
                return difference
        return None
    if generated != committed:
        return f"{path}: generated {generated!r}, committed {committed!r}"
    return None


def _csv_equivalent(left: Path, right: Path) -> bool:
    try:
        left_rows = read_report(left)
        right_rows = read_report(right)
    except (OSError, ValueError):
        return False
    if len(left_rows) != len(right_rows):
        return False
    for left_row, right_row in zip(left_rows, right_rows):
        for field in REPORT_FIELDS:
            if field in NUMERIC_FIELDS:
                if not _numbers_close(
                    float(left_row[field]), float(right_row[field]), field
                ):
                    return False
            elif left_row[field] != right_row[field]:
                return False
    return True


def artifacts_equivalent(generated: Path, committed: Path) -> bool:
    if not committed.is_file():
        return False
    if generated.name == "gamut_synthetic_input.csv" or generated.suffix == ".svg":
        return generated.read_bytes() == committed.read_bytes()
    if generated.suffix == ".csv":
        return _csv_equivalent(generated, committed)
    if generated.suffix == ".json":
        try:
            left = json.loads(generated.read_text(encoding="utf-8"))
            right = json.loads(committed.read_text(encoding="utf-8"))
        except (OSError, ValueError):
            return False
        return _json_equivalent(left, right)
    return generated.read_bytes() == committed.read_bytes()


def artifact_difference(generated: Path, committed: Path) -> str:
    if generated.suffix == ".csv":
        try:
            left_rows = read_report(generated)
            right_rows = read_report(committed)
        except (OSError, ValueError) as error:
            return str(error)
        if len(left_rows) != len(right_rows):
            return f"row count {len(left_rows)} != {len(right_rows)}"
        for left_row, right_row in zip(left_rows, right_rows):
            for field in REPORT_FIELDS:
                if field in NUMERIC_FIELDS:
                    left = float(left_row[field])
                    right = float(right_row[field])
                    if not _numbers_close(left, right, field):
                        return (
                            f"sample {left_row['id']!r}, field {field!r}: "
                            f"generated {left:.17g}, committed {right:.17g}, "
                            f"absolute difference {abs(left - right):.17g}"
                        )
                elif left_row[field] != right_row[field]:
                    return (
                        f"sample {left_row['id']!r}, field {field!r}: "
                        f"generated {left_row[field]!r}, committed {right_row[field]!r}"
                    )
    if generated.suffix == ".json":
        try:
            left = json.loads(generated.read_text(encoding="utf-8"))
            right = json.loads(committed.read_text(encoding="utf-8"))
        except (OSError, ValueError) as error:
            return str(error)
        return _json_difference(left, right) or "no semantic difference"
    return "bytes differ"


def synthetic_rows() -> list[tuple[str, float, float, float]]:
    levels = (0.0, 0.25, 0.5, 0.75, 1.0)
    rows: list[tuple[str, float, float, float]] = []
    for red in levels:
        for green in levels:
            for blue in levels:
                identifier = (
                    f"p3_r{round(red * 100):03d}_"
                    f"g{round(green * 100):03d}_b{round(blue * 100):03d}"
                )
                rows.append((identifier, red, green, blue))
    return rows


def write_input(path: Path) -> None:
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle, lineterminator="\n")
        writer.writerow(("id", "r", "g", "b"))
        for identifier, red, green, blue in synthetic_rows():
            writer.writerow((identifier, f"{red:.17g}", f"{green:.17g}",
                             f"{blue:.17g}"))


def sample_report(identifier: str, modified: bool = False) -> dict[str, object]:
    row: dict[str, object] = {field: 0.0 for field in NUMERIC_FIELDS}
    row.update({
        "id": identifier,
        "input_in_destination": not modified,
        "modified": modified,
        "branch": "fixed_Lh_radial_boundary_clip" if modified else "identity",
        "mapping_coordinate_space": "CIELAB_D65",
        "input_Lstar": 50.0,
        "input_Cstar": 20.0 if modified else 0.0,
        "input_Lab_hue_degrees": 30.0,
        "output_Lstar": 50.0,
        "output_Cstar": 15.0 if modified else 0.0,
        "output_Lab_hue_degrees": 30.0,
        "input_OkLCh_hue_defined": modified,
        "output_OkLCh_hue_defined": modified,
        "input_mapping_chroma": 20.0 if modified else 0.0,
        "output_mapping_chroma": 15.0 if modified else 0.0,
        "boundary_evidence_applicable": True,
        "source_connected_boundary_mapping_chroma": 30.0,
        "destination_connected_boundary_mapping_chroma": 15.0,
        "delta_e_2000": 2.0 if modified else 0.0,
        "delta_e_ok": 0.02 if modified else 0.0,
        "delta_Cstar": -5.0 if modified else 0.0,
        "IPT_hue_defined": modified,
        "destination_boundary_utilization": 1.0 if modified else 0.0,
        "local_minde_applicable": False,
        "local_minde_returned_clipped_color": False,
        "output_in_destination": True,
    })
    return row


def sample_report_row(identifier: str) -> str:
    output = io.StringIO()
    writer = csv.DictWriter(output, fieldnames=REPORT_FIELDS, lineterminator="")
    row = sample_report(identifier)
    for field in BOOLEAN_FIELDS:
        row[field] = "true" if bool(row[field]) else "false"
    writer.writerow(row)
    return output.getvalue()


def read_report(path: Path) -> list[dict[str, object]]:
    with path.open(newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle)
        if reader.fieldnames != REPORT_FIELDS:
            raise ValueError(f"unexpected gamut report header: {path}")
        rows: list[dict[str, object]] = []
        identifiers: set[str] = set()
        for line_number, source in enumerate(reader, 2):
            identifier = source["id"]
            if not identifier or identifier in identifiers:
                raise ValueError(f"empty or duplicate id at {path}:{line_number}")
            identifiers.add(identifier)
            row: dict[str, object] = dict(source)
            for field in NUMERIC_FIELDS:
                try:
                    value = float(source[field])
                except (TypeError, ValueError) as error:
                    raise ValueError(
                        f"invalid {field} at {path}:{line_number}"
                    ) from error
                if not math.isfinite(value):
                    raise ValueError(f"non-finite {field} at {path}:{line_number}")
                row[field] = value
            for field in BOOLEAN_FIELDS:
                if source[field] not in {"true", "false"}:
                    raise ValueError(f"invalid {field} at {path}:{line_number}")
                row[field] = source[field] == "true"
            rows.append(row)
    if not rows:
        raise ValueError(f"empty gamut report: {path}")
    return rows


def _hue_degrees(a: float, b: float) -> float:
    if math.hypot(a, b) <= 1e-12:
        return 0.0
    value = math.degrees(math.atan2(b, a))
    return value + 360.0 if value < 0.0 else value


def _require_sequence(value: object, size: int, context: str) -> list[object]:
    if not isinstance(value, list) or len(value) != size:
        raise ValueError(f"invalid gamut JSON {context}")
    return value


def _flatten_json_sample(sample: object) -> dict[str, object]:
    if not isinstance(sample, dict):
        raise ValueError("invalid gamut JSON sample")
    try:
        input_rgb = _require_sequence(sample["input_encoded_rgb"], 3, "input RGB")
        output_rgb = _require_sequence(sample["output_encoded_rgb"], 3, "output RGB")
        input_lab = _require_sequence(sample["input_Lab_D65"], 3, "input Lab")
        output_lab = _require_sequence(sample["output_Lab_D65"], 3, "output Lab")
        input_oklab = _require_sequence(sample["input_OkLab_D65"], 3, "input OkLab")
        output_oklab = _require_sequence(sample["output_OkLab_D65"], 3, "output OkLab")
        input_oklch = sample["input_OkLCh_D65"]
        output_oklch = sample["output_OkLCh_D65"]
        coordinates = sample["mapping_coordinates"]
        boundary = sample["boundary_evidence"]
        local_minde = sample["local_minde"]
        if not all(isinstance(value, dict) for value in (
            input_oklch, output_oklch, coordinates, boundary, local_minde
        )):
            raise ValueError("invalid gamut JSON nested sample object")
        row: dict[str, object] = {
            "id": sample["id"],
            "input_r": input_rgb[0], "input_g": input_rgb[1],
            "input_b": input_rgb[2],
            "input_in_destination": sample["input_in_destination"],
            "modified": sample["modified"], "branch": sample["branch"],
            "output_r": output_rgb[0], "output_g": output_rgb[1],
            "output_b": output_rgb[2],
            "mapping_coordinate_space": coordinates["space"],
            "input_Lstar": input_lab[0],
            "input_Cstar": math.hypot(float(input_lab[1]), float(input_lab[2])),
            "input_Lab_hue_degrees": _hue_degrees(
                float(input_lab[1]), float(input_lab[2])
            ),
            "output_Lstar": output_lab[0],
            "output_Cstar": math.hypot(float(output_lab[1]), float(output_lab[2])),
            "output_Lab_hue_degrees": _hue_degrees(
                float(output_lab[1]), float(output_lab[2])
            ),
            "input_OkLab_L": input_oklab[0], "input_OkLab_a": input_oklab[1],
            "input_OkLab_b": input_oklab[2], "input_OkLCh_C": input_oklch["C"],
            "input_OkLCh_h_degrees": (
                input_oklch["h_degrees"] if input_oklch["hue_defined"] else 0.0
            ),
            "input_OkLCh_hue_defined": input_oklch["hue_defined"],
            "output_OkLab_L": output_oklab[0], "output_OkLab_a": output_oklab[1],
            "output_OkLab_b": output_oklab[2], "output_OkLCh_C": output_oklch["C"],
            "output_OkLCh_h_degrees": (
                output_oklch["h_degrees"] if output_oklch["hue_defined"] else 0.0
            ),
            "output_OkLCh_hue_defined": output_oklch["hue_defined"],
            "input_mapping_chroma": coordinates["input_chroma"],
            "output_mapping_chroma": coordinates["output_chroma"],
            "boundary_evidence_applicable": boundary["applicable"],
            "source_connected_boundary_mapping_chroma": (
                boundary.get("source_connected_boundary_mapping_chroma", 0.0)
            ),
            "destination_connected_boundary_mapping_chroma": (
                boundary.get("destination_connected_boundary_mapping_chroma", 0.0)
            ),
            "knee_mapping_chroma": boundary.get("knee_mapping_chroma", 0.0),
            "destination_boundary_utilization": (
                boundary.get("destination_boundary_utilization", 0.0)
            ),
            "local_minde_applicable": local_minde["applicable"],
            "local_minde_iterations": local_minde.get("iterations", 0),
            "local_minde_final_delta_e_ok": (
                local_minde.get("final_delta_e_ok", 0.0)
            ),
            "local_minde_returned_clipped_color": (
                local_minde.get("returned_clipped_color", False)
            ),
            "delta_e_2000": sample["delta_e_2000"],
            "delta_e_ok": sample["delta_e_ok"],
            "delta_Lstar": sample["delta_Lstar"],
            "delta_Cstar": sample["delta_Cstar"],
            "delta_Lab_hue_degrees": sample["delta_Lab_hue_degrees"],
            "input_IPT_hue_degrees": sample["input_IPT_hue_degrees"],
            "output_IPT_hue_degrees": sample["output_IPT_hue_degrees"],
            "delta_IPT_hue_degrees": sample["delta_IPT_hue_degrees"],
            "IPT_hue_defined": sample["IPT_hue_defined"],
            "destination_margin_before": sample["destination_margin_before"],
            "destination_margin_after": sample["destination_margin_after"],
            "output_in_destination": sample["output_in_destination"],
        }
    except (KeyError, TypeError, ValueError) as error:
        if isinstance(error, ValueError) and str(error).startswith("invalid gamut"):
            raise
        raise ValueError("invalid gamut JSON sample") from error
    if row.keys() != set(REPORT_FIELDS):
        raise ValueError("gamut JSON sample mapping is incomplete")
    return row


def _percentile(values: list[float], quantile: float) -> float:
    if not values:
        return 0.0
    ordered = sorted(values)
    position = quantile * (len(ordered) - 1)
    lower = math.floor(position)
    upper = math.ceil(position)
    return ordered[lower] + (position - lower) * (ordered[upper] - ordered[lower])


def _assert_value_close(actual: object, expected: object, context: str) -> None:
    if isinstance(expected, bool) or isinstance(actual, bool):
        if type(actual) is not type(expected) or actual != expected:
            raise ValueError(context)
        return
    if isinstance(expected, (int, float)) and isinstance(actual, (int, float)):
        if not _numbers_close(float(actual), float(expected)):
            raise ValueError(context)
        return
    if actual != expected:
        raise ValueError(context)


def validate_report_pair(json_path: Path, csv_path: Path) -> None:
    try:
        document = json.loads(json_path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise ValueError("invalid gamut JSON report") from error
    if not isinstance(document, dict) or type(document.get("schema_version")) is not int:
        raise ValueError("invalid gamut JSON report schema")
    json_samples = document.get("samples")
    aggregate = document.get("aggregate")
    if not isinstance(json_samples, list) or not isinstance(aggregate, dict):
        raise ValueError("invalid gamut JSON report structure")
    csv_rows = read_report(csv_path)
    flat_json = [_flatten_json_sample(sample) for sample in json_samples]
    json_ids = [row["id"] for row in flat_json]
    csv_ids = [row["id"] for row in csv_rows]
    if json_ids != csv_ids:
        raise ValueError("gamut JSON and CSV sample IDs differ")
    for identifier, json_row, csv_row in zip(json_ids, flat_json, csv_rows, strict=True):
        for field in REPORT_FIELDS:
            try:
                _assert_value_close(
                    json_row[field], csv_row[field],
                    f"gamut JSON and CSV sample {identifier!r} field {field!r} differs",
                )
            except KeyError as error:
                raise ValueError("gamut JSON and CSV sample structure differs") from error

    modified_ipt = [
        abs(float(row["delta_IPT_hue_degrees"])) for row in csv_rows
        if bool(row["modified"]) and bool(row["IPT_hue_defined"])
    ]
    expected_aggregate: dict[str, object] = {
        "sample_count": len(csv_rows),
        "out_of_gamut_count": sum(
            not bool(row["input_in_destination"]) for row in csv_rows
        ),
        "modified_count": sum(bool(row["modified"]) for row in csv_rows),
        "mean_delta_e_2000": sum(float(row["delta_e_2000"]) for row in csv_rows) / len(csv_rows),
        "max_delta_e_2000": max(float(row["delta_e_2000"]) for row in csv_rows),
        "mean_delta_e_ok": sum(float(row["delta_e_ok"]) for row in csv_rows) / len(csv_rows),
        "max_delta_e_ok": max(float(row["delta_e_ok"]) for row in csv_rows),
        "max_abs_delta_Lstar": max(abs(float(row["delta_Lstar"])) for row in csv_rows),
        "max_abs_delta_Lab_hue_degrees": max(
            abs(float(row["delta_Lab_hue_degrees"])) for row in csv_rows
        ),
    }
    for field, expected in expected_aggregate.items():
        _assert_value_close(
            aggregate.get(field), expected,
            f"gamut JSON aggregate field {field!r} differs from samples",
        )
    ipt = aggregate.get("IPT_hue_diagnostic")
    if not isinstance(ipt, dict):
        raise ValueError("gamut JSON aggregate IPT diagnostic is missing")
    expected_ipt: dict[str, object] = {
        "modified_chromatic_sample_count": len(modified_ipt),
        "median_abs_delta_degrees": _percentile(modified_ipt, 0.5),
        "p90_abs_delta_degrees": _percentile(modified_ipt, 0.9),
        "max_abs_delta_degrees": max(modified_ipt, default=0.0),
        "count_abs_delta_above_3_degrees": sum(value > 3.0 for value in modified_ipt),
    }
    for field, expected in expected_ipt.items():
        _assert_value_close(
            ipt.get(field), expected,
            f"gamut JSON aggregate IPT field {field!r} differs from samples",
        )


def _rgb(row: dict[str, object]) -> str:
    values = [max(0, min(255, round(float(row[f"output_{c}"]) * 255)))
              for c in "rgb"]
    return f"rgb({values[0]},{values[1]},{values[2]})"


def _lab_point(row: dict[str, object], output: bool) -> tuple[float, float]:
    chroma = float(row["output_Cstar" if output else "input_Cstar"])
    hue = math.radians(float(row["input_Lab_hue_degrees"]))
    return chroma * math.cos(hue), chroma * math.sin(hue)


def _histogram(values: list[float], bins: int, maximum: float) -> list[int]:
    counts = [0] * bins
    if maximum <= 0:
        return counts
    for value in values:
        index = min(bins - 1, int(value / maximum * bins))
        counts[index] += 1
    return counts


def render_figure(radial: list[dict[str, object]],
                  soft: list[dict[str, object]],
                  oklch_radial: list[dict[str, object]],
                  css_local_minde: list[dict[str, object]]) -> str:
    reports = (radial, soft, oklch_radial, css_local_minde)
    identifiers = [{row["id"] for row in rows} for rows in reports]
    if any(current != identifiers[0] for current in identifiers[1:]):
        raise ValueError("gamut reports contain different sample IDs")
    width, height = 1200, 720
    pieces = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" '
        f'height="{height}" viewBox="0 0 {width} {height}" role="img" '
        'aria-labelledby="gamut-title gamut-description">',
        '<title id="gamut-title">Display-P3 to sRGB controlled gamut-mapping comparison</title>',
        '<desc id="gamut-description">A synthetic 125-color study separating '
        'the effect of mapping coordinates from the effect of the mapping '
        'algorithm, with common outcome diagnostics.</desc>',
        '<rect width="1200" height="720" fill="#f7f8fa"/>',
        '<style>text{font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif;fill:#17202a}'
        '.title{font-size:24px;font-weight:700}.subtitle{font-size:14px;fill:#52606d}'
        '.panel{font-size:17px;font-weight:650}.axis{stroke:#a9b3bd;stroke-width:1}'
        '.label{font-size:12px;fill:#52606d}.summary{font-size:14px;font-weight:600}'
        '.series-radial{fill:#2368a2;stroke:#123d5a;stroke-width:1}'
        '.series-soft{fill:#d97706;stroke:#7c3f00;stroke-width:1}</style>',
        '<text x="40" y="38" class="title">Display-P3 → sRGB gamut mapping</text>',
        '<text x="40" y="62" class="subtitle">125-point controlled study: coordinates and algorithms change one at a time</text>',
        '<text x="40" y="98" class="panel">fixed-L*, Lab-hue radial clipping</text>',
    ]

    # Panel A: fixed-L*, Lab-hue displacement vectors.
    plot_x, plot_y, plot_w, plot_h = 40, 120, 540, 520
    all_points = [_lab_point(row, output) for row in radial for output in (False, True)]
    extent = max(1.0, max(max(abs(a), abs(b)) for a, b in all_points)) * 1.08
    def point(a: float, b: float) -> tuple[float, float]:
        return (plot_x + plot_w / 2 + a / extent * plot_w / 2,
                plot_y + plot_h / 2 - b / extent * plot_h / 2)
    zero_x, zero_y = point(0, 0)
    pieces.extend([
        f'<rect x="{plot_x}" y="{plot_y}" width="{plot_w}" height="{plot_h}" rx="8" fill="white" stroke="#d8dee6"/>',
        f'<line x1="{plot_x}" y1="{zero_y:.2f}" x2="{plot_x + plot_w}" y2="{zero_y:.2f}" class="axis"/>',
        f'<line x1="{zero_x:.2f}" y1="{plot_y}" x2="{zero_x:.2f}" y2="{plot_y + plot_h}" class="axis"/>',
        f'<text x="{plot_x + plot_w - 15}" y="{zero_y - 7:.2f}" class="label">+a*</text>',
        f'<text x="{zero_x + 7:.2f}" y="{plot_y + 15}" class="label">+b*</text>',
    ])
    for row in radial:
        if not bool(row["modified"]):
            continue
        start = point(*_lab_point(row, False))
        end = point(*_lab_point(row, True))
        color = _rgb(row)
        pieces.append(
            f'<line x1="{start[0]:.2f}" y1="{start[1]:.2f}" '
            f'x2="{end[0]:.2f}" y2="{end[1]:.2f}" '
            f'stroke="{color}" stroke-width="1.35" opacity="0.55"/>'
        )
        pieces.append(
            f'<circle cx="{end[0]:.2f}" cy="{end[1]:.2f}" r="2.2" '
            f'fill="{color}" stroke="#273746" stroke-width="0.35"/>'
        )

    # Panel B: displacement distributions.
    panel_x, panel_w = 620, 540
    pieces.extend([
        f'<text x="{panel_x}" y="98" class="panel">CIELAB algorithm baseline</text>',
        f'<text x="{panel_x}" y="116" class="subtitle">radial clip vs experimental protected-core compression</text>',
        f'<rect x="{panel_x}" y="130" width="{panel_w}" height="240" rx="8" fill="white" stroke="#d8dee6"/>',
        f'<text x="{panel_x + 16}" y="156" class="summary">CIEDE2000 displacement of modified samples</text>',
    ])
    radial_de = [float(row["delta_e_2000"]) for row in radial if bool(row["modified"])]
    soft_de = [float(row["delta_e_2000"]) for row in soft if bool(row["modified"])]
    maximum_de = max([1.0, *radial_de, *soft_de])
    radial_hist = _histogram(radial_de, 12, maximum_de)
    soft_hist = _histogram(soft_de, 12, maximum_de)
    hist_max = max(1, *radial_hist, *soft_hist)
    base_y = 340
    for index, (r_count, s_count) in enumerate(zip(radial_hist, soft_hist)):
        x = panel_x + 24 + index * 41
        r_height = r_count / hist_max * 155
        s_height = s_count / hist_max * 155
        pieces.append(f'<rect x="{x}" y="{base_y-r_height:.2f}" width="15" height="{r_height:.2f}" class="series-radial" opacity="0.8"/>')
        pieces.append(f'<rect x="{x+16}" y="{base_y-s_height:.2f}" width="15" height="{s_height:.2f}" class="series-soft" stroke-dasharray="3 2" opacity="0.75"/>')
    pieces.extend([
        f'<text x="{panel_x + 24}" y="362" class="label">0</text>',
        f'<text x="{panel_x + panel_w - 75}" y="362" class="label">ΔE00 {maximum_de:.1f}</text>',
        f'<circle cx="{panel_x + 296}" cy="142" r="6" class="series-radial"/><text x="{panel_x + 307}" y="147" class="label">radial</text>',
        f'<rect x="{panel_x + 365}" y="136" width="12" height="12" class="series-soft" stroke-dasharray="3 2"/><text x="{panel_x + 382}" y="147" class="label">soft</text>',
    ])

    # Panel C: factorial comparison using the same external diagnostics.
    pieces.extend([
        f'<rect x="{panel_x}" y="390" width="{panel_w}" height="250" rx="8" fill="white" stroke="#d8dee6"/>',
        f'<text x="{panel_x + 16}" y="418" class="summary">Coordinate-space and algorithm comparison</text>',
        f'<text x="{panel_x + 16}" y="440" class="label">method</text>',
        f'<text x="{panel_x + 282}" y="440" class="label">modified</text>',
        f'<text x="{panel_x + 360}" y="440" class="label">mean ΔE00</text>',
        f'<text x="{panel_x + 458}" y="440" class="label">IPT p90</text>',
    ])
    method_rows = (
        ("CIELAB radial", radial, "#2368a2"),
        ("OkLCh radial", oklch_radial, "#16836b"),
        ("CSS Local MINDE", css_local_minde, "#7c3aed"),
        ("CIELAB soft", soft, "#d97706"),
    )
    for index, (name, rows, color) in enumerate(method_rows):
        y = 474 + index * 38
        modified = sum(bool(row["modified"]) for row in rows)
        mean_de = sum(float(row["delta_e_2000"]) for row in rows) / len(rows)
        ipt = sorted(
            abs(float(row["delta_IPT_hue_degrees"]))
            for row in rows
            if bool(row["modified"]) and bool(row["IPT_hue_defined"])
        )
        position = 0.9 * (len(ipt) - 1) if ipt else 0.0
        lower = math.floor(position)
        upper = math.ceil(position)
        p90 = (ipt[lower] + (position - lower) * (ipt[upper] - ipt[lower])
               if ipt else 0.0)
        pieces.extend([
            f'<circle cx="{panel_x + 24}" cy="{y - 4}" r="6" fill="{color}"/>',
            f'<text x="{panel_x + 38}" y="{y}" class="label">{html.escape(name)}</text>',
            f'<text x="{panel_x + 294}" y="{y}" class="label">{modified}/125</text>',
            f'<text x="{panel_x + 378}" y="{y}" class="label">{mean_de:.3f}</text>',
            f'<text x="{panel_x + 470}" y="{y}" class="label">{p90:.2f}°</text>',
        ])
    pieces.extend([
        f'<text x="{panel_x + 16}" y="628" class="label">Lab radial → OkLCh radial isolates coordinates; OkLCh radial → Local MINDE isolates algorithm.</text>',
        f'<text x="40" y="682" class="subtitle">Common metrics: CIEDE2000 displacement and IPT hue-coordinate difference.</text>',
        f'<text x="40" y="704" class="subtitle">Coordinate diagnostics are not observer validation; ideal encoding gamuts are not measured-device gamuts.</text>',
        '</svg>\n',
    ])
    return "".join(pieces)


def generate(camera_iq: Path, destination: Path) -> dict[str, Path]:
    destination.mkdir(parents=True, exist_ok=True)
    input_path = destination / "gamut_synthetic_input.csv"
    radial_json = destination / "gamut_synthetic_radial.json"
    radial_csv = destination / "gamut_synthetic_radial.csv"
    soft_json = destination / "gamut_synthetic_soft.json"
    soft_csv = destination / "gamut_synthetic_soft.csv"
    oklch_radial_json = destination / "gamut_synthetic_oklch_radial.json"
    oklch_radial_csv = destination / "gamut_synthetic_oklch_radial.csv"
    css_local_minde_json = destination / "gamut_synthetic_css_local_minde.json"
    css_local_minde_csv = destination / "gamut_synthetic_css_local_minde.csv"
    figure = destination / "gamut_mapping_synthetic.svg"
    write_input(input_path)
    subprocess.run(
        [str(camera_iq), "gamut-map", str(input_path), "--out-json",
         str(radial_json), "--out-csv", str(radial_csv)],
        check=True,
    )
    subprocess.run(
        [str(camera_iq), "gamut-map", str(input_path), "--intent", "soft-knee",
         "--knee", "0.75", "--out-json", str(soft_json), "--out-csv",
         str(soft_csv)],
        check=True,
    )
    subprocess.run(
        [str(camera_iq), "gamut-map", str(input_path), "--intent",
         "oklch-radial", "--out-json", str(oklch_radial_json), "--out-csv",
         str(oklch_radial_csv)],
        check=True,
    )
    subprocess.run(
        [str(camera_iq), "gamut-map", str(input_path), "--intent",
         "css-local-minde", "--out-json", str(css_local_minde_json),
         "--out-csv", str(css_local_minde_csv)],
        check=True,
    )
    for json_path, csv_path in (
        (radial_json, radial_csv), (soft_json, soft_csv),
        (oklch_radial_json, oklch_radial_csv),
        (css_local_minde_json, css_local_minde_csv),
    ):
        validate_report_pair(json_path, csv_path)
    figure.write_text(
        render_figure(read_report(radial_csv), read_report(soft_csv),
                      read_report(oklch_radial_csv),
                      read_report(css_local_minde_csv)),
        encoding="utf-8",
    )
    return {path.name: path for path in (
        input_path, radial_json, radial_csv, soft_json, soft_csv,
        oklch_radial_json, oklch_radial_csv, css_local_minde_json,
        css_local_minde_csv, figure
    )}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--camera-iq", type=Path, default=ROOT / "build" / "camera_iq")
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    if not args.camera_iq.is_file():
        parser.error(f"camera_iq executable not found: {args.camera_iq}")

    with tempfile.TemporaryDirectory() as temp:
        generated = generate(args.camera_iq.resolve(), Path(temp))
        targets = {
            name: (FIGURES / name if name.endswith(".svg") else DATA / name)
            for name in generated
        }
        if args.check:
            stale = [name for name, source in generated.items()
                     if not artifacts_equivalent(source, targets[name])]
            if stale:
                for name in stale:
                    print(f"{name}: {artifact_difference(generated[name], targets[name])}")
                raise SystemExit("stale gamut portfolio artifacts: " + ", ".join(stale))
            print("gamut portfolio artifacts current: 9 data files, 1 figure")
            return 0
        DATA.mkdir(parents=True, exist_ok=True)
        FIGURES.mkdir(parents=True, exist_ok=True)
        for name, source in generated.items():
            shutil.copyfile(source, targets[name])
        print("generated gamut portfolio artifacts: 9 data files, 1 figure")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
