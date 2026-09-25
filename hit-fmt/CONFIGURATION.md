# Configuration

The formatter always parses and emits the original token and trivia bytes.
No formatting rule is enabled. `IndentWidth` and `ColumnLimit` remain passive
settings; changing either cannot change output.

`hit-fmt` searches for `.hit-fmt` beside each source file and then in its
parent directories. The nearest file wins. `--config <file>` selects one
file for every input; `--no-config` disables configuration lookup.

Blank lines and `#` comments are allowed. Only three settings are accepted:

```text
IndentWidth = 4
ColumnLimit = 120
Exclude = ["(^|/)generated(/|$)", "(^|/)legacy\\.cpp$"]
```

- `IndentWidth`: positive integer up to 32, retained as an inert dimension.
- `ColumnLimit`: positive integer up to 10000, retained as an inert column
  width dimension. It does not cause wrapping.
- `Exclude`: ECMAScript regular expressions searched against paths relative
  to the config directory. A matching directory and its contents are skipped
  during recursive processing. An explicit file input is skipped if it
  matches. `--no-config` disables these exclusions; repeatable CLI
  `--exclude` patterns apply relative to each input directory instead.
  Backslashes in patterns must be doubled inside quoted config strings.

Former formatting settings are rejected rather than silently ignored.
The retained settings do not affect parsing or emission of processed files.
