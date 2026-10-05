# Where instructions are spent

This investigation profiles main and the compact runtime branch on the
same complete pinned corpora and shipped configurations used in
[runtime validation](runtime-completion.md). It changes no production code.
The measured branch source is `764c2f7`; main's measured source is `e102de7`
(the subsequent main integration changes CI only).

## Hardware sampling method

A direct perf_event_open event samples the instruction pointer every
10,000,019 retired user-space instructions. Collection is enabled only
around CheckFile. The event is paused, not reset, between files so small
files contribute to the next overflow rather than losing their remainder.
Input reading, configuration preparation, report writing, outer result
destruction, and kernel work are excluded. The allocation instrumentation
is identical to the runtime harness. Sampling overhead is not speedup
measurement evidence.

All 18,674 inputs complete on both revisions. Branch samples total 20,225;
main samples total 21,533. There are no lost records, throttles, or
multiplexed events. The branch per-corpus sample counts are 6,950 YTsaurus,
8,535 LLVM/Clang, and 4,740 TensorFlow. The corresponding main counts are
7,349, 9,128, and 5,056.

Addresses resolve to emitted functions, including their inlined work.
Shares below sum self instruction-pointer samples, not inclusive call
costs. Standard-library helpers without caller attribution remain separate.
About 0.3% of samples have unresolved addresses. This is non-precise PMU
sampling: interrupt skid and compiler line attribution mean individual
source lines are investigation leads, not exact instruction accounting.

For source-line resolution, four branch objects were rebuilt with minimal
debug information: document, source analysis, syntax layout, and syntax
spacing. Each object's text section and the entire relinked executable's
text section match the originally sampled executable byte-for-byte. The
original samples can therefore be resolved without rerunning a modified
implementation.

## Combined instruction distribution

The aggregate weights corpora by their instruction samples, not by an
unweighted average of corpus percentages. Function families are disjoint;
parser/tokenizer excludes syntax-tree accessors used by other phases.

| Function family | Main share | Branch share |
| --- | ---: | ---: |
| Lowering planner and document construction | 26.0% | 29.0% |
| Renderer and text/gap measurement | 19.6% | 20.1% |
| Source analysis and safety queries | 11.0% | 11.7% |
| Spacing and syntax classification | 8.4% | 9.1% |
| Parser and tokenizer | 8.2% | 8.5% |
| Structural and namespace processing | 6.0% | 6.4% |
| Shared-library helpers | 5.6% | 5.5% |
| Diagnostic positions and checks | 3.9% | 4.0% |
| Syntax-tree accessors | 1.9% | 2.1% |
| Map composition and map sorting | 5.8% | 0.2% |
| Other and unresolved | 3.6% | 3.5% |

Percentages have sampling uncertainty and rounding. A larger branch share
is not proof of more instructions: removing map composition changes the
denominator. Inlining boundaries also matter when interpreting narrow
function comparisons. These are self-sample groups, not inclusive phase
measurements; shared-library work is not charged back to its callers.

The largest visible removal is map composition: 1,244 main samples versus
31 branch samples. Remaining lowering/construction and rendering together
account for roughly half the branch's instruction samples. Parsing is not
the dominant instruction consumer in this workload.

## Largest branch functions and loops

Combined branch self shares:

- Render's atom-dispatch helper: 9.3%. This includes fitting/probing,
  whitespace handling, text advancement, emission, and mapping decisions.
- Render's main body: 7.9%. Its reverse summary/cache walk, forward owner
  traversal, and cache initialization remain substantial work.
- BuildSourceAnalysis main body: 7.8%, plus its region-visit helper at 2.0%.
  Physical-line scanning, token-gap classification, region/protection walks,
  and vector construction all appear in the source-line samples.
- Document::Add: 3.7%. Packing, ownership/logical-position bookkeeping, and
  copying packed atoms into the node vector are visible; this should not be
  mistaken for time spent exclusively in the allocator.
- BuildSyntaxDocument's inlined planner work: 3.4%.
- BuildSyntaxSpacing: 3.3%, plus Classifier::Enter at 2.3%.
- Planner::Emit: 3.2%.
- Document::EmitSourceText: 3.0%.
- Planner::ComputeFlatWidth: 1.8%.

These functions are components of the table above, not additional costs.
No one function owns the entire lowering budget; planning, ownership,
validation, width calculation, document construction, and emission split it.

Useful branch source locations for the next investigation:

- document/document.cpp:1090-1288: reverse summaries, group/gap side caches,
  text measurement, and joining flat/broken/flexible tails.
- document/document.cpp:1525-2105: dispatch, fitting/probes, whitespace,
  output advancement, and per-atom mapping decisions.
- document/document.cpp:361-385: packed-atom construction and insertion.
- formatter/source_analysis.cpp:470-491: the physical-line byte walk and
  horizontal-whitespace membership check.
- formatter/source_analysis.cpp:519-537: token-gap byte classification.
- formatter/syntax_spacing.cpp:2039-2079: region traversal and classifier
  dispatch, including frame and region lookups.
- formatter/syntax_layout.cpp: planner projection, safety, width, ownership,
  forest validation, and the emission loop all retain measurable work.

The shared memchr implementation alone accounts for 3.3% of branch samples.
Without call chains, that cost cannot all be assigned to source analysis:
several text/whitespace consumers use it. It is a reason to investigate
membership and repeated text scans, not justification to replace every
library search or weaken whitespace/safety semantics.

## Next targets

Prioritize lowering/IR construction and renderer dispatch/summary work.
Check whether repeated per-atom bookkeeping and width facts can be shared
without changing ownership, fit behavior, diagnostics, or provenance.
Then isolate source-analysis scanning and region/frame lookup costs.
Do not start with a parser rewrite or allocator redesign solely because
they were suspected earlier: the instruction distribution does not support
them as the largest remaining opportunity.

This report identifies opportunities; it retains no new optimization.
Any candidate still needs uninstrumented runtime comparisons and complete
correctness evidence. Hardware instruction savings alone do not establish
a runtime improvement.

## External artifacts

Under /state/data/compact-pipeline/:

- instruction_sampler.hpp and instruction-sampling-measure.cpp implement
  the bounded, paused-across-files hardware sampler.
- instruction-{baseline,candidate}-sampling-measure are the executables.
- instruction-{baseline,candidate}-{corpus}.ips contain metadata and raw IP
  counts; matching JSON and TSV files preserve processing outcomes.
- resolve-instruction-samples.py, instruction-samples-resolved.json, and
  instruction-*-functions.txt preserve emitted-function/source resolution.
- group-instruction-samples.py and instruction-profile-summary.json preserve
  the function-family definitions and aggregate/per-corpus distributions.
- build-instruction-debug.py, instruction-debug-candidate/, and
  instruction-debug-build.log preserve code-identical debug resolution.
- instruction-line-summary.txt preserves the detailed source-line leads.
