#!/usr/bin/env python3
"""Behavior test for the privacy-safe spectroradiometer result receipt."""

from __future__ import annotations

import json
import pathlib
import subprocess
import sys
import tempfile
import unittest


SCRIPT = pathlib.Path(__file__).with_name("generate_spectro_receipt.py")


class SpectroReceiptTests(unittest.TestCase):
    def test_receipt_binds_inputs_metrics_and_recorded_metadata_check(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            result = {
                "schema_version": 2,
                "dataset": "dataset-root:fixture",
                "ledger": {"file": "ledger.csv", "sha256": "a" * 64},
                "evidence": {
                    "canonical_readings": 3,
                    "declared_aliases": 1,
                    "aliases_verified": True,
                    "measurement_groups": 2,
                },
                "closure": {
                    "observer_file": "cmf.csv",
                    "observer_sha256": "b" * 64,
                    "sample_weighting": "uniform_equal_weight",
                    "scale_source": "derived_from_recorded_xyz",
                    "scale_value": 10.0,
                    "max_absolute_relative_residual_percent": 1e-12,
                    "rms_relative_residual_percent": 5e-13,
                },
                "groups": [
                    {"group_id": "scene_01", "count": 2},
                    {"group_id": "scene_02", "count": 1},
                ],
            }
            (root / "result.json").write_text(json.dumps(result), encoding="utf-8")
            (root / "groups.csv").write_text(
                "group_id,count,mean_spectral_integral,"
                "sample_stddev_spectral_integral,coefficient_of_variation,"
                "max_pair_delta_u_prime_v_prime,max_shape_relative_l2,"
                "variation_label\n"
                "scene_01,2,2,1,0.5,0.002,0.01,"
                "within_group_observed_variation\n"
                "scene_02,1,3,,,,,not_established_single_measurement\n",
                encoding="utf-8",
            )
            (root / "readings.csv").write_text(
                "group_id,canonical_path,spectral_integral,"
                "recorded_total_radiance\n"
                "scene_01,PRD measurments/PRD_01.mat,2,2\n"
                "scene_01,PRD measurments/PRD_02.mat,2,2\n"
                "scene_02,Old/example.mat,4,0.008\n",
                encoding="utf-8",
            )
            output = root / "receipt.json"
            completed = subprocess.run(
                [
                    sys.executable,
                    str(SCRIPT),
                    "--result",
                    str(root / "result.json"),
                    "--groups-csv",
                    str(root / "groups.csv"),
                    "--readings-csv",
                    str(root / "readings.csv"),
                    "--out",
                    str(output),
                ],
                text=True,
                capture_output=True,
                check=False,
            )
            self.assertEqual(completed.returncode, 0, completed.stderr)
            receipt = json.loads(output.read_text(encoding="utf-8"))
            self.assertEqual(receipt["dataset"], "fixture")
            self.assertEqual(receipt["result_schema_version"], 2)
            self.assertEqual(receipt["inputs"]["identity_ledger"]["sha256"], "a" * 64)
            self.assertEqual(receipt["evidence"]["repeated_groups"], 1)
            self.assertEqual(
                receipt["group_metrics"]
                ["spectral_integral_coefficient_of_variation_percent"]["maximum"],
                50.0,
            )
            self.assertEqual(
                receipt["recorded_metadata_checks"]["numbered_prd"]
                ["max_absolute_integral_ratio_error"],
                0.0,
            )
            self.assertEqual(
                receipt["recorded_metadata_checks"]["other_records"]
                ["total_radiance_to_integral_ratio"]["minimum"],
                0.002,
            )


if __name__ == "__main__":
    unittest.main()
