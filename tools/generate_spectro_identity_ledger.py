#!/usr/bin/env python3
"""Derive a source-relative identity and repeat-group ledger for CLRS-589 MAT files.

The private MAT payloads are not published. This tool hashes them in place and
writes only relative source names, repeat identities, and SHA-256 digests. Exact
content aliases are resolved by digest, so grouping does not depend on directory
iteration order or on the numbered PRD filenames.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import pathlib
import re
import sys
from collections import Counter, defaultdict
from typing import Iterable, NamedTuple, TextIO


FIELDNAMES = (
    "group_id",
    "repeat_index",
    "canonical_path",
    "sha256",
    "alias_paths",
)
PATCH = re.compile(r"patch_(\d+)trail_(\d+)\.mat", re.IGNORECASE)
SCENE_ALIAS = re.compile(r"PRD(\d+)scene(\d+)\.mat", re.IGNORECASE)
NUMBERED_PRD = re.compile(r"PRD_(\d+)\.mat", re.IGNORECASE)
OLD_PRD = re.compile(r"prd_(\d+)\.mat", re.IGNORECASE)
SCAN_DIRECTORIES = (
    "Old/1 to 6",
    "Old/7 to 9",
    "Old/10 to 15",
    "Old/prd",
    "PRD measurments",
    "PRD measurments copy",
)


class Identity(NamedTuple):
    group_id: str
    repeat_index: int


class SourceFile(NamedTuple):
    relative_path: str
    digest: str
    identity: Identity | None
    alias_only: bool


class LedgerRow(NamedTuple):
    group_id: str
    repeat_index: int
    canonical_path: str
    sha256: str
    alias_paths: tuple[str, ...]


def sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def classify(relative_path: pathlib.PurePosixPath) -> tuple[Identity | None, bool]:
    relative = relative_path.as_posix()
    name = relative_path.name

    match = PATCH.fullmatch(name)
    if match and relative.startswith("Old/"):
        return Identity(f"ramp_patch_{int(match.group(1)):02d}",
                        int(match.group(2))), False

    match = OLD_PRD.fullmatch(name)
    if match and relative_path.parent.as_posix() == "Old/prd":
        return Identity("reference_prd", int(match.group(1))), False

    match = SCENE_ALIAS.fullmatch(name)
    if match and relative_path.parent.as_posix() == "PRD measurments copy":
        return Identity(f"scene_{int(match.group(2)):02d}",
                        int(match.group(1))), True

    if (NUMBERED_PRD.fullmatch(name) and
            relative_path.parent.as_posix() == "PRD measurments"):
        # These files carry acquisition order but not scene/repeat identity.
        # Their exact-content aliases provide that identity below.
        return None, False

    raise ValueError(f"unrecognized spectro source path: {relative}")


def discover_sources(archive_root: pathlib.Path) -> list[SourceFile]:
    root = archive_root.resolve()
    sources: list[SourceFile] = []
    for relative_directory in SCAN_DIRECTORIES:
        directory = root / relative_directory
        if not directory.is_dir():
            continue
        for path in sorted(directory.glob("*.mat")):
            relative = pathlib.PurePosixPath(path.relative_to(root).as_posix())
            identity, alias_only = classify(relative)
            sources.append(SourceFile(relative.as_posix(), sha256(path),
                                      identity, alias_only))
    if not sources:
        raise ValueError("no recognized spectro MAT files found")
    return sources


def group_sort_key(row: LedgerRow) -> tuple[int, int, int]:
    if row.group_id.startswith("ramp_patch_"):
        return 0, int(row.group_id.rsplit("_", 1)[1]), row.repeat_index
    if row.group_id == "reference_prd":
        return 1, 0, row.repeat_index
    return 2, int(row.group_id.rsplit("_", 1)[1]), row.repeat_index


def derive_rows(archive_root: pathlib.Path) -> list[LedgerRow]:
    by_digest: dict[str, list[SourceFile]] = defaultdict(list)
    for source in discover_sources(archive_root):
        by_digest[source.digest].append(source)

    rows: list[LedgerRow] = []
    for digest, matching in by_digest.items():
        identities = {source.identity for source in matching
                      if source.identity is not None}
        if len(identities) != 1:
            detail = ", ".join(source.relative_path for source in matching)
            raise ValueError(
                f"content identity has missing or conflicting repeat labels: {detail}")
        identity = next(iter(identities))
        canonical = [source for source in matching if not source.alias_only]
        if len(canonical) != 1:
            detail = ", ".join(source.relative_path for source in matching)
            raise ValueError(
                f"content identity must have exactly one canonical path: {detail}")
        aliases = tuple(sorted(source.relative_path for source in matching
                               if source.alias_only))
        rows.append(LedgerRow(identity.group_id, identity.repeat_index,
                              canonical[0].relative_path, digest, aliases))
    return sorted(rows, key=group_sort_key)


def row_dict(row: LedgerRow) -> dict[str, str]:
    return {
        "group_id": row.group_id,
        "repeat_index": str(row.repeat_index),
        "canonical_path": row.canonical_path,
        "sha256": row.sha256,
        "alias_paths": ";".join(row.alias_paths),
    }


def write_rows(rows: Iterable[LedgerRow], output: TextIO) -> None:
    writer = csv.DictWriter(output, fieldnames=FIELDNAMES, lineterminator="\n")
    writer.writeheader()
    for row in rows:
        writer.writerow(row_dict(row))


def require_relative(value: str, field: str) -> None:
    path = pathlib.PurePosixPath(value)
    has_scheme = re.match(r"^[A-Za-z][A-Za-z0-9+.-]*:", value) is not None
    if (path.is_absolute() or ".." in path.parts or not value or
            value.startswith("~") or "\\" in value or has_scheme or
            path.as_posix() != value or any(ord(char) < 32 for char in value)):
        raise ValueError(f"{field} must be a non-empty source-relative path")


def validate_rows(rows: list[dict[str, str]], expected_profile: str | None) -> None:
    if not rows:
        raise ValueError("identity ledger is empty")
    if tuple(rows[0]) != FIELDNAMES:
        raise ValueError("identity ledger header does not match the schema")

    identities: set[tuple[str, int]] = set()
    canonical_paths = {row.get("canonical_path", "") for row in rows}
    if len(canonical_paths) != len(rows):
        raise ValueError("duplicate canonical path")
    aliases_seen: set[str] = set()
    digests: set[str] = set()
    group_counts: Counter[str] = Counter()
    alias_count = 0
    repeat_indices: dict[str, set[int]] = defaultdict(set)

    for row in rows:
        if tuple(row) != FIELDNAMES:
            raise ValueError("identity ledger row does not match the schema")
        try:
            repeat_index = int(row["repeat_index"])
        except (KeyError, ValueError) as error:
            raise ValueError("repeat_index must be an integer") from error
        if repeat_index < 1:
            raise ValueError("repeat_index must be positive")
        identity = (row["group_id"], repeat_index)
        if identity in identities:
            raise ValueError(f"duplicate group/repeat identity: {identity}")
        identities.add(identity)

        canonical = row["canonical_path"]
        require_relative(canonical, "canonical_path")
        canonical_identity, canonical_is_alias = classify(
            pathlib.PurePosixPath(canonical)
        )
        if canonical_is_alias:
            raise ValueError(f"canonical path uses an alias-only name: {canonical}")
        if canonical_identity is not None and canonical_identity != Identity(*identity):
            raise ValueError(f"canonical path identity disagrees with row: {canonical}")

        digest = row["sha256"]
        if not re.fullmatch(r"[0-9a-f]{64}", digest):
            raise ValueError("sha256 must contain 64 lowercase hexadecimal digits")
        if digest in digests:
            raise ValueError(f"duplicate content digest: {digest}")
        digests.add(digest)

        aliases = [alias for alias in row["alias_paths"].split(";") if alias]
        for alias in aliases:
            require_relative(alias, "alias_paths")
            if alias in canonical_paths or alias in aliases_seen:
                raise ValueError(f"source path is reused as an alias: {alias}")
            alias_identity, alias_only = classify(pathlib.PurePosixPath(alias))
            if not alias_only or alias_identity != Identity(*identity):
                raise ValueError(f"alias path identity disagrees with row: {alias}")
            aliases_seen.add(alias)
        alias_count += len(aliases)
        group_counts[row["group_id"]] += 1
        repeat_indices[row["group_id"]].add(repeat_index)

    for group_id, count in group_counts.items():
        if repeat_indices[group_id] != set(range(1, count + 1)):
            raise ValueError(f"non-contiguous repeat indices for {group_id}")

    if expected_profile == "clrs589":
        expected = {f"ramp_patch_{index:02d}": (2 if 7 <= index <= 9 else 3)
                    for index in range(1, 16)}
        expected["reference_prd"] = 2
        expected.update({f"scene_{index:02d}": (2 if index <= 21 else 1)
                         for index in range(1, 25)})
        if dict(group_counts) != expected:
            raise ValueError(
                f"group profile differs from the 40 recorded groups: {dict(group_counts)}")
        if len(rows) != 89 or alias_count != 45:
            raise ValueError(
                f"expected 89 canonical readings and 45 aliases, got "
                f"{len(rows)} and {alias_count}")


def read_rows(path: pathlib.Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as source:
        return list(csv.DictReader(source))


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--archive-root", type=pathlib.Path,
                      help="root of the private CLRS-589 project archive")
    mode.add_argument("--check", type=pathlib.Path,
                      help="validate a previously derived public ledger")
    parser.add_argument("--output", type=pathlib.Path,
                        help="write a derived CSV here; otherwise use stdout")
    args = parser.parse_args(argv)

    try:
        if args.check:
            rows = read_rows(args.check)
            validate_rows(rows, expected_profile="clrs589")
            print(
                "spectro identity ledger structure valid: 89 canonical "
                "readings, 45 declared aliases, 40 measurement groups "
                "(37 repeated, 3 singleton); private files were not re-hashed")
            return 0

        derived = derive_rows(args.archive_root)
        rendered = [row_dict(row) for row in derived]
        validate_rows(rendered, expected_profile="clrs589")
        if args.output:
            with args.output.open("w", newline="", encoding="utf-8") as output:
                write_rows(derived, output)
        else:
            write_rows(derived, sys.stdout)
        print(
            "derived 89 canonical readings and 45 exact-content aliases",
            file=sys.stderr)
        return 0
    except (OSError, ValueError) as error:
        print(f"spectro identity ledger: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
