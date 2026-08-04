#!/usr/bin/env python3
"""Focused tests for the CAM16 equation-audit portfolio generator."""

from __future__ import annotations

import importlib.util
import json
import math
import tempfile
import unittest
from pathlib import Path


SCRIPT = Path(__file__).with_name("generate_cam16_equation_audit.py")
SPEC = importlib.util.spec_from_file_location("generate_cam16_equation_audit", SCRIPT)
assert SPEC and SPEC.loader
AUDIT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(AUDIT)


class Cam16EquationAuditGeneratorTests(unittest.TestCase):
    def test_artifact_comparison_tolerates_only_numeric_roundoff(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            expected_json = root / "expected.json"
            actual_json = root / "actual.json"
            expected_json.write_text(
                json.dumps({"schema_version": 2, "value": 1.0}),
                encoding="utf-8",
            )
            actual_json.write_text(
                json.dumps({"schema_version": 2, "value": 1.0 + 5e-13}),
                encoding="utf-8",
            )
            self.assertTrue(AUDIT.artifacts_equivalent(actual_json, expected_json))
            actual_json.write_text(
                json.dumps({"schema_version": 2, "value": 1.0 + 5e-8}),
                encoding="utf-8",
            )
            self.assertFalse(AUDIT.artifacts_equivalent(actual_json, expected_json))

            expected_csv = root / "expected.csv"
            actual_csv = root / "actual.csv"
            expected_csv.write_text(
                "series,x,reference_j,value,comparison_value\n"
                "normalized_brightness,5,,0.5,0.25\n",
                encoding="utf-8",
            )
            actual_csv.write_text(
                "series,x,reference_j,value,comparison_value\n"
                "normalized_brightness,5,,0.5000000000005,0.25\n",
                encoding="utf-8",
            )
            self.assertTrue(AUDIT.artifacts_equivalent(actual_csv, expected_csv))
            actual_csv.write_text(
                "series,x,reference_j,value,comparison_value\n"
                "isolated_ncb_factor,5,,0.5000000000005,0.25\n",
                encoding="utf-8",
            )
            self.assertFalse(AUDIT.artifacts_equivalent(actual_csv, expected_csv))

    def test_svg_states_scope_and_published_tradeoff(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            csv_path = root / "audit.csv"
            json_path = root / "audit.json"
            rows = ["series,x,reference_j,value,comparison_value"]
            brightness_points: list[dict[str, float]] = []
            for reference_j in range(0, 101, 5):
                cam16 = math.sqrt(reference_j / 100.0)
                proposed = reference_j / 100.0
                rows.append(
                    "normalized_brightness,"
                    f"{reference_j},,{cam16:.15g},{proposed:.15g}"
                )
                brightness_points.append(
                    {
                        "J": float(reference_j),
                        "cam16_Q_over_Q_white": cam16,
                        "hellwig_2022_Q_over_Q_white": proposed,
                    }
                )
            backgrounds = (20.0, 10.0, 5.0, 2.0, 1.0, 0.5, 0.2, 0.1)
            background_points: list[dict[str, float]] = []
            for y_background in backgrounds:
                value = (20.0 / y_background) ** 0.18
                rows.append(f"isolated_ncb_factor,{y_background:g},,{value:.15g},")
                background_points.append(
                    {"Y_b": y_background, "relative_factor": value}
                )
            coupled_points: list[dict[str, float]] = []
            background_increments = {
                20.0: 0.0,
                10.0: 0.1,
                5.0: 0.2,
                2.0: 0.4,
                1.0: 0.6,
                0.5: 0.8,
                0.2: 1.0,
                0.1: 1.2,
            }
            for y_background in backgrounds:
                for reference_j in range(10, 100, 10):
                    value = 1.0 + background_increments[y_background] * (
                        100.0 - reference_j
                    ) / 90.0
                    rows.append(
                        "cam16_chroma_expression,"
                        f"{y_background:g},{reference_j},{value:.12g},"
                    )
                    coupled_points.append(
                        {
                            "Y_b": y_background,
                            "reference_J": float(reference_j),
                            "relative_chroma": value,
                        }
                    )
            csv_path.write_text("\n".join(rows) + "\n", encoding="utf-8")
            document = {
                "schema_version": 2,
                "scope": "equation_level_not_perceptual_validation",
                "study": "Hellwig_Fairchild_2022_CAM16_equation_audit",
                "source_doi": "10.1002/col.22792",
                "equation_23_correction_date": "2022-04-22",
                "equation_23_coefficient": 43,
                "brightness": {
                    "contract": (
                        "normalized_Q_over_Q_white_under_fixed_viewing_conditions"
                    ),
                    "points": brightness_points,
                },
                "background_dependence": {
                    "contract": "isolated_Ncb_to_the_0_9_contribution",
                    "not_full_cam16_chroma": True,
                    "reference_Y_b": 20,
                    "points": background_points,
                },
                "complete_chroma_expression": {
                    "contract": "fixed_adapted_response_reference_J_sweep",
                    "not_full_cam16_forward": True,
                    "relative_Y_white": 100,
                    "reference_Y_b": 20,
                    "reference_J_min": 10,
                    "reference_J_max": 90,
                    "reference_J_step": 10,
                    "points": coupled_points,
                },
                "published_performance_R_squared": {
                    "brightness_CAM16": 0.86,
                    "brightness_proposed": 0.95,
                    "Munsell_chroma_CAM16": 0.87,
                    "Munsell_chroma_proposed": 0.96,
                    "LUTCHI_colorfulness_CAM16": 0.81,
                    "LUTCHI_colorfulness_proposed": 0.71,
                },
            }
            json_path.write_text(json.dumps(document), encoding="utf-8")
            svg = AUDIT.render_svg(csv_path, json_path)
        self.assertIn("CAM16 equation audit", svg)
        self.assertIn("isolated Ncb^0.9", svg)
        self.assertIn("Fixed-response CAM16 chroma (J₀ at Yb=20)", svg)
        self.assertIn("J₀=10–90", svg)
        self.assertNotIn(">J=10–90<", svg)
        self.assertIn("0.81 → 0.71", svg)
        self.assertIn("Equation behavior, not observer validation", svg)
        self.assertIn("Paper equations and literal regression tests", svg)
        self.assertIn("depends on Yb and reference J₀", svg)
        self.assertIn('role="img"', svg)
        self.assertIn('<title id="audit-title">', svg)
        self.assertIn('<desc id="audit-description">', svg)

    def test_reader_rejects_missing_render_metadata(self) -> None:
        repo_root = SCRIPT.parents[1]
        source_csv = repo_root / "docs/data/cam16_equation_audit.csv"
        source_json = repo_root / "docs/data/cam16_equation_audit.json"
        document = json.loads(source_json.read_text(encoding="utf-8"))
        document.pop("equation_23_coefficient")
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            csv_path = root / "audit.csv"
            json_path = root / "audit.json"
            csv_path.write_bytes(source_csv.read_bytes())
            json_path.write_text(json.dumps(document), encoding="utf-8")
            with self.assertRaises(ValueError):
                AUDIT.render_svg(csv_path, json_path)

    def test_reader_rejects_out_of_range_published_performance(self) -> None:
        repo_root = SCRIPT.parents[1]
        source_csv = repo_root / "docs/data/cam16_equation_audit.csv"
        source_json = repo_root / "docs/data/cam16_equation_audit.json"
        document = json.loads(source_json.read_text(encoding="utf-8"))
        document["published_performance_R_squared"][
            "Munsell_chroma_proposed"
        ] = 1.01
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            csv_path = root / "audit.csv"
            json_path = root / "audit.json"
            csv_path.write_bytes(source_csv.read_bytes())
            json_path.write_text(json.dumps(document), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "R-squared outside"):
                AUDIT.render_svg(csv_path, json_path)

    def test_reader_rejects_json_csv_curve_disagreement(self) -> None:
        repo_root = SCRIPT.parents[1]
        source_csv = repo_root / "docs/data/cam16_equation_audit.csv"
        source_json = repo_root / "docs/data/cam16_equation_audit.json"
        document = json.loads(source_json.read_text(encoding="utf-8"))
        document["complete_chroma_expression"]["points"][0][
            "relative_chroma"
        ] = 1.25
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            csv_path = root / "audit.csv"
            json_path = root / "audit.json"
            csv_path.write_bytes(source_csv.read_bytes())
            json_path.write_text(json.dumps(document), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "JSON and CSV curves differ"):
                AUDIT.render_svg(csv_path, json_path)

    def test_reader_rejects_original_json_csv_curve_disagreement(self) -> None:
        repo_root = SCRIPT.parents[1]
        source_csv = repo_root / "docs/data/cam16_equation_audit.csv"
        source_json = repo_root / "docs/data/cam16_equation_audit.json"
        mutations = (
            ("brightness", "cam16_Q_over_Q_white"),
            ("background_dependence", "relative_factor"),
        )
        for section, field in mutations:
            with self.subTest(section=section):
                document = json.loads(source_json.read_text(encoding="utf-8"))
                document[section]["points"][0][field] = 1.25
                with tempfile.TemporaryDirectory() as temp:
                    root = Path(temp)
                    csv_path = root / "audit.csv"
                    json_path = root / "audit.json"
                    csv_path.write_bytes(source_csv.read_bytes())
                    json_path.write_text(json.dumps(document), encoding="utf-8")
                    with self.assertRaisesRegex(
                        ValueError, "JSON and CSV curves differ"
                    ):
                        AUDIT.render_svg(csv_path, json_path)

    def test_reader_rejects_isolated_factor_comparison_value(self) -> None:
        repo_root = SCRIPT.parents[1]
        source_csv = repo_root / "docs/data/cam16_equation_audit.csv"
        source_json = repo_root / "docs/data/cam16_equation_audit.json"
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            csv_path = root / "audit.csv"
            json_path = root / "audit.json"
            rows = source_csv.read_text(encoding="utf-8").splitlines()
            csv_path.write_text(
                "\n".join(
                    row + "1" if row.startswith("isolated_ncb_factor,20,") else row
                    for row in rows
                )
                + "\n",
                encoding="utf-8",
            )
            json_path.write_bytes(source_json.read_bytes())
            with self.assertRaisesRegex(ValueError, "isolated-factor row"):
                AUDIT.render_svg(csv_path, json_path)

    def test_reader_rejects_duplicate_coupled_point(self) -> None:
        repo_root = SCRIPT.parents[1]
        source_csv = repo_root / "docs/data/cam16_equation_audit.csv"
        source_json = repo_root / "docs/data/cam16_equation_audit.json"
        document = json.loads(source_json.read_text(encoding="utf-8"))
        document["complete_chroma_expression"]["points"].append(
            dict(document["complete_chroma_expression"]["points"][0])
        )
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            csv_path = root / "audit.csv"
            json_path = root / "audit.json"
            csv_text = source_csv.read_text(encoding="utf-8")
            duplicate = next(
                row
                for row in csv_text.splitlines()
                if row.startswith("cam16_chroma_expression,")
            )
            csv_path.write_text(csv_text + duplicate + "\n", encoding="utf-8")
            json_path.write_text(json.dumps(document), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "duplicate or extra points"):
                AUDIT.render_svg(csv_path, json_path)

    def test_reader_rejects_missing_brightness_endpoint(self) -> None:
        repo_root = SCRIPT.parents[1]
        source_csv = repo_root / "docs/data/cam16_equation_audit.csv"
        source_json = repo_root / "docs/data/cam16_equation_audit.json"
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            csv_path = root / "audit.csv"
            json_path = root / "audit.json"
            lines = source_csv.read_text(encoding="utf-8").splitlines()
            csv_path.write_text(
                "\n".join(
                    line
                    for line in lines
                    if not line.startswith("normalized_brightness,100,")
                )
                + "\n",
                encoding="utf-8",
            )
            json_path.write_bytes(source_json.read_bytes())
            with self.assertRaisesRegex(ValueError, "brightness curve must span"):
                AUDIT.render_svg(csv_path, json_path)


if __name__ == "__main__":
    unittest.main()
