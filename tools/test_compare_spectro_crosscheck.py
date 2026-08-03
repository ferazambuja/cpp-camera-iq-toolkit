#!/usr/bin/env python3
"""Behavior tests for the MATLAB/C++ spectroradiometer comparator."""

from __future__ import annotations

import csv
import hashlib
import json
import pathlib
import subprocess
import sys
import tempfile


SCRIPT = pathlib.Path(__file__).with_name("compare_spectro_crosscheck.py")
FIELDS = [
    "group_id",
    "measurement_index",
    "canonical_path",
    "sha256",
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


def run(
    cpp: pathlib.Path,
    matlab: pathlib.Path,
    *extra: str,
) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, str(SCRIPT), str(cpp), str(matlab), *extra],
        text=True,
        capture_output=True,
        check=False,
    )


def main() -> int:
    row = {
        "group_id": "scene_01",
        "measurement_index": "1",
        "canonical_path": "reading.mat",
        "sha256": "c" * 64,
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

        ledger = root / "ledger.csv"
        exporter = root / "exporter.m"
        ledger.write_text(
            "group_id,repeat_index,canonical_path,sha256\n"
            f"scene_01,1,reading.mat,{'c' * 64}\n",
            encoding="utf-8",
        )
        exporter.write_text("exporter fixture\n", encoding="utf-8")
        receipt_path = root / "receipt.json"
        matched = run(
            cpp,
            matlab,
            "--receipt",
            str(receipt_path),
            "--dataset-id",
            "fixture_dataset",
            "--matlab-release",
            "R2026a",
            "--ledger",
            str(ledger),
            "--matlab-exporter",
            str(exporter),
        )
        assert matched.returncode == 0, matched.stderr
        receipt = json.loads(receipt_path.read_text(encoding="utf-8"))
        assert receipt["schema"] == "camera_iq.spectro_matlab_crosscheck.v1"
        assert receipt["dataset_id"] == "fixture_dataset"
        assert receipt["matlab_release"] == "R2026a"
        comparison = receipt["comparison"]
        assert comparison["result"] == "match"
        assert comparison["reading_count"] == 1
        assert comparison["source_file_hash_comparisons"] == 1
        assert comparison["exact_hash_comparisons"] == 2
        assert comparison["numeric_comparisons"] == 7
        assert comparison["numeric_fields"]["recorded_y"] == {
            "max_absolute_difference": 0.0,
            "max_relative_difference": 0.0,
            "max_tolerance_ratio": 0.0,
        }
        assert receipt["artifact_sha256"]["cpp_readings_csv"] == hashlib.sha256(
            cpp.read_bytes()
        ).hexdigest()
        assert receipt["source_sha256"]["identity_ledger"] == hashlib.sha256(
            ledger.read_bytes()
        ).hexdigest()
        assert not any(
            str(root) in value
            for value in receipt["artifact_sha256"].values()
        )

        ledger.write_text(
            "group_id,repeat_index,canonical_path,sha256\n"
            f"scene_01,1,different.mat,{'c' * 64}\n",
            encoding="utf-8",
        )
        wrong_ledger = run(
            cpp,
            matlab,
            "--receipt",
            str(receipt_path),
            "--dataset-id",
            "fixture_dataset",
            "--matlab-release",
            "R2026a",
            "--ledger",
            str(ledger),
            "--matlab-exporter",
            str(exporter),
        )
        assert wrong_ledger.returncode != 0
        assert "keys do not match the ledger" in wrong_ledger.stderr
        ledger.write_text(
            "group_id,repeat_index,canonical_path,sha256\n"
            f"scene_01,1,reading.mat,{'c' * 64}\n",
            encoding="utf-8",
        )

        ledger.write_text(
            "group_id,repeat_index,canonical_path,sha256\n"
            f"scene_01,1,reading.mat,{'d' * 64}\n",
            encoding="utf-8",
        )
        wrong_source = run(
            cpp,
            matlab,
            "--receipt",
            str(receipt_path),
            "--dataset-id",
            "fixture_dataset",
            "--matlab-release",
            "R2026a",
            "--ledger",
            str(ledger),
            "--matlab-exporter",
            str(exporter),
        )
        assert wrong_source.returncode != 0
        assert "source SHA-256 does not match the ledger" in wrong_source.stderr
        ledger.write_text(
            "group_id,repeat_index,canonical_path,sha256\n"
            f"scene_01,1,reading.mat,{'c' * 64}\n",
            encoding="utf-8",
        )

        for option, value in (
            ("--relative-tolerance", "-1"),
            ("--relative-tolerance", "nan"),
            ("--absolute-tolerance", "inf"),
        ):
            invalid_tolerance = run(cpp, matlab, option, value)
            assert invalid_tolerance.returncode != 0
            assert "tolerances must be finite and non-negative" in (
                invalid_tolerance.stderr
            )

        changed_hash = dict(row)
        changed_hash["radiance_binary64_le_sha256"] = "c" * 64
        write_csv(matlab, changed_hash)
        mismatch = run(cpp, matlab)
        assert mismatch.returncode != 0
        assert "radiance_binary64_le_sha256" in mismatch.stderr

        invalid_hash = dict(row)
        invalid_hash["radiance_binary64_le_sha256"] = "not-a-sha256"
        write_csv(cpp, invalid_hash)
        write_csv(matlab, invalid_hash)
        malformed = run(cpp, matlab)
        assert malformed.returncode != 0
        assert "radiance_binary64_le_sha256 is not a lowercase SHA-256" in (
            malformed.stderr
        )
        write_csv(cpp, row)

        changed_source = dict(row)
        changed_source["sha256"] = "d" * 64
        write_csv(matlab, changed_source)
        source_mismatch = run(cpp, matlab)
        assert source_mismatch.returncode != 0
        assert "sha256 differs" in source_mismatch.stderr

        changed_number = dict(row)
        changed_number["recorded_y"] = "20.1"
        write_csv(matlab, changed_number)
        mismatch = run(cpp, matlab)
        assert mismatch.returncode != 0
        assert "recorded_y" in mismatch.stderr

        zero_tolerance = run(
            cpp,
            matlab,
            "--relative-tolerance",
            "0",
            "--absolute-tolerance",
            "0",
        )
        assert zero_tolerance.returncode != 0
        assert "recorded_y" in zero_tolerance.stderr
        assert "Traceback" not in zero_tolerance.stderr

        write_csv(matlab, row)
        colliding_output = run(
            cpp,
            matlab,
            "--receipt",
            str(cpp),
            "--dataset-id",
            "fixture_dataset",
            "--matlab-release",
            "R2026a",
            "--ledger",
            str(ledger),
            "--matlab-exporter",
            str(exporter),
        )
        assert colliding_output.returncode != 0
        assert "must not overwrite an input" in colliding_output.stderr
        assert cpp.read_text(encoding="utf-8").startswith("group_id,")

    print("spectro cross-check comparator behavior: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
