#!/usr/bin/env python3
"""Negative-path and derivation tests for the spectro identity ledger."""

from __future__ import annotations

import csv
import importlib.util
import io
import pathlib
import tempfile


MODULE_PATH = pathlib.Path(__file__).with_name("generate_spectro_identity_ledger.py")
SPEC = importlib.util.spec_from_file_location("spectro_ledger", MODULE_PATH)
assert SPEC and SPEC.loader
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def write(root: pathlib.Path, relative: str, payload: bytes) -> None:
    path = root / relative
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(payload)


def fixture(root: pathlib.Path) -> None:
    write(root, "Old/1 to 6/patch_1trail_1.mat", b"patch-a")
    write(root, "Old/1 to 6/patch_1trail_2.mat", b"patch-b")
    write(root, "Old/prd/prd_1.mat", b"old-prd-a")
    write(root, "PRD measurments/PRD_01.mat", b"scene-a")
    write(root, "PRD measurments copy/PRD1scene1.mat", b"scene-a")


def main() -> int:
    with tempfile.TemporaryDirectory() as temporary:
        root = pathlib.Path(temporary)
        fixture(root)
        rows = MODULE.derive_rows(root)
        assert len(rows) == 4
        scene = next(row for row in rows if row.group_id == "scene_01")
        assert scene.repeat_index == 1
        assert scene.canonical_path == "PRD measurments/PRD_01.mat"
        assert scene.alias_paths == ("PRD measurments copy/PRD1scene1.mat",)

        stream = io.StringIO()
        MODULE.write_rows(rows, stream)
        parsed = list(csv.DictReader(io.StringIO(stream.getvalue())))
        MODULE.validate_rows(parsed, expected_profile=None)

        duplicate = [dict(row) for row in parsed]
        duplicate.append(dict(duplicate[0]))
        try:
            MODULE.validate_rows(duplicate, expected_profile=None)
            raise AssertionError("duplicate canonical identity was accepted")
        except ValueError as error:
            assert "duplicate" in str(error)

        absolute = [dict(row) for row in parsed]
        absolute[0]["canonical_path"] = "/private/archive/file.mat"
        try:
            MODULE.validate_rows(absolute, expected_profile=None)
            raise AssertionError("absolute private path was accepted")
        except ValueError as error:
            assert "relative" in str(error)

        windows_path = (
            "C:" + chr(92) + "Users" + chr(92) + "account" +
            chr(92) + "secret.mat"
        )
        file_uri = "file" + ":///private/archive.mat"
        for leaked in (
            windows_path,
            "~/private/archive.mat",
            file_uri,
            "Old//1 to 6/file.mat",
        ):
            try:
                MODULE.require_relative(leaked, "test_path")
                raise AssertionError(f"machine-specific path was accepted: {leaked}")
            except ValueError as error:
                assert "relative" in str(error) or "canonical" in str(error)

        scene_alias = next(
            row["alias_paths"] for row in parsed if row["group_id"] == "scene_01"
        )
        reused_alias = [dict(row) for row in parsed]
        scene_index = next(
            index for index, row in enumerate(reused_alias)
            if row["group_id"] == "scene_01"
        )
        reused_alias[scene_index]["alias_paths"] = f"{scene_alias};{scene_alias}"
        try:
            MODULE.validate_rows(reused_alias, expected_profile=None)
            raise AssertionError("one alias reused by two rows was accepted")
        except ValueError as error:
            assert "reused" in str(error)

        canonical_as_alias = [dict(row) for row in parsed]
        canonical_as_alias[0]["alias_paths"] = parsed[1]["canonical_path"]
        try:
            MODULE.validate_rows(canonical_as_alias, expected_profile=None)
            raise AssertionError("canonical path reused as an alias was accepted")
        except ValueError as error:
            assert "reused" in str(error)

        wrong_identity = [dict(row) for row in parsed]
        scene_index = next(
            index for index, row in enumerate(wrong_identity)
            if row["group_id"] == "scene_01"
        )
        wrong_identity[scene_index]["group_id"] = "scene_02"
        try:
            MODULE.validate_rows(wrong_identity, expected_profile=None)
            raise AssertionError("path/group identity mismatch was accepted")
        except ValueError as error:
            assert "identity" in str(error)

        conflict_root = root / "conflict"
        fixture(conflict_root)
        write(conflict_root, "PRD measurments copy/PRD2scene2.mat", b"scene-a")
        try:
            MODULE.derive_rows(conflict_root)
            raise AssertionError("one payload assigned to two groups was accepted")
        except ValueError as error:
            assert "conflicting" in str(error)

    print("spectro identity ledger derivation and negative paths ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
