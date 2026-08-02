#!/usr/bin/env python3
"""Export shading aggregate tables from camera_iq JSON outputs."""

from __future__ import annotations

import argparse
import csv
import json
import math
import re
from pathlib import Path
from typing import Any, Iterable


NAME_RE = re.compile(
    r"Sphere_f(?P<aperture>[0-9.]+)_(?P<shutter>1:[0-9]+)_DSCF[0-9]+\.RAF$"
)

SCHEMA_VERSION = 3
POLICY_ID = "shading-v2-grid16x12-screening-coverage"
PEDESTAL_TOLERANCE_DN = 1.0
EXPECTED_OPTIONS: dict[str, float | int] = {
    "grid_cols": 16,
    "grid_rows": 12,
    "gate_center_frac": 0.20,
    "corner_block_px": 400,
    "corner_inset_px": 120,
    "near_ceiling_level": 0.98,
    "near_ceiling_max": 0.01,
    "min_finite_coverage": 0.90,
    "min_center_signal": 0.05,
    "max_negative_frac": 0.01,
    "min_bin_coverage": 0.90,
    "asymmetry_policy": 0.05,
}


def load(path: Path) -> dict[str, Any]:
    with path.open(encoding="utf-8") as handle:
        value = json.load(handle)
    if not isinstance(value, dict):
        raise ValueError(f"{path}: expected a JSON object")
    return value


def measurements(document: dict[str, Any]) -> Iterable[dict[str, Any]]:
    if "primary" in document and "repeat" in document:
        yield document["primary"]
        yield document["repeat"]
    else:
        yield document


def finite(value: Any, label: str) -> float:
    try:
        number = float(value)
    except (TypeError, ValueError) as error:
        raise ValueError(f"{label}: expected a finite number") from error
    if not math.isfinite(number):
        raise ValueError(f"{label}: expected a finite number")
    return number


def validate_options(item: dict[str, Any], label: str) -> None:
    if item.get("schema_version") != SCHEMA_VERSION:
        raise ValueError(f"{label}: unsupported shading schema")
    options = item.get("analysis_options")
    if not isinstance(options, dict) or set(options) != set(EXPECTED_OPTIONS):
        raise ValueError(f"{label}: incomplete or unexpected analysis options")
    for key, expected in EXPECTED_OPTIONS.items():
        actual = finite(options[key], f"{label} {key}")
        if actual != float(expected):
            raise ValueError(f"{label}: {key}={actual} does not match the published method")


def fraction4(value: Any, label: str) -> list[float]:
    values = four_values(value, label)
    if any(number < 0.0 or number > 1.0 for number in values):
        raise ValueError(f"{label}: fractions must lie in [0, 1]")
    return values


def validated_gates(
    item: dict[str, Any], label: str
) -> tuple[
    list[float], list[float], list[float], list[float], list[float], list[str]
]:
    gates = item.get("gates")
    if not isinstance(gates, dict):
        raise ValueError(f"{label}: missing gate evidence")
    if gates.get("measured") is not True:
        raise ValueError(f"{label}: gate evidence was not measured")
    gate = fraction4(
        gates.get("near_ceiling_fraction_gate"), f"{label} near-ceiling gate"
    )
    frame = fraction4(
        gates.get("near_ceiling_fraction_frame"), f"{label} near-ceiling frame"
    )
    finite_gate = fraction4(
        gates.get("finite_fraction_gate"), f"{label} finite gate coverage"
    )
    finite_frame = fraction4(
        gates.get("finite_fraction_frame"), f"{label} finite frame coverage"
    )
    negative = fraction4(gates.get("negative_fraction"), f"{label} negative")
    center = fraction4(
        gates.get("center_signal_fraction"), f"{label} center signal"
    )
    coverage = finite(gates.get("min_bin_coverage"), f"{label} bin coverage")
    if coverage < 0.0 or coverage > 1.0:
        raise ValueError(f"{label}: bin coverage must lie in [0, 1]")
    expected = {
        "near_ceiling_ok": all(
            gate_value <= float(EXPECTED_OPTIONS["near_ceiling_max"])
            and frame_value <= float(EXPECTED_OPTIONS["near_ceiling_max"])
            for gate_value, frame_value in zip(gate, frame)
        ),
        "screening_coverage_ok": all(
            gate_value >= float(EXPECTED_OPTIONS["min_finite_coverage"])
            and frame_value >= float(EXPECTED_OPTIONS["min_finite_coverage"])
            for gate_value, frame_value in zip(finite_gate, finite_frame)
        ),
        "low_signal_ok": all(
            value >= float(EXPECTED_OPTIONS["min_center_signal"]) for value in center
        ),
        "negative_ok": all(
            value <= float(EXPECTED_OPTIONS["max_negative_frac"])
            for value in negative
        ),
        "coverage_ok": coverage >= float(EXPECTED_OPTIONS["min_bin_coverage"]),
    }
    for key, verdict in expected.items():
        if gates.get(key) is not verdict:
            raise ValueError(f"{label}: {key} disagrees with measured fractions")
    if not isinstance(gates.get("finite_ok"), bool):
        raise ValueError(f"{label}: finite_ok verdict is not boolean")
    failed = [
        name
        for name, key in (
            ("near_ceiling", "near_ceiling_ok"),
            ("screening_coverage", "screening_coverage_ok"),
            ("low_signal", "low_signal_ok"),
            ("negative", "negative_ok"),
            ("coverage", "coverage_ok"),
            ("finite", "finite_ok"),
        )
        if not bool(gates[key])
    ]
    return gate, frame, finite_gate, finite_frame, center, failed


def cfa_positions(item: dict[str, Any], label: str) -> dict[str, int]:
    positions = item.get("cfa_positions")
    if not isinstance(positions, dict) or set(positions) != {"r", "g1", "g2", "b"}:
        raise ValueError(f"{label}: missing CFA-position provenance")
    mapped = {key: int(value) for key, value in positions.items()}
    if set(mapped.values()) != {0, 1, 2, 3}:
        raise ValueError(f"{label}: CFA positions are not a four-plane mapping")
    return mapped


def validated_rect(value: Any, label: str) -> dict[str, int]:
    keys = {"x", "y", "width", "height"}
    if not isinstance(value, dict) or set(value) != keys:
        raise ValueError(f"{label}: expected x/y/width/height geometry")
    if any(isinstance(value[key], bool) or not isinstance(value[key], int) for key in keys):
        raise ValueError(f"{label}: geometry values must be integers")
    rect = {key: int(value[key]) for key in keys}
    if rect["x"] < 0 or rect["y"] < 0 or rect["width"] <= 0 or rect["height"] <= 0:
        raise ValueError(f"{label}: geometry must be positive and in bounds")
    if any(rect[key] % 2 != 0 for key in keys):
        raise ValueError(f"{label}: geometry is not CFA-balanced")
    return rect


def validated_measurement_evidence(item: dict[str, Any], label: str) -> None:
    ceiling = four_values(item.get("signal_ceiling_dn"), f"{label} signal ceiling")
    if any(value <= 0.0 for value in ceiling):
        raise ValueError(f"{label}: signal ceilings must be positive")

    geometry = item.get("geometry")
    if not isinstance(geometry, dict) or set(geometry) != {"gate", "center", "corners"}:
        raise ValueError(f"{label}: missing measured geometry")
    gate = validated_rect(geometry["gate"], f"{label} gate")
    center = validated_rect(geometry["center"], f"{label} center")
    if not (
        center["x"] >= gate["x"]
        and center["y"] >= gate["y"]
        and center["x"] + center["width"] <= gate["x"] + gate["width"]
        and center["y"] + center["height"] <= gate["y"] + gate["height"]
    ):
        raise ValueError(f"{label}: center geometry is outside the screening gate")
    corners_value = geometry["corners"]
    if not isinstance(corners_value, list) or len(corners_value) != 4:
        raise ValueError(f"{label}: expected four corner rectangles")
    for index, rect in enumerate(corners_value):
        validated_rect(rect, f"{label} corner {index}")

    center_median = four_values(
        item.get("center_block_median"), f"{label} center median"
    )
    if any(value <= 0.0 for value in center_median):
        raise ValueError(f"{label}: center medians must be positive")
    corners = item.get("corner_median")
    if not isinstance(corners, list) or len(corners) != 4:
        raise ValueError(f"{label}: expected four corner-median rows")
    for index, row in enumerate(corners):
        four_values(row, f"{label} corner {index} median")


def corner_relative(item: dict[str, Any], label: str) -> list[list[float]]:
    corners = item.get("corner_relative")
    if not isinstance(corners, list) or len(corners) != 4:
        raise ValueError(f"{label}: expected four corner-relative rows")
    validated: list[list[float]] = []
    for q, row in enumerate(corners):
        if not isinstance(row, list) or len(row) != 4:
            raise ValueError(f"{label}: corner {q} is not a four-plane row")
        validated.append(
            [finite(value, f"{label} corner {q} plane {p}") for p, value in enumerate(row)]
        )
    return validated


def four_values(value: Any, label: str) -> list[float]:
    if not isinstance(value, list) or len(value) != 4:
        raise ValueError(f"{label}: expected four values")
    return [finite(item, f"{label} plane {index}") for index, item in enumerate(value)]


def validated_green_asymmetry(
    item: dict[str, Any], label: str, positions: dict[str, int]
) -> float:
    corners = corner_relative(item, label)
    green = [
        0.5 * (row[positions["g1"]] + row[positions["g2"]]) for row in corners
    ]
    mean = sum(green) / 4.0
    if not math.isfinite(mean) or mean <= 0.0:
        raise ValueError(f"{label}: green corner mean is not positive and finite")
    computed = (max(green) - min(green)) / mean
    serialized = finite(item.get("green_asymmetry"), f"{label} green asymmetry")
    if not math.isclose(computed, serialized, rel_tol=1e-12, abs_tol=1e-12):
        raise ValueError(f"{label}: serialized green asymmetry disagrees with corners")
    verdict = item.get("asymmetry_exceeds_policy")
    if not isinstance(verdict, bool) or verdict != (
        computed > float(EXPECTED_OPTIONS["asymmetry_policy"])
    ):
        raise ValueError(f"{label}: asymmetry policy verdict disagrees with value")
    return computed


def validated_pedestal(item: dict[str, Any], label: str) -> bool:
    pedestal = item.get("pedestal")
    if not isinstance(pedestal, dict):
        raise ValueError(f"{label}: missing pedestal evidence")
    verified = pedestal.get("verified")
    if not isinstance(verified, bool):
        raise ValueError(f"{label}: pedestal verified verdict is not boolean")
    if pedestal.get("pedestal_unverified") is not (not verified):
        raise ValueError(f"{label}: pedestal warning disagrees with verified verdict")
    if not verified:
        return False
    required_true = (
        "measured",
        "compatible",
        "make_model_metadata_matches",
        "body_serials_consistent",
        "exposure_metadata_present",
        "exposure_metadata_matches",
        "full_finite_coverage",
        "spatial_checked",
        "within_tolerance",
    )
    missing = [key for key in required_true if pedestal.get(key) is not True]
    if missing:
        raise ValueError(f"{label}: verified pedestal lacks {', '.join(missing)}")
    serials_present = pedestal.get("body_serials_present")
    serials_match = pedestal.get("body_serials_match")
    if not isinstance(serials_present, bool) or not isinstance(serials_match, bool):
        raise ValueError(f"{label}: body-serial evidence is not boolean")
    if serials_match and not serials_present:
        raise ValueError(f"{label}: body serials cannot match when not both present")
    if serials_present and not serials_match:
        raise ValueError(f"{label}: present body serials conflict")

    residual = four_values(pedestal.get("residual_dn"), f"{label} residual")
    finite_fraction = four_values(
        pedestal.get("finite_fraction"), f"{label} finite fraction"
    )
    if any(value != 1.0 for value in finite_fraction):
        raise ValueError(f"{label}: full finite coverage disagrees with fractions")
    center = four_values(
        pedestal.get("center_residual_dn"), f"{label} center residual"
    )
    corners_value = pedestal.get("corner_residual_dn")
    if not isinstance(corners_value, list) or len(corners_value) != 4:
        raise ValueError(f"{label}: expected four corner residual rows")
    corners = [
        four_values(row, f"{label} corner {index} residual")
        for index, row in enumerate(corners_value)
    ]
    global_max = max(abs(value) for value in residual)
    spatial_max = max(
        [abs(value) for value in center]
        + [abs(value) for row in corners for value in row]
    )
    reported_global = finite(
        pedestal.get("max_abs_residual_dn"), f"{label} max residual"
    )
    reported_spatial = finite(
        pedestal.get("max_abs_spatial_residual_dn"),
        f"{label} max spatial residual",
    )
    if not math.isclose(global_max, reported_global, rel_tol=1e-12, abs_tol=1e-12):
        raise ValueError(f"{label}: max residual disagrees with residual array")
    if not math.isclose(spatial_max, reported_spatial, rel_tol=1e-12, abs_tol=1e-12):
        raise ValueError(f"{label}: max spatial residual disagrees with regions")
    within_tolerance = (
        global_max <= PEDESTAL_TOLERANCE_DN
        and spatial_max <= PEDESTAL_TOLERANCE_DN
    )
    if pedestal.get("within_tolerance") is not within_tolerance:
        raise ValueError(f"{label}: tolerance verdict disagrees with residuals")
    return True


def recompute_comparison(
    primary: dict[str, Any], repeat: dict[str, Any]
) -> tuple[float, float]:
    a = corner_relative(primary, "comparison primary")
    b = corner_relative(repeat, "comparison repeat")
    deltas = [
        100.0 * abs(a[q][p] - b[q][p]) for q in range(4) for p in range(4)
    ]
    return max(deltas), math.sqrt(sum(value * value for value in deltas) / len(deltas))


def validated_chromatic_maps(
    item: dict[str, Any], label: str, positions: dict[str, int]
) -> None:
    relative = item.get("relative_response")
    if not isinstance(relative, list) or len(relative) != 4:
        raise ValueError(f"{label}: expected four relative-response maps")
    grid = item.get("grid")
    if not isinstance(grid, dict):
        raise ValueError(f"{label}: missing grid")
    bins = int(grid.get("cols", 0)) * int(grid.get("rows", 0))
    maps = [
        [finite(value, f"{label} relative plane {plane}") for value in values]
        if isinstance(values, list) and len(values) == bins
        else []
        for plane, values in enumerate(relative)
    ]
    if any(len(values) != bins for values in maps):
        raise ValueError(f"{label}: relative-response map size disagrees with grid")
    chroma: dict[str, list[float]] = {}
    for key in ("c_rg", "c_bg", "c_g1g2"):
        values = item.get(key)
        if not isinstance(values, list) or len(values) != bins:
            raise ValueError(f"{label}: {key} map size disagrees with grid")
        chroma[key] = [finite(value, f"{label} {key}") for value in values]
    if item.get("chromatic_complete") is not True or item.get(
        "missing_chromatic_bin_count"
    ) != 0:
        raise ValueError(f"{label}: accepted chromatic maps must be complete")
    for index in range(bins):
        red = maps[positions["r"]][index]
        green1 = maps[positions["g1"]][index]
        green2 = maps[positions["g2"]][index]
        blue = maps[positions["b"]][index]
        green = 0.5 * (green1 + green2)
        if green <= 0.0 or green2 <= 0.0:
            raise ValueError(f"{label}: chromatic denominator is not positive")
        expected = {
            "c_rg": red / green,
            "c_bg": blue / green,
            "c_g1g2": green1 / green2,
        }
        for key, value in expected.items():
            if not math.isclose(
                chroma[key][index], value, rel_tol=1e-12, abs_tol=1e-12
            ):
                raise ValueError(f"{label}: {key} disagrees with relative maps")


def write_summary(
    inventory_dir: Path,
    detailed: dict[str, dict[str, Any]],
    comparison: dict[str, Any] | None,
    output: Path,
) -> dict[str, dict[str, Any]]:
    rows: list[list[str]] = []
    seen_files: set[str] = set()
    accepted_files: set[str] = set()
    accepted_evidence: dict[str, dict[str, Any]] = {}
    aperture_census: dict[str, int] = {}
    for path in sorted(inventory_dir.glob("*.json")):
        item = load(path)
        file_label = str(item["file"])
        if file_label in seen_files:
            raise ValueError(f"{path}: duplicate inventory file label {file_label}")
        seen_files.add(file_label)
        validate_options(item, str(path))
        match = NAME_RE.search(file_label)
        if not match:
            raise ValueError(f"{path}: filename does not encode aperture/shutter")
        gate, frame, finite_gate, finite_frame, center, failed = validated_gates(
            item, str(path)
        )
        positions = cfa_positions(item, str(path))
        if item["gates"]["finite_ok"] is True:
            validated_measurement_evidence(item, str(path))
        green_center = 0.5 * (
            finite(center[positions["g1"]], "G1 center signal")
            + finite(center[positions["g2"]], "G2 center signal")
        )
        accepted_value = item.get("accepted")
        if not isinstance(accepted_value, bool):
            raise ValueError(f"{path}: accepted verdict is not boolean")
        accepted = accepted_value
        if accepted == bool(failed):
            raise ValueError(f"{path}: acceptance and gate verdicts disagree")
        if accepted:
            accepted_files.add(file_label)
        if accepted and file_label not in detailed:
            raise ValueError(f"{file_label}: accepted result lacks detailed evidence")
        detail = detailed.get(file_label, item)
        if detail is not item:
            validate_options(detail, f"detailed {file_label}")
            for key in (
                "accepted",
                "gates",
                "grid",
                "analysis_options",
                "cfa_positions",
                "signal_ceiling_dn",
                "geometry",
                "center_block_median",
                "corner_median",
                "corner_relative",
                "relative_response",
                "c_rg",
                "c_bg",
                "c_g1g2",
                "green_asymmetry",
                "asymmetry_exceeds_policy",
            ):
                if detail.get(key) != item.get(key):
                    raise ValueError(f"{file_label}: detailed and inventory {key} disagree")
        asymmetry = None
        dark_controls_verified = False
        if accepted:
            detail_positions = cfa_positions(detail, f"detailed {file_label}")
            asymmetry = validated_green_asymmetry(
                detail, f"detailed {file_label}", detail_positions
            )
            validated_chromatic_maps(
                detail, f"detailed {file_label}", detail_positions
            )
            dark_controls_verified = validated_pedestal(
                detail, f"detailed {file_label}"
            )
            accepted_evidence[file_label] = detail
        if accepted and not dark_controls_verified:
            raise ValueError(f"{file_label}: accepted result lacks verified dark controls")
        comparison_file = ""
        max_delta = ""
        rms_delta = ""
        if comparison and file_label == comparison["primary_file"]:
            comparison_file = str(comparison["repeat_file"])
            max_delta = f"{finite(comparison['max_corner_delta_pp'], 'max delta'):.8f}"
            rms_delta = f"{finite(comparison['rms_corner_delta_pp'], 'RMS delta'):.8f}"
        rows.append(
            [
                file_label,
                match.group("aperture"),
                match.group("shutter"),
                str(accepted).lower(),
                ";".join(failed),
                f"{max(gate):.8f}",
                f"{max(frame):.8f}",
                *[f"{gate[positions[name]]:.8f}" for name in ("r", "g1", "g2", "b")],
                *[f"{frame[positions[name]]:.8f}" for name in ("r", "g1", "g2", "b")],
                *[f"{finite_gate[positions[name]]:.8f}" for name in ("r", "g1", "g2", "b")],
                *[f"{finite_frame[positions[name]]:.8f}" for name in ("r", "g1", "g2", "b")],
                f"{green_center:.8f}",
                "" if asymmetry is None else f"{finite(asymmetry, 'asymmetry'):.8f}",
                str(dark_controls_verified).lower(),
                comparison_file,
                max_delta,
                rms_delta,
                POLICY_ID,
            ]
        )
        aperture = match.group("aperture")
        aperture_census[aperture] = aperture_census.get(aperture, 0) + 1

    if len(rows) != 52 or len(seen_files) != 52 or len(accepted_files) != 3:
        raise ValueError("expected 52 sphere frames with exactly 3 accepted")
    if aperture_census != {"5.6": 18, "8.0": 21, "9.0": 13}:
        raise ValueError(f"unexpected aperture census: {aperture_census}")
    if comparison:
        if comparison["primary_file"] not in accepted_files or comparison["repeat_file"] not in accepted_files:
            raise ValueError("comparison files must both be accepted inventory members")
        if comparison["primary_file"] not in detailed or comparison["repeat_file"] not in detailed:
            raise ValueError("comparison files must both have detailed evidence")
        if comparison.get("measured") is not True:
            raise ValueError("comparison must be measured before publication")
        expected_max, expected_rms = recompute_comparison(
            accepted_evidence[comparison["primary_file"]],
            accepted_evidence[comparison["repeat_file"]],
        )
        reported_max = finite(comparison.get("max_corner_delta_pp"), "max delta")
        reported_rms = finite(comparison.get("rms_corner_delta_pp"), "RMS delta")
        if not math.isclose(expected_max, reported_max, rel_tol=1e-12, abs_tol=1e-12):
            raise ValueError("comparison max delta disagrees with corner evidence")
        if not math.isclose(expected_rms, reported_rms, rel_tol=1e-12, abs_tol=1e-12):
            raise ValueError("comparison RMS delta disagrees with corner evidence")
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle, lineterminator="\n")
        writer.writerow(
            [
                "file",
                "aperture",
                "shutter",
                "accepted",
                "failed_gates",
                "max_near_ceiling_gate",
                "max_near_ceiling_frame",
                "near_ceiling_gate_r",
                "near_ceiling_gate_g1",
                "near_ceiling_gate_g2",
                "near_ceiling_gate_b",
                "near_ceiling_frame_r",
                "near_ceiling_frame_g1",
                "near_ceiling_frame_g2",
                "near_ceiling_frame_b",
                "finite_fraction_gate_r",
                "finite_fraction_gate_g1",
                "finite_fraction_gate_g2",
                "finite_fraction_gate_b",
                "finite_fraction_frame_r",
                "finite_fraction_frame_g1",
                "finite_fraction_frame_g2",
                "finite_fraction_frame_b",
                "green_center_signal",
                "green_asymmetry",
                "dark_controls_verified",
                "comparison_file",
                "max_corner_delta_pp",
                "rms_corner_delta_pp",
                "analysis_policy",
            ]
        )
        writer.writerows(rows)
    return accepted_evidence


def write_response(
    inputs: list[Path], output: Path, accepted_evidence: dict[str, dict[str, Any]]
) -> None:
    rows: list[list[str]] = []
    accepted_items: dict[str, dict[str, Any]] = {}
    for path in inputs:
        for item in measurements(load(path)):
            if not item.get("accepted"):
                continue
            file_label = str(item["file"])
            validate_options(item, str(path))
            positions = cfa_positions(item, str(path))
            validated_chromatic_maps(item, str(path), positions)
            canonical = accepted_evidence.get(file_label)
            if canonical is None:
                raise ValueError(f"{file_label}: response is not an accepted inventory member")
            for key in (
                "analysis_options",
                "grid",
                "cfa_positions",
                "relative_response",
                "c_rg",
                "c_bg",
                "c_g1g2",
                "corner_relative",
                "green_asymmetry",
                "asymmetry_exceeds_policy",
            ):
                if item.get(key) != canonical.get(key):
                    raise ValueError(f"{file_label}: response and detailed {key} disagree")
            if file_label in accepted_items:
                prior = accepted_items[file_label]
                for key in ("relative_response", "c_rg", "c_bg", "c_g1g2", "grid", "cfa_positions"):
                    if prior.get(key) != item.get(key):
                        raise ValueError(f"{file_label}: duplicate response evidence disagrees")
                continue
            accepted_items[file_label] = item
            relative = item["relative_response"]
            grid = item["grid"]
            cols, grid_rows = int(grid["cols"]), int(grid["rows"])
            bins = cols * grid_rows
            maps = {
                name: relative[positions[name]]
                for name in ("r", "g1", "g2", "b")
            }
            chroma = {name: item[name] for name in ("c_rg", "c_bg", "c_g1g2")}
            if any(len(values) != bins for values in (*maps.values(), *chroma.values())):
                raise ValueError(f"{path}: map size does not match grid")
            for index in range(bins):
                values = {name: finite(data[index], name) for name, data in maps.items()}
                rows.append(
                    [
                        file_label,
                        str(index // cols),
                        str(index % cols),
                        f"{values['r']:.8f}",
                        f"{values['g1']:.8f}",
                        f"{values['g2']:.8f}",
                        f"{values['b']:.8f}",
                        f"{0.5 * (values['g1'] + values['g2']):.8f}",
                        f"{finite(chroma['c_rg'][index], 'c_rg'):.8f}",
                        f"{finite(chroma['c_bg'][index], 'c_bg'):.8f}",
                        f"{finite(chroma['c_g1g2'][index], 'c_g1g2'):.8f}",
                    ]
                )
    if set(accepted_items) != set(accepted_evidence):
        raise ValueError("response file set does not match accepted inventory set")
    if len(rows) != 3 * 16 * 12:
        raise ValueError("expected three accepted 16x12 response maps")
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle, lineterminator="\n")
        writer.writerow(
            [
                "file",
                "bin_row",
                "bin_col",
                "r_relative",
                "g1_relative",
                "g2_relative",
                "b_relative",
                "green_relative",
                "c_rg",
                "c_bg",
                "c_g1g2",
            ]
        )
        writer.writerows(rows)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--inventory-dir", type=Path, required=True)
    parser.add_argument("--detailed", type=Path, nargs="*", default=[])
    parser.add_argument("--response", type=Path, nargs="+", required=True)
    parser.add_argument("--summary-out", type=Path, required=True)
    parser.add_argument("--response-out", type=Path, required=True)
    args = parser.parse_args()

    detailed: dict[str, dict[str, Any]] = {}
    comparison: dict[str, Any] | None = None
    for path in args.detailed:
        document = load(path)
        for item in measurements(document):
            file_label = str(item["file"])
            if file_label in detailed:
                raise ValueError(f"{path}: duplicate detailed file {file_label}")
            detailed[file_label] = item
        if "comparison" in document:
            if comparison is not None:
                raise ValueError("multiple comparison documents are not supported")
            comparison = {
                **document["comparison"],
                "primary_file": document["primary"]["file"],
                "repeat_file": document["repeat"]["file"],
            }
    accepted_evidence = write_summary(
        args.inventory_dir, detailed, comparison, args.summary_out
    )
    write_response(args.response, args.response_out, accepted_evidence)
    print(f"wrote {args.summary_out}")
    print(f"wrote {args.response_out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
