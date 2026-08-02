#!/usr/bin/env python3
"""Validate the public Markdown graph and documentation language rules."""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path
from urllib.parse import unquote


LINK_RE = re.compile(r"!?\[[^\]]*\]\(([^)]+)\)")
REQUIRED_PROJECT_DOCUMENTS = (
    Path("docs/README.md"),
    Path("docs/case-studies/sfr-mtf-aperture-field.md"),
    Path("docs/case-studies/spectral-color-fidelity.md"),
    Path("docs/case-studies/colorchecker-ccm.md"),
    Path("docs/case-studies/cfa-flat-field-response.md"),
    Path("docs/case-studies/spectroradiometer-ingest.md"),
)
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
    "audience-targeting language": re.compile(
        r"\bhiring[- ]manager\b|\brecruiter\b|"
        r"\broutes three kinds of readers\b|"
        r"\bfive-minute technical tour\b",
        re.IGNORECASE,
    ),
    "self-promotional portfolio language": re.compile(
        r"\bportfolio audit\b|\bportfolio landing page\b|"
        r"\bportfolio and report index\b|\bportfolio plots\b|"
        r"\bresearch and portfolio toolkit\b|"
        r"^## Public evidence model\s*$",
        re.IGNORECASE | re.MULTILINE,
    ),
    "persuasion-oriented summary language": re.compile(
        r"^## (?:Executive Verdict|Public Summary|Bottom Line)\s*$|"
        r"\bDefensible summary:",
        re.IGNORECASE | re.MULTILINE,
    ),
    "self-conscious claim language": re.compile(
        r"\bhonest scope\b|\bhonesty contract\b|\bclaim-scoped\b|"
        r"\bnot globally data-blocked\b|\bmore honest number\b|"
        r"^#{1,6} (?:"
        r"Hazards \(do not trip on these\)|"
        r"Verified this session(?: \(machine-precision\))?|"
        r"Available but unused data \(cataloged so it is not [\"“]?ignored[\"”]?\)|"
        r"Authority rule"
        r")\s*$|"
        r"^\*\*Caveat preserved\.\*\*|"
        r"^\*\*Do not claim\*\*|"
        r"\bcurrent honest claim\b",
        re.IGNORECASE | re.MULTILINE,
    ),
    "internal prioritization or claim lifecycle": re.compile(
        r"\brather than unimplemented parser loops\b|"
        r"\bhighest scientific gap\b|\buseful engineering polish\b|"
        r"\bless scientifically important\b|"
        r"\bsmall provenance-strengthening tasks\b|\blower priority\b|"
        r"\bbefore any analysis claims\b|"
        r"\bNo [^\n.]{0,80} numbers are claimed\b|"
        r"\bEvery claim\b|\bNo claim\b|"
        r"\bpending follow-up checks\b|"
        r"\bearlier blocked conclusion\b|"
        r"\bbefore claiming [^\n.]{0,80} fully migrated\b|"
        r"\bnot yet approved\b|"
        r"^## Implemented and optional extensions\s*$|"
        r"\[Finished SFR/MTF report\]|"
        r"\bFinished D800/D810 center, aperture, and field analysis\b",
        re.IGNORECASE | re.MULTILINE,
    ),
}
INTERNAL_PATTERNS = {
    "AI-assistance disclosure": re.compile(r"\bAI[- ]assistance\b", re.IGNORECASE),
    "learner state": re.compile(r"\bWAITING_OWNER\b|\blearner state\b", re.IGNORECASE),
    "internal evidence receipt": re.compile(r"\bevidence receipt\b", re.IGNORECASE),
    "internal DF identifier": re.compile(r"\bDF-\d+\b"),
}


def public_markdown(repo_root: Path) -> list[Path]:
    result = subprocess.run(
        ["git", "ls-files", "-z", "*.md"],
        cwd=repo_root,
        check=True,
        capture_output=True,
    )
    paths = {
        repo_root / entry.decode("utf-8")
        for entry in result.stdout.split(b"\0")
        if entry
    }
    # New public documentation must pass before it is staged; relying only on
    # git ls-files creates a blind spot exactly when a report is introduced.
    paths.add(repo_root / "README.md")
    paths.update((repo_root / "docs").rglob("*.md"))
    return sorted(paths)


def link_target(raw: str) -> str:
    target = raw.strip()
    if target.startswith("<") and ">" in target:
        return target[1 : target.index(">")]
    if " " in target:
        target = target.split(" ", 1)[0]
    return target


HEADING_RE = re.compile(r"^(#{1,6})\s+(.*?)\s*$")


def heading_slug(line: str) -> str | None:
    """GitHub's anchor for a Markdown heading, or None if the line is not one.

    Lowercase, punctuation dropped rather than replaced, spaces to hyphens.
    """
    match = HEADING_RE.match(line)
    if not match:
        return None
    text = match.group(2)
    text = re.sub(r"`([^`]*)`", r"\1", text)          # inline code markers
    text = re.sub(r"!?\[([^\]]*)\]\([^)]*\)", r"\1", text)  # links keep their label
    text = text.lower()
    text = re.sub(r"[^\w\s-]", "", text)
    return re.sub(r"\s+", "-", text.strip())


def document_anchors(text: str) -> set[str]:
    anchors = set()
    fenced = False
    for line in text.splitlines():
        if line.lstrip().startswith("```"):
            fenced = not fenced
            continue
        if fenced:
            continue
        slug = heading_slug(line)
        if slug:
            anchors.add(slug)
    return anchors


def anchor_failures(repo_root: Path, path: Path) -> list[str]:
    """Reports `#fragment` link targets that no heading in the target file
    defines. Checking the file path alone lets a heading rename break every
    cross-reference to it without any check noticing."""
    failures: list[str] = []
    text = path.read_text(encoding="utf-8")
    for match in LINK_RE.finditer(text):
        target = link_target(match.group(1))
        if not target or target.startswith(("http://", "https://", "mailto:")):
            continue
        if "://" in target or "#" not in target:
            continue
        file_part, _, fragment = target.partition("#")
        if not fragment:
            continue
        if file_part:
            resolved = (path.parent / unquote(file_part)).resolve()
        else:
            resolved = path
        if resolved.suffix != ".md" or not resolved.is_file():
            continue
        anchors = document_anchors(resolved.read_text(encoding="utf-8"))
        if unquote(fragment) not in anchors:
            failures.append(
                f"broken link anchor: {path.relative_to(repo_root)} -> {target}"
            )
    return failures


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo-root", type=Path, required=True)
    args = parser.parse_args()
    repo_root = args.repo_root.resolve()
    failures: list[str] = []

    for relative in REQUIRED_PROJECT_DOCUMENTS:
        path = repo_root / relative
        if not path.is_file():
            failures.append(
                f"missing required project document: {path.relative_to(repo_root)}"
            )

    markdown_files = public_markdown(repo_root)
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
        failures.extend(anchor_failures(repo_root, path))
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
        print("project documentation validation failed:", file=sys.stderr)
        for failure in failures:
            print(f"  {failure}", file=sys.stderr)
        return 1
    print(
        f"project docs valid: {len(markdown_files)} Markdown files, "
        f"{len(list((repo_root / 'docs' / 'reports').glob('*.md')))} reports indexed"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
