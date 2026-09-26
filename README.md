# hit-fmt

`hit-fmt` parses C and C++ source. It preserves source by default, with
configurable declaration layout rules. In `--check` mode it can also report
naming violations without renaming identifiers.

## Build

A C++23 compiler and CMake 3.20 or newer are required.

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

SourceCraft runs the test suite and verifies source preservation on pull
requests into `main`. To publish a release, update the `VERSION` in `project()` in
`CMakeLists.txt`, merge the change into `main`, then manually run the
`release` workflow in SourceCraft CI/CD on `main`. It repeats the checks before
pushing an annotated `v<version>` tag for that commit. An existing version tag
is never overwritten; bump the project version for each release. Preservation
checks run on the repository folder. The root `.hit-fmt` excludes build
outputs, golden test fixtures, and benchmark sources.

## Usage

Emit a formatted file to standard output:

```sh
hit-fmt source.cpp
```

Format files or directories in place:

```sh
hit-fmt --in-place source.cpp include/
```

Check layout and configured naming rules without modifying files:

```sh
hit-fmt --check src/ include/
```

Check mode reports affected source chunks with caret-marked formatting,
expression-layout, naming and parameter-name violations. It suggests
formatter output where available; naming checks remain advisory. Expression
reflow is not run to decide check-mode formatting mismatches: layout checks
inspect the original source. `--summary` reports only affected files.
Formatting mode (standard output or `--in-place`) emits formatted source.

Skip files or whole subdirectories while formatting or checking a folder:

```sh
hit-fmt --in-place --exclude '(^|/)generated(/|$)' \
    --exclude '(^|/)legacy\.cpp$' src/
```

`--exclude` accepts a repeatable ECMAScript regular expression. It searches
paths relative to each input directory (using `/` as the separator); an
explicit file input is matched by its filename. A matching directory and
all its contents are skipped. Anchor patterns to path components as above
to avoid matching similarly named files or directories.

Run `hit-fmt --help` for all command-line options. See
[`hit-fmt/CONFIGURATION.md`](hit-fmt/CONFIGURATION.md) for configuration
settings and defaults.

## License

The project is licensed under the [MIT License](LICENSE). Generated Unicode
character-property ranges are distributed under the
[Unicode License v3](LICENSES/Unicode-3.0.txt); their exact provenance is
[documented separately](unicode/README.md).
