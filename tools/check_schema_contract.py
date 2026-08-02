#!/usr/bin/env python3
"""Pin the shading schema version across the C++ producer and its Python consumers.

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


def check(root: Path) -> list[str]:
    errors: list[str] = []
    found: list[tuple[Path, str, int]] = []

    for rel, description, pattern in SOURCES:
        path = root / rel
        if not path.is_file():
            errors.append(f"{rel}: missing")
            continue
        matches = pattern.findall(path.read_text())
        if not matches:
            name = pattern.pattern.split("\\")[0].strip("^\"' ")
            errors.append(
                f"{rel}: no {'SCHEMA_VERSION' if 'export_shading_portfolio' in rel.name else name} "
                f"declaration found ({description})"
            )
            continue
        # A file may state the version more than once; they must agree with
        # each other before they can agree with anything else.
        values = {int(m) for m in matches}
        if len(values) > 1:
            errors.append(
                f"{rel}: declares conflicting schema versions {sorted(values)}"
            )
            continue
        found.append((rel, description, values.pop()))

    if errors:
        return errors

    versions = {v for _, _, v in found}
    if len(versions) > 1:
        detail = ", ".join(f"{rel} ({desc}) = {v}" for rel, desc, v in found)
        errors.append(
            "shading schema version disagrees across producer and consumers: "
            + detail
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
    match = SOURCES[0][2].search((Path(args.repo_root) / SOURCES[0][0]).read_text())
    version = match.group(1) if match else "?"
    print(f"shading schema contract ok: producer and consumers all at v{version}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
