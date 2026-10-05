# hit-fmt

`hit-fmt` is an experimental C and C++ formatter with syntax-owned group
policies. The shipped repository profile uses canonical YTsaurus-inspired
layout with 80 columns and block continuation indentation. Spacing and
structural rules are independent. Use an explicit configuration as shown below.

## Modules

- `tokenizer` groups input characters into tokens.
- `parser` builds the lossless syntax tree.
- `formatter` lowers syntax and formatting policies to the document algebra.
- `document` applies document algebra rules and renders text and source maps.
  It is independent of the tokenizer, parser, and formatter.

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

A CLI-only build does not validate the test suite. Library-level pipeline
and CLI checks require GoogleTest:

```sh
cmake -S . -B build-checks -DHIT_FMT_TEST=ON -DHIT_FMT_BENCHMARK=OFF
cmake --build build-checks --target pipeline_checks cli_checks -j1
build-checks/tests/pipeline_checks
build-checks/tests/cli_checks
```

Run GoogleTest binaries directly, not through CTest. The separate tokenizer
allocation check is available as the `allocation_tests` target; it is not
whole-pipeline allocation certification. The `document_checks` target tests
only the document algebra, linked independently of the formatter:

```sh
cmake --build build-checks --target document_checks -j1
build-checks/tests/document_checks
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
build-cli/hit-fmt/hit-fmt src/ include/
```

Format files or directories in place:

```sh
build-cli/hit-fmt/hit-fmt --fix source.cpp include/
```

`--check` may be given explicitly when desired. Configuration is discovered
from `.hit-fmt` files; this repository uses its root `.hit-fmt`. Use
`--config .hit-fmt` to select it explicitly.

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
    --exclude '(^|/)generated(/|$)' \
    --exclude '(^|/)legacy\.cpp$' src/
```

`--exclude` accepts a repeatable ECMAScript regular expression. It searches
paths relative to each input directory (using `/` as the separator); an
explicit file input is matched by its filename. A matching directory and
all its contents are skipped. Anchor patterns to path components as above
to avoid matching similarly named files or directories.

### Experimental configs and limitations

Use `--help-config` for accepted options and values. The shipped Google,
LLVM and YTsaurus examples are explicit configurations. For YTsaurus:

```sh
build-cli/hit-fmt/hit-fmt --config hit-fmt/examples/ytsaurus.hit-fmt \
    source.cpp
build-cli/hit-fmt/hit-fmt --fix \
    --config hit-fmt/examples/ytsaurus.hit-fmt source.cpp
```

`FunctionBodyMacroNames` defaults to empty. It lists exact, unqualified macro
names whose balanced invocation followed by a balanced brace body introduces
function-body grammar. Macro headers remain byte-exact; body statements use
normal function-body policies, retaining protection for uncertain syntax.
The hint does not infer a function name or C++ parameters from macro arguments.
Source formatting and CLI checks apply it automatically. Tree-based library
calls must parse using `PreparedFormat::ParsingOptions()`; normalization
reparses use the same hints. The repository opts in for its test declarations.

`TemplateArguments` uses the shared list defaults and supports the
`Layout`, `Break`, `Packing`, `FirstItem`, `Closer`, `Alignment`,
`ContinuationOffset`, and `AlignmentFallbackOffset` overrides. These apply
only to proven template argument lists and named-cast type brackets, not
comparison or shift operators. Nested lists retain shared `>>` closers as
one unchanged token; the outer list owns their closing placement. Argument
expressions remain intact unless their own syntax supplies safe breaks.
`TemplateParameters` independently provides the same overrides for
`template<class T, int N>` headers, nested template-template parameters,
and explicit generic-lambda template headers. Defaults containing template
arguments use `TemplateArguments` for their nested lists. Shared `>>` tokens
remain intact, with placement controlled by the outer list's policy.
`Parameters` continues to control function and lambda function parameters,
not template parameters. Neither template list family controls the break
between a template header and its following declaration.

Nonempty `CallLikeMacroNames` is currently rejected by the public pipeline.
The YTsaurus example clears that hint and conservatively byte-protects
`REGISTER_DERIVED_EXTERNALIZED_YSON_STRUCT` via `WhitespaceSensitiveMacros`.
Byte protection is not equivalent macro formatting. The profile forces
base and constructor lists onto new lines with leading colons and commas,
uses four-space continuations for all safely parsed chains, and repairs
namespace closing names. `IndentedChainScopePairs` adds one `IndentWidth`
inside matched, nested fluent scopes; `StickyChainCallPairs` keeps consecutive
call pairs together when they fit, even with forced vertical chains. Both
accept exact `First:Second` identifier pairs and default to empty. The
YTsaurus profile nests `BeginMap`, `BeginList`, and `BeginAttributes` scopes
and keeps `Item` beside its value or nested builder. A sticky pair may also
start with a simple root receiver or factory name; `registrar:Parameter`
keeps `registrar.Parameter(...)` together without flattening `.Default()`.
Generic forced chains count suffix calls across mixed member and invocation
links. `AlwaysBreakChainRootNames` is a separate, empty-by-default convention
list: the YTsaurus profile lists logging/error builders so a single `.With`
still starts a new line, without expanding ordinary single-call expressions.
Multiline captures use `CapturesCloser = OwnLine`; this keeps the lambda's
parameter/header line and preserved body on a consistent indentation base.
The profile sets `FunctionRefQualifierSpacing = Space` for `f() &`/`f() &&`
and `FunctionCvRefQualifierSpacing = Space` for `f() const &`/`const &&` and
volatile-qualified equivalents, independently of left-bound type references
such as `const T&`. The CV-specific setting defaults to `Inherit` from
`FunctionRefQualifierSpacing`; explicit `Preserve` uses normal gap-preservation
rules instead of inheriting the generic setting.
Use `FunctionCvRefQualifierSpacing = NoSpace` for `f() const&`/`const&&`
without changing bare ref qualifiers. Each setting covers both `&` and `&&`. It remains a guide approximation for `yt/yt`,
`yt/chyt`, `yt/yql` and `library/cpp/yt`, not `yt/cpp` or unrelated libraries;
namespace rulers and include regrouping remain unsupported. Calls use
`CallsFirstItem = NextLine` with
`CallsSoleArgumentFirstItem = InlineIfFits`: a sole argument's opening can
stay beside the caller when it fits, including a multiline lambda. Calls
with multiple arguments keep the normal first-item policy. The sole-argument
option defaults to `Inherit` and does not override preservation or required
breaks. Lambda body indentation and its closing brace follow the actual
opening line's leading indentation, independently of subsequent arguments.
For repository runs, the root `.hit-fmt` supplies the repository configuration;
Google and LLVM are alternative explicit profiles. Self-format width and anchor
limitations remain.

Continuation indentation is line-based, not syntax-depth-based. Scopes
opened on the same line share one prospective continuation offset.
Expressions also reuse the realized continuation of an enclosing list or
control header. A nested expression starting on a later line establishes
another level, keeping distinct operator precedence visible. Once a callee
or another list owner starts on a continued line, its contents establish
a new level.
Wrapped ternary branch expressions share the delimiter line's
continuation instead of adding another level. Calls within a branch
still establish a new level when their owner starts on a continued line.
`TernaryOperatorIndentation = Align` (the default) shares the surrounding
continuation for `?` and `:`. `Indent` adds one continuation offset when
the condition starts on an already-continued line; it does not alter
continuations within the condition. With trailing operators, the setting
controls the following branch lines instead. Fitting inline ternaries
remain unchanged. These rules apply without general nesting or context
options. Structural control and lambda bodies,
braced initializer lists, and enum lists establish fresh indentation bases.
Continuation offsets and block/delimiter alignment remain configurable.
Block-aligned continuations reuse the enclosing anchor while sharing a
level. Delimiter alignment uses physical columns, with continuation floors
and fallback offsets.

`--changed-hunks` and `--no-spillover` are explicitly unsupported. Non-Preserve
`AmbiguousTemplateDeclarationSpacing` is also rejected. Incompletely parsed
CTAD initializer lists preserve punctuation conservatively and can retain
awkward or overwidth layout; safety checks do not certify ideal layout.
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
