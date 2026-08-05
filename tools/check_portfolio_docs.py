#!/usr/bin/env python3
"""Validate the public Markdown graph and documentation language rules."""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path
from typing import NamedTuple
from urllib.parse import unquote


LINK_RE = re.compile(r"!?\[[^\]]*\]\(([^)]+)\)")
DOCUMENT_EVIDENCE_RE = re.compile(
    r"<!--\s*test-evidence:\s*([a-z0-9][a-z0-9._-]*)\s*-->"
)
EXECUTABLE_TEST_EVIDENCE_RE = re.compile(
    r"\bCAMERA_IQ_DOC_EVIDENCE\(\s*([a-z][a-z0-9_]*)\s*,\s*"
    r"(?:(?:test::)?check(?:_near)?)\s*\("
)
CMAKE_CPP_TEST_RE = re.compile(
    r"\bcamera_iq_add_test\s*\(\s*([A-Za-z0-9_-]+)\s+([^\s)]+)\s*\)",
    re.MULTILINE,
)
CMAKE_TEST_HELPER_RE = re.compile(
    r"^[ \t]*function\s*\(\s*camera_iq_add_test\s+name\s+source\s*\)"
    r"(?P<body>.*?)"
    r"^[ \t]*endfunction(?:\s*\([^)]*\))?[ \t]*$",
    re.DOTALL | re.MULTILINE,
)
CMAKE_HELPER_ADD_EXECUTABLE_RE = re.compile(
    r"\badd_executable\s*\(\s*\$\{name\}\s+\$\{source\}\s*\)"
)
CMAKE_HELPER_ADD_TEST_RE = re.compile(
    r"\badd_test\s*\(\s*NAME\s+\$\{name\}\s+COMMAND\s+\$\{name\}\s*\)"
)
EVIDENCE_MACRO_DEFINITION_RE = re.compile(
    r"^[ \t]*#[ \t]*define[ \t]+CAMERA_IQ_DOC_EVIDENCE\("
    r"[ \t]*evidence_id[ \t]*,[ \t]*assertion[ \t]*\)"
    r"[ \t]+assertion[ \t]*$",
    re.MULTILINE,
)
ANY_EVIDENCE_MACRO_DEFINITION_RE = re.compile(
    r"^[ \t]*#[ \t]*define[ \t]+CAMERA_IQ_DOC_EVIDENCE\b",
    re.MULTILINE,
)
EVIDENCE_MACRO_MUTATION_RE = re.compile(
    r"^[ \t]*#[ \t]*(?:define|undef)[ \t]+CAMERA_IQ_DOC_EVIDENCE\b",
    re.MULTILINE,
)


class EvidenceAttributionContract(NamedTuple):
    document: Path
    test: Path
    test_target: str = ""
    assertion_count: int = 1


EVIDENCE_ATTRIBUTION_CONTRACTS: dict[str, EvidenceAttributionContract] = {
    "color_characterization_localization_shifted": EvidenceAttributionContract(
        Path("docs/implementation/color-characterization.md"),
        Path("tests/test_patches.cpp"),
        "test_patches",
        3,
    ),
    "color_characterization_localization_offset": EvidenceAttributionContract(
        Path("docs/implementation/color-characterization.md"),
        Path("tests/test_patches.cpp"),
        "test_patches",
        3,
    ),
    "color_characterization_localization_verdict": EvidenceAttributionContract(
        Path("docs/implementation/color-characterization.md"),
        Path("tests/test_patches.cpp"),
        "test_patches",
        2,
    ),
    "color_characterization_ccm_external_labels": EvidenceAttributionContract(
        Path("docs/implementation/color-characterization.md"),
        Path("tests/test_cmd_ccm_fit.cpp"),
        "test_cmd_ccm_fit",
        2,
    ),
    "color_characterization_ccm_provenance": EvidenceAttributionContract(
        Path("docs/implementation/color-characterization.md"),
        Path("tests/test_cmd_ccm_fit.cpp"),
        "test_cmd_ccm_fit",
        3,
    ),
    "color_characterization_ccm_refusals": EvidenceAttributionContract(
        Path("docs/implementation/color-characterization.md"),
        Path("tests/test_cmd_ccm_fit.cpp"),
        "test_cmd_ccm_fit",
        6,
    ),
    "flat_field_radial_asymmetry": EvidenceAttributionContract(
        Path("docs/implementation/flat-field.md"),
        Path("tests/test_shading.cpp"),
        "test_shading",
        2,
    ),
    "flat_field_cfa_balanced_roi": EvidenceAttributionContract(
        Path("docs/implementation/flat-field.md"),
        Path("tests/test_flat_field_gate.cpp"),
        "test_flat_field_gate",
        4,
    ),
    "flat_field_threshold_boundaries": EvidenceAttributionContract(
        Path("docs/implementation/flat-field.md"),
        Path("tests/test_flat_field_gate.cpp"),
        "test_flat_field_gate",
        9,
    ),
    "sfr_broad_gaussian_bounds": EvidenceAttributionContract(
        Path("docs/implementation/sfr-mtf.md"),
        Path("tests/test_sfr.cpp"),
        "test_sfr",
        4,
    ),
    "sfr_nyquist_accuracy": EvidenceAttributionContract(
        Path("docs/implementation/sfr-mtf.md"),
        Path("tests/test_sfr.cpp"),
        "test_sfr",
        1,
    ),
    "spectral_fidelity_luther_scale_invariance": EvidenceAttributionContract(
        Path("docs/implementation/spectral-fidelity.md"),
        Path("tests/test_spectral_quality.cpp"),
        "test_spectral_quality",
        3,
    ),
    "raw_foundation_row_pitch": EvidenceAttributionContract(
        Path("docs/implementation/raw-foundation.md"),
        Path("tests/test_raw_meta.cpp"),
        "test_raw_meta",
        2,
    ),
    "raw_foundation_black_2x2": EvidenceAttributionContract(
        Path("docs/implementation/raw-foundation.md"),
        Path("tests/test_raw_meta.cpp"),
        "test_raw_meta",
        4,
    ),
    "raw_foundation_black_repeat_periodicity": EvidenceAttributionContract(
        Path("docs/implementation/raw-foundation.md"),
        Path("tests/test_raw_meta.cpp"),
        "test_raw_meta",
        6,
    ),
}
REQUIRED_PROJECT_DOCUMENTS = (
    Path("docs/README.md"),
    Path("docs/PUBLIC_DOCUMENTATION_STANDARD.md"),
    Path("docs/implementation/README.md"),
    Path("docs/implementation/raw-foundation.md"),
    Path("docs/implementation/sfr-mtf.md"),
    Path("docs/implementation/color-characterization.md"),
    Path("docs/implementation/flat-field.md"),
    Path("docs/implementation/spectral-fidelity.md"),
    Path("docs/implementation/spectroradiometer.md"),
    Path("docs/implementation/gamut-mapping.md"),
    Path("docs/implementation/color-model-audit.md"),
    Path("docs/case-studies/sfr-mtf-aperture-field.md"),
    Path("docs/case-studies/spectral-color-fidelity.md"),
    Path("docs/case-studies/colorchecker-ccm.md"),
    Path("docs/case-studies/cfa-flat-field-response.md"),
    Path("docs/case-studies/spectroradiometer-ingest.md"),
    Path("docs/case-studies/gamut-mapping.md"),
    Path("docs/case-studies/color-model-equation-audit.md"),
)

IMPLEMENTATION_TEST_LINK_RE = re.compile(
    r"\]\(\.\./\.\./tests/[^)#]+(?:#[^)]+)?\)", re.IGNORECASE
)
IMPLEMENTATION_EVIDENCE_SECTION_RE = re.compile(
    r"^#{2,3} [^\n]*(?:\bevidence\b|\bverification\b|"
    r"\b(?:tests?|fixtures?|cross-checks?)\b[^\n]{0,80}"
    r"\b(?:establish|verify|demonstrate)\b)[^\n]*$",
    re.IGNORECASE | re.MULTILINE,
)
IMPLEMENTATION_EVIDENCE_ROLE_RE = re.compile(
    r"\b(?:test(?:s|ed|ing)?|cross-check(?:s|ed|ing)?|fixtures?)\b"
    r".{0,420}"
    r"\b(?:algorithm(?:ic)?|analytic|numeric|invariants?|contracts?|"
    r"refusals?|reject(?:s|ed|ion)?|serialization|parser|geometry|synthetic|"
    r"independent|boundar(?:y|ies)|domains?|equations?|behavio(?:u)?r|"
    r"artifacts?|physical|archive)\w*\b|"
    r"\b(?:algorithm(?:ic)?|analytic|numeric|invariants?|contracts?|"
    r"refusals?|reject(?:s|ed|ion)?|serialization|parser|geometry|synthetic|"
    r"independent|boundar(?:y|ies)|domains?|equations?|behavio(?:u)?r|"
    r"artifacts?|physical|archive)\w*\b"
    r".{0,420}"
    r"\b(?:test(?:s|ed|ing)?|cross-check(?:s|ed|ing)?|fixtures?)\b",
    re.IGNORECASE,
)
IMPLEMENTATION_EVIDENCE_LIMIT_RE = re.compile(
    r"\b(?:does|do|did)\s+not\s+"
    r"(?:establish|prove|validate|remeasure|rerun|recover|inspect|identify|turn)\w*\b|"
    r"\bcannot\s+"
    r"(?:establish|prove|validate|remeasure|rerun|recover|inspect|identify)\w*\b|"
    r"\bneither\s+(?:is|are)\b.{0,160}"
    r"\b(?:validation|evidence|proof|measurement)\w*\b|"
    r"\bnot\s+(?:a|an|the)\s+"
    r"(?:claim|proof|validation|measurement|evidence)\w*\b|"
    r"\bnot\s+(?:a|an|the)\s+"
    r"(?:physical|perceptual|observer|capture|archive|instrument|display|"
    r"scene|measurement|scientific|causal)\w*"
    r"(?:\s+\w+){0,6}\s+"
    r"(?:validity|accuracy|proof|validation|evidence)\w*\b|"
    r"\b(?:reports?|measurements?|captures?)\b.{0,220}"
    r"\b(?:remain|remains)\s+(?:the\s+)?authority\b",
    re.IGNORECASE,
)
IMPLEMENTATION_EVIDENCE_ASSERTION_RE = re.compile(
    r"(?<![A-Za-z0-9_])\d[\d,]*(?:\.\d+)?(?:e[+-]?\d+)?"
    r"(?![A-Za-z0-9_])|"
    r"\b(?:reject(?:s|ed|ion)?|refus(?:e|es|ed|al)|preserv(?:e|es|ed)|"
    r"round[- ]trip(?:s|ped)?|mutation)\b.{0,120}`[^`]+`|"
    r"`[^`]+`.{0,120}\b(?:reject(?:s|ed|ion)?|refus(?:e|es|ed|al)|"
    r"preserv(?:e|es|ed)|round[- ]trip(?:s|ped)?|mutation)\b",
    re.IGNORECASE,
)

# Every public study and report must route implementation detail to one named
# companion. Exact links are intentional: a generic implementation index does
# not establish which architecture realizes a particular measurement.
IMPLEMENTATION_COMPANION_LINKS = {
    Path("docs/case-studies/sfr-mtf-aperture-field.md"): "../implementation/sfr-mtf.md",
    Path("docs/case-studies/spectral-color-fidelity.md"): "../implementation/spectral-fidelity.md",
    Path("docs/case-studies/colorchecker-ccm.md"): "../implementation/color-characterization.md",
    Path("docs/case-studies/cfa-flat-field-response.md"): "../implementation/flat-field.md",
    Path("docs/case-studies/spectroradiometer-ingest.md"): "../implementation/spectroradiometer.md",
    Path("docs/case-studies/gamut-mapping.md"): "../implementation/gamut-mapping.md",
    Path("docs/case-studies/color-model-equation-audit.md"): "../implementation/color-model-audit.md",
    Path("docs/reports/BILINEAR_DEMOSAIC.md"): "../implementation/raw-foundation.md",
    Path("docs/reports/CAM16_EQUATION_AUDIT.md"): "../implementation/color-model-audit.md",
    Path("docs/reports/CAMERA_IQ_COVERAGE.md"): "../implementation/README.md",
    Path("docs/reports/CCM_FIT.md"): "../implementation/color-characterization.md",
    Path("docs/reports/DARK_CALIBRATION.md"): "../implementation/raw-foundation.md",
    Path("docs/reports/DARK_FRAME_NOISE.md"): "../implementation/raw-foundation.md",
    Path("docs/reports/EXPOSURE_RESPONSE.md"): "../implementation/raw-foundation.md",
    Path("docs/reports/FLAT_FIELD_RESPONSE.md"): "../implementation/flat-field.md",
    Path("docs/reports/FUJI_XT100_CCSG_MANIFEST.md"): "../implementation/raw-foundation.md",
    Path("docs/reports/GAMUT_MAPPING.md"): "../implementation/gamut-mapping.md",
    Path("docs/reports/OECF_FIT.md"): "../implementation/raw-foundation.md",
    Path("docs/reports/OECF_STEPCHART.md"): "../implementation/raw-foundation.md",
    Path("docs/reports/PATCH_EXTRACTION.md"): "../implementation/color-characterization.md",
    Path("docs/reports/RAW_CHART_LOCALIZATION.md"): "../implementation/color-characterization.md",
    Path("docs/reports/RAW_STATS.md"): "../implementation/raw-foundation.md",
    Path("docs/reports/SFR_MTF.md"): "../implementation/sfr-mtf.md",
    Path("docs/reports/SFR_MTF_ARCHIVE_INVENTORY.md"): "../implementation/sfr-mtf.md",
    Path("docs/reports/SG_REFERENCE_PROVENANCE.md"): "../implementation/color-characterization.md",
    Path("docs/reports/SPECTRAL_ARCHIVE_INVENTORY.md"): "../implementation/spectral-fidelity.md",
    Path("docs/reports/SPECTRAL_SENSITIVITY.md"): "../implementation/spectral-fidelity.md",
    Path("docs/reports/SPECTRORADIOMETER_INGEST.md"): "../implementation/spectroradiometer.md",
}

REPORT_LAYER_PATTERNS = {
    "shell transcript in scientific report": re.compile(r"^```bash\s*$", re.MULTILINE),
    "serialized object in scientific report": re.compile(
        r"^```json\s*$", re.IGNORECASE | re.MULTILINE
    ),
    "direct source or test link in scientific report": re.compile(
        r"\]\(\.\./\.\./(?:include|src|tests)/", re.IGNORECASE
    ),
    "software-operation section in scientific report": re.compile(
        r"^## (?:Command-line interfaces|JSON and CSV behavior|Manifest tool notes|"
        r"[^\n]*parser[^\n]*|"
        r"Reproduce(?: the artifacts)?|Reproduction|Reproducibility|Validation)\s*$",
        re.IGNORECASE | re.MULTILINE,
    ),
    "command metadata in scientific report": re.compile(
        r"^(?:Command|Tool):\s*", re.IGNORECASE | re.MULTILINE
    ),
    "CLI option in scientific report": re.compile(r"`--[a-z][a-z0-9-]*"),
    "command narration in scientific report": re.compile(
        r"\bThe command (?:accepts|can|emits|implements|reads|reports|uses|writes)\b|"
        r"`camera_iq\s+[a-z]",
        re.IGNORECASE,
    ),
    "serialization mechanics in scientific report": re.compile(
        r"\b(?:JSON|CSV)\s+(?:carr(?:y|ies)|emit(?:s|ted)?|records?|serializ(?:e|es|ed))\b|"
        r"\bJSON\b.{0,60}\b(?:deliberately|retain(?:s|ed)?|separat(?:e|es|ed))\b|"
        r"\b(?:reports?|retains?)\b.{0,60}\bin JSON\b|"
        r"\bserializ(?:e|es|ed|ation)\s+(?:as|in|the|every|effective)\b|"
        r"\bschema-?\d+\b",
        re.IGNORECASE,
    ),
    "parser mechanics in scientific report": re.compile(
        r"\bparser\b", re.IGNORECASE
    ),
    "repository-specific numerical method in scientific report": re.compile(
        r"\bin-repo DFT\b", re.IGNORECASE
    ),
    "library or serialization identifier in scientific report": re.compile(
        r"`(?:COLOR\(\)|unpack\(\)|effective_black_levels\(\)|"
        r"rawdata\.raw_image|sizes\.(?:width|height|top_margin|left_margin|"
        r"raw_pitch)|raw_width|cblack(?:\[[^`]*\])?|"
        r"dsnu_below_temporal_floor)`|\bpost[- ]unpack\b",
        re.IGNORECASE,
    ),
}
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

# Prose-only tripwires are evaluated after whitespace normalization so ordinary
# Markdown wrapping cannot hide a known semantic regression. Heading, table,
# and line-structure checks remain in STALE_PATTERNS and are not normalized.
NORMALIZED_STALE_PATTERNS = {
    "unsupported cross-camera time-to-lens inference": re.compile(
        r"(?:"
        r"\b(?:captures?|sweeps?)\b.{0,80}"
        r"\b(?:under|within|about|approximately|less than)\b.{0,50}"
        r"\b(?:hours?|minutes?)\b|"
        r"\b(?:close|nearby)\s+(?:capture\s+)?"
        r"(?:times?|timestamps?|windows?)\b"
        r")"
        r".{0,100}\b(?:mak(?:e|es|ing)|suggest(?:s|ed|ing)?|"
        r"support(?:s|ed|ing)?|indicat(?:e|es|ed|ing)|"
        r"impl(?:y|ies|ied|ying))\b"
        r".{0,80}\b(?:same|single|shared|one)\s+"
        r"(?:physical\s+)?(?:lens|copy|sample)\b",
        re.IGNORECASE,
    ),
}

# Public case studies can omit nonessential dates, but their technical reports
# must retain the evidence relationships needed to interpret the measurements.
# These checks protect source classification and session identity rather than
# treating the presence of date-shaped text as proof of provenance.
PROVENANCE_CONTRACTS = {
    Path("docs/case-studies/sfr-mtf-aperture-field.md"): (
        (
            "case-study cross-body clock boundary",
            re.compile(
                r"(?:timestamps|camera clocks).{0,100}"
                r"(?:no|without).{0,60}synchronization",
                re.IGNORECASE,
            ),
        ),
        (
            "case-study elapsed-time and lens-identity boundary",
            re.compile(
                r"(?:not|do not|does not|cannot).{0,50}elapsed time.{0,80}"
                r"(?:shared|same).{0,40}(?:physical\s+)?lens",
                re.IGNORECASE,
            ),
        ),
    ),
    Path("docs/reports/SFR_MTF.md"): (
        (
            "per-body camera-clock windows",
            re.compile(
                r"\|\s*Camera-clock window\s*\|"
                r"(?=[^|]*\d{1,2}:\d{2})[^|]+\|"
                r"(?=[^|]*\d{1,2}:\d{2})[^|]+\|",
                re.IGNORECASE,
            ),
        ),
        (
            "cross-body clock synchronization boundary",
            re.compile(
                r"different camera bodies.{0,100}no clock-synchronization "
                r"record survives",
                re.IGNORECASE,
            ),
        ),
        (
            "capture versus advisory-run date distinction",
            re.compile(r"run date, not the capture date", re.IGNORECASE),
        ),
    ),
    Path("docs/reports/SFR_MTF_ARCHIVE_INVENTORY.md"): (
        (
            "camera-local timestamp classification",
            re.compile(
                r"\|\s*Camera-clock window\s*\|[^|]+\|[^|]+\|"
                r"\s*Camera-local timestamps\s*\|",
                re.IGNORECASE,
            ),
        ),
        (
            "archive clock-synchronization boundary",
            re.compile(
                r"different camera bodies.{0,120}no record that their clocks "
                r"were synchronized",
                re.IGNORECASE,
            ),
        ),
    ),
    Path("docs/reports/SPECTRAL_ARCHIVE_INVENTORY.md"): (
        (
            "canonical four-camera archive identity",
            re.compile(r"`archive:2016_Monochromator/`"),
        ),
        (
            "separate IQ3 archive identity",
            re.compile(r"`archive:2017_camspec/`"),
        ),
        (
            "same-session SSF and target pairing",
            re.compile(r"same-session SSF\+capture pairing", re.IGNORECASE),
        ),
        (
            "separate-rig IQ3 relationship",
            re.compile(r"distinct rig and timeline", re.IGNORECASE),
        ),
    ),
    Path("docs/reports/CCM_FIT.md"): (
        (
            "capture/reference timeline separation",
            re.compile(r"cross-timeline by design", re.IGNORECASE),
        ),
        (
            "compatible-reference scope",
            re.compile(
                r"compatible spectral reference.{0,120}not a measurement of "
                r"the physical chart unit",
                re.IGNORECASE,
            ),
        ),
    ),
    Path("docs/implementation/color-characterization.md"): (
        (
            "serialized timeline provenance",
            re.compile(r"`timeline_provenance`"),
        ),
        (
            "serialized compatible-reference scope",
            re.compile(r"`compatible_sg_spectral_not_exact_per_unit`"),
        ),
        (
            "serialized compatible physical-chart identity",
            re.compile(
                r"`compatible_reference_not_proven_same_physical_chart`"
            ),
        ),
    ),
    Path("docs/case-studies/color-model-equation-audit.md"): (
        (
            "historical CIE94 role",
            re.compile(
                r"CIE94.{0,80}(?:preserv|retain).{0,80}"
                r"(?:prior|historical).{0,40}result",
                re.IGNORECASE,
            ),
        ),
        (
            "CIE94 versus CIEDE2000 method boundary",
            re.compile(
                r"\bCIE94\b.{0,240}\bCIEDE2000\b.{0,240}"
                r"(?:formula|formulas).{0,80}weighting.{0,100}"
                r"method-specific.{0,80}not compared numerically",
                re.IGNORECASE,
            ),
        ),
    ),
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


def normalize_markdown(text: str) -> str:
    """Collapse prose wrapping without combining separate documents."""
    return re.sub(r"\s+", " ", text).strip()


def provenance_contract_failures_for_text(relative: Path, text: str) -> list[str]:
    normalized = normalize_markdown(text)
    return [
        f"missing provenance contract ({label}): {relative}"
        f" — restore the evidence relationship, or change it deliberately in"
        f" PROVENANCE_CONTRACTS (tools/check_portfolio_docs.py)"
        for label, pattern in PROVENANCE_CONTRACTS.get(relative, ())
        if not pattern.search(normalized)
    ]


def provenance_contract_failures(repo_root: Path) -> list[str]:
    failures: list[str] = []
    for relative in PROVENANCE_CONTRACTS:
        path = repo_root / relative
        if not path.is_file():
            failures.append(f"missing provenance contract document: {relative}")
            continue
        failures.extend(
            provenance_contract_failures_for_text(
                relative, path.read_text(encoding="utf-8")
            )
        )
    return failures


def implementation_link_failures_for_text(relative: Path, text: str) -> list[str]:
    expected = IMPLEMENTATION_COMPANION_LINKS.get(relative)
    if expected is None:
        return []
    failures = []
    if f"]({expected})" not in text:
        failures.append(
            f"missing implementation companion link: {relative} -> {expected}"
        )
    if relative.parent == Path("docs/reports"):
        count = len(re.findall(r"^## Engineering companion\s*$", text, re.MULTILINE))
        if count != 1:
            failures.append(
                f"scientific report requires exactly one engineering companion "
                f"section: {relative} (found {count})"
            )
    return failures


def implementation_link_failures(repo_root: Path) -> list[str]:
    failures: list[str] = []
    for relative in IMPLEMENTATION_COMPANION_LINKS:
        path = repo_root / relative
        if not path.is_file():
            failures.append(f"missing implementation-linked document: {relative}")
            continue
        failures.extend(
            implementation_link_failures_for_text(
                relative, path.read_text(encoding="utf-8")
            )
        )
    return failures


def implementation_evidence_failures_for_text(
    relative: Path, text: str
) -> list[str]:
    if relative.parent != Path("docs/implementation") or relative.name == "README.md":
        return []

    failures: list[str] = []
    if not IMPLEMENTATION_TEST_LINK_RE.search(text):
        failures.append(f"implementation companion missing public test link: {relative}")

    # The heading can say evidence, verification, or what the tests/fixtures
    # establish; it is not pinned to one exact title. The explanatory prose must
    # live inside that section. Otherwise an empty heading plus a stray mention
    # of tests elsewhere would satisfy the guard while the evidence itself was
    # still missing.
    evidence_headings = list(IMPLEMENTATION_EVIDENCE_SECTION_RE.finditer(text))
    if not evidence_headings:
        failures.append(
            f"implementation companion missing a verification-evidence section: "
            f"{relative} — give the evidence its own heading so removing it is "
            f"visible in review"
        )

    evidence_sections = []
    for evidence_heading in evidence_headings:
        heading_level = len(evidence_heading.group(0)) - len(
            evidence_heading.group(0).lstrip("#")
        )
        tail = text[evidence_heading.end():]
        next_peer_or_parent = re.search(
            rf"^#{{1,{heading_level}}}\s+", tail, re.MULTILINE
        )
        evidence_sections.append(
            tail[: next_peer_or_parent.start()] if next_peer_or_parent else tail
        )

    if evidence_sections and not any(
        IMPLEMENTATION_TEST_LINK_RE.search(section)
        for section in evidence_sections
    ):
        failures.append(
            f"implementation evidence section missing public test link: "
            f"{relative} — link the executable assertion beside the claim it pins"
        )

    prose_paragraphs = []
    for evidence_text in evidence_sections:
        for paragraph in re.split(r"\n\s*\n", evidence_text):
            stripped = paragraph.lstrip()
            if not stripped or stripped.startswith(("#", "- ", "```")):
                continue
            normalized = normalize_markdown(paragraph)
            if len(normalized.split()) >= 8:
                prose_paragraphs.append(normalized)
    if not any(
        IMPLEMENTATION_EVIDENCE_ROLE_RE.search(paragraph)
        for paragraph in prose_paragraphs
    ):
        failures.append(
            f"implementation companion missing verification evidence explanation: "
            f"{relative} — explain what tests or cross-checks establish and what "
            f"scientific validity still depends on"
        )
    if not any(
        IMPLEMENTATION_EVIDENCE_ASSERTION_RE.search(paragraph)
        for paragraph in prose_paragraphs
    ):
        failures.append(
            f"implementation companion missing a numeric assertion or semantic "
            f"contract in its verification evidence: {relative} — retain at "
            f"least one test-backed count, bound, precondition, or exact refusal"
        )
    if not any(
        IMPLEMENTATION_EVIDENCE_LIMIT_RE.search(paragraph)
        for paragraph in prose_paragraphs
    ):
        failures.append(
            f"implementation companion missing a scientific limitation in its "
            f"verification evidence: {relative} — state what the tests do not "
            f"establish about the physical evidence"
        )
    return failures


def implementation_evidence_failures(repo_root: Path) -> list[str]:
    failures: list[str] = []
    implementation_dir = repo_root / "docs" / "implementation"
    for path in sorted(implementation_dir.glob("*.md")):
        if path.name == "README.md":
            continue
        relative = path.relative_to(repo_root)
        failures.extend(
            implementation_evidence_failures_for_text(
                relative, path.read_text(encoding="utf-8")
            )
        )
    return failures


def _text_files(root: Path, suffixes: set[str]) -> list[Path]:
    if not root.is_dir():
        return []
    return sorted(
        path for path in root.rglob("*")
        if path.is_file() and path.suffix in suffixes
    )


def _preceding_paragraph(text: str, marker_start: int) -> str:
    prefix = text[:marker_start].rstrip()
    return re.split(r"\n[ \t]*\n", prefix)[-1]


def _mask_markdown_fences(text: str) -> str:
    """Blank fenced examples so example markers cannot become live claims."""
    output: list[str] = []
    fence_character = ""
    fence_length = 0
    for line in text.splitlines(keepends=True):
        match = re.match(r"^[ \t]{0,3}(`{3,}|~{3,})", line)
        if not fence_character:
            if match is None:
                output.append(line)
                continue
            fence_character = match.group(1)[0]
            fence_length = len(match.group(1))
        elif (
            match is not None
            and match.group(1)[0] == fence_character
            and len(match.group(1)) >= fence_length
            and re.fullmatch(
                r"[ \t]{0,3}(?:`{%d,}|~{%d,})[ \t]*(?:\r?\n)?"
                % (fence_length, fence_length),
                line,
            )
            is not None
        ):
            fence_character = ""
            fence_length = 0
        output.append("".join("\n" if char == "\n" else " " for char in line))
    return "".join(output)


def _splice_cpp_lines(source: str) -> str:
    """Apply C++ translation-phase backslash-newline deletion."""
    return re.sub(r"\\(?:\r\n|\n)", "", source)


def _mask_cpp_conditionals(source: str) -> str:
    """Blank conditionally compiled regions from executable evidence scans."""
    output: list[str] = []
    conditional_depth = 0
    for line in source.splitlines(keepends=True):
        directive = re.match(
            r"^[ \t]*#[ \t]*(if|ifdef|ifndef|endif)\b", line
        )
        inside = conditional_depth > 0
        if directive is not None:
            keyword = directive.group(1)
            if keyword == "endif":
                inside = True
                conditional_depth = max(0, conditional_depth - 1)
            else:
                conditional_depth += 1
                inside = True
        if inside:
            output.append(
                "".join("\n" if char == "\n" else " " for char in line)
            )
        else:
            output.append(line)
    return "".join(output)


def _mask_cpp_comments_and_literals(source: str) -> str:
    """Apply line splicing, then blank C++ comments and literals."""
    source = _splice_cpp_lines(source)
    output = list(source)

    def blank(start: int, end: int) -> None:
        for position in range(start, end):
            if output[position] != "\n":
                output[position] = " "

    index = 0
    while index < len(source):
        raw = re.match(r'(?:u8|u|U|L)?R"([^\s\\()]*)\(', source[index:])
        if raw is not None:
            terminator = ")" + raw.group(1) + '"'
            end = source.find(terminator, index + raw.end())
            end = len(source) if end < 0 else end + len(terminator)
            blank(index, end)
            index = end
            continue
        current = source[index]
        following = source[index + 1] if index + 1 < len(source) else ""
        if current == "/" and following == "/":
            end = source.find("\n", index + 2)
            end = len(source) if end < 0 else end
            blank(index, end)
            index = end
            continue
        if current == "/" and following == "*":
            end = source.find("*/", index + 2)
            end = len(source) if end < 0 else end + 2
            blank(index, end)
            index = end
            continue
        if current in {'"', "'"}:
            quote = current
            end = index + 1
            while end < len(source):
                if source[end] == "\\" and end + 1 < len(source):
                    end += 2
                    continue
                end += 1
                if source[end - 1] == quote:
                    break
            blank(index, end)
            index = end
            continue
        index += 1
    return "".join(output)


def _mask_cpp_noncode(source: str) -> str:
    """Blank non-executable C++ text and conditionally compiled regions."""
    return _mask_cpp_conditionals(_mask_cpp_comments_and_literals(source))


def _mask_cmake_noncode(source: str) -> str:
    """Blank CMake comments, strings, and conditional registration blocks."""
    output = list(source)

    def blank(start: int, end: int) -> None:
        for position in range(start, end):
            if output[position] != "\n":
                output[position] = " "

    index = 0
    while index < len(source):
        if source[index] == "#":
            bracket = re.match(r"#\[(=*)\[", source[index:])
            if bracket is not None:
                terminator = "]" + bracket.group(1) + "]"
                end = source.find(terminator, index + bracket.end())
                end = len(source) if end < 0 else end + len(terminator)
            else:
                end = source.find("\n", index + 1)
                end = len(source) if end < 0 else end
            blank(index, end)
            index = end
            continue
        if source[index] == "[":
            bracket = re.match(r"\[(=*)\[", source[index:])
            if bracket is not None:
                terminator = "]" + bracket.group(1) + "]"
                end = source.find(terminator, index + bracket.end())
                end = len(source) if end < 0 else end + len(terminator)
                blank(index, end)
                index = end
                continue
        if source[index] == '"':
            end = index + 1
            while end < len(source):
                if source[end] == "\\" and end + 1 < len(source):
                    end += 2
                    continue
                end += 1
                if source[end - 1] == '"':
                    break
            blank(index, end)
            index = end
            continue
        index += 1

    unconditioned = list("".join(output))
    conditional_depth = 0
    offset = 0
    for line in "".join(output).splitlines(keepends=True):
        directive = re.match(r"^[ \t]*(if|endif)\s*\(", line, re.IGNORECASE)
        inside = conditional_depth > 0
        if directive is not None:
            if directive.group(1).lower() == "endif":
                inside = True
                conditional_depth = max(0, conditional_depth - 1)
            else:
                conditional_depth += 1
                inside = True
        if inside:
            for position in range(offset, offset + len(line)):
                if unconditioned[position] != "\n":
                    unconditioned[position] = " "
        offset += len(line)
    return "".join(unconditioned)


def _evidence_macro_definition_failures(repo_root: Path) -> list[str]:
    header = repo_root / "tests" / "harness.hpp"
    if not header.is_file():
        return [
            "evidence macro must execute its assertion: missing tests/harness.hpp"
        ]
    executable_text = _mask_cpp_noncode(header.read_text(encoding="utf-8"))
    definitions = ANY_EVIDENCE_MACRO_DEFINITION_RE.findall(executable_text)
    if len(definitions) != 1 or EVIDENCE_MACRO_DEFINITION_RE.search(
        executable_text
    ) is None:
        return [
            "evidence macro must execute its assertion exactly once: "
            "expected '#define CAMERA_IQ_DOC_EVIDENCE(evidence_id, assertion) "
            "assertion' in tests/harness.hpp"
        ]
    for path in _text_files(repo_root / "tests", {".cpp", ".hpp"}):
        if path.resolve() == header.resolve():
            continue
        directives = _mask_cpp_comments_and_literals(
            path.read_text(encoding="utf-8")
        )
        if EVIDENCE_MACRO_MUTATION_RE.search(directives):
            return [
                "evidence macro must not be redefined or undefined outside "
                f"tests/harness.hpp: {path.relative_to(repo_root)}"
            ]
    return []


def _paragraph_links_to(document: Path, paragraph: str, expected: Path) -> bool:
    expected = expected.resolve()
    for match in LINK_RE.finditer(paragraph):
        target = link_target(match.group(1))
        if not target or target.startswith(("#", "http://", "https://", "mailto:")):
            continue
        target_path = unquote(target.split("#", 1)[0])
        if (document.parent / target_path).resolve() == expected:
            return True
    return False


def _paragraph_test_links(document: Path, paragraph: str) -> set[Path]:
    links: set[Path] = set()
    for match in LINK_RE.finditer(paragraph):
        target = link_target(match.group(1))
        if not target or target.startswith(("#", "http://", "https://", "mailto:")):
            continue
        target_path = unquote(target.split("#", 1)[0])
        parts = Path(target_path).parts
        if "tests" not in parts and not Path(target_path).name.startswith("test_"):
            continue
        links.add((document.parent / target_path).resolve())
    return links


def evidence_attribution_failures(
    repo_root: Path,
    contracts: dict[str, EvidenceAttributionContract] | None = None,
) -> list[str]:
    """Bind selected documentation claims to executable test assertions.

    A regular Markdown link proves only that a test file exists. These selected
    contracts additionally require one stable identifier in the claim's
    paragraph and an exact number of wrappers around assertions in the
    registered, CTest-enabled source.
    """
    contracts = EVIDENCE_ATTRIBUTION_CONTRACTS if contracts is None else contracts
    failures: list[str] = []
    document_markers: dict[str, list[tuple[Path, re.Match[str], str]]] = {}
    test_markers: dict[str, list[tuple[Path, re.Match[str], str]]] = {}
    cmake_path = repo_root / "CMakeLists.txt"
    cmake_text = (
        cmake_path.read_text(encoding="utf-8") if cmake_path.is_file() else ""
    )
    active_cmake_text = _mask_cmake_noncode(cmake_text)
    ctest_sources = {
        (target, Path(source))
        for target, source in CMAKE_CPP_TEST_RE.findall(active_cmake_text)
    }
    if contracts:
        failures.extend(_evidence_macro_definition_failures(repo_root))
    helper_bodies = [
        match.group("body")
        for match in CMAKE_TEST_HELPER_RE.finditer(active_cmake_text)
    ]
    has_ctest_helper = (
        len(helper_bodies) == 1
        and CMAKE_HELPER_ADD_EXECUTABLE_RE.search(helper_bodies[0]) is not None
        and CMAKE_HELPER_ADD_TEST_RE.search(helper_bodies[0]) is not None
    )

    document_paths = _text_files(repo_root / "docs", {".md"})
    readme = repo_root / "README.md"
    if readme.is_file():
        document_paths.append(readme)
    for path in document_paths:
        text = path.read_text(encoding="utf-8")
        marker_text = _mask_markdown_fences(text)
        for match in DOCUMENT_EVIDENCE_RE.finditer(marker_text):
            line_start = marker_text.rfind("\n", 0, match.start()) + 1
            if match.start() != line_start:
                continue
            document_markers.setdefault(match.group(1), []).append(
                (path, match, text)
            )

    for path in _text_files(repo_root / "tests", {".cpp", ".hpp", ".py"}):
        text = path.read_text(encoding="utf-8")
        executable_text = (
            _mask_cpp_noncode(text)
            if path.suffix in {".cpp", ".hpp"}
            else text
        )
        for match in EXECUTABLE_TEST_EVIDENCE_RE.finditer(executable_text):
            line_start = executable_text.rfind("\n", 0, match.start()) + 1
            if re.match(
                r"\s*#\s*define\b",
                executable_text[line_start:match.start()],
            ):
                continue
            test_markers.setdefault(match.group(1), []).append(
                (path, match, executable_text)
            )

    for evidence_id in sorted(set(document_markers) - set(contracts)):
        failures.append(
            f"unregistered document evidence marker: {evidence_id}"
        )
    for evidence_id in sorted(set(test_markers) - set(contracts)):
        failures.append(f"unregistered test evidence marker: {evidence_id}")

    for evidence_id, contract in contracts.items():
        expected_document = (repo_root / contract.document).resolve()
        expected_test = (repo_root / contract.test).resolve()
        document_entries = document_markers.get(evidence_id, [])
        test_entries = test_markers.get(evidence_id, [])

        if contract.test_target and (
            contract.test_target, contract.test
        ) not in ctest_sources:
            failures.append(
                f"test target {contract.test_target} for {evidence_id} is not "
                f"registered with CTest from {contract.test}"
            )
        elif contract.test_target and not has_ctest_helper:
            failures.append(
                f"test target {contract.test_target} for {evidence_id} is not "
                "registered with CTest: camera_iq_add_test does not have active "
                "add_executable target/source and add_test bodies"
            )

        if len(document_entries) != 1:
            failures.append(
                f"document marker count is {len(document_entries)} for "
                f"{evidence_id}; expected exactly 1 in {contract.document}"
            )
        elif document_entries[0][0].resolve() != expected_document:
            failures.append(
                f"document marker is not in its registered file: {evidence_id}"
            )
        else:
            path, match, text = document_entries[0]
            paragraph = _preceding_paragraph(text, match.start())
            if not _paragraph_links_to(path, paragraph, expected_test):
                failures.append(
                    f"claim paragraph does not link its registered test: "
                    f"{evidence_id} -> {contract.test}"
                )
            paragraph_tests = _paragraph_test_links(path, paragraph)
            if paragraph_tests - {expected_test}:
                failures.append(
                    f"registered claim paragraph mixes multiple test files: "
                    f"{evidence_id} in {contract.document} — split each "
                    f"evidence cluster into its own paragraph"
                )

        if len(test_entries) != contract.assertion_count:
            failures.append(
                f"executable assertion count is {len(test_entries)} for "
                f"{evidence_id}; expected exactly {contract.assertion_count} "
                f"in {contract.test}"
            )
        elif any(entry[0].resolve() != expected_test for entry in test_entries):
            failures.append(
                f"test marker is not in its registered file: {evidence_id}"
            )

    return failures


def report_layer_failures_for_text(relative: Path, text: str) -> list[str]:
    if relative.parent != Path("docs/reports"):
        return []
    return [
        f"{label}: {relative} — move software operation to docs/implementation/"
        for label, pattern in REPORT_LAYER_PATTERNS.items()
        if pattern.search(text)
    ]


def figure_caption_failures_for_text(relative: Path, text: str) -> list[str]:
    if relative.parent not in {
        Path("docs/case-studies"),
        Path("docs/reports"),
    }:
        return []
    lines = text.splitlines()
    failures = []
    for index, line in enumerate(lines):
        if not re.match(r"^!\[[^]]*\]\([^)]+\)\s*$", line):
            continue
        next_index = index + 1
        while next_index < len(lines) and not lines[next_index].strip():
            next_index += 1
        if next_index >= len(lines) or not lines[next_index].lstrip().startswith("*"):
            failures.append(
                f"figure missing adjacent caption: {relative}:{index + 1}"
            )
    return failures


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

    failures.extend(provenance_contract_failures(repo_root))
    failures.extend(implementation_link_failures(repo_root))
    failures.extend(implementation_evidence_failures(repo_root))
    failures.extend(evidence_attribution_failures(repo_root))

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
            normalized = normalize_markdown(text)
            for label, pattern in NORMALIZED_STALE_PATTERNS.items():
                if pattern.search(normalized):
                    failures.append(f"{label}: {path.relative_to(repo_root)}")
        failures.extend(
            report_layer_failures_for_text(path.relative_to(repo_root), text)
        )
        failures.extend(
            figure_caption_failures_for_text(path.relative_to(repo_root), text)
        )

    index_path = repo_root / "docs" / "README.md"
    if index_path.is_file():
        index_text = index_path.read_text(encoding="utf-8")
        for report in sorted((repo_root / "docs" / "reports").glob("*.md")):
            expected = f"reports/{report.name}"
            if expected not in index_text:
                failures.append(f"report missing from docs index: {report.name}")

    implementation_index = repo_root / "docs" / "implementation" / "README.md"
    if implementation_index.is_file():
        implementation_index_text = implementation_index.read_text(encoding="utf-8")
        for companion in sorted(
            (repo_root / "docs" / "implementation").glob("*.md")
        ):
            if companion == implementation_index:
                continue
            expected = f"]({companion.name})"
            if expected not in implementation_index_text:
                failures.append(
                    f"implementation companion missing from index: {companion.name}"
                )

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
