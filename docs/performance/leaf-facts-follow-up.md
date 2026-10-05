# Lowering and rendering leaf-fact reuse

Implementation `c1d1dde` follows the [exclusive phase profile](phase-profile.md).
The comparison here is against the preceding branch `5c4a3bc`, not main.

## Changes

Width scans batch plain ASCII words with bounded, unaligned-safe reads and
retain scalar handling for controls, tabs, Unicode and malformed UTF8.
The 32-bit source/text bounds permit a 64-bit width accumulator, with one
final saturation instead of saturation at every code point.

Source emission records a positively validated, policy-independent plain
ASCII fact. Rendering reuses it for width summaries and column advancement,
including individual pieces of mixed source runs. Facts are computed by the
document itself, not trusted from an external producer. Nonplain text still
uses the complete existing measurement and tab-stop rules.

The fact occupies existing padding: atoms remain 32 bytes and run parts
16 bytes. Allocation counts and requested capacities are unchanged. No
new parser API, per-file fact vector, cross-file cache or safety bypass is
introduced. Existing properties now cover word-boundary lengths, tabs,
UTF8, malformed bytes, CRLF, embedded NUL/controls and mixed source runs.

## Runtime acceptance under variable host load

Whole-corpus sequential comparisons became highly unstable: unchanged
executables varied by more than 2x. These observations are preserved, but
are not used as acceptance speedup claims or a replacement for the prior
main-to-branch absolute totals.

The external measurement harness therefore supports persistent sequential
batch requests. Two processes retain configurations and file-path lists;
only one runs at a time, both pinned to CPU 2. Each processes the same next
50 files before advancing. Execution order reverses between batches and
repetitions. File contents are read normally; trees, formatting results and
file-scoped allocations are not cached. Every round covers the complete
pinned corpus with its unchanged shipped configuration. Wall timing retains
the original CheckFile scope. Batch CPU deltas exclude process startup and
configuration preparation, so they are not the earlier whole-process metric.

Six complete paired rounds give these median within-round wall reductions:

| Corpus | Median paired wall reduction | Faster pairs |
| --- | ---: | ---: |
| YTsaurus | 3.34% | 6/6 |
| LLVM/Clang | 3.07% | 5/6 |
| TensorFlow | 2.16% | 6/6 |

Ratios of the independently selected six-run corpus medians are 4.36%,
4.20% and 2.10% respectively. The paired ratios above better represent the
step under changing conditions. Neither interpretation establishes a new
absolute combined wall-time total against main.

Six short fixed-input comparisons favor service, template and mixed cases,
with CPU median reductions of 2.98%, 5.25% and 5.92%. Platform is inconclusive:
its initial six-run CPU median regresses 2.63%, while twelve additional
reverse-order pairs are essentially neutral (-0.09%). Individual pairs vary
substantially. No universal fixed-input speedup is claimed.

## Direct hardware and phase costs

Three interleaved uninstrumented counter runs show stable instruction wins:

| Corpus | Prior instructions/B | Current instructions/B | Reduction |
| --- | ---: | ---: | ---: |
| YTsaurus | 1013.43 | 970.79 | 4.21% |
| LLVM/Clang | 1056.90 | 1010.08 | 4.43% |
| TensorFlow | 914.65 | 873.51 | 4.50% |
| Byte-weighted combined | 1005.47 | 961.53 | 4.37% |

Counters cover user-space CheckFile with no multiplexing. Combined cycles/B
are 669.61 -> 679.72 (+1.51%); noisy cycles do not establish a cycle win.
Instruction reduction is not used instead of the paired runtime evidence.

Separate scoped measurements isolate the intended phases:

| Exclusive phase | Prior instructions/B | Current instructions/B | Reduction |
| --- | ---: | ---: | ---: |
| Lowering/document construction | 356.24 | 337.03 | 5.39% |
| Rendering | 216.99 | 191.86 | 11.58% |

Other phase instruction costs remain effectively unchanged. Scoped wall and
cycle observations are preserved, but broad changes in even untouched phases
show environmental drift; they are not independent phase-speedup claims.

## Fresh total comparison against main

A subsequent six-round comparison directly measures main `e102de7` against
implementation `c1d1dde`, using the same closer-interleaved persistent batches.
All 18 pairs favor the branch. This is a fresh measurement, not a compound
estimate from earlier percentages.

| Corpus | Main CheckFile seconds | Current seconds | Reduction |
| --- | ---: | ---: | ---: |
| YTsaurus | 10.6102 | 9.3787 | 11.61% |
| LLVM/Clang | 13.5178 | 11.6738 | 13.64% |
| TensorFlow | 7.2319 | 6.3748 | 11.85% |
| Sum of corpus medians | 31.3599 | 27.4273 | 12.54% |

This saves 3.9326 seconds of formatting wall time, or approximately 1.143x
throughput. Summed batch CPU medians are 32.2474 -> 28.2112 seconds (-12.52%).
Batch CPU excludes startup/configuration preparation; CheckFile wall excludes
input reading and outer result destruction. These totals are not end-to-end
CLI timings and are not directly interchangeable with the earlier whole-
process measurements or the noisier whole-corpus interleaving.

Three fresh interleaved hardware counter runs against main give combined
instructions/B 1098.83 -> 961.53 (-12.49%) and cycles/B 775.54 -> 656.43
(-15.36%). These counters retain their user-space CheckFile-only scope;
cycles remain substantially less stable than instruction counts.

External evidence: main-current-batch-* preserves all six complete rounds
and per-batch observations; main-current-hardware-* preserves three counter
pairs per corpus; main-current-total-summary.json preserves the aggregation.

## Rejected work and validation

A compact sort-key table saved only about 0.1% of whole-pipeline instructions
and added scratch allocation/moves without convincing runtime improvement.
It was removed. Width-only and word-only prototypes also lacked a reliable
runtime win; acceptance applies to the complete retained fact-reuse change.

All 1,266 direct tests and separate allocation checks pass. No goldens were
regenerated. All 18,674 inputs process without failures/skips, and full-map
serialized evidence matches integrated main byte-for-byte. Repository
formatting and git diff --check pass. Original worktree/repros remain intact.

Artifacts under /state/data/compact-pipeline/ include next-leaffacts-batch-*
(raw per-batch rows and six-round summaries), next-leaffacts-hardware-*,
next-leaffacts-micro-* (platform repetitions 1-18), phase-hardware-lower-render-*
(scope observations and sum assertions), next-leaffacts-final-* (validation
and formatting logs), and rejected-next-sortkeys.patch. The incomplete
next-leaffacts-final sequential comparison ended at the shell deadline;
no process remains running and those partial timings are not accepted.
