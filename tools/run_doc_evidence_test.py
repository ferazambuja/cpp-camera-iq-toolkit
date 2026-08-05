#!/usr/bin/env python3
"""Supervise one runtime documentation-evidence test.

The child test is successful only when it exits with status zero and leaves the
fresh, nonce-bound receipt that the C++ harness writes after evidence-count
verification. This prevents a dependency or initializer from making the CTest
pass by terminating the child successfully before the harness verifies it.
"""

from __future__ import annotations

import argparse
import secrets
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Sequence


def run_evidence_test(command: Sequence[str], expectations: str) -> int:
    if not command or not expectations:
        print(
            "[fail] documentation-evidence supervisor: empty command or "
            "expectation",
            file=sys.stderr,
        )
        return 1

    nonce = secrets.token_hex(32)
    with tempfile.TemporaryDirectory(prefix="camera-iq-doc-evidence-") as temp:
        receipt = Path(temp) / "verified.receipt"
        child = subprocess.run(
            [
                *command,
                "--camera-iq-doc-evidence-expect",
                expectations,
                "--camera-iq-doc-evidence-receipt",
                str(receipt),
                "--camera-iq-doc-evidence-nonce",
                nonce,
            ],
            check=False,
        )
        if child.returncode != 0:
            print(
                "[fail] documentation-evidence supervisor: child test failed "
                f"with status {child.returncode}",
                file=sys.stderr,
            )
            return 1
        try:
            observed = receipt.read_text(encoding="utf-8")
        except OSError as error:
            print(
                "[fail] documentation-evidence supervisor: successful child "
                f"did not leave a readable completion receipt: {error}",
                file=sys.stderr,
            )
            return 1
        expected = nonce + "\n" + expectations + "\n"
        if observed != expected:
            print(
                "[fail] documentation-evidence supervisor: completion receipt "
                "does not match the issued nonce and expectations",
                file=sys.stderr,
            )
            return 1
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    parser.add_argument("--expectations", required=True)
    args = parser.parse_args()
    return run_evidence_test([args.binary], args.expectations)


if __name__ == "__main__":
    raise SystemExit(main())
