#!/usr/bin/env python3
"""Check the shading and spectroradiometer cross-language result contracts.

The shading check runs a test-only C++ emitter through the real serializer and
the exporter's shared publication validator. The spectroradiometer check reads
the compiled C++ authority and behavior-probes the production receipt tools.

Usage:
  python3 tools/check_schema_contract.py --shading-producer PATH \
      --spectro-producer PATH [--repo-root PATH]
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import tempfile
from pathlib import Path
from types import ModuleType
from typing import Any


SPECTRO_PYTHON_SOURCES = [
    (
        Path("tools/generate_spectro_receipt.py"),
        "receipt generator accepted version",
        "generator",
    ),
    (
        Path("tools/check_spectro_receipt.py"),
        "receipt checker accepted version",
        "checker",
    ),
]


def load_module(name: str, path: Path) -> ModuleType:
    # Execute the source we just read instead of accepting a timestamp/size
    # matched .pyc. Mutation tests deliberately make same-size edits within one
    # filesystem timestamp tick; stale bytecode would turn those into a false
    # pass in the contract checker itself.
    module = ModuleType(name)
    module.__file__ = str(path)
    source = path.read_text()
    try:
        exec(compile(source, str(path), "exec"), module.__dict__)
    except Exception as error:
        raise RuntimeError(f"{path}: cannot load module: {error}") from error
    return module


def load_producer_document(producer: Path) -> dict[str, Any]:
    try:
        result = subprocess.run(
            [str(producer)],
            check=False,
            capture_output=True,
            text=True,
        )
    except OSError as error:
        raise RuntimeError(f"{producer}: cannot execute: {error}") from error
    if result.returncode != 0:
        detail = result.stderr.strip() or f"exit status {result.returncode}"
        raise RuntimeError(f"{producer}: producer failed: {detail}")
    try:
        document = json.loads(result.stdout)
    except json.JSONDecodeError as error:
        raise RuntimeError(
            f"{producer}: producer did not emit valid JSON: {error}"
        ) from error
    if not isinstance(document, dict):
        raise RuntimeError(f"{producer}: producer JSON is not an object")
    return document


def check_shading_documents(
    exporter: ModuleType,
    producer_document: dict[str, Any],
    fixture_document: dict[str, Any],
) -> list[str]:
    errors: list[str] = []
    for label, document in (
        ("live C++ shading JSON", producer_document),
        ("canonical exporter fixture", fixture_document),
    ):
        try:
            exporter.validate_inventory_document(
                document, label, require_verified_pedestal=True
            )
        except (AttributeError, KeyError, TypeError, ValueError) as error:
            errors.append(str(error))

    producer_contract = {
        "schema_version": producer_document.get("schema_version"),
        "analysis_options": producer_document.get("analysis_options"),
    }
    fixture_contract = {
        "schema_version": fixture_document.get("schema_version"),
        "analysis_options": fixture_document.get("analysis_options"),
    }
    if producer_contract != fixture_contract:
        errors.append(
            "live C++ shading JSON and canonical exporter fixture disagree: "
            f"producer schema={producer_contract['schema_version']!r}, "
            f"fixture schema={fixture_contract['schema_version']!r}, "
            "or their complete analysis_options differ"
        )
    return errors


def load_spectro_producer_version(producer: Path) -> int:
    try:
        result = subprocess.run(
            [str(producer)], check=False, capture_output=True, text=True
        )
    except OSError as error:
        raise RuntimeError(f"{producer}: cannot execute: {error}") from error
    if result.returncode != 0:
        detail = result.stderr.strip() or f"exit status {result.returncode}"
        raise RuntimeError(f"{producer}: producer failed: {detail}")
    try:
        version = int(result.stdout.strip())
    except ValueError as error:
        raise RuntimeError(
            f"{producer}: producer did not emit one integer schema version"
        ) from error
    if version <= 0:
        raise RuntimeError(f"{producer}: schema version must be positive")
    return version


def probe_python_version_binding(
    module: ModuleType, kind: str, version: int, label: str
) -> str | None:
    """Prove the exported constant controls the production admission path."""
    probe = version + 97
    module.RESULT_SCHEMA_VERSION = probe

    def invoke(result_version: int) -> str:
        try:
            if kind == "generator":
                result = {
                    "schema_version": result_version,
                    "groups": [],
                    "evidence": {
                        "measurement_groups": 0,
                        "canonical_readings": 0,
                    },
                }
                module.summarize(result, [], [], {})
            elif kind == "checker":
                with tempfile.TemporaryDirectory() as directory:
                    root = Path(directory)
                    receipt = root / "receipt.json"
                    groups = root / "groups.csv"
                    receipt.write_text(
                        json.dumps(
                            {
                                "receipt_schema_version": (
                                    module.RECEIPT_SCHEMA_VERSION
                                ),
                                "result_schema_version": result_version,
                            }
                        )
                    )
                    groups.write_text("")
                    module.validate(root, receipt, groups)
            else:
                return f"unknown version-binding probe {kind!r}"
        except Exception as error:
            return str(error)
        return ""

    try:
        prior_error = invoke(version)
        probe_error = invoke(probe)
        if str(probe) not in prior_error or str(probe) in probe_error:
            return (
                f"{label}: RESULT_SCHEMA_VERSION does not control the production "
                f"{kind} schema check"
            )
    finally:
        module.RESULT_SCHEMA_VERSION = version
    return None


def check_spectro(root: Path, producer_version: int) -> list[str]:
    errors: list[str] = []
    found: list[tuple[Path, str, int]] = [
        (
            Path("compiled C++ spectro schema"),
            "producer authority",
            producer_version,
        )
    ]

    for index, (rel, description, kind) in enumerate(SPECTRO_PYTHON_SOURCES):
        path = root / rel
        if not path.is_file():
            errors.append(f"{rel}: missing")
            continue
        try:
            module = load_module(f"spectro_contract_{index}", path)
        except (ImportError, OSError, RuntimeError) as error:
            errors.append(f"{rel}: cannot load {description}: {error}")
            continue
        version = getattr(module, "RESULT_SCHEMA_VERSION", None)
        if isinstance(version, bool) or not isinstance(version, int):
            errors.append(
                f"{rel}: no live integer RESULT_SCHEMA_VERSION ({description})"
            )
            continue
        binding_error = probe_python_version_binding(
            module, kind, version, str(rel)
        )
        if binding_error:
            errors.append(binding_error)
            continue
        found.append((rel, description, version))

    if errors:
        return errors
    versions = {version for _, _, version in found}
    if len(versions) > 1:
        detail = ", ".join(
            f"{rel} ({description}) = {version}"
            for rel, description, version in found
        )
        errors.append(
            "spectro-ingest schema version disagrees across producer and "
            f"consumers: {detail}"
        )
    return errors


def load_shading_contract(
    root: Path, producer: Path
) -> tuple[ModuleType, dict[str, Any], dict[str, Any]]:
    exporter = load_module(
        "schema_contract_exporter", root / "tools" / "export_shading_portfolio.py"
    )
    fixture_module = load_module(
        "schema_contract_fixture",
        root / "tools" / "test_export_shading_portfolio.py",
    )
    fixture_document = fixture_module.document(
        "dataset:fixture/Images/Sphere/Sphere_f8.0_1:1000_DSCF0001.RAF",
        True,
    )
    return exporter, load_producer_document(producer), fixture_document


def check(root: Path, shading_producer: Path, spectro_producer: Path) -> list[str]:
    try:
        spectro_version = load_spectro_producer_version(spectro_producer)
    except RuntimeError as error:
        return [str(error)]
    try:
        exporter, producer_document, fixture_document = load_shading_contract(
            root, shading_producer
        )
    except (AttributeError, ImportError, OSError, RuntimeError) as error:
        return [str(error), *check_spectro(root, spectro_version)]
    return [
        *check_shading_documents(exporter, producer_document, fixture_document),
        *check_spectro(root, spectro_version),
    ]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--repo-root", type=Path, default=Path(__file__).resolve().parents[1]
    )
    parser.add_argument("--shading-producer", type=Path, required=True)
    parser.add_argument("--spectro-producer", type=Path, required=True)
    args = parser.parse_args()

    root = args.repo_root.resolve()
    errors = check(
        root, args.shading_producer.resolve(), args.spectro_producer.resolve()
    )
    for error in errors:
        print(error, file=sys.stderr)
    if errors:
        return 1

    exporter, _, _ = load_shading_contract(root, args.shading_producer.resolve())
    spectro_version = load_spectro_producer_version(
        args.spectro_producer.resolve()
    )
    print(
        "schema contracts ok: live shading JSON accepted by exporter "
        f"v{exporter.SCHEMA_VERSION}; compiled spectro-ingest schema "
        f"v{spectro_version} is behavior-bound to both receipt tools"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
