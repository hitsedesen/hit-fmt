# hit-fmt

`hit-fmt` parses C and C++ source. It preserves compliant source by default,
with configurable layout rules. Safe overflowing initializer lists can reflow
without enforcing changes to already-fitting lists. This repository uses a
YTsaurus-inspired example profile: 120 columns, overflow reflow, three-line
ternaries, and one-level
rather than delimiter-aligned continuation indentation. Checks also report
naming violations without renaming identifiers, including after `--fix`.

## Build

A C++20 compiler and CMake 3.20 or newer are required.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The executable is written to `build/cli/hit-fmt` by default. Install it
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

The corpus benchmark can be built separately with
`-DHIT_FMT_BENCHMARK=ON`.

## Usage

Check files or directories without modifying them (the default):

```sh
hit-fmt src/ include/
```

Format files or directories in place:

```sh
hit-fmt --fix source.cpp include/
```

## License

The project is licensed under the [MIT License](LICENSE). Generated Unicode
character-property ranges are distributed under the
[Unicode License v3](LICENSES/Unicode-3.0.txt); their exact provenance is
[documented separately](unicode/README.md).
