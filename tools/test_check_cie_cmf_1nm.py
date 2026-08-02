#!/usr/bin/env python3
"""Negative paths for the CIE source and derived-table guard."""

from __future__ import annotations

import importlib.util
import shutil
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "check_cie_cmf_1nm", ROOT / "tools" / "check_cie_cmf_1nm.py"
)
assert SPEC and SPEC.loader
CHECK = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CHECK)

FILES = (
    Path("data/third_party/CIE_xyz_1931_2deg.csv"),
    Path("data/third_party/CIE_std_illum_D50.csv"),
    Path("data/third_party/CIE_illum_D55.csv"),
    Path("data/cie1931_2deg_cmf_1nm.csv"),
    Path("data/cie1931_2deg_cmf.csv"),
    Path("data/cie_d50.csv"),
    Path("data/cie_d55.csv"),
)


def staged(temp: Path) -> Path:
    for relative in FILES:
        target = temp / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy(ROOT / relative, target)
    return temp


def expect_error(root: Path, needle: str) -> None:
    errors = CHECK.check(root)
    if not any(needle in error for error in errors):
        raise SystemExit(f"expected {needle!r} in {errors!r}")


def replace_once(path: Path, old: str, new: str) -> None:
    text = path.read_text(encoding="utf-8")
    if old not in text:
        raise SystemExit(f"fixture text not found in {path}: {old!r}")
    path.write_text(text.replace(old, new, 1), encoding="utf-8")


def main() -> int:
    errors = CHECK.check(ROOT)
    if errors:
        raise SystemExit(f"committed CIE data should validate: {errors}")

    with tempfile.TemporaryDirectory() as raw:
        root = staged(Path(raw))

        observer = root / "data/third_party/CIE_xyz_1931_2deg.csv"
        replace_once(observer, "360,0.000129900000", "360,0.000129900001")
        expect_error(root, "official observer copy SHA-256")

        root = staged(Path(raw))
        coarse = root / "data/cie1931_2deg_cmf.csv"
        replace_once(coarse, "380,0.001368", "380,0.001369")
        expect_error(root, "10 nm observer")

        root = staged(Path(raw))
        d50 = root / "data/cie_d50.csv"
        replace_once(d50, "380,24.488", "380,24.489")
        expect_error(root, "D50 subset")

        root = staged(Path(raw))
        d55 = root / "data/cie_d55.csv"
        replace_once(d55, "380,32.584", "380,32.585")
        expect_error(root, "D55 subset")

    print("CIE reference-data negative paths ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
