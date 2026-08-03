#!/usr/bin/env python3
"""Behavior tests for the public MATLAB/C++ cross-check receipt guard."""

from __future__ import annotations

import csv
import hashlib
import json
import pathlib
import subprocess
import sys
import tempfile


TOOLS = pathlib.Path(__file__).parent
COMPARATOR = TOOLS / "compare_spectro_crosscheck.py"
CHECKER = TOOLS / "check_spectro_matlab_crosscheck_receipt.py"
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


def run_checker(
    receipt: pathlib.Path,
    ledger: pathlib.Path,
    exporter: pathlib.Path,
    result_receipt: pathlib.Path,
    cpp: pathlib.Path | None = None,
    matlab: pathlib.Path | None = None,
    repo_root: pathlib.Path | None = None,
) -> subprocess.CompletedProcess[str]:
    command = [
        sys.executable,
        str(CHECKER),
        "--receipt",
        str(receipt),
        "--result-receipt",
        str(result_receipt),
        "--ledger",
        str(ledger),
        "--matlab-exporter",
        str(exporter),
        "--comparator",
        str(COMPARATOR),
    ]
    if repo_root is not None:
        command.extend(["--repo-root", str(repo_root)])
    if cpp is not None and matlab is not None:
        command.extend(["--cpp-csv", str(cpp), "--matlab-csv", str(matlab)])
    return subprocess.run(
        command,
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
        for path in (cpp, matlab):
            with path.open("w", newline="", encoding="utf-8") as stream:
                writer = csv.DictWriter(stream, fieldnames=FIELDS)
                writer.writeheader()
                writer.writerow(row)
        ledger = root / "ledger.csv"
        exporter = root / "exporter.m"
        receipt = root / "receipt.json"
        ledger.write_text(
            "group_id,repeat_index,canonical_path,sha256\n"
            f"scene_01,1,reading.mat,{'c' * 64}\n",
            encoding="utf-8",
        )
        exporter.write_text("exporter fixture\n", encoding="utf-8")
        generated = subprocess.run(
            [
                sys.executable,
                str(COMPARATOR),
                str(cpp),
                str(matlab),
                "--receipt",
                str(receipt),
                "--dataset-id",
                "fixture_dataset",
                "--matlab-release",
                "R2026a",
                "--ledger",
                str(ledger),
                "--matlab-exporter",
                str(exporter),
            ],
            text=True,
            capture_output=True,
            check=False,
        )
        assert generated.returncode == 0, generated.stderr

        result_receipt = root / "result-receipt.json"
        result_receipt.write_text(
            json.dumps(
                {
                    "dataset": "fixture_dataset",
                    "evidence": {"canonical_readings": 1},
                    "inputs": {
                        "identity_ledger": {
                            "sha256": hashlib.sha256(ledger.read_bytes()).hexdigest()
                        }
                    },
                    "artifacts": {
                        "archive_run_readings": {
                            "sha256": hashlib.sha256(cpp.read_bytes()).hexdigest()
                        }
                    }
                }
            ),
            encoding="utf-8",
        )

        valid = run_checker(receipt, ledger, exporter, result_receipt)
        assert valid.returncode == 0, valid.stderr
        assert "valid" in valid.stdout

        repo_root_supported = run_checker(
            receipt, ledger, exporter, result_receipt, repo_root=root
        )
        assert repo_root_supported.returncode == 0, repo_root_supported.stderr

        result_document = json.loads(result_receipt.read_text(encoding="utf-8"))
        result_document["dataset"] = "different_dataset"
        result_receipt.write_text(json.dumps(result_document), encoding="utf-8")
        wrong_dataset = run_checker(receipt, ledger, exporter, result_receipt)
        assert wrong_dataset.returncode != 0
        assert "dataset_id does not match" in wrong_dataset.stderr
        result_document["dataset"] = "fixture_dataset"

        result_document["evidence"]["canonical_readings"] = 2
        result_receipt.write_text(json.dumps(result_document), encoding="utf-8")
        wrong_authority_count = run_checker(receipt, ledger, exporter, result_receipt)
        assert wrong_authority_count.returncode != 0
        assert "reading_count does not match the result receipt" in (
            wrong_authority_count.stderr
        )
        result_document["evidence"]["canonical_readings"] = 1

        result_document["evidence"]["canonical_readings"] = True
        result_receipt.write_text(json.dumps(result_document), encoding="utf-8")
        invalid_authority_count = run_checker(
            receipt, ledger, exporter, result_receipt
        )
        assert invalid_authority_count.returncode != 0
        assert "result receipt evidence.canonical_readings" in (
            invalid_authority_count.stderr
        )
        result_document["evidence"]["canonical_readings"] = 1

        result_document["inputs"]["identity_ledger"]["sha256"] = "d" * 64
        result_receipt.write_text(json.dumps(result_document), encoding="utf-8")
        wrong_ledger_authority = run_checker(
            receipt, ledger, exporter, result_receipt
        )
        assert wrong_ledger_authority.returncode != 0
        assert "identity_ledger does not match the result receipt" in (
            wrong_ledger_authority.stderr
        )
        result_document["inputs"]["identity_ledger"]["sha256"] = hashlib.sha256(
            ledger.read_bytes()
        ).hexdigest()
        result_receipt.write_text(json.dumps(result_document), encoding="utf-8")

        document = json.loads(receipt.read_text(encoding="utf-8"))
        document["comparison"]["reading_count"] = 2
        document["comparison"]["exact_hash_comparisons"] = 4
        document["comparison"]["numeric_comparisons"] = 14
        receipt.write_text(json.dumps(document), encoding="utf-8")
        wrong_ledger_count = run_checker(receipt, ledger, exporter, result_receipt)
        assert wrong_ledger_count.returncode != 0
        assert "reading_count does not match the identity ledger" in (
            wrong_ledger_count.stderr
        )
        document["comparison"]["reading_count"] = 1
        document["comparison"]["exact_hash_comparisons"] = 2
        document["comparison"]["numeric_comparisons"] = 7
        receipt.write_text(json.dumps(document), encoding="utf-8")

        document["comparison"]["source_file_hash_comparisons"] = 0
        receipt.write_text(json.dumps(document), encoding="utf-8")
        wrong_source_count = run_checker(receipt, ledger, exporter, result_receipt)
        assert wrong_source_count.returncode != 0
        assert "source_file_hash_comparisons is inconsistent" in (
            wrong_source_count.stderr
        )
        document["comparison"]["source_file_hash_comparisons"] = 1
        receipt.write_text(json.dumps(document), encoding="utf-8")

        fresh = run_checker(
            receipt, ledger, exporter, result_receipt, cpp=cpp, matlab=matlab
        )
        assert fresh.returncode == 0, fresh.stderr

        original_cpp = cpp.read_text(encoding="utf-8")
        cpp.write_text(original_cpp + "changed\n", encoding="utf-8")
        stale_artifact = run_checker(
            receipt, ledger, exporter, result_receipt, cpp=cpp, matlab=matlab
        )
        assert stale_artifact.returncode != 0
        assert "cpp_readings_csv" in stale_artifact.stderr
        cpp.write_text(original_cpp, encoding="utf-8")

        ledger.write_text("changed ledger fixture\n", encoding="utf-8")
        stale = run_checker(receipt, ledger, exporter, result_receipt)
        assert stale.returncode != 0
        assert "identity ledger" in stale.stderr
        ledger.write_text(
            "group_id,repeat_index,canonical_path,sha256\n"
            f"scene_01,1,reading.mat,{'c' * 64}\n",
            encoding="utf-8",
        )

        document = json.loads(receipt.read_text(encoding="utf-8"))
        original_numeric = document["comparison"]["numeric_fields"]["recorded_x"]
        document["comparison"]["numeric_fields"]["recorded_x"] = None
        receipt.write_text(json.dumps(document), encoding="utf-8")
        missing_numeric = run_checker(receipt, ledger, exporter, result_receipt)
        assert missing_numeric.returncode != 0
        assert "recorded_x must be an object" in missing_numeric.stderr
        document["comparison"]["numeric_fields"]["recorded_x"] = original_numeric

        document["comparison"]["numeric_fields"]["recorded_y"][
            "max_tolerance_ratio"
        ] = 1.01
        receipt.write_text(json.dumps(document), encoding="utf-8")
        outside_tolerance = run_checker(receipt, ledger, exporter, result_receipt)
        assert outside_tolerance.returncode != 0
        assert "max_tolerance_ratio" in outside_tolerance.stderr

        document["comparison"]["numeric_fields"]["recorded_y"][
            "max_tolerance_ratio"
        ] = 0.0
        document["private_path"] = "/" + "Users/example/archive"
        receipt.write_text(json.dumps(document), encoding="utf-8")
        exposed = run_checker(receipt, ledger, exporter, result_receipt)
        assert exposed.returncode != 0
        assert "path-like private value" in exposed.stderr

    print("spectro MATLAB receipt guard behavior: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
