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
    {"Opt in to canonical layout:",
        R"(Layout = Canonical
IndentWidth = 2
ColumnLimit = 80
WidthDiagnostics = Report
# Override any category with Inherit, Preserve or Canonical.
BaseListsLayout = Preserve
)"},
};
constexpr HelpExample ListExamples[] = {
    {"Shared list defaults and a category override:",
        R"(ListBreak = IfNeeded
ListPacking = Fill
ListFirstItem = InlineIfFits
ListCloser = SameLine
ListAlignment = Delimiter
ContinuationOffset = 4
AlignmentFallbackOffset = 4
# Under IfNeeded, broken calls are vertical and fitting calls stay flat.
CallsPacking = VerticalWhenBroken
# The Always override below requires an eligible owned break, even if fitting:
CallsBreak = Always
CallsFirstItem = NextLine
CallsCloser = OwnLine
CallsAlignment = Block
)"},
    {"Keep a sole argument's opening inline when it fits:",
        R"(CallsFirstItem = NextLine
CallsSoleArgumentFirstItem = InlineIfFits
# Use Inherit to follow CallsFirstItem (or its shared default).
# Multiple arguments retain CallsFirstItem; Always still requires an owned break.
)"},
};
constexpr HelpExample ExpressionExamples[] = {
    {"Syntax-based expressions and all safely "
        "parsed member chains:",
        R"(ExpressionsLayout = Canonical
ExpressionsBreak = IfNeeded
ExpressionsPacking = Fill
BinaryOperatorSide = Trailing
ShiftOperatorSide = Leading
AssignmentOperatorSide = Trailing
TernaryOperatorSide = Leading
TernaryLayout = ThreePart
TernaryOperatorIndentation = Align
ChainsLayout = Canonical
ChainsBreak = IfNeeded
ChainsPacking = Fill
ChainsAlignment = Block
)"},
};
constexpr HelpExample ShapeExamples[] = {
    {"Independent brace, body and comma decisions:",
        R"(FunctionBracePlacement = NextLine
ControlBracePlacement = NextLineIfHeaderBroken
BodiesLayout = Canonical
BodyShape = Expanded
EmptyBodyShape = CompactIfFits
LambdaBodiesLayout = Inherit
LambdaBodyShape = CompactIfFits
LambdaEmptyBodyShape = Inherit
IndentNamespaces = NoIndent
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
    {"Exact syntax-owned empty-line counts and an "
        "independent cap:",
        R"(NamespaceBlankLinesAfterOpen = 1
NamespaceBlankLinesBeforeClose = 1
AccessSectionBlankLinesBefore = 1
AccessSectionBlankLinesAfterLabel = 1
DeclarationBlankLines = 1
MemberDeclarationBlankLines = Preserve
DefinitionBlankLines = 1
MemberDefinitionBlankLines = 0
MaxEmptyLines = 1
# Default: Preserve for each count/cap, Inherit for member definitions.
# Zero removes surplus empty lines but does not itself change boundary topology.
)"},
};
constexpr HelpExample GapExamples[] = {
    {"The same typed values for all syntax "
        "spacing:",
        R"(FunctionSpaceBeforeParameters = NoSpace
ControlBlockKeywordSpacing = Space
CommaSpaceBefore = NoSpace
CommaSpaceAfter = Space
EmptyBlockInteriorSpacing = GeneratedNoSpace
ScopeResolutionSpacing = Preserve
FunctionRefQualifierSpacing = Space
FunctionCvRefQualifierSpacing = NoSpace
# CV/ref spacing defaults to Inherit; Preserve uses usual preservation rules.
# Generated* applies to properties rebuilt by their canonical owner.
# Existing adjacencies qualify; unrelated nested edits do not rebuild an owner.
)"},
};
constexpr HelpExample AnnotationExamples[] = {
    {"Explicit library layout annotations:",
        R"CONFIG(CallLineFirstStringNames = ["WRITE_MESSAGE"]
ChainedCallRootNames = ["factory", "pipeline::Builder"]
AlwaysBreakChainRootNames = ["WRITE_EVENT", "logging::Event"]
IndentedCallScopePairs = ["OpenItems:CloseItems"]
IndentedChainScopePairs = ["BeginMap:EndMap", "BeginList:EndList"]
StickyChainCallPairs = ["Item:Value", "Item:BeginMap", "registrar:Parameter"]
# Every call uses global ColumnLimit; there are no named-width overrides.
# Lowering selects generic policies; the renderer never matches identifiers.
)CONFIG"},
};
constexpr HelpExample SafetyExamples[] = {
    {"Semantic hints, explicit transforms and "
        "independent diagnostics:",
        R"CONFIG(HonorClangFormatDirectives = true
CallLikeMacroNames = ["REGISTER_NODE"]
WhitespaceSensitiveMacros = ["STRINGIZE_TEXT"]
AddMissingControlBraces = false
RemoveNullStatements = false
RemoveNullDeclarations = false
IncludeOrder = Preserve
NamespaceComments = Preserve
FunctionDeclarationParameterNames = Required
FunctionDefinitionParameterNames = NameOrCommentedNameRequired
ClassNameCase = PascalCase
ClassNamePrefixes = []
NamingNamespaceExemptions = ["std"]
Exclude = ["(^|/)generated(/|$)"]
)CONFIG"},
};

constexpr HelpExample StagedExamples[] = {
    {"Defaults for configuration with limited public-pipeline support:",
        R"(StandaloneCommentBlankLinesBefore = Preserve
StandaloneCommentBlankLinesAfter = Preserve
BreakAfterTemplateHeader = false
ExpressionsAlignment = Block
IncludeMatchingHeader = Off
IncludeGrouping = Preserve
IncludeGroupPrefixes = []
ConstructorBracePlacement = Inherit
# Template breaks, Operand, matching/regrouping and constructor overrides
# are rejected by public preparation. Inactive prefixes do not enable grouping.
)"},
};

constexpr HelpSection HelpSections[] = {
    {"Layout and width", "Layout defaults to Preserve: retain source "
        "break topology. "
        "Canonical selects fitting/broken groups; "
            "IfNeeded permits fitting groups to flatten. "
        "Always requires a legal owned break, even "
            "if a child is multiline, but cannot "
        "fabricate breaks in "
            "empty/indivisible/protected groups. "
            "Preserve/protection win. "
        "WidthDiagnostics is Off or Report; "
        "Report checks emitted line widths without a "
            "feasibility-format probe.\n", LayoutExamples},
    {"Lists and alignment", "Categories: Calls, Parameters, "
        "Initializers, Captures, "
        "ControlHeaders, BaseLists, "
            "ConstructorLists, Enumerators. Each accepts "
            "Layout, Break, Packing, "
        "FirstItem, Alignment, ContinuationOffset and AlignmentFallbackOffset. "
        "Closer is available only for delimiter-bearing categories, not "
        "BaseLists/ConstructorLists. Optional values default to Inherit. "
        "First/Closer apply only when broken. "
        "CallsSoleArgumentFirstItem overrides first-item placement for exactly "
        "one parsed argument, irrespective of expression kind; Inherit follows "
        "CallsFirstItem. Empty and multiple-argument calls are unaffected. "
        "InlineIfFits respects width, required breaks and preservation. "
        "Lambda bodies indent from their opening line's leading indentation, "
        "with the closing brace returning to that base; following arguments "
        "use the call's continuation alignment independently. "
        "Fill packs items; VerticalWhenBroken splits at item boundaries. "
        "Delimiter alignment uses the column after "
            "the opener; Block uses the block "
        "column plus offset. Zero offsets use "
            "IndentWidth. Scopes opened on the same line share one "
        "prospective continuation offset. A list whose owner starts on "
        "a continued line establishes a new level for its contents. "
        "Expressions still share the surrounding level. "
        "This line-based rule applies without nesting or context options. "
        "Structural bodies establish a "
        "fresh base, as do braced initializer and enum lists. "
        "Delimiter columns remain aligned; nesting controls "
        "their block-relative floor and fallback. Delimiter alignment falls "
        "back when the next atom plus suffix cannot fit.\n", ListExamples},
    {"Expressions and chains", "TernaryLayout is Flexible or ThreePart; "
        "ThreePart "
        "couples both breaks in a broken ternary and softly prefers that "
        "shape when an assignment initializer wraps. It "
        "alone does not force a fitting expression. "
        "TernaryOperatorIndentation is Align (default) or Indent. Align shares "
        "the surrounding continuation; Indent adds a continuation offset "
        "when the condition starts on an already-continued line. Condition "
        "continuations stay unchanged. For trailing operators it controls "
        "the following branch lines. Inline ternaries remain unchanged. "
        "Wrapped branch expressions share the delimiter line's continuation; "
        "calls in a branch still establish their own argument indentation. "
        "ExpressionsBreak and ChainsBreak default independently to IfNeeded. "
        "ChainsBreak = Always requires multiple suffix calls in a consecutive "
        "same-kind link run (. / -> / repeated invocation); switching kind "
        "resets the count. Receiver construction and argument calls do not "
        "count. Field paths and ordinary single-call chains break only when "
        "needed. AlwaysBreakChainRootNames can explicitly force conventional "
        "roots with even one suffix call, independently of ChainsBreak. "
        "ExpressionsPacking uses Fill or "
            "VerticalWhenBroken on precedence-aware "
            "owned "
        "operator sequences; operand subtrees stay child groups. "
        "Spines compose MemberExpression receiver "
            "and CallExpression callee links: "
        ". / -> member calls and repeated invocation "
            "factory()(arg)(next), including "
        "mixed spines. Nested argument calls are "
            "child groups, not chain links. "
        "Library annotations select reusable group "
            "policies.\n", ExpressionExamples},
    {"Output shape", "All brace placements accept Attached, NextLine or "
        "NextLineIfHeaderBroken. "
            "DeclarationHeadersLayout controls "
            "declaration headers; "
        "BodiesLayout and LambdaBodiesLayout control "
            "bodies. BodyShape and independent "
        "EmptyBodyShape default to Expanded; "
            "matching lambda shapes inherit separately. "
        "Empty means no interior tokens/comments "
            "after safe transforms; comment-only bodies "
        "are nonempty. Enums/lists/requirement "
            "bodies are excluded. IndentNamespaces "
        "uses Preserve/Indent/NoIndent; zero "
            "NamespaceIndentWidth uses IndentWidth, not "
        "zero indentation. NoIndent uses the "
            "enclosing rendered structural base. "
        "EnumBracePlacement observes the enum "
            "header, not enumerator brokenness; "
        "Enumerators uses shared list policies and "
            "leaves existing enum commas unchanged. "
        "Base/constructor lists "
        "use ColonSide and CommaSide (Leading/Trailing). Trailing commas are "
        "Preserve, Never or WhenMultiline, only at "
            "grammar-approved initializer sites; "
        "comma presence never determines closer placement.\n", ShapeExamples},
    {"Structural vertical gaps", "Exact counts and caps accept Preserve or "
        "0..10000. Canonical item/body boundaries retain authored blank lines "
        "unless an exact count or cap overrides them; list/header interiors "
        "still follow their layout policies. "
        "MemberDeclarationBlankLines and MemberDefinitionBlankLines also "
            "accept Inherit from their respective non-member settings. "
        "At broken boundaries, n empty lines mean "
            "n+1 newlines. Positive exact counts "
        "own a boundary break independently of "
            "surrounding Preserve; zero alone does not. "
        "Namespace edges apply only to nonempty "
            "content, outside attached comments. "
        "Access-before applies before subsequent "
            "explicit sections, not the first at "
            "opening; "
        "after-label applies only with following "
            "content. Declaration gaps separate complete neighboring "
            "declaration items in file, namespace and record scopes, including "
            "prototypes and variables and their attached comments. "
            "An exact definition count overrides the general declaration "
            "count at definition boundaries. Definition gaps separate "
            "neighboring "
        "complete declaration items if either is a "
            "function/record/enum definition; not two "
        "ordinary variables/forward declarations, "
            "lambdas, namespaces or access labels. "
        "Member counts replace, not add to, general "
            "counts. Comments stay attached; "
        "directives/protection and unattached "
            "comments are barriers. MaxEmptyLines only "
            "caps "
        "editable existing empty lines, never inline "
            "boundaries, mandatory terminators or "
        "token interiors. Exact/resolved counts "
            "above an explicit cap are rejected after "
        "overrides, irrespective of input/layout. No "
            "post-layout cleanup pass.\n", VerticalExamples},
    {"Syntax spacing", "All spacing fields use Preserve, Space, NoSpace, "
        "GeneratedSpace or GeneratedNoSpace. "
            "Generated* applies when the owning "
        "canonical combinator rebuilds the property, "
            "including existing adjacencies; "
        "inserted gaps always qualify. Unrelated nested edits do not activate "
        "preserved owners. Preserve retains source "
            "horizontal spelling on preserved or "
        "genuinely flat-only gaps, but canonical "
            "topology-changing gaps use syntax-safe flat "
        "fallback BEFORE fitting, even if selected "
            "flat. This distinction is structural, "
        "not final brokenness or text-history. "
        "Before/After keys control the two sides "
            "of colons, arrows and operator-overload boundaries independently. "
        "Lexical safety and comment boundaries "
            "override spacing. Use --generate-config "
        "for the complete option list and defaults, "
            "including indentation, declarators "
        "and keyword/delimiter spacing. FunctionCvRefQualifierSpacing controls "
        "the gap between const/volatile and a function's &/&& ref qualifier, "
        "not type references. It defaults to Inherit from "
        "FunctionRefQualifierSpacing; explicit Preserve uses ordinary "
        "gap-preservation rules rather than inheriting the generic policy.\n",
        GapExamples},
    {"Library layout annotations",
        "CallLineFirstStringNames softly pins only a "
        "complete "
        "string-literal first argument; Always "
            "outranks the pin when a legal owned break "
            "is needed. Empty ChainedCallRootNames means "
            "AllSafe; "
        "nonempty means AnnotatedRoots, independently gated by ChainsLayout. "
        "AlwaysBreakChainRootNames is an independent exact-name convention list, "
        "not a message-pin side effect or a replacement for root selection. "
        "It obeys ChainsLayout, root selection and protection. "
        "IndentedCallScopePairs preserves an "
            "existing visual extra level only for "
        "matched pairs in the same statement "
            "sequence/receiver/wrapper context; "
        "uncertain, unmatched and PP-crossing "
            "regions remain unchanged. IndentedChainScopePairs adds one "
        "IndentWidth inside matched, properly nested suffix-call pairs. "
        "StickyChainCallPairs keeps consecutive named suffix calls together "
        "when they fit, independently of ChainsBreak/ChainsPacking. "
        "Its first name may also match a simple root receiver or factory, "
        "so registrar:Parameter keeps registrar.Parameter together. "
        "Both use First:Second identifier pairs, default to empty, and obey "
        "ChainsLayout and ChainedCallRootNames; scopes require distinct, "
        "unambiguous endpoints, while sticky pairs may share names. Names/pairs "
        "are exact validated metadata, not "
            "heuristics or a matcher DSL. All calls "
        "use global ColumnLimit. Macro safety hints "
            "are separate.\n", AnnotationExamples},
    {"Configuration limits",
        "Public preparation rejects template breaks, Operand alignment, "
        "matching First, Regroup and constructor overrides. "
        "Low-level scalar compilation "
        "and owning prefix metadata may be used separately. Comment counts "
        "accept Preserve or 0..10000 and must fit MaxEmptyLines. Constructor "
        "Inherit resolves final FunctionBracePlacement once. IncludeGroupPrefixes "
        "is an ordered quoted array of normalized relative directory prefixes "
        "ending in /; duplicates, absolute paths, backslashes, empty, . and .. "
        "components are invalid. Earliest rule wins, not longest match. "
        "Regroup conflicts with explicit MaxEmptyLines=0 before unsupported-feature "
        "rejection. IncludeOrder defaults to Preserve. ColumnLimit "
        "accepts 1..10000; zero is invalid.\n",
        StagedExamples},
    {"Safety, transforms and checks",
        "Directives and protected bytes are never "
        "rewritten. hit-fmt off/on always protects "
            "regions; clang-format off/on is "
        "honored by default. Macro hints describe "
            "syntax/protection, not aesthetics. "
        "Transforms are explicitly opt-in and may "
            "not cross protected spans. Naming "
        "and parameter checks never rename code. "
            "Profiles approximate styles, not "
        "clang-format compatibility.\n", SafetyExamples},
};
// hit-fmt on

} // namespace hit_fmt
