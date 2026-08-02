#!/usr/bin/env python3
"""Behavior tests for the MATLAB/C++ spectroradiometer comparator."""

from __future__ import annotations

import csv
import pathlib
import subprocess
import sys
import tempfile


SCRIPT = pathlib.Path(__file__).with_name("compare_spectro_crosscheck.py")
FIELDS = [
    "group_id",
    "measurement_index",
    "canonical_path",
    "wavelength_binary64_le_sha256",
    "radiance_binary64_le_sha256",
    "spectral_integral",
    "recorded_x",
    "recorded_y",
    "recorded_z",
    "recorded_total_radiance",
    "recorded_cct_k",
    "recorded_duv",
]


def write_csv(path: pathlib.Path, row: dict[str, str]) -> None:
    with path.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=FIELDS)
        writer.writeheader()
        writer.writerow(row)


def run(cpp: pathlib.Path, matlab: pathlib.Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, str(SCRIPT), str(cpp), str(matlab)],
        text=True,
        capture_output=True,
        check=False,
    )


def main() -> int:
    row = {
        "group_id": "scene_01",
        "measurement_index": "1",
        "canonical_path": "reading.mat",
        "wavelength_binary64_le_sha256": "a" * 64,
        "radiance_binary64_le_sha256": "b" * 64,
        "spectral_integral": "12",
        "recorded_x": "10",
        "recorded_y": "20",
        "recorded_z": "30",
        "recorded_total_radiance": "12",
        "recorded_cct_k": "5500",
        "recorded_duv": "-0.001",
    }
    with tempfile.TemporaryDirectory() as directory:
        root = pathlib.Path(directory)
        cpp = root / "cpp.csv"
        matlab = root / "matlab.csv"
        write_csv(cpp, row)
        write_csv(matlab, row)
        matched = run(cpp, matlab)
        assert matched.returncode == 0, matched.stderr
        assert "matches: 1 readings" in matched.stdout

        changed_hash = dict(row)
        changed_hash["radiance_binary64_le_sha256"] = "c" * 64
        write_csv(matlab, changed_hash)
        mismatch = run(cpp, matlab)
        assert mismatch.returncode != 0
        assert "radiance_binary64_le_sha256" in mismatch.stderr

        changed_number = dict(row)
        changed_number["recorded_y"] = "20.1"
        write_csv(matlab, changed_number)
        mismatch = run(cpp, matlab)
        assert mismatch.returncode != 0
        assert "recorded_y" in mismatch.stderr

    print("spectro cross-check comparator behavior: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
