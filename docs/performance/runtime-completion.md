# Integrated runtime validation

This report supersedes the runtime conclusions in
[the initial measurements](compact-pipeline.md) and
[the first follow-up](runtime-follow-up.md). Their corpus-selection audit
and intermediate implementation history remain applicable.

## Final implementation

The retained pipeline compacts document atoms and payloads, specializes
renderer caches, shares leaf and protection facts, streams ordered syntax
bodies through the existing validated emitter, avoids copying construction
drafts, writes diagnostic token positions directly, and derives flat width
from prefix summaries. Specialized ownership and forest validation remain;
this is not a claim that every lowering pass has disappeared.

The final runtime change, `d3e8c3b`, consolidates common arena allocations
into bounded, input-local backing. The initial file block scales with token
count from 4 KiB to 1 MiB. A 1 KiB to 256 KiB slice provides reusable scratch
backing. Scratch overflow uses its normal upstream resource and is freed
on reset rather than retained in the file arena. All backing is destroyed
when the operation finishes; nothing is cached across files or placed in
an input-dependent stack buffer.

Service profiling identified increased heap growth/trim calls, not just
lowering computation. Several alternatives were rejected: scratch-only
backing, separate initial buffers, shared unbounded monotonic backing,
pool resources, and a deferred-free block recycler. Extending the recycler
into parsing also failed to resolve the fixed-input regression. None of
that allocator machinery or its experimental parser API is retained.

## Main integration

`e5ce138` incorporates main at
`e102de76719e3cf28eaf291210c81432dfd5bfbd`. The integration preserves main's
Unicode code-point width handling while retaining the ASCII fast path.
Hunk formatting retains full source associations for edit authority;
whole-file checking requests only the diagnostic token information needed.
The leaf-fact property test now uses explicit code-point width expectations.
No golden expectations were regenerated during integration.

`9724515` also incorporates main's subsequent CI-only update,
`ae724b863b7557a9393ed8dd866d5cb0c1dd3080`. This update changes no measured
source code. The comparison baseline is an archived, independent Release
build of `e102de7`; the candidate contains the same source-level main fixes
plus the optimization commits. Earlier comparisons against `6d36496` must
not be substituted for this integrated comparison.

## Method

Both sides use GCC 13.3, C++20, Release `-O3 -DNDEBUG`, the same configuration
files and immutable corpus inputs, one worker pinned to CPU 2, and no
incremental cache. Builds and measurements run sequentially. Three
interleaved corpus comparisons reverse execution order on the middle run.
The shipped YTsaurus, LLVM and Google configurations remain byte-identical
to the copies used for both sides. Every selected input's length and SHA256
was checked again after validation.

| Corpus | Files | Bytes |
| --- | ---: | ---: |
| YTsaurus | 9,007 | 66,841,052 |
| LLVM/Clang | 3,917 | 78,691,166 |
| TensorFlow | 5,750 | 50,435,035 |

Primary timings write no correctness evidence. CheckFile wall time is the
sum of individual checks; whole-process CPU also includes harness work.
Explicit-full-map evidence generation is a separate correctness run and
must not be used as the primary runtime measurement.

## Integrated runtime results

Three-run medians, baseline to candidate:

| Corpus | CheckFile seconds | Change | Process CPU seconds | Change |
| --- | ---: | ---: | ---: | ---: |
| YTsaurus | 11.1217 -> 10.1118 | -9.08% | 11.5183 -> 10.4964 | -8.87% |
| LLVM/Clang | 14.9000 -> 12.8764 | -13.58% | 15.5508 -> 13.1072 | -15.71% |
| TensorFlow | 7.5531 -> 6.7576 | -10.53% | 7.9536 -> 6.9980 | -12.02% |

The existing default/preserving fixed-input benchmarks also improve:

| Case | CPU microseconds | Change |
| --- | ---: | ---: |
| Service | 1798.14 -> 1489.04 | -17.19% |
| Template library | 1867.47 -> 1363.08 | -27.01% |
| Platform header | 795.19 -> 743.73 | -6.47% |
| Mixed corpus | 4307.43 -> 3588.50 | -16.69% |

Service and template use three interleaved repetitions. Platform and mixed
use six: three additional reverse-order pairs at a longer minimum duration
check the noisier initial results. Individual pairs still vary; these are
median comparisons, not a guarantee of a speedup on every invocation.
Runtime gains, rather than compact record sizes, determine acceptance.

## Hardware instructions and cycles per input byte

A separate three-run interleaved comparison measures hardware events on
CPU 2 with the same baseline, candidate, inputs, and configurations.
Direct perf_event_open groups count user-space retired instructions and
CPU cycles around each CheckFile call. Both events run simultaneously;
all recorded enabled/running times agree, without multiplexing or scaling.
The normal perf command fell back to software events in this environment,
so its output is not used for these hardware measurements.

Counts exclude input reading, configuration preparation, evidence/report
writing, outer result destruction, and kernel work. They include the common
allocation-counting instrumentation. Small counter-control overhead is
identical on both sides. Divide median event counts by original input bytes,
not output bytes. These differ in scope from whole-process CPU timings.

| Corpus | Main instructions/B | Branch instructions/B | Main cycles/B | Branch cycles/B |
| --- | ---: | ---: | ---: | ---: |
| YTsaurus | 1099.4 | 1039.9 | 622.7 | 551.0 |
| LLVM/Clang | 1160.1 | 1084.7 | 689.1 | 666.4 |
| TensorFlow | 1002.5 | 939.9 | 604.9 | 554.0 |
| Combined | 1098.8 | 1032.2 | 644.8 | 598.1 |

The combined row divides the sum of corpus median counts by the total
195,967,253 input bytes: 6.07% fewer instructions and 7.25% fewer user-space
cycles per byte. Instructions are stable across repetitions. Cycle counts
are materially noisier; for example, YTsaurus spans 590.7-750.1 cycles/B on
main and 539.3-894.8 on the branch. Do not interpret these medians as a
speedup on every invocation or equate them with whole-process CPU savings.

## Allocation and memory results

Primary-run medians; requested bytes are cumulative allocation requests,
not simultaneously live memory. Measurements include harness allocations.

| Corpus | Allocation calls | Requested bytes | Maximum RSS KiB |
| --- | ---: | ---: | ---: |
| YTsaurus | 869,518 -> 534,379 | 12,996,134,421 -> 12,066,728,209 | 60,904 -> 56,396 |
| LLVM/Clang | 470,206 -> 301,379 | 13,745,725,842 -> 12,296,597,390 | 217,696 -> 191,044 |
| TensorFlow | 343,964 -> 123,201 | 8,391,862,054 -> 7,796,006,970 | 39,488 -> 34,272 |

Bounded initial backing increases requested capacity relative to the
preceding compact implementation. It is retained for its runtime benefit,
not as an allocation-capacity optimization. The complete integrated
pipeline still lowers requested bytes and peak RSS versus integrated main.

## Correctness and final checks

All 1,266 GoogleTest cases and the separate allocation checks pass. The
repository formatter check and `git diff --check` pass after reviewing
formatter changes. No tests invoke the CLI.

Serialized correctness evidence matches integrated main byte-for-byte for
all 18,674 selected files, including output, diagnostics, original token
positions, explicit full-map identities and spans, structural changes and
protection information. Both sides have zero failures and zero skips.
Main's intervening fixes resolve the old YTsaurus lowering failure; that
resolution is not attributed to these performance changes.

The integrated semantic/width diagnostic counts are respectively
1,079/2,114 for YTsaurus, 31,441/5,862 for LLVM/Clang, and 0/7,708 for
TensorFlow. Changes from the older baseline's counts reflect main's fixes;
the integrated baseline and candidate counts agree.

The original model-perf worktree and untracked repros remain untouched.
The optimization branch contains the integrated main revisions and has no
pending experiment. Runtime validation was completed before publication.

## External reproducibility artifacts

Artifacts remain outside Git under `/state/data/compact-pipeline/`:

- `integration-baseline-src/` and `integration-baseline-build/` preserve the
  independent integrated-main baseline.
- `integration-{baseline,candidate}-{measure,micro}` are the compared
  harness and benchmark executables.
- `integration-{baseline,candidate}-{corpus}-{1,2,3}.json` and matching TSVs
  preserve primary observations; `integration-summary.json` aggregates them.
- `integration-{baseline,candidate}-{case}-{rep}.json` preserve fixed-input
  results. Platform and mixed include repetitions 4 through 6.
- `evidence/integration-{baseline,candidate}-{corpus}.bin` and
  `integration-validation-*` preserve matching correctness evidence.
- `integration-formatted-{build,tests,allocations}.log` and
  `integration-format-check.log` preserve final validation.
- `hardware_counters.hpp`, `hardware-measure.cpp`, and
  `hardware-{baseline,candidate}-measure` preserve the hardware-count harness.
  `hardware-{baseline,candidate}-{corpus}-{1,2,3}.jsonl` and matching TSVs
  preserve observations; `hardware-summary.json` aggregates their counts.
- `bounded-final-*` preserve the preceding original-main comparison;
  `single-buffer-*`, `buffer-reuse-*`, `bounded-scratch-*`, and rejected
  patches preserve allocator investigation without adding it to Git.
