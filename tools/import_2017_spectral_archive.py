#!/usr/bin/env python3
"""Normalize the retained 2017 spectral files into public, strict schemas.

The source archive is read-only input. CGATS exports are copied byte-for-byte
because their schema declarations are part of the interoperability evidence;
plain numeric blocks are converted to long-form CSV. The receipt records only
source-relative names and hashes, never the archive's machine path.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import shutil
from pathlib import Path


HID_NAMES = ("PR655_HID.txt", "i1Pro_HID.txt")
REPEAT_NAMES = ("CC4.txt", "CC_Measurement_1.txt")
CGATS_NAMES = (
    "CC4_CGATS.txt",
    "CC4_4.txt",
    "CC4_CGATS_M0.txt",
    "CC4_4_M0.txt",
)
ALL_NAMES = HID_NAMES + REPEAT_NAMES + CGATS_NAMES
OUTPUT_NAMES = (
    "hid_repeats.csv",
    "colorchecker_measurement_01.csv",
    "colorchecker_measurement_02.csv",
    *CGATS_NAMES,
    "archive_summary.json",
    "source_receipt.json",
)


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def parse_plain_blocks(path: Path) -> list[list[tuple[float, float]]]:
    blocks: list[list[tuple[float, float]]] = []
    current: list[tuple[float, float]] = []
    for line_number, raw in enumerate(path.read_text().splitlines(), 1):
        clean = raw.strip()
        if not clean:
            if current:
                blocks.append(current)
                current = []
            continue
        fields = clean.split()
        if len(fields) != 2:
            raise ValueError(f"{path.name}:{line_number}: expected two fields")
        wavelength, value = map(float, fields)
        if not math.isfinite(wavelength) or not math.isfinite(value):
            raise ValueError(f"{path.name}:{line_number}: non-finite value")
        if current and wavelength <= current[-1][0]:
            blocks.append(current)
            current = []
        current.append((wavelength, value))
    if current:
        blocks.append(current)
    if not blocks or any(len(block) < 2 for block in blocks):
        raise ValueError(f"{path.name}: missing non-trivial spectral blocks")
    axis = [item[0] for item in blocks[0]]
    step = axis[1] - axis[0]
    if step <= 0 or any(abs((axis[i] - axis[i - 1]) - step) > 1e-9
                        for i in range(1, len(axis))):
        raise ValueError(f"{path.name}: first wavelength grid is not uniform")
    for block in blocks[1:]:
        if [item[0] for item in block] != axis:
            raise ValueError(f"{path.name}: spectral blocks do not share one axis")
    return blocks


def scalar(value: float) -> str:
    return format(value, ".17g")


def write_hid(path: Path, parsed: dict[str, list[list[tuple[float, float]]]]) -> None:
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.writer(handle, lineterminator="\n")
        writer.writerow(["series_id", "reading_id", "wavelength_nm", "value"])
        for source_name, series_id in ((HID_NAMES[0], "pr655"),
                                       (HID_NAMES[1], "i1pro")):
            for reading_index, block in enumerate(parsed[source_name], 1):
                reading_id = f"repeat_{reading_index:02d}"
                for wavelength, value in block:
                    writer.writerow(
                        [series_id, reading_id, scalar(wavelength), scalar(value)]
                    )


def write_reference(path: Path, blocks: list[list[tuple[float, float]]]) -> None:
    wavelengths = [item[0] for item in blocks[0]]
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.writer(handle, lineterminator="\n")
        writer.writerow(["patch_id", *(scalar(value) for value in wavelengths)])
        for sample_index, block in enumerate(blocks, 1):
            writer.writerow(
                [f"patch_{sample_index:02d}", *(scalar(value) for _, value in block)]
            )


def write_repeats(output: Path,
                  parsed: dict[str, list[list[tuple[float, float]]]]) -> None:
    first = parsed[REPEAT_NAMES[0]]
    second = parsed[REPEAT_NAMES[1]]
    if len(first) != len(second):
        raise ValueError("ColorChecker repeat files have different patch counts")
    write_reference(output / "colorchecker_measurement_01.csv", first)
    write_reference(output / "colorchecker_measurement_02.csv", second)


def import_archive(archive: Path, output: Path) -> None:
    try:
        archive_root = archive.resolve(strict=True)
    except FileNotFoundError as error:
        raise ValueError("archive root does not exist") from error
    if not archive_root.is_dir():
        raise ValueError("archive root is not a directory")
    output_root = output.resolve(strict=False)
    if output_root == archive_root or archive_root in output_root.parents:
        raise ValueError("output directory must be outside the archive root")
    archive = archive_root
    output = output_root
    missing = [name for name in ALL_NAMES if not (archive / name).is_file()]
    if missing:
        raise ValueError("archive is missing: " + ", ".join(missing))
    if output.exists() and not output.is_dir():
        raise ValueError("output path is not a directory")
    archive_inputs = [archive / name for name in ALL_NAMES]
    for name in OUTPUT_NAMES:
        target = output / name
        if target.is_symlink():
            raise ValueError("output file aliases an archive input or uses a symlink")
        if target.exists() and any(target.samefile(source) for source in archive_inputs):
            raise ValueError("output file aliases an archive input")
    output.mkdir(parents=True, exist_ok=True)
    parsed = {
        name: parse_plain_blocks(archive / name)
        for name in HID_NAMES + REPEAT_NAMES
    }
    write_hid(output / "hid_repeats.csv", parsed)
    write_repeats(output, parsed)
    for name in CGATS_NAMES:
        shutil.copyfile(archive / name, output / name)

    summary = {
        "schema_version": 1,
        "archive_label": "retained_2017_coursework_archive",
        "archive_scope_id": "spectral_yes_subset",
        "hid_series": [
            {
                "id": "pr655",
                "reading_count": len(parsed[HID_NAMES[0]]),
                "sample_count": len(parsed[HID_NAMES[0]][0]),
                "first_wavelength_nm": parsed[HID_NAMES[0]][0][0][0],
                "last_wavelength_nm": parsed[HID_NAMES[0]][0][-1][0],
            },
            {
                "id": "i1pro",
                "reading_count": len(parsed[HID_NAMES[1]]),
                "sample_count": len(parsed[HID_NAMES[1]][0]),
                "first_wavelength_nm": parsed[HID_NAMES[1]][0][0][0],
                "last_wavelength_nm": parsed[HID_NAMES[1]][0][-1][0],
            },
        ],
        "colorchecker_patch_count": len(parsed[REPEAT_NAMES[0]]),
        "colorchecker_measurement_count": 2,
        "cgats_export_count": len(CGATS_NAMES),
        "cgats_evidence_scope": "one_measurement_reserialized",
    }
    summary_path = output / "archive_summary.json"
    summary_path.write_text(json.dumps(summary, indent=2) + "\n")

    output_names = OUTPUT_NAMES[:-1]
    receipt = {
        "schema_version": 1,
        "archive_label": "retained_2017_coursework_archive",
        "archive_scope_id": "spectral_yes_subset",
        "sources": [
            {"file": name, "sha256": sha256(archive / name)}
            for name in ALL_NAMES
        ],
        "outputs": [
            {"file": name, "sha256": sha256(output / name)}
            for name in output_names
        ],
    }
    (output / "source_receipt.json").write_text(
        json.dumps(receipt, indent=2) + "\n"
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--archive-root", type=Path, required=True)
    parser.add_argument("--out-dir", type=Path, required=True)
    args = parser.parse_args()
    import_archive(args.archive_root, args.out_dir)
    print(f"normalized 8 retained source files into {args.out_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
