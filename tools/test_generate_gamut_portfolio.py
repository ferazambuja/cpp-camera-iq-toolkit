#!/usr/bin/env python3
"""Focused tests for the synthetic gamut-mapping portfolio generator."""

from __future__ import annotations

import csv
import importlib.util
import json
import tempfile
import unittest
from pathlib import Path


SCRIPT = Path(__file__).with_name("generate_gamut_portfolio.py")
SPEC = importlib.util.spec_from_file_location("generate_gamut_portfolio", SCRIPT)
assert SPEC and SPEC.loader
GAMUT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(GAMUT)


class GamutPortfolioTests(unittest.TestCase):
    def test_report_contract_keeps_ipt_hue_diagnostic(self) -> None:
        fields = [
            "input_IPT_hue_degrees",
            "output_IPT_hue_degrees",
            "delta_IPT_hue_degrees",
        ]
        row = GAMUT.sample_report("mapped", modified=True)
        for field in fields:
            self.assertIn(field, row)
        self.assertIn("IPT_hue_defined", row)
        self.assertIn("mapping_coordinate_space", row)
        self.assertIn("delta_e_ok", row)

    def test_synthetic_grid_is_complete_and_deterministic(self) -> None:
        rows = GAMUT.synthetic_rows()
        self.assertEqual(len(rows), 125)
        self.assertEqual(len({row[0] for row in rows}), 125)
        self.assertEqual(rows[0], ("p3_r000_g000_b000", 0.0, 0.0, 0.0))
        self.assertEqual(rows[-1], ("p3_r100_g100_b100", 1.0, 1.0, 1.0))

    def test_report_reader_rejects_duplicate_ids(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "report.csv"
            path.write_text(
                GAMUT.REPORT_HEADER + "\n" + GAMUT.sample_report_row("a") + "\n"
                + GAMUT.sample_report_row("a") + "\n",
                encoding="utf-8",
            )
            with self.assertRaises(ValueError):
                GAMUT.read_report(path)

    def test_report_reader_rejects_non_finite_metric(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "report.csv"
            values = next(csv.reader([GAMUT.sample_report_row("a")]))
            values[GAMUT.REPORT_FIELDS.index("delta_e_2000")] = "nan"
            row = ",".join(values)
            path.write_text(GAMUT.REPORT_HEADER + "\n" + row + "\n", encoding="utf-8")
            with self.assertRaises(ValueError):
                GAMUT.read_report(path)

    def test_svg_names_method_and_scope(self) -> None:
        rows = [GAMUT.sample_report("a"), GAMUT.sample_report("b", modified=True)]
        svg = GAMUT.render_figure(rows, rows, rows, rows)
        self.assertIn("fixed-L*, Lab-hue radial clipping", svg)
        self.assertIn("experimental protected-core compression", svg)
        self.assertIn("coordinates and algorithms change one at a time", svg)
        self.assertIn("OkLCh radial", svg)
        self.assertIn("CSS Local MINDE", svg)
        self.assertIn("isolates coordinates", svg)
        self.assertIn('role="img"', svg)
        self.assertIn('<title id="gamut-title">', svg)
        self.assertIn('<desc id="gamut-description">', svg)
        self.assertIn('class="series-radial"', svg)
        self.assertIn('class="series-soft"', svg)
        self.assertIn('stroke-dasharray="3 2"', svg)

    def test_artifact_comparison_tolerates_only_roundoff(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            expected = root / "expected.json"
            actual = root / "actual.json"
            expected.write_text(
                json.dumps({"value": 1.0, "branch": "identity"}),
                encoding="utf-8",
            )
            actual.write_text(
                json.dumps({"value": 1.0 + 5e-14, "branch": "identity"}),
                encoding="utf-8",
            )
            self.assertTrue(GAMUT.artifacts_equivalent(actual, expected))

            actual.write_text(
                json.dumps({"value": 1.0 + 5e-9, "branch": "identity"}),
                encoding="utf-8",
            )
            self.assertFalse(GAMUT.artifacts_equivalent(actual, expected))

            actual.write_text(
                json.dumps({"value": 1.0, "branch": "mapped"}),
                encoding="utf-8",
            )
            self.assertFalse(GAMUT.artifacts_equivalent(actual, expected))

            expected.write_text(
                json.dumps({"schema_version": 3, "iterations": 7}),
                encoding="utf-8",
            )
            actual.write_text(
                json.dumps({"schema_version": 3.0, "iterations": 7.0}),
                encoding="utf-8",
            )
            self.assertFalse(
                GAMUT.artifacts_equivalent(actual, expected),
                "integer-to-float schema drift must not pass freshness checks",
            )

            expected.write_text(
                json.dumps({"input_IPT_hue_degrees": 44.0}),
                encoding="utf-8",
            )
            actual.write_text(
                json.dumps({"input_IPT_hue_degrees": 44.0 + 8e-6}),
                encoding="utf-8",
            )
            self.assertTrue(
                GAMUT.artifacts_equivalent(actual, expected),
                "platform-level angular roundoff must not stale the study",
            )

            expected.write_text(
                json.dumps({"samples": [{"delta_e_ok": 0.0}]}),
                encoding="utf-8",
            )
            actual.write_text(
                json.dumps({"samples": [{"delta_e_ok": 0.001}]}),
                encoding="utf-8",
            )
            diagnostic = GAMUT.artifact_difference(actual, expected)
            self.assertIn("$.samples[0].delta_e_ok", diagnostic)
            self.assertIn("generated", diagnostic)
            self.assertIn("committed", diagnostic)

    def test_result_csv_comparison_checks_ids_and_numeric_tolerance(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            expected = root / "expected.csv"
            actual = root / "actual.csv"
            expected.write_text(
                GAMUT.REPORT_HEADER + "\n" + GAMUT.sample_report_row("a") + "\n",
                encoding="utf-8",
            )
            values = next(csv.reader([GAMUT.sample_report_row("a")]))
            metric_index = GAMUT.REPORT_FIELDS.index("delta_e_2000")
            values[metric_index] = "5e-14"
            actual.write_text(
                GAMUT.REPORT_HEADER + "\n" + ",".join(values) + "\n",
                encoding="utf-8",
            )
            self.assertTrue(GAMUT.artifacts_equivalent(actual, expected))

            values[metric_index] = "5e-9"
            actual.write_text(
                GAMUT.REPORT_HEADER + "\n" + ",".join(values) + "\n",
                encoding="utf-8",
            )
            self.assertFalse(GAMUT.artifacts_equivalent(actual, expected))

            values[metric_index] = "0.0"
            values[0] = "b"
            actual.write_text(
                GAMUT.REPORT_HEADER + "\n" + ",".join(values) + "\n",
                encoding="utf-8",
            )
            self.assertFalse(GAMUT.artifacts_equivalent(actual, expected))

    def test_json_and_csv_are_reconciled_per_sample_and_in_aggregate(self) -> None:
        repo_root = SCRIPT.parents[1]
        json_path = repo_root / "docs/data/gamut_synthetic_radial.json"
        csv_path = repo_root / "docs/data/gamut_synthetic_radial.csv"
        GAMUT.validate_report_pair(json_path, csv_path)

        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            mutated_json = root / "result.json"
            document = json.loads(json_path.read_text(encoding="utf-8"))
            document["samples"][0]["delta_e_2000"] = 0.25
            mutated_json.write_text(json.dumps(document), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "JSON and CSV sample"):
                GAMUT.validate_report_pair(mutated_json, csv_path)

            document = json.loads(json_path.read_text(encoding="utf-8"))
            document["aggregate"]["mean_delta_e_2000"] += 0.25
            mutated_json.write_text(json.dumps(document), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "aggregate"):
                GAMUT.validate_report_pair(mutated_json, csv_path)

    def test_json_csv_reconciliation_rejects_missing_sample(self) -> None:
        repo_root = SCRIPT.parents[1]
        json_path = repo_root / "docs/data/gamut_synthetic_radial.json"
        csv_path = repo_root / "docs/data/gamut_synthetic_radial.csv"
        with tempfile.TemporaryDirectory() as temp:
            mutated_json = Path(temp) / "result.json"
            document = json.loads(json_path.read_text(encoding="utf-8"))
            document["samples"].pop()
            document["aggregate"]["sample_count"] -= 1
            mutated_json.write_text(json.dumps(document), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "sample IDs"):
                GAMUT.validate_report_pair(mutated_json, csv_path)


if __name__ == "__main__":
    unittest.main()
