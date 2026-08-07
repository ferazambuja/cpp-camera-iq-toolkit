#!/usr/bin/env python3
"""Hermetic tests for the 2017 spectral-archive normalizer."""

from __future__ import annotations

import importlib.util
import csv
import json
import os
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "import_2017_spectral_archive",
    ROOT / "tools" / "import_2017_spectral_archive.py",
)
assert SPEC and SPEC.loader
IMPORTER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(IMPORTER)


def plain(readings: list[list[tuple[int, float]]]) -> str:
    return "\n\n".join(
        "\n".join(f"{wavelength}\t{value}" for wavelength, value in reading)
        for reading in readings
    ) + "\n"


def cgats(label: str) -> str:
    return (
        "CGATS.17\n"
        f"ORIGINATOR \"{label}\"\n"
        "NUMBER_OF_FIELDS 3\nBEGIN_DATA_FORMAT\n"
        "SAMPLE_ID SAMPLE_NAME SPECTRAL_NM380\nEND_DATA_FORMAT\n"
        "NUMBER_OF_SETS 1\nBEGIN_DATA\n1 A1 0.5\nEND_DATA\n"
    )


def main() -> int:
    with tempfile.TemporaryDirectory() as raw:
        root = Path(raw)
        archive = root / "archive"
        output = root / "output"
        archive.mkdir()
        (archive / "PR655_HID.txt").write_text(
            plain([[(380, 1.0), (384, 2.0)], [(380, 2.0), (384, 4.0)]])
        )
        (archive / "i1Pro_HID.txt").write_text(
            plain([[(380, 3.0), (390, 6.0)], [(380, 4.0), (390, 8.0)]])
        )
        (archive / "CC4.txt").write_text(
            plain([[(380, 0.1), (390, 0.2)], [(380, 0.3), (390, 0.4)]])
        )
        (archive / "CC_Measurement_1.txt").write_text(
            plain([[(380, 0.11), (390, 0.21)], [(380, 0.31), (390, 0.41)]])
        )
        for name in IMPORTER.CGATS_NAMES:
            (archive / name).write_text(cgats(name))

        IMPORTER.import_archive(archive, output)
        hid = (output / "hid_repeats.csv").read_text()
        if hid.splitlines()[0] != (
            "series_id,reading_id,wavelength_nm,value"
        ):
            raise SystemExit("unexpected HID schema")
        if "pr655,repeat_02,384,4" not in hid or "i1pro,repeat_01,390,6" not in hid:
            raise SystemExit("HID repeats were not preserved")
        with (output / "colorchecker_measurement_02.csv").open() as handle:
            repeat_rows = list(csv.reader(handle))
        if repeat_rows[2][0] != "patch_02" or any(
            abs(float(actual) - expected) > 1e-15
            for actual, expected in zip(repeat_rows[2][1:], (0.31, 0.41))
        ):
            raise SystemExit("paired ColorChecker spectra were not preserved")
        receipt = json.loads((output / "source_receipt.json").read_text())
        if receipt["archive_label"] != "retained_2017_coursework_archive":
            raise SystemExit("receipt leaked or omitted the bounded archive label")
        if len(receipt["sources"]) != 8 or len(receipt["outputs"]) != 8:
            raise SystemExit("receipt does not bind all sources and outputs")
        for name in IMPORTER.CGATS_NAMES:
            if (archive / name).read_bytes() != (output / name).read_bytes():
                raise SystemExit("CGATS source must be copied byte-for-byte")

        original_archive_files = sorted(path.name for path in archive.iterdir())
        for unsafe_output in (archive, archive / "generated"):
            try:
                IMPORTER.import_archive(archive, unsafe_output)
            except ValueError as error:
                if "outside the archive root" not in str(error):
                    raise
            else:
                raise SystemExit("archive-contained output directory was accepted")
        if sorted(path.name for path in archive.iterdir()) != original_archive_files:
            raise SystemExit("unsafe output preflight modified the source archive")

        alias_output = root / "alias-output"
        alias_output.mkdir()
        protected_source = archive / "PR655_HID.txt"
        protected_bytes = protected_source.read_bytes()
        os.link(protected_source, alias_output / "hid_repeats.csv")
        try:
            IMPORTER.import_archive(archive, alias_output)
        except ValueError as error:
            if "aliases an archive input" not in str(error):
                raise
        else:
            raise SystemExit("hard-linked output file was accepted")
        if protected_source.read_bytes() != protected_bytes:
            raise SystemExit("hard-linked output preflight modified an archive input")

        symlink_output = root / "symlink-output"
        symlink_output.mkdir()
        (symlink_output / "hid_repeats.csv").symlink_to(protected_source)
        try:
            IMPORTER.import_archive(archive, symlink_output)
        except ValueError as error:
            if "uses a symlink" not in str(error):
                raise
        else:
            raise SystemExit("symlinked output file was accepted")
        if protected_source.read_bytes() != protected_bytes:
            raise SystemExit("symlinked output preflight modified an archive input")

    print("2017 spectral archive importer tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
