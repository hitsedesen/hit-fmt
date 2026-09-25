# Configuration

The formatter preserves source-owned token and trivia bytes except where an
explicitly configured declaration brace or function parameter rule applies.
All formatting rules default to preserving the source.

`hit-fmt` searches for `.hit-fmt` beside each source file and then in its
parent directories. The nearest file wins. `--config <file>` selects one
file for every input; `--no-config` disables configuration lookup.

Blank lines and `#` comments are allowed. Example:

```text
IndentWidth = 4
ColumnLimit = 120
ClassBracePlacement = Preserve
StructBracePlacement = Preserve
UnionBracePlacement = Preserve
FunctionBracePlacement = Preserve
LambdaBracePlacement = Preserve
NamespaceBracePlacement = Preserve
FunctionParameterSplit = Pack
FirstFunctionParameterPosition = NameLine
EnforceFirstFunctionParameterPosition = false
FunctionParameterIndent = IndentWidth
MergeFunctionParameters = false
Exclude = ["(^|/)generated(/|$)", "(^|/)legacy\\.cpp$"]
```

- `IndentWidth`: positive integer up to 32. Used for parameter continuation
  lines when `FunctionParameterIndent = IndentWidth`.
- `ColumnLimit`: positive integer up to 10000. Function parameter lists
  exceeding this limit can be wrapped according to `FunctionParameterSplit`.
- `*BracePlacement`: `Preserve` (default) keeps the existing layout,
  `Attach` joins a brace to its header, and `NewLine` moves it to its own
  line. Existing same-line spacing is left unchanged by `Attach`. Each of
  class, struct, union, function definition, lambda, and
  namespace is configured independently. `Attach` places the brace before
  a trailing header comment (`//` or `/* ... */`). When `NewLine` splits an
  inline brace followed by a trailing comment, the comment stays with the
  declaration header and the brace goes below it. Other intervening comments
  and directives block attachment. Other braces are unaffected.
- `FunctionParameterSplit`: `Preserve` (default, leave parameter lists
  unchanged), `Pack` (put as many parameters as fit on each line), or
  `OnePerLine`. Lists already fitting on one line are left unchanged.
- `FirstFunctionParameterPosition`: `NameLine` (default) or `NewLine`.
  Determines whether the first parameter starts beside the function name
  when a list is split.
- `EnforceFirstFunctionParameterPosition`: `false` (default) retains the
  first parameter's position in already split lists; `true` applies the
  configured position to those lists as well.
- `FunctionParameterIndent`: `IndentWidth` (default, indent one width
  beyond the declaration) or `Continuation` (align with its opening `(`).
- `MergeFunctionParameters`: `false` (default) retains split lists as
  multiline even when they would fit on one line; `true` joins them when
  the result fits within `ColumnLimit`.
- `Exclude`: ECMAScript regular expressions searched against paths relative
  to the config directory. A matching directory and its contents are skipped
  during recursive processing. An explicit file input is skipped if it
  matches. `--no-config` disables these exclusions; repeatable CLI
  `--exclude` patterns apply relative to each input directory instead.
  Backslashes in patterns must be doubled inside quoted config strings.

Only safely movable parameter lists in function declarators are changed:
comments, directives, multiline parameter expressions and CRLF lists are
left intact. Calls are unaffected. Other former formatting settings are
rejected rather than silently ignored.
