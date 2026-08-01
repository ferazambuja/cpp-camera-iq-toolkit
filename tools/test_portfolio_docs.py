#!/usr/bin/env python3
"""Focused tests for project-documentation language guards."""

from __future__ import annotations

import importlib.util
import subprocess
import tempfile
import unittest
from pathlib import Path


SCRIPT = Path(__file__).with_name("check_portfolio_docs.py")
SPEC = importlib.util.spec_from_file_location("check_portfolio_docs", SCRIPT)
assert SPEC and SPEC.loader
DOCS = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(DOCS)


def matched(text: str) -> bool:
    return any(
        pattern.search(text)
        for pattern in {**DOCS.STALE_PATTERNS, **DOCS.INTERNAL_PATTERNS}.values()
    )


class PublicationLanguageTests(unittest.TestCase):
    def test_public_markdown_includes_untracked_public_plan(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            repo = Path(temp)
            subprocess.run(
                ["git", "init", "--quiet"], cwd=repo, check=True
            )
            (repo / "README.md").write_text("# Project\n", encoding="utf-8")
            plan = repo / "docs" / "plans" / "PLAN.md"
            plan.parent.mkdir(parents=True)
            plan.write_text("# Public implementation record\n", encoding="utf-8")

            self.assertIn(plan, DOCS.public_markdown(repo))

    def test_rejects_completed_slice_and_staging_language(self) -> None:
        phrases = [
            "The first implementation slice should use the D810 sweep.",
            "The second implementation slice adds RAW extraction.",
            "A later development slice can remove the dependency.",
            "Recommended local cache id when a slice stages files:",
            "Copy each subset only when its slice runs; do not bulk-copy.",
            "The first public slices used this dark-frame set.",
            "Downstream closure and quality slices consume the CSV.",
            "This limitation is carried forward to a later phase.",
            "## Not Claimed",
            "## Not claimed",
        ]
        for phrase in phrases:
            with self.subTest(phrase=phrase):
                self.assertTrue(matched(phrase))

    def test_allows_scientific_scope_language(self) -> None:
        phrases = [
            "The validation threshold is 0.999 correlation.",
            "The 24-patch slice of the reflectance matrix is analyzed.",
            "Copy configs/datasets.example.json to configure local roots.",
        ]
        for phrase in phrases:
            with self.subTest(phrase=phrase):
                self.assertFalse(matched(phrase))

    def test_rejects_audience_strategy_and_self_promotional_language(self) -> None:
        phrases = [
            "**Hiring-manager summary:** The D810 showed a strong f/5.6 peak.",
            "**Recruiter — 30 to 60 seconds:** start with the overview.",
            "**Hiring manager — about five minutes:** inspect the reports.",
            "This page routes three kinds of readers through the same evidence.",
            "For a five-minute technical tour, start with the portfolio index.",
            "Portfolio audit: 2026-07-28",
            "## Executive Verdict",
            "## Public Summary",
            "Defensible summary: the archive supports this comparison.",
            "## Bottom Line",
            "Return to the portfolio landing page.",
            "# Camera IQ portfolio and report index",
            "## Public evidence model",
            "Rebuild the portfolio plots.",
            "This is a research and portfolio toolkit.",
            "Honest scope: the workbook is a compatible reference.",
            "JSON honesty contract: DN units and non-support flags.",
            "This is a claim-scoped green-linear measurement.",
            "Slanted-edge SFR/MTF is not globally data-blocked.",
            "## Hazards (do not trip on these)",
            "## Verified this session (machine-precision)",
            "**Caveat preserved.** The reference is not per-unit.",
            "**Do not claim** exact measured-reference Delta E.",
            "The current honest claim is bounded.",
            "## Available but unused data (cataloged so it is not ignored)",
            "## Authority rule",
            "CC-18 is the more discriminating and more honest number.",
            "The gap requires calibration rather than unimplemented parser loops.",
            "This is the highest scientific gap.",
            "Automatic localization is useful engineering polish.",
            "This is less scientifically important than the seeded workflow.",
            "These are small provenance-strengthening tasks.",
            "Rendered-luma parity is lower priority.",
            "Complete the manifest before any analysis claims.",
            "No color-accuracy numbers are claimed.",
            "Every claim is tied to a reproducible command.",
            "No claim that the original outputs are correct.",
            "Feasibility is promising, pending follow-up checks.",
            "The earlier blocked conclusion was too broad.",
            "Regenerate artifacts before claiming the table is fully migrated.",
            "The projective grid is not yet approved.",
            "## Implemented and optional extensions",
            "[Finished SFR/MTF report](SFR_MTF.md)",
            "Finished D800/D810 center, aperture, and field analysis.",
        ]
        for phrase in phrases:
            with self.subTest(phrase=phrase):
                self.assertTrue(matched(phrase))

    def test_allows_project_centered_overview_language(self) -> None:
        phrases = [
            "## Overview",
            "The D810 showed a strong f/5.6 peak.",
            "For the core technical path, start with the documentation index.",
            "## Coverage summary",
            "The archive supports this bounded comparison.",
        ]
        for phrase in phrases:
            with self.subTest(phrase=phrase):
                self.assertFalse(matched(phrase))


if __name__ == "__main__":
    unittest.main()
