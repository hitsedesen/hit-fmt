# Direct phase cost measurements

This report measures phases directly, rather than assigning instruction-IP
samples to function families. Compared sources are main `e102de7` and branch
`5a160c4` (implementation `398bb7f`). Instrumentation is external to Git;
no production code changed during this investigation.

The same 18,674 pinned files, 195,967,253 original input bytes, unchanged
shipped configurations, Release settings, single worker and CPU 2 affinity
are used. Three paired runs reverse version order on the middle repetition.

## Exclusive phase costs

Scopes read grouped hardware instructions/cycles and a monotonic wall clock.
Counter events cover user-space CheckFile only, with no multiplexing. Child
costs are subtracted from parents. Every run's exclusive instructions,
cycles and wall nanoseconds sum exactly to its inclusive CheckFile cost.
Reparsing and repeat source analysis after structural edits remain counted
in their respective phases, not also in structural processing.

Each row sums three-run phase medians across corpora; counters are divided
by total original input bytes. The total row sums these phase medians, not
medians of an independently executed combined process.

All values below are main -> branch.

| Phase | Instructions/byte | Cycles/byte | Wall seconds |
| --- | ---: | ---: | ---: |
| Tokenization and delimiter matching | 33.67 -> 33.68 | 21.16 -> 21.43 | 1.106 -> 1.124 |
| Parsing and tree association | 55.65 -> 55.65 | 41.06 -> 41.87 | 2.273 -> 2.338 |
| Source analysis and safety | 156.63 -> 130.37 | 72.69 -> 61.49 | 3.850 -> 3.333 |
| Spacing and classification | 71.44 -> 71.32 | 39.91 -> 40.65 | 2.061 -> 2.108 |
| Lowering and document construction | 347.45 -> 356.24 | 191.83 -> 195.76 | 10.490 -> 10.700 |
| Rendering | 229.70 -> 216.99 | 144.01 -> 122.83 | 7.645 -> 6.558 |
| Structural and namespace processing | 71.91 -> 71.56 | 33.59 -> 33.88 | 1.801 -> 1.817 |
| Map composition | 65.48 -> 3.38 | 23.41 -> 1.77 | 1.325 -> 0.110 |
| Diagnostics and position projection | 50.69 -> 50.69 | 20.41 -> 20.57 | 1.110 -> 1.126 |
| Orchestration and other CheckFile work | 26.29 -> 19.13 | 30.41 -> 18.55 | 1.954 -> 1.344 |
| Sum of phase medians | 1108.92 -> 1008.99 | 618.47 -> 558.79 | 33.614 -> 30.559 |

The branch's largest exclusive costs are lowering/construction (35.3% of
instructions, 35.0% of wall time), rendering (21.5% of each), and source
analysis/safety (12.9% of instructions, 10.9% of wall time). Lowering is not
a measured win here: its instructions increase about 2.5%. The main savings
come from map composition, source analysis, rendering and orchestration.

## Boundaries and interpretation

- Tokenization includes delimiter pairing and token-store sealing. Parsing
  includes grammar work, comment/blank-line association, setup, and rebinds.
- Analysis is BuildSourceAnalysis, including protection and safety facts.
- Spacing is BuildSyntaxSpacing. Lowering is BuildSyntaxDocument, including
  planning, forest work and document construction. Rendering is Document::Render.
- Structural/namespace phases include planning, resolution and materialization;
  nested parse/analysis work is removed from these exclusive costs.
- Map composition covers token/protection and ordinary full-map composition;
  primary runs request the unchanged configurations' default maps, not forced
  full maps. Diagnostics include width checks and result-position projection.
- Other includes unscoped CheckFile/formatting work, semantic checks and arena
  lifetime management. Allocation costs are charged to the scope invoking them.

Instrumentation changes generated code and adds counter-read syscalls. Its
aggregate instruction totals are about 0.92% above the uninstrumented main
and 0.35% above the uninstrumented branch. Wall time and cycles also vary
with instrumentation and environmental noise. Small phase differences are
not independently proven production regressions or improvements.

Use the [uninstrumented follow-up](hotspot-follow-up.md) for the accepted
overall runtime comparison: 34.956 -> 29.839 CheckFile seconds. Do not replace
that total with this instrumented profile's sum or mix the two sets of costs.
Neither wall measurement includes input I/O or outer result destruction.

## Validation and evidence

All profile timing runs have zero failures/skips and process every selected
byte. Instrumented branch full-map evidence was streamed and compared
byte-for-byte with integrated-main evidence for all three corpora. Output,
diagnostics, positions, structural data, identities and protection match.

External artifacts are in /state/data/compact-pipeline/:

- phase-hardware-override/util/phase_profiler.hpp: bounded nested collector.
- build-phase-hardware.py: staging and recompilation of scoped objects.
- phase-hardware-next-{baseline,candidate}-*: staged libraries, raw timing
  observations and phase logs; original libraries remain unchanged.
- summarize-phase-hardware.py and phase-hardware-next-summary.{json,txt}:
  scope-to-phase grouping, raw sum assertions, per-corpus runs and medians.
- phase-hardware-next-validation-*: streaming full-map validation logs.
