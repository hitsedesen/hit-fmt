# hit-fmt

`hit-fmt` parses C and C++ source. This experimental checkpoint provides the
whole-file group-policy pipeline, not final whole-project acceptance. The
migrated repository profile uses canonical YTsaurus-inspired layout with
80 columns and block continuation indentation. Explicit spacing and
structural rules are independent. Use an explicit migrated config below.

## Build

Run these commands from the repository root. CMake 3.20+ and a C++20
compiler are required; GCC 13 is tested. This is a CLI-only build:

```sh
cmake -S . -B build-cli -DCMAKE_BUILD_TYPE=Release \
    -DHIT_FMT_TEST=OFF -DHIT_FMT_BENCHMARK=OFF
cmake --build build-cli --target hit-fmt -j1
build-cli/hit-fmt/hit-fmt --help
```

The executable is written to `build-cli/hit-fmt/hit-fmt`. Install it and its
license notices under a chosen prefix with:

```sh
cmake --install build-cli --prefix ~/.local
```

The full `tests` target now compiles after the reviewed test-API migration,
but the normal and ASan/UBSan runs have 207 failures out of 798 tests.
Existing goldens remain unchanged. This CLI-only build does not imply
full-suite acceptance. Focused component checks require GoogleTest:

```sh
cmake -S . -B build-checks -DHIT_FMT_TEST=ON -DHIT_FMT_BENCHMARK=OFF
cmake --build build-checks --target pipeline_checks cli_checks -j1
build-checks/tests/pipeline_checks
build-checks/tests/cli_checks
```

Run GoogleTest binaries directly, not through CTest. The separate tokenizer
allocation check is available as the `allocation_tests` target; it is not
whole-pipeline allocation certification.

Benchmarks can be built separately with
`-DHIT_FMT_BENCHMARK=ON`; they require Google Benchmark.

## Releases

SourceCraft runs the test suite and checks the repository's strict formatting
on pull requests into `main`. To publish a release, update the `VERSION` in `project()` in
`CMakeLists.txt`, merge the change into `main`, then manually run the
`release` workflow in SourceCraft CI/CD on `main`. It repeats the checks before
pushing an annotated `v<version>` tag for that commit. An existing version tag
is never overwritten; bump the project version for each release. Formatting checks run on the repository folder. The root `.hit-fmt` excludes
build outputs, golden test fixtures, benchmarks, and generated Unicode data.

## Usage

Check files or directories without modifying them (the default):

```sh
build-cli/hit-fmt/hit-fmt --config hit-fmt/examples/repository.hit-fmt \
    src/ include/
```

Format files or directories in place:

```sh
build-cli/hit-fmt/hit-fmt --fix \
    --config hit-fmt/examples/repository.hit-fmt source.cpp include/
```

`--check` may be given explicitly when desired.

Use `-j N` or `--threads N` to process up to N files concurrently (default 1).
Bare `-j` uses the available hardware concurrency (or 1 if unknown).
With multiple workers, formatting starts while discovery continues. Files
are queued in batches of up to four or 256 KiB of source, whichever limit
comes first; large files get their own batch. File diagnostics remain in
discovery order.

Checks report affected source chunks with caret-marked formatting,
naming and parameter-name violations. Check-only runs the whole-file
pipeline without writing files and compares its output with the source.
`--fix` writes auto-fixable changes, reports them as `fixed`, then checks the
result and reports remaining violations as `fail`. Check-only returns a
nonzero status for any violation; `--fix` does so only for remaining ones
(such as naming errors, which never rewrite identifiers). `--summary`
reports file status lines in either mode.

Skip files or whole subdirectories while formatting or checking a folder:

```sh
build-cli/hit-fmt/hit-fmt --fix \
    --config hit-fmt/examples/repository.hit-fmt \
    --exclude '(^|/)generated(/|$)' \
    --exclude '(^|/)legacy\.cpp$' src/
```

`--exclude` accepts a repeatable ECMAScript regular expression. It searches
paths relative to each input directory (using `/` as the separator); an
explicit file input is matched by its filename. A matching directory and
all its contents are skipped. Anchor patterns to path components as above
to avoid matching similarly named files or directories.

### Experimental configs and limitations

Use explicit migrated configurations; legacy keys have no compatibility
aliases. See [the migration guide](docs/config-migration.md). Google and
LLVM examples use the migrated schema. The canonical YTsaurus example
retains its syntax hints, but nonempty `CallLikeMacroNames` is currently
rejected. Use the runnable preview instead:

```sh
build-cli/hit-fmt/hit-fmt --config hit-fmt/examples/ytsaurus-preview.hit-fmt \
    source.cpp
build-cli/hit-fmt/hit-fmt --fix \
    --config hit-fmt/examples/ytsaurus-preview.hit-fmt source.cpp
```

The preview clears that syntax hint and conservatively byte-protects
`REGISTER_DERIVED_EXTERNALIZED_YSON_STRUCT` via `WhitespaceSensitiveMacros`.
This is a temporary workaround, not equivalent macro formatting. Both
YTsaurus profiles force base lists onto new lines with leading colons and
commas. The migrated repository profile is
`hit-fmt/examples/repository.hit-fmt`; Google and LLVM are alternative
explicit profiles. Always pass `--config` when running this new CLI in
the repository. The checked-in root `.hit-fmt` deliberately remains the
legacy bootstrap profile and cannot be consumed by the new CLI.

Repository development formatting uses a trusted pre-migration bootstrap
formatter with that legacy root config until new-engine self-hosting is
ready. This is a development workflow, not a runtime legacy fallback in
the new CLI. The experimental engine still has self-format width/anchor
limitations; they are not claimed solved by bootstrap formatting.

`--changed-hunks` and `--no-spillover` are explicitly unsupported. Non-Preserve
`AmbiguousTemplateDeclarationSpacing` is also rejected. Incompletely parsed
CTAD initializer lists preserve punctuation conservatively and can retain
awkward or overwidth layout; new safety goldens do not certify ideal layout.
No corpus compatibility, speedup or whole-pipeline allocation certification
is claimed.

Run `hit-fmt --help` for command-line options and `hit-fmt --help-config`
for the configuration reference and per-option C++ examples. `hit-fmt --version` (or `-V`) prints the version built into the
binary. A clean checkout at the matching `v<version>` Git tag reports the
release version. Other checkouts include `-dev+<commit>` and append `.dirty`
when there are uncommitted changes (including untracked files). Source
archives without Git metadata report only `-dev`. The version updates on
each build. Help is built into the binary and colored on terminals; set
`NO_COLOR` to disable color. Generate a complete preserving configuration
with all settings and their accepted values annotated:

```sh
build-cli/hit-fmt/hit-fmt --generate-config > preserving.hit-fmt
```

## License

The project is licensed under the [MIT License](LICENSE). Generated Unicode
character-property ranges are distributed under the
[Unicode License v3](LICENSES/Unicode-3.0.txt); their exact provenance is
[documented separately](unicode/README.md).
