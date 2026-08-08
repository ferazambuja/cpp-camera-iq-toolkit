#!/usr/bin/env python3
"""Focused tests for portfolio figure inputs and presentation choices."""

from __future__ import annotations

import importlib.util
import csv
import shutil
import tempfile
import unittest
from pathlib import Path


SCRIPT = Path(__file__).with_name("generate_portfolio_figures.py")
SPEC = importlib.util.spec_from_file_location("generate_portfolio_figures", SCRIPT)
assert SPEC and SPEC.loader
FIGURES = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(FIGURES)
SOURCE_DATA = SCRIPT.parents[1] / "docs" / "data"


class PortfolioFigureTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory()
        self.data = Path(self.temp.name)
        for source in SOURCE_DATA.glob("*.csv"):
            shutil.copy2(source, self.data / source.name)

    def tearDown(self) -> None:
        self.temp.cleanup()

    def test_sfr_rejects_duplicate_camera_aperture_key(self) -> None:
        path = self.data / "sfr_aperture_summary.csv"
        lines = path.read_text(encoding="utf-8").splitlines()
        lines[-1] = lines[-2]
        path.write_text("\n".join(lines) + "\n", encoding="utf-8")
        with self.assertRaises(ValueError):
            FIGURES.generate_sfr(self.data)

    def test_sfr_rejects_center_outside_displayed_axis(self) -> None:
        path = self.data / "sfr_aperture_summary.csv"
        with path.open(newline="", encoding="utf-8") as handle:
            rows = list(csv.DictReader(handle))
            fieldnames = list(rows[0])
        rows[0]["toolkit_center_mtf50"] = "0.30000001"
        with path.open("w", newline="", encoding="utf-8") as handle:
            writer = csv.DictWriter(handle, fieldnames=fieldnames, lineterminator="\n")
            writer.writeheader()
            writer.writerows(rows)
        with self.assertRaises(ValueError):
            FIGURES.generate_sfr(self.data)

    def test_sfr_rejects_margin_outside_displayed_axis(self) -> None:
        path = self.data / "sfr_aperture_summary.csv"
        with path.open(newline="", encoding="utf-8") as handle:
            rows = list(csv.DictReader(handle))
            fieldnames = list(rows[0])
        row = next(
            row
            for row in rows
            if row["camera"] == "D810" and row["aperture"] == "4"
        )
        row["toolkit_corner_max_mtf50"] = "0.30000000"
        with path.open("w", newline="", encoding="utf-8") as handle:
            writer = csv.DictWriter(handle, fieldnames=fieldnames, lineterminator="\n")
            writer.writeheader()
            writer.writerows(rows)
        with self.assertRaises(ValueError):
            FIGURES.generate_sfr(self.data)

    def test_current_tables_all_generate(self) -> None:
        self.assertIn("<svg", FIGURES.generate_sfr(self.data))
        self.assertIn("<svg", FIGURES.generate_spectral(self.data))
        self.assertIn("<svg", FIGURES.generate_ccm(self.data))
        self.assertIn("<svg", FIGURES.generate_shading(self.data))

    def test_shading_rejects_changed_acceptance_count(self) -> None:
        path = self.data / "flat_field_summary.csv"
        text = path.read_text(encoding="utf-8").replace(",true,,", ",false,,", 1)
        path.write_text(text, encoding="utf-8")
        with self.assertRaises(ValueError):
            FIGURES.generate_shading(self.data)

    def test_shading_rejects_duplicate_bin(self) -> None:
        path = self.data / "flat_field_response.csv"
        lines = path.read_text(encoding="utf-8").splitlines()
        lines[-1] = lines[-2]
        path.write_text("\n".join(lines) + "\n", encoding="utf-8")
        with self.assertRaises(ValueError):
            FIGURES.generate_shading(self.data)

    def test_shading_rejects_mixed_policy(self) -> None:
        path = self.data / "flat_field_summary.csv"
        text = path.read_text(encoding="utf-8").replace(
            "shading-v2-grid16x12-screening-coverage", "different-policy", 1
        )
        path.write_text(text, encoding="utf-8")
        with self.assertRaises(ValueError):
            FIGURES.generate_shading(self.data)

    def test_shading_rejects_screening_coverage_below_policy(self) -> None:
        path = self.data / "flat_field_summary.csv"
        with path.open(newline="", encoding="utf-8") as handle:
            rows = list(csv.DictReader(handle))
            fieldnames = list(rows[0])
        rows[0]["finite_fraction_gate_r"] = "0.89999999"
        with path.open("w", newline="", encoding="utf-8") as handle:
            writer = csv.DictWriter(handle, fieldnames=fieldnames, lineterminator="\n")
            writer.writeheader()
            writer.writerows(rows)
        with self.assertRaises(ValueError):
            FIGURES.generate_shading(self.data)

    def test_shading_rejects_changed_aperture_census(self) -> None:
        path = self.data / "flat_field_summary.csv"
        text = path.read_text(encoding="utf-8").replace(",9.0,", ",8.0,", 1)
        path.write_text(text, encoding="utf-8")
        with self.assertRaises(ValueError):
            FIGURES.generate_shading(self.data)

    def test_spectral_rejects_non_finite_score(self) -> None:
        path = self.data / "spectral_color_fidelity.csv"
        text = path.read_text(encoding="utf-8").replace("90.7", "nan", 1)
        path.write_text(text, encoding="utf-8")
        with self.assertRaises(ValueError):
            FIGURES.generate_spectral(self.data)

    def test_spectral_rejects_score_outside_displayed_axis(self) -> None:
        path = self.data / "spectral_color_fidelity.csv"
        text = path.read_text(encoding="utf-8").replace("90.7", "85.9", 1)
        path.write_text(text, encoding="utf-8")
        with self.assertRaises(ValueError):
            FIGURES.generate_spectral(self.data)

    def test_ccm_rejects_score_outside_displayed_axis(self) -> None:
        path = self.data / "ccm_validation_summary.csv"
        text = path.read_text(encoding="utf-8").replace("7.952", "9.001", 1)
        path.write_text(text, encoding="utf-8")
        with self.assertRaises(ValueError):
            FIGURES.generate_ccm(self.data)

    def test_spectral_uses_markers_for_truncated_axis(self) -> None:
        svg = FIGURES.generate_spectral(self.data)
        self.assertIn('class="score-marker"', svg)

    def test_architecture_wraps_measurement_detail(self) -> None:
        svg = FIGURES.generate_architecture()
        self.assertNotIn(
            "patches · CCM · spectral · OECF · noise · SFR",
            svg,
        )


if __name__ == "__main__":
    unittest.main()
