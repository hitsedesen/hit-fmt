# Agent instructions

Before submitting a final version of the PR for publication, run the hit-fmt
on the repository source code. Review and commit any formatting changes, 
and resolve any remaining violations. Do not block pushing to a branch or
creating a PR if the formatter is temporarily unavailable due to work
in the repo.

## Documentation

- Document command-line options in `--help` and configuration options in
  `--help-config` and the generated configuration template. Update their
  descriptions and examples when adding or changing options.
- Do not add option lists, option references, or detailed option guidance to
  `README.md`. Keep it focused on the project overview, building, and basic
  usage; the built-in help is the source of option documentation.

## Tests and examples

- Run `build/tests/tests` directly to see each GoogleTest case, and
  run `build/tests/allocation_tests` for the separate allocation
  check. Use `--gtest_filter` on the GoogleTest binary for focused runs.
  Do not use CTest for this suite.
- Use golden tests for input-to-output behavior, including formatting,
  tokenization, parsing, and diagnostic output. Use the shared helpers in
  `tests/golden.hpp` and the existing fixture conventions. Keep full-file
  cases in `Tokenizer/GoodTest.Golden`, `Tokenizer/BadTest.Golden`,
  `Parser/FileTest.Golden`, or `Formatter/FormatTest.Golden` as appropriate;
  keep diagnostic and property checks separate.
- Regenerate goldens explicitly with `UPDATE_GOLDEN=1`. Review each changed
  expectation for correctness; generated output is not proof of correctness.
  Normal test runs must not update expectations.
- Do not add tests that run the CLI, spawn the executable, or invoke it through
  a shell. Exercise the underlying library functions directly. Extract
  testable logic from CLI code when necessary.
- Tests must protect meaningful behavior or a plausible regression. Extend
  an existing test or fixture when it already covers the relevant behavior;
  avoid redundant tests and tests of trivial accessors.
- Do not copy code from other repositories into examples or tests. Write
  small, synthesized examples that exercise the same syntax or behavior.
  Preserve the relevant structural features, not upstream names or content.

## C++ conventions

- Use the integer and size aliases from `util/types.hpp`, and `Vector<T>` and
  `Arena` from `util/memory.hpp` for their corresponding types. Preserve domain
  aliases such as `TokenIndex` and `SourceOffset`.
- Reuse `fs::Path` and the `fs` operations for production filesystem work,
  `TryParseInteger` for integer parsing, and `DEFER` for local scope cleanup
  where appropriate. Extend established utilities rather than introducing
  parallel conventions.

## General solutions

- Fix the underlying rule or abstraction rather than special-casing each
  reported example. Prefer solutions based on syntax, context, and semantics
  over lists of particular identifiers, strings, or fixture-specific patterns.
- Use reported cases to identify the general behavior. Cover meaningful
  variations and nearby counterexamples so a fix does not just recognize
  the original input.
- Keep solutions as simple as the general rule allows. Avoid speculative
  frameworks or unrelated refactoring.

## Performance and memory

- Performance is a design constraint, especially in tokenizer, parser, and
  formatter hot paths. Avoid unnecessary dynamic allocations, particularly
  per-token, per-node, and per-iteration allocations.
- Scope allocation to the actual lifetime: configuration/discovery state,
  reusable worker buffers, per-file data, temporary scratch, and batches awaiting
  consumption. Reuse the existing allocator infrastructure.
- Keep returned output and errors valid after worker memory resets. Destroy
  arena-backed objects before releasing their arena. Reuse bounded backing
  buffers and release overflow allocations when their operation or batch
  finishes.
- Prefer views, spans, indices, and references over copying strings or
  containers when their backing storage remains valid. Reserve capacity
  when a reasonable size is known, and reuse buffers where practical.
- Avoid repeated full-file or full-tree scans, repeated parsing, and quadratic
  behavior. Compute shared information once where practical; favor compact,
  contiguous data structures and predictable access patterns.
- Bound scratch memory and stack usage. Do not replace heap allocations with
  large stack buffers or input-dependent recursion. Avoid retaining peak
  allocations indefinitely across files.
- Treat discovery, path handling, diagnostics, and output buffering as
  performance-sensitive parts of the pipeline. Bound queued work and retained
  output as well as worker count. State ownership transfers between discovery,
  processing, and printing explicitly.
- Measure nontrivial performance changes with the existing benchmarks using
  representative input sizes. Consider runtime, allocation count, and peak
  memory; do not sacrifice correctness for an unmeasured micro-optimization.
- For performance measurements, also run the example configurations on their
  respective codebases and compare runtime, allocation count, and peak memory
  before and after the change. Use the same revision, configuration, build
  settings, and environment for both runs. Keep external codebases out of
  committed examples and test fixtures.
