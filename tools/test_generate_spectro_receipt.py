#!/usr/bin/env python3
"""Behavior tests for the privacy-safe spectroradiometer result receipt."""

from __future__ import annotations

import hashlib
import json
import pathlib
import subprocess
import sys
import tempfile
import unittest


SCRIPT = pathlib.Path(__file__).with_name("generate_spectro_receipt.py")
RESULT_SCHEMA_VERSION = 2


def result_fixture() -> dict[str, object]:
    def reading(index: int, path: str, digest: str, integral: float) -> dict[str, object]:
        return {
            "measurement_index": index,
            "path": path,
            "sha256": digest,
            "spectral_integral": integral,
            "recorded_xyz": [1.0, 2.0, 3.0],
            "recorded_total_radiance": integral,
            "recorded_cct_k": 5000.0,
            "recorded_duv": 0.001,
            "computed_xyz": [1.1, 2.2, 3.3],
            "signed_relative_residual_percent": [0.1, 0.2, 0.3],
            "chromaticity": {
                "x": 1.0 / 6.0,
                "y": 1.0 / 3.0,
                "u_prime": 4.0 / 40.0,
                "v_prime": 18.0 / 40.0,
            },
        }

    return {
        "schema_version": RESULT_SCHEMA_VERSION,
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
            {
                "group_id": "scene_01",
                "count": 2,
                "absolute": {
                    "mean_spectral_integral": 2.0,
                    "sample_stddev_spectral_integral": 1.0,
                    "coefficient_of_variation": 0.5,
                },
                "shape": {"max_relative_l2": 0.01},
                "recorded_xyz_chromaticity": {
                    "max_pair_delta_u_prime_v_prime": 0.002
                },
                "readings": [
                    reading(1, "PRD measurments/PRD_01.mat", "c" * 64, 2.0),
                    reading(2, "PRD measurments/PRD_02.mat", "d" * 64, 2.0),
                ],
            },
            {
                "group_id": "scene_02",
                "count": 1,
                "absolute": {
                    "mean_spectral_integral": 4.0,
                    "sample_stddev_spectral_integral": None,
                    "coefficient_of_variation": None,
                },
                "shape": {"max_relative_l2": None},
                "recorded_xyz_chromaticity": {
                    "max_pair_delta_u_prime_v_prime": None
                },
                "readings": [reading(1, "Old/example.mat", "e" * 64, 4.0)],
            },
        ],
    }


GROUPS = (
    "group_id,count,mean_spectral_integral,"
    "sample_stddev_spectral_integral,coefficient_of_variation,"
    "max_pair_delta_u_prime_v_prime,max_shape_relative_l2,variation_label\n"
    "scene_01,2,2,1,0.5,0.002,0.01,within_group_observed_variation\n"
    "scene_02,1,4,,,,,not_established_single_measurement\n"
)

READING_HEADER = (
    "group_id,measurement_index,canonical_path,sha256,spectral_integral,"
    "wavelength_binary64_le_sha256,radiance_binary64_le_sha256,"
    "recorded_x,recorded_y,recorded_z,chromaticity_x,chromaticity_y,"
    "u_prime,v_prime,recorded_total_radiance,recorded_cct_k,recorded_duv,"
    "computed_x,computed_y,computed_z,residual_x_percent,residual_y_percent,"
    "residual_z_percent\n"
)
READINGS = READING_HEADER + (
    f"scene_01,1,PRD measurments/PRD_01.mat,{'c' * 64},2,{'1' * 64},"
    f"{'2' * 64},1,2,3,{1 / 6},{1 / 3},0.1,0.45,2,5000,0.001,"
    "1.1,2.2,3.3,0.1,0.2,0.3\n"
    f"scene_01,2,PRD measurments/PRD_02.mat,{'d' * 64},2,{'3' * 64},"
    f"{'4' * 64},1,2,3,{1 / 6},{1 / 3},0.1,0.45,2,5000,0.001,"
    "1.1,2.2,3.3,0.1,0.2,0.3\n"
    f"scene_02,1,Old/example.mat,{'e' * 64},4,{'5' * 64},{'6' * 64},"
    f"1,2,3,{1 / 6},{1 / 3},0.1,0.45,4,5000,0.001,"
    "1.1,2.2,3.3,0.1,0.2,0.3\n"
)


class SpectroReceiptTests(unittest.TestCase):
    def run_generator(
        self,
        root: pathlib.Path,
        *,
        result: dict[str, object] | None = None,
        groups: str = GROUPS,
        readings: str = READINGS,
    ) -> tuple[subprocess.CompletedProcess[str], pathlib.Path]:
        (root / "result.json").write_text(
            json.dumps(result or result_fixture()), encoding="utf-8"
        )
        (root / "groups.csv").write_text(groups, encoding="utf-8")
        (root / "readings.csv").write_text(readings, encoding="utf-8")
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
        return completed, output

    def test_receipt_binds_artifacts_metrics_and_recorded_metadata(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            completed, output = self.run_generator(root)
            self.assertEqual(completed.returncode, 0, completed.stderr)
            receipt = json.loads(output.read_text(encoding="utf-8"))
            self.assertEqual(receipt["dataset"], "fixture")
            self.assertEqual(
                receipt["result_schema_version"], RESULT_SCHEMA_VERSION
            )
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
                1.0,
            )
            self.assertEqual(
                receipt["artifacts"]["public_group_summary"]["sha256"],
                hashlib.sha256(GROUPS.encode()).hexdigest(),
            )
            self.assertEqual(
                receipt["artifacts"]["archive_run_result"]["sha256"],
                hashlib.sha256((root / "result.json").read_bytes()).hexdigest(),
            )

    def test_receipt_refuses_spliced_json_and_csv_artifacts(self) -> None:
        mutations = {
            "group metric": (GROUPS.replace(",0.5,0.002,", ",0.4,0.002,"), READINGS),
            "reading path": (
                GROUPS,
                READINGS.replace("PRD measurments/PRD_01.mat", "PRD measurments/wrong.mat"),
            ),
            "reading digest": (GROUPS, READINGS.replace("c" * 64, "f" * 64, 1)),
            "reading value": (GROUPS, READINGS.replace(",2,5000,0.001,", ",3,5000,0.001,", 1)),
        }
        for label, (groups, readings) in mutations.items():
            with self.subTest(label=label), tempfile.TemporaryDirectory() as directory:
                completed, _ = self.run_generator(
                    pathlib.Path(directory), groups=groups, readings=readings
                )
                self.assertNotEqual(completed.returncode, 0, completed.stderr)

        inconsistent_count = result_fixture()
        inconsistent_count["groups"][0]["count"] = 3  # type: ignore[index]
        with tempfile.TemporaryDirectory() as directory:
            completed, _ = self.run_generator(
                pathlib.Path(directory),
                result=inconsistent_count,
                groups=GROUPS.replace("scene_01,2,", "scene_01,3,"),
            )
            self.assertNotEqual(completed.returncode, 0, completed.stderr)


if __name__ == "__main__":
    unittest.main()
