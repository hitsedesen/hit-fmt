# Runtime-focused follow-up

The [integrated runtime validation](runtime-completion.md) supersedes
these timing conclusions and resolves the fixed-input regressions below.

This follow-up uses the pinned inputs, unchanged example configurations,
Release settings, single worker, CPU affinity, and evidence contracts from
[the initial report](compact-pipeline.md). Runtime, not allocation request
counts or record sizes, is the acceptance criterion.

## Retained changes

- `19f6b64`: write identity-indexed diagnostic token positions during
  rendering. Token-only requests no longer retain intermediate per-token
  Mapping records. Tracked exact protection copies remain independently
  mapped and validated. Explicit full-map requests remain unchanged.
- `935e893`: derive a summary's flat width from its prefix and physical-line
  flag. This removes duplicate width arithmetic and shrinks summaries passed
  through the renderer hot path. Prefixes, physical boundaries, tab geometry,
  break selection, and logical probe limits remain unchanged.

The indexed-position step reduced median whole-process CPU by approximately
2.2%, 1.3%, and 1.7% on YTsaurus, LLVM/Clang, and TensorFlow in its isolated
three-run comparison. These are modest gains, not the main outcome.

The summary step's isolated LLVM/Clang and TensorFlow comparisons improved
CPU in all three pairs. YTsaurus had an outlier in the initial three pairs;
three additional reverse-order pairs all improved. Across its six pairs,
YTsaurus median CPU improved approximately 4%.

## Overall comparison against original main

Three interleaved runs per corpus, original baseline versus both retained
follow-up changes on the compact pipeline:

| Corpus | CheckFile median seconds | Change | Process CPU seconds | Change |
|---|---:|---:|---:|---:|
| YTsaurus | 9.77614 → 9.27250 | -5.15% | 10.105376 → 9.603969 | -4.96% |
| LLVM/Clang | 12.59260 → 11.51100 | -8.59% | 12.914690 → 11.735090 | -9.13% |
| TensorFlow | 6.67956 → 6.38623 | -4.39% | 6.882368 → 6.596525 | -4.15% |

CheckFile wall time excludes input reading and result destruction. Process
CPU includes harness work. These results supersede the initial overall
runtime measurements; they do not supersede its corpus-selection audit.
The unchanged YTsaurus failure is still attempted and timed.

All serialized output, diagnostics, maps, identities, structural operations,
token offsets, and protection evidence matched the original baseline
byte-for-byte on all three corpora. The direct GoogleTest suite passed all
1,221 cases, and the separate allocation check passed. Repository formatting
was applied to the changed source files.

## Remaining regressions and unfinished work

The fixed-input suite still prevents calling the entire optimization done.
Its latest three-run full-pipeline CPU medians against the original baseline:

| Case | Baseline → follow-up microseconds | Change |
|---|---:|---:|
| Service | 1637.50 → 1868.50 | +14.11% |
| Template library | 1564.78 → 1486.12 | -5.03% |
| Platform header | 544.59 → 517.27 | -5.02% |
| Mixed corpus | 3820.95 → 4017.30 | +5.14% |

These cases use their original default/preserving configurations, not the
shipped corpus configurations. The service and mixed-input regressions must
still be diagnosed, rather than dismissed because corpus timings improved.
Most lowering classification, ownership, forest validation, and planning
passes also remain. No grammar reduction or safety bypass was introduced.
The branch has not been rebased onto advanced main or published.

## Rejected experiments and profiling

Source-run output batching, prefix-addition reuse without the smaller
summary, synchronous mapping callbacks, and a unified dense renderer cache
were tested and rejected: timings were mixed or regressed other workloads.
Patches remain outside Git for investigation, not as retained improvements.

Native perf commands accepted events but produced no usable samples. A
bounded SIGPROF instruction-pointer sampler and a line-resolved profiling
build instead identified renderer summary/emission, source analysis, and
lowering construction as substantial costs. Sampling is diagnostic evidence,
not a replacement for the interleaved uninstrumented comparisons above.

External artifacts under `/state/data/compact-pipeline/`:

- `runtime-summary-final.json`: exact overall runtime observations.
- `runtime-delivery{,-baseline}-{corpus}-{1,2,3}.json`: overall comparisons.
- `runtime-indexed{,-before}-{corpus}-{1,2,3}.json`: indexed-position step.
- `runtime-measure8{,-before}-{corpus}-{rep}.json`: summary step; six YTsaurus
  pairs, three LLVM/Clang and TensorFlow pairs.
- `runtime-measure8-micro{,-baseline}-{1,2,3}.json`: fixed-input comparisons.
- `evidence/runtime-measure8-{corpus}.bin`: streamed correctness evidence.
- `debug-lines-{google,llvm}.txt`, `sample.py`, `sampling-measure.cpp`:
  sampling method and resolved profiles.
- `runtime-{batch,summary,sink,dense}.patch`: rejected experiments.
- `runtime-tests-delivery.log`, `runtime-allocations-delivery.log`:
  final direct validation logs.
