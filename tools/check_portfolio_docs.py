#!/usr/bin/env python3
"""Validate the public Markdown graph and portfolio publication invariants."""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path
from urllib.parse import unquote


LINK_RE = re.compile(r"!?\[[^\]]*\]\(([^)]+)\)")
STALE_PATTERNS = {
    "obsolete CTest count": re.compile(r"\b16/16 CTest tests\b"),
    "implemented work labeled Next": re.compile(r"^## Next\b", re.MULTILINE),
    "implementation-slice lifecycle language": re.compile(
        r"(?:"
        r"^#{1,6} .*\bSlice\b|"
        r"\b(?:first|second|later|next|development|implementation|"
        r"follow-on|physical-closure|patch-statistics)\s+"
        r"(?:implementation\s+)?slice\b|"
        r"\bwhen (?:a|its) slice\b|\beach slice\b"
        r")",
        re.IGNORECASE | re.MULTILINE,
    ),
    "plural slice lifecycle language": re.compile(
        r"\b(?:first|second|downstream|public)\b[^\n.]{0,80}\bslices\b",
        re.IGNORECASE,
    ),
    "future-phase lifecycle language": re.compile(
        r"\bcarried forward\b|\blater phase\b",
        re.IGNORECASE,
    ),
    "compliance-style nonclaim heading": re.compile(
        r"^## Not Claimed\s*$",
        re.IGNORECASE | re.MULTILINE,
    ),
    "local staging instruction": re.compile(
        r"\blocal cache id when a slice stages files\b|"
        r"\bcopy each .* only when (?:its|the) slice runs\b|"
        r"\bdo not bulk-copy\b",
        re.IGNORECASE,
    ),
    "internal R0 milestone": re.compile(r"\bR0\b"),
    "internal exit criterion": re.compile(r"exit criterion", re.IGNORECASE),
    "completed-item ledger": re.compile(r"\[DONE\b", re.IGNORECASE),
    "internal lifecycle phrase": re.compile(r"\bthis slice\b", re.IGNORECASE),
}
INTERNAL_PATTERNS = {
    "AI-assistance disclosure": re.compile(r"\bAI[- ]assistance\b", re.IGNORECASE),
    "learner state": re.compile(r"\bWAITING_OWNER\b|\blearner state\b", re.IGNORECASE),
    "internal evidence receipt": re.compile(r"\bevidence receipt\b", re.IGNORECASE),
    "internal DF identifier": re.compile(r"\bDF-\d+\b"),
}


def tracked_markdown(repo_root: Path) -> list[Path]:
    result = subprocess.run(
        ["git", "ls-files", "-z", "*.md"],
        cwd=repo_root,
        check=True,
        capture_output=True,
    )
    return [
        repo_root / entry.decode("utf-8")
        for entry in result.stdout.split(b"\0")
        if entry
    ]


def link_target(raw: str) -> str:
    target = raw.strip()
    if target.startswith("<") and ">" in target:
        return target[1 : target.index(">")]
    if " " in target:
        target = target.split(" ", 1)[0]
    return target


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo-root", type=Path, required=True)
    args = parser.parse_args()
    repo_root = args.repo_root.resolve()
    failures: list[str] = []

    required = [
        repo_root / "docs" / "README.md",
        repo_root / "docs" / "case-studies" / "sfr-mtf-aperture-field.md",
        repo_root / "docs" / "case-studies" / "spectral-color-fidelity.md",
        repo_root / "docs" / "case-studies" / "colorchecker-ccm.md",
    ]
    for path in required:
        if not path.is_file():
            failures.append(f"missing portfolio document: {path.relative_to(repo_root)}")

    markdown_files = tracked_markdown(repo_root)
    for path in markdown_files:
        text = path.read_text(encoding="utf-8")
        for match in LINK_RE.finditer(text):
            target = link_target(match.group(1))
            if (
                not target
                or target.startswith(("#", "http://", "https://", "mailto:"))
                or "://" in target
            ):
                continue
            relative = unquote(target.split("#", 1)[0])
            resolved = (path.parent / relative).resolve()
            if not resolved.exists():
                failures.append(
                    f"broken internal link: {path.relative_to(repo_root)} -> {target}"
                )
        if path == repo_root / "README.md" or repo_root / "docs" in path.parents:
            for label, pattern in {**STALE_PATTERNS, **INTERNAL_PATTERNS}.items():
                if pattern.search(text):
                    failures.append(f"{label}: {path.relative_to(repo_root)}")

    index_path = repo_root / "docs" / "README.md"
    if index_path.is_file():
        index_text = index_path.read_text(encoding="utf-8")
        for report in sorted((repo_root / "docs" / "reports").glob("*.md")):
            expected = f"reports/{report.name}"
            if expected not in index_text:
                failures.append(f"report missing from docs index: {report.name}")

    readme = (repo_root / "README.md").read_text(encoding="utf-8")
    if "https://github.com/ferazambuja" not in readme:
        failures.append("README is missing the GitHub profile link")

    if failures:
        print("portfolio documentation validation failed:", file=sys.stderr)
        for failure in failures:
            print(f"  {failure}", file=sys.stderr)
        return 1
    print(
        f"portfolio docs valid: {len(markdown_files)} Markdown files, "
        f"{len(list((repo_root / 'docs' / 'reports').glob('*.md')))} reports indexed"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
