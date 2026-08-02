#!/usr/bin/env python3
"""Behavior tests for the committed spectroradiometer receipt guard."""

from __future__ import annotations

import hashlib
import json
import pathlib
import subprocess
import sys
import tempfile
import unittest


SCRIPT = pathlib.Path(__file__).with_name("check_spectro_receipt.py")


class SpectroReceiptGuardTests(unittest.TestCase):
    def test_guard_recomputes_public_counts_hashes_and_metrics(self) -> None:
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
                "receipt_schema_version": 1,
                "result_schema_version": 2,
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
                "recorded_metadata_checks": {
                    "numbered_prd": {"count": 1},
                    "other_records": {"count": 1},
                },
            }
            (data / "receipt.json").write_text(json.dumps(receipt), encoding="utf-8")

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
            valid = subprocess.run(command, text=True, capture_output=True, check=False)
            self.assertEqual(valid.returncode, 0, valid.stderr)

            (data / "groups.csv").write_text(
                groups.replace(",0.5,0.002,", ",0.4,0.002,"), encoding="utf-8"
            )
            stale = subprocess.run(command, text=True, capture_output=True, check=False)
            self.assertNotEqual(stale.returncode, 0)


if __name__ == "__main__":
    unittest.main()
