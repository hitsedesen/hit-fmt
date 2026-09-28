# hit-fmt

`hit-fmt` parses C and C++ source. It preserves source by default, with
configurable layout rules. This repository uses a stricter YTsaurus-inspired
profile: 80 columns, overflow reflow, three-line ternaries, and one-level
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
ctest --test-dir build
```

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
check errors. See
[`hit-fmt/CONFIGURATION.md`](hit-fmt/CONFIGURATION.md) for details.
Run `hit-fmt --help` for all command-line options, settings and defaults.

## License

The project is licensed under the [MIT License](LICENSE). Generated Unicode
character-property ranges are distributed under the
[Unicode License v3](LICENSES/Unicode-3.0.txt); their exact provenance is
[documented separately](unicode/README.md).
