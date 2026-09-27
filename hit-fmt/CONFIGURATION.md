# Configuration

The formatter preserves source-owned token and trivia bytes except where an
explicitly configured declaration brace, lambda capture-to-parameter
spacing, lambda capture spacing, function parameter or call spacing,
control keyword spacing or brace placement, token spacing, generated
indentation, preprocessor indentation, trailing comment spacing,
namespace comment, include ordering, or function parameter rule applies.
All formatting rules default to preserving the source.
Naming rules default to unchecked and never rewrite identifiers; checks
report violations with file, line and column and return a nonzero status.
`--fix` also checks: it reports applied changes as `fixed` and returns a
nonzero status only when violations remain.

`hit-fmt` searches for `.hit-fmt` beside each source file and then in its
parent directories. The nearest file wins. `--config <file>` selects one
file for every input; `--no-config` disables configuration lookup.

## Changed-hunk mode

Pass `--changed-hunks <file>` (or `--changed-hunks -` for stdin) instead
of input paths to check only changed hunks. `--changed-ranges` is retained
as an alias. Add `--fix` to apply available fixes and check the result:

```text
file src/a.cpp
hunk 12:18
hunk 35:35
file src/b.cpp
hunk 4:9
```

Each `file` path is relative to the process's current working directory,
not the input file's directory. `hunk` endpoints are inclusive, one-based
line numbers in the **current** file; a deletion should be anchored to a
surviving neighbouring line. The ranges input can have blank lines and
repeated file groups. Each group needs at least one hunk. Empty input
processes no files; malformed input and out-of-bounds ranges are errors.
Configuration and file exclusions still apply per file.

Hunks are anchors, not hard edit boundaries. Edits can spill across an
enclosing parsed argument, parameter, initializer or capture list (or a
for-header), and a sorted include run can spill across its contiguous group.
Whitespace edits retaining line breaks are selected per source line.
Adding/removing line breaks requires one selected window to contain the
whole gap. Include sorting may change token order within a selected run;
other token-changing rules are not applied in this mode until they can
expose atomic edits. If such a rule targets a selected hunk, formatting
of that file is skipped rather than applying a partial structural change.
Spill-over is on by default and capped at 200 source lines per unit.
With `--no-spillover`, a fix that needs to leave the selected hunk lines
is not applied; the usual formatting error is still reported. Independent
fixable hunks can still be fixed. Check-only and `--fix` use the same
scope for reporting violations, and naming and parameter-name checks run
only on anchored lines. This mode does not run Git; a caller can supply
hunks from any diff. File names containing newlines cannot be represented
in this format.


Standalone `// hit-fmt off` / `// hit-fmt on` and
`// clang-format off` / `// clang-format on` comments disable formatting
between and including their marker lines. Single-line `/* ... */` marker
comments also work. Markers must be the only non-whitespace content on
those lines; strings and trailing comments do not count. Nested off/on
pairs are supported, an unclosed off runs to the end of the file, and a
lone on has no effect. Disabled lines are excluded from layout diagnostics;
other checks, such as naming and parameter names, remain active.

Blank lines and `#` comments are allowed. Arrays of quoted strings may
span lines, with comments between items; each quoted item must stay on one
line. Example:

```text
# Indentation and limits
IndentWidth = 4
ColumnLimit = 120
IndentationKind = Preserve
TrailingWhitespace = Preserve

# Includes, directives, and comments
PreprocessorIndentation = Preserve
TrailingCommentSpacing = Preserve
IncludeOrder = Preserve
# SeparatorLength = 80 enforces existing standalone slash rulers

# Templates and attributes
TemplateKeywordSpacing = Preserve
TemplateTypeArgumentInteriorSpacing = Preserve
TemplateTypeArgumentSpacing = Preserve
AmbiguousTemplateDeclarationSpacing = Preserve
RequirementBodyInteriorSpacing = Preserve
AttributeSpaceBeforeSpecifier = Preserve
AttributeInteriorSpacing = Preserve
EmptyAttributeInteriorSpacing = Preserve
AttributeSpaceAfterSpecifier = Preserve

# Records, namespaces, and declarators
RecordBodyInteriorSpacing = Preserve
NamespaceBodyInteriorSpacing = Preserve
EnumBodyInteriorSpacing = Preserve
AccessModifierColonSpacing = Preserve
BitFieldColonSpacing = Preserve
EnumUnderlyingTypeColonSpacing = Preserve
PointerReferenceDeclaratorLocation = Preserve
NestedDeclaratorInteriorSpacing = Preserve
NestedDeclaratorPointerSpacing = Preserve
MemberPointerDeclaratorSpacing = Preserve
ArrayDeclarationInteriorSpacing = Preserve
ArrayDeclarationSpacing = Preserve
EmptyArrayDeclarationInteriorSpacing = Preserve
StructuredBindingBracketSpacing = Preserve
IndentAccessModifiers = Preserve
AccessModifierIndentWidth = 0
IndentMembers = Preserve
MemberIndentWidth = 0
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
ClassBraceSpacing = SingleSpace
EnforceClassBraceSpacing = false
StructBracePlacement = Preserve
StructBraceSpacing = SingleSpace
EnforceStructBraceSpacing = false
UnionBracePlacement = Preserve
UnionBraceSpacing = SingleSpace
EnforceUnionBraceSpacing = false
EnumBraceSpacing = Preserve
NamespaceBracePlacement = Preserve
NamespaceBraceSpacing = SingleSpace
EnforceNamespaceBraceSpacing = false
NamespaceComments = Preserve
NamespaceCommentSpaces = 1
ShortNamespaceLines = 0

# Functions and parameters
FunctionSuffixSpecifierSpacing = Preserve
FunctionQualifierSpacing = Preserve
FunctionRefQualifierSpacing = Preserve
NoexceptSpaceBeforeSpecifier = Preserve
NoexceptSpaceBeforeParentheses = Preserve
ReturnArrowSpacing = Preserve
FunctionSpaceBeforeParameters = Preserve
SpaceInsideFunctionParameters = NoSpace
EnforceSpaceInsideFunctionParameters = false
EmptyFunctionParametersSpacing = Preserve
FunctionParameterSplit = Pack
FirstFunctionParameterPosition = NameLine
EnforceFirstFunctionParameterPosition = false
FunctionParameterIndent = IndentWidth
# FunctionParameterNewLineIndent = 4 overrides indentation when first is new
MergeFunctionParameters = false
FunctionParameterClosingParenthesis = SameLine
EnforceFunctionParameterClosingParenthesis = false
FunctionBracePlacement = Preserve
FunctionBraceSpacing = SingleSpace
EnforceFunctionBraceSpacing = false

# Lambdas
SpaceAroundCaptureList = Preserve
EmptyLambdaCaptureSpacing = Preserve
SpaceAroundCaptureAssignment = Preserve
LambdaCaptureSplit = Preserve
LambdaSpaceBeforeParameters = Preserve
SpaceInsideLambdaParameters = Preserve
EmptyLambdaParametersSpacing = Preserve
LambdaCallSpaceBeforeArguments = Inherit
LambdaBracePlacement = Preserve
LambdaBraceSpacing = SingleSpace
EnforceLambdaBraceSpacing = false
EmptyLambdaBodySpacing = Inherit
SingleStatementLambdaBodyLayout = Inherit

# Calls and chains
FunctionCallSpaceBeforeParameters = Preserve
SpaceInsideCallArguments = NoSpace
EnforceSpaceInsideCallArguments = false
EmptyFunctionCallArgumentsSpacing = Preserve
FunctionCallArgumentClosingParenthesis = SameLine
EnforceFunctionCallArgumentClosingParenthesis = false
CallArgumentSplitColumnLimit = 0
CallArgumentSplitColumnLimitNames = []
MultilineSingleCallArgumentPosition = CallLine
EnforceMultilineSingleCallArgumentPosition = false
CallLineFirstStringNames = []
CallLikeMacroNames = []
WhitespaceSensitiveMacros = []
ChainedCallRootNames = []
ChainedCallSplitCondition = Preserve
ChainedCallLayout = OnePerLine
ChainedCallIndent = IndentWidth

# Control flow
ControlBlockKeywordSpacing = Preserve
ControlBlockConditionSpacing = Preserve
LoopSemicolonSpacing = Preserve
ConditionInitSemicolonSpacing = Preserve
RangeBasedForColonSpacing = Preserve
ControlHeaderClauseSplit = Preserve
ControlConditionIndent = IndentWidth
ForHeaderClauseIndent = Inherit
ControlBlockBracePlacement = SameLine
EnforceControlBlockBracePlacement = false
ControlBlockSpaceBeforeBrace = Space
EnforceControlBlockSpaceBeforeBrace = false
AddMissingControlBraces = false
EmptyControlBody = Preserve
IndentCaseLabels = Preserve
CaseLabelIndentWidth = 0
LabelColonSpacing = Preserve

# Blocks and statements
BlockInteriorSpacing = Preserve
EmptyBlockInteriorSpacing = NoSpace
EnforceEmptyBlockInteriorSpacing = false
StatementSemicolonSpacing = Preserve
DeclarationSemicolonSpacing = Preserve
IndentBlockBodies = false
StandaloneBlockBraceIndentation = Indent
CollapseEmptyFunctionBodies = false
NonEmptyBlockBodyLayout = Preserve
BlockBodyIndentWidth = 0
BlockClosingBrace = NewLine
EnforceBlockClosingBrace = false
StatementAttributeIndent = Preserve
StatementAttributeIndentWidth = 0

# Initializers, new, and delete
BraceInitializerSpacing = Preserve
InitializerBracesSpacing = NoSpace
EnforceInitializerBracesSpacing = false
EmptyBraceInitializerInteriorSpacing = Preserve
InitializerListSplit = Preserve
FirstInitializerElementPosition = BraceLine
InitializerListIndent = IndentWidth
InitializerClosingBrace = SameLine
EnforceInitializerClosingBrace = false
InitializerTrailingComma = Keep
MergeInitializerList = false

PlacementNewSpaceBeforeArguments = Preserve
PlacementNewArgumentInteriorSpacing = Preserve
PlacementNewSpaceAfterArguments = Preserve
NewArraySpaceBeforeBrackets = Preserve
NewArrayInteriorSpacing = Preserve
EmptyNewArrayInteriorSpacing = Preserve
NewInitializerSpaceBeforeParentheses = Preserve
NewInitializerParenthesisInteriorSpacing = Preserve
NewInitializerSpaceBeforeBraces = Preserve
NewInitializerBraceInteriorSpacing = Preserve
DeleteArraySpaceBeforeBrackets = Preserve
DeleteArraySpaceAfterBrackets = Preserve

ConstructorInitializerList = Preserve
ConstructorInitializerListColonPlacement = AttachToInitializer
ConstructorInitializerListColonSpaceBefore = NoSpace
ConstructorInitializerListColonSpaceAfter = Space
ConstructorInitializerListCommaPlacement = AttachToNext
ConstructorInitializerListCommaSpaceBefore = NoSpace
ConstructorInitializerListCommaSpaceAfter = Space
ConstructorInitializerListStart = NewLine
ConstructorInitializerListSplit = OnePerLine
# ConstructorInitializerListIndent defaults to IndentWidth when omitted

# Expressions and operators
NamedCastSpaceBeforeTypeArguments = Preserve
NamedCastSpaceBeforeOperandParentheses = Preserve
CastArgumentSpacing = Preserve
CastSpacing = Preserve
ReturnSpaceBeforeParentheses = Preserve
ThrowSpaceBeforeParentheses = Preserve
CoReturnSpaceBeforeParentheses = Preserve
SizeofPackSpaceBeforeParentheses = Preserve
CoAwaitSpaceBeforeParentheses = Preserve
AlignasSpaceBeforeParentheses = Preserve
AlignofSpaceBeforeParentheses = Preserve
SizeofSpaceBeforeParentheses = Preserve
DecltypeSpaceBeforeParentheses = Preserve
TypeidSpaceBeforeParentheses = Preserve
RequiresSpaceBeforeParentheses = Preserve
StaticAssertSpaceBeforeParentheses = Preserve
GroupingInteriorSpacing = Preserve
MemberAccessOperatorsSpacing = Preserve
ScopeResolutionSpacing = Preserve
UnaryOperatorSpacing = Preserve
PostfixOperatorSpacing = Preserve
BinaryOperatorSpacing = Preserve
AssignmentOperatorSpacing = Preserve
TernaryOperatorSpacing = Preserve
OperatorOverloadOperatorSpacing = Preserve
CommaSpacing = Space
EnforceCommaSpacing = false
SubscriptOperatorSpacing = Preserve
SubscriptOperatorInteriorSpacing = Preserve
ExpressionLayoutCondition = Preserve
TernaryOperatorPlacement = Leading
EnforceTernaryOperatorPlacement = false

# Naming checks
FunctionDeclarationParameterNames = Unchecked
FunctionDefinitionParameterNames = Unchecked
StructNameCase = PascalCase
StructNamePrefixes = ["S", "C"]
NamingNamespaceExemptions = ["std"]
VariableNameCase = snake_case
VariableNamePrefixes = ["m_", "s_"]

# Path exclusions
Exclude = [
    "(^|/)generated(/|$)",
    "(^|/)legacy\\.cpp$"
]
```

## Option reference

### Indentation and limits

- `IndentWidth`: positive integer up to 32. Used for record bodies and base
  lists, and for parameter or call argument continuation lines when their
  respective indent setting is `IndentWidth`.
- `ColumnLimit`: positive integer up to 10000. Function parameter lists,
  call arguments, expressions, and inheritance lists can be wrapped
  according to their split settings.
- `IndentationKind`: `Preserve` (default), `Spaces`, or `Tabs` for
  formatter-generated indentation. `Preserve` and `Spaces` retain the
  previous space-based output; `Tabs` emits tabs at `IndentWidth`-column
  stops, then spaces for any remainder. Existing indentation and comment
  text are not converted. This also affects indentation generated by
  `PreprocessorIndentation = PreprocessorIndented`.
- `TrailingWhitespace`: `Preserve` (default) leaves line endings intact;
  `Trim` removes trailing spaces and tabs before LF, CRLF, or end of file.
  Whitespace inside tokens (notably raw strings and preprocessor directives)
  and lines ending in a backslash are preserved to avoid changing a line
  splice.

### Includes, directives, and comments

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
- `PreprocessorIndentation`: `Preserve` (default), `LineStart`, or
  `PreprocessorIndented`. `LineStart` places single-line directives at
  column zero. `PreprocessorIndented` does the same for outer directives
  and adds one `IndentWidth` level after `#if`, `#ifdef`, and `#ifndef`;
  nested directives, including `#define`, use that level. `#elif`,
  `#else`, and `#endif` align with their opener. This setting changes
  only directive prefixes, not code in branches, directive contents, or
  multiline/continued directives. Unbalanced conditional groups remain
  unchanged in `PreprocessorIndented` mode.
- `TrailingCommentSpacing`: `Preserve` (default), `Space`, or `NoSpace`
  between code and a same-line `//` comment. Standalone comments, block
  comments, comments within directives, and line breaks stay unchanged;
  this does not align comment columns.
- `SeparatorLength`: unset (default) leaves slash rulers unchanged; set to
  an integer from 8 to 10000 to normalize existing, standalone comment
  lines made solely of eight or more `/` characters. Rulers are also moved
  to column 1, even when their length is already correct. It does not add
  missing separators or alter trailing, documentation, or raw-string
  comments.

### Templates and attributes

- `TemplateKeywordSpacing`: `Preserve` (default), `Space`, or `NoSpace`
  between `template` and `<` in a complete parsed template declaration,
  including `template<>`. `template` disambiguators in expressions are not
  changed.
- `TemplateTypeArgumentInteriorSpacing`: `Preserve` (default), `Space`, or
  `NoSpace` just inside a nonempty, complete parsed template parameter or
  argument list in an unambiguous type or template-id context. Comparisons,
  shifts, unknown expressions, `>>`-terminated lists, and empty `<>` stay
  unchanged. This renames `TemplateAngleBracketsSpacing`.
- `TemplateTypeArgumentSpacing`: `Preserve` (default), `Space`, or
  `NoSpace` before `<` in a complete, confirmed template argument list
  following an identifier (`vector <int>` versus `vector<int>`). This
  also covers empty `<>`; template parameter lists use
  `TemplateKeywordSpacing` instead. Multiline and commented gaps are
  preserved.
- `AmbiguousTemplateDeclarationSpacing`: `Preserve` (default) or
  `Space`. In a conservatively parsed `Type<...> name = value;` statement,
  `Space` normalizes the type-to-name gap to one space. It also normalizes
  the name-to-`=` gap if `AssignmentOperatorSpacing = Space`. Other
  ambiguous statements remain unchanged.
- `RequirementBodyInteriorSpacing`: `Preserve` (default), `Space`, or
  `NoSpace` just inside a parsed requires-expression body, including
  empty bodies: `requires(T x){ x; }` versus `requires(T x){x;}`.
  This rule preserves newlines, comments, and recovered syntax.
- `AttributeSpaceAfterSpecifier`: `Preserve` (default), `Space`, or
  `NoSpace` between a parsed `[[...]]` specifier and a following
  declaration word (`[[nodiscard]] int f()` versus `[[nodiscard]]int f()`).
  This is independent of `AttributeInteriorSpacing`.
- `AttributeSpaceBeforeSpecifier`: `Preserve` (default), `Space`, or
  `NoSpace` before a parsed `[[...]]` specifier following a declaration
  token (`int [[maybe_unused]] x` versus `int[[maybe_unused]] x`).
  Leading attributes have no preceding declaration gap.
- `EmptyAttributeInteriorSpacing`: `Preserve` (default), `Space`, or
  `NoSpace` inside an empty parsed attribute list (`[[ ]]` versus
  `[[]]`). `AttributeInteriorSpacing` controls nonempty lists.
- `AttributeInteriorSpacing`: `Preserve` (default), `Space`, or
  `NoSpace` just inside nonempty parsed C++ attributes:
  `[[ nodiscard ]]` versus `[[nodiscard]]`. The two bracket pairs are
  kept intact. Multiline and commented gaps are preserved.

### Records, namespaces, and declarators

- `RecordBodyInteriorSpacing`, `NamespaceBodyInteriorSpacing`, and
  `EnumBodyInteriorSpacing`: `Preserve` (default), `Space`, or `NoSpace`
  just inside their respective parsed braces on one line, including
  empty bodies: `struct S{ int x; }` versus `struct S{int x;}` and
  `namespace N{ }` versus `namespace N{}`. Class, struct, and union
  bodies share the record option; brace placement is separate.
- `AccessModifierColonSpacing` and `RangeBasedForColonSpacing`:
  `Preserve` (default), `Space`, or `NoSpace` on both same-line sides of
  parsed access-label colons (`public : int x;` versus `public:int x;`)
  and range-for separators (`for (auto x : items)` versus
  `for (auto x:items)`), respectively. Other colons are unaffected.
  Both colon settings leave multiline, commented, or incomplete gaps
  source-owned.
- `BitFieldColonSpacing`, `EnumUnderlyingTypeColonSpacing`, and
  `LabelColonSpacing`: `Preserve` (default), `NoSpace` (`x:1`), `Left`
  (`x :1`), `Right` (`x: 1`), or `LeftAndRight` (`x : 1`). The options
  apply only to their parsed bit-field, enum underlying-type, and label
  colons, respectively; `LabelColonSpacing` covers `case` and `default`
  labels as well. Line breaks, comments, and directives are untouched.
- `PointerReferenceDeclaratorLocation`: `Preserve` (default), `Left`
  (`int* p`), `Middle` (`int * p`), or `Right` (`int *p`). Applies only to
  complete parsed pointer and reference declarators, including adjacent
  `*`/`&` tokens. A later declarator after a comma has no repeated type;
  under `Left` its `*`/`&` binds to the name (`int* a, *b`). Member pointers
  and nested declarators have independent rules. In expression
  contexts, `UnaryOperatorSpacing` controls prefix dereference/address-of;
  an ambiguous macro argument remains source-owned.
- `NestedDeclaratorInteriorSpacing`: `Preserve` (default), `Space`, or
  `NoSpace` just inside complete nested declarator parentheses:
  `( *callback )` versus `(*callback)`.
- `NestedDeclaratorPointerSpacing`: `Preserve` (default), `Space`, or
  `NoSpace` between a parsed `*`/`&` in a nested declarator and its
  immediately following name (`(* callback)` versus `(*callback)`).
- `MemberPointerDeclaratorSpacing`: `Preserve` (default), `Space`, or
  `NoSpace` between a parsed member-pointer `::*` and the following
  declarator name (`S::* member` versus `S::*member`). These rules
  preserve multiline and commented gaps and do not alter expressions.
- `*BracePlacement`: `Preserve` (default) keeps the existing layout,
  `Attach` joins a brace to its header, and `NewLine` moves it to its own
  line. Existing same-line spacing is left unchanged by `Attach`. Each of
  class, struct, union, function definition, lambda, and
  namespace is configured independently. `Attach` places the brace before
  a trailing header comment (`//` or `/* ... */`). When `NewLine` splits an
  inline brace followed by a trailing comment, the comment stays with the
  declaration header and the brace goes below it. Other intervening comments
  and directives block attachment. Other braces are unaffected.
- `*BraceSpacing`: `SingleSpace` (default) selects the spacing when a
  brace is attached to its header. For class, lambda, struct, union,
  function definition, and namespace, the corresponding `Enforce*BraceSpacing`
  (default `false`) also normalizes existing same-line gaps. Enum braces
  retain their separate `Preserve`/`SingleSpace` setting.
  Line breaks and gaps containing comments or directives remain untouched.
  Placement is applied first: `NewLine` keeps the brace on its own line;
  `Attach` joins it to the header, using one space at the join.
- `NamespaceComments`: `Preserve` (default) leaves closing comments alone,
  `Remove` removes a line comment immediately after a namespace's closing
  brace, `AddMissing` inserts `// namespace name` (or `// namespace` for an
  anonymous namespace) only when no same-line comment is present, and `Fix`
  also corrects existing trailing line comments. The former `Required`
  spelling is no longer accepted. `NamespaceCommentSpaces` sets the spaces
  before generated or corrected comments (1 by default, positive integer
  up to 32); it has no effect on existing comments with `AddMissing`, or
  in `Preserve` and `Remove` modes. Unmatched braces and closings followed
  by a block comment are left alone.
- `ShortNamespaceLines`: nonnegative integer up to 10000. With
  `NamespaceComments = AddMissing` or `Fix`, skip adding a closing comment
  when the namespace has at most this many lines between its opening and
  closing braces. `0` (default) disables the exemption. Existing closing
  comments remain untouched with `AddMissing` and are corrected even on
  short namespaces with `Fix`; `Remove` still removes them.
- `IndentAccessModifiers` and `IndentMembers`: `Preserve` (default)
  leaves indentation unchanged, `Indent` uses `AccessModifierIndentWidth`
  or `MemberIndentWidth` relative to the record declaration, and `NoIndent`
  aligns with that declaration.
  Access labels and direct members of class, struct, and union bodies use
  independent settings. Enum enumerators use `IndentMembers` (enums do
  not have access labels). Only existing lines are reindented; nested
  closing braces follow their reindented declarations. Inline members,
  comments, directives, and conditional branches remain source-owned.
  The indent-width overrides for access labels, members, block bodies,
  case labels, and statement attributes accept positive integers up to
  32; `0` (default) uses the global `IndentWidth`. They affect only
  constructs whose corresponding indentation setting is enabled.
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

### Functions and parameters

- `FunctionSuffixSpecifierSpacing`: `Preserve` (default), `Space`, or
  `NoSpace` before parsed function `override`/`final` and lambda
  `mutable`/`constexpr`/`consteval`/`static` suffixes. For example,
  `f() override` versus `f()override` and `[]() mutable` versus
  `[]()mutable`. Gaps between two words cannot be removed when doing
  so would merge their tokens.
- `NoexceptSpaceBeforeSpecifier`: `Preserve` (default), `Space`, or
  `NoSpace` before a parsed function or lambda exception specifier:
  `void f() noexcept` versus `void f()noexcept`. The gap after
  `noexcept` before its optional `(` uses
  `NoexceptSpaceBeforeParentheses`. Multiline and commented gaps are
  preserved by these options.
- `FunctionQualifierSpacing`: `Preserve` (default), `Space`, or `NoSpace`
  between a function's closing parameter `)` and an immediately following
  `const` or `volatile` (`f() const` versus `f()const`).
- `FunctionRefQualifierSpacing`: `Preserve` (default), `Space`, or
  `NoSpace` before a parsed trailing `&` or `&&` after the parameters
  or a `const`/`volatile` qualifier (`f() const &` versus `f() const&`).
- `ReturnArrowSpacing`: `Preserve` (default), `NoSpace` (`f()->T`), `Left`
  (`f() ->T`), `Right` (`f()-> T`), or `LeftAndRight` (`f() -> T`) around
  the arrow of a parsed trailing return type. Member-access `->` is
  unaffected.
- `FunctionParameterClosingParenthesis` and
  `FunctionCallArgumentClosingParenthesis`: `SameLine` (default, beside the
  final parameter or argument) or `NewLine` selects the position when
  reflowing a list. Their matching `Enforce...` options (default `false`)
  also apply the placement to existing closing gaps without reflow.
- `FunctionParameterSplit`: `Preserve` (default, leave parameter lists
  unchanged), `Pack` (put as many parameters as fit on each line), or
  `OnePerLine`. Lists already fitting on one line are left unchanged.
- `FirstFunctionParameterPosition`: `NameLine` (default), `NewLine`, or
  `NameLineIfFits`. The latter keeps the first parameter beside `(` only
  if it and its following comma or closing suffix fit within `ColumnLimit`;
  otherwise it moves the first parameter to a new line. Existing split
  lists retain their first parameter's position unless that placement
  already exceeds the limit or enforcement is enabled.
- `EnforceFirstFunctionParameterPosition`: `false` (default) retains the
  first parameter's position in already split lists; `true` applies the
  configured position to those lists as well.
- `FunctionParameterIndent`: `IndentWidth` (default, indent one width
  beyond the declaration) or `Continuation` (align with the character
  immediately after the opening `(`).
- `FunctionParameterNewLineIndent`: optional integer from 0 to 32. When
  nonzero and the first parameter starts on a new line, indent all its
  continuation lines by this many columns relative to the declaration,
  instead of aligning them beneath `(`. Zero (default) uses the normal
  `FunctionParameterIndent` policy.
- `MergeFunctionParameters`: `false` (default) retains split lists as
  multiline even when they would fit on one line; `true` joins them when
  the result fits within `ColumnLimit`.
- `FunctionSpaceBeforeParameters`: `Preserve` (default) leaves the gap
  between a named function declarator and its parameter `(` unchanged;
  `Space` inserts one space, and `NoSpace` removes it. Applies to function
  declarations and definitions, including constructors, destructors, and
  operators. Newlines, comments, and directives remain unchanged. Calls,
  lambdas, control conditions, and function-pointer declarations are
  unaffected.
- `SpaceInsideFunctionParameters`: `NoSpace` (default) or `Space` sets
  the edges of reflowed nonempty parameter lists. Set
  `EnforceSpaceInsideFunctionParameters` (default `false`) to also
  normalize existing same-line gaps. Newlines, adjacent comments, and
  directives are preserved. Empty `()` pairs, lambdas, calls, control
  conditions, and `requires` expressions are unaffected.

Function parameter rules change only safely movable lists in function
declarators; comments, directives, multiline parameter expressions, and
CRLF lists are left intact. They do not change calls.

### Lambdas

- `EmptyLambdaBodySpacing`: `Inherit` (default) uses
  `EmptyBlockInteriorSpacing`; `Preserve`, `Space`, or `NoSpace` overrides
  it only for empty parsed lambda bodies. YTsaurus uses both `{}` and
  `{ }`, so its example profile chooses `Preserve`.
- `SingleStatementLambdaBodyLayout`: `Inherit` (default) uses
  `BlockInteriorSpacing` for inline lambda bodies; `Preserve` leaves the
  interior of single-statement lambdas untouched. `OneLineIfFits` joins a
  safe multiline, single-line statement and its braces if the complete
  resulting line fits `ColumnLimit`; it uses spaces around the statement.
  Comments, directives, multiline statements, and recovered bodies stay
  source-owned. The YTsaurus profile chooses `Preserve` because both
  inline and multiline forms appear in the source.
- `LambdaSpaceBeforeParameters`: `Preserve` (default) leaves the gap between a
  lambda's capture `]` and immediate parameter `(` unchanged; `Space`
  inserts exactly one space and `NoSpace` removes it. Newlines and
  intervening comments or directives are left unchanged. Calls and other
  parenthesized expressions are unaffected.
- `SpaceInsideLambdaParameters`: `Preserve` (default) leaves the gaps just
  inside the `(` and `)` of a nonempty lambda parameter list unchanged;
  `Space` uses one space and `NoSpace` removes it. For example, `Space`
  formats `[](x)` as `[]( x )`. Applies to generic lambdas as well.
  Empty `()`, newlines, incomplete lists, and gaps beside directives or
  line comments remain untouched. A trailing block comment before `)`
  follows this option only when separated by horizontal whitespace; the
  comment text stays unchanged. Does not affect capture brackets, the gap
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
- `LambdaCaptureSplit`: `Preserve` (default) leaves capture lists as
  written; `OnePerLineOnOverflow` puts each safe, parsed capture on its
  own line with `[` on the header line and `]` aligned with that line.
  It triggers only when the capture list itself crosses `ColumnLimit`,
  not when a short capture list precedes long lambda parameters. Such a
  lambda can still move below `(` if its full header then fits. Existing
  multiline lists, comments, directives, and ambiguous captures stay
  source-owned. The YTsaurus profile uses `OnePerLineOnOverflow` to avoid
  moving a multiline lambda away from its call merely to shorten the
  header.
- `LambdaCallSpaceBeforeArguments`: `Inherit` (default) uses
  `FunctionCallSpaceBeforeParameters` for an immediately invoked lambda;
  `Preserve` keeps the original gap, `Space` inserts one space, and
  `NoSpace` removes it. Other calls and lambda parameter declarations
  are unaffected.

### Calls and chains

- `EmptyFunctionCallArgumentsSpacing`, `EmptyFunctionParametersSpacing`,
  `EmptyLambdaParametersSpacing`, and `EmptyLambdaCaptureSpacing`:
  `Preserve` (default), `Space`, or `NoSpace` inside their respective
  empty, parsed pairs: `f( )`, `void f( )`, `[]( )`, and `[ ]` versus
  `f()`, `void f()`, `[]()`, and `[]`. Nonempty interiors have separate
  options. All token-spacing rules leave newlines, comments, directives,
  and incomplete statements unchanged.
- `CallArgumentSplit`: `Preserve` (default, leave call arguments
  unchanged), `Pack` (put as many arguments as fit on each line), or
  `OnePerLine` (one argument per continuation line if a split is chosen).
  Independent of `ExpressionLayoutCondition`. Calls that fit on one line
  are not forced to split. A single safe argument can move to a continuation
  line. For nested single-argument calls, safe one-break inner and outer
  layouts are compared so the chosen split balances the resulting lines.
  With expression layout enabled, safe logical-operator, cast-operand,
  and single-call-argument breaks are compared on overlong lines.
  Fitting outer logical expressions take priority over nested breaks;
  remaining candidates are ranked by maximum width, then balance.
  Ternary layout runs first;
  name-specific call limits and source-owned syntax retain their policies.
  With `OnePerLine`, a flat enclosing multi-argument call owns the
  overflow; unsafe calls remain source-owned rather than splitting only
  inside an argument.
  A short single-argument member call is not split just because the following
  chain segment overflows; multiline lambda segments stay source-owned.
  A direct multiline lambda can move below `(` only if the relocated
  header fits and its body can be reindented safely. When a call split
  resolves an overlong assignment RHS, a safe break after `=` can join
  if the new first line
  fits; other existing breaks stay as written. Safe two-argument
  `static_assert` declarations use the same split policy.
- `CallArgumentSplitColumnLimit`: `0` (default) uses `ColumnLimit` for every
  call. With a positive value, calls named in
  `CallArgumentSplitColumnLimitNames` use the smaller of that value and
  `ColumnLimit` to decide when to reflow arguments. The names are exact,
  bare call identifiers, default `[]`; other calls keep the global limit.
  This does not change the global expression line-length check. The
  YTsaurus profile selects `EXPECT_THROW` at 90 columns without splitting
  shorter assertions.
- `FirstCallArgumentPosition`: `CallLine` (default) or `NewLine` selects
  where the first argument starts when a call is split.
- `MultilineSingleCallArgumentPosition`: `CallLine` (default) keeps the
  opening of a multiline sole argument beside the call name; `NewLine`
  places it below `(` using `CallArgumentIndent` when a split is needed.
  `EnforceMultilineSingleCallArgumentPosition` (default `false`) also
  checks and corrects safe existing multiline calls, even when
  `CallArgumentSplit = Preserve`. It does not split flat, fitting arguments or
  reshape the argument's interior. A sole nested call stays on the outer
  call's line so its own arguments can split first. Braced and
  lambda-containing arguments, comments, directives, and recovered syntax
  remain source-owned (apart from safe overlong direct lambdas, whose body
  indentation moves with the capture). Without enforcement it applies when
  `CallArgumentSplit` is enabled. The YTsaurus profile uses `NewLine`
  with enforcement enabled.
- `ChainedCallRootNames`: exact names of call expressions that may start
  a formatted member-call chain, default `[]`. A simple name also matches
  namespace-qualified calls ending in that name (`MakeAction` matches
  `NExample::MakeAction`); a fully qualified name can restrict matching
  to that spelling. A similarly named function or a method invoked on an
  object is not a match. An empty list leaves all chains unchanged.
- `ChainedCallSplitCondition`: `Preserve` (default) keeps chains unchanged;
  `Overflow` splits only if an individual line of the chain exceeds
  `ColumnLimit` measured from its root, excluding preceding statements
  and surrounding ternary text. `Always` puts the first eligible
  continuation on its own line regardless of width. Safe existing breaks
  retain their indentation in `Overflow` mode.
- `ChainedCallLayout`: `OnePerLine` (default) splits each member call once
  the condition applies; `Pack` places as many calls as fit on a line.
  A non-call receiver keeps its first call on the receiver line. Nested
  calls, comments, directives, CRLF, and multiline call arguments remain
  source-owned when they cannot be reflowed safely.
- `ChainedCallIndent`: `IndentWidth` (default) indents new chain lines one
  level relative to the chain statement. `Continuation` aligns them with
  the start of the root call. Existing breaks are not reindented by
  `Overflow`.
- `CallLikeMacroNames`: exact macro names whose declaration-like syntax
  the parser treats as abstract function declarators. These opt-in names
  follow `FunctionParameterSplit` without reflowing unrelated macros.
- `WhitespaceSensitiveMacros`: exact macro names, default `[]`. Preserve
  the bytes from each named invocation through its matching `)`, even
  if other formatting passes reflow or respaced the enclosing statement.
  Other code around the invocation remains eligible for formatting.
  Incomplete or unmatchable invocations keep the source unchanged.
  YTsaurus selects `PP_STRINGIZE`, the four `DEFINE_*ENUM*` variants,
  and `YT_DEFINE_ERROR_ENUM`. This preserves the source placement of
  `DEFINE_ENUM`'s closing `);` rather than imposing a new layout.
- `CallLineFirstStringNames`: array of exact, quoted call identifier names,
  default `[]`. In a parsed call to one of these identifiers, the first
  complete, single-token string-literal argument stays on the call line
  when the call is split. Earlier arguments remain on that line too,
  even if it exceeds `ColumnLimit`. Other arguments follow
  `CallArgumentSplit`. Inter-argument comments, multiline arguments,
  and unsafe calls remain unchanged. The YTsaurus profile lists the direct logging macro
  names; it does not select `_IF` or `_UNLESS` variants.
- `EnforceFirstCallArgumentPosition`: `false` (default) retains the first
  argument's position when reflowing an already split call in `Overflow`
  or `FitOrOverflow`; `true` applies `FirstCallArgumentPosition` to
  those calls.
  `Always` always applies the configured position.
- `CallArgumentIndent`: `IndentWidth` (default, indent one width beyond
  the call's line) or `Continuation` (align with the character after the
  opening `(`). With expression layout enabled, `Continuation` also aligns
  new operator breaks inside complete parsed calls, including single-
  argument macros such as `assert`. Call-list reflow only affects calls
  with plain, single-line arguments and whitespace-only list gaps.
  Single-line `/*...*/` comments inside a parameter move with that
  parameter. Comments between arguments, directives, CRLF, and ambiguous
  parenthesized callees remain unchanged. Parsed function-like
  macro calls follow the same call layout rules; nested calls are considered
  after enclosing calls, so outer commas can resolve overflow before inner
  commas are tried. Multiline arguments keep their call list unchanged.
  Reflowed lists use comma-space separators;
  `SpaceInsideCallArguments = Space` also applies to their same-line edges.
- `FunctionCallSpaceBeforeParameters`: `Preserve` (default) leaves the gap
  between a parsed call's callee and argument `(` unchanged; `Space`
  inserts one space and `NoSpace` removes it. Newlines, intervening
  comments, and directives are left unchanged. Function declarations,
  lambda parameters, built-in type casts, and control or `requires`
  expressions are unaffected. Ambiguous parenthesized callees, which
  could instead be C-style casts, are preserved unless the inner expression
  is clearly callable (such as `(*fn)(arg)`). Calls in incompletely parsed
  statements remain source-owned. Parentheses inside a templated return
  type (including member-pointer types) are not mistaken for calls. The
  setting controls the gap *before* the argument list, not whitespace
  between individual arguments.
- `SpaceInsideCallArguments`: `NoSpace` (default) or `Space` selects
  the edges of reflowed nonempty call argument lists. Set
  `EnforceSpaceInsideCallArguments` (default `false`) to also normalize
  existing same-line gaps. Empty pairs,
  newlines, and gaps adjacent to comments or directives remain untouched.
  Ambiguous parenthesized callees (which may be casts), built-in type
  casts, declarations, lambda parameters, control conditions, and calls
  in incompletely parsed statements are unaffected. The rule does not
  change whitespace between arguments.

### Control flow

- `ControlBlockConditionSpacing`: `Preserve` (default), `Space`, or
  `NoSpace` just inside complete `if`, `for`, `while`, `switch`, and `catch`
  condition parentheses (including `do`/`while`).
  `ControlBlockKeywordSpacing` controls the gap *before* `(`.
- `ControlBlockSpaceBeforeBrace`: `Space` (default) or `NoSpace` before
  a new brace; `EnforceControlBlockSpaceBeforeBrace` (default `false`)
  also normalizes existing same-line gaps between a control header
  (`if`, `else`, `for`, `while`, `do`, `switch`, `try`, `catch`) and a
  same-line opening brace. Brace placement
  runs first; a brace left on a separate line stays on that line.
- `LoopSemicolonSpacing` and `ConditionInitSemicolonSpacing`: `Preserve`
  (default), `Space`, or `NoSpace`. The first controls the two clause
  separators of a parsed `for` header; the second controls the single
  top-level init-statement separator in a complete `if` or `switch`
  condition. When enabled, remove same-line space before the semicolon
  and use the selected spacing after it; no space is added before `)` or
  another semicolon. Statement and nested-expression semicolons are not
  changed.
- `AddMissingControlBraces`: `false` (default) or `true` wraps complete,
  unbraced `if`/`else`, `for`, `while`, `switch`, and `do` bodies in `{}`.
  Unbraced switch case labels remain unchanged. It handles nested controls
  and `else if` without changing the binding of `else`.
  Comments, directives, recovered statements and CRLF gaps remain unchanged.
  Braces follow `ControlBlockBracePlacement = AttachUnlessMultiline` for
  multiline headers; inserted bodies use `IndentWidth`. A null-statement
  body (`;`) is governed separately by `EmptyControlBody` and is never
  wrapped into `{ ; }`.
- `EmptyControlBody`: `Preserve` (default) keeps an unbraced `;` body as
  written even if `AddMissingControlBraces = true`; `EmptyBlock` replaces
  the null statement with an empty `{}` body, independently of that option.
  `EmptyBlockInteriorSpacing = Space` produces `{ }`. Brace placement
  follows `ControlBlockBracePlacement`; comments and incomplete or CRLF
  statements stay source-owned.
- `IndentCaseLabels`: `Preserve` (default), `NoIndent` (align labels with
  their switch header), or `Indent` (`CaseLabelIndentWidth` inside the switch).
  Statements following a case get one additional level with `Indent` when
  `IndentBlockBodies` is enabled. Only parsed case/default labels directly
  in complete switch blocks are affected. An explicit `{` following a case
  aligns with the case label. This is independent of `LabelColonSpacing`.
- `ControlHeaderClauseSplit`: `Preserve` (default) leaves control header
  clauses as written; `WhenNeeded` breaks after both top-level `for` header
  semicolons or the `if`/`switch` init-statement semicolon when the header
  exceeds `ColumnLimit` or is already multiline. A nested semicolon is not
  a clause separator. Empty clauses, comments, directives, and incomplete
  headers and existing clause breaks remain source-owned.
  `if`/`switch` clause breaks use
  `ControlConditionIndent`; `for` breaks use `ForHeaderClauseIndent`.
  YTsaurus selects `WhenNeeded`, with `if`/`switch` clauses indented by
  `IndentWidth` and `for` clauses aligned just after `(`.
- `ForHeaderClauseIndent`: `Inherit` (default) uses
  `ControlConditionIndent` for `for` clause breaks, preserving the previous
  setting. `IndentWidth` indents one level beyond the `for` statement;
  `Continuation` aligns under the first character after `(`. It applies
  only to semicolon breaks selected by `ControlHeaderClauseSplit`.
- `ControlConditionIndent`: `IndentWidth` (default) indents new expression
  breaks by one width beyond the original line; `Continuation` aligns
  breaks inside complete parsed `if`, `for`, `while`, `switch`, and `catch`
  conditions below the character after the opening `(`. Header clause
  splitting uses this setting independently of expression layout (for
  headers inherit it unless `ForHeaderClauseIndent` overrides it); other
  expression breaks require expression layout. Existing expression breaks
  stay source-owned in `Overflow` mode;
  tabs and recovered headers retain safe fallback layout.
- `ControlBlockKeywordSpacing`: `Preserve` (default) leaves whitespace
  between a control keyword and its condition `(` unchanged; `Space` uses
  exactly one space and `NoSpace` removes it. Applies to `if`, `for`,
  `while` (including `do`/`while`), `switch`, and `catch`, as well as
  `if constexpr (` and `for co_await (`. Newlines, comments, directives,
  macro-wrapped conditions, calls, and function declarations are unchanged.
  `else` and `do` have no condition `(` to which this rule applies.
- `ControlBlockBracePlacement`: `SameLine` (default) attaches generated
  braces to the header; `NewLine` puts them on a separate line.
  `AttachUnlessMultiline` separates them when the header spans multiple
  lines (not merely because the brace was detached).
  `EnforceControlBlockBracePlacement` (default `false`) also corrects
  existing braces, including after header reflow. Applies
  to compound bodies of `if`, `else`, `for`, `while`, `do`, `switch`, `try`,
  and `catch`, but not unbraced statements or unrelated compound blocks.
  Existing same-line whitespace is preserved; attaching a detached brace
  uses one space. Comments and directives that cannot be moved safely stay
  in place.

### Blocks and statements

- `BlockInteriorSpacing`: `Preserve` (default), `Space`, or `NoSpace`
  immediately inside a nonempty same-line compound or lambda body
  (`{ statement; }` versus `{statement;}`). A complete braced call
  argument with a top-level statement semicolon is classified as a block,
  not an initializer; its multiline indentation remains source-owned. The
  rule does not change line breaks, record/namespace braces, or
  initializer-brace spacing.
- `EmptyBlockInteriorSpacing`: `NoSpace` (`{}`, default) or `Space` (`{ }`)
  selects spacing in newly generated empty blocks. Set
  `EnforceEmptyBlockInteriorSpacing` (default `false`) to normalize
  existing empty compound and lambda bodies. It is independent
  of the nonempty block setting.
- `IndentBlockBodies`: `false` (default) preserves statement indentation;
  `true` reindents statement starts and closing braces in complete compound
  and lambda blocks by `BlockBodyIndentWidth` relative to the header. It also
  indents parsed unbraced control bodies one level beyond the control header.
  Inline statements, continuations, comments, directives, and recovered
  bodies remain source-owned. Unbraced switch case labels and their following
  statements retain their original indentation because they have no
  compound body to anchor case indentation.
- `StandaloneBlockBraceIndentation`: `Indent` (default) aligns an anonymous
  scope's opening `{` with other statements at the same nesting level when
  `IndentBlockBodies = true`; `Preserve` leaves only that opening brace's
  existing indentation untouched. The scope body and closing `}` still
  follow their parsed nesting depth. Control, function, and case braces are
  unaffected.
- `NonEmptyBlockBodyLayout`: `Preserve` (default) leaves one-line
  compound bodies as written; `Multiline` separates complete parsed
  statements and their braces onto their own lines. It applies to
  functions and control blocks, not lambdas or empty bodies. Comments,
  directives, recovered syntax, and CRLF gaps stay source-owned. The
  YTsaurus profile uses `Multiline`; its empty functions still use `{ }`
  on a separate line.
- `CollapseEmptyFunctionBodies`: `false` (default) or `true` turns a complete
  function body containing only whitespace into `{ }`. It does not collapse
  bodies containing comments or directives, or CRLF bodies. Function brace
  placement still controls whether the opening brace is on its own line.
- `BlockClosingBrace`: `NewLine` (default) or `SameLine` (beside the last
  statement) selects the preferred position. `EnforceBlockClosingBrace`
  (default `false`) also corrects existing closers. Applies to parsed
  compound statements and lambda bodies, not namespace or type
  declarations. Empty bodies and unsafe gaps remain unchanged.
  For all closing-placement options, comments and directives adjacent to
  the closer and CRLF gaps remain source-owned. Unrecognized constructs
  other than the restricted typed-initializer closing gap stay unchanged.
- `StatementAttributeIndent`: `Preserve` (default) leaves standalone
  `[[...]]` statement attributes at their existing indentation, even when
  block body indentation is enabled. `Block` aligns them with the containing
  block's statement indent (or the control header for an unbraced body);
  `Indent` adds `StatementAttributeIndentWidth` to that indent. Only parsed, line-leading
  attributes in complete blocks and control bodies are affected. Inline
  attributes, comments, directives, and conditional branches stay unchanged.
- `StatementSemicolonSpacing`: `Preserve` (default) or `NoSpace` before
  the terminator of a complete parsed statement (`return x ;` becomes
  `return x;`). Loop and condition-init separators have separate options;
  declarations at file scope are not statement terminators.
- `DeclarationSemicolonSpacing`: `Preserve` (default), `Space`, or
  `NoSpace` *before* a parsed declaration's terminating semicolon, both
  at file scope and in declaration statements. If both it and
  `StatementSemicolonSpacing` are enabled, this setting takes precedence
  for declaration statements. It does not change the gap after `;` or
  the separators in control headers. Comments, directives, and
  multiline gaps remain unchanged.

### Initializers, new, and delete

- `BraceInitializerSpacing`: `Preserve` (default), `Space`, or `NoSpace`
  before a parsed braced initializer (`T x {1}` versus `T x{1}`). The gap
  following `=` remains controlled by `AssignmentOperatorSpacing`.
  The gap after `return`/`co_return` stays source-owned.
- `InitializerBracesSpacing`: `NoSpace` (default) or `Space` selects
  the edges of reflowed nonempty parsed braced initializers (`{ x, y }` versus
  `{x, y}`). `EnforceInitializerBracesSpacing` (default `false`) also
  normalizes existing same-line gaps.
  `EmptyBraceInitializerInteriorSpacing` controls `{ }` versus `{}`
  for empty parsed braced initializers. Neither setting
  affects block, namespace, record, or lambda braces.
- `InitializerListSplit`: `Preserve` (default), `Pack` (put as many braced
  initializer elements on a line as fit), or `OnePerLine` (one element per
  continuation line when a split is needed). Applies only to complete
  parsed braced initializer lists with single-line elements, comma
  separators, and whitespace-only gaps. Trailing commas are preserved;
  comments, directives, CRLF, and multiline elements stay source-owned.
  Lists that already fit on one line are not split. Nested lists are
  reparsed until stable, with a limit of 16 passes; deeper layouts remain
  source-owned rather than leaving a partially formatted result.
- `FirstInitializerElementPosition`: `BraceLine` (default) or `NewLine`
  determines where the first element starts when a list is split.
- `InitializerListIndent`: `IndentWidth` (default, one width beyond the
  list's line) or `Continuation` (align with the character after `{`).
- `InitializerClosingBrace`: `SameLine` (default, beside the last element
  unless a retained comma precedes it) or `NewLine` selects the position
  when reflowing a list. `EnforceInitializerClosingBrace` (default `false`)
  also applies that position without reflow, including with
  `InitializerListSplit = Preserve` when the gap before `}` is safe. For
  unknown typed temporary initializers, only the closing gap (or a safe
  trailing comma, if requested) is changed; their contents are not reflowed.
  A multiline final element retains its own closing-brace line.
- `InitializerTrailingComma`: `Keep` (default) leaves a trailing comma
  intact and puts `}` on its own line, even with
  `InitializerClosingBrace = SameLine`; safe parsed compact lists are split one
  element per line rather than leaving a comma immediately before the
  newly separated brace. `Remove` removes the trailing comma and joins
  `}` to the last element. Only applies to safe braced initializer gaps
  when `InitializerClosingBrace = SameLine`; comments and directives
  adjacent to the brace remain untouched.
- `MergeInitializerList`: `false` (default) leaves already split lists
  multiline; `true` joins safe lists that fit within `ColumnLimit` unless
  `InitializerClosingBrace = NewLine` is requested.
- `PlacementNewSpaceBeforeArguments`: `Preserve` (default), `Space`, or
  `NoSpace` between `new` and a parsed placement argument list:
  `new (buffer) T` versus `new(buffer) T`. Ordinary `new T(...)` and
  incomplete expressions remain unchanged. Multiline and commented
  gaps are preserved.
- `PlacementNewArgumentInteriorSpacing`: `Preserve` (default), `Space`,
  or `NoSpace` just inside nonempty parsed placement arguments:
  `new ( buffer ) T` versus `new (buffer) T`.
- `PlacementNewSpaceAfterArguments`: `Preserve` (default), `Space`, or
  `NoSpace` between a complete placement argument list and the new type:
  `new(buffer) T` versus `new(buffer)T`. The before-argument gap is
  controlled by `PlacementNewSpaceBeforeArguments`. Multiline and
  commented gaps, missing types, and missing operands are preserved.
- `NewArraySpaceBeforeBrackets` and `NewArrayInteriorSpacing`:
  `Preserve` (default), `Space`, or `NoSpace` before a parsed array
  bracket in a `new` type-id (`new T [4]` versus `new T[4]`) and just
  inside its nonempty brackets (`new T[ 4 ]` versus `new T[4]`),
  respectively. The parser treats `new T*[4]` as an array of pointers,
  not multiplication; the bracket gap follows this array setting. Nested
  template arguments and attributes are unaffected.
- `EmptyArrayDeclarationInteriorSpacing` and
  `EmptyNewArrayInteriorSpacing`: `Preserve` (default), `Space`, or
  `NoSpace` inside empty brackets in array declarations (`int a[ ]`
  versus `int a[]`) and `new` type-ids (`new T[ ]{1}` versus
  `new T[]{1}`), respectively. Nonempty brackets use their own rules.
- `NewInitializerSpaceBeforeParentheses`: `Preserve` (default), `Space`,
  or `NoSpace` between the type and parsed initializer parentheses in
  `new T (1)` versus `new T(1)`. Placement arguments and braced
  initializers have separate rules.
- `NewInitializerParenthesisInteriorSpacing`: `Preserve` (default),
  `Space`, or `NoSpace` just inside parsed `new` initializer parentheses,
  including both `new T( 1 )` versus `new T(1)` and `new T( )` versus
  `new T()`. Placement arguments and braced initializers are unaffected.
- `NewInitializerSpaceBeforeBraces`: `Preserve` (default), `Space`, or
  `NoSpace` between the type and a parsed `new` initializer `{`:
  `new T {1}` versus `new T{1}`.
- `NewInitializerBraceInteriorSpacing`: `Preserve` (default), `Space`,
  or `NoSpace` just inside parsed `new` braced initializers, both
  nonempty (`new T{ 1 }` versus `new T{1}`) and empty (`new T{ }`
  versus `new T{}`). The gap before `{` has a separate option.
- `DeleteArraySpaceBeforeBrackets`: `Preserve` (default), `Space`, or
  `NoSpace` between `delete` and the complete array marker in
  `delete [] p` versus `delete[] p`. Ordinary `delete p` is unchanged.
- `DeleteArraySpaceAfterBrackets`: `Preserve` (default), `Space`, or
  `NoSpace` between a complete `delete[]` marker and its operand:
  `delete[] p` versus `delete[]p`. The before-bracket gap is controlled
  by `DeleteArraySpaceBeforeBrackets`.
- `ConstructorInitializerList`: `Preserve` (default) or `Format` for
  complete constructor definitions. `Format` enables the same layout
  controls as `InheritanceList`, using `ConstructorInitializerList*`
  names. `ConstructorInitializerListColonPlacement` is
  `AttachToInitializer` (default) or `AttachToConstructor`;
  `ConstructorInitializerListStart` is `NewLine` (default),
  `ConstructorLine`, or `NewLineOnOverflow`.
  `ConstructorInitializerListSplit` is `OnePerLine` (default), `Pack`,
  or `OnePerLineOnOverflow`. Colon and comma spacing use independent
  `*SpaceBefore` and `*SpaceAfter` options (`NoSpace` or `Space`,
  defaulting to `NoSpace` before and `Space` after). Comma placement is
  `AttachToNext` (default) or `AttachToPrevious`.
  `ConstructorInitializerListIndent` is an optional positive integer up
  to 32, defaulting to `IndentWidth`; `ColumnLimit` controls packing and
  overflow. Only complete single-line initializer entries with
  whitespace-only separators are movable. Comments, directives,
  recovered syntax, and multiline entries remain source-owned.

### Expressions and operators

- `NamedCastSpaceBeforeTypeArguments`: `Preserve` (default), `Space`, or
  `NoSpace` between `static_cast`, `const_cast`, `reinterpret_cast`, or
  `dynamic_cast` and the `<` of a complete parsed cast
  (`static_cast <int>(x)` versus `static_cast<int>(x)`). Ambiguous or
  `>>`-terminated type arguments remain unchanged.
- `NamedCastSpaceBeforeOperandParentheses`: `Preserve` (default),
  `Space`, or `NoSpace` between the closing `>` and operand `(` of a
  complete parsed named cast (`static_cast<int> (x)` versus
  `static_cast<int>(x)`). Ambiguous `>>`-terminated type arguments
  remain source-owned.
- `ReturnSpaceBeforeParentheses`, `ThrowSpaceBeforeParentheses`, and
  `CoReturnSpaceBeforeParentheses`: `Preserve` (default), `Space`, or
  `NoSpace` between the keyword and a complete grouped operand:
  `return (x)` versus `return(x)`, similarly for `throw` and
  `co_return`. They do not change other expression operands.
- `MemberAccessOperatorsSpacing`: `Preserve` (default) or `NoSpace`
  around parsed `.` and `->` access (`obj . field` becomes `obj.field`)
  and unambiguous `.*` and `->*` tokens. `::` is independent.
- `ScopeResolutionSpacing`: `Preserve` (default) or `NoSpace` around parsed
  qualified `::` names (`std :: vector` becomes `std::vector`). Leading
  `::` keeps its left gap. Prefer `Preserve` when declaration-prefix macros
  may occur: without name lookup or macro expansion, `GTEST_API_ ::std`
  cannot reliably be distinguished from a qualified name.
- `NoexceptSpaceBeforeParentheses`, `AlignasSpaceBeforeParentheses`,
  `AlignofSpaceBeforeParentheses`, `SizeofSpaceBeforeParentheses`,
  `DecltypeSpaceBeforeParentheses`, `TypeidSpaceBeforeParentheses`,
  `RequiresSpaceBeforeParentheses`, and
  `StaticAssertSpaceBeforeParentheses`: `Preserve` (default), `Space`, or
  `NoSpace` between the respective keyword and an immediately following
  parsed `(`, for example `noexcept (x)` versus `noexcept(x)`.
  `noexcept` expressions and function specifiers share one setting;
  `requires` expressions and clauses share another. `alignas` applies
  to parsed record headers and standalone declarations. `sizeof...(Ts)`
  is unaffected. Other unparsed syntax, incomplete pairs, comments, and
  newlines are preserved.
- `SizeofPackSpaceBeforeParentheses`: `Preserve` (default), `Space`, or
  `NoSpace` between `sizeof...` and its parsed pack-argument `(` in
  `sizeof... (Ts)` versus `sizeof...(Ts)`. Ordinary `sizeof(x)` has
  its own setting. Multiline and commented gaps are preserved.
- `CoAwaitSpaceBeforeParentheses`: `Preserve` (default), `Space`, or
  `NoSpace` between `co_await` and a complete grouped operand:
  `co_await (expr)` versus `co_await(expr)`. Other operands are
  unaffected. Multiline and commented gaps are preserved.
- `OperatorOverloadOperatorSpacing`: `Preserve` (default), `NoSpace`,
  `Left`, `Right`, or `LeftAndRight` around the symbolic operator in a
  parsed overload name: `operator+()` versus `operator + ()`. `Left`
  controls the gap after `operator`, and `Right` the gap before the
  function parameters. Conversion operators (`operator int`) are
  untouched to avoid merging keywords.
- `CastArgumentSpacing`: `Preserve` (default), `Space`, or `NoSpace` just
  inside the parentheses of a complete parsed C-style or named cast:
  `( int )value` and `static_cast<int>( value )`. Empty, multiline, and
  commented gaps are unchanged. `CastSpacing` independently controls the
  space *after* a C-style cast.
- `GroupingInteriorSpacing`: `Preserve` (default), `Space`, or `NoSpace`
  immediately inside nonempty parsed expression grouping parentheses:
  `( a + b )` versus `(a + b)`. Calls, declarators, and casts use their
  own spacing rules.
- `UnaryOperatorSpacing` and `PostfixOperatorSpacing`: `Preserve`
  (default) or `NoSpace`. The former removes same-line whitespace between
  a parsed symbolic prefix operator and its operand (`! x` becomes `!x`);
  the latter removes space before a parsed postfix `++` or `--` (`x ++`
  becomes `x++`). Word operators (`sizeof`, `alignof`, `co_await`) retain
  their required whitespace. Gaps that could merge two operators remain
  untouched.
- `TernaryOperatorSpacing`: `Preserve` (default) or `Space` on both
  same-line sides of parsed `?` and `:` (`a?b:c` becomes `a ? b : c`).
  This is independent of `TernaryOperatorPlacement` (line breaks).
- `BinaryOperatorSpacing` and `AssignmentOperatorSpacing`: `Preserve`
  (default), `Space`, or `NoSpace` on both sides of parsed binary and
  assignment operators. Commas use `CommaSpacing` independently, even when
  that option is `Preserve`. Assignment initializers are included. Operators
  that could also be template delimiters (`<`, `>`, `<<`, `>>`) remain
  unchanged. An operator with no valid operand is not treated as binary;
  for example `char**` in an otherwise ambiguous call stays source-owned.
  `NoSpace` does not join two operator spellings if that could change
  tokenization. In a fold expression, `GroupingInteriorSpacing` owns the
  gap between the trailing `...` and `)`.
- `CastSpacing`: `Preserve` (default), `Space`, or `NoSpace` between the
  closing `)` and operand of a parsed C-style cast. Named casts and
  parenthesized operands of `sizeof`, `alignof`, `decltype`, `typeid`, and
  `noexcept` are not affected.
- `CommaSpacing`: `Space` (default, also spelled `After`) separates
  reflowed list elements; `NoSpace` joins them without a gap. Set
  `EnforceCommaSpacing` (default `false`) to also normalize existing
  same-line comma gaps (`f(a ,b)` becomes `f(a, b)` under `Space` or
  `f(a,b)` under `NoSpace`). A trailing comma before a closing delimiter does not receive
  an added space. Comments, line breaks, and recovered syntax are preserved.
- `SubscriptOperatorInteriorSpacing`, `ArrayDeclarationInteriorSpacing`,
  and `StructuredBindingBracketSpacing`: `Preserve` (default), `Space`, or
  `NoSpace` just inside nonempty parsed brackets of their respective
  constructs: `a[ i ]`, `int a[ 4 ]`, and `auto [ x, y ]`. Lambda captures
  and attributes have separate rules or remain untouched.
- `SubscriptOperatorSpacing` and `ArrayDeclarationSpacing`: `Preserve`
  (default), `Space`, or `NoSpace` before the respective opening bracket:
  `a [i]` versus `a[i]`, and `int a [4]` versus `int a[4]`. These settings
  are independent of their interior-spacing counterparts.
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
  separately by `CallArgumentSplit`. New continuations normally use
  `IndentWidth` beyond the original line's leading spaces; parsed control
  conditions can instead use `ControlConditionIndent`, and parsed calls
  can use `CallArgumentIndent`. Comments,
  directives, CRLF, backslashes, and ambiguous or recovered syntax stay
  source-owned. Bare `<`, `>`, `<<`, and `>>` remain untouched even when
  parsed as operators: without name lookup they can be template syntax.
- `BinaryOperatorPlacement`: `Trailing` (default, keep a wrapped binary
  operator at the end of the previous line) or `Leading` (put it at the
  start of the continuation line). Only applies when
  `ExpressionLayoutCondition` is not `Preserve`; assignment operators use
  `AssignmentOperatorPlacement` and conditional `?`/`:` have their own
  setting. `Overflow` keeps existing operator breaks but safely moves the
  newline to the configured side of a binary operator, even if the
  expression otherwise fits on one line.
- `AssignmentOperatorPlacement`: `Leading` (default, put a wrapped
  assignment operator at the start of the continuation line) or `Trailing`
  (keep it at the end of the preceding line). Like binary placement, this
  only applies to parsed expressions when expression layout is enabled;
  `Overflow` also corrects safe existing assignment breaks without
  regrouping the expression.
- `TernaryOperatorPlacement`: `Leading` (default) puts newly created
  `?` and `:` breaks at the start of continuation lines; `Trailing`
  puts them at the end of the preceding line. Existing breaks retain
  their side unless `EnforceTernaryOperatorPlacement` is `true` (default
  `false`). Enforcement also corrects safe existing breaks in `Overflow`
  mode, independently of `TernarySplit`.
- `TernarySplit`: `Preserve` (default) neither adds nor joins ternary
  breaks. `AsNeeded` allows either delimiter or both to break. `ThreeLines`
  keeps a flat ternary flat when it fits; if the ternary is split, it
  separates the condition, true branch, and false branch by breaking at
  both delimiters. Safe overlong ternaries are considered before call
  argument reflow so a single argument does not hide the ternary break.
  Safe leading `ThreeLines` breaks align `?` and `:` at
  least one indent level past the condition's line, including when a
  trailing-operator break is moved to the leading side. These settings
  apply only when `ExpressionLayoutCondition` is not `Preserve`.
  Comments, directives, CRLF, and ambiguous or incomplete
  syntax stay source-owned. In `--check` mode, expression reflow is
  skipped; safe call and parameter reflow still produces suggested fixes.
  Unresolved layout checks report overlong lines, incorrect existing
  operator placement, and incomplete `ThreeLines` splits.

### Naming checks

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

### Path exclusions

- `Exclude`: ECMAScript regular expressions searched against paths relative
  to the config directory. A matching directory and its contents are skipped
  during recursive processing. An explicit file input is skipped if it
  matches. `--no-config` disables these exclusions; repeatable CLI
  `--exclude` patterns apply relative to each input directory instead.
  Backslashes in patterns must be doubled inside quoted config strings.


Other former formatting settings are rejected rather than silently ignored.

## YTsaurus-style example and limits

[`examples/ytsaurus.hit-fmt`](examples/ytsaurus.hit-fmt) approximates the
[YTsaurus C++ style guide](https://github.com/ytsaurus/ytsaurus/blob/main/yt/styleguide/cpp.md)
for `yt/yt`, `yt/chyt`, `yt/yql`, and `library/cpp/yt`. Use it with
`--config hit-fmt/examples/ytsaurus.hit-fmt` or copy it to `.hit-fmt`
only for those trees. It is not suitable for `yt/cpp`, `util`, or the rest
of `library/cpp`. Four spaces, 120 columns (an approximation inferred from
code, not a specified guide limit), next-line class/struct/function braces,
attached namespaces/lambdas, indented case labels, multiline-control
braces, one-per-line arguments and bases, block-body indentation, required
control braces, collapsed empty function bodies, and supported token spacing
are configured. Only explicit YT fluent-call roots may be reflowed when
those chains themselves overflow; unrelated library calls are left alone.
YT logging calls retain the first string argument on the call line.
`PreprocessorIndentation = Preserve` keeps both indented
conditional includes and unindented inl-header guards. Parameter names
are required in declarations; definitions require a name or trailing
`/*name*/` comment. General name checks remain off until scope-aware
rules can distinguish local camelCase names, global PascalCase names and
trailing-underscore fields. Include grouping
is intentionally out of scope; `IncludeOrder = Preserve` leaves groups alone.

This is not a replacement for a YTsaurus style review. Missing options or
insufficient support include:

- `#pragma once` and separator lines of 80 `/` characters around
  namespaces are not generated or checked.
- Full semantic classification and structural extra-indentation for
  fluent chains, general continuation indentation, blank-line policies,
  comment prose, and complete column-limit wrapping. Macro-defined
  declarations and
  statements are only partially parsed. Log and error message wording,
  API practices, integer types, and other semantic rules need review.
- Scope-aware names (local variables vs globals, fields with `_`, enum
  values, and macro-defined enums), acronym exceptions, and whether a
  parameter is unused. General name checks are disabled in this profile;
  parameter-name checks report missing names but cannot determine whether
  an unused named parameter should be commented out. Namespace
  comments can be added, but their surrounding separator lines cannot.

On upstream `yt/yt/core/misc/error.cpp`, lambda bodies inside call
arguments retain their indentation with block-body reindentation enabled.
On `misc/fs.cpp`, the unknown header type in `(PULARGE_INTEGER) &result`
is ambiguous with binary `&`; the formatter preserves that gap instead of
inserting a space after `&`. The same applies to ambiguous `*`. Known
casts and unambiguous binary operators retain their own spacing. Existing
conditional includes and inl-header `#ifndef` guards are preserved: the
available `PreprocessorIndented` option cannot distinguish their
indentation policies. A 137-column function declaration in
`misc/error.cpp` is split, but column limits are not enforced on every
expression or comment.

Safe chains rooted at configured calls may be split; fluent variables and
chains with multiline arguments remain source-owned. Braced initializer
splitting remains disabled in this example to avoid reshaping nested
initializers; the closing `}` policy still runs independently on safe
initializer lists. With a trailing comma, the brace stays on its own line.
The YT guide does not prescribe spaces inside initializer braces, and both
layouts occur in source. `EnforceInitializerBracesSpacing = false` avoids bulk
whitespace changes.
`CallArgumentSplit = OnePerLine` only handles safe parsed calls with
single-line arguments. Overlong safe ternaries split at both leading
`?` and `:` operators; comments and ambiguous expressions stay source-owned.
Keep initializer reflow off for nested braced aggregates and macro arguments.
Check diffs before applying `--fix` to unfamiliar code.

## Google-style example and limits

[`examples/google.hit-fmt`](examples/google.hit-fmt) approximates Google's
C++ style (and clang-format's Google preset). Use it with `--config` or copy
it to `.hit-fmt`. It selects two-space members and block bodies, one-space
access-label indentation, 80 columns, attached braces, two-space newly
added namespace comments, and supported token spacing. It is not a replacement for
`clang-format -style=Google` or a Google C++ style review.

Still missing or insufficient for a full Google profile:

- Include grouping (related header first, C/system/other libraries and
  project headers), blank lines between groups, and sorting inside each
  group; `IncludeOrder = Sort` cannot represent these categories. Header
  guards and forward declarations are not enforced. Existing
  leading/trailing comment columns and prose are not aligned or reflowed.
- General continuation indentation, return-type and template-declaration
  breaks, short-function policies, existing whitespace in unparsed code,
  and context-dependent constructor/base-list layout cannot be fully
  expressed. `ColumnLimit` does not guarantee wrapping of arbitrary lines.
  For example, `gmock_main.cc:57` in GoogleTest contains a 143-column URL
  comment which this profile reports but cannot fix.
- Naming rules for fields with trailing `_`, constants with leading `k`,
  enum values, and exceptions for tests/macros cannot be represented by
  one case and prefix policy per declaration kind. Google conventions for
  headers, documentation, API design, and language usage need review.

Corpus check: tested against the Ubuntu-packaged GoogleTest/GoogleMock
(`/usr/src/googletest`, GoogleTest 1.12.1). The inline member bodies in
`gmock-cardinalities.cc`, the macro-annotated record header and the
`static_assert` comma in `gtest-port.h` now remain intact with active block,
brace-initializer and binary-operator settings. `NamespaceCommentSpaces = 2`
adds Google's two-space comment gap; `NamespaceComments = AddMissing` leaves
existing anonymous-namespace comments such as `// Unnamed namespace` intact.
Member-pointer parameter declarators in templated return-type functions,
`char**` inside type-taking macros, and braced macro arguments containing
statements now retain their correct Google spacing. `MemberAccessOperatorsSpacing = NoSpace` tightens `.` and `->`, while
`ScopeResolutionSpacing = Preserve` avoids joining
`GTEST_API_ ::std::string` into `GTEST_API_::std::string` without
macro expansion or name lookup.
`TernaryOperatorSpacing = Space` can collapse multiline alignment without
realigning other branches, so the profile preserves it. Four-space newline
function parameter indentation is selected explicitly, and unsafe nested
initializer-list reflow is disabled.

## LLVM-style example and limits

[`examples/llvm.hit-fmt`](examples/llvm.hit-fmt) is an approximate LLVM C++
profile. Copy it to `.hit-fmt` or pass `--config hit-fmt/examples/llvm.hit-fmt`.
It selects LLVM's 2-space indent and 80-column limit, attached braces,
spacing for supported constructs, packed parameters and call arguments,
trailing binary and assignment operators, leading ternary operators,
call-expression continuation and statement-attribute indentation,
LLVM-style pointer and reference placement, and basic naming checks.
This is **not** a
replacement for `clang-format -style=LLVM` or a
complete implementation of the [LLVM Coding
Standards](https://llvm.org/docs/CodingStandards.html).
In particular, `ColumnLimit` does not guarantee that every line is wrapped,
and naming checks only report violations in the declarations they recognize.

LLVM rules not fully expressible or enforced by this profile include:

- Include categories and ordering (main module header first, then local,
  project, and system headers), required/minimal includes, and header guards.
  `IncludeOrder = Sort` only sorts safe contiguous literal include runs by
  path, with quoted headers before angled headers; it cannot encode LLVM's
  categories. The example preserves includes instead.
- LLVM's context-sensitive inheritance and constructor-initializer layout,
  return-type, template, and `requires` breaks; alignment of other
  continuations, operands, trailing comments, and escaped newlines. The
  example preserves inheritance lists rather than rewriting their layout.
- Full layout of unsafe or multiline initializer elements, short functions or
  lambdas, and empty lines. Specific initializer and label-colon gaps,
  pointer/reference declarators, directive prefixes, and generated
  indentation are configurable; these do not amount to full-file
  formatting. Existing continuation indentation, multiline directives,
  and comment prose (including Doxygen text and file headers) remain
  source-owned.
  Expression and argument wrapping applies only to safe parsed constructs.
- Contextual naming rules: enumerators and public members, enum `Kind`
  suffixes and enumerator prefixes, STL-like method exceptions, and the
  distinction between declarations and definitions. The example's case
  checks can flag legitimate exceptions and skip ambiguous declarations;
  they cannot check whether names are descriptive nouns or verbs.
- Language, library, documentation, and API-design guidance in the LLVM
  Coding Standards. Those rules require review beyond a formatter.
