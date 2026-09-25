# Configuration

The formatter preserves source-owned token and trivia bytes except where an
explicitly configured declaration brace, lambda capture-to-parameter
spacing, lambda capture spacing, function parameter or call spacing,
control keyword spacing or brace placement, namespace comment, or function
parameter rule applies.
All formatting rules default to preserving the source.
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
FunctionDeclarationParameterNames = Unchecked
FunctionDefinitionParameterNames = Unchecked
ClassBraceSpacing = Preserve
LambdaBraceSpacing = Preserve
LambdaSpaceBeforeParameters = Preserve
SpaceInsideLambdaParameters = Preserve
SpaceAroundCaptureList = Preserve
SpaceAroundCaptureAssignment = Preserve
FunctionSpaceBeforeParameters = Preserve
FunctionCallSpaceBeforeParameters = Preserve
SpaceInsideFunctionParameters = Preserve
SpaceInsideCallArguments = Preserve
ControlBlockKeywordSpacing = Preserve
ControlBlockBracePlacement = Preserve
StructBraceSpacing = Preserve
EnumBraceSpacing = Preserve
UnionBraceSpacing = Preserve
FunctionBraceSpacing = Preserve
NamespaceBraceSpacing = Preserve
StructNameCase = PascalCase
StructNamePrefixes = ["S", "C"]
NamingNamespaceExemptions = ["std"]
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
- `LambdaSpaceBeforeParameters`: `Preserve` (default) leaves the gap between a
  lambda's capture `]` and immediate parameter `(` unchanged; `Space`
  inserts exactly one space and `NoSpace` removes it. Newlines and
  intervening comments or directives are left unchanged. Calls and other
  parenthesized expressions are unaffected.
- `SpaceInsideLambdaParameters`: `Preserve` (default) leaves the gaps just
  inside the `(` and `)` of a nonempty lambda parameter list unchanged;
  `Space` uses one space and `NoSpace` removes it. For example, `Space`
  formats `[](x)` as `[]( x )`. Applies to generic lambdas as well.
  Empty `()`, newlines, adjacent comments or directives, and incomplete
  lists remain untouched. Does not affect capture brackets, the gap
  before `(`, named functions, calls, or control conditions.
- `SpaceAroundCaptureList`: `Preserve` (default) leaves the whitespace
  immediately inside the `[` and `]` of a nonempty lambda capture list
  unchanged; `Space` uses one space and `NoSpace` removes it. Applies to
  implicit (`[=]`, `[&]`) and explicit (`[value]`, `[&value]`) captures.
  Empty brackets, multiline gaps, adjacent comments or directives, and
  unrelated brackets remain untouched. Comma spacing is not changed.
- `SpaceAroundCaptureAssignment`: `Preserve` (default) leaves whitespace
  on either side of an initialized capture's `=` unchanged; `Space` uses
  one space on each same-line side and `NoSpace` removes it. Standalone
  capture defaults (`[=]` and `[=, x]`) are not assignments, nor are `=`
  tokens inside initializers. Newlines and gaps next to comments or
  directives remain untouched. This setting does not change `&` spacing.
- `FunctionSpaceBeforeParameters`: `Preserve` (default) leaves the gap
  between a named function declarator and its parameter `(` unchanged;
  `Space` inserts one space, and `NoSpace` removes it. Applies to function
  declarations and definitions, including constructors, destructors, and
  operators. Newlines, comments, and directives remain unchanged. Calls,
  lambdas, control conditions, and function-pointer declarations are
  unaffected.
- `FunctionCallSpaceBeforeParameters`: `Preserve` (default) leaves the gap
  between a parsed call's callee and argument `(` unchanged; `Space`
  inserts one space and `NoSpace` removes it. Newlines, intervening
  comments, and directives are left unchanged. Function declarations,
  lambda parameters, built-in type casts, and control or `requires`
  expressions are unaffected. Ambiguous parenthesized callees, which
  could instead be C-style casts, are preserved unless the inner expression
  is clearly callable (such as `(*fn)(arg)`). Calls in incompletely parsed
  statements remain source-owned. The setting controls the gap *before*
  the argument list, not whitespace between individual arguments.
- `SpaceInsideFunctionParameters`: `Preserve` (default) leaves gaps just
  inside the `(` and `)` of a named function's nonempty parameter list
  unchanged; `Space` uses one space and `NoSpace` removes it. Newlines,
  adjacent comments, and directives are preserved. Empty `()` pairs,
  lambdas, calls, control conditions, and `requires` expressions are
  unaffected. When function parameters are reflowed, `Space` also applies
  to the resulting list's same-line edges.
- `SpaceInsideCallArguments`: `Preserve` (default) leaves same-line gaps
  just inside the `(` and `)` of a nonempty parsed call argument list
  unchanged; `Space` uses one space and `NoSpace` removes it. Empty pairs,
  newlines, and gaps adjacent to comments or directives remain untouched.
  Ambiguous parenthesized callees (which may be casts), built-in type
  casts, declarations, lambda parameters, control conditions, and calls
  in incompletely parsed statements are unaffected. The rule does not
  change whitespace between arguments.
- `ControlBlockKeywordSpacing`: `Preserve` (default) leaves whitespace
  between a control keyword and its condition `(` unchanged; `Space` uses
  exactly one space and `NoSpace` removes it. Applies to `if`, `for`,
  `while` (including `do`/`while`), `switch`, and `catch`, as well as
  `if constexpr (` and `for co_await (`. Newlines, comments, directives,
  macro-wrapped conditions, calls, and function declarations are unchanged.
  `else` and `do` have no condition `(` to which this rule applies.
- `ControlBlockBracePlacement`: `Preserve` (default) leaves opening braces
  untouched; `SameLine` attaches them to the control header, and `NewLine`
  puts them on a separate line. `AttachUnlessMultiline` attaches braces
  for single-line headers but puts them on a separate line when the header
  spans multiple lines (not merely because the brace was detached). Applies
  to compound bodies of `if`, `else`, `for`, `while`, `do`, `switch`, `try`,
  and `catch`, but not unbraced statements or unrelated compound blocks.
  Existing same-line whitespace is preserved; attaching a detached brace
  uses one space. Comments and directives that cannot be moved safely stay
  in place.
- `<Kind>NameCase` and `<Kind>NamePrefixes`: independent naming checks for
  `Class`, `Enum`, `Union`, `Struct`, `Function`, `Concept`, `Variable`, and
  `Namespace`. Cases are `camelCase`, `PascalCase`, `snake_case`, and
  `SCREAMING_SNAKE_CASE` (ASCII letters and digits). Prefixes are a nonempty
  array of quoted identifier fragments; at least one must match. The case
  applies to the name *after* its prefix. Use `[""]` to explicitly allow no
  prefix. Either check can be enabled independently; omitted checks are
  disabled. Only unambiguous definitions and variable declarations are
  checked; forward declarations, function prototypes, explicit template
  instantiations, references, aliases, macros and uncertain declarators are
  skipped. Names in function definitions, including parameter names, are
  checked. `-i` and ordinary output mode leave names untouched.
- `NamingNamespaceExemptions`: qualified namespace paths excluded from all
  naming checks, including the namespace name and everything it contains.
  Defaults to `["std"]`, which also excludes `std::chrono`. Use `[]` to
  disable exemptions or list other paths such as `"vendor::api"`.
  Matching uses complete path components (so `std` does not exclude
  `standard`). These exemptions do not affect formatting or the independent
  unnamed-function-parameter checks.
- `FunctionDeclarationParameterNames`: `Unchecked` (default) or
  `Required`. When required, `--check` reports unnamed parameters in
  unambiguous function declarations; a comment does not count as a name.
- `FunctionDefinitionParameterNames`: `Unchecked` (default),
  `NameRequired`, or `NameOrCommentedNameRequired`. The last option also
  permits a trailing identifier comment such as `int /*unused*/`.
  Neither check renames parameters or alters formatting output.
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
