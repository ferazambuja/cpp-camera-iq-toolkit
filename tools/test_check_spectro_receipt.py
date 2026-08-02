#!/usr/bin/env python3
"""Behavior tests for the committed spectroradiometer receipt guard."""

from __future__ import annotations

import copy
import hashlib
import json
import pathlib
import subprocess
import sys
import tempfile
import unittest


SCRIPT = pathlib.Path(__file__).with_name("check_spectro_receipt.py")
RESULT_SCHEMA_VERSION = 2


class SpectroReceiptGuardTests(unittest.TestCase):
    def test_guard_recomputes_public_facts_and_validates_archive_run_receipt(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            data = root / "data"
            data.mkdir()
            ledger = (
                "group_id,repeat_index,canonical_path,sha256,alias_paths\n"
                f"scene_01,1,a.mat,{'a' * 64},alias.mat\n"
                f"scene_01,2,b.mat,{'b' * 64},\n"
            )
            cmf = "Wavelength (nm),X,Y,Z\n1,1,0,0\n2,0,1,0\n"
            groups = (
                "group_id,count,mean_spectral_integral,"
                "sample_stddev_spectral_integral,coefficient_of_variation,"
                "max_pair_delta_u_prime_v_prime,max_shape_relative_l2,"
                "variation_label\n"
                "scene_01,2,2,1,0.5,0.002,0.01,"
                "within_group_observed_variation\n"
            )
            (data / "ledger.csv").write_text(ledger, encoding="utf-8")
            (data / "cmf.csv").write_text(cmf, encoding="utf-8")
            (data / "groups.csv").write_text(groups, encoding="utf-8")
            receipt = {
                "receipt_schema_version": 2,
                "result_schema_version": RESULT_SCHEMA_VERSION,
                "derivation": {
                    "tool": "tools/generate_spectro_receipt.py",
                    "version": 1,
                },
                "artifacts": {
                    "public_group_summary": {
                        "sha256": hashlib.sha256(groups.encode()).hexdigest()
                    },
                    "archive_run_result": {"sha256": "c" * 64},
                    "archive_run_readings": {"sha256": "d" * 64},
                },
                "inputs": {
                    "identity_ledger": {
                        "file": "ledger.csv",
                        "sha256": hashlib.sha256(ledger.encode()).hexdigest(),
                    },
                    "observer": {
                        "file": "cmf.csv",
                        "sha256": hashlib.sha256(cmf.encode()).hexdigest(),
                    },
                },
                "evidence": {
                    "canonical_readings": 2,
                    "declared_aliases": 1,
                    "aliases_verified": True,
                    "measurement_groups": 1,
                    "repeated_groups": 1,
                    "singleton_groups": 0,
                },
                "group_metrics": {
                    "spectral_integral_coefficient_of_variation_percent": {
                        "median": 50.0,
                        "maximum": 50.0,
                    },
                    "normalized_shape_relative_l2_percent": {
                        "median": 1.0,
                        "maximum": 1.0,
                    },
                    "recorded_xyz_max_pair_delta_u_prime_v_prime": {
                        "median": 0.002,
                        "maximum": 0.002,
                    },
                },
                "closure": {
                    "sample_weighting": "uniform_equal_weight",
                    "scale_source": "derived_from_recorded_xyz",
                    "scale_value": 683.0,
                    "max_absolute_relative_residual_percent": 1e-12,
                    "rms_relative_residual_percent": 5e-13,
                },
                "recorded_metadata_checks": {
                    "numbered_prd": {
                        "count": 1,
                        "max_absolute_integral_ratio_error": 1e-9,
                    },
                    "other_records": {
                        "count": 1,
                        "total_radiance_to_integral_ratio": {
                            "minimum": 0.001,
                            "maximum": 0.002,
                        },
                    },
                },
            }

            command = [
                sys.executable,
                str(SCRIPT),
                "--repo-root",
                str(root),
                "--receipt",
                "data/receipt.json",
                "--groups-csv",
                "data/groups.csv",
            ]

            def run(candidate: dict[str, object]) -> subprocess.CompletedProcess[str]:
                (data / "receipt.json").write_text(
                    json.dumps(candidate), encoding="utf-8"
                )
                return subprocess.run(
                    command, text=True, capture_output=True, check=False
                )

            valid = run(receipt)
            self.assertEqual(valid.returncode, 0, valid.stderr)

            regrouped_ledger = ledger.replace("scene_01", "different_group")
            (data / "ledger.csv").write_text(regrouped_ledger, encoding="utf-8")
            regrouped_receipt = copy.deepcopy(receipt)
            regrouped_receipt["inputs"]["identity_ledger"]["sha256"] = (  # type: ignore[index]
                hashlib.sha256(regrouped_ledger.encode()).hexdigest()
            )
            regrouped = run(regrouped_receipt)
            self.assertNotEqual(
                regrouped.returncode,
                0,
                "receipt guard accepted aggregate membership that disagrees "
                "with the ledger",
            )
            (data / "ledger.csv").write_text(ledger, encoding="utf-8")

            mutations = {
                "public artifact digest": (
                    "artifacts",
                    "public_group_summary",
                    "sha256",
                    "e" * 64,
                ),
                "archive artifact digest": (
                    "artifacts",
                    "archive_run_result",
                    "sha256",
                    "not-a-digest",
                ),
                "derivation version": ("derivation", "version", 2),
                "closure source": ("closure", "scale_source", "assumed"),
                "closure residual": (
                    "closure",
                    "max_absolute_relative_residual_percent",
                    -1.0,
                ),
                "metadata error": (
                    "recorded_metadata_checks",
                    "numbered_prd",
                    "max_absolute_integral_ratio_error",
                    -1.0,
                ),
                "metadata ratio order": (
                    "recorded_metadata_checks",
                    "other_records",
                    "total_radiance_to_integral_ratio",
                    {"minimum": 2.0, "maximum": 1.0},
                ),
            }
            for label, path in mutations.items():
                with self.subTest(label=label):
                    candidate = copy.deepcopy(receipt)
                    *parents, value = path
                    target: object = candidate
                    for key in parents[:-1]:
                        target = target[key]  # type: ignore[index]
                    target[parents[-1]] = value  # type: ignore[index]
                    rejected = run(candidate)
                    self.assertNotEqual(rejected.returncode, 0, rejected.stderr)


if __name__ == "__main__":
    unittest.main()
