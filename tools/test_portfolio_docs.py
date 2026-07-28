#!/usr/bin/env python3
"""Focused tests for portfolio-language publication guards."""

from __future__ import annotations

import importlib.util
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


if __name__ == "__main__":
    unittest.main()
