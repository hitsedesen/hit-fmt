#pragma once

#include <span>
#include <string_view>

namespace hit_fmt {

struct HelpExample
{
    std::string_view Title;
    std::string_view Code;
};

struct HelpSection
{
    std::string_view Title;
    std::string_view Description;
    std::span<const HelpExample> Examples;
};

// hit-fmt off
constexpr HelpExample LayoutExamples[] = {
    {"Enable canonical layout:", R"(Layout = Canonical
IndentWidth = 2
ColumnLimit = 80
WidthDiagnostics = Report
BaseListsLayout = Preserve
)"},
};
constexpr HelpExample ListExamples[] = {
    {"Set shared list defaults:", R"(ListBreak = IfNeeded
ListPacking = Fill
ListFirstItem = InlineIfFits
ListCloser = SameLine
ListAlignment = Delimiter
ContinuationOffset = 4
AlignmentFallbackOffset = 4
)"},
    {"Put call arguments on separate lines:", R"(CallsBreak = Always
CallsPacking = VerticalWhenBroken
CallsFirstItem = NextLine
CallsCloser = OwnLine
CallsAlignment = Block
# Let a sole argument start inline if it fits.
CallsSoleArgumentFirstItem = InlineIfFits
)"},
    {"Wrap template lists independently:", R"(TemplateParametersBreak = IfNeeded
TemplateParametersPacking = VerticalWhenBroken
TemplateParametersFirstItem = NextLine
TemplateParametersCloser = SameLine
TemplateParametersAlignment = Block
TemplateArgumentsBreak = IfNeeded
TemplateArgumentsPacking = VerticalWhenBroken
TemplateArgumentsFirstItem = NextLine
TemplateArgumentsCloser = SameLine
TemplateArgumentsAlignment = Block
)"},
    {"Separate conditions in a wrapped if header:", R"(Layout = Canonical
IfConditionsPacking = VerticalWhenBroken
ControlHeadersFirstItem = InlineIfFits
BinaryOperatorSide = Leading
)"},
};
constexpr HelpExample ExpressionExamples[] = {
    {"Wrap expressions and ternaries:", R"(ExpressionsLayout = Canonical
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
)"},
    {"Keep assignment prefixes beside wrapped expressions:",
        R"(ExpressionsPacking = VerticalWhenBroken
AssignmentsPacking = Fill
AssignmentRhsPlacement = InlineIfFits
# When the RHS wraps:
# auto ready = firstCheck() ||
#     secondCheck();
)"},
    {"Wrap call chains:", R"(ChainsLayout = Canonical
ChainsBreak = IfNeeded
ChainsPacking = Fill
ChainsAlignment = Block
)"},
};
constexpr HelpExample ShapeExamples[] = {
    {"Choose braces and body shapes:", R"(FunctionBracePlacement = NextLine
ConstructorBracePlacement = Inherit
DestructorBracePlacement = Inherit
ControlBracePlacement = NextLineIfHeaderBroken
CaseBlockBracePlacement = Attached
BodiesLayout = Canonical
BodyShape = Expanded
EmptyBodyShape = CompactIfFits
LambdaBodiesLayout = Inherit
LambdaBodyShape = CompactIfFits
LambdaEmptyBodyShape = Inherit
)"},
    {"Set namespace, enum and initializer layout:",
        R"(IndentNamespaces = NoIndent
NamespaceIndentWidth = 0
EnumBracePlacement = NextLineIfHeaderBroken
EnumeratorsLayout = Canonical
EnumeratorsBreak = Always
EnumeratorsPacking = VerticalWhenBroken
EnumeratorsCloser = OwnLine
InitializersCloser = OwnLine
InitializersTrailingComma = WhenMultiline
)"},
};
constexpr HelpExample VerticalExamples[] = {
    {"Set blank lines at structural boundaries:",
        R"(NamespaceBlankLinesAfterOpen = 1
NamespaceBlankLinesBeforeClose = 1
AccessSectionBlankLinesBefore = 1
AccessSectionBlankLinesAfterLabel = 1
DeclarationBlankLines = 1
MemberDeclarationBlankLines = Preserve
DefinitionBlankLines = 1
MemberDefinitionBlankLines = 0
MaxEmptyLines = 1
)"},
};
constexpr HelpExample GapExamples[] = {
    {"Set common spacing:", R"(FunctionSpaceBeforeParameters = NoSpace
ControlBlockKeywordSpacing = Space
CommaSpaceBefore = NoSpace
CommaSpaceAfter = Space
EmptyBlockInteriorSpacing = GeneratedNoSpace
ScopeResolutionSpacing = NoSpace
TemplateTypeArgumentInteriorSpacing = NoSpace
)"},
    {"Space comments, attributes and function qualifiers:",
        R"(ArgumentCommentSpacing = Space
# Example: consume(/* first */ /* second */ value /* note */);
AttributeSpaceBeforeSpecifier = Space
AttributeSpaceAfterSpecifier = Space
FunctionRefQualifierSpacing = Space
FunctionCvRefQualifierSpacing = NoSpace
)"},
};
constexpr HelpExample AnnotationExamples[] = {
    {"Keep selected first arguments beside the opener:",
        R"CONFIG(CallLineFirstArgumentNames = ["dispatch"]
CallLineFirstArgumentOverflow = Wrap
CallLineFirstStringNames = ["WRITE_MESSAGE"]
CallLineFirstStringOverflow = BreakBefore
)CONFIG"},
    {"Select chain roots and preferred breaks:",
        R"CONFIG(ChainedCallRootNames = ["factory", "pipeline::Builder"]
AlwaysBreakChainRootNames = ["WRITE_EVENT", "logging::Event"]
# Root names omit template arguments: factory<T> matches factory.
)CONFIG"},
    {"Indent paired calls and keep related chain calls together:",
        R"CONFIG(IndentedCallScopePairs = ["OpenItems:CloseItems"]
GenerateCallScopeIndentation = true
IndentedChainScopePairs = ["BeginMap:EndMap", "BeginList:EndList"]
StickyChainCallPairs = ["Item:Value", "registrar:Parameter"]
)CONFIG"},
};
constexpr HelpExample IncludeExamples[] = {
    {"Sort headers in configured directory groups:", R"(IncludeOrder = Sort
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
)"},
    {"Put project headers before third-party groups:", R"(IncludeOrder = Sort
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
)"},
};
constexpr HelpExample StagedExamples[] = {
    {"Control template headers, comments and rulers:",
        R"(DeclarationHeadersLayout = Canonical
BreakAfterTemplateHeader = true
StandaloneCommentBlankLinesBefore = Preserve
StandaloneCommentBlankLinesAfter = Preserve
RulerLength = 0
RulerBlankLinesBefore = Preserve
RulerBlankLinesAfter = Preserve
)"},
};
constexpr HelpExample SafetyExamples[] = {
    {"Describe macro syntax and protection:",
        R"CONFIG(HonorClangFormatDirectives = true
CallLikeMacroNames = ["REGISTER_NODE"]
FunctionBodyMacroNames = ["DEFINE_CASE", "DEFINE_METHOD"]
ClassMacroNames = ["BEGIN_SETTER:END_SETTER"]
WhitespaceSensitiveMacros = ["STRINGIZE_TEXT"]
)CONFIG"},
    {"Choose transforms explicitly (disabled here):",
        R"(AddMissingControlBraces = false
RemoveNullStatements = false
RemoveNullDeclarations = false
IncludeOrder = Preserve
NamespaceComments = Preserve
IndentPreprocessingDirectives = false
AlignMacroBackslashes = false
)"},
    {"Check naming and exclude generated paths:",
        R"CONFIG(FunctionDeclarationParameterNames = Required
FunctionDefinitionParameterNames = NameOrCommentedNameRequired
ClassNameCase = PascalCase
ClassNamePrefixes = []
NamingNamespaceExemptions = ["std"]
Exclude = ["(^|/)generated(/|$)"]
)CONFIG"},
};

constexpr HelpSection HelpSections[] = {
    {"Layout and width",
        "- Layout = Preserve (default) keeps source line breaks.\n"
        "  Canonical reflows syntax to fit; category Layout overrides may use\n"
        "  Inherit, Preserve or Canonical. Protection always "
        "takes precedence.\n"
        "- Break = IfNeeded allows fitting syntax to flatten. Always requests\n"
        "  a break even when it fits, but cannot split empty, indivisible or\n"
        "  protected syntax. A nested break alone may not satisfy Always.\n"
        "- ColumnLimit: 0 (unlimited) or 1..10000. Zero disables "
        "width wrapping\n"
        "  and diagnostics, not required breaks.\n"
        "- WidthDiagnostics: Off or Report. Widths count Unicode code points,\n"
        "  not display cells; invalid UTF-8 bytes count individually. Report\n"
        "  checks output line widths and expands tabs to tab stops.\n",
        LayoutExamples},
    {"Lists and alignment",
        "- Categories: Calls, Parameters, TemplateParameters, "
        "TemplateArguments,\n"
        "  Initializers, Captures, ControlHeaders, BaseLists, "
        "ConstructorLists,\n"
        "  Enumerators. Use category prefixes for per-category overrides.\n"
        "- Each supports Layout, Break, Packing, FirstItem, Alignment,\n"
        "  ContinuationOffset and AlignmentFallbackOffset. Optional overrides\n"
        "  default to Inherit. Closer excludes BaseLists and "
        "ConstructorLists.\n"
        "- Packing: Fill packs items; VerticalWhenBroken separates them once\n"
        "  the list wraps. FirstItem: InlineIfFits or NextLine. "
        "Closer: SameLine\n"
        "  or OwnLine. First-item and closer placement apply to broken lists.\n"
        "- Alignment: Delimiter aligns after the opener; Block uses the block\n"
        "  indentation plus an offset. Zero offsets use "
        "IndentWidth. Delimiter\n"
        "  alignment falls back when the next item and suffix cannot fit.\n"
        "  Openers on one line share a continuation level; lists starting on\n"
        "  continued lines add a level. Bodies establish a fresh base.\n"
        "- Parameters controls function parameters. "
        "TemplateParameters controls\n"
        "  template and generic-lambda headers; "
        "TemplateArguments also controls\n"
        "  named-cast type brackets. Shared >> closers stay together.\n"
        "- CallsSoleArgumentFirstItem overrides CallsFirstItem "
        "for one argument\n"
        "  of any kind; Inherit follows CallsFirstItem. Other "
        "calls are unchanged.\n"
        "- ControlHeadersFirstItem places the first wrapped condition. With\n"
        "  canonical control-header and expression layout, "
        "IfConditionsPacking\n"
        "  = VerticalWhenBroken separates top-level &&/|| operands in wrapped\n"
        "  if headers. Fill is the default; parenthesized subexpressions and\n"
        "  other control kinds keep their own rules. BinaryOperatorSide sets\n"
        "  the operator side. Fitting headers stay flat unless "
        "Always applies.\n"
        "- Block comments after separators move with the next item. Other\n"
        "  comment attachments and protected gaps take "
        "precedence. Lambda bodies\n"
        "  indent from their opening line, independently of later arguments.\n",
        ListExamples},
    {"Expressions and chains",
        "- ExpressionsBreak and ChainsBreak default "
        "independently to IfNeeded.\n"
        "  Packing uses Fill or VerticalWhenBroken; expression "
        "wrapping respects\n"
        "  operator precedence. BinaryOperatorSide, ShiftOperatorSide,\n"
        "  AssignmentOperatorSide and TernaryOperatorSide use "
        "Leading/Trailing.\n"
        "- ExpressionsAlignment = Operand aligns to the first "
        "operand, falling\n"
        "  back when it cannot fit; Block uses continuation indentation.\n"
        "- AssignmentsPacking: Inherit (default) follows ExpressionsPacking.\n"
        "  Fill can keep the RHS prefix beside the assignment while nested\n"
        "  expressions wrap. VerticalWhenBroken splits all assignment\n"
        "  boundaries in a broken sequence. Includes = initializers,\n"
        "  parameter defaults and enumerator values. ExpressionsBreak\n"
        "  = Always and AssignmentRhsPlacement = NextLine still force breaks.\n"
        "- AssignmentRhsPlacement: InlineIfFits or NextLine. NextLine moves\n"
        "  assignment and declaration initializers below the operator in\n"
        "  canonical expressions, regardless of AssignmentOperatorSide.\n"
        "- TernaryLayout: Flexible or ThreePart. ThreePart "
        "breaks both ? and :\n"
        "  together when wrapping; it does not force a fitting "
        "ternary to wrap.\n"
        "  TernaryOperatorIndentation: Align (default) shares "
        "the continuation;\n"
        "  Indent adds a level when the condition starts on a continued line.\n"
        "- Chains include . and -> calls and repeated calls: factory()(arg).\n"
        "  Nested argument calls are not chain links. ChainsBreak = Always\n"
        "  requires multiple calls after the receiver; field paths and single\n"
        "  calls wrap only as needed. In control headers, "
        "ordinary chains still\n"
        "  wrap only as needed, preferring operator, clause or "
        "argument breaks.\n"
        "  Nested lambda bodies keep the ordinary chain policy.\n",
        ExpressionExamples},
    {"Output shape",
        "- Brace placements: Attached, NextLine or NextLineIfHeaderBroken.\n"
        "  ConstructorBracePlacement and DestructorBracePlacement may Inherit\n"
        "  FunctionBracePlacement. CaseBlockBracePlacement applies to a case\n"
        "  containing only one block, not mixed statements and blocks.\n"
        "- DeclarationHeadersLayout controls headers; BodiesLayout and\n"
        "  LambdaBodiesLayout control bodies. BodyShape and EmptyBodyShape\n"
        "  default to Expanded; CompactIfFits allows compact bodies.\n"
        "  LambdaBodyShape and LambdaEmptyBodyShape inherit independently.\n"
        "  Comment-only bodies are not empty. Enum, list and requirement\n"
        "  bodies are not controlled by these body shapes.\n"
        "- IndentNamespaces: Preserve, Indent or NoIndent. "
        "NamespaceIndentWidth\n"
        "  = 0 uses IndentWidth; NoIndent uses the enclosing indentation.\n"
        "- EnumBracePlacement follows the enum header, not its enumerators.\n"
        "  Enumerators uses list policies and preserves existing enum commas.\n"
        "- Base and constructor lists use ColonSide and CommaSide with\n"
        "  Leading/Trailing values. InitializersTrailingComma uses Preserve,\n"
        "  Never or WhenMultiline at legal initializer sites only. Trailing\n"
        "  commas and closer placement are independent.\n",
        ShapeExamples},
    {"Blank lines",
        "- Exact counts and MaxEmptyLines accept Preserve or 0..10000.\n"
        "  Existing blank lines between items/bodies remain "
        "unless overridden.\n"
        "  Positive counts force a boundary break; zero does not "
        "split a line.\n"
        "- NamespaceBlankLinesAfterOpen/BeforeClose apply to "
        "nonempty content,\n"
        "  outside attached comments. AccessSectionBlankLinesBefore skips the\n"
        "  first section at the opening; AfterLabel needs following content.\n"
        "- DeclarationBlankLines separates declarations, including variables\n"
        "  and prototypes. DefinitionBlankLines takes precedence when either\n"
        "  neighbor is a function, record or enum definition, not a lambda,\n"
        "  namespace or access label.\n"
        "- MemberDeclarationBlankLines and MemberDefinitionBlankLines replace\n"
        "  the corresponding general counts; Inherit follows those counts.\n"
        "- MaxEmptyLines caps editable blank lines, not inline gaps or text\n"
        "  inside tokens. Exact counts above an explicit cap are rejected,\n"
        "  including inherited counts after overrides. Comments "
        "stay attached;\n"
        "  directives, protection and unattached comments stop "
        "these changes.\n",
        VerticalExamples},
    {"Syntax spacing",
        "- Values: Preserve, Space, NoSpace, GeneratedSpace, "
        "GeneratedNoSpace.\n"
        "  Generated* changes spacing only where canonical layout rebuilds\n"
        "  the containing syntax, including existing gaps. Nested edits alone\n"
        "  do not activate it. Preserve keeps source spacing where possible;\n"
        "  reflowed gaps use syntax-safe spacing. Safety always wins.\n"
        "- Before/After options independently control each side of colons,\n"
        "  arrows and operator-overload boundaries. Use --generate-config for\n"
        "  all indentation, declarator, keyword and delimiter "
        "options/defaults.\n"
        "- TemplateTypeArgumentInteriorSpacing = NoSpace permits nested >>.\n"
        "  ScopeResolutionSpacing controls qualified ::, not a leading ::.\n"
        "  BraceInitializerSpacing controls the gap before { except after\n"
        "  comments; comparison and shift operators remain safely separated.\n"
        "- ArgumentCommentSpacing (default Preserve) controls gaps around\n"
        "  attached block comments in call arguments and "
        "initializer elements,\n"
        "  including consecutive comments. Space/NoSpace forces one/no space.\n"
        "  Newlines, comment contents, separator-facing gaps and "
        "line comments\n"
        "  are unchanged. Generated* requires canonical layout of that list.\n"
        "- FunctionCvRefQualifierSpacing controls const/volatile to &/&& in\n"
        "  function qualifiers, not type references. Inherit "
        "(default) follows\n"
        "  FunctionRefQualifierSpacing; Preserve does not inherit.\n"
        "- AttributeSpaceBeforeSpecifier/AfterSpecifier covers [[...]],\n"
        "  __attribute__ and __declspec boundaries; extension contents stay\n"
        "  unchanged. AttributeInteriorSpacing only covers C++ [[...]].\n",
        GapExamples},
    {"Library layout annotations",
        "- CallLineFirstArgumentNames prefers the first argument beside the\n"
        "  opener of exact named calls, including members.\n"
        "  CallLineFirstStringNames does so only for "
        "string-literal arguments.\n"
        "  Both lists default to empty; the general argument "
        "list wins overlaps.\n"
        "- CallLineFirstArgumentOverflow and CallLineFirstStringOverflow:\n"
        "  Wrap (default) wraps normally; BreakBefore moves the "
        "argument below\n"
        "  the opener if it cannot fit in full; KeepInline keeps its opening\n"
        "  piece inline even beyond ColumnLimit. Nested syntax "
        "may still wrap.\n"
        "  Concatenated literals may break between pieces, never inside them.\n"
        "  Preserve and required Always breaks take precedence over pinning.\n"
        "- ChainedCallRootNames: empty selects all safe chains; "
        "otherwise only\n"
        "  listed roots. AlwaysBreakChainRootNames requests breaks even with\n"
        "  one following call. Both obey ChainsLayout, root selection and\n"
        "  protection. All calls use the global ColumnLimit.\n"
        "  Only unambiguously parsed template arguments are ignored.\n"
        "  ns::Builder<T>::Make<U> matches ns::Builder::Make, not Make.\n"
        "  Omit template arguments and template from configured names.\n"
        "- IndentedCallScopePairs preserves an existing extra "
        "indentation level\n"
        "  between matched calls in the same context and label section.\n"
        "  GenerateCallScopeIndentation creates the level even "
        "from flat input;\n"
        "  it requires IndentationKind other than Preserve. "
        "Uncertain, unmatched\n"
        "  or directive-crossing source-line regions remain unchanged.\n"
        "- IndentedChainScopePairs adds IndentWidth inside "
        "nested matched chain\n"
        "  calls. StickyChainCallPairs keeps consecutive named calls together\n"
        "  if they fit; its first name may also name a root receiver/factory.\n"
        "  Both obey ChainsLayout and ChainedCallRootNames.\n"
        "- Pairs use First:Second exact identifiers. Scope endpoints must be\n"
        "  distinct and unambiguous; sticky pairs may share names. These are\n"
        "  exact names, not patterns. Macro safety hints are separate.\n",
        AnnotationExamples},
    {"Include sorting",
        "- IncludeOrder: Preserve (default) disables transforms; Sort sorts\n"
        "  safe blocks, quoted before angled. Duplicate paths stay stable.\n"
        "- IncludeMatchingHeader: Off or First. First pairs a "
        "source file with\n"
        "  its same-directory, same-stem .h/.hh/.hpp/.hxx/.h++ header. Header\n"
        "  inputs or missing input filenames disable pairing.\n"
        "- IncludeGrouping: Preserve sorts adjacent blocks without changing\n"
        "  blank lines and ignores group rules; Regroup combines "
        "blocks across\n"
        "  blank lines by rule; Directory groups by containing directory.\n"
        "  Regroup/Directory insert one blank line between "
        "groups and conflict\n"
        "  with MaxEmptyLines = 0. Paired headers form a separate group.\n"
        "- IncludeGroups rules use Prefix or Prefixes, Depth (default 127)\n"
        "  and Priority (default 0). First matching rule/prefix wins.\n"
        "  Depth: 0 keeps a rule together; 1..126 caps suffix "
        "directory depth;\n"
        "  127 uses the full directory. Regroup ignores Depth. No rules means\n"
        "  full directories; unmatched paths also keep full-directory groups.\n"
        "  Prefixes aliases merge corresponding suffix groups "
        "without changing\n"
        "  include spelling; the first prefix supplies the group path.\n"
        "- Prefixes must be unique relative directories ending in /, with no\n"
        "  backslashes, empty components, '.' or '..'. A sole "
        "empty Prefix is a\n"
        "  fallback; its Depth counts from the resolved directory's start.\n"
        "  Matching uses literal normalized spelling; ./, ../ and absolute\n"
        "  paths only match the fallback. Rules are validated "
        "even if inactive.\n"
        "- Priority: -128..127, lower first within quoted/angled categories;\n"
        "  unmatched paths use 0. IncludeGroupOrder: Directory (default) or\n"
        "  Configured (rule order, unmatched last within the priority tier).\n"
        "  Priority changes output order, not rule matching.\n"
        "- IncludeDirectoryOrder: NearestFirst (default) or Alphabetical.\n"
        "  NearestFirst counts parent/child steps from the input directory;\n"
        "  ties are alphabetical. With Configured, it orders rule subgroups.\n"
        "- IncludeInGroupOrder: Alphabetical (default) or NearestFirst. The\n"
        "  latter uses full containing directories, not Depth or aliases;\n"
        "  ties use include spelling. Also works with Preserve grouping.\n"
        "  Paired and final standard-header groups remain alphabetical.\n"
        "- IncludePathBase: SourceDirectory (default) or ProjectRoot. With\n"
        "  ProjectRoot, input filenames use the working-directory root;\n"
        "  absolute input filenames beneath it are rebased to it. Bare\n"
        "  quoted names and quoted ./ or ../ paths remain source-relative;\n"
        "  absolute paths stay absolute. No filesystem/header-path lookup.\n"
        "- IncludeStandardHeaders: Preserve (default) or Last. Last makes a\n"
        "  final alphabetical group of angled names with neither / nor .h.\n"
        "  This heuristic includes extensionless third-party "
        "names, not stdio.h.\n"
        "- Comments, macros, conditionals, protection and mixed line endings\n"
        "  stop sorting blocks. Attached-comment blocks stay exact. Selected\n"
        "  hunks must cover a whole block. Check mode reports "
        "one include-sort\n"
        "  failure per file; other checks remain independent.\n",
        IncludeExamples},
    {"Template headers, comments and rulers",
        "- BreakAfterTemplateHeader breaks after template declaration headers\n"
        "  when DeclarationHeadersLayout is canonical.\n"
        "- StandaloneCommentBlankLinesBefore/After accepts "
        "Preserve or 0..10000,\n"
        "  subject to MaxEmptyLines.\n"
        "- RulerLength sets the slash count in standalone all-slash comments\n"
        "  containing at least three slashes; 0 preserves spelling.\n"
        "  RulerBlankLinesBefore/After overrides standalone-comment counts\n"
        "  at rulers, with the same count range and cap.\n",
        StagedExamples},
    {"Safety, transforms and checks",
        "- Protected text is never rewritten. hit-fmt off/on always applies;\n"
        "  HonorClangFormatDirectives defaults to true for "
        "clang-format off/on.\n"
        "- Macro hints describe syntax, not style: CallLikeMacroNames marks\n"
        "  call-like syntax; FunctionBodyMacroNames marks "
        "balanced invocations\n"
        "  followed by a function-like brace body. Headers stay unchanged.\n"
        "  WhitespaceSensitiveMacros protects "
        "whitespace-sensitive invocations.\n"
        "- ClassMacroNames uses Begin:End unqualified identifier pairs for\n"
        "  function-like macros enclosing class members. Endpoints must be\n"
        "  distinct and unique; nested pairs must close with the "
        "matching end.\n"
        "  Arguments stay exact; no expanded class/type names are inferred.\n"
        "- AddMissingControlBraces, RemoveNullStatements, "
        "RemoveNullDeclarations\n"
        "  and namespace-comment changes are opt-in. Required semicolons,\n"
        "  uncertain macro syntax and commented standalone placeholders stay.\n"
        "  No transform crosses protection. NamespaceComments "
        "controls namespace\n"
        "  closing comments; Exclude contains path exclusion patterns.\n"
        "- IndentPreprocessingDirectives indents whole "
        "directives by conditional\n"
        "  depth using IndentWidth spaces, including # or %:. It "
        "removes space\n"
        "  after the marker; else/elif/endif align with their if.\n"
        "- AlignMacroBackslashes aligns existing whitespace-separated macro\n"
        "  continuations one column after the longest prefix (tab stops: 8).\n"
        "  Tokens and spliced identifiers, literals and comments stay intact.\n"
        "- Parameter-name and naming checks report issues; they never rename\n"
        "  code. Use name-case/prefix options and "
        "NamingNamespaceExemptions to\n"
        "  tune checks. Profiles approximate styles, not "
        "clang-format parity.\n",
        SafetyExamples},
};
// hit-fmt on

} // namespace hit_fmt
