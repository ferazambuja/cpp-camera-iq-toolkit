#!/usr/bin/env python3
"""Focused tests for project-documentation language guards."""

from __future__ import annotations

import importlib.util
import subprocess
import tempfile
import unittest
from pathlib import Path


SCRIPT = Path(__file__).with_name("check_portfolio_docs.py")
SPEC = importlib.util.spec_from_file_location("check_portfolio_docs", SCRIPT)
assert SPEC and SPEC.loader
DOCS = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(DOCS)


def matched(text: str) -> bool:
    raw_match = any(
        pattern.search(text)
        for pattern in {**DOCS.STALE_PATTERNS, **DOCS.INTERNAL_PATTERNS}.values()
    )
    normalized = DOCS.normalize_markdown(text)
    return raw_match or any(
        pattern.search(normalized)
        for pattern in DOCS.NORMALIZED_STALE_PATTERNS.values()
    )


class PublicationLanguageTests(unittest.TestCase):
    def test_spectro_case_study_is_a_required_public_document(self) -> None:
        self.assertIn(
            Path("docs/case-studies/spectroradiometer-ingest.md"),
            DOCS.REQUIRED_PROJECT_DOCUMENTS,
        )

    def test_documentation_standard_and_implementation_index_are_required(self) -> None:
        self.assertIn(
            Path("docs/PUBLIC_DOCUMENTATION_STANDARD.md"),
            DOCS.REQUIRED_PROJECT_DOCUMENTS,
        )
        self.assertIn(
            Path("docs/implementation/README.md"),
            DOCS.REQUIRED_PROJECT_DOCUMENTS,
        )

    def test_public_markdown_includes_untracked_public_plan(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            repo = Path(temp)
            subprocess.run(
                ["git", "init", "--quiet"], cwd=repo, check=True
            )
            (repo / "README.md").write_text("# Project\n", encoding="utf-8")
            plan = repo / "docs" / "plans" / "PLAN.md"
            plan.parent.mkdir(parents=True)
            plan.write_text("# Public implementation record\n", encoding="utf-8")

            self.assertIn(plan, DOCS.public_markdown(repo))

    def test_rejects_completed_slice_and_staging_language(self) -> None:
        phrases = [
            "The first implementation slice should use the D810 sweep.",
            "The second implementation slice adds RAW extraction.",
            "A later development slice can remove the dependency.",
            "Recommended local cache id when a slice stages files:",
            "Copy each subset only when its slice runs; do not bulk-copy.",
            "The first public slices used this dark-frame set.",
            "Downstream closure and quality slices consume the CSV.",
            "This limitation is carried forward to a later phase.",
            "## Not Claimed",
            "## Not claimed",
        ]
        for phrase in phrases:
            with self.subTest(phrase=phrase):
                self.assertTrue(matched(phrase))

    def test_allows_scientific_scope_language(self) -> None:
        phrases = [
            "The validation threshold is 0.999 correlation.",
            "The 24-patch slice of the reflectance matrix is analyzed.",
            "Copy configs/datasets.example.json to configure local roots.",
        ]
        for phrase in phrases:
            with self.subTest(phrase=phrase):
                self.assertFalse(matched(phrase))

    def test_rejects_audience_strategy_and_self_promotional_language(self) -> None:
        phrases = [
            "**Hiring-manager summary:** The D810 showed a strong f/5.6 peak.",
            "**Recruiter — 30 to 60 seconds:** start with the overview.",
            "**Hiring manager — about five minutes:** inspect the reports.",
            "This page routes three kinds of readers through the same evidence.",
            "For a five-minute technical tour, start with the portfolio index.",
            "Portfolio audit: 2026-07-28",
            "## Executive Verdict",
            "## Public Summary",
            "Defensible summary: the archive supports this comparison.",
            "## Bottom Line",
            "Return to the portfolio landing page.",
            "# Camera IQ portfolio and report index",
            "## Public evidence model",
            "Rebuild the portfolio plots.",
            "This is a research and portfolio toolkit.",
            "Honest scope: the workbook is a compatible reference.",
            "JSON honesty contract: DN units and non-support flags.",
            "This is a claim-scoped green-linear measurement.",
            "Slanted-edge SFR/MTF is not globally data-blocked.",
            "## Hazards (do not trip on these)",
            "## Verified this session (machine-precision)",
            "**Caveat preserved.** The reference is not per-unit.",
            "**Do not claim** exact measured-reference Delta E.",
            "The current honest claim is bounded.",
            "## Available but unused data (cataloged so it is not ignored)",
            "## Authority rule",
            "CC-18 is the more discriminating and more honest number.",
            "The gap requires calibration rather than unimplemented parser loops.",
            "This is the highest scientific gap.",
            "Automatic localization is useful engineering polish.",
            "This is less scientifically important than the seeded workflow.",
            "These are small provenance-strengthening tasks.",
            "Rendered-luma parity is lower priority.",
            "Complete the manifest before any analysis claims.",
            "No color-accuracy numbers are claimed.",
            "Every claim is tied to a reproducible command.",
            "No claim that the original outputs are correct.",
            "Feasibility is promising, pending follow-up checks.",
            "The earlier blocked conclusion was too broad.",
            "Regenerate artifacts before claiming the table is fully migrated.",
            "The projective grid is not yet approved.",
            (
                "Captures about 50 minutes apart make a single copy plausible "
                "but unverified."
            ),
            (
                "The common lens model and close capture times make a\n"
                "single lens copy plausible."
            ),
            (
                "The captures were about 50\nminutes apart, suggesting a "
                "shared physical lens."
            ),
            (
                "The sweeps were under an hour apart, making one lens sample "
                "likely."
            ),
            "## Implemented and optional extensions",
            "[Finished SFR/MTF report](SFR_MTF.md)",
            "Finished D800/D810 center, aperture, and field analysis.",
        ]
        for phrase in phrases:
            with self.subTest(phrase=phrase):
                self.assertTrue(matched(phrase))

    def test_allows_project_centered_overview_language(self) -> None:
        phrases = [
            "## Overview",
            "The D810 showed a strong f/5.6 peak.",
            "For the core technical path, start with the documentation index.",
            "## Coverage summary",
            "The archive supports this bounded comparison.",
            (
                "The timestamps support ordering within each sweep, but not "
                "elapsed time between sweeps or a shared physical lens."
            ),
        ]
        for phrase in phrases:
            with self.subTest(phrase=phrase):
                self.assertFalse(matched(phrase))


class ProvenanceContractTests(unittest.TestCase):
    def test_current_reports_satisfy_contracts(self) -> None:
        repo_root = SCRIPT.parent.parent
        self.assertEqual([], DOCS.provenance_contract_failures(repo_root))

    def test_missing_contract_document_is_reported(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            failures = DOCS.provenance_contract_failures(Path(temp))
        self.assertTrue(
            any("missing provenance contract document" in item for item in failures)
        )

    def test_each_contract_detects_its_marker_removal(self) -> None:
        repo_root = SCRIPT.parent.parent
        for relative, requirements in DOCS.PROVENANCE_CONTRACTS.items():
            text = DOCS.normalize_markdown(
                (repo_root / relative).read_text(encoding="utf-8")
            )
            for label, pattern in requirements:
                with self.subTest(path=relative, label=label):
                    self.assertIsNotNone(pattern.search(text))
                    mutated = pattern.sub("", text)
                    failures = DOCS.provenance_contract_failures_for_text(
                        relative, mutated
                    )
                    self.assertTrue(
                        any(label in failure for failure in failures), failures
                    )

    def test_sfr_contract_allows_date_correction(self) -> None:
        relative = Path("docs/reports/SFR_MTF.md")
        text = (SCRIPT.parent.parent / relative).read_text(encoding="utf-8")
        mutated = text.replace("2016-12-09", "2035-01-02")
        self.assertEqual(
            [], DOCS.provenance_contract_failures_for_text(relative, mutated)
        )

    def test_wrapped_clock_boundary_is_detected(self) -> None:
        relative = Path("docs/reports/SFR_MTF.md")
        text = (SCRIPT.parent.parent / relative).read_text(encoding="utf-8")
        wrapped = text.replace(
            "different camera bodies, and no clock-synchronization record survives",
            "different camera bodies, and no clock-synchronization\nrecord survives",
        )
        self.assertEqual(
            [], DOCS.provenance_contract_failures_for_text(relative, wrapped)
        )

    def test_metric_contract_allows_substantive_rewording(self) -> None:
        relative = Path("docs/case-studies/color-model-equation-audit.md")
        reworded = (
            "CIE94 is retained to test the historical result; CIEDE2000 is "
            "used by current studies. Their formulas and weighting rules "
            "differ, so values are method-specific and are not compared "
            "numerically across studies."
        )
        self.assertEqual(
            [], DOCS.provenance_contract_failures_for_text(relative, reworded)
        )

    def test_metric_contract_rejects_term_only_boundary(self) -> None:
        relative = Path("docs/case-studies/color-model-equation-audit.md")
        weakened = (
            "CIE94 is retained to test the historical result. The current "
            "studies use CIEDE2000."
        )
        failures = DOCS.provenance_contract_failures_for_text(relative, weakened)
        self.assertTrue(
            any("CIE94 versus CIEDE2000 method boundary" in item for item in failures),
            failures,
        )

    def test_metric_contract_rejects_deleted_current_metric(self) -> None:
        relative = Path("docs/case-studies/color-model-equation-audit.md")
        weakened = (
            "CIE94 is retained to test the historical result. Their formulas "
            "and weighting rules differ, so values are method-specific and are "
            "not compared numerically across studies."
        )
        failures = DOCS.provenance_contract_failures_for_text(relative, weakened)
        self.assertTrue(
            any("CIE94 versus CIEDE2000 method boundary" in item for item in failures),
            failures,
        )


class EvidenceAttributionTests(unittest.TestCase):
    def fixture_contracts(self):
        return {
            "example_threshold": DOCS.EvidenceAttributionContract(
                document=Path("docs/implementation/example.md"),
                test=Path("tests/test_example.cpp"),
            )
        }

    def write_evidence_macro(
        self,
        repo: Path,
        expansion: str = (
            "((assertion), ::test::record_doc_evidence(#evidence_id))"
        ),
    ) -> None:
        header = repo / "tests" / "harness.hpp"
        header.parent.mkdir(parents=True, exist_ok=True)
        header.write_text(
            "namespace test {\n"
            "inline void record_doc_evidence(const char*) {}\n"
            "template <typename Body> int run(Body body) { body(); return 0; }\n"
            "}\n"
            "#define CAMERA_IQ_DOC_EVIDENCE(evidence_id, assertion) "
            f"{expansion}\n"
            "void TESTS();\n"
            "#ifndef CAMERA_IQ_TEST_HARNESS_NO_MAIN\n"
            "int main() { return test::run([] { TESTS(); }); }\n"
            "#endif\n",
            encoding="utf-8",
        )

    def write_cmake_registration(self, repo: Path) -> None:
        (repo / "CMakeLists.txt").write_text(
            "function(camera_iq_add_test name source)\n"
            "  add_executable(${name} ${source})\n"
            "  add_test(NAME ${name} COMMAND ${name})\n"
            "endfunction()\n"
            "function(camera_iq_expect_doc_evidence target expectations)\n"
            "  set(evidence_tmpdir "
            "${CAMERA_IQ_TEST_TMPDIR}/doc-evidence-${target})\n"
            "  file(MAKE_DIRECTORY ${evidence_tmpdir})\n"
            "  add_test(NAME check_doc_evidence_${target}\n"
            "    COMMAND ${CMAKE_COMMAND} -E env\n"
            "      TMPDIR=${evidence_tmpdir}\n"
            "      TMP=${evidence_tmpdir}\n"
            "      TEMP=${evidence_tmpdir}\n"
            "      CAMERA_IQ_DOC_EVIDENCE_EXPECT=${expectations}\n"
            "      $<TARGET_FILE:${target}>)\n"
            "endfunction()\n"
            "camera_iq_add_test(test_example tests/test_example.cpp)\n"
            "camera_iq_expect_doc_evidence(test_example example_threshold=1)\n",
            encoding="utf-8",
        )

    def write_valid_fixture(self, repo: Path) -> None:
        document = repo / "docs" / "implementation" / "example.md"
        document.parent.mkdir(parents=True)
        document.write_text(
            "The threshold is pinned by "
            "[`test_example.cpp`](../../tests/test_example.cpp).\n"
            "<!-- test-evidence: example_threshold -->\n",
            encoding="utf-8",
        )
        test = repo / "tests" / "test_example.cpp"
        test.parent.mkdir(parents=True)
        test.write_text(
            "void TESTS() {\n"
            "  CAMERA_IQ_DOC_EVIDENCE(example_threshold, "
            "check(value == 1));\n"
            "}\n",
            encoding="utf-8",
        )
        self.write_evidence_macro(repo)

    def test_executable_evidence_call_can_bind_a_claim(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            repo = Path(temp)
            document = repo / "docs" / "implementation" / "example.md"
            document.parent.mkdir(parents=True)
            document.write_text(
                "The threshold is pinned by "
                "[`test_example.cpp`](../../tests/test_example.cpp).\n"
                "<!-- test-evidence: example_threshold -->\n",
                encoding="utf-8",
            )
            test = repo / "tests" / "test_example.cpp"
            test.parent.mkdir(parents=True)
            test.write_text(
                "void TESTS() {\n"
                "  CAMERA_IQ_DOC_EVIDENCE(example_threshold, "
                "check(value == 1));\n"
                "}\n",
                encoding="utf-8",
            )
            self.write_evidence_macro(repo)
            contracts = {
                "example_threshold": DOCS.EvidenceAttributionContract(
                    document=Path("docs/implementation/example.md"),
                    test=Path("tests/test_example.cpp"),
                )
            }
            self.assertEqual(
                [], DOCS.evidence_attribution_failures(repo, contracts)
            )

    def test_wrapper_in_helper_is_checked_by_runtime_count_not_location(self) -> None:
        # A called helper is a valid refactor. Static attribution establishes
        # identity and exact source count; the CTest runtime expectation decides
        # whether the helper is actually reached.
        with tempfile.TemporaryDirectory() as temp:
            repo = Path(temp)
            self.write_valid_fixture(repo)
            (repo / "tests" / "test_example.cpp").write_text(
                "namespace { void never_called() {\n"
                "  CAMERA_IQ_DOC_EVIDENCE(example_threshold, check(value == 1));\n"
                "} }\n"
                "void TESTS() {\n"
                "  check(other == 2);\n"
                "}\n",
                encoding="utf-8",
            )
            failures = DOCS.evidence_attribution_failures(
                repo, self.fixture_contracts()
            )
            self.assertEqual(
                [],
                failures,
            )

    def test_registered_source_without_tests_body_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            repo = Path(temp)
            self.write_valid_fixture(repo)
            (repo / "tests" / "test_example.cpp").write_text(
                "CAMERA_IQ_DOC_EVIDENCE(example_threshold, "
                "check(value == 1));\n",
                encoding="utf-8",
            )
            failures = DOCS.evidence_attribution_failures(
                repo, self.fixture_contracts()
            )
            self.assertTrue(
                any("does not define TESTS()" in item for item in failures),
                failures,
            )

    def test_registered_source_cannot_disable_runtime_harness(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            repo = Path(temp)
            self.write_valid_fixture(repo)
            source = repo / "tests" / "test_example.cpp"
            source.write_text(
                "#define CAMERA_IQ_TEST_HARNESS_NO_MAIN\n"
                + source.read_text(encoding="utf-8"),
                encoding="utf-8",
            )
            failures = DOCS.evidence_attribution_failures(
                repo, self.fixture_contracts()
            )
            self.assertTrue(
                any("disables the harness main()" in item for item in failures),
                failures,
            )

    def test_harness_entry_point_is_required(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            repo = Path(temp)
            self.write_valid_fixture(repo)
            header = repo / "tests" / "harness.hpp"
            header.write_text(
                header.read_text(encoding="utf-8").replace(
                    "int main() { return test::run([] { TESTS(); }); }",
                    "int main() { return 0; }",
                ),
                encoding="utf-8",
            )
            failures = DOCS.evidence_attribution_failures(
                repo, self.fixture_contracts()
            )
            self.assertTrue(
                any("default harness entry point" in item for item in failures),
                failures,
            )

    def test_wrapper_nested_inside_the_tests_body_is_accepted(self) -> None:
        # The rule is containment, not top-level placement: these assertions
        # normally sit inside scoped fixture blocks.
        with tempfile.TemporaryDirectory() as temp:
            repo = Path(temp)
            self.write_valid_fixture(repo)
            (repo / "tests" / "test_example.cpp").write_text(
                "void TESTS() {\n"
                "  {\n"
                "    const char brace = '{';\n"
                "    // a closing } in a comment must not end the body\n"
                "    CAMERA_IQ_DOC_EVIDENCE(example_threshold,\n"
                "                           check(value == 1));\n"
                "  }\n"
                "}\n",
                encoding="utf-8",
            )
            self.assertEqual(
                [],
                DOCS.evidence_attribution_failures(
                    repo, self.fixture_contracts()
                ),
            )

    def test_registered_sources_declare_their_wrapper_counts(self) -> None:
        repo_root = SCRIPT.parent.parent
        for evidence_id, contract in DOCS.EVIDENCE_ATTRIBUTION_CONTRACTS.items():
            with self.subTest(evidence_id=evidence_id):
                source = (repo_root / contract.test).read_text(encoding="utf-8")
                executable = DOCS._mask_cpp_noncode(source)
                wrappers = [
                    match
                    for match in DOCS.EXECUTABLE_TEST_EVIDENCE_RE.finditer(
                        executable
                    )
                    if match.group(1) == evidence_id
                ]
                self.assertEqual(contract.assertion_count, len(wrappers))

    def test_non_executable_evidence_text_is_rejected(self) -> None:
        for source in (
            "// CAMERA_IQ_DOC_EVIDENCE(example_threshold, check(true));\n",
            "/* CAMERA_IQ_DOC_EVIDENCE(example_threshold, check(true)); */\n",
            'const char* fixture = "CAMERA_IQ_DOC_EVIDENCE('
            'example_threshold, check(true));";\n',
            'const char* fixture = R"(CAMERA_IQ_DOC_EVIDENCE('
            'example_threshold, check(true));)";\n',
        ):
            with self.subTest(source=source):
                with tempfile.TemporaryDirectory() as temp:
                    repo = Path(temp)
                    document = repo / "docs" / "implementation" / "example.md"
                    document.parent.mkdir(parents=True)
                    document.write_text(
                        "The threshold is pinned by "
                        "[`test_example.cpp`](../../tests/test_example.cpp).\n"
                        "<!-- test-evidence: example_threshold -->\n",
                        encoding="utf-8",
                    )
                    test = repo / "tests" / "test_example.cpp"
                    test.parent.mkdir(parents=True)
                    test.write_text(source, encoding="utf-8")
                    self.write_evidence_macro(repo)
                    contracts = {
                        "example_threshold": DOCS.EvidenceAttributionContract(
                            document=Path("docs/implementation/example.md"),
                            test=Path("tests/test_example.cpp"),
                        )
                    }
                    failures = DOCS.evidence_attribution_failures(repo, contracts)
                    self.assertTrue(
                        any(
                            "executable assertion count is 0" in item
                            for item in failures
                        ),
                        failures,
                    )

    def test_pre_assertion_runtime_recording_macro_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            repo = Path(temp)
            self.write_valid_fixture(repo)
            self.write_evidence_macro(
                repo,
                "(::test::record_doc_evidence(#evidence_id), (assertion))",
            )
            failures = DOCS.evidence_attribution_failures(
                repo, self.fixture_contracts()
            )
            self.assertTrue(
                any("must record and execute" in item for item in failures),
                failures,
            )

    def test_direct_runtime_evidence_recording_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            repo = Path(temp)
            self.write_valid_fixture(repo)
            source = repo / "tests" / "test_example.cpp"
            source.write_text(
                "namespace { void never_called() {\n"
                "  CAMERA_IQ_DOC_EVIDENCE(example_threshold, check(true));\n"
                "} }\n"
                "void TESTS() {\n"
                '  test::record_doc_evidence("example_threshold");\n'
                "}\n",
                encoding="utf-8",
            )
            failures = DOCS.evidence_attribution_failures(
                repo, self.fixture_contracts()
            )
            self.assertTrue(
                any("accesses documentation-evidence runtime internals directly" in item
                    for item in failures),
                failures,
            )

    def test_unqualified_runtime_evidence_recording_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            repo = Path(temp)
            self.write_valid_fixture(repo)
            source = repo / "tests" / "test_example.cpp"
            source.write_text(
                "using test::record_doc_evidence;\n"
                "void TESTS() {\n"
                '  record_doc_evidence("example_threshold");\n'
                "}\n",
                encoding="utf-8",
            )
            failures = DOCS.evidence_attribution_failures(
                repo, self.fixture_contracts()
            )
            self.assertTrue(
                any("accesses documentation-evidence runtime internals directly" in item
                    for item in failures),
                failures,
            )

    def test_runtime_evidence_recorder_alias_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            repo = Path(temp)
            self.write_valid_fixture(repo)
            source = repo / "tests" / "test_example.cpp"
            source.write_text(
                "void TESTS() {\n"
                "  auto hit = test::record_doc_evidence;\n"
                '  hit("example_threshold");\n'
                "}\n",
                encoding="utf-8",
            )
            failures = DOCS.evidence_attribution_failures(
                repo, self.fixture_contracts()
            )
            self.assertTrue(
                any("accesses documentation-evidence runtime internals directly" in item
                    for item in failures),
                failures,
            )

    def test_direct_runtime_evidence_state_access_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            repo = Path(temp)
            self.write_valid_fixture(repo)
            source = repo / "tests" / "test_example.cpp"
            source.write_text(
                "void TESTS() {\n"
                '  test::observed_doc_evidence["example_threshold"]++;\n'
                "}\n",
                encoding="utf-8",
            )
            failures = DOCS.evidence_attribution_failures(
                repo, self.fixture_contracts()
            )
            self.assertTrue(
                any("accesses documentation-evidence runtime internals directly" in item
                    for item in failures),
                failures,
            )

    def test_disabled_cpp_evidence_call_is_rejected(self) -> None:
        for source in (
            "#if 0\n"
            "CAMERA_IQ_DOC_EVIDENCE(example_threshold, check(true));\n"
            "#endif\n",
            "#i\\\nf 0\n"
            "CAMERA_IQ_DOC_EVIDENCE(example_threshold, check(true));\n"
            "#endif\n",
        ):
            with self.subTest(source=source):
                with tempfile.TemporaryDirectory() as temp:
                    repo = Path(temp)
                    self.write_valid_fixture(repo)
                    (repo / "tests" / "test_example.cpp").write_text(
                        source, encoding="utf-8"
                    )
                    failures = DOCS.evidence_attribution_failures(
                        repo, self.fixture_contracts()
                    )
                    self.assertTrue(
                        any(
                            "executable assertion count is 0" in item
                            for item in failures
                        ),
                        failures,
                    )

    def test_spliced_comment_and_multiline_macro_are_not_evidence(self) -> None:
        for source in (
            "// explanation \\\n"
            "CAMERA_IQ_DOC_EVIDENCE(example_threshold, check(true));\n",
            "#define FAKE_EVIDENCE \\\n"
            "CAMERA_IQ_DOC_EVIDENCE(example_threshold, check(true))\n",
        ):
            with self.subTest(source=source):
                with tempfile.TemporaryDirectory() as temp:
                    repo = Path(temp)
                    self.write_valid_fixture(repo)
                    (repo / "tests" / "test_example.cpp").write_text(
                        source, encoding="utf-8"
                    )
                    failures = DOCS.evidence_attribution_failures(
                        repo, self.fixture_contracts()
                    )
                    self.assertTrue(
                        any(
                            "executable assertion count is 0" in item
                            for item in failures
                        ),
                        failures,
                    )

    def test_non_assertion_wrapper_expression_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            repo = Path(temp)
            self.write_valid_fixture(repo)
            (repo / "tests" / "test_example.cpp").write_text(
                "CAMERA_IQ_DOC_EVIDENCE(example_threshold, 0);\n",
                encoding="utf-8",
            )
            failures = DOCS.evidence_attribution_failures(
                repo, self.fixture_contracts()
            )
            self.assertTrue(
                any("executable assertion count is 0" in item for item in failures),
                failures,
            )

    def test_noop_evidence_macro_is_rejected(self) -> None:
        for content in (
            "#define CAMERA_IQ_DOC_EVIDENCE(evidence_id, assertion) "
            "((void)0)\n",
            "#if 0\n"
            "#define CAMERA_IQ_DOC_EVIDENCE(evidence_id, assertion) assertion\n"
            "#endif\n",
        ):
            with self.subTest(content=content):
                with tempfile.TemporaryDirectory() as temp:
                    repo = Path(temp)
                    self.write_valid_fixture(repo)
                    (repo / "tests" / "harness.hpp").write_text(
                        content, encoding="utf-8"
                    )
                    failures = DOCS.evidence_attribution_failures(
                        repo, self.fixture_contracts()
                    )
                    self.assertTrue(
                        any("evidence macro must record and execute" in item
                            for item in failures),
                        failures,
                    )

    def test_local_evidence_macro_redefinition_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            repo = Path(temp)
            self.write_valid_fixture(repo)
            (repo / "tests" / "test_example.cpp").write_text(
                "#undef CAMERA_IQ_DOC_EVIDENCE\n"
                "#define CAMERA_IQ_DOC_EVIDENCE(evidence_id, assertion) "
                "((void)0)\n"
                "CAMERA_IQ_DOC_EVIDENCE(example_threshold, check(true));\n",
                encoding="utf-8",
            )
            failures = DOCS.evidence_attribution_failures(
                repo, self.fixture_contracts()
            )
            self.assertTrue(
                any("must not be redefined" in item for item in failures),
                failures,
            )

    def test_directive_text_in_comment_does_not_hide_executable_evidence(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as temp:
            repo = Path(temp)
            self.write_valid_fixture(repo)
            (repo / "tests" / "test_example.cpp").write_text(
                "/* example only\n"
                "#if 0\n"
                "*/\n"
                "void TESTS() {\n"
                "  CAMERA_IQ_DOC_EVIDENCE(example_threshold, check(true));\n"
                "}\n",
                encoding="utf-8",
            )
            self.assertEqual(
                [],
                DOCS.evidence_attribution_failures(
                    repo, self.fixture_contracts()
                ),
            )

    def test_fenced_markdown_marker_is_not_a_claim(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            repo = Path(temp)
            self.write_valid_fixture(repo)
            document = repo / "docs" / "implementation" / "example.md"
            document.write_text(
                "```markdown\n"
                "<!-- test-evidence: example_threshold -->\n"
                "```\n",
                encoding="utf-8",
            )
            failures = DOCS.evidence_attribution_failures(
                repo, self.fixture_contracts()
            )
            self.assertTrue(
                any("document marker count is 0" in item for item in failures),
                failures,
            )

    def test_invalid_fence_close_and_container_marker_are_not_claims(
        self,
    ) -> None:
        for text in (
            "```markdown\n"
            "```not-a-close\n"
            "<!-- test-evidence: example_threshold -->\n"
            "```\n",
            "> ```markdown\n"
            "> <!-- test-evidence: example_threshold -->\n"
            "> ```\n",
            "    <!-- test-evidence: example_threshold -->\n",
        ):
            with self.subTest(text=text):
                with tempfile.TemporaryDirectory() as temp:
                    repo = Path(temp)
                    self.write_valid_fixture(repo)
                    (repo / "docs" / "implementation" / "example.md").write_text(
                        text, encoding="utf-8"
                    )
                    failures = DOCS.evidence_attribution_failures(
                        repo, self.fixture_contracts()
                    )
                    self.assertTrue(
                        any("document marker count is 0" in item
                            for item in failures),
                        failures,
                    )

    def test_unregistered_executable_evidence_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            repo = Path(temp)
            test = repo / "tests" / "test_example.cpp"
            test.parent.mkdir(parents=True)
            test.write_text(
                "CAMERA_IQ_DOC_EVIDENCE(unregistered_claim, check(true));\n",
                encoding="utf-8",
            )
            failures = DOCS.evidence_attribution_failures(repo, {})
            self.assertTrue(
                any("unregistered test evidence marker" in item for item in failures),
                failures,
            )

    def test_evidence_macro_definition_is_not_an_assertion(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            repo = Path(temp)
            header = repo / "tests" / "harness.hpp"
            header.parent.mkdir(parents=True)
            header.write_text(
                "#define CAMERA_IQ_DOC_EVIDENCE(evidence_id, assertion) "
                "assertion\n",
                encoding="utf-8",
            )
            self.assertEqual(
                [], DOCS.evidence_attribution_failures(repo, {})
            )

    def test_comment_marker_is_not_executable_evidence(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            repo = Path(temp)
            self.write_valid_fixture(repo)
            (repo / "tests" / "test_example.cpp").write_text(
                "// DOC-EVIDENCE: example_threshold\ncheck(value == 1);\n",
                encoding="utf-8",
            )
            failures = DOCS.evidence_attribution_failures(
                repo, self.fixture_contracts()
            )
            self.assertTrue(
                any("executable assertion count is 0" in item for item in failures),
                failures,
            )

    def test_contract_requires_every_executable_evidence_assertion(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            repo = Path(temp)
            document = repo / "docs" / "implementation" / "example.md"
            document.parent.mkdir(parents=True)
            document.write_text(
                "Two boundaries are pinned by "
                "[`test_example.cpp`](../../tests/test_example.cpp).\n"
                "<!-- test-evidence: example_threshold -->\n",
                encoding="utf-8",
            )
            test = repo / "tests" / "test_example.cpp"
            test.parent.mkdir(parents=True)
            test.write_text(
                "void TESTS() {\n"
                "  CAMERA_IQ_DOC_EVIDENCE(example_threshold, check(low));\n"
                "  CAMERA_IQ_DOC_EVIDENCE(example_threshold, check(high));\n"
                "}\n",
                encoding="utf-8",
            )
            self.write_evidence_macro(repo)
            contract = DOCS.EvidenceAttributionContract(
                document=Path("docs/implementation/example.md"),
                test=Path("tests/test_example.cpp"),
                assertion_count=2,
            )
            contracts = {"example_threshold": contract}
            self.assertEqual(
                [], DOCS.evidence_attribution_failures(repo, contracts)
            )
            test.write_text(
                "void TESTS() {\n"
                "  CAMERA_IQ_DOC_EVIDENCE(example_threshold, check(low));\n"
                "}\n",
                encoding="utf-8",
            )
            failures = DOCS.evidence_attribution_failures(repo, contracts)
            self.assertTrue(
                any("executable assertion count is 1" in item for item in failures),
                failures,
            )

    def test_contract_requires_its_test_target_to_be_registered(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            repo = Path(temp)
            document = repo / "docs" / "implementation" / "example.md"
            document.parent.mkdir(parents=True)
            document.write_text(
                "The threshold is pinned by "
                "[`test_example.cpp`](../../tests/test_example.cpp).\n"
                "<!-- test-evidence: example_threshold -->\n",
                encoding="utf-8",
            )
            test = repo / "tests" / "test_example.cpp"
            test.parent.mkdir(parents=True)
            test.write_text(
                "void TESTS() {\n"
                "  CAMERA_IQ_DOC_EVIDENCE(example_threshold, check(value));\n"
                "}\n",
                encoding="utf-8",
            )
            self.write_evidence_macro(repo)
            contract = DOCS.EvidenceAttributionContract(
                document=Path("docs/implementation/example.md"),
                test=Path("tests/test_example.cpp"),
                test_target="test_example",
            )
            failures = DOCS.evidence_attribution_failures(
                repo, {"example_threshold": contract}
            )
            self.assertTrue(
                any("is not registered with CTest" in item for item in failures),
                failures,
            )
            (repo / "CMakeLists.txt").write_text(
                "camera_iq_add_test(test_example tests/test_example.cpp)\n",
                encoding="utf-8",
            )
            failures = DOCS.evidence_attribution_failures(
                repo, {"example_threshold": contract}
            )
            self.assertTrue(
                any("does not have active add_executable" in item
                    for item in failures),
                failures,
            )
            (repo / "CMakeLists.txt").write_text(
                "function(camera_iq_add_test name source)\n"
                "  add_executable(${name} tests/other.cpp)\n"
                "  add_test(NAME ${name} COMMAND ${name})\n"
                "endfunction()\n"
                "camera_iq_add_test(test_example tests/test_example.cpp)\n",
                encoding="utf-8",
            )
            failures = DOCS.evidence_attribution_failures(
                repo, {"example_threshold": contract}
            )
            self.assertTrue(
                any("does not have active add_executable" in item
                    for item in failures),
                failures,
            )
            self.write_cmake_registration(repo)
            self.assertEqual(
                [],
                DOCS.evidence_attribution_failures(
                    repo, {"example_threshold": contract}
                ),
            )

    def test_commented_or_conditional_ctest_registration_is_rejected(
        self,
    ) -> None:
        for cmake in (
            "# camera_iq_add_test(test_example tests/test_example.cpp)\n",
            "if(FALSE)\n"
            "  camera_iq_add_test(test_example tests/test_example.cpp)\n"
            "endif()\n",
            "set(example [[camera_iq_add_test("
            "test_example tests/test_example.cpp)]])\n",
        ):
            with self.subTest(cmake=cmake):
                with tempfile.TemporaryDirectory() as temp:
                    repo = Path(temp)
                    self.write_valid_fixture(repo)
                    (repo / "CMakeLists.txt").write_text(cmake, encoding="utf-8")
                    contract = DOCS.EvidenceAttributionContract(
                        document=Path("docs/implementation/example.md"),
                        test=Path("tests/test_example.cpp"),
                        test_target="test_example",
                    )
                    failures = DOCS.evidence_attribution_failures(
                        repo, {"example_threshold": contract}
                    )
                    self.assertTrue(
                        any("is not registered with CTest" in item
                            for item in failures),
                        failures,
                    )

    def test_ctest_registration_inside_uncalled_block_is_rejected(self) -> None:
        for opening, closing in (
            ("function(unused)", "endfunction()"),
            ("macro(unused)", "endmacro()"),
            ("while(FALSE)", "endwhile()"),
            ("foreach(unused RANGE 0 -1)", "endforeach()"),
        ):
            with self.subTest(opening=opening):
                with tempfile.TemporaryDirectory() as temp:
                    repo = Path(temp)
                    self.write_valid_fixture(repo)
                    (repo / "CMakeLists.txt").write_text(
                        "function(camera_iq_add_test name source)\n"
                        "  add_executable(${name} ${source})\n"
                        "  add_test(NAME ${name} COMMAND ${name})\n"
                        "endfunction()\n"
                        f"{opening}\n"
                        "  camera_iq_add_test(test_example "
                        "tests/test_example.cpp)\n"
                        f"{closing}\n",
                        encoding="utf-8",
                    )
                    contract = DOCS.EvidenceAttributionContract(
                        document=Path("docs/implementation/example.md"),
                        test=Path("tests/test_example.cpp"),
                        test_target="test_example",
                    )
                    failures = DOCS.evidence_attribution_failures(
                        repo, {"example_threshold": contract}
                    )
                    self.assertTrue(
                        any("is not registered with CTest" in item
                            for item in failures),
                        failures,
                    )

    def test_runtime_expectation_count_must_match_contract(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            repo = Path(temp)
            self.write_valid_fixture(repo)
            self.write_cmake_registration(repo)
            cmake = repo / "CMakeLists.txt"
            cmake.write_text(
                cmake.read_text(encoding="utf-8").replace(
                    "example_threshold=1", "example_threshold=2"
                ),
                encoding="utf-8",
            )
            contract = DOCS.EvidenceAttributionContract(
                document=Path("docs/implementation/example.md"),
                test=Path("tests/test_example.cpp"),
                test_target="test_example",
            )
            failures = DOCS.evidence_attribution_failures(
                repo, {"example_threshold": contract}
            )
            self.assertTrue(
                any("do not match the registered claim counts" in item
                    for item in failures),
                failures,
            )

    def test_runtime_evidence_helper_cannot_be_overridden(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            repo = Path(temp)
            self.write_valid_fixture(repo)
            self.write_cmake_registration(repo)
            cmake = repo / "CMakeLists.txt"
            cmake.write_text(
                cmake.read_text(encoding="utf-8").replace(
                    "camera_iq_add_test(test_example",
                    "macro(camera_iq_expect_doc_evidence target expectations)\n"
                    "endmacro()\n"
                    "camera_iq_add_test(test_example",
                ),
                encoding="utf-8",
            )
            contract = DOCS.EvidenceAttributionContract(
                document=Path("docs/implementation/example.md"),
                test=Path("tests/test_example.cpp"),
                test_target="test_example",
            )
            failures = DOCS.evidence_attribution_failures(
                repo, {"example_threshold": contract}
            )
            self.assertTrue(
                any("do not have dedicated runtime checks" in item
                    for item in failures),
                failures,
            )

    def test_registered_target_cannot_override_runtime_properties(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            repo = Path(temp)
            self.write_valid_fixture(repo)
            self.write_cmake_registration(repo)
            cmake = repo / "CMakeLists.txt"
            cmake.write_text(
                cmake.read_text(encoding="utf-8")
                + "set_tests_properties(test_example PROPERTIES DISABLED TRUE)\n",
                encoding="utf-8",
            )
            contract = DOCS.EvidenceAttributionContract(
                document=Path("docs/implementation/example.md"),
                test=Path("tests/test_example.cpp"),
                test_target="test_example",
            )
            failures = DOCS.evidence_attribution_failures(
                repo, {"example_threshold": contract}
            )
            self.assertTrue(
                any("has test properties outside" in item for item in failures),
                failures,
            )

    def test_later_global_environment_cannot_erase_runtime_expectations(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            repo = Path(temp)
            self.write_valid_fixture(repo)
            self.write_cmake_registration(repo)
            cmake = repo / "CMakeLists.txt"
            cmake.write_text(
                cmake.read_text(encoding="utf-8")
                + "set_tests_properties(${ALL_TESTS} PROPERTIES "
                "ENVIRONMENT TMPDIR=/tmp)\n",
                encoding="utf-8",
            )
            contract = DOCS.EvidenceAttributionContract(
                document=Path("docs/implementation/example.md"),
                test=Path("tests/test_example.cpp"),
                test_target="test_example",
            )
            failures = DOCS.evidence_attribution_failures(
                repo, {"example_threshold": contract}
            )
            self.assertTrue(
                any("must be installed after all direct CTest property" in item
                    for item in failures),
                failures,
            )

    def test_indirect_mutator_cannot_follow_runtime_expectation_block(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            repo = Path(temp)
            self.write_valid_fixture(repo)
            self.write_cmake_registration(repo)
            cmake = repo / "CMakeLists.txt"
            cmake.write_text(
                cmake.read_text(encoding="utf-8").replace(
                    "camera_iq_expect_doc_evidence(test_example",
                    "function(disable_evidence)\n"
                    "  set_tests_properties(check_doc_evidence_test_example "
                    "PROPERTIES DISABLED TRUE)\n"
                    "endfunction()\n"
                    "camera_iq_expect_doc_evidence(test_example",
                )
                + "disable_evidence()\n",
                encoding="utf-8",
            )
            contract = DOCS.EvidenceAttributionContract(
                document=Path("docs/implementation/example.md"),
                test=Path("tests/test_example.cpp"),
                test_target="test_example",
            )
            failures = DOCS.evidence_attribution_failures(
                repo, {"example_threshold": contract}
            )
            self.assertTrue(
                any("must form the final active CMake command block" in item
                    for item in failures),
                failures,
            )

    def test_ctest_helper_body_cannot_be_borrowed_or_redefined(self) -> None:
        for cmake in (
            "function(camera_iq_add_test name source)\n"
            "endfunction()\n"
            "function(other_helper name source)\n"
            "  add_executable(${name} ${source})\n"
            "  add_test(NAME ${name} COMMAND ${name})\n"
            "endfunction()\n"
            "camera_iq_add_test(test_example tests/test_example.cpp)\n",
            "function(camera_iq_add_test name source)\n"
            "  add_executable(${name} ${source})\n"
            "  add_test(NAME ${name} COMMAND ${name})\n"
            "endfunction()\n"
            "function(camera_iq_add_test name source)\n"
            "endfunction()\n"
            "camera_iq_add_test(test_example tests/test_example.cpp)\n",
        ):
            with self.subTest(cmake=cmake):
                with tempfile.TemporaryDirectory() as temp:
                    repo = Path(temp)
                    self.write_valid_fixture(repo)
                    (repo / "CMakeLists.txt").write_text(cmake, encoding="utf-8")
                    contract = DOCS.EvidenceAttributionContract(
                        document=Path("docs/implementation/example.md"),
                        test=Path("tests/test_example.cpp"),
                        test_target="test_example",
                    )
                    failures = DOCS.evidence_attribution_failures(
                        repo, {"example_threshold": contract}
                    )
                    self.assertTrue(
                        any("does not have active add_executable" in item
                            for item in failures),
                        failures,
                    )

    def test_matching_claim_link_and_test_marker_pass(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            repo = Path(temp)
            self.write_valid_fixture(repo)
            self.assertEqual(
                [],
                DOCS.evidence_attribution_failures(
                    repo, self.fixture_contracts()
                ),
            )

    def test_wrong_but_existing_test_link_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            repo = Path(temp)
            self.write_valid_fixture(repo)
            other = repo / "tests" / "test_other.cpp"
            other.write_text("check(true);\n", encoding="utf-8")
            document = repo / "docs" / "implementation" / "example.md"
            document.write_text(
                document.read_text(encoding="utf-8").replace(
                    "../../tests/test_example.cpp",
                    "../../tests/test_other.cpp",
                ),
                encoding="utf-8",
            )
            failures = DOCS.evidence_attribution_failures(
                repo, self.fixture_contracts()
            )
            self.assertTrue(
                any("claim paragraph does not link its registered test" in item
                    for item in failures),
                failures,
            )

    def test_registered_claim_paragraph_cannot_mix_test_files(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            repo = Path(temp)
            self.write_valid_fixture(repo)
            other = repo / "tests" / "test_other.cpp"
            other.write_text("check(true);\n", encoding="utf-8")
            document = repo / "docs" / "implementation" / "example.md"
            document.write_text(
                "One result is in "
                "[`test_example.cpp`](../../tests/test_example.cpp), while "
                "another is in "
                "[`test_other.cpp`](../../tests/test_other.cpp).\n"
                "<!-- test-evidence: example_threshold -->\n",
                encoding="utf-8",
            )
            failures = DOCS.evidence_attribution_failures(
                repo, self.fixture_contracts()
            )
            self.assertTrue(
                any("mixes multiple test files" in item for item in failures),
                failures,
            )

    def test_marker_in_wrong_test_file_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            repo = Path(temp)
            self.write_valid_fixture(repo)
            expected = repo / "tests" / "test_example.cpp"
            expected.write_text("check(value == 1);\n", encoding="utf-8")
            (repo / "tests" / "test_other.cpp").write_text(
                "CAMERA_IQ_DOC_EVIDENCE(example_threshold, check(true));\n",
                encoding="utf-8",
            )
            failures = DOCS.evidence_attribution_failures(
                repo, self.fixture_contracts()
            )
            self.assertTrue(
                any("test marker is not in its registered file" in item
                    for item in failures),
                failures,
            )

    def test_duplicate_document_marker_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            repo = Path(temp)
            self.write_valid_fixture(repo)
            document = repo / "docs" / "implementation" / "example.md"
            document.write_text(
                document.read_text(encoding="utf-8")
                + "\n<!-- test-evidence: example_threshold -->\n",
                encoding="utf-8",
            )
            failures = DOCS.evidence_attribution_failures(
                repo, self.fixture_contracts()
            )
            self.assertTrue(
                any("document marker count is 2" in item for item in failures),
                failures,
            )

    def test_unregistered_marker_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            repo = Path(temp)
            self.write_valid_fixture(repo)
            document = repo / "docs" / "implementation" / "example.md"
            document.write_text(
                document.read_text(encoding="utf-8")
                + "\n<!-- test-evidence: example_typo -->\n",
                encoding="utf-8",
            )
            failures = DOCS.evidence_attribution_failures(
                repo, self.fixture_contracts()
            )
            self.assertTrue(
                any("unregistered document evidence marker" in item
                    for item in failures),
                failures,
            )

    def test_current_critical_claims_have_machine_checked_attribution(self) -> None:
        contracts = DOCS.EVIDENCE_ATTRIBUTION_CONTRACTS
        self.assertEqual(
            {
                "color_characterization_localization_shifted",
                "color_characterization_localization_offset",
                "color_characterization_localization_verdict",
                "color_characterization_ccm_external_labels",
                "color_characterization_ccm_provenance",
                "color_characterization_ccm_refusals",
                "flat_field_radial_asymmetry",
                "flat_field_cfa_balanced_roi",
                "flat_field_threshold_boundaries",
                "sfr_broad_gaussian_bounds",
                "sfr_nyquist_accuracy",
                "spectral_fidelity_luther_scale_invariance",
                "raw_foundation_row_pitch",
                "raw_foundation_black_2x2",
                "raw_foundation_black_repeat_periodicity",
            },
            set(contracts),
        )
        self.assertTrue(
            all(contract.test_target for contract in contracts.values())
        )
        self.assertTrue(
            all(contract.assertion_count > 0 for contract in contracts.values())
        )
        repo_root = SCRIPT.parent.parent
        self.assertEqual(
            [], DOCS.evidence_attribution_failures(repo_root, contracts)
        )


class DocumentationLayerTests(unittest.TestCase):
    def test_current_studies_and_reports_link_to_companions(self) -> None:
        repo_root = SCRIPT.parent.parent
        self.assertEqual([], DOCS.implementation_link_failures(repo_root))

    def test_removing_companion_link_is_detected(self) -> None:
        relative = Path("docs/case-studies/cfa-flat-field-response.md")
        repo_root = SCRIPT.parent.parent
        text = (repo_root / relative).read_text(encoding="utf-8")
        expected = DOCS.IMPLEMENTATION_COMPANION_LINKS[relative]
        mutated = text.replace(f"]({expected})", "](missing.md)")
        failures = DOCS.implementation_link_failures_for_text(relative, mutated)
        self.assertTrue(any("missing implementation companion link" in item
                            for item in failures), failures)

    def test_report_requires_one_engineering_companion_section(self) -> None:
        relative = Path("docs/reports/SFR_MTF.md")
        repo_root = SCRIPT.parent.parent
        text = (repo_root / relative).read_text(encoding="utf-8")
        mutated = text.replace("## Engineering companion", "## Software note")
        failures = DOCS.implementation_link_failures_for_text(relative, mutated)
        self.assertTrue(any("engineering companion section" in item
                            for item in failures), failures)

    def test_scientific_report_rejects_software_operation_sections(self) -> None:
        relative = Path("docs/reports/EXAMPLE.md")
        examples = (
            "## Reproduce\n\n```bash\nctest --test-dir build\n```\n",
            "[test](../../tests/test_example.cpp)\n",
            "Tool: `camera_iq example`\n",
            "The command emits a summary.\n",
            "Use `--threshold 0.5` for this run.\n",
            "```json\n{\"accepted\": true}\n```\n",
            "The JSON records the effective threshold.\n",
            "The JSON deliberately separates two questions.\n",
            "The analyzer reports rectangles in JSON.\n",
            "Accepted patches JSON retain the gate diagnostics.\n",
            "The result is serialized as `uniform_equal_weight`.\n",
            "All files used schema-3 diagnostics.\n",
            "## Parser boundary\n",
            "The parser reads all retained rows.\n",
            "The parser is deliberately table-scoped.\n",
            "This verifies that the parser sees archive metadata.\n",
            "The signal is transformed with an in-repo DFT.\n",
            "Pixels come from LibRaw `rawdata.raw_image` after `unpack()`.\n",
            "The result emits `dsnu_below_temporal_floor`.\n",
            "The pedestal uses `cblack[6..]` and `sizes.raw_pitch`.\n",
            "The report measures post-unpack black metadata.\n",
        )
        for text in examples:
            with self.subTest(text=text):
                self.assertTrue(
                    DOCS.report_layer_failures_for_text(relative, text)
                )

    def test_scientific_report_allows_formulas_and_measurement_crosschecks(self) -> None:
        relative = Path("docs/reports/EXAMPLE.md")
        text = (
            "## Method\n\n"
            "```text\nresponse = signal / center_signal\n```\n\n"
            "## Measurement cross-check\n\nThe independent reference agreed.\n"
        )
        self.assertEqual(
            [], DOCS.report_layer_failures_for_text(relative, text)
        )

    def test_engineering_companion_heading_cannot_hide_software_details(self) -> None:
        relative = Path("docs/reports/EXAMPLE.md")
        text = (
            "## Engineering companion\n\n"
            "The [companion](../implementation/example.md) is linked here. "
            "The parser reads a private schema.\n"
        )
        self.assertTrue(DOCS.report_layer_failures_for_text(relative, text))

    def test_engineering_companion_link_without_inventory_is_allowed(self) -> None:
        relative = Path("docs/reports/EXAMPLE.md")
        text = (
            "## Engineering companion\n\n"
            "The [companion](../implementation/example.md) explains how the "
            "method is realized in C++ and routes readers to the public source.\n"
        )
        self.assertEqual([], DOCS.report_layer_failures_for_text(relative, text))

    def test_current_implementation_companions_explain_verification_evidence(self) -> None:
        repo_root = SCRIPT.parent.parent
        self.assertEqual([], DOCS.implementation_evidence_failures(repo_root))

    def test_implementation_companion_requires_a_test_link(self) -> None:
        relative = Path("docs/implementation/example.md")
        text = (
            "Synthetic tests exercise numeric fixtures, invariants, and "
            "rejection behavior.\n"
        )
        failures = DOCS.implementation_evidence_failures_for_text(relative, text)
        self.assertTrue(
            any("public test link" in item for item in failures), failures
        )

    def test_evidence_section_requires_its_own_test_link(self) -> None:
        relative = Path("docs/implementation/example.md")
        text = (
            "## Verification evidence\n\n"
            "Synthetic fixtures establish a 1e-6 numeric bound. They do not "
            "establish the physical validity of a capture.\n\n"
            "## Source and tests\n\n"
            "- Test: [test_example.cpp](../../tests/test_example.cpp)\n"
        )
        failures = DOCS.implementation_evidence_failures_for_text(relative, text)
        self.assertTrue(
            any("evidence section missing public test link" in item
                for item in failures),
            failures,
        )

    def test_evidence_section_requires_a_pinned_assertion(self) -> None:
        relative = Path("docs/implementation/example.md")
        text = (
            "## Verification evidence\n\n"
            "Synthetic tests exercise useful behavior and rejection paths. "
            "They do not establish the physical validity of a capture.\n\n"
            "- Test: [test_example.cpp](../../tests/test_example.cpp)\n"
        )
        failures = DOCS.implementation_evidence_failures_for_text(relative, text)
        self.assertTrue(
            any("numeric assertion or semantic contract" in item
                for item in failures),
            failures,
        )

    def test_implementation_companion_requires_evidence_role_prose(self) -> None:
        relative = Path("docs/implementation/example.md")
        text = "- Tests: [test_example.cpp](../../tests/test_example.cpp)\n"
        failures = DOCS.implementation_evidence_failures_for_text(relative, text)
        self.assertTrue(
            any("verification evidence explanation" in item for item in failures),
            failures,
        )

    def test_implementation_evidence_contract_allows_rewording_and_new_numbers(self) -> None:
        relative = Path("docs/implementation/example.md")
        text = (
            "## Evidence from verification\n\n"
            "The test suite challenges analytic invariants, malformed-input "
            "refusals, and a 4,096-point numerical fixture. It establishes "
            "software behavior, not the physical validity of a capture.\n\n"
            "- Test: [test_example.cpp](../../tests/test_example.cpp)\n"
        )
        self.assertEqual(
            [], DOCS.implementation_evidence_failures_for_text(relative, text)
        )

    def test_implementation_companion_requires_an_evidence_section(self) -> None:
        # Evidence stranded in an unrelated section reads as covered while being
        # one deletion away from gone. A dedicated heading makes its removal
        # visible in a diff.
        relative = Path("docs/implementation/example.md")
        text = (
            "## Source and tests\n\n"
            "- Test: [test_example.cpp](../../tests/test_example.cpp)\n\n"
            "Synthetic tests cover analytic invariants and rejection paths. "
            "The scientific report remains the authority for physical claims.\n"
        )
        failures = DOCS.implementation_evidence_failures_for_text(relative, text)
        self.assertTrue(
            any("verification-evidence section" in item for item in failures),
            failures,
        )

    def test_empty_evidence_section_cannot_borrow_prose_from_elsewhere(self) -> None:
        relative = Path("docs/implementation/example.md")
        text = (
            "## Verification evidence\n\n"
            "## Source and tests\n\n"
            "- Test: [test_example.cpp](../../tests/test_example.cpp)\n\n"
            "Synthetic tests cover analytic invariants and rejection paths. "
            "The scientific report remains the authority for physical claims.\n"
        )
        failures = DOCS.implementation_evidence_failures_for_text(relative, text)
        self.assertTrue(
            any("verification evidence explanation" in item for item in failures),
            failures,
        )

    def test_evidence_section_requires_a_scientific_limitation(self) -> None:
        # A verification inventory without its scientific boundary invites the
        # reader to treat software behavior as physical validation.
        relative = Path("docs/implementation/example.md")
        text = (
            "## Verification evidence\n\n"
            "Synthetic tests establish analytic invariants, numeric bounds, "
            "and malformed-input rejection behavior.\n\n"
            "- Test: [test_example.cpp](../../tests/test_example.cpp)\n"
        )
        failures = DOCS.implementation_evidence_failures_for_text(relative, text)
        self.assertTrue(
            any("scientific limitation" in item for item in failures),
            failures,
        )

    def test_algorithm_negation_is_not_a_scientific_limitation(self) -> None:
        relative = Path("docs/implementation/example.md")
        text = (
            "## Verification evidence\n\n"
            "Display-P3 mapping tests establish numeric bounds. Mapped outputs "
            "do not increase chroma, and the algorithms do not use LittleCMS.\n\n"
            "- Test: [test_example.cpp](../../tests/test_example.cpp)\n"
        )
        failures = DOCS.implementation_evidence_failures_for_text(relative, text)
        self.assertTrue(
            any("scientific limitation" in item for item in failures),
            failures,
        )

    def test_evidence_section_heading_wording_is_not_pinned(self) -> None:
        relative = Path("docs/implementation/example.md")
        text = (
            "## What the fixtures establish, and what they do not\n\n"
            "Synthetic tests cover 3 analytic invariants and rejection paths. "
            "They do not establish the physical validity of a capture.\n\n"
            "- Test: [test_example.cpp](../../tests/test_example.cpp)\n"
        )
        self.assertEqual(
            [], DOCS.implementation_evidence_failures_for_text(relative, text)
        )
        reworded = text.replace(
            "What the fixtures establish, and what they do not",
            "How the tests verify the boundary",
        )
        self.assertEqual(
            [], DOCS.implementation_evidence_failures_for_text(relative, reworded)
        )

    def test_new_implementation_companion_is_covered_without_registration(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            repo = Path(temp)
            companion = repo / "docs" / "implementation" / "new-method.md"
            companion.parent.mkdir(parents=True)
            companion.write_text(
                "# New method\n\n"
                "- Test: [test_new.cpp](../../tests/test_new.cpp)\n",
                encoding="utf-8",
            )
            failures = DOCS.implementation_evidence_failures(repo)
        self.assertTrue(
            any("new-method.md" in item for item in failures), failures
        )

    def test_current_case_study_and_report_figures_have_captions(self) -> None:
        repo_root = SCRIPT.parent.parent
        failures = []
        for folder in ("case-studies", "reports"):
            for path in (repo_root / "docs" / folder).glob("*.md"):
                failures.extend(
                    DOCS.figure_caption_failures_for_text(
                        path.relative_to(repo_root), path.read_text(encoding="utf-8")
                    )
                )
        self.assertEqual([], failures)

    def test_uncaptioned_figure_is_rejected(self) -> None:
        relative = Path("docs/reports/EXAMPLE.md")
        text = "![Plot](../figures/plot.svg)\n\n## Results\n"
        failures = DOCS.figure_caption_failures_for_text(relative, text)
        self.assertTrue(any("figure missing adjacent caption" in item
                            for item in failures), failures)

    def test_captioned_figure_is_allowed(self) -> None:
        relative = Path("docs/case-studies/EXAMPLE.md")
        text = "![Plot](../figures/plot.svg)\n\n*Axes and marks explained.*\n"
        self.assertEqual(
            [], DOCS.figure_caption_failures_for_text(relative, text)
        )

class HeadingAnchorTests(unittest.TestCase):
    """A link to `FILE.md#section` is only checked as far as FILE.md unless the
    fragment is resolved too, so renaming a heading breaks cross-references
    silently."""

    def test_slug_matches_github_heading_rules(self) -> None:
        self.assertEqual(
            DOCS.heading_slug("## The center gate applies wherever a flat normalizes"),
            "the-center-gate-applies-wherever-a-flat-normalizes",
        )
        self.assertEqual(
            DOCS.heading_slug("### Instrument identity as the files record it"),
            "instrument-identity-as-the-files-record-it",
        )
        # Punctuation is dropped, not replaced, and `code` markers do not survive.
        self.assertEqual(
            DOCS.heading_slug("## `patches`, `shading`: one policy"),
            "patches-shading-one-policy",
        )

    def test_non_heading_returns_no_slug(self) -> None:
        self.assertIsNone(DOCS.heading_slug("Not a heading"))
        self.assertIsNone(DOCS.heading_slug("#NoSpaceAfterHash"))

    def test_document_anchors_collects_every_level(self) -> None:
        text = "# Title\n\nbody\n\n## Section One\n\n### Sub Two\n"
        self.assertEqual(
            DOCS.document_anchors(text), {"title", "section-one", "sub-two"}
        )

    def test_missing_anchor_is_reported(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            repo = Path(temp)
            subprocess.run(["git", "init", "--quiet"], cwd=repo, check=True)
            docs = repo / "docs"
            docs.mkdir()
            (docs / "target.md").write_text("# Target\n\n## Real Section\n",
                                            encoding="utf-8")
            (docs / "source.md").write_text(
                "[ok](target.md#real-section)\n[bad](target.md#renamed-section)\n",
                encoding="utf-8",
            )
            problems = DOCS.anchor_failures(repo, docs / "source.md")
            self.assertEqual(len(problems), 1)
            self.assertIn("renamed-section", problems[0])

    def test_same_document_anchor_is_resolved(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            repo = Path(temp)
            subprocess.run(["git", "init", "--quiet"], cwd=repo, check=True)
            docs = repo / "docs"
            docs.mkdir()
            page = docs / "page.md"
            page.write_text("## Present\n\n[here](#present)\n[gone](#absent)\n",
                            encoding="utf-8")
            problems = DOCS.anchor_failures(repo, page)
            self.assertEqual(len(problems), 1)
            self.assertIn("absent", problems[0])


if __name__ == "__main__":
    unittest.main()
