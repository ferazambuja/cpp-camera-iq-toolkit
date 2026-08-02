#!/usr/bin/env python3
"""Pin cross-language result schema versions across producers and consumers.

`camera_iq shading` stamps `schema_version` from a C++ constant. The portfolio
exporter declares the version it accepts and *rejects every input that does not
match*, and its own test fixture declares that version a third time. Three
independent literals in two languages, agreeing by convention.

That convention has already failed once. Bumping the producer to 3 left the
exporter pinned at 2, so the documented regeneration pipeline rejected every
real result file while the test suite stayed green -- the fixture encoded the
old contract, so nothing in CI ever compared the two sides.

A static_assert cannot span the language boundary, so this does the comparison
instead: read all three declarations and require one value.

Usage: python3 tools/check_schema_contract.py [--repo-root PATH]
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path


# (path, human description, regex capturing the version)
SOURCES = [
    (
        Path("src/cmd_shading.cpp"),
        "producer constant",
        re.compile(r"constexpr int kShadingSchemaVersion\s*=\s*(\d+)\s*;"),
    ),
    (
        Path("tools/export_shading_portfolio.py"),
        "exporter accepted version",
        re.compile(r"^SCHEMA_VERSION\s*=\s*(\d+)\s*$", re.MULTILINE),
    ),
    (
        Path("tools/test_export_shading_portfolio.py"),
        "exporter test fixture",
        re.compile(r'"schema_version":\s*(\d+)'),
    ),
]

SPECTRO_SOURCES = [
    (
        Path("src/cmd_spectro_ingest.cpp"),
        "producer constant",
        re.compile(r"constexpr int kSpectroIngestSchemaVersion\s*=\s*(\d+)\s*;"),
    ),
    (
        Path("tools/generate_spectro_receipt.py"),
        "receipt generator accepted version",
        re.compile(r"^RESULT_SCHEMA_VERSION\s*=\s*(\d+)\s*$", re.MULTILINE),
    ),
    (
        Path("tools/check_spectro_receipt.py"),
        "receipt checker accepted version",
        re.compile(r"^RESULT_SCHEMA_VERSION\s*=\s*(\d+)\s*$", re.MULTILINE),
    ),
    (
        Path("tools/test_generate_spectro_receipt.py"),
        "receipt generator test fixture",
        re.compile(r"^RESULT_SCHEMA_VERSION\s*=\s*(\d+)\s*$", re.MULTILINE),
    ),
    (
        Path("tools/test_check_spectro_receipt.py"),
        "receipt checker test fixture",
        re.compile(r"^RESULT_SCHEMA_VERSION\s*=\s*(\d+)\s*$", re.MULTILINE),
    ),
]

CONTRACTS = {
    "shading": SOURCES,
    "spectro-ingest": SPECTRO_SOURCES,
}


def check(root: Path) -> list[str]:
    errors: list[str] = []
    for contract, sources in CONTRACTS.items():
        found: list[tuple[Path, str, int]] = []
        contract_errors: list[str] = []
        for rel, description, pattern in sources:
            path = root / rel
            if not path.is_file():
                contract_errors.append(f"{rel}: missing")
                continue
            matches = pattern.findall(path.read_text())
            if not matches:
                declaration = (
                    "SCHEMA_VERSION"
                    if "export_shading_portfolio" in rel.name
                    else "schema-version"
                )
                contract_errors.append(
                    f"{rel}: no {declaration} declaration found ({description})"
                )
                continue
            values = {int(match) for match in matches}
            if len(values) > 1:
                contract_errors.append(
                    f"{rel}: declares conflicting schema versions {sorted(values)}"
                )
                continue
            found.append((rel, description, values.pop()))
        errors.extend(contract_errors)
        if contract_errors:
            continue
        versions = {version for _, _, version in found}
        if len(versions) > 1:
            detail = ", ".join(
                f"{rel} ({description}) = {version}"
                for rel, description, version in found
            )
            errors.append(
                f"{contract} schema version disagrees across producer and "
                f"consumers: {detail}"
            )
    return errors


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo-root", default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()

    errors = check(Path(args.repo_root))
    for error in errors:
        print(error, file=sys.stderr)
    if errors:
        return 1
    # check() already proved this matches; re-derive it only for the message.
    summaries = []
    for contract, sources in CONTRACTS.items():
        match = sources[0][2].search(
            (Path(args.repo_root) / sources[0][0]).read_text()
        )
        summaries.append(f"{contract} v{match.group(1) if match else '?'}")
    print("schema contracts ok: " + ", ".join(summaries))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
