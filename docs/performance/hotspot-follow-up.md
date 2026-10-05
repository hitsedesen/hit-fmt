# Instruction-hotspot follow-up

This follow-up targets the costs found in
[instruction sampling](instruction-profile.md). The pinned corpora,
unchanged shipped configurations, GCC 13.3 Release settings, CPU 2 affinity,
single worker, and correctness contracts remain unchanged. This report
supersedes the preceding integrated runtime totals with a fresh comparison;
do not subtract percentages measured in different runs to infer a step gain.

## Retained changes

- `ff49213`: replace the per-byte string search in physical-line analysis
  with the ASCII horizontal-whitespace interval. LF is still handled by
  the newline branch. Space, HT, VT, FF, and CR retain their exact meaning;
  other bytes still establish content boundaries. The existing nested-marker
  property covers all of these horizontal whitespace bytes.
- `398bb7f`: select the needed gap specification before renderer emission
  rather than copying both alternatives. The summary pass borrows conditional
  broken specifications, and indentation-only payloads are read by reference.
  Shape ownership, fit checks, source associations, and protection remain.

The whitespace change reduces instruction cost by about 24 instructions
per input byte. Its three-run CPU median reductions relative to the prior
branch are 3.42% YTsaurus, 1.87% LLVM/Clang, and 2.41% TensorFlow.

The gap change is modest. Across six paired corpus runs against the
whitespace change, CPU median reductions are 0.47%, 0.92%, and 0.26%.
Three paired fixed-input medians improve 1.80% service, 4.53% template,
2.10% platform, and 0.71% mixed. Individual pairs vary; the result is not
presented as a large structural lowering simplification.

## Rejected experiment

Packing atoms directly into the retained vector instead of using a
construction temporary saved only about 0.6 instructions per input byte.
Runtime results were mixed and regressed some corpora. The experiment was
reverted; no in-place packing overload or exception-handling machinery is
retained. This reinforces that an apparent hot copy in a source-line profile
is not proof that eliminating its source-level temporary speeds up execution.

## Fresh integrated-main comparison

Main baseline source remains `e102de7`; the subsequent integrated main
update is CI-only. The final measured implementation is `398bb7f`.
Three interleaved runs reverse execution order on the middle repetition.
Primary runtime runs write no correctness evidence.

| Corpus | CheckFile seconds, main -> branch | Process CPU seconds, main -> branch | CPU change |
| --- | ---: | ---: | ---: |
| YTsaurus | 11.6764 -> 10.0805 | 12.1570 -> 10.5373 | -13.32% |
| LLVM/Clang | 15.1002 -> 12.9238 | 15.4599 -> 13.2151 | -14.52% |
| TensorFlow | 8.1792 -> 6.8349 | 8.5179 -> 7.0976 | -16.67% |

Summed corpus CPU medians are 36.1347 -> 30.8499 seconds: 14.63% less CPU.
Summing medians is an aggregate summary, not a separately measured combined
process. CheckFile wall time excludes input reading and outer result
destruction; process CPU also includes harness work.

Three-run fixed-input CPU medians, microseconds:

| Case | Main | Branch | Change |
| --- | ---: | ---: | ---: |
| Service | 1852.48 | 1450.56 | -21.70% |
| Template library | 1698.02 | 1375.09 | -19.02% |
| Platform header | 812.16 | 796.39 | -1.94% |
| Mixed corpus | 4490.60 | 3445.02 | -23.28% |

## Hardware instructions per byte

A separate three-run comparison uses direct hardware counters for user-space
CheckFile, with both events running together and no multiplexing. Original
input bytes are the denominator. This excludes kernel work and has a
narrower scope than process CPU. Cycle results remain sensitive to noise.

| Corpus | Main instructions/B | Branch instructions/B | Main cycles/B | Branch cycles/B |
| --- | ---: | ---: | ---: | ---: |
| YTsaurus | 1099.4 | 1013.4 | 663.9 | 554.9 |
| LLVM/Clang | 1160.1 | 1056.9 | 715.1 | 622.1 |
| TensorFlow | 1002.5 | 914.7 | 624.1 | 575.9 |
| Combined | 1098.8 | 1005.5 | 674.2 | 587.3 |

The combined row weights corpus median counts by original bytes. It shows
8.50% fewer instructions per byte and 12.90% fewer user-space cycles per
byte. Compared with the preceding branch's stable instruction cost of
1032.2/B, the retained changes remove a further approximately 2.6%.

Allocation counts and requested bytes are unchanged relative to the prior
branch; final peak-RSS medians versus main decrease 7.49%, 12.22%, and
13.31% respectively. The changes do not add retained data or scratch tables.

## Correctness

All 1,266 direct GoogleTest cases and the separate allocation checks pass.
The repository formatter check and git diff --check pass. Normal runs do not
update goldens. Both retained steps match integrated-main serialized evidence
byte-for-byte over all 18,674 selected inputs, with zero failures or skips.
This covers output, diagnostics and their positions, explicit full source
maps, identities, structural operations, token positions, and protection.

No new grammar restriction, safety shortcut, or verbatim fallback is added.
The remaining lowering and renderer costs still warrant further work; this
is a bounded follow-up, not a claim that every planned optimization is done.

## External evidence

Artifacts are outside Git in /state/data/compact-pipeline/:

- next-scan-* preserve the isolated whitespace comparison and validation.
- next-gap-* preserve six isolated corpus pairs, three fixed-input pairs,
  instruction checks, and full correctness evidence.
- rejected-next-inplace-pack.patch and next-pack-* preserve the rejected
  packing experiment and its measurements.
- next-final-* preserve final runtime/fixed-input/hardware comparisons,
  final build/tests/allocation/formatter logs, and next-final-summary.json.
- evidence/next-{scan,gap}-{corpus}.bin preserve byte-identical full-map
  validation against evidence/integration-baseline-{corpus}.bin.
