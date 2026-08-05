#!/usr/bin/env python3
"""Behavior tests for the documentation-evidence test supervisor."""

from __future__ import annotations

import contextlib
import io
import tempfile
import unittest
from pathlib import Path
import sys

import run_doc_evidence_test as SUPERVISOR


HELPER = """\
from pathlib import Path
import sys

mode = sys.argv[1]
arguments = dict(zip(sys.argv[2::2], sys.argv[3::2]))
receipt = Path(arguments["--camera-iq-doc-evidence-receipt"])
nonce = arguments["--camera-iq-doc-evidence-nonce"]
expectations = arguments["--camera-iq-doc-evidence-expect"]

if mode == "success":
    receipt.write_text(nonce + "\\n" + expectations + "\\n", encoding="utf-8")
elif mode == "wrong-nonce":
    receipt.write_text(
        "not-the-issued-nonce\\n" + expectations + "\\n", encoding="utf-8"
    )
elif mode == "wrong-expectations":
    receipt.write_text(nonce + "\\nother=2\\n", encoding="utf-8")
elif mode == "duplicate":
    receipt.write_text(
        nonce + "\\n" + expectations + "\\n" + nonce + "\\n",
        encoding="utf-8",
    )
elif mode == "child-failure":
    raise SystemExit(7)
elif mode != "missing-receipt":
    raise SystemExit(9)
"""


class DocumentationEvidenceSupervisorTests(unittest.TestCase):
    def run_helper(self, mode: str) -> tuple[int, str]:
        with tempfile.TemporaryDirectory() as temp:
            helper = Path(temp) / "helper.py"
            helper.write_text(HELPER, encoding="utf-8")
            diagnostics = io.StringIO()
            with contextlib.redirect_stderr(diagnostics):
                result = SUPERVISOR.run_evidence_test(
                    [sys.executable, str(helper), mode], "example=1"
                )
            return result, diagnostics.getvalue()

    def test_accepts_zero_exit_with_exact_fresh_completion_receipt(self) -> None:
        self.assertEqual((0, ""), self.run_helper("success"))

    def test_rejects_zero_exit_without_completion_receipt(self) -> None:
        result, diagnostics = self.run_helper("missing-receipt")
        self.assertEqual(1, result)
        self.assertIn("did not leave a readable completion receipt", diagnostics)

    def test_rejects_mismatched_completion_receipt(self) -> None:
        for mode in ("wrong-nonce", "wrong-expectations", "duplicate"):
            with self.subTest(mode=mode):
                result, diagnostics = self.run_helper(mode)
                self.assertEqual(1, result)
                self.assertIn(
                    "does not match the issued nonce and expectations",
                    diagnostics,
                )

    def test_preserves_child_failure(self) -> None:
        result, diagnostics = self.run_helper("child-failure")
        self.assertEqual(1, result)
        self.assertIn("child test failed with status 7", diagnostics)


if __name__ == "__main__":
    unittest.main()
