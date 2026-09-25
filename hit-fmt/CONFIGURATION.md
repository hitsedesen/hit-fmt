# Configuration

The formatter parses and emits source-owned token and trivia bytes. Only
configured declaration opening braces change their preceding whitespace.
`IndentWidth` and `ColumnLimit` remain passive settings.

`hit-fmt` searches for `.hit-fmt` beside each source file and then in its
parent directories. The nearest file wins. `--config <file>` selects one
file for every input; `--no-config` disables configuration lookup.

Blank lines and `#` comments are allowed. The settings are:

```text
IndentWidth = 4
ColumnLimit = 120
ClassBracePlacement = Preserve
StructBracePlacement = Preserve
UnionBracePlacement = Preserve
FunctionBracePlacement = Preserve
LambdaBracePlacement = Preserve
NamespaceBracePlacement = Preserve
Exclude = ["(^|/)generated(/|$)", "(^|/)legacy\\.cpp$"]
```

- `IndentWidth`: positive integer up to 32, retained as an inert dimension.
- `ColumnLimit`: positive integer up to 10000, retained as an inert column
  width dimension. It does not cause wrapping.
- `*BracePlacement`: `Preserve` (default) keeps the existing layout,
  `Attach` joins a brace to its header, and `NewLine` moves it to its own
  line. Existing same-line spacing is left unchanged by `Attach`. Each of
  class, struct, union, function definition, lambda, and
  namespace is configured independently. `Attach` places the brace before
  a trailing header comment (`//` or `/* ... */`). When `NewLine` splits an
  inline brace followed by a trailing comment, the comment stays with the
  declaration header and the brace goes below it. Other intervening comments
  and directives block attachment. Other braces are unaffected.
- `Exclude`: ECMAScript regular expressions searched against paths relative
  to the config directory. A matching directory and its contents are skipped
  during recursive processing. An explicit file input is skipped if it
  matches. `--no-config` disables these exclusions; repeatable CLI
  `--exclude` patterns apply relative to each input directory instead.
  Backslashes in patterns must be doubled inside quoted config strings.

Other former formatting settings are rejected rather than silently ignored.
