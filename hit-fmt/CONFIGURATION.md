# Configuration

The formatter preserves source-owned token and trivia bytes except where an
explicitly configured declaration brace, namespace comment, or function
parameter rule applies. All formatting rules default to preserving the source.
Naming rules default to unchecked and never rewrite identifiers; `--check`
reports violations with file, line and column and returns a nonzero status.

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
NamespaceComments = Preserve
FunctionParameterSplit = Pack
FirstFunctionParameterPosition = NameLine
EnforceFirstFunctionParameterPosition = false
FunctionParameterIndent = IndentWidth
MergeFunctionParameters = false
CheckFunctionParameterNames = false
ClassBraceSpacing = Preserve
LambdaBraceSpacing = Preserve
StructBraceSpacing = Preserve
EnumBraceSpacing = Preserve
UnionBraceSpacing = Preserve
FunctionBraceSpacing = Preserve
NamespaceBraceSpacing = Preserve
StructNameCase = PascalCase
StructNamePrefixes = ["S", "C"]
VariableNameCase = snake_case
VariableNamePrefixes = ["m_", "s_"]
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
- `NamespaceComments`: `Preserve` (default) leaves closing comments alone,
  `Remove` removes a line comment immediately after a namespace's closing
  brace, and `Required` adds `// namespace name` (or `// namespace` for an
  anonymous namespace) and corrects an existing trailing line comment.
  Unmatched braces and closings followed by a block comment are left alone.
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
  beyond the declaration) or `Continuation` (align with the first
  parameter immediately after the opening `(`).
- `MergeFunctionParameters`: `false` (default) retains split lists as
  multiline even when they would fit on one line; `true` joins them when
  the result fits within `ColumnLimit`.
- `*BraceSpacing`: `Preserve` (default) leaves existing same-line whitespace
  untouched, including zero or multiple spaces; `SingleSpace` uses exactly
  one space before an opening brace on the same line. Class, lambda, struct,
  enum, union, function definition, and namespace have independent settings.
  Line breaks and gaps containing comments or directives remain untouched.
  Placement is applied first: `NewLine` keeps the brace on its own line;
  `Attach` joins it to the header, using one space at the join.
- `<Kind>NameCase` and `<Kind>NamePrefixes`: independent naming checks for
  `Class`, `Enum`, `Union`, `Struct`, `Function`, `Concept`, `Variable`, and
  `Namespace`. Cases are `camelCase`, `PascalCase`, `snake_case`, and
  `SCREAMING_SNAKE_CASE` (ASCII letters and digits). Prefixes are a nonempty
  array of quoted identifier fragments; at least one must match. The case
  applies to the name *after* its prefix. Use `[""]` to explicitly allow no prefix. Either check can be
  enabled independently; omitted checks are disabled. Only unambiguous
  declarations are checked (including function parameters and local
  variables), not references, aliases, macros or uncertain declarators.
  `-i` and ordinary output mode leave names untouched.
- `CheckFunctionParameterNames`: `false` (default) disables the check;
  `true` reports unnamed parameters of unambiguous function declarations
  in `--check` mode. Definitions may use a name or a trailing identifier
  comment such as `int /*unused*/`. Declarations require a real name.
  The check does not rename parameters or alter formatting output.
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
