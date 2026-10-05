# Compact pipeline measurements

## Scope and conclusions

Baseline: `6d36496ba097337c7ca69e73654e3c1a32258985`, the freshly
fetched main revision at the start of this work. Final code: `8a9e1e3`.

The pipeline remains input -> tokens -> lossless tree -> compact document
IR -> output, with shared safety/spacing facts and necessary structural
normalization. This is not a second formatter or a restricted C++ mode.

Measured reductions are primarily in memory: cumulative allocation
requests fall about 19-21%, and peak RSS about 15-18%. Final median
CheckFile time changes are approximately -0.2% YTsaurus, -6.3% LLVM/Clang,
and -1.6% TensorFlow. Three repetitions and overlapping ranges do not
establish a general throughput improvement. Fixed-input preserving-layout
benchmarks include material regressions, documented below. There is no
Ruff-comparable performance claim.

Implementation commits, each validated before proceeding:

- `9979c42`: 80-byte retained atoms become 32-byte tagged/indexed atoms.
  Simple text/gaps are inline; complex alternatives, group/anchor data,
  and prefixes are arena-backed side records. Source runs remain compact.
- `e618bdf`: dense renderer cache becomes 16 bytes rather than 56;
  group/gap metadata is sparse. Policy-local ASCII facts avoid repeated
  advancement scans; multiline measurement stops at the first newline.
  Existing suffix summaries and bounded owned-boundary probes remain.
- `0a8925c`: balanced body intervals stream in syntax preorder alongside
  sorted specialized owners. The same validator/emitter is used, with
  interval-planning fallback for non-source-ordered projections.
  Contextual and opaque protection facts share one token sweep.
- `154e929`: explicit TokenMap requests retain original-token offsets
  without full source maps. File diagnostics request only what they use.
  Full SourceMap requests and internal namespace/protection anchors remain.
- `8a9e1e3`: borrow transient construction drafts while packing atoms,
  rather than copying the full draft through Add. Ownership and logical
  positions are assigned to retained records; allocation totals do not
  change.

## Corpus selection

Every primary comparison uses the same original bytes, revisions and
unchanged shipped example configuration. Files are content-deduplicated
within each corpus; final manifests also have zero cross-corpus exact
content duplicates. No small subset or repeated synthetic input is used
as a primary corpus.

| Corpus | Exact revision | Selected files | Selected bytes |
| --- | --- | ---: | ---: |
| YTsaurus | `d7aff49af1aac895248a7ea0756c01c4b2644580` | 9,007 | 66,841,052 |
| LLVM/Clang | `8d63a4d4cc75f018a7e376ba1247f9e343438600` | 3,917 | 78,691,166 |
| TensorFlow | `930d88b8f4ac7372602ff5bc576da30f1ce9c67d` | 5,750 | 50,435,035 |

Selection includes these substantial source families:

| Corpus | Directory | Files | Bytes |
| --- | --- | ---: | ---: |
| YTsaurus | `yt/yt` | 7,615 | 56,763,275 |
| YTsaurus | `yt/chyt` | 168 | 1,371,430 |
| YTsaurus | `yt/yql` | 850 | 7,461,013 |
| YTsaurus | `library/cpp/yt` | 374 | 1,245,334 |
| LLVM/Clang | `llvm/lib/IR` | 88 | 3,078,157 |
| LLVM/Clang | `llvm/lib/Analysis` | 124 | 4,173,564 |
| LLVM/Clang | `llvm/lib/Transforms` | 368 | 17,995,108 |
| LLVM/Clang | `llvm/include/llvm` | 2,160 | 19,587,376 |
| LLVM/Clang | `clang/lib/AST` | 157 | 7,173,749 |
| LLVM/Clang | `clang/lib/Basic` | 102 | 1,249,062 |
| LLVM/Clang | `clang/lib/Lex` | 27 | 1,209,454 |
| LLVM/Clang | `clang/lib/Sema` | 95 | 13,377,729 |
| LLVM/Clang | `clang/include/clang` | 796 | 10,846,967 |
| TensorFlow | `tensorflow/core` | 3,598 | 30,965,237 |
| TensorFlow | `tensorflow/compiler` | 1,787 | 17,121,415 |
| TensorFlow | `tensorflow/c` | 276 | 1,744,261 |
| TensorFlow | `tensorflow/cc` | 89 | 604,122 |

Rules exclude vendored/third-party, build, generated, test, fixture and
benchmark directories/files, detected generated headers, and shipped
YTsaurus exclusions. Accepted C/C++ extensions, exact regexes, every
selected path/digest, and every exclusion/reason are in the manifests
and prepare.py, not inferred from the formatted output.

A final manual audit additionally excluded the two machine-expanded
YTsaurus calendar lookup tables dates_lut_localtime.cpp and
dates_lut_utc.cpp under yt/yt/library/query/engine/time; neither has a
normal generated-header marker. All YTsaurus validation and primary
comparisons were rerun after this correction. Earlier selection-v1 and
profile-ir prototype measurements are superseded and are not reported
here. Preliminary small LLVM/Google and gRPC selections were rejected.

One baseline and unchanged final processing failure remains:
`library/cpp/yt/error/error.cpp`, 24,597 bytes,
`syntax lowering failed: code 4 token 846 core 0`.
Every run attempts it and includes its failure time; successful-byte and
allocation totals exclude it. Per-file TSVs record its allocation costs.
It is not silently skipped or replaced by a verbatim fallback.

| Corpus | Successful files | Successful bytes | Failures | Skips | Semantic diagnostics | Width diagnostics |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| YTsaurus | 9,006 | 66,816,455 | 1 | 0 | 1,082 | 2,228 |
| LLVM/Clang | 3,917 | 78,691,166 | 0 | 0 | 31,494 | 5,647 |
| TensorFlow | 5,750 | 50,435,035 | 0 | 0 | 0 | 7,272 |

## Measurement method

- AMD Ryzen 7 2700X, x86-64 WSL2 Linux 6.18.40.1; GCC 13.3.0,
  libstdc++, C++20, Release, -O3 -DNDEBUG, Ninja 1.11.1, CMake 3.28.3.
- Session limits: 4 GiB RAM, no swap, two-core CPU quota, 256 tasks.
  Builds use -j1. Corpus workers are single-threaded, pinned to CPU 2.
- The library CheckFile harness prepares configuration once, rereads
  immutable padded input each run, and never writes to the corpus.
  Final file digests were checked against the selection manifests.
  No incremental-result cache or formatted-output feedback is used.
- Three interleaved before/after repetitions per step and final comparison.
  Compare within each pair/window: step timings are not additive and
  host load/frequency varies. min/max ranges are shown, not confidence
  intervals. No diagnostics-disabled variant is a primary measurement.
- Wall seconds sum CheckFile calls, including the known failure; disk
  reading, configuration preparation, evidence serialization and result
  destruction are outside that wall interval. Whole-process user+system
  CPU includes reads, TSV logging, and result destruction.
- Allocation calls/requested bytes count C++ normal and aligned allocations
  during successful CheckFile calls. Requested bytes are cumulative
  upstream requests, not live memory, retained IR bytes, or peak RSS.
  C malloc calls bypassing C++ new are not counted. Peak RSS is the maximum
  process high-water mark across the three runs, including loader/harness.
- Primary repetitions use /dev/null evidence mode. Separate validation
  runs serialize full evidence and perform an additional full-map format;
  their CPU/RSS cannot be used as the primary comparisons.

Configuration contents are unchanged copies of
hit-fmt/examples/{ytsaurus,llvm,google}.hit-fmt at the baseline revision.
Copies and SHA256 digests are retained externally:

| Configuration | SHA256 |
| --- | --- |
| ytsaurus.hit-fmt | ca39dd008de268fb81bacd292e46104f442e07002621032904fedb0f62738910 |
| llvm.hit-fmt | b947f982b9deb96bf3191169b5c978829268160054f5fb18543715783b17d942 |
| google.hit-fmt | 59f382e0b459ed956e6a3eed760ab72c9d934a4eb71cf369c25a257b1a82f193 |

## Per-step runtime

Seconds are median [minimum, maximum]. Negative changes are faster.

| Step | Corpus | Before seconds | After seconds | Change |
| --- | --- | --- | --- | ---: |
| Compact IR | YTsaurus | 9.749 [9.693, 10.504] | 9.915 [9.867, 10.136] | +1.69% |
| Compact IR | LLVM/Clang | 12.395 [12.319, 12.530] | 12.726 [12.720, 12.865] | +2.67% |
| Compact IR | TensorFlow | 6.769 [6.610, 6.849] | 6.849 [6.748, 6.941] | +1.19% |
| Renderer | YTsaurus | 9.942 [9.830, 10.265] | 9.895 [9.833, 10.117] | -0.47% |
| Renderer | LLVM/Clang | 14.464 [14.154, 15.915] | 14.059 [13.880, 15.332] | -2.80% |
| Renderer | TensorFlow | 7.847 [7.659, 7.926] | 7.658 [7.604, 8.230] | -2.40% |
| Lowering | YTsaurus | 10.111 [10.068, 10.238] | 9.914 [9.905, 10.050] | -1.94% |
| Lowering | LLVM/Clang | 13.922 [13.034, 14.063] | 13.745 [13.089, 13.852] | -1.28% |
| Lowering | TensorFlow | 7.401 [7.014, 7.546] | 7.505 [7.023, 7.556] | +1.40% |
| Token provenance | YTsaurus | 10.018 [9.968, 10.023] | 9.503 [9.496, 9.507] | -5.14% |
| Token provenance | LLVM/Clang | 12.670 [12.627, 12.815] | 12.205 [12.093, 12.281] | -3.67% |
| Token provenance | TensorFlow | 6.830 [6.794, 7.376] | 6.586 [6.551, 6.640] | -3.57% |
| Borrowed drafts | YTsaurus | 9.736 [9.624, 9.753] | 9.548 [9.490, 9.626] | -1.94% |
| Borrowed drafts | LLVM/Clang | 11.960 [11.958, 12.315] | 12.138 [11.788, 12.288] | +1.49% |
| Borrowed drafts | TensorFlow | 6.899 [6.715, 6.967] | 6.642 [6.564, 6.931] | -3.72% |
| Final | YTsaurus | 9.962 [9.837, 10.539] | 9.946 [9.568, 10.129] | -0.16% |
| Final | LLVM/Clang | 13.320 [12.526, 13.518] | 12.487 [12.074, 12.652] | -6.25% |
| Final | TensorFlow | 6.895 [6.779, 7.346] | 6.782 [6.684, 6.820] | -1.64% |

Compact IR alone regresses median time in all three corpora while reducing
requested capacity and RSS; it also adds side-table allocation calls.
Renderer medians improve in these windows, but TensorFlow RSS increases.
Lowering barely changes allocation capacity; its TensorFlow median time
regresses, with overlapping ranges. Conditional provenance produces the
clearest isolated wall-time and capacity reductions. Borrowed drafts
have unchanged allocation totals and mixed/noisy timing results.

## Allocation and peak-memory results

Allocation counts and capacity are exact repeated-run totals; capacity
columns use GiB. RSS uses MiB. Each cell is before -> after.

| Step | Corpus | Allocation calls | Requested GiB | Peak RSS MiB |
| --- | --- | ---: | ---: | ---: |
| Compact IR | YTsaurus | 869,673 -> 870,456 | 12.078 -> 11.527 | 59.64 -> 53.64 |
| Compact IR | LLVM/Clang | 470,262 -> 471,223 | 12.768 -> 12.375 | 211.66 -> 194.58 |
| Compact IR | TensorFlow | 343,526 -> 344,356 | 7.806 -> 7.429 | 38.41 -> 35.72 |
| Renderer | YTsaurus | 870,456 -> 867,319 | 11.527 -> 10.508 | 53.62 -> 51.74 |
| Renderer | LLVM/Clang | 471,223 -> 469,350 | 12.375 -> 11.197 | 194.53 -> 185.77 |
| Renderer | TensorFlow | 344,356 -> 341,846 | 7.429 -> 6.720 | 35.78 -> 37.73 |
| Lowering | YTsaurus | 867,319 -> 867,318 | 10.508 -> 10.508 | 51.81 -> 51.77 |
| Lowering | LLVM/Clang | 469,350 -> 469,347 | 11.197 -> 11.197 | 185.77 -> 185.84 |
| Lowering | TensorFlow | 341,846 -> 341,842 | 6.720 -> 6.720 | 37.77 -> 37.72 |
| Token provenance | YTsaurus | 867,318 -> 854,562 | 10.508 -> 9.799 | 51.78 -> 48.98 |
| Token provenance | LLVM/Clang | 469,347 -> 461,725 | 11.197 -> 10.238 | 185.84 -> 180.74 |
| Token provenance | TensorFlow | 341,842 -> 334,244 | 6.720 -> 6.205 | 37.76 -> 32.80 |
| Borrowed drafts | YTsaurus | 854,562 -> 854,562 | 9.799 -> 9.799 | 49.02 -> 49.04 |
| Borrowed drafts | LLVM/Clang | 461,725 -> 461,725 | 10.238 -> 10.238 | 180.80 -> 180.77 |
| Borrowed drafts | TensorFlow | 334,244 -> 334,244 | 6.205 -> 6.205 | 32.82 -> 32.82 |
| Final | YTsaurus | 869,673 -> 854,562 | 12.078 -> 9.799 | 59.58 -> 48.97 |
| Final | LLVM/Clang | 470,262 -> 461,725 | 12.768 -> 10.238 | 211.70 -> 180.74 |
| Final | TensorFlow | 343,526 -> 334,244 | 7.806 -> 6.205 | 38.42 -> 32.82 |

## Whole-process CPU

User+system seconds, median [minimum, maximum]. These are not the sum
of the instrumented phases and include harness work described above.

| Step | Corpus | Before CPU seconds | After CPU seconds |
| --- | --- | --- | --- |
| Compact IR | YTsaurus | 10.265 [10.060, 10.858] | 10.235 [10.195, 10.489] |
| Compact IR | LLVM/Clang | 12.643 [12.579, 12.802] | 13.027 [13.022, 13.172] |
| Compact IR | TensorFlow | 6.989 [6.818, 7.071] | 7.064 [6.955, 7.164] |
| Renderer | YTsaurus | 10.266 [10.147, 10.630] | 10.214 [10.144, 10.437] |
| Renderer | LLVM/Clang | 14.819 [14.508, 16.328] | 14.356 [14.157, 15.680] |
| Renderer | TensorFlow | 8.104 [7.883, 8.223] | 7.926 [7.846, 8.536] |
| Lowering | YTsaurus | 10.454 [10.397, 10.600] | 10.250 [10.240, 10.385] |
| Lowering | LLVM/Clang | 14.220 [13.298, 14.333] | 14.033 [13.340, 14.145] |
| Lowering | TensorFlow | 7.597 [7.242, 7.801] | 7.763 [7.248, 7.801] |
| Token provenance | YTsaurus | 10.348 [10.280, 10.352] | 9.811 [9.798, 9.812] |
| Token provenance | LLVM/Clang | 12.917 [12.844, 13.064] | 12.423 [12.314, 12.472] |
| Token provenance | TensorFlow | 7.026 [6.998, 7.649] | 6.774 [6.736, 6.825] |
| Borrowed drafts | YTsaurus | 10.057 [9.937, 10.438] | 9.839 [9.775, 9.949] |
| Borrowed drafts | LLVM/Clang | 12.182 [12.164, 12.716] | 12.332 [11.993, 12.496] |
| Borrowed drafts | TensorFlow | 7.184 [6.909, 7.325] | 6.838 [6.760, 7.149] |
| Final | YTsaurus | 10.294 [10.170, 10.886] | 10.285 [9.871, 10.477] |
| Final | LLVM/Clang | 13.610 [12.765, 13.756] | 12.687 [12.297, 12.872] |
| Final | TensorFlow | 7.101 [6.988, 7.608] | 6.999 [6.861, 7.038] |

## Existing fixed-input benchmarks

Existing formatter-benchmark cases use their original default/preserving
configuration, not the shipped corpus configurations above. Baseline and
final were interleaved three times on CPU 2 with 0.1s minimum runs. CPU
microseconds are median [minimum, maximum]. These are supplementary, not
primary corpus results. Results include significant service/platform
source-format regressions and do not support a universal speedup claim.

| Case | Baseline CPU us | Final CPU us | Change |
| --- | --- | --- | ---: |
| FormatService | 1710.5 [1690.2, 1933.1] | 2056.1 [2034.0, 2358.7] | +20.21% |
| FormatTemplateLibrary | 1674.5 [1667.6, 1774.6] | 1663.4 [1656.8, 1918.8] | -0.67% |
| FormatPlatformHeader | 579.3 [555.4, 671.2] | 740.5 [717.8, 815.0] | +27.83% |
| FormatMixedCorpus | 4426.9 [3935.8, 4574.2] | 4394.4 [4336.3, 5163.6] | -0.74% |
| FormatParsedService | 1586.9 [1546.0, 1754.3] | 1727.1 [1579.0, 1775.1] | +8.83% |
| FormatParsedTemplateLibrary | 1352.6 [1314.2, 1512.6] | 1466.4 [1393.7, 1484.5] | +8.41% |
| FormatParsedPlatformHeader | 479.6 [467.3, 492.4] | 460.4 [451.1, 473.9] | -4.01% |
| FormatParsedMixedCorpus | 3645.2 [3276.5, 3711.4] | 3572.3 [3241.0, 3872.9] | -2.00% |

An additional prototype split ordinary/conditional gap payloads and
stored prefixes as flat run parts. Although record sizes shrank and all
serialized correctness evidence matched, allocation capacity increased
about 2-2.4% and fixed-input timings did not improve. It was rejected;
its patch and measurements are retained externally, not in this branch.
Compact record size alone is not proof of a faster or smaller pipeline.

## Coarse phase profile and IR counts

Separate instrumented Release builds use HIT_FMT_PROFILE_PHASES=1.
One run per version/corpus provides indicative attribution only, not the
primary runtime evidence. Exclusive seconds below avoid double-counting
nested group-ordering phases. Structural edits still reparse when needed.

| Phase | YTsaurus before -> after | LLVM/Clang before -> after | TensorFlow before -> after |
| --- | ---: | ---: | ---: |
| parser.tokenize_and_delimiters | 0.390 -> 0.363 | 0.420 -> 0.403 | 0.244 -> 0.244 |
| parser.syntax | 0.654 -> 0.606 | 0.685 -> 0.650 | 0.371 -> 0.371 |
| analysis | 1.449 -> 1.369 | 1.606 -> 1.568 | 0.818 -> 0.830 |
| spacing | 0.740 -> 0.687 | 0.760 -> 0.727 | 0.441 -> 0.446 |
| structural_plan | 0.248 -> 0.232 | 0.272 -> 0.257 | 0.147 -> 0.148 |
| lowering | 3.569 -> 3.367 | 4.332 -> 3.839 | 2.054 -> 2.073 |
| group_ordering | 0.077 -> 0.088 | 0.095 -> 0.111 | 0.042 -> 0.055 |
| rendering | 2.299 -> 2.677 | 3.326 -> 3.718 | 1.795 -> 2.168 |
| compose_full_maps | 0.425 -> 0.000 | 0.558 -> 0.000 | 0.307 -> 0.000 |
| compose_token_maps | 0.000 -> 0.127 | 0.000 -> 0.184 | 0.000 -> 0.109 |
| namespace_resolve | 0.205 -> 0.186 | 0.225 -> 0.211 | 0.134 -> 0.134 |
| semantic_positions | 0.011 -> 0.010 | 0.151 -> 0.148 | 0.001 -> 0.001 |
| width_diagnostics | 0.102 -> 0.094 | 0.121 -> 0.113 | 0.075 -> 0.075 |
| width_positions | 0.096 -> 0.084 | 0.205 -> 0.187 | 0.096 -> 0.092 |

These profiles show renderer and group-ordering regressions despite the
smaller caches/order arrays; indirection and extra stream traversal remain
costs to investigate. Do not infer a phase speedup from record size.

Retained atom size is asserted at 32 bytes (previously 80); inline text
and gap records are asserted at no more than 12 bytes. Run parts remain
16 bytes. Complex gap/group/prefix side payloads are 60/36/40 bytes.
Renderer dense cache is 16 bytes (previously 56), with separate 40-byte
group and 16-byte gap cache records. A transient construction draft is
still larger; it is not retained per atom.

The following are sums across successful files, not simultaneous live
counts. Atom/group/anchor/gap/run-part/prefix counts match the baseline.

| IR inventory | YTsaurus | LLVM/Clang | TensorFlow |
| --- | ---: | ---: | ---: |
| Atoms | 16,259,664 | 17,963,625 | 10,144,384 |
| Groups | 2,186,642 | 2,514,705 | 1,360,045 |
| Anchors | 2,287,524 | 2,287,280 | 1,323,045 |
| Source-run parts | 10,435,433 | 12,453,527 | 7,603,234 |
| Gaps | 3,741,683 | 3,989,031 | 2,215,107 |
| Prefixes | 846,145 | 750,229 | 653,526 |
| Conditional text | 9,775 | 0 | 0 |
| Complex gap side records | 2,379,432 | 2,723,317 | 1,413,099 |
| Group/anchor side records | 4,474,166 | 4,801,985 | 2,683,090 |

| Interval-order inventory | YTsaurus | LLVM/Clang | TensorFlow |
| --- | ---: | ---: | ---: |
| Previously sorted groups | 2,186,642 | 2,514,705 | 1,360,045 |
| Currently sorted specialized groups | 1,973,621 | 2,313,850 | 1,235,061 |

## Remaining expensive structures and work

| Structure/work | Purpose and lifetime; why retained |
| --- | --- |
| Padded source, tokens, delimiter/punctuation facts, lossless syntax tree | Per-file ownership. Exact spelling, diagnostics, stable token identity, recovery and structural edits depend on them; no token/tree layer is skipped. |
| SourceAnalysis gaps, regions, lines and protection | File-arena derived facts shared by spacing, structural planning and lowering. Gap replacement still checks lexical separation, protection, uncertainty and requested edit scope. Region facts also support diagnostic roles and normalization. |
| SyntaxSpacing gap preferences | Compact file-arena array, at most 8 bytes per gap. Encodes owner/context, priority, policy and uncertainty. Sharing computed preferences avoids rescanning syntax at each emission site. |
| Lowering Syntax_ projection | Scratch-arena region-aligned safety/root/sibling/operator/definition facts. One classifier traversal; not another parser/tree. Some classifications still duplicate SourceAnalysis/parser facts and are a future consolidation target. |
| Lowering Groups_ plus BlockOf_, ArgsOf_, BasesOf_, ExprOf_, InitOf_, ParamsOf_ and related index maps | Scratch ownership/layout metadata. Bodies stream without interval sorting when ordered, but list/header/chain/expression contexts still require reusable owners and references. Final ownership/nesting validation remains common to both paths. |
| Order_, Stack_, close/open emission scheduling | Scratch specialized-interval ordering and one shared forest validator/emitter. Natural body groups are omitted from Order_, not from validation or layout. Nonordered projections retain the complete interval fallback. |
| Gap owners/facts, comma and generated-punctuation plans, prefix counters | Scratch per-gap/token arrays. Required for unique ownership, layout topology, lexical safety, delimiter decisions, protected scopes and structural provenance. Contextual/opaque protection prefixes now share their source-range sweep; other gap-fact/emission passes remain. |
| Compact atoms, borrowed-text references, source-run parts and group/anchor/gap/prefix side tables | File-arena retained IR. Common cases no longer pay for the largest payload. Side-table indirection and capacity growth remain expensive. Some ordinary source gaps/prefixes still carry unused fields in the shared complex records; semantic necessity does not justify every byte. The failed smaller-record experiment demonstrates that better capacity planning is needed before splitting them further. |
| Renderer dense/sparse caches, suffix summaries, anchor slots and frames | Render-scoped temporary PMR storage; arena backing is bounded by the file operation. Needed for stable group/anchor identities, owned suffixes, conditional alternatives, exact columns and bounded lookahead. Sparse records are not free: extra indirection can regress small/default-layout cases. |
| StructuralPlan, provenance pieces, normalized source and reparsed tree | File-scoped when structural normalization occurs. Original and normalized identities/protection must coexist; generated braces, include moves and namespace edits cannot be treated as whitespace-only changes. No reparsing/safety validation was bypassed. |
| Token offsets, full maps, structural-operation records and output protection | Token offsets are retained only for requested diagnostics/token-map consumers or internal namespace anchors; full maps remain opt-in. Original identities and protected anchors are still internally mandatory for relevant structural operations. Explicit full-map output remains unchanged. |
| Original/emitted diagnostic coordinate sweeps and output-width scan | Required for unchanged messages, ordering and coordinates. No naming/width check is disabled by the optimization. Full original/output scans still cost time when diagnostics require them. |

Largest unresolved costs are lowering classification/ownership/emission,
rendering, source analysis, and syntax spacing. Most planning arrays still
scale with tokens/regions; body streaming removes only a subset of global
interval work. Further gains require measured consolidation/capacity
planning, not a reduced grammar or silent fallback.

## Validation and publication state

- All 1,221 GoogleTest cases and the separate allocation suite pass after
  each implementation stage and the final repository formatting pass.
- Full binary evidence compares byte-for-byte with baseline for every
  successful selected file and matches failure records for the known case.
  It contains formatted text, diagnostic messages/coordinates, map spans
  and identity/flags, structural operations/diagnostics, original-token
  offsets, output protection and semantic/width coordinate results.
- Validation separately formats with an explicit full map and compares
  its text/token offsets with the diagnostic-only pipeline. Shared public
  pipeline properties also compare unmapped, token-only and full-map
  requests, including structural edits and protected scopes.
- Existing source-run/rendering properties cover Unicode/invalid bytes,
  tabs, multiline content, exact fitting boundaries and mapping. The
  synthesized function-try golden extension covers nested namespaces,
  lambda, switch/case, catches and control bodies; its expected output
  was independently checked against the baseline formatter.
- Repository hit-fmt was run in fix mode with the repository .hit-fmt
  exclusions, followed by a clean diff/check. No additional formatting
  changes were needed. Source goldens/generated outputs are not blindly
  reformatted; the repository configuration excludes them.
- No PR was published. origin/main advanced during the measurements;
  this branch intentionally preserves its pinned baseline. Integration
  with newer main requires rebasing and renewed correctness/performance
  validation before publication. Original model-perf worktree and repros
  were not modified.

## Reproduction artifacts

External root: /state/data/compact-pipeline/. Corpora, profiling copies,
measurement binaries and temporary instrumentation are not committed.

- prepare.py, corpora.json and {corpus}-selection.json: exact revisions,
  directory/extension/filter rules, selected hashes and exclusions.
- configs/*.hit-fmt: original configuration contents; environment.txt and
  environment-delivery.txt: compiler/kernel/CPU/limits and artifact hashes.
- measure.cpp: single-worker CheckFile allocation/time/evidence harness.
  Baseline and each stage binary link its corresponding archived Release
  libraries. Primary invocation is taskset -c 2 {stage}-measure
  corpora/{corpus} configs/{corpus}.hit-fmt /dev/null {detail}.tsv.
- {step}-{corpus}-{1,2,3}.json and evidence/*.tsv: individual repetition
  metrics/statuses. Prefixes are compact-ir-baseline/compact-ir,
  renderer-before/renderer, lowering-before/lowering,
  provenance-before/provenance, draft-ref-before/draft-ref, and
  delivery-baseline/delivery. summary-final.json keeps exact byte totals.
- evidence/{baseline,compact-ir,renderer,lowering,provenance,draft-ref}
  -{corpus}.bin: streamed full-map correctness evidence, never loaded
  wholesale for comparison. Serialization is outside primary timing.
- instrument-final.py and phase-{baseline,final}-{corpus}.log/.json:
  opt-in coarse phase timing and retained-record/counter inventory.
- micro-interleave-{baseline,draft-ref}-{1,2,3}.json: existing fixed-input
  benchmark measurements; earlier noninterleaved runs are superseded.
- rejected-gap-refinement.patch and gap-refinement artifacts: rejected
  smaller-record experiment, preserved for analysis but not committed.

Selection manifest SHA256 digests:

| Corpus | SHA256 |
| --- | --- |
| YTsaurus | 2bf0632f9967509a6cba70d6ef071dbd78416376ecff1a649b8adea4201b8769 |
| LLVM/Clang | 7774b7b621143c063fea3dc1dcb1584fadd086c050178e55617353d2549fa2c9 |
| TensorFlow | 6eeef3f19fee72cd45f594602b37c0578b4e0461eeb4c4f77c97cdc3a927f9ab |
