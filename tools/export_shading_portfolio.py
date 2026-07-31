#!/usr/bin/env python3
"""Export publication-safe shading aggregates from camera_iq JSON outputs."""

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
    number = float(value)
    if not math.isfinite(number):
        raise ValueError(f"{label}: expected a finite number")
    return number


def write_summary(
    inventory_dir: Path,
    detailed: dict[str, dict[str, Any]],
    comparison: dict[str, Any] | None,
    output: Path,
) -> None:
    rows: list[list[str]] = []
    for path in sorted(inventory_dir.glob("*.json")):
        item = load(path)
        file_label = str(item["file"])
        match = NAME_RE.search(file_label)
        if not match:
            raise ValueError(f"{path}: filename does not encode aperture/shutter")
        gates = item["gates"]
        gate = max(finite(v, "near-ceiling gate") for v in gates["near_ceiling_fraction_gate"])
        frame = max(finite(v, "near-ceiling frame") for v in gates["near_ceiling_fraction_frame"])
        center = gates["center_signal_fraction"]
        green_center = 0.5 * (
            finite(center[1], "G1 center signal")
            + finite(center[2], "G2 center signal")
        )
        failed = [
            name
            for name, key in (
                ("near_ceiling", "near_ceiling_ok"),
                ("low_signal", "low_signal_ok"),
                ("negative", "negative_ok"),
                ("coverage", "coverage_ok"),
                ("finite", "finite_ok"),
            )
            if not bool(gates[key])
        ]
        accepted = bool(item["accepted"])
        if accepted == bool(failed):
            raise ValueError(f"{path}: acceptance and gate verdicts disagree")
        detail = detailed.get(file_label, item)
        asymmetry = detail.get("green_asymmetry")
        pedestal_verified = bool(detail.get("pedestal", {}).get("verified", False))
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
                f"{gate:.8f}",
                f"{frame:.8f}",
                f"{green_center:.8f}",
                "" if asymmetry is None else f"{finite(asymmetry, 'asymmetry'):.8f}",
                str(pedestal_verified).lower(),
                comparison_file,
                max_delta,
                rms_delta,
            ]
        )

    if len(rows) != 52 or sum(row[3] == "true" for row in rows) != 3:
        raise ValueError("expected 52 sphere frames with exactly 3 accepted")
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
                "green_center_signal",
                "green_asymmetry",
                "pedestal_verified",
                "comparison_file",
                "max_corner_delta_pp",
                "rms_corner_delta_pp",
            ]
        )
        writer.writerows(rows)


def write_response(inputs: list[Path], output: Path) -> None:
    rows: list[list[str]] = []
    seen: set[str] = set()
    for path in inputs:
        for item in measurements(load(path)):
            if not item.get("accepted"):
                continue
            file_label = str(item["file"])
            if file_label in seen:
                continue
            seen.add(file_label)
            positions = item.get("cfa_positions")
            if not isinstance(positions, dict):
                raise ValueError(f"{path}: accepted result lacks CFA positions")
            relative = item["relative_response"]
            grid = item["grid"]
            cols, grid_rows = int(grid["cols"]), int(grid["rows"])
            bins = cols * grid_rows
            maps = {
                name: relative[int(positions[name])]
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
    if len(seen) != 3 or len(rows) != 3 * 16 * 12:
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
            detailed[str(item["file"])] = item
        if "comparison" in document:
            comparison = {
                **document["comparison"],
                "primary_file": document["primary"]["file"],
                "repeat_file": document["repeat"]["file"],
            }
    write_summary(args.inventory_dir, detailed, comparison, args.summary_out)
    write_response(args.response, args.response_out)
    print(f"wrote {args.summary_out}")
    print(f"wrote {args.response_out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
