# Public scientific and implementation documentation standard

Public documentation in this repository must let a first-time reader understand
the subject without reading source code or knowing the history of the project.
It must also give an engineering reader a clear route into the implementation.
Those goals are served by separate documents with separate jobs.

This standard applies to every public case study, scientific report,
implementation companion, figure caption, dataset note, and navigation page.
It is an authoring guide, not part of the scientific evidence for a result.

## The four document layers

### 1. Case study: the readable scientific story

The case study is the shortest complete account. A reader arriving by a direct
link should learn:

1. what physical or color-science problem is being studied;
2. why the problem matters in imaging work;
3. what was measured or modeled, in plain language;
4. the most important result and what it means;
5. the most important limitation or rejected interpretation; and
6. where to find the scientific report, aggregate data, and implementation
   companion.

A case study is not a source-file tour. It does not list functions, schemas,
command options, test names, or build mechanics. It may summarize a formula
when the formula is the subject of the study, but it should not make the reader
decode mathematics before explaining the question.

### 2. Scientific report: the measurement and reasoning

The scientific report owns the technical meaning of the work. It contains the
question, measurement design, mathematical model, data selection, calculations,
results, interpretation, and limitations. A technical reader should be able to
evaluate the reasoning without inspecting the C++.

The normal reading order is:

1. **Purpose and importance** — define the phenomenon and the decision the
   measurement informs.
2. **Question or hypothesis** — state what the work is trying to distinguish.
3. **Inputs and conditions** — identify the dataset or synthetic input, units,
   reference data, relevant capture conditions, and exclusions.
4. **Method and reasons** — explain each measurement step and why it is needed.
5. **Mathematical model** — give the formulas, define every symbol and unit, and
   state assumptions or normalization conventions.
6. **Scientific data flow** — show the path from physical input or model input
   to the reported metric.
7. **Results** — present tables and figures with conditions attached.
8. **Interpretation** — explain what changed, why the result matters, and which
   alternative explanations remain possible.
9. **Limitations and next experiment** — state what the evidence does not
   isolate and what measurement would resolve it.
10. **Engineering companion** — link to the separate implementation document.

The scientific data flow describes meaning, not software structure. For
example:

```text
RAW mosaic
  -> black-subtracted green samples
  -> oversampled edge-spread function
  -> line-spread function
  -> Fourier magnitude
  -> MTF50 and MTF at Nyquist
```

Build commands, CLI invocations, parser mechanics, JSON examples, serialized
field names, schema versions, source inventories, test catalogs, and
function-by-function explanations belong in the implementation companion. The
scientific report links there instead of interrupting the measurement narrative
with software operation.

### 3. Implementation companion: the software explanation

The implementation companion explains how the scientific method becomes
software. It should give a broad view before asking the reader to open a source
file. It contains:

- the software boundary and public entry point;
- the typed inputs and outputs;
- code-level data flow from parsing to serialization;
- formula-to-function mapping;
- numerical representation, discretization, indexing, and coordinate
  conventions;
- algorithms, branches, tolerances, and quality gates;
- invariants and failure behavior;
- output schemas and artifact generation where relevant;
- test strategy and independent cross-checks;
- performance or memory behavior when it is material; and
- links to the relevant public headers, source files, tests, and tools.

Its code-level flow should be visibly different from the scientific flow. For
example:

```text
dataset ID + relative path
  -> command parser
  -> LibRaw-backed RawCfaImage
  -> analyze_green_sfr()
  -> SfrResult
  -> JSON writer and aggregate generator
```

An implementation companion must not treat compilation or passing tests as
scientific validation. Tests show that the declared algorithm and contracts
are implemented consistently; measurement validity still comes from the
scientific design, controls, references, and evidence.

Every implementation companion must also explain its verification evidence in
prose, under its own heading rather than as a stray paragraph beside the
source-and-test map. The heading may name evidence or verification, or state
what the tests or fixtures establish; it is not pinned to one title. A
dedicated section makes removing the evidence visible in review instead of
leaving a link list that still looks complete. That explanation identifies
representative numeric fixtures, invariants,
refusal paths, serialization checks, artifact-freshness checks, or independent
comparisons, as applicable. It also states which physical, perceptual, or
archive-validity question those checks do **not** answer. At least one public
test must be linked inside that section so the explanation is inspectable. The
section must retain at least one test-backed numeric assertion or exact semantic
contract—a count, bound, precondition, or refusal. Generic statements such as
"tests cover the algorithm" are not a substitute for the evidence actually
pinned by the executable assertion.

Verification claims must be portable. Do not freeze a local observed value,
test count, library-version result, or platform-dependent rounding outcome as a
general contract unless it is pinned by a committed generated artifact or by an
executable assertion. Prefer the enforced tolerance or invariant and identify
optional reference builds as optional.

Name the evidence layer rather than blending unlike checks into one claim:

- **Library or unit fixtures** establish equations, numerical behavior, typed
  invariants, and local refusal paths.
- **Command or integration tests** establish orchestration, path handling,
  cross-component wiring, serialization, and command-level refusals.
- **Generated-artifact guards** establish freshness and internal consistency of
  committed tables, figures, or receipts; they do not rerun private inputs.
- **Archive-backed runtime evidence** establishes the identity and conditions
  of physical inputs and measured conclusions, subject to the report's stated
  provenance and controls.

Do not promote coverage from one layer to another. A library assertion is not a
command success path, a synthetic exporter corpus is not an archive rerun, and
an artifact-freshness check is not a new physical measurement.

Every implementation companion backed by a default-harness C++ test must
register at least one representative machine-checked claim. Choose it from the
document's interpretation-bearing numeric bounds, scientific refusals or
provenance contracts, or conditions whose loss would change a public
conclusion. Add separate anchors for selected downstream serialization/
provenance contracts and decision-changing acceptance bounds when they carry a
different reader-facing conclusion. This is representative protection, not a
requirement to wrap every number or test assertion. Archive identities and
generated-artifact values stay with their owning receipt or freshness guard
instead of being misrepresented as C++ unit evidence.

Two writing rules govern such a claim. Keep its paragraph limited to one test
file, so a citation cannot appear to support a claim that is held somewhere
else. If the paragraph states an assertion count, describe it as the number
**registered for this claim**, never as a census of the entire test file; the
guard compares that stated count against the registered wrappers, so either
side changing forces a review.

Everything else about registration — marker syntax, assertion wrappers, CTest
registration, and the supervised evidence run — is tooling, and is documented
with the tooling in [`tools/README.md`](../tools/README.md).

What that machinery establishes is bounded, and public prose must not imply
more. It protects registered claims from silent wrapper deletion, identifier
mix-ups, and assertions that compile without being reached by the test run. It
does not show that the prose interprets or scopes the assertion correctly, or
that the assertion expresses the right scientific property. Human review must
still verify the assertion, bound, fixture, and operating conditions. Facts
outside the eligibility rule remain protected by ordinary source-navigation
links, artifact guards, and scientific review; they do not need this
annotation.

### 4. Evidence reference: identity and provenance

Inventories, manifests, reference-data notes, and dataset pages establish which
files, sessions, chart references, or archive roles feed an analysis. They are
not substitutes for a scientific introduction and should not be presented as
results by themselves. Their job is traceability, pairing, and boundary
definition.

## Where formulas belong

The canonical mathematical definition belongs in the scientific report because
the reader cannot judge a result without knowing how it was calculated. The
implementation companion links back to that definition and explains how the
software realizes it.

For every formula:

- define every symbol when it first appears;
- give units, or say explicitly that a quantity is dimensionless;
- state the reference white, observer, illuminant, encoding, or normalization
  when applicable;
- distinguish population, sample, and fitted statistics;
- identify the valid input domain and behavior at boundaries; and
- attach thresholds to a scientific reason, not only to a constant name.

Do not maintain two independent versions of the same equation. The scientific
report is canonical; the implementation note describes representation and
links to the exact functions that implement it.

## Writing for a first-time reader

The title is not the introduction. The first paragraph must create the question
before presenting the answer. Unless a number is itself the subject of the
study, do not open with an acceptance count, threshold, or final statistic.

This is technically accurate but unreadable as an opening:

> Three of 52 integrating-sphere captures retained usable headroom; the primary
> f/8 frame showed 19.65% green corner-field asymmetry.

It gives the answer to a question that has not been asked. A readable opening
first explains that a flat-field tests whether a nominally uniform input
produces a uniform sensor response, why clipping hides falloff, and why
asymmetry matters to a centered correction model. The counts and percentage
then have meaning.

Use these rules throughout:

- Define necessary terms on first use. One clause is usually enough for MTF50,
  CIEDE2000, coefficient of variation, or Delta u-prime-v-prime.
- Explain the purpose of a quality gate before giving its threshold.
- Prefer physical and mathematical language to repository vocabulary.
- State why a rejected result failed and what it taught; do not present a
  refusal as dead work.
- Keep a limitation beside the result it constrains, then summarize it again in
  the limitations section if it affects the whole study.
- Make conclusions answer the original question rather than inventorying work
  performed.

## Figures and tables

A figure must be understandable without inspecting its SVG or generator. Its
caption states what the axes, marks, color, size, or connecting segments mean
and identifies the visual pattern that supports the conclusion. Decorative
figures are not added merely to make a page look complete.

A table must identify units and conditions in its heading, caption, or nearby
text. A statistic without its population, input, or measurement condition is
not a portable result.

## Cross-link and duplication rules

Every featured study should expose this route:

```text
case study -> scientific report -> implementation companion -> source and tests
          \-> aggregate data and figure
```

Navigation pages may link directly to each layer, but the case study should not
send a general reader straight into a `.cpp` file.

Headline values may be quoted in the case study, but their full conditions and
calculation belong in the scientific report and aggregate artifact. The
implementation companion should avoid copying result tables unless a value is
needed to demonstrate serialization or a test oracle. This reduces numerical
drift between pages.

## Public boundary

Separating documents does not relax the public-data boundary. No layer may
expose absolute private paths, undistributed source captures, private serials,
or internal evidence systems. Use dataset IDs, archive labels, reduced
illustrative images, and aggregate outputs. Public source paths and public test
fixtures are appropriate in implementation companions.

## Review checklist

Before publishing or materially revising a public portfolio document, verify:

- [ ] A direct-link reader can identify the subject, question, importance, and
      conclusion without opening code.
- [ ] The first paragraph creates context before introducing result statistics.
- [ ] Terms, symbols, units, coordinate spaces, and normalization are defined.
- [ ] The scientific report contains the canonical formulas and scientific data
      flow.
- [ ] Implementation architecture, source maps, test catalogs, schemas, and CLI
      mechanics are routed to an implementation companion.
- [ ] Scientific reports contain no build transcript, parser/serialization
      mechanics, source/test inventory, or command-option catalog.
- [ ] The implementation companion maps formulas and data flow to real public
      functions and types.
- [ ] The implementation companion explains what its tests or independent
      cross-checks establish, links public tests, and separates software
      correctness from scientific validity.
- [ ] Verification prose labels library/unit, command/integration,
      generated-artifact, and archive-backed runtime evidence where those
      layers are present, without promoting one layer's coverage into another.
- [ ] Each implementation companion has a representative runtime-backed claim;
      selected additional decision-changing numeric, refusal, provenance, or
      serialization claims use their own wrappers, while other claims remain
      subject to direct technical review.
- [ ] For every marked claim, a reviewer compares the prose with its exact
      wrapped assertions: fixture, bound, operating conditions, semantic scope,
      and any stated assertion count. A count names wrappers registered for the
      claim, not all assertions in the test file.
- [ ] Its verification section links the relevant executable assertion and
      retains a numeric bound/count or exact semantic contract with every
      precondition needed to interpret it.
- [ ] Passing tests are not presented as proof of physical or perceptual
      validity.
- [ ] Every figure has a self-contained caption grounded in what is drawn.
- [ ] Limitations say both what the evidence supports and what experiment would
      resolve the remaining ambiguity.
- [ ] Cross-links work and repeated numerical claims agree with generated
      aggregates.
- [ ] Public-path and documentation checks pass.
