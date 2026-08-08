# Scientific study-decision record template

Use this working record when a measurement, reanalysis, or implementation
decision can change a public scientific conclusion. It captures the reasoning
while the choice is being made; it is not a substitute for the scientific
report and it does not need to become a reader-facing page.

Create a record when the work:

- lacks a control, calibration, per-unit reference, repeated capture, or
  acquisition condition that the preferred design would use;
- substitutes archived, compatible, synthetic, modeled, or public data for a
  direct measurement;
- excludes or screens inputs in a way that can change the reported population;
- retains a negative or unresolved result;
- chooses among scientifically meaningful algorithms, thresholds, coordinate
  systems, observers, illuminants, or normalization conventions; or
- changes the comparison class or claim strength.

Do not create a record for routine naming, formatting, refactoring, or
implementation choices that do not affect scientific meaning.

## Record

Copy the fields below into the study's working note. Replace every prompt; do
not leave an empty field to imply that the answer is known.

```text
Decision ID:
Study and public surface:
Decision date:
Decision owner:

Scientific question:
Why the question matters:

Preferred controlled design for this question:
Available inputs and conditions:
Missing or ambiguous evidence:

Alternatives considered:
- Alternative:
  Scientific advantage:
  Scientific cost:
  Disposition and reason:

Chosen method or workaround:
Why this choice is defensible:
Quality gates and cross-checks:
Assumptions:

What the result may support:
What remains unresolved or prohibited:
Resolving capture, calibration, or experiment:

Rationale basis:
- CONTEMPORANEOUS_RECORD | ARCHIVE_DERIVED | AUTHOR_RECOLLECTION |
  PRESENT_ANALYSIS_DECISION | UNKNOWN
Source pointers:

Implementation consequences:
Report sections that must carry this decision:
Tests, fixtures, receipts, or artifact guards affected:
Public-safe wording draft:
```

## Rationale-basis rules

- `CONTEMPORANEOUS_RECORD` means a retained note, protocol, sidecar, source
  comment, or other record made with the original work.
- `ARCHIVE_DERIVED` means the reason follows from retained files or metadata,
  but was not stated as a motive at the time.
- `AUTHOR_RECOLLECTION` means a later first-person recollection. Preserve its
  status instead of rewriting it as recorded history.
- `PRESENT_ANALYSIS_DECISION` belongs to the current reanalysis or software,
  even when it responds to an older archive gap.
- `UNKNOWN` means the original reason cannot be recovered. State the current
  scientific question separately.

More than one basis may apply to different parts of a decision. Source
pointers should identify public files by repository-relative path. Private
source identities stay in their approved private record and are translated to
public-safe dataset IDs or aggregate receipts before publication.

## From working record to public documentation

The public case study normally needs one compact narrative: the preferred
experiment, what was available, the chosen route, and what remains unresolved.
The scientific report owns the full method and interpretation. The
implementation companion includes the decision only when it changes code flow,
quality gates, refusals, or numerical behavior. Evidence references own file
identity, not the story.

For example, a useful archive paragraph has this shape:

> A controlled comparison would interleave both instruments on a monitored
> source while preserving calibration state, geometry, wavelength accuracy,
> and bandpass. The archive retains repeated spectra but not those controls, so
> the analysis compares level, normalized shape, and residual localization
> without calling the result an accuracy test. The missing controls remain the
> design for a resolving experiment.

That paragraph is candid without asking for an exception. It explains why the
analysis is useful, what judgment was exercised, and where the claim stops.

Before publishing, apply the
[public documentation standard](PUBLIC_DOCUMENTATION_STANDARD.md) and verify
that every reported number still comes from its canonical aggregate or test.
