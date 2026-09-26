# Configuration

The formatter preserves source-owned token and trivia bytes except where an
explicitly configured declaration brace, lambda capture-to-parameter
spacing, lambda capture spacing, function parameter or call spacing,
control keyword spacing or brace placement, namespace comment, include
ordering, or function parameter rule applies.
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
IncludeOrder = Preserve
IndentAccessModifiers = Preserve
IndentMembers = Preserve
InheritanceList = Preserve
InheritanceListColonPlacement = AttachToBase
InheritanceListColonSpaceBefore = NoSpace
InheritanceListColonSpaceAfter = Space
InheritanceListCommaPlacement = AttachToNext
InheritanceListCommaSpaceBefore = NoSpace
InheritanceListCommaSpaceAfter = Space
InheritanceListStart = NewLine
InheritanceListSplit = OnePerLine
# InheritanceListIndent defaults to IndentWidth when omitted
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

- `IncludeOrder`: `Preserve` (default) leaves directives untouched. `Sort`
  stably sorts consecutive, column-one literal `#include "..."` and
  `#include <...>` lines by header path, with quoted headers before angled
  headers. It keeps each directive's spelling and the line endings in its
  group. Blank lines, comments, non-include directives (including `#if`,
  `#else`, and `#endif`), macro headers, trailing comments, continued lines,
  and indentation break groups. An include immediately after a comment is
  kept with that comment; later includes may still sort. No includes move
  across these boundaries. This does not infer a main header, deduplicate
  includes, or merge groups.
- `IndentWidth`: positive integer up to 32. Used for record bodies and base
  lists, and for parameter or call argument continuation lines when their
  respective indent setting is `IndentWidth`.
- `ColumnLimit`: positive integer up to 10000. Function parameter lists,
  call arguments, expressions, and inheritance lists can be wrapped
  according to their split settings.
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
- `IndentAccessModifiers` and `IndentMembers`: `Preserve` (default)
  leaves indentation unchanged, `Indent` uses one `IndentWidth` relative
  to the record declaration, and `NoIndent` aligns with that declaration.
  Access labels and direct members of class, struct, and union bodies use
  independent settings. Enum enumerators use `IndentMembers` (enums do
  not have access labels). Only existing lines are reindented; nested
  closing braces follow their reindented declarations. Inline members,
  comments, directives, and conditional branches remain source-owned.
- `InheritanceList`: `Preserve` (default) leaves the entire base list
  unchanged. `Format` applies the following settings to complete base
  lists in class and struct definitions. Whitespace-only blank lines
  between the header, colon, commas, and bases are normalized. Comments,
  directives, and multiline base specifiers keep a list source-owned.
  Enum underlying-type colons are not inheritance lists.
- `InheritanceListColonPlacement`: `AttachToBase` (default) puts `:` on
  the first base line; `AttachToClass` puts it on the class header line.
  `InheritanceListColonSpaceBefore` uses `NoSpace` (default) or `Space`;
  `InheritanceListColonSpaceAfter` uses `Space` (default) or `NoSpace`.
  Spacing applies only to same-line gaps around `:`.
- `InheritanceListCommaPlacement`: `AttachToNext` (default) puts a comma
  before the next base; `AttachToPrevious` puts it after the preceding
  base. `InheritanceListCommaSpaceBefore` uses `NoSpace` (default) or
  `Space`; `InheritanceListCommaSpaceAfter` uses `Space` (default) or
  `NoSpace`. Spacing options do not remove line breaks.
- `InheritanceListStart`: `NewLine` (default), `ClassLine`, or
  `NewLineOnOverflow` controls the first base's line. The overflow case
  checks whether the entire projected list fits on the class line.
- `InheritanceListSplit`: `OnePerLine` (default) puts each subsequent
  base on its own line; `Pack` joins bases until the next would exceed
  `ColumnLimit`; `OnePerLineOnOverflow` splits only when the entire
  projected list exceeds `ColumnLimit`. Safe existing line breaks are
  merged whenever the chosen layout permits.
- `InheritanceListIndent`: an optional positive integer up to 32 for
  lines created in a base list, relative to the record declaration.
  Defaults to `IndentWidth`. `Format` also uses one space after
  `public`, `protected`, `private`, and `virtual` within each base
  specifier; other internal spacing remains source-owned.
- `ExpressionLayoutCondition`: `Preserve` (default) retains expression
  and call argument layout. `Overflow` splits safe overlong lines without
  joining existing breaks. `FitOrOverflow` also joins safe multiline
  expressions and calls when their flat form fits within `ColumnLimit`;
  otherwise it behaves like `Overflow`. `Always` starts from the flat form
  and reflows it according to `ColumnLimit`, even when that flat form
  overflows. `Always` is not recommended for stable formatting: formatter
  updates or small nearby edits can cause widespread layout changes.
  Expression operator layout covers parsed binary, assignment, and
  conditional operators. Operator gaps are normalized in `Always` mode;
  other expression gaps, including call argument breaks, are controlled
  separately by `CallArgumentSplit`. New continuations are indented by
  `IndentWidth` beyond the original line's leading spaces. Comments,
  directives, CRLF, backslashes, and ambiguous or recovered syntax stay
  source-owned. Bare `<`, `>`, `<<`, and `>>` remain untouched even when
  parsed as operators: without name lookup they can be template syntax.
- `ExpressionLayoutAlgorithm`: `Greedy` (default) picks a preferred
  breakpoint that fits the current line; `DynamicProgramming` minimizes
  a bounded whole-line cost for raggedness, extra breaks, and operator
  precedence. Both use the same parser-approved breakpoints, respect
  `BinaryOperatorPlacement`, and prefer outer expression breaks over breaks
  inside parentheses. The choice also affects `Pack` call argument grouping,
  but not `Preserve` or `OnePerLine` calls, function parameters, or
  inheritance. Searches with more than 96 candidates, or without a fitting
  path, fall back to `Greedy`.
- `BinaryOperatorPlacement`: `Trailing` (default, keep a wrapped binary
  operator at the end of the previous line) or `Leading` (put it at the
  start of the continuation line). Only applies when
  `ExpressionLayoutCondition` is not `Preserve`; assignment operators and
  conditional `?`/`:` remain leading. `Overflow` keeps existing operator
  breaks but safely moves
  the newline to the configured side of a binary operator, even if the
  expression otherwise fits on one line.
- `TernaryOperatorPlacement`: `Preserve` (default) leaves the side of
  existing `?` and `:` breaks unchanged. New breaks use leading placement
  unless another delimiter is already split, in which case `ThreeLines`
  follows that delimiter's side. `Leading` puts the operators at the start
  of their continuation lines; `Trailing` keeps them at the end of the
  preceding line. Explicit placement also corrects safe existing breaks
  in `Overflow` mode, independently of `TernarySplit`.
- `TernarySplit`: `Preserve` (default) neither adds nor joins ternary
  breaks. `AsNeeded` allows either delimiter or both to break. `ThreeLines`
  keeps a flat ternary flat when it fits; if the ternary is split, it
  separates the condition, true branch, and false branch by breaking at
  both delimiters. These settings apply only when `ExpressionLayoutCondition`
  is not `Preserve`. Comments, directives, CRLF, and ambiguous or incomplete
  syntax stay source-owned. In `--check` mode, expression and call reflow
  is skipped: configured layout checks report overlong lines, incorrect
  existing operator placement, and incomplete `ThreeLines` splits instead
  of showing a speculative expression reflow diff.
- `CallArgumentSplit`: `Preserve` (default, leave call arguments
  unchanged), `Pack` (put as many arguments as fit on each line), or
  `OnePerLine` (one argument per continuation line if a split is chosen).
  Enabled only when `ExpressionLayoutCondition` is not `Preserve`. Calls
  that fit on one line are not forced to split.
- `FirstCallArgumentPosition`: `CallLine` (default) or `NewLine` selects
  where the first argument starts when a call is split.
- `EnforceFirstCallArgumentPosition`: `false` (default) retains the first
  argument's position when reflowing an already split call in `Overflow`
  or `FitOrOverflow`; `true` applies the setting above to those calls.
  `Always` always applies the configured position.
- `CallArgumentIndent`: `IndentWidth` (default, indent one width beyond
  the call's line) or `Continuation` (align with the opening `(`).
  These options only affect complete parsed calls with plain, single-line
  arguments and whitespace-only list gaps. Comments, directives, CRLF,
  and ambiguous parenthesized callees remain unchanged. Parsed function-like
  macro calls follow the same call layout rules; nested calls are considered
  after enclosing calls, so outer commas can resolve overflow before inner
  commas are tried. Multiline arguments keep their call list unchanged.
  Reflowed lists use comma-space separators;
  `SpaceInsideCallArguments = Space` also applies to their same-line edges.
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
  `SCREAMING_SNAKE_CASE` (ASCII letters and digits), or `Unchecked` to disable
  the case check. Prefixes are a nonempty array of quoted identifier
  fragments; at least one must match. The case
  applies to the name *after* its prefix. Use `[""]` to explicitly allow no
  prefix. Either check can be enabled independently; omitted checks are
  disabled. Only unambiguous definitions and variable declarations are
  checked; forward declarations, function prototypes, explicit template
  instantiations, references, aliases, macros and uncertain declarators are
  skipped. Names in function definitions, including parameter names, are
  checked. `--fix` leaves names untouched.
- `NamingNamespaceExemptions`: qualified namespace paths excluded from all
  naming checks, including the namespace name and everything it contains.
  Defaults to `["std"]`, which also excludes `std::chrono`. Use `[]` to
  disable exemptions or list other paths such as `"vendor::api"`.
  Matching uses complete path components (so `std` does not exclude
  `standard`). These exemptions do not affect formatting or the independent
  unnamed-function-parameter checks.
- `FunctionDeclarationParameterNames`: `Unchecked` (default) or
  `Required`. When required, check mode reports unnamed parameters in
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
