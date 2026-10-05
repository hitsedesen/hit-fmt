#include "policy.hpp"

#include "formatter.hpp"
#include "include_policy.hpp"

#include <stdexcept>
#include <string>
#include <string_view>

namespace formatter {

namespace {

LayoutMode ResolveLayout(CategoryLayout category, LayoutMode parent)
{
    switch (category) {
        case CategoryLayout::Inherit:
            return parent;
        case CategoryLayout::Preserve:
            return LayoutMode::Preserve;
        case CategoryLayout::Canonical:
            return LayoutMode::Canonical;
    }
    throw std::invalid_argument("invalid category layout");
}

std::uint8_t ResolveOffset(std::size_t offset, std::size_t indentWidth)
{
    // CompilePolicies validates the raw size_t values before resolution.
    return static_cast<std::uint8_t>(offset == 0 ? indentWidth : offset);
}

void CheckOffset(std::size_t offset, std::string_view key)
{
    if (offset > 32) {
        throw std::invalid_argument(
            std::string(key) + ": indentation offset must not exceed 32");
    }
}

void CheckBlankSetting(
    BlankLineSetting setting,
    std::string_view key,
    bool allowInherit = false)
{
    if (setting.EmptyLines > 10000) {
        throw std::invalid_argument(
            std::string(key) + " must not exceed 10000");
    }
    if (setting.Mode == BlankLineMode::Inherit && !allowInherit) {
        throw std::invalid_argument(
            std::string(key) + " does not accept Inherit");
    }
    if (setting.Mode != BlankLineMode::Preserve &&
        setting.Mode != BlankLineMode::Inherit &&
        setting.Mode != BlankLineMode::Exact)
    {
        throw std::invalid_argument(
            std::string(key) + " has invalid blank-line mode");
    }
}

BlankLineSetting ResolveMemberBlankLines(const Style& style)
{
    return style.MemberDefinitionBlankLines.Mode == BlankLineMode::Inherit
        ? style.DefinitionBlankLines
        : style.MemberDefinitionBlankLines;
}

BlankLineSetting ResolveMemberDeclarationBlankLines(const Style& style)
{
    return style.MemberDeclarationBlankLines.Mode == BlankLineMode::Inherit
        ? style.DeclarationBlankLines
        : style.MemberDeclarationBlankLines;
}

void CheckBlankCap(
    BlankLineSetting setting,
    std::optional<std::size_t> cap,
    std::string_view key)
{
    if (cap && setting.Mode == BlankLineMode::Exact &&
        setting.EmptyLines > *cap)
    {
        throw std::invalid_argument(
            std::string(key) + " exceeds MaxEmptyLines");
    }
}

VerticalGapPolicy CompileBlankLines(BlankLineSetting setting)
{
    return setting.Mode == BlankLineMode::Exact
        ? VerticalGapPolicy{
            VerticalGapMode::Exact,
            static_cast<std::uint16_t>(setting.EmptyLines),
        }
        : VerticalGapPolicy{};
}

} // namespace

void ValidatePolicyStyle(const Style& style)
{
    if (style.IndentWidth == 0 || style.IndentWidth > 32) {
        throw std::invalid_argument("IndentWidth must be between 1 and 32");
    }
    if (style.LineEnding != LineEnding::Preserve &&
        style.LineEnding != LineEnding::LF &&
        style.LineEnding != LineEnding::CRLF)
    {
        throw std::invalid_argument("LineEnding has invalid value");
    }
    if (style.ColumnLimit > 10000) {
        throw std::invalid_argument("ColumnLimit must be between 0 and 10000");
    }
    if (style.RulerLength != 0 &&
        (style.RulerLength < 3 || style.RulerLength > 10000))
    {
        throw std::invalid_argument(
            "RulerLength must be 0 or between 3 and 10000");
    }
    CheckBlankSetting(style.RulerBlankLinesBefore, "RulerBlankLinesBefore");
    CheckBlankSetting(style.RulerBlankLinesAfter, "RulerBlankLinesAfter");
    CheckBlankCap(
        style.RulerBlankLinesBefore,
        style.MaxEmptyLines,
        "RulerBlankLinesBefore");
    CheckBlankCap(
        style.RulerBlankLinesAfter,
        style.MaxEmptyLines,
        "RulerBlankLinesAfter");
    if (static_cast<std::size_t>(style.AssignmentRhsPlacement) >=
        ConfigEnumNames<FirstItemPlacement>::Values.size())
    {
        throw std::invalid_argument("AssignmentRhsPlacement has invalid value");
    }
    if (style.DestructorBracePlacement &&
        static_cast<std::size_t>(*style.DestructorBracePlacement) >=
            ConfigEnumNames<BracePlacement>::Values.size())
    {
        throw std::invalid_argument(
            "DestructorBracePlacement has invalid value");
    }
    CheckOffset(style.ContinuationOffset, "ContinuationOffset");
    CheckOffset(style.AlignmentFallbackOffset, "AlignmentFallbackOffset");
#define CHECK_LIST_OFFSETS(category)                                        \
    CheckOffset(style.category##ContinuationOffset.value_or(0),              \
        #category "ContinuationOffset");                                    \
    CheckOffset(style.category##AlignmentFallbackOffset.value_or(0),         \
        #category "AlignmentFallbackOffset");
    FORMATTER_LIST_CATEGORIES(CHECK_LIST_OFFSETS)
#undef CHECK_LIST_OFFSETS
    if (style.CallsSoleArgumentFirstItem &&
        *style.CallsSoleArgumentFirstItem != FirstItemPlacement::InlineIfFits &&
        *style.CallsSoleArgumentFirstItem != FirstItemPlacement::NextLine)
    {
        throw std::invalid_argument(
            "CallsSoleArgumentFirstItem has invalid value");
    }
    CheckOffset(style.AccessModifierIndentWidth, "AccessModifierIndentWidth");
    CheckOffset(style.MemberIndentWidth, "MemberIndentWidth");
    CheckOffset(style.CaseLabelIndentWidth, "CaseLabelIndentWidth");
    CheckOffset(style.BlockBodyIndentWidth, "BlockBodyIndentWidth");
    CheckOffset(style.NamespaceIndentWidth, "NamespaceIndentWidth");
    CheckOffset(
        style.StatementAttributeIndentWidth,
        "StatementAttributeIndentWidth");
    CheckBlankSetting(
        style.NamespaceBlankLinesAfterOpen,
        "NamespaceBlankLinesAfterOpen");
    CheckBlankSetting(
        style.NamespaceBlankLinesBeforeClose,
        "NamespaceBlankLinesBeforeClose");
    CheckBlankSetting(
        style.AccessSectionBlankLinesBefore,
        "AccessSectionBlankLinesBefore");
    CheckBlankSetting(
        style.AccessSectionBlankLinesAfterLabel,
        "AccessSectionBlankLinesAfterLabel");
    CheckBlankSetting(style.DeclarationBlankLines, "DeclarationBlankLines");
    CheckBlankSetting(
        style.MemberDeclarationBlankLines,
        "MemberDeclarationBlankLines",
        true);
    CheckBlankSetting(style.DefinitionBlankLines, "DefinitionBlankLines");
    CheckBlankSetting(
        style.MemberDefinitionBlankLines,
        "MemberDefinitionBlankLines",
        true);
    CheckBlankSetting(
        style.StandaloneCommentBlankLinesBefore,
        "StandaloneCommentBlankLinesBefore");
    CheckBlankSetting(
        style.StandaloneCommentBlankLinesAfter,
        "StandaloneCommentBlankLinesAfter");
    if (style.MaxEmptyLines && *style.MaxEmptyLines > 10000) {
        throw std::invalid_argument("MaxEmptyLines must not exceed 10000");
    }
    CheckBlankCap(
        style.NamespaceBlankLinesAfterOpen,
        style.MaxEmptyLines,
        "NamespaceBlankLinesAfterOpen");
    CheckBlankCap(
        style.NamespaceBlankLinesBeforeClose,
        style.MaxEmptyLines,
        "NamespaceBlankLinesBeforeClose");
    CheckBlankCap(
        style.AccessSectionBlankLinesBefore,
        style.MaxEmptyLines,
        "AccessSectionBlankLinesBefore");
    CheckBlankCap(
        style.AccessSectionBlankLinesAfterLabel,
        style.MaxEmptyLines,
        "AccessSectionBlankLinesAfterLabel");
    CheckBlankCap(
        style.DeclarationBlankLines,
        style.MaxEmptyLines,
        "DeclarationBlankLines");
    CheckBlankCap(
        ResolveMemberDeclarationBlankLines(style),
        style.MaxEmptyLines,
        style.MemberDeclarationBlankLines.Mode == BlankLineMode::Inherit
        ? "MemberDeclarationBlankLines "
            "(inherited from DeclarationBlankLines)"
        : "MemberDeclarationBlankLines");
    CheckBlankCap(
        ResolveMemberBlankLines(style),
        style.MaxEmptyLines,
        style.MemberDefinitionBlankLines.Mode == BlankLineMode::Inherit
        ? "MemberDefinitionBlankLines (inherited from DefinitionBlankLines)"
        : "MemberDefinitionBlankLines");
    CheckBlankCap(
        style.DefinitionBlankLines,
        style.MaxEmptyLines,
        "DefinitionBlankLines");
    CheckBlankCap(
        style.StandaloneCommentBlankLinesBefore,
        style.MaxEmptyLines,
        "StandaloneCommentBlankLinesBefore");
    CheckBlankCap(
        style.StandaloneCommentBlankLinesAfter,
        style.MaxEmptyLines,
        "StandaloneCommentBlankLinesAfter");
    if (style.IncludeGrouping != IncludeGrouping::Preserve &&
        style.MaxEmptyLines && *style.MaxEmptyLines == 0)
    {
        throw std::invalid_argument("IncludeGrouping = " +
            std::string(SerializeConfigEnum(style.IncludeGrouping)) +
            " requires one empty line, exceeding " "MaxEmptyLines = 0");
    }
    ValidateIncludeGroupPrefixes(style.IncludeGroupPrefixes);
    if (style.ExpressionsAlignment != ExpressionAlignment::Block &&
        style.ExpressionsAlignment != ExpressionAlignment::Operand)
    {
        throw std::invalid_argument("ExpressionsAlignment has invalid value");
    }
    if (style.TernaryOperatorIndentation != TernaryOperatorIndentation::Align &&
        style.TernaryOperatorIndentation != TernaryOperatorIndentation::Indent)
    {
        throw std::invalid_argument(
            "TernaryOperatorIndentation has invalid value");
    }
    if (style.IncludeMatchingHeader != IncludeMatchingHeader::Off &&
        style.IncludeMatchingHeader != IncludeMatchingHeader::First)
    {
        throw std::invalid_argument("IncludeMatchingHeader has invalid value");
    }
    if (static_cast<std::size_t>(style.IncludeGrouping) >=
        ConfigEnumNames<IncludeGrouping>::Values.size())
    {
        throw std::invalid_argument("IncludeGrouping has invalid value");
    }
    if (static_cast<std::size_t>(style.IncludeDirectoryOrder) >=
        ConfigEnumNames<IncludeDirectoryOrder>::Values.size())
    {
        throw std::invalid_argument("IncludeDirectoryOrder has invalid value");
    }
    if (static_cast<std::size_t>(style.IncludePathBase) >=
        ConfigEnumNames<IncludePathBase>::Values.size())
    {
        throw std::invalid_argument("IncludePathBase has invalid value");
    }
    if (style.ConstructorBracePlacement &&
        *style.ConstructorBracePlacement != BracePlacement::Attached &&
        *style.ConstructorBracePlacement != BracePlacement::NextLine &&
        *style.ConstructorBracePlacement !=
            BracePlacement::NextLineIfHeaderBroken)
    {
        throw std::invalid_argument(
            "ConstructorBracePlacement has invalid value");
    }
    if (style.FunctionCvRefQualifierSpacing &&
        static_cast<std::size_t>(*style.FunctionCvRefQualifierSpacing) >=
            ConfigEnumNames<GapPolicy>::Values.size())
    {
        throw std::invalid_argument(
            "FunctionCvRefQualifierSpacing has invalid value");
    }
    if (style.ShortNamespaceLines > 10000) {
        throw std::invalid_argument(
            "ShortNamespaceLines must not exceed 10000");
    }
    if (style.TrailingCommentSpaces && style.TrailingCommentSpaces->Count &&
        *style.TrailingCommentSpaces->Count > 10000)
    {
        throw std::invalid_argument(
            "TrailingCommentSpaces must be between 0 and 10000");
    }
    if (style.NamespaceCommentSpaces == 0 || style.NamespaceCommentSpaces > 32)
    {
        throw std::invalid_argument(
            "NamespaceCommentSpaces must be between 1 and 32");
    }
}

CompiledPolicies CompilePolicies(const Style& style)
{
    ValidatePolicyStyle(style);
    const auto continuation =
        ResolveOffset(style.ContinuationOffset, style.IndentWidth);
    const auto fallback =
        ResolveOffset(style.AlignmentFallbackOffset, style.IndentWidth);
#define COMPILE_LIST(category, closer)                                      \
    const ListPolicy category{                                              \
        ResolveLayout(style.category##Layout, style.Layout),                 \
        style.category##Packing.value_or(style.ListPacking),                 \
        style.category##FirstItem.value_or(style.ListFirstItem),             \
        closer,                                                             \
        {style.category##Alignment.value_or(style.ListAlignment),            \
            ResolveOffset(style.category##ContinuationOffset.value_or(      \
                style.ContinuationOffset), style.IndentWidth),              \
            ResolveOffset(style.category##AlignmentFallbackOffset.value_or( \
                style.AlignmentFallbackOffset), style.IndentWidth)},         \
        style.category##Break.value_or(style.ListBreak)};
#define COMPILE_DELIMITED_LIST(category)                                     \
    COMPILE_LIST(category, style.category##Closer.value_or(style.ListCloser))
#define COMPILE_HEADER_LIST(category)                                        \
    COMPILE_LIST(category, CloserPlacement::SameLine)
    FORMATTER_DELIMITED_LIST_CATEGORIES(COMPILE_DELIMITED_LIST)
    FORMATTER_HEADER_LIST_CATEGORIES(COMPILE_HEADER_LIST)
#undef COMPILE_HEADER_LIST
#undef COMPILE_DELIMITED_LIST
#undef COMPILE_LIST
    const auto bodiesLayout = ResolveLayout(style.BodiesLayout, style.Layout);
    GapPolicies spacing;
#define COPY_GAP(type, name, initial) \
    spacing.name = ResolveGapPolicy( \
        style.name, style.FunctionRefQualifierSpacing);
    FORMATTER_GAP_OPTIONS(COPY_GAP)
#undef COPY_GAP
    return {
        .Layout = style.Layout,
        .IndentWidth = static_cast<std::uint8_t>(style.IndentWidth),
        .ColumnLimit = static_cast<std::uint16_t>(style.ColumnLimit),
        .WidthDiagnostics = style.WidthDiagnostics,
        .IndentationKind = style.IndentationKind,
        .LineEnding = style.LineEnding,
        .TrailingWhitespace = style.TrailingWhitespace,
        .PointerReferenceDeclaratorLocation =
            style.PointerReferenceDeclaratorLocation,
        .Indentation = {
            {style.IndentAccessModifiers, static_cast<std::uint8_t>(
                ResolveOffset(style.AccessModifierIndentWidth,
                    style.IndentWidth))},
            {style.IndentMembers, static_cast<std::uint8_t>(
                ResolveOffset(style.MemberIndentWidth, style.IndentWidth))},
            {style.IndentCaseLabels, static_cast<std::uint8_t>(
                ResolveOffset(style.CaseLabelIndentWidth, style.IndentWidth))},
            {style.StandaloneBlockBraceIndentation,
                static_cast<std::uint8_t>(style.IndentWidth)},
            {style.StatementAttributeIndent, static_cast<std::uint8_t>(
                ResolveOffset(style.StatementAttributeIndentWidth,
                    style.IndentWidth))},
            {style.IndentNamespaces, static_cast<std::uint8_t>(
                ResolveOffset(style.NamespaceIndentWidth, style.IndentWidth))}},
        .Calls = Calls,
        .CallsSoleArgumentFirstItem =
            style.CallsSoleArgumentFirstItem.value_or(Calls.FirstItem),
        .Parameters = Parameters,
        .TemplateParameters = TemplateParameters,
        .TemplateArguments = TemplateArguments,
        .Initializers = Initializers,
        .Captures = Captures,
        .ControlHeaders = ControlHeaders,
        .BaseLists = {BaseLists, style.BaseListsColonSide,
            style.BaseListsCommaSide},
        .ConstructorLists = {ConstructorLists, style.ConstructorListsColonSide,
            style.ConstructorListsCommaSide},
        .Expressions = {
            ResolveLayout(style.ExpressionsLayout, style.Layout),
            style.BinaryOperatorSide, style.AssignmentOperatorSide,
            style.ShiftOperatorSide, style.TernaryOperatorSide,
            style.TernaryLayout, style.TernaryOperatorIndentation,
            {ContinuationAlignment::Block, continuation, fallback},
            style.ExpressionsBreak, style.ExpressionsPacking,
            style.ExpressionsAlignment, style.AssignmentRhsPlacement},
        .Chains = {ResolveLayout(style.ChainsLayout, style.Layout),
            style.ChainsPacking,
            {style.ChainsAlignment, continuation, fallback}, style.ChainsBreak},
        .DeclarationHeaders =
            ResolveLayout(style.DeclarationHeadersLayout, style.Layout),
        .Bodies = {bodiesLayout, style.BodyShape,
            ResolveOffset(style.BlockBodyIndentWidth, style.IndentWidth),
            style.EmptyBodyShape},
        .LambdaBodies = {ResolveLayout(style.LambdaBodiesLayout, bodiesLayout),
            style.LambdaBodyShape.value_or(style.BodyShape),
            ResolveOffset(style.BlockBodyIndentWidth, style.IndentWidth),
            style.LambdaEmptyBodyShape.value_or(style.EmptyBodyShape)},
        .Braces = {style.ClassBracePlacement, style.StructBracePlacement,
            style.UnionBracePlacement, style.FunctionBracePlacement,
            style.LambdaBracePlacement, style.NamespaceBracePlacement,
            style.ControlBracePlacement, style.CaseBlockBracePlacement,
            style.EnumBracePlacement,
            style.ConstructorBracePlacement.value_or(
                style.FunctionBracePlacement),
            style.DestructorBracePlacement.value_or(
                style.FunctionBracePlacement)},
        .InitializersTrailingComma = style.InitializersTrailingComma,
        .Spacing = spacing,
        .TrailingCommentSpaces = style.TrailingCommentSpaces,
        .Enumerators = Enumerators,
        .VerticalGaps = {
            CompileBlankLines(style.NamespaceBlankLinesAfterOpen),
            CompileBlankLines(style.NamespaceBlankLinesBeforeClose),
            CompileBlankLines(style.AccessSectionBlankLinesBefore),
            CompileBlankLines(style.AccessSectionBlankLinesAfterLabel),
            CompileBlankLines(style.DeclarationBlankLines),
            CompileBlankLines(ResolveMemberDeclarationBlankLines(style)),
            CompileBlankLines(style.DefinitionBlankLines),
            CompileBlankLines(ResolveMemberBlankLines(style)),
            {style.MaxEmptyLines.has_value(), static_cast<std::uint16_t>(
                style.MaxEmptyLines.value_or(0))},
            CompileBlankLines(style.StandaloneCommentBlankLinesBefore),
            CompileBlankLines(style.StandaloneCommentBlankLinesAfter)},
        .BreakAfterTemplateHeader = style.BreakAfterTemplateHeader,
        .GenerateCallScopeIndentation = style.GenerateCallScopeIndentation,
        .RulerBlankLinesBefore = CompileBlankLines(style.RulerBlankLinesBefore),
        .RulerBlankLinesAfter = CompileBlankLines(style.RulerBlankLinesAfter),
        .Includes = {style.IncludeMatchingHeader, style.IncludeGrouping,
            style.IncludeDirectoryOrder, style.IncludePathBase},
    };
}

} // namespace formatter
