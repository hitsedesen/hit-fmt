# hit-fmt

`hit-fmt` is a configurable C++ formatter. It uses a recovery-oriented,
lossless parser so incomplete or unsupported source regions can be preserved
instead of rewritten unsafely.

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

SourceCraft runs the test suite and checks source formatting on pull requests
into `main`. To publish a release, update the `VERSION` in `project()` in
`CMakeLists.txt`, merge the change into `main`, then manually run the
`release` workflow in SourceCraft CI/CD on `main`. It repeats the checks before
pushing an annotated `v<version>` tag for that commit. An existing version tag
is never overwritten; bump the project version for each release. Formatting
checks exclude golden test fixtures and benchmark sources, which deliberately
use other layouts.

## Usage

Format one file to standard output:

```sh
hit-fmt source.cpp
```

Format files or directories in place:

```sh
hit-fmt --in-place source.cpp include/
```

Check formatting without modifying files:

```sh
hit-fmt --check src/ include/
```

Run `hit-fmt --help` for all command-line options. See
[`hit-fmt/CONFIGURATION.md`](hit-fmt/CONFIGURATION.md) for configuration
settings and defaults.

## License

The project is licensed under the [MIT License](LICENSE). Generated Unicode
character-property ranges are distributed under the
[Unicode License v3](LICENSES/Unicode-3.0.txt); their exact provenance is
[documented separately](unicode/README.md).
