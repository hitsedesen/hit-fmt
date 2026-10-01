# hit-fmt

`hit-fmt` parses C and C++ source. It preserves compliant source by default,
with configurable layout rules. Safe overflowing initializer lists can reflow
without enforcing changes to already-fitting lists. This repository uses a
stricter YTsaurus-inspired profile: 80 columns, overflow reflow, three-line
ternaries, and one-level
rather than delimiter-aligned continuation indentation. Checks also report
naming violations without renaming identifiers, including after `--fix`.

## Build

A C++20 compiler and CMake 3.20 or newer are required.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The executable is written to `build/hit-fmt/hit-fmt` by default. Install it
and its license notices under a chosen prefix with:

```sh
cmake --install build --prefix ~/.local
```

Tests require GoogleTest:

```sh
cmake -S . -B build -DHIT_FMT_TEST=ON
cmake --build build
build/tests/tests
build/tests/allocation_tests
```

The GoogleTest executable prints each test as it runs. Select a subset with
`build/tests/tests --gtest_filter='Parser/*:Parser.*'`, or list names
with `--gtest_list_tests`. The tokenizer allocation check uses a separate
executable.

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
hit-fmt src/ include/
```

Format files or directories in place:

```sh
hit-fmt --fix source.cpp include/
```

`--check` may be given explicitly when desired.

Use `-j N` or `--threads N` to process up to N files concurrently (default 1).
Bare `-j` uses the available hardware concurrency (or 1 if unknown).
With multiple workers, formatting starts while discovery continues. Files
are queued in batches of up to four or 256 KiB of source, whichever limit
comes first; large files get their own batch. File diagnostics remain in
discovery order.

Checks report affected source chunks with caret-marked formatting,
expression-layout, naming and parameter-name violations. Source excerpts
are cropped around the marked token to fit the terminal width; proposed
formatter output wraps when needed. Expression reflow is not run to decide
check-only formatting mismatches: layout checks inspect the original source.
`--fix` writes auto-fixable changes, reports them as `fixed`, then checks the
result and reports remaining violations as `fail`. Check-only returns a
nonzero status for any violation; `--fix` does so only for remaining ones
(such as naming errors, which never rewrite identifiers). `--summary`
reports file status lines in either mode.

Skip files or whole subdirectories while formatting or checking a folder:

```sh
hit-fmt --fix --exclude '(^|/)generated(/|$)' \
    --exclude '(^|/)legacy\.cpp$' src/
```

`--exclude` accepts a repeatable ECMAScript regular expression. It searches
paths relative to each input directory (using `/` as the separator); an
explicit file input is matched by its filename. A matching directory and
all its contents are skipped. Anchor patterns to path components as above
to avoid matching similarly named files or directories.

Format only changed hunks from one or more files by supplying `file` and
`hunk` records on stdin:

```text
printf 'file src/a.cpp\nhunk 12:18\nfile src/b.cpp\nhunk 4:9\n' |
  hit-fmt --fix --changed-hunks -
```

Paths are relative to the working directory. Formatting may spill into a
bounded enclosing list or include group by default. Use `--no-spillover`
to restrict fixes to the hunk lines; changes requiring spill-over remain
check errors. Run `hit-fmt --help` for command-line options and
`hit-fmt --help-config` for the configuration reference and per-option C++
examples. `hit-fmt --version` (or `-V`) prints the version built into the
binary. A clean checkout at the matching `v<version>` Git tag reports the
release version. Other checkouts include `-dev+<commit>` and append `.dirty`
when there are uncommitted changes (including untracked files). Source
archives without Git metadata report only `-dev`. The version updates on
each build. Help is built into the binary and colored on terminals; set
`NO_COLOR` to disable color. Generate a complete preserving configuration
with all settings and their accepted values annotated:

```sh
hit-fmt --generate-config > .hit-fmt
```

## License

The project is licensed under the [MIT License](LICENSE). Generated Unicode
character-property ranges are distributed under the
[Unicode License v3](LICENSES/Unicode-3.0.txt); their exact provenance is
[documented separately](unicode/README.md).
