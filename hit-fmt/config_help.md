# Configuration options
Use Key = Value, # comments and ["quoted", "strings"] lists.
Defaults keep line breaks; Layout = Canonical reflows code.
Protected code stays unchanged. Invalid keys or values are errors.
Use --generate-config for all options, values and defaults.
Use --generate-config google (or ytsaurus/llvm) for a preset.

# Inheritance
include = "base.hit-fmt" loads a base config.
- Paths start beside this config; @/ starts at the nearest ancestor
  with a .git or .arc marker. Absolute paths work too.
- Local settings win. Lists replace inherited lists; [] clears them.
- Validate after merging. Include depth: 64 files maximum.
  Cycles, missing files and duplicate includes are errors.

```config
include = "@/configs/base.hit-fmt"
ColumnLimit = 100
```

# Layout and width
- Layout: Preserve keeps line breaks; Canonical reflows code to fit.
  Category overrides accept Inherit, Preserve or Canonical.
- Break: IfNeeded allows fitting code to flatten; Always requests a
  break even when it fits. Empty, indivisible or protected syntax may
  prevent it; a nested break alone may not satisfy Always.
- ColumnLimit: 0 disables width wrapping and diagnostics; 1..10000
  sets the limit. Required breaks still apply at 0.
- WidthDiagnostics: Off or Report. Counts Unicode code points, not
  display cells; invalid UTF-8 bytes count individually. Tabs expand
  to tab stops when checking output widths.

## Canonical layout
```config
Layout = Canonical
IndentWidth = 2
ColumnLimit = 80
WidthDiagnostics = Report
BaseListsLayout = Preserve
```

# Lists and alignment
- Categories override shared rules: CallsPacking, ParametersBreak, etc.
  Inherit uses shared values. Base/constructor lists have no Closer.
- Packing: Fill packs items; VerticalWhenBroken puts each item on a line
  when the list breaks; AllOrVertical keeps a full row or splits every item.
- FirstItem: InlineIfFits or NextLine. Closer: SameLine or OwnLine.
  Both apply to broken lists, independently of item packing.
- Alignment: Delimiter aligns after the opener, with a fallback when
  the next item and suffix cannot fit. Block indents from the block.
  Zero offsets use IndentWidth.
- TemplateParameters includes generic lambdas; TemplateArguments includes
  named casts. Shared >> closers stay together.
- StringLiteralFragmentsAsArguments = true makes adjacent literals use
  Calls layout, break policy, packing and indentation as one expression.
  Direct arguments share their call's broken shape. Default false keeps
  Expressions formatting and authored layout follows those existing rules.
- CallsSoleArgumentFirstItem overrides CallsFirstItem for a sole argument.
  With StringLiteralFragmentsAsArguments, fragmented strings instead use
  CallsFirstItem.
- IfConditionsPacking = VerticalWhenBroken splits top-level &&/|| in
  wrapped if headers. Requires canonical header/expression layout;
  parentheses and other controls keep their own rules.
- Block comments after separators move with the next item. Other
  attachments stay intact; lambdas indent from their opening line.

## List defaults
```config
ListBreak = IfNeeded
ListPacking = Fill
ListFirstItem = InlineIfFits
ListCloser = SameLine
ListAlignment = Delimiter
ContinuationOffset = 4
AlignmentFallbackOffset = 4
```

## One argument per line
```config
CallsBreak = Always
CallsPacking = VerticalWhenBroken
CallsFirstItem = NextLine
CallsCloser = OwnLine
CallsAlignment = Block
# Let a sole argument start inline if it fits.
CallsSoleArgumentFirstItem = InlineIfFits
```

## Keep argument rows together
```config
ColumnLimit = 32
CallsPacking = AllOrVertical
CallsFirstItem = NextLine
CallsCloser = OwnLine
# ProcessPendingTasks(
#     first, second
# );
```

## Template lists
```config
TemplateParametersBreak = IfNeeded
TemplateParametersPacking = VerticalWhenBroken
TemplateParametersFirstItem = NextLine
TemplateParametersCloser = SameLine
TemplateParametersAlignment = Block
TemplateArgumentsBreak = IfNeeded
TemplateArgumentsPacking = VerticalWhenBroken
TemplateArgumentsFirstItem = NextLine
TemplateArgumentsCloser = SameLine
TemplateArgumentsAlignment = Block
```

## Wrapped if conditions
```config
Layout = Canonical
IfConditionsPacking = VerticalWhenBroken
ControlHeadersFirstItem = InlineIfFits
BinaryOperatorSide = Leading
```

# Expressions and chains
- ExpressionsBreak and ChainsBreak default to IfNeeded.
  Wrapping respects precedence. OperatorSide options choose Leading
  or Trailing; ExpressionsAlignment chooses Operand or Block.
- AssignmentsPacking inherits ExpressionsPacking. Fill can keep an RHS
  prefix inline; VerticalWhenBroken splits assignment boundaries.
  AssignmentRhsPlacement = NextLine puts initializers below the operator
  in canonical expressions. Always breaks still apply.
  With StringLiteralFragmentsAsArguments, broken string initializers also
  start their first fragment below the assignment operator.
- TernaryLayout = ThreePart breaks ? and : together when wrapping;
  Flexible may split them independently. TernaryOperatorIndentation
  = Indent adds a level when the condition starts on a continued line;
  Align shares the continuation.
- Chains include . / -> calls and factory()(arg), not nested arguments.
  ChainsBreak = Always needs multiple calls after the receiver;
  fields and single calls wrap as needed. Control headers prefer other
  breaks; nested lambdas keep ordinary chain rules.
- AllOrVertical keeps a chain tail together or splits all its links.
  For expressions and IfConditionsPacking it means VerticalWhenBroken.

## Expression wrapping
```config
ExpressionsLayout = Canonical
ExpressionsBreak = IfNeeded
ExpressionsPacking = Fill
ExpressionsAlignment = Block
BinaryOperatorSide = Trailing
ShiftOperatorSide = Leading
AssignmentOperatorSide = Trailing
AssignmentRhsPlacement = InlineIfFits
TernaryOperatorSide = Leading
TernaryLayout = ThreePart
TernaryOperatorIndentation = Align
```

## Keep assignment prefixes inline
```config
ExpressionsPacking = VerticalWhenBroken
AssignmentsPacking = Fill
AssignmentRhsPlacement = InlineIfFits
# When the RHS wraps:
# auto ready = firstCheck() ||
#     secondCheck();
```

## Call chains
```config
ChainsLayout = Canonical
ChainsBreak = IfNeeded
ChainsPacking = Fill
ChainsAlignment = Block
```

## Keep a chain tail together
```config
ColumnLimit = 40
ChainsPacking = AllOrVertical
# auto client = MakeServiceClient()
#     .Configure(mode).Start();
```

# Braces and bodies
- Brace placements: Attached, NextLine, NextLineIfHeaderBroken,
  NextLineIfHeaderIndented. The last breaks when the final output header
  line reaches body indentation; a closer at brace indentation attaches.
  Constructors/destructors may Inherit FunctionBracePlacement.
  CaseBlockBracePlacement applies to a case containing only one block.
- DeclarationHeadersLayout controls headers; BodiesLayout controls bodies.
  BodyShape and EmptyBodyShape default to Expanded. CompactIfFits allows
  inline bodies; CompactSingleStatementIfFits allows one simple statement
  within ColumnLimit, including a local declaration. Canonical bodies only.
- FunctionBodyShape and ControlBodyShape override nonempty function and
  control bodies independently; Inherit (default) uses BodyShape.
  Functions include constructors, destructors and configured body macros.
  Controls include braced/unbraced arms, try/catch and case blocks.
  Standalone blocks, records and namespaces keep BodyShape.
- Nested controls/blocks, records, namespaces and sibling comments or
  directives prevent single-statement compaction. Comment-only bodies
  are not empty; empty bodies use EmptyBodyShape. The single-statement
  mode expands empty bodies. Enums, lists and requirements use their own rules.
- LambdaBodiesLayout, LambdaBodyShape and LambdaEmptyBodyShape can
  override body policies independently. LambdaBodyShape inherits BodyShape,
  independently of FunctionBodyShape and ControlBodyShape.
- IndentNamespaces: Preserve, Indent or NoIndent (enclosing indent).
  NamespaceIndentWidth = 0 uses IndentWidth.
- IndentLinkageBlocks: Preserve, Indent or NoIndent (enclosing indent)
  inside braced extern "C" / extern "C++" blocks. LinkageIndentWidth = 0
  uses IndentWidth. Body line breaks are preserved; single extern
  declarations without braces are unaffected.
- Enumerators uses list policies; EnumBracePlacement follows the header.
  Base/constructor lists use Leading/Trailing ColonSide and CommaSide.
  InitializersTrailingComma: Preserve, Never or WhenMultiline, where legal.
  Trailing commas and closer placement are independent.

## Braces and body shapes
```config
FunctionBracePlacement = NextLine
ConstructorBracePlacement = Inherit
DestructorBracePlacement = Inherit
ControlBracePlacement = NextLineIfHeaderBroken
CaseBlockBracePlacement = Attached
BodiesLayout = Canonical
BodyShape = Expanded
FunctionBodyShape = CompactSingleStatementIfFits
ControlBodyShape = Expanded
EmptyBodyShape = CompactIfFits
LambdaBodiesLayout = Inherit
LambdaBodyShape = CompactIfFits
LambdaEmptyBodyShape = Inherit
```

## Braces after continued headers
```config
FunctionBracePlacement = NextLineIfHeaderIndented
ConstructorBracePlacement = Inherit
# With canonical headers, keep ) { at the function indentation:
# void Process(
#     Request request
# ) {
# Move { below a header ending at or beyond the body indentation:
# Worker::Worker() :
#     Cache_(CreateCache())
# {
```

## Compact one-statement bodies
```config
BodiesLayout = Canonical
FunctionBodyShape = CompactSingleStatementIfFits
ControlBodyShape = Expanded
FunctionBracePlacement = Attached
# int Next(int x) { return x + 1; }
# void Reset() { Clear(); Notify(); } stays expanded.
```

## Namespaces, linkage, enums and initializers
```config
IndentNamespaces = NoIndent
NamespaceIndentWidth = 0
IndentLinkageBlocks = Indent
LinkageIndentWidth = 0
# extern "C" {
#     int Entry();
# }
EnumBracePlacement = NextLineIfHeaderBroken
EnumeratorsLayout = Canonical
EnumeratorsBreak = Always
EnumeratorsPacking = VerticalWhenBroken
EnumeratorsCloser = OwnLine
InitializersCloser = OwnLine
InitializersTrailingComma = WhenMultiline
```

# Blank lines
- Counts accept Preserve or 0..10000. Positive counts force a break;
  zero adds no break. Existing blank lines stay unless overridden.
- NamespaceBlankLines* and LinkageBlankLines* apply inside nonempty
  blocks, outside attached comments. AccessSectionBlankLinesBefore skips
  the first section; AfterLabel needs following content.
- DeclarationBlankLines separates declarations. DefinitionBlankLines wins
  beside function, record or enum definitions, not lambdas/namespaces.
  DeclarationCategoryBlankLines overrides both between types, functions
  and variables; aliases are types, templates follow their entity.
- MemberDeclarationBlankLines and MemberDefinitionBlankLines override
  general counts; Inherit follows them.
- MaxEmptyLines caps editable blank lines, not inline gaps or tokens.
  Exact counts above an explicit cap are invalid after inheritance.
  Comment attachments, directives and protection can stop these edits.

## Structural blank lines
```config
NamespaceBlankLinesAfterOpen = 1
NamespaceBlankLinesBeforeClose = 1
LinkageBlankLinesAfterOpen = 1
LinkageBlankLinesBeforeClose = 1
AccessSectionBlankLinesBefore = 1
AccessSectionBlankLinesAfterLabel = 1
DeclarationBlankLines = 0
DeclarationCategoryBlankLines = 1
MemberDeclarationBlankLines = Preserve
DefinitionBlankLines = 1
MemberDefinitionBlankLines = 0
MaxEmptyLines = 1
```

# Spacing
- Values: Preserve, Space, NoSpace, GeneratedSpace, GeneratedNoSpace.
  Generated* applies when canonical layout rebuilds the owning syntax;
  nested edits alone do not activate it. Syntax safety always wins.
- Before/After controls each side separately.
  TemplateTypeArgumentInteriorSpacing = NoSpace permits nested >>;
  ScopeResolutionSpacing excludes leading ::. BraceInitializerSpacing
  controls the gap before {, except after comments.
- ArgumentCommentSpacing controls attached block comments in arguments
  and initializers. ArgumentNameCommentSpacing separately controls the
  gap after a leading /*name=*/ label (also /* name = */), where name
  is an identifier. Gaps between comments and before trailing comments
  use ArgumentCommentSpacing. Both default to Preserve. Newlines,
  contents, separator-facing gaps and line comments stay intact.
  Generated* needs canonical layout of that list.
- FunctionCvRefQualifierSpacing controls const/volatile to &/&& in
  function qualifiers. Inherit follows FunctionRefQualifierSpacing;
  Preserve keeps source spacing.
- AttributeSpaceBeforeSpecifier and AttributeSpaceAfterSpecifier control
  [[...]], __attribute__ and __declspec boundaries, not extension contents.
  AttributeInteriorSpacing covers C++ [[...]] only.

## Common spacing
```config
FunctionSpaceBeforeParameters = NoSpace
ControlBlockKeywordSpacing = Space
CommaSpaceBefore = NoSpace
CommaSpaceAfter = Space
EmptyBlockInteriorSpacing = GeneratedNoSpace
ScopeResolutionSpacing = NoSpace
TemplateTypeArgumentInteriorSpacing = NoSpace
```

## Comments, attributes and qualifiers
```config
ArgumentCommentSpacing = Space
ArgumentNameCommentSpacing = NoSpace
# Example: consume(/* note */ value, /*count=*/size /* suffix */);
AttributeSpaceBeforeSpecifier = Space
AttributeSpaceAfterSpecifier = Space
FunctionRefQualifierSpacing = Space
FunctionCvRefQualifierSpacing = NoSpace
```

# Call annotations
- CallLineFirstArgumentNames pins any first argument beside the opener;
  CallLineFirstStringNames pins string literals only. Both default to [].
  Use exact unqualified call names, including members; the general list
  wins overlaps. Overflow: Wrap normally; BreakBefore moves the argument
  below the opener if it cannot fit in full; KeepInline retains its opening
  piece even past ColumnLimit. Nested syntax may wrap; literals split only
  between pieces. Preserve/Always win.
- ChainedCallRootNames: [] selects all safe chains; otherwise use listed
  roots. AlwaysBreakChainRootNames may break even one following call.
  Omit template arguments and the template keyword:
  ns::Builder<T>::Make<U> matches ns::Builder::Make, not Make.
- IndentedCallScopePairs preserves extra indentation between matched calls
  in one context and label section. GenerateCallScopeIndentation creates it
  from flat input; requires non-Preserve IndentationKind.
- IndentedChainScopePairs indents inside matched chain calls;
  StickyChainCallPairs keeps fitting consecutive calls together.
  Both obey ChainsLayout/root selection; sticky pairs may name a factory.
- Pairs use exact First:Second names, not patterns. Scope endpoints must
  be distinct and unambiguous; sticky pairs may share names.

## Pin first arguments
```config
CallLineFirstArgumentNames = ["dispatch"]
CallLineFirstArgumentOverflow = Wrap
CallLineFirstStringNames = ["WRITE_MESSAGE"]
CallLineFirstStringOverflow = BreakBefore
```

## Chain roots and forced breaks
```config
ChainedCallRootNames = ["factory", "pipeline::Builder"]
AlwaysBreakChainRootNames = ["WRITE_EVENT", "logging::Event"]
# Root names omit template arguments: factory<T> matches factory.
```

## Paired calls and chains
```config
IndentationKind = Spaces
IndentedCallScopePairs = ["OpenItems:CloseItems"]
GenerateCallScopeIndentation = true
IndentedChainScopePairs = ["BeginMap:EndMap", "BeginList:EndList"]
StickyChainCallPairs = ["Item:Value", "registrar:Parameter"]
```

# Include sorting
- IncludeOrder = Sort sorts safe blocks;
  Preserve disables sorting. IncludeMatchingHeader = First pairs a
  source with a same-directory, same-stem .h/.hh/.hpp/.hxx/.h++ header.
  Header inputs or missing source filenames disable pairing.
- IncludeCategoryOrder: QuotedFirst (default) or AngledFirst orders quoted
  and angled headers in every grouping mode. Matching headers stay first;
  IncludeStandardHeaders = First/Last puts standard headers before/after
  all remaining headers, overriding category and group order.
- IncludeGrouping: Preserve keeps adjacent blocks and blank lines;
  Regroup merges across blanks by rule; Directory groups by directory.
  Regroup/Directory insert one blank line, conflicting with MaxEmptyLines
  = 0. Preserve ignores group rules; Regroup ignores Depth.
- IncludeGroups uses Prefix or Prefixes, Depth (default 127) and Priority
  (default 0). First matching prefix wins. Prefixes must be unique relative
  directories ending in /; a sole empty Prefix is a fallback.
  Depth: 0 keeps a rule together; 1..126 limits suffix directory depth;
  127 keeps full directories. Aliases merge groups without rewriting paths.
- Priority: -128..127, lower first within quoted/angled categories.
  IncludeGroupOrder: Directory or Configured (rule order); unmatched
  groups use priority 0 and follow matched groups in a configured tier.
  Priority affects output order, not matching. Rules validate even if inactive.
- IncludeDirectoryOrder and IncludeInGroupOrder choose NearestFirst or
  Alphabetical. Distance counts parent/child steps from the input directory;
  ties are alphabetical. In-group distance ignores Depth and aliases.
  Paired and standard-header groups stay alphabetical.
- IncludePathBase: SourceDirectory or ProjectRoot (working directory).
  Bare quoted names and quoted ./ or ../ paths stay source-relative;
  absolute paths stay absolute. Paths are lexical; no filesystem lookup.
- IncludeStandardHeaders = First/Last adds an initial/final alphabetical
  group for angled names with neither / nor .h. This includes extensionless
  vendor names; quoted names and stdio.h do not qualify. Preserve applies
  ordinary category and group sorting. A matching header still comes first.
- Comments, macros, conditionals, protection and mixed line endings stop
  blocks. Attached-comment blocks stay exact; hunks must cover a whole
  block. Check mode reports one include-order failure per file.

## Group headers by directory
```config
IncludeOrder = Sort
IncludeCategoryOrder = AngledFirst
IncludeMatchingHeader = First
IncludeGrouping = Directory
IncludeGroupOrder = Configured
IncludeDirectoryOrder = NearestFirst
IncludeInGroupOrder = Alphabetical
IncludePathBase = SourceDirectory
IncludeStandardHeaders = Last
IncludeGroups = [
    { Prefix = "library/", Depth = 1 },
    { Prefix = "util/", Depth = 1 }
]
# library/core/a.hpp and library/core/detail/b.hpp share a group.
```

## Put standard headers before system headers
```config
Layout = Canonical
IncludeOrder = Sort
IncludeGrouping = Directory
IncludeStandardHeaders = First
# <vector> precedes <Windows.h>, with a blank line between the groups.
```

## Order project groups first
```config
IncludeOrder = Sort
IncludeGrouping = Directory
IncludeGroupOrder = Directory
IncludeDirectoryOrder = NearestFirst
IncludeInGroupOrder = NearestFirst
IncludePathBase = ProjectRoot
IncludeGroups = [
    { Prefix = "library/", Priority = 1 },
    { Prefix = "util/", Priority = 2 },
    { Prefix = "", Depth = 3, Priority = 0 }
]
# ProjectRoot uses the working directory for input filenames.
```

# Templates and comments
- BreakAfterTemplateHeader requires canonical DeclarationHeadersLayout.
- RequiresClausePosition also requires canonical DeclarationHeadersLayout.
  Preserve (default) keeps placement. OwnLine separates the clause;
  OwnLineWithBrace allows an attached function brace. WithPreceding prefers
  the preceding header; WithFollowing prefers the following declaration or
  brace. SingleLine prefers both sides inline, wrapping when necessary.
  Explicit brace placement and protected/comment boundaries take precedence.
  A prefix clause's position overrides BreakAfterTemplateHeader. SingleLine
  also joins the template header of a function with a trailing clause.
- IndentRequiresClause (default true) adds IndentWidth for OwnLine,
  OwnLineWithBrace and WithFollowing. Other explicit positions use the
  ordinary continuation offset when wrapped. Requires-expressions and
  nested requirements are unaffected.
- StandaloneCommentBlankLinesBefore/After uses blank-line counts and caps.
- RulerLength: 0 preserves standalone all-slash comments; 3..10000 sets
  the slash count. RulerBlankLinesBefore/After overrides surrounding blanks.

## Template headers and comments
```config
DeclarationHeadersLayout = Canonical
BreakAfterTemplateHeader = true
StandaloneCommentBlankLinesBefore = Preserve
StandaloneCommentBlankLinesAfter = Preserve
RulerLength = 0
RulerBlankLinesBefore = Preserve
RulerBlankLinesAfter = Preserve
```

## Put constraints on an indented line
```config
DeclarationHeadersLayout = Canonical
RequiresClausePosition = OwnLine
IndentRequiresClause = true
# Result:
# template<class T>
#     requires Ready<T>
# void process(T value);
```

# Safety and checks
- hit-fmt off/on always protects text. HonorClangFormatDirectives
  defaults to true for clang-format off/on.
- FunctionBodyMacroNames marks balanced invocations with function bodies;
  WhitespaceSensitiveMacros protects invocation bytes. Nonempty
  CallLikeMacroNames is currently unsupported.
- ClassMacroNames uses Begin:End pairs of unqualified function-like macros.
  Endpoints must be distinct and unique; nested pairs must match.
- Brace insertion, null statement/declaration removal and namespace-comment
  changes are opt-in. Required semicolons, uncertain macro syntax and
  commented placeholders stay; no transform crosses protection.
- IndentPreprocessingDirectives indents whole directives by conditional
  depth using IndentWidth, removes space after # or %:, and aligns
  else/elif/endif with their if. AlignMacroBackslashes aligns existing
  continuations after the longest prefix; tokens/spliced text stay intact.
- Naming/parameter-name checks report issues without renaming code.
  Exclude filters paths; NamingNamespaceExemptions exempts namespaces.
  Presets approximate style guides, not clang-format compatibility.

## Macro hints
```config
HonorClangFormatDirectives = true
CallLikeMacroNames = []
FunctionBodyMacroNames = ["DEFINE_CASE", "DEFINE_METHOD"]
ClassMacroNames = ["BEGIN_SETTER:END_SETTER"]
WhitespaceSensitiveMacros = ["STRINGIZE_TEXT"]
```

## Optional transforms
```config
AddMissingControlBraces = false
RemoveNullStatements = false
RemoveNullDeclarations = false
IncludeOrder = Preserve
NamespaceComments = Preserve
IndentPreprocessingDirectives = false
AlignMacroBackslashes = false
```

## Naming checks and excluded paths
```config
FunctionDeclarationParameterNames = Required
FunctionDefinitionParameterNames = NameOrCommentedNameRequired
ClassNameCase = PascalCase
ClassNamePrefixes = []
NamingNamespaceExemptions = ["std"]
Exclude = ["(^|/)generated(/|$)"]
```
