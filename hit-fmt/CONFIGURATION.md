# Configuration

The formatter preserves source-owned token and trivia bytes except where an
explicitly configured declaration brace, lambda capture-to-parameter
spacing, lambda capture spacing, function parameter or call spacing,
control keyword spacing or brace placement, token spacing, generated
indentation, preprocessor indentation, trailing comment spacing,
namespace comment, include ordering, or function parameter rule applies.
All formatting rules default to preserving the source.
Naming rules default to unchecked and never rewrite identifiers; `--check`
reports violations with file, line and column and returns a nonzero status.

`hit-fmt` searches for `.hit-fmt` beside each source file and then in its
parent directories. The nearest file wins. `--config <file>` selects one
file for every input; `--no-config` disables configuration lookup.

Blank lines and `#` comments are allowed. Arrays of quoted strings may
span lines, with comments between items; each quoted item must stay on one
line. Example:

```text
IndentWidth = 4
ColumnLimit = 120
IndentationKind = Preserve
PreprocessorIndentation = Preserve
TrailingCommentSpacing = Preserve
NamedCastSpaceBeforeOperandParentheses = Preserve
ReturnSpaceBeforeParentheses = Preserve
ThrowSpaceBeforeParentheses = Preserve
CoReturnSpaceBeforeParentheses = Preserve
FunctionSuffixSpecifierSpacing = Preserve
RecordBodyInteriorSpacing = Preserve
NamespaceBodyInteriorSpacing = Preserve
EnumBodyInteriorSpacing = Preserve
RequirementBodyInteriorSpacing = Preserve
TrailingWhitespace = Preserve
TemplateKeywordSpacing = Preserve
TemplateTypeArgumentInteriorSpacing = Preserve
TemplateTypeArgumentSpacing = Preserve
EmptyBraceInitializerInteriorSpacing = Preserve
OperatorOverloadOperatorSpacing = Preserve
AttributeInteriorSpacing = Preserve
AttributeSpaceAfterSpecifier = Preserve
NamedCastSpaceBeforeTypeArguments = Preserve
PlacementNewSpaceBeforeArguments = Preserve
AttributeSpaceBeforeSpecifier = Preserve
NewInitializerSpaceBeforeParentheses = Preserve
DeleteArraySpaceBeforeBrackets = Preserve
SizeofPackSpaceBeforeParentheses = Preserve
NewInitializerSpaceBeforeBraces = Preserve
NewInitializerParenthesisInteriorSpacing = Preserve
DeleteArraySpaceAfterBrackets = Preserve
PlacementNewSpaceAfterArguments = Preserve
NewArraySpaceBeforeBrackets = Preserve
NewArrayInteriorSpacing = Preserve
EmptyAttributeInteriorSpacing = Preserve
CoAwaitSpaceBeforeParentheses = Preserve
NewInitializerBraceInteriorSpacing = Preserve
PlacementNewArgumentInteriorSpacing = Preserve
EmptyArrayDeclarationInteriorSpacing = Preserve
EmptyNewArrayInteriorSpacing = Preserve
NoexceptSpaceBeforeSpecifier = Preserve
FunctionQualifierSpacing = Preserve
FunctionRefQualifierSpacing = Preserve
NestedDeclaratorInteriorSpacing = Preserve
NestedDeclaratorPointerSpacing = Preserve
MemberPointerDeclaratorSpacing = Preserve
NoexceptSpaceBeforeParentheses = Preserve
AlignasSpaceBeforeParentheses = Preserve
AlignofSpaceBeforeParentheses = Preserve
SizeofSpaceBeforeParentheses = Preserve
DecltypeSpaceBeforeParentheses = Preserve
TypeidSpaceBeforeParentheses = Preserve
RequiresSpaceBeforeParentheses = Preserve
StaticAssertSpaceBeforeParentheses = Preserve
StatementSemicolonSpacing = Preserve
MemberAccessOperatorsSpacing = Preserve
InitializerBracesSpacing = Preserve
CastArgumentSpacing = Preserve
GroupingInteriorSpacing = Preserve
ControlBlockConditionSpacing = Preserve
AccessModifierColonSpacing = Preserve
RangeBasedForColonSpacing = Preserve
BlockInteriorSpacing = Preserve
EmptyBlockInteriorSpacing = Preserve
ControlBlockSpaceBeforeBrace = Preserve
DeclarationSemicolonSpacing = Preserve
BitFieldColonSpacing = Preserve
EnumUnderlyingTypeColonSpacing = Preserve
LabelColonSpacing = Preserve
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
PointerReferenceLocation = Preserve
UnaryOperatorSpacing = Preserve
PostfixOperatorSpacing = Preserve
TernaryOperatorSpacing = Preserve
BinaryOperatorSpacing = Preserve
AssignmentOperatorSpacing = Preserve
CastSpacing = Preserve
CommaSpacing = Preserve
LoopSemicolonSpacing = Preserve
ConditionInitSemicolonSpacing = Preserve
SubscriptOperatorInteriorSpacing = Preserve
SubscriptOperatorSpacing = Preserve
ArrayDeclarationInteriorSpacing = Preserve
ArrayDeclarationSpacing = Preserve
BraceInitializerSpacing = Preserve
ReturnArrowSpacing = Preserve
EmptyFunctionCallArgumentsSpacing = Preserve
EmptyFunctionParametersSpacing = Preserve
EmptyLambdaParametersSpacing = Preserve
EmptyLambdaCaptureSpacing = Preserve
StructuredBindingBracketSpacing = Preserve
ExpressionLayoutCondition = Preserve
ControlConditionIndent = IndentWidth
IncludeOrder = Preserve
IndentAccessModifiers = Preserve
IndentMembers = Preserve
IndentBlockBodies = false
IndentCaseLabels = Preserve
StatementAttributeIndent = Preserve
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
ShortNamespaceLines = 0
FunctionParameterSplit = Pack
FirstFunctionParameterPosition = NameLine
EnforceFirstFunctionParameterPosition = false
FunctionParameterIndent = IndentWidth
# FunctionParameterNewLineIndent = 4 overrides indentation when first is new
MergeFunctionParameters = false
InitializerListSplit = Preserve
FirstInitializerElementPosition = BraceLine
InitializerListIndent = IndentWidth
InitializerClosingBrace = Preserve
MergeInitializerList = false
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
Exclude = [
    "(^|/)generated(/|$)",
    "(^|/)legacy\\.cpp$"
]
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
- `IndentationKind`: `Preserve` (default), `Spaces`, or `Tabs` for
  formatter-generated indentation. `Preserve` and `Spaces` retain the
  previous space-based output; `Tabs` emits tabs at `IndentWidth`-column
  stops, then spaces for any remainder. Existing indentation and comment
  text are not converted. This also affects indentation generated by
  `PreprocessorIndentation = PreprocessorIndented`.
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
- `FunctionSuffixSpecifierSpacing`: `Preserve` (default), `Space`, or
  `NoSpace` before parsed function `override`/`final` and lambda
  `mutable`/`constexpr`/`consteval`/`static` suffixes. For example,
  `f() override` versus `f()override` and `[]() mutable` versus
  `[]()mutable`. Gaps between two words cannot be removed when doing
  so would merge their tokens.
- `RecordBodyInteriorSpacing`, `NamespaceBodyInteriorSpacing`, and
  `EnumBodyInteriorSpacing`: `Preserve` (default), `Space`, or `NoSpace`
  just inside their respective parsed braces on one line, including
  empty bodies: `struct S{ int x; }` versus `struct S{int x;}` and
  `namespace N{ }` versus `namespace N{}`. Class, struct, and union
  bodies share the record option; brace placement is separate.
- `RequirementBodyInteriorSpacing`: `Preserve` (default), `Space`, or
  `NoSpace` just inside a parsed requires-expression body, including
  empty bodies: `requires(T x){ x; }` versus `requires(T x){x;}`.
  All these rules preserve newlines, comments, and recovered syntax.
- `TrailingWhitespace`: `Preserve` (default) leaves line endings intact;
  `Trim` removes trailing spaces and tabs before LF, CRLF, or end of file.
  Whitespace inside tokens (notably raw strings and preprocessor directives)
  and lines ending in a backslash are preserved to avoid changing a line
  splice.
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
- `StatementSemicolonSpacing`: `Preserve` (default) or `NoSpace` before
  the terminator of a complete parsed statement (`return x ;` becomes
  `return x;`). Loop and condition-init separators have separate options;
  declarations at file scope are not statement terminators.
- `MemberAccessOperatorsSpacing`: `Preserve` (default) or `NoSpace`
  around parsed `.` and `->` access (`obj . field` becomes `obj.field`),
  qualified `::` names (`std :: vector` becomes `std::vector`), and the
  unambiguous `.*` and `->*` tokens. Leading `::` keeps its left gap.
- `AttributeSpaceAfterSpecifier`: `Preserve` (default), `Space`, or
  `NoSpace` between a parsed `[[...]]` specifier and a following
  declaration word (`[[nodiscard]] int f()` versus `[[nodiscard]]int f()`).
  This is independent of `AttributeInteriorSpacing`.
- `NamedCastSpaceBeforeTypeArguments`: `Preserve` (default), `Space`, or
  `NoSpace` between `static_cast`, `const_cast`, `reinterpret_cast`, or
  `dynamic_cast` and the `<` of a complete parsed cast
  (`static_cast <int>(x)` versus `static_cast<int>(x)`). Ambiguous or
  `>>`-terminated type arguments remain unchanged.
- `PlacementNewSpaceBeforeArguments`: `Preserve` (default), `Space`, or
  `NoSpace` between `new` and a parsed placement argument list:
  `new (buffer) T` versus `new(buffer) T`. Ordinary `new T(...)` and
  incomplete expressions remain unchanged. All these options preserve
  multiline and commented gaps.
- `AttributeSpaceBeforeSpecifier`: `Preserve` (default), `Space`, or
  `NoSpace` before a parsed `[[...]]` specifier following a declaration
  token (`int [[maybe_unused]] x` versus `int[[maybe_unused]] x`).
  Leading attributes have no preceding declaration gap.
- `NewInitializerSpaceBeforeParentheses`: `Preserve` (default), `Space`,
  or `NoSpace` between the type and parsed initializer parentheses in
  `new T (1)` versus `new T(1)`. Placement arguments and braced
  initializers have separate rules.
- `DeleteArraySpaceBeforeBrackets`: `Preserve` (default), `Space`, or
  `NoSpace` between `delete` and the complete array marker in
  `delete [] p` versus `delete[] p`. Ordinary `delete p` is unchanged.
- `SizeofPackSpaceBeforeParentheses`: `Preserve` (default), `Space`, or
  `NoSpace` between `sizeof...` and its parsed pack-argument `(` in
  `sizeof... (Ts)` versus `sizeof...(Ts)`. Ordinary `sizeof(x)` has
  its own setting. All four rules preserve multiline and commented gaps.
- `NewInitializerSpaceBeforeBraces`: `Preserve` (default), `Space`, or
  `NoSpace` between the type and a parsed `new` initializer `{`:
  `new T {1}` versus `new T{1}`.
- `NewInitializerParenthesisInteriorSpacing`: `Preserve` (default),
  `Space`, or `NoSpace` just inside parsed `new` initializer parentheses,
  including both `new T( 1 )` versus `new T(1)` and `new T( )` versus
  `new T()`. Placement arguments and braced initializers are unaffected.
- `DeleteArraySpaceAfterBrackets`: `Preserve` (default), `Space`, or
  `NoSpace` between a complete `delete[]` marker and its operand:
  `delete[] p` versus `delete[]p`. The before-bracket gap is controlled
  by `DeleteArraySpaceBeforeBrackets`.
- `PlacementNewSpaceAfterArguments`: `Preserve` (default), `Space`, or
  `NoSpace` between a complete placement argument list and the new type:
  `new(buffer) T` versus `new(buffer)T`. The before-argument gap is
  controlled by `PlacementNewSpaceBeforeArguments`. Multiline and
  commented gaps, missing types, and missing operands are preserved.
- `NewArraySpaceBeforeBrackets` and `NewArrayInteriorSpacing`:
  `Preserve` (default), `Space`, or `NoSpace` before a parsed array
  bracket in a `new` type-id (`new T [4]` versus `new T[4]`) and just
  inside its nonempty brackets (`new T[ 4 ]` versus `new T[4]`),
  respectively. Nested template arguments and attributes are unaffected.
- `EmptyAttributeInteriorSpacing`: `Preserve` (default), `Space`, or
  `NoSpace` inside an empty parsed attribute list (`[[ ]]` versus
  `[[]]`). `AttributeInteriorSpacing` controls nonempty lists.
- `CoAwaitSpaceBeforeParentheses`: `Preserve` (default), `Space`, or
  `NoSpace` between `co_await` and a complete grouped operand:
  `co_await (expr)` versus `co_await(expr)`. Other operands are
  unaffected. These rules preserve multiline and commented gaps.
- `NewInitializerBraceInteriorSpacing`: `Preserve` (default), `Space`,
  or `NoSpace` just inside parsed `new` braced initializers, both
  nonempty (`new T{ 1 }` versus `new T{1}`) and empty (`new T{ }`
  versus `new T{}`). The gap before `{` has a separate option.
- `PlacementNewArgumentInteriorSpacing`: `Preserve` (default), `Space`,
  or `NoSpace` just inside nonempty parsed placement arguments:
  `new ( buffer ) T` versus `new (buffer) T`.
- `EmptyArrayDeclarationInteriorSpacing` and
  `EmptyNewArrayInteriorSpacing`: `Preserve` (default), `Space`, or
  `NoSpace` inside empty brackets in array declarations (`int a[ ]`
  versus `int a[]`) and `new` type-ids (`new T[ ]{1}` versus
  `new T[]{1}`), respectively. Nonempty brackets use their own rules.
- `NoexceptSpaceBeforeSpecifier`: `Preserve` (default), `Space`, or
  `NoSpace` before a parsed function or lambda exception specifier:
  `void f() noexcept` versus `void f()noexcept`. The gap after
  `noexcept` before its optional `(` uses
  `NoexceptSpaceBeforeParentheses`. Multiline and commented gaps are
  preserved by all five options.
- `InitializerBracesSpacing`: `Preserve` (default), `Space`, or `NoSpace`
  just inside nonempty parsed braced initializers (`{ x, y }` versus
  `{x, y}`). `EmptyBraceInitializerInteriorSpacing` controls `{ }`
  versus `{}` for empty parsed braced initializers. Neither setting
  affects block, namespace, record, or lambda braces.
- `OperatorOverloadOperatorSpacing`: `Preserve` (default), `NoSpace`,
  `Left`, `Right`, or `LeftAndRight` around the symbolic operator in a
  parsed overload name: `operator+()` versus `operator + ()`. `Left`
  controls the gap after `operator`, and `Right` the gap before the
  function parameters. Conversion operators (`operator int`) are
  untouched to avoid merging keywords.
- `AttributeInteriorSpacing`: `Preserve` (default), `Space`, or
  `NoSpace` just inside nonempty parsed C++ attributes:
  `[[ nodiscard ]]` versus `[[nodiscard]]`. The two bracket pairs are
  kept intact. Multiline and commented gaps are preserved.
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
- `CastArgumentSpacing`: `Preserve` (default), `Space`, or `NoSpace` just
  inside the parentheses of a complete parsed C-style or named cast:
  `( int )value` and `static_cast<int>( value )`. Empty, multiline, and
  commented gaps are unchanged. `CastSpacing` independently controls the
  space *after* a C-style cast.
- `GroupingInteriorSpacing`: `Preserve` (default), `Space`, or `NoSpace`
  immediately inside nonempty parsed expression grouping parentheses:
  `( a + b )` versus `(a + b)`. Calls, declarators, and casts use their
  own spacing rules.
- `ControlBlockConditionSpacing`: `Preserve` (default), `Space`, or
  `NoSpace` just inside complete `if`, `for`, `while`, `switch`, and `catch`
  condition parentheses (including `do`/`while`). The separate
  `ControlBlockKeywordSpacing` controls the gap *before* `(`.
- `AccessModifierColonSpacing` and `RangeBasedForColonSpacing`:
  `Preserve` (default), `Space`, or `NoSpace` on both same-line sides of
  parsed access-label colons (`public : int x;` versus `public:int x;`)
  and range-for separators (`for (auto x : items)` versus
  `for (auto x:items)`), respectively. Other colons are unaffected.
  The parenthesis rules skip empty pairs; all four settings leave
  multiline, commented, or incomplete gaps source-owned.
- `BlockInteriorSpacing`: `Preserve` (default), `Space`, or `NoSpace`
  immediately inside a nonempty same-line compound or lambda body
  (`{ statement; }` versus `{statement;}`). It does not change line
  breaks, record/namespace braces, or initializer-brace spacing.
- `EmptyBlockInteriorSpacing`: `Preserve` (default), `Space` (`{ }`), or
  `NoSpace` (`{}`) for empty compound and lambda bodies. It is independent
  of the nonempty block setting.
- `ControlBlockSpaceBeforeBrace`: `Preserve` (default), `Space`, or
  `NoSpace` between a control header (`if`, `else`, `for`, `while`, `do`,
  `switch`, `try`, `catch`) and a same-line opening brace. Brace placement
  runs first; a brace left on a separate line stays on that line.
- `DeclarationSemicolonSpacing`: `Preserve` (default), `Space`, or
  `NoSpace` *before* a parsed declaration's terminating semicolon, both
  at file scope and in declaration statements. If both it and
  `StatementSemicolonSpacing` are enabled, this setting takes precedence
  for declaration statements. It does not change the gap after `;` or
  the separators in control headers. All four options leave comments,
  directives, and multiline gaps unchanged.
- `BitFieldColonSpacing`, `EnumUnderlyingTypeColonSpacing`, and
  `LabelColonSpacing`: `Preserve` (default), `NoSpace` (`x:1`), `Left`
  (`x :1`), `Right` (`x: 1`), or `LeftAndRight` (`x : 1`). The options
  apply only to their parsed bit-field, enum underlying-type, and label
  colons, respectively; `LabelColonSpacing` covers `case` and `default`
  labels as well. Line breaks, comments, and directives are untouched.
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
- `PointerReferenceLocation`: `Preserve` (default), `Left` (`int* p`),
  `Middle` (`int * p`), or `Right` (`int *p`). Applies to complete parsed
  pointer and reference declarators, including adjacent `*`/`&` tokens.
  Member pointers and nested declarators have independent rules below;
  commas between declarators are not treated as type names. It does not
  alter multiplication, address-of, or dereference expressions.
- `FunctionQualifierSpacing`: `Preserve` (default), `Space`, or `NoSpace`
  between a function's closing parameter `)` and an immediately following
  `const` or `volatile` (`f() const` versus `f()const`).
- `FunctionRefQualifierSpacing`: `Preserve` (default), `Space`, or
  `NoSpace` before a parsed trailing `&` or `&&` after the parameters
  or a `const`/`volatile` qualifier (`f() const &` versus `f() const&`).
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
  assignment operators. Assignment initializers are included. Operators
  that could also be template delimiters (`<`, `>`, `<<`, `>>`) remain
  unchanged. `NoSpace` does not join two operator spellings if that could
  change tokenization.
- `CastSpacing`: `Preserve` (default), `Space`, or `NoSpace` between the
  closing `)` and operand of a parsed C-style cast. Named casts are not
  affected.
- `CommaSpacing`: `Preserve` (default) leaves list commas unchanged;
  `After` removes same-line space before a parsed list comma and uses one
  space after it (`f(a ,b)` becomes `f(a, b)`); `NoSpace` removes same-line
  spaces on both sides (`f(a , b)` becomes `f(a,b)`). A trailing comma
  before a closing delimiter does not receive an added space.
- `LoopSemicolonSpacing` and `ConditionInitSemicolonSpacing`: `Preserve`
  (default), `Space`, or `NoSpace`. The first controls the two clause
  separators of a parsed `for` header; the second controls the single
  top-level init-statement separator in a complete `if` or `switch`
  condition. When enabled, remove same-line space before the semicolon
  and use the selected spacing after it; no space is added before `)` or
  another semicolon. Statement and nested-expression semicolons are not
  changed.
- `SubscriptOperatorInteriorSpacing`, `ArrayDeclarationInteriorSpacing`,
  and `StructuredBindingBracketSpacing`: `Preserve` (default), `Space`, or
  `NoSpace` just inside nonempty parsed brackets of their respective
  constructs: `a[ i ]`, `int a[ 4 ]`, and `auto [ x, y ]`. Lambda captures
  and attributes have separate rules or remain untouched.
- `SubscriptOperatorSpacing` and `ArrayDeclarationSpacing`: `Preserve`
  (default), `Space`, or `NoSpace` before the respective opening bracket:
  `a [i]` versus `a[i]`, and `int a [4]` versus `int a[4]`. These settings
  are independent of their interior-spacing counterparts.
- `BraceInitializerSpacing`: `Preserve` (default), `Space`, or `NoSpace`
  before a parsed braced initializer (`T x {1}` versus `T x{1}`). The gap
  following `=` remains controlled by `AssignmentOperatorSpacing`.
- `ReturnArrowSpacing`: `Preserve` (default), `NoSpace` (`f()->T`), `Left`
  (`f() ->T`), `Right` (`f()-> T`), or `LeftAndRight` (`f() -> T`) around
  the arrow of a parsed trailing return type. Member-access `->` is
  unaffected.
- `EmptyFunctionCallArgumentsSpacing`, `EmptyFunctionParametersSpacing`,
  `EmptyLambdaParametersSpacing`, and `EmptyLambdaCaptureSpacing`:
  `Preserve` (default), `Space`, or `NoSpace` inside their respective
  empty, parsed pairs: `f( )`, `void f( )`, `[]( )`, and `[ ]` versus
  `f()`, `void f()`, `[]()`, and `[]`. Nonempty interiors have separate
  options. All token-spacing rules leave newlines, comments, directives,
  and incomplete statements unchanged.
- `*BracePlacement`: `Preserve` (default) keeps the existing layout,
  `Attach` joins a brace to its header, and `NewLine` moves it to its own
  line. Existing same-line spacing is left unchanged by `Attach`. Each of
  class, struct, union, function definition, lambda, and
  namespace is configured independently. `Attach` places the brace before
  a trailing header comment (`//` or `/* ... */`). When `NewLine` splits an
  inline brace followed by a trailing comment, the comment stays with the
  declaration header and the brace goes below it. Other intervening comments
  and directives block attachment. Other braces are unaffected.
- `ShortNamespaceLines`: nonnegative integer up to 10000. With
  `NamespaceComments = Required`, skip adding a closing comment when the
  namespace has at most this many lines between its opening and closing
  braces. `0` (default) disables the exemption. Existing closing comments
  are retained and corrected even on short namespaces; `Remove` still
  removes them.
- `NamespaceComments`: `Preserve` (default) leaves closing comments alone,
  `Remove` removes a line comment immediately after a namespace's closing
  brace, and `Required` adds `// namespace name` (or `// namespace` for an
  anonymous namespace) and corrects an existing trailing line comment.
  Unmatched braces and closings followed by a block comment are left alone.
- `IndentBlockBodies`: `false` (default) preserves statement indentation;
  `true` reindents statement starts and closing braces in complete compound
  and lambda blocks by one `IndentWidth` relative to the header. It also
  indents parsed unbraced control bodies one level beyond the control header.
  Inline statements, continuations, comments, directives, and recovered
  bodies remain source-owned.
- `IndentCaseLabels`: `Preserve` (default), `NoIndent` (align labels with
  their switch header), or `Indent` (one level inside the switch).
  Statements following a case get one additional level with `Indent` when
  `IndentBlockBodies` is enabled. Only parsed case/default labels directly
  in complete switch blocks are affected. An explicit `{` following a case
  aligns with the case label. This is independent of `LabelColonSpacing`.
- `StatementAttributeIndent`: `Preserve` (default) leaves standalone
  `[[...]]` statement attributes at their existing indentation, even when
  block body indentation is enabled. `Block` aligns them with the containing
  block's statement indent (or the control header for an unbraced body);
  `Indent` adds one `IndentWidth` to that indent. Only parsed, line-leading
  attributes in complete blocks and control bodies are affected. Inline
  attributes, comments, directives, and conditional branches stay unchanged.
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
  separately by `CallArgumentSplit`. New continuations normally use
  `IndentWidth` beyond the original line's leading spaces; parsed control
  conditions can instead use `ControlConditionIndent`, and parsed calls
  can use `CallArgumentIndent`. Comments,
  directives, CRLF, backslashes, and ambiguous or recovered syntax stay
  source-owned. Bare `<`, `>`, `<<`, and `>>` remain untouched even when
  parsed as operators: without name lookup they can be template syntax.
- `ControlConditionIndent`: `IndentWidth` (default) indents new expression
  breaks by one width beyond the original line; `Continuation` aligns
  breaks inside complete parsed `if`, `for`, `while`, `switch`, and `catch`
  conditions below the character after the opening `(`. Requires expression
  layout to be enabled. Existing breaks stay source-owned in `Overflow` mode;
  tabs and recovered headers retain safe fallback layout.
- `ExpressionLayoutAlgorithm`: `Greedy` (default) picks a preferred
  breakpoint that fits the current line; `DynamicProgramming` minimizes
  a bounded whole-line cost for raggedness, extra breaks, and operator
  precedence. Both use the same parser-approved breakpoints, respect
  `BinaryOperatorPlacement` and `AssignmentOperatorPlacement`, and prefer
  outer expression breaks over breaks
  inside parentheses. The choice also affects `Pack` call argument and
  initializer grouping, but not `Preserve` or `OnePerLine` lists, function
  parameters, or inheritance. Searches with more than 96 candidates, or
  without a fitting path, fall back to `Greedy`.
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
  the call's line) or `Continuation` (align with the character after the
  opening `(`). With expression layout enabled, `Continuation` also aligns
  new operator breaks inside complete parsed calls, including single-
  argument macros such as `assert`. Call-list reflow only affects calls
  with plain, single-line
  arguments and whitespace-only list gaps. Comments, directives, CRLF,
  and ambiguous parenthesized callees remain unchanged. Parsed function-like
  macro calls follow the same call layout rules; nested calls are considered
  after enclosing calls, so outer commas can resolve overflow before inner
  commas are tried. Multiline arguments keep their call list unchanged.
  Reflowed lists use comma-space separators;
  `SpaceInsideCallArguments = Space` also applies to their same-line edges.
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
- `InitializerClosingBrace`: `Preserve` (default, retain the closing brace's
  line), `SameLine`, or `NewLine` when a list is reflowed. `NewLine` aligns
  the brace with the original list line.
- `MergeInitializerList`: `false` (default) leaves already split lists
  multiline; `true` joins safe lists that fit within `ColumnLimit` unless
  `InitializerClosingBrace = NewLine` is requested.
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

## Google-style example and limits

[`examples/google.hit-fmt`](examples/google.hit-fmt) is a conservative
approximation of Google's C++ style (and clang-format's Google preset). Use
`--config hit-fmt/examples/google.hit-fmt`, or copy it to `.hit-fmt`. It
selects two-space indentation, an 80-column limit, attached braces, left
pointer alignment, packed arguments, and supported token spacing. It is not
a replacement for `clang-format -style=Google` or a Google C++ style review.
The profile deliberately preserves options that caused incorrect changes in
real Google code (see below).

Missing or insufficient controls for a full Google profile include:

- Access labels have a **one-space** offset (` public:`) with two-space
  member indentation. `IndentAccessModifiers` supports only zero or one
  *IndentWidth* (two spaces), not an arbitrary offset. Namespace comments
  need **two** spaces before `//`; `NamespaceComments = Required` emits
  one. No setting can add the comment without changing that spacing.
- Include grouping (related header first, C/system/other libraries and
  project headers), blank lines between groups, and sorting inside each
  group; `IncludeOrder = Sort` cannot represent these categories. Header
  guards, forward declarations, and `clang-format off` regions are not
  enforced. Existing leading/trailing comment columns and prose are not
  aligned or reflowed.
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

Corpus check: tested against the C++ sources in the Ubuntu-packaged
GoogleTest/GoogleMock (`/usr/src/googletest`, GoogleTest 1.12.1). Copies of
`gtest-filepath.cc`, `gmock-cardinalities.cc`, `gtest-port.h`, and
`gmock-internal-utils.cc` remain unchanged with the profile. Observed
behavior when trying the more aggressive, seemingly appropriate options:

- `IndentBlockBodies = true` moves statements in inline methods of
  `googlemock/src/gmock-cardinalities.cc` (around line 54) two spaces to the
  left and moves closing braces too, destroying the already correct nesting.
- `BraceInitializerSpacing = NoSpace` turns `class GTEST_API_ RE {` into
  `class GTEST_API_ RE{` in `googletest/include/gtest/internal/gtest-port.h`
  (around line 940): a macro-annotated record is mistaken for an initializer.
  `MemberAccessOperatorsSpacing = NoSpace` changes
  `GTEST_API_ ::std::string` to `GTEST_API_::std::string` there (around line
  1016), potentially changing the meaning after macro expansion.
- `BinaryOperatorSpacing = Space` introduces `value ,` in a
  `static_assert(std::is_base_of<Base, Derived>::value, ...)` in the same
  header (around line 1151). `TernaryOperatorSpacing = Space` collapses
  deliberate alignment around `?` in the nested conditional expressions
  in `googletest/src/gtest-death-test.cc` (around line 519) without aligning
  the other branches.
- `FunctionParameterIndent = Continuation` alone reindents the already
  correct four-space, first-parameter-on-next-line layout in
  `googlemock/src/gmock-internal-utils.cc:59` to the column after `(`.
  `FunctionParameterNewLineIndent = 4` avoids that case. With
  `InitializerListSplit = Pack`, the nested aggregate initializer near
  line 217 of that file is also needlessly expanded.
- `NamespaceComments = Required` rewrites `}  // namespace testing` to
  `} // namespace testing` in `gtest-filepath.cc`, and replaces the
  descriptive `}  // Unnamed namespace` in `gmock-cardinalities.cc` with
  `} // namespace`. It is therefore left at `Preserve`.

These examples are limitations of the current formatter, not violations in
GoogleTest. Profile settings that preserve such constructs trade off coverage
against modifying already correctly formatted code.

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
