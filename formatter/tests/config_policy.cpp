#include <formatter/config.hpp>
#include <formatter/policy.hpp>
#include <hit-fmt/config_template.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>

namespace {

std::filesystem::path ConfigData(std::string_view name)
{
    return std::filesystem::path(FORMATTER_TESTDATA_DIR).parent_path() /
        "config-policy" / (std::string(name) + ".hit-fmt");
}

struct TemporaryConfig
{
    std::filesystem::path Path = std::filesystem::temp_directory_path() /
        ("hit-fmt-policy-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()) +
            ".hit-fmt");

    explicit TemporaryConfig(std::string_view contents)
    {
        std::ofstream output(Path);
        output << contents;
        if (!output) {
            throw std::runtime_error("cannot write test config");
        }
    }

    ~TemporaryConfig()
    {
        std::error_code error;
        std::filesystem::remove(Path, error);
    }
};

TEST(ConfigPolicy, CompilesCategoryInheritanceWithoutSourceOwnedSentinels)
{
    auto style =
        formatter::LoadConfigFile(ConfigData("compiled")).FormattingStyle;
    const auto policies = formatter::CompilePolicies(style);
    EXPECT_EQ(policies.Calls.Layout, formatter::LayoutMode::Canonical);
    EXPECT_EQ(policies.Parameters.Layout, formatter::LayoutMode::Preserve);
    EXPECT_EQ(policies.Initializers.Layout, formatter::LayoutMode::Canonical);
    EXPECT_EQ(
        policies.Calls.Packing,
        formatter::ListPacking::VerticalWhenBroken);
    EXPECT_EQ(policies.Parameters.Packing, formatter::ListPacking::Fill);
    EXPECT_EQ(
        policies.Calls.Indent.Alignment,
        formatter::ContinuationAlignment::Delimiter);
    EXPECT_EQ(policies.Calls.Indent.ContinuationOffset, 4u);
    EXPECT_EQ(policies.Calls.Indent.FallbackOffset, 2u);
    EXPECT_EQ(policies.Parameters.Indent.ContinuationOffset, 3u);
    EXPECT_EQ(policies.ControlHeaders.Indent.FallbackOffset, 6u);
    EXPECT_EQ(policies.Bodies.Layout, formatter::LayoutMode::Preserve);
    EXPECT_EQ(policies.LambdaBodies.Layout, formatter::LayoutMode::Preserve);
    EXPECT_EQ(policies.LambdaBodies.Shape, formatter::BodyShape::CompactIfFits);
    EXPECT_EQ(
        policies.Expressions.TernaryShape,
        formatter::TernaryLayout::ThreePart);
    EXPECT_EQ(
        policies.InitializersTrailingComma,
        formatter::TrailingCommaPolicy::WhenMultiline);
    EXPECT_EQ(
        policies.Braces.Control,
        formatter::BracePlacement::NextLineIfHeaderBroken);
    EXPECT_EQ(
        policies.Indentation.AccessModifiers.Mode,
        formatter::BodyIndentation::Indent);
    EXPECT_EQ(policies.Indentation.AccessModifiers.Offset, 1u);
    EXPECT_EQ(
        policies.Indentation.Members.Mode,
        formatter::BodyIndentation::NoIndent);
    EXPECT_EQ(policies.Indentation.Members.Offset, 2u);
    EXPECT_EQ(
        policies.Indentation.CaseLabels.Mode,
        formatter::BodyIndentation::Indent);
    EXPECT_EQ(policies.Indentation.CaseLabels.Offset, 3u);
    EXPECT_EQ(
        policies.Indentation.StandaloneBraces.Mode,
        formatter::StandaloneBlockBraceIndentation::Indent);
    EXPECT_EQ(policies.Indentation.StandaloneBraces.Offset, 2u);
    EXPECT_EQ(
        policies.Indentation.StatementAttributes.Mode,
        formatter::StatementAttributeIndent::Indent);
    EXPECT_EQ(policies.Indentation.StatementAttributes.Offset, 4u);
    EXPECT_EQ(
        policies.PointerReferenceDeclaratorLocation,
        formatter::PointerReferenceDeclaratorLocation::Right);
    EXPECT_EQ(policies.IndentationKind, formatter::IndentationKind::Tabs);
    EXPECT_EQ(policies.TrailingWhitespace, formatter::TrailingWhitespace::Trim);
    EXPECT_EQ(policies.Calls.Break, formatter::BreakRequirement::IfNeeded);
    EXPECT_EQ(policies.Parameters.Break, formatter::BreakRequirement::Always);
    EXPECT_EQ(policies.Expressions.Break, formatter::BreakRequirement::Always);
    EXPECT_EQ(
        policies.Expressions.Packing,
        formatter::ListPacking::VerticalWhenBroken);
    EXPECT_EQ(policies.Chains.Break, formatter::BreakRequirement::Always);
    EXPECT_EQ(policies.Bodies.Shape, formatter::BodyShape::Expanded);
    EXPECT_EQ(policies.Bodies.EmptyShape, formatter::BodyShape::CompactIfFits);
    EXPECT_EQ(
        policies.LambdaBodies.EmptyShape,
        formatter::BodyShape::CompactIfFits);
    EXPECT_EQ(
        policies.Indentation.Namespaces.Mode,
        formatter::BodyIndentation::NoIndent);
    EXPECT_EQ(policies.Indentation.Namespaces.Offset, 2u);
    EXPECT_EQ(
        policies.Braces.Enum,
        formatter::BracePlacement::NextLineIfHeaderBroken);
    EXPECT_EQ(policies.Enumerators.Layout, formatter::LayoutMode::Canonical);
    EXPECT_EQ(policies.Enumerators.Break, formatter::BreakRequirement::Always);
    EXPECT_EQ(
        policies.Enumerators.Packing,
        formatter::ListPacking::VerticalWhenBroken);
    EXPECT_EQ(
        policies.Enumerators.FirstItem,
        formatter::FirstItemPlacement::NextLine);
    EXPECT_EQ(policies.Enumerators.Closer, formatter::CloserPlacement::OwnLine);
    EXPECT_EQ(
        policies.Enumerators.Indent.Alignment,
        formatter::ContinuationAlignment::Block);
    EXPECT_EQ(policies.Enumerators.Indent.ContinuationOffset, 2u);
    EXPECT_EQ(policies.Enumerators.Indent.FallbackOffset, 7u);
    EXPECT_EQ(
        policies.VerticalGaps.MemberDefinitionBlankLines.Mode,
        formatter::VerticalGapMode::Exact);
    EXPECT_EQ(policies.VerticalGaps.MemberDefinitionBlankLines.EmptyLines, 2u);
    EXPECT_TRUE(policies.VerticalGaps.MaxEmptyLines.Enabled);
    EXPECT_EQ(policies.VerticalGaps.MaxEmptyLines.EmptyLines, 2u);
    // Mutating the raw configuration cannot alter a reusable compiled snapshot.
    style.Layout = formatter::LayoutMode::Preserve;
    style.IndentWidth = 8;
    style.IndentAccessModifiers = formatter::BodyIndentation::NoIndent;
    style.AccessModifierIndentWidth = 0;
    style.IndentMembers = formatter::BodyIndentation::Indent;
    style.MemberIndentWidth = 5;
    style.IndentCaseLabels = formatter::BodyIndentation::NoIndent;
    style.CaseLabelIndentWidth = 0;
    style.StandaloneBlockBraceIndentation =
        formatter::StandaloneBlockBraceIndentation::Preserve;
    style.StatementAttributeIndent = formatter::StatementAttributeIndent::Block;
    style.StatementAttributeIndentWidth = 0;
    style.PointerReferenceDeclaratorLocation =
        formatter::PointerReferenceDeclaratorLocation::Left;
    style.IndentationKind = formatter::IndentationKind::Spaces;
    style.TrailingWhitespace = formatter::TrailingWhitespace::Preserve;
    style.ListBreak = formatter::BreakRequirement::IfNeeded;
    style.IndentNamespaces = formatter::BodyIndentation::Indent;
    style.NamespaceIndentWidth = 3;
    style.EmptyBodyShape = formatter::BodyShape::Expanded;
    style.EnumeratorsLayout = formatter::CategoryLayout::Preserve;
    style.DefinitionBlankLines = {formatter::BlankLineMode::Exact, 1};
    const auto changed = formatter::CompilePolicies(style);
    EXPECT_EQ(changed.Parameters.Break, formatter::BreakRequirement::IfNeeded);
    EXPECT_EQ(changed.Indentation.Namespaces.Offset, 3u);
    EXPECT_EQ(changed.Bodies.EmptyShape, formatter::BodyShape::Expanded);
    EXPECT_EQ(changed.LambdaBodies.EmptyShape, formatter::BodyShape::Expanded);
    EXPECT_EQ(changed.Enumerators.Layout, formatter::LayoutMode::Preserve);
    EXPECT_EQ(changed.Enumerators.Break, formatter::BreakRequirement::IfNeeded);
    EXPECT_EQ(changed.VerticalGaps.MemberDefinitionBlankLines.EmptyLines, 1u);
    EXPECT_EQ(policies.Parameters.Break, formatter::BreakRequirement::Always);
    EXPECT_EQ(policies.Indentation.Namespaces.Offset, 2u);
    EXPECT_EQ(policies.Bodies.EmptyShape, formatter::BodyShape::CompactIfFits);
    EXPECT_EQ(policies.Enumerators.Layout, formatter::LayoutMode::Canonical);
    EXPECT_EQ(policies.VerticalGaps.MemberDefinitionBlankLines.EmptyLines, 2u);
    EXPECT_EQ(
        changed.Indentation.AccessModifiers.Mode,
        formatter::BodyIndentation::NoIndent);
    EXPECT_EQ(changed.Indentation.AccessModifiers.Offset, 8u);
    EXPECT_EQ(
        changed.Indentation.Members.Mode,
        formatter::BodyIndentation::Indent);
    EXPECT_EQ(changed.Indentation.Members.Offset, 5u);
    EXPECT_EQ(
        changed.Indentation.CaseLabels.Mode,
        formatter::BodyIndentation::NoIndent);
    EXPECT_EQ(changed.Indentation.CaseLabels.Offset, 8u);
    EXPECT_EQ(
        changed.Indentation.StandaloneBraces.Mode,
        formatter::StandaloneBlockBraceIndentation::Preserve);
    EXPECT_EQ(changed.Indentation.StandaloneBraces.Offset, 8u);
    EXPECT_EQ(
        changed.Indentation.StatementAttributes.Mode,
        formatter::StatementAttributeIndent::Block);
    EXPECT_EQ(changed.Indentation.StatementAttributes.Offset, 8u);
    EXPECT_EQ(
        changed.PointerReferenceDeclaratorLocation,
        formatter::PointerReferenceDeclaratorLocation::Left);
    EXPECT_EQ(changed.IndentationKind, formatter::IndentationKind::Spaces);
    EXPECT_EQ(
        changed.TrailingWhitespace,
        formatter::TrailingWhitespace::Preserve);
    EXPECT_EQ(policies.Calls.Layout, formatter::LayoutMode::Canonical);
    EXPECT_EQ(policies.Calls.Indent.FallbackOffset, 2u);
    EXPECT_EQ(policies.Indentation.AccessModifiers.Offset, 1u);
    EXPECT_EQ(
        policies.Indentation.Members.Mode,
        formatter::BodyIndentation::NoIndent);
    EXPECT_EQ(policies.Indentation.Members.Offset, 2u);
    EXPECT_EQ(
        policies.Indentation.CaseLabels.Mode,
        formatter::BodyIndentation::Indent);
    EXPECT_EQ(policies.Indentation.CaseLabels.Offset, 3u);
    EXPECT_EQ(
        policies.Indentation.StandaloneBraces.Mode,
        formatter::StandaloneBlockBraceIndentation::Indent);
    EXPECT_EQ(policies.Indentation.StandaloneBraces.Offset, 2u);
    EXPECT_EQ(
        policies.Indentation.StatementAttributes.Mode,
        formatter::StatementAttributeIndent::Indent);
    EXPECT_EQ(policies.Indentation.StatementAttributes.Offset, 4u);
    EXPECT_EQ(
        policies.PointerReferenceDeclaratorLocation,
        formatter::PointerReferenceDeclaratorLocation::Right);
    EXPECT_EQ(policies.IndentationKind, formatter::IndentationKind::Tabs);
    EXPECT_EQ(policies.TrailingWhitespace, formatter::TrailingWhitespace::Trim);
    static_assert(std::is_const_v<decltype(policies.Calls)>);
    static_assert(std::is_const_v<decltype(policies.Indentation)>);
    static_assert(std::is_trivially_copyable_v<formatter::CompiledPolicies>);
}

TEST(ConfigPolicy, PreserveDefaultAndExplicitCanonicalOverrideAreDistinct)
{
    formatter::Style style;
    style.ExpressionsLayout = formatter::CategoryLayout::Canonical;
    const auto policies = formatter::CompilePolicies(style);
    EXPECT_EQ(policies.Calls.Layout, formatter::LayoutMode::Preserve);
    EXPECT_EQ(policies.Bodies.Layout, formatter::LayoutMode::Preserve);
    EXPECT_EQ(policies.DeclarationHeaders, formatter::LayoutMode::Preserve);
    EXPECT_EQ(policies.Calls.Break, formatter::BreakRequirement::IfNeeded);
    EXPECT_EQ(policies.Chains.Break, formatter::BreakRequirement::IfNeeded);
    EXPECT_EQ(
        policies.Expressions.Break,
        formatter::BreakRequirement::IfNeeded);
    EXPECT_EQ(policies.Expressions.Packing, formatter::ListPacking::Fill);
    EXPECT_EQ(policies.Bodies.EmptyShape, formatter::BodyShape::Expanded);
    EXPECT_EQ(policies.LambdaBodies.EmptyShape, formatter::BodyShape::Expanded);
    EXPECT_EQ(
        policies.Indentation.Namespaces.Mode,
        formatter::BodyIndentation::Preserve);
    EXPECT_EQ(policies.Indentation.Namespaces.Offset, 4u);
    EXPECT_EQ(policies.Braces.Enum, formatter::BracePlacement::Attached);
    EXPECT_EQ(policies.Enumerators.Layout, formatter::LayoutMode::Preserve);
    EXPECT_EQ(
        policies.Enumerators.Break,
        formatter::BreakRequirement::IfNeeded);
    EXPECT_EQ(
        policies.VerticalGaps.NamespaceBlankLinesAfterOpen.Mode,
        formatter::VerticalGapMode::Preserve);
    EXPECT_EQ(
        policies.VerticalGaps.MemberDefinitionBlankLines.Mode,
        formatter::VerticalGapMode::Preserve);
    EXPECT_FALSE(policies.VerticalGaps.MaxEmptyLines.Enabled);
    EXPECT_EQ(policies.Expressions.Layout, formatter::LayoutMode::Canonical);
    EXPECT_EQ(policies.Calls.Indent.ContinuationOffset, 4u);
    EXPECT_EQ(policies.Calls.Indent.FallbackOffset, 4u);
    EXPECT_EQ(policies.WidthDiagnostics, formatter::WidthDiagnosticPolicy::Off);
    EXPECT_EQ(
        policies.Indentation.AccessModifiers.Mode,
        formatter::BodyIndentation::Preserve);
    EXPECT_EQ(policies.Indentation.AccessModifiers.Offset, 4u);
    EXPECT_EQ(
        policies.Indentation.Members.Mode,
        formatter::BodyIndentation::Preserve);
    EXPECT_EQ(policies.Indentation.Members.Offset, 4u);
    EXPECT_EQ(
        policies.Indentation.CaseLabels.Mode,
        formatter::BodyIndentation::Preserve);
    EXPECT_EQ(policies.Indentation.CaseLabels.Offset, 4u);
    EXPECT_EQ(
        policies.Indentation.StandaloneBraces.Mode,
        formatter::StandaloneBlockBraceIndentation::Preserve);
    EXPECT_EQ(policies.Indentation.StandaloneBraces.Offset, 4u);
    EXPECT_EQ(
        policies.Indentation.StatementAttributes.Mode,
        formatter::StatementAttributeIndent::Preserve);
    EXPECT_EQ(policies.Indentation.StatementAttributes.Offset, 4u);
    EXPECT_EQ(
        policies.PointerReferenceDeclaratorLocation,
        formatter::PointerReferenceDeclaratorLocation::Preserve);
    EXPECT_EQ(policies.IndentationKind, formatter::IndentationKind::Preserve);
    EXPECT_EQ(
        policies.TrailingWhitespace,
        formatter::TrailingWhitespace::Preserve);
}

TEST(ConfigPolicy, IndependentFamiliesDoNotCrossInherit)
{
    formatter::Style style;
    style.ListBreak = formatter::BreakRequirement::Always;
    style.BodyShape = formatter::BodyShape::CompactIfFits;
    style.LambdaBodyShape = formatter::BodyShape::Expanded;
    const auto policies = formatter::CompilePolicies(style);
    EXPECT_EQ(policies.Calls.Break, formatter::BreakRequirement::Always);
    EXPECT_EQ(policies.Calls.Layout, formatter::LayoutMode::Preserve);
    EXPECT_EQ(policies.Chains.Break, formatter::BreakRequirement::IfNeeded);
    EXPECT_EQ(
        policies.Expressions.Break,
        formatter::BreakRequirement::IfNeeded);
    EXPECT_EQ(policies.Bodies.Shape, formatter::BodyShape::CompactIfFits);
    EXPECT_EQ(policies.Bodies.EmptyShape, formatter::BodyShape::Expanded);
    EXPECT_EQ(policies.LambdaBodies.Shape, formatter::BodyShape::Expanded);
    EXPECT_EQ(policies.LambdaBodies.EmptyShape, formatter::BodyShape::Expanded);
    style.EmptyBodyShape = formatter::BodyShape::CompactIfFits;
    const auto compactEmpty = formatter::CompilePolicies(style);
    EXPECT_EQ(compactEmpty.LambdaBodies.Shape, formatter::BodyShape::Expanded);
    EXPECT_EQ(
        compactEmpty.LambdaBodies.EmptyShape,
        formatter::BodyShape::CompactIfFits);
    style.LambdaEmptyBodyShape = formatter::BodyShape::Expanded;
    const auto overridden = formatter::CompilePolicies(style);
    EXPECT_EQ(
        overridden.Bodies.EmptyShape,
        formatter::BodyShape::CompactIfFits);
    EXPECT_EQ(
        overridden.LambdaBodies.EmptyShape,
        formatter::BodyShape::Expanded);
}

TEST(ConfigPolicy, VerticalCountsResolveZeroAndMemberOverrides)
{
    const TemporaryConfig config(
        "DefinitionBlankLines = 1\n"
        "MemberDefinitionBlankLines = 0\n"
        "NamespaceBlankLinesAfterOpen = 0\n"
        "NamespaceBlankLinesBeforeClose = Preserve\n"
        "MaxEmptyLines = 1\n");
    const auto style = formatter::LoadConfigFile(config.Path).FormattingStyle;
    const auto policies = formatter::CompilePolicies(style);
    EXPECT_EQ(policies.Layout, formatter::LayoutMode::Preserve);
    EXPECT_EQ(policies.VerticalGaps.DefinitionBlankLines.EmptyLines, 1u);
    EXPECT_EQ(
        policies.VerticalGaps.MemberDefinitionBlankLines.Mode,
        formatter::VerticalGapMode::Exact);
    EXPECT_EQ(policies.VerticalGaps.MemberDefinitionBlankLines.EmptyLines, 0u);
    EXPECT_EQ(
        policies.VerticalGaps.NamespaceBlankLinesAfterOpen.Mode,
        formatter::VerticalGapMode::Exact);
    EXPECT_EQ(
        policies.VerticalGaps.NamespaceBlankLinesBeforeClose.Mode,
        formatter::VerticalGapMode::Preserve);
    auto changed = style;
    changed.MemberDefinitionBlankLines = {};
    const auto explicitPreserve = formatter::CompilePolicies(changed);
    EXPECT_EQ(
        explicitPreserve.VerticalGaps.MemberDefinitionBlankLines.Mode,
        formatter::VerticalGapMode::Preserve);
    changed.MemberDefinitionBlankLines =
        formatter::BlankLineSetting::Inherited();
    const auto inherited = formatter::CompilePolicies(changed);
    EXPECT_EQ(inherited.VerticalGaps.MemberDefinitionBlankLines.EmptyLines, 1u);
    changed.DefinitionBlankLines = {};
    changed.MaxEmptyLines = 0;
    const auto zeroCap = formatter::CompilePolicies(changed);
    EXPECT_TRUE(zeroCap.VerticalGaps.MaxEmptyLines.Enabled);
    EXPECT_EQ(zeroCap.VerticalGaps.MaxEmptyLines.EmptyLines, 0u);
    EXPECT_EQ(
        zeroCap.VerticalGaps.MemberDefinitionBlankLines.Mode,
        formatter::VerticalGapMode::Preserve);
}

TEST(ConfigPolicy, VerticalBoundsCannotBeBypassedByProgrammaticStyles)
{
    for (const auto member : {
        &formatter::Style::NamespaceBlankLinesAfterOpen,
        &formatter::Style::NamespaceBlankLinesBeforeClose,
        &formatter::Style::AccessSectionBlankLinesBefore,
        &formatter::Style::AccessSectionBlankLinesAfterLabel,
        &formatter::Style::DefinitionBlankLines,
        &formatter::Style::MemberDefinitionBlankLines,
    })
    {
        formatter::Style style;
        style.*member = {formatter::BlankLineMode::Exact, 10001};
        EXPECT_THROW(
            (void)formatter::CompilePolicies(style),
            std::invalid_argument);
        style.*member = {formatter::BlankLineMode::Exact, 10000};
        style.MaxEmptyLines = 10000;
        EXPECT_NO_THROW((void)formatter::CompilePolicies(style));
        style.MaxEmptyLines = 9999;
        EXPECT_THROW(
            (void)formatter::CompilePolicies(style),
            std::invalid_argument);
    }
    formatter::Style style;
    style.MaxEmptyLines = 10001;
    EXPECT_THROW(
        (void)formatter::CompilePolicies(style),
        std::invalid_argument);
    style.MaxEmptyLines.reset();
    style.DefinitionBlankLines = formatter::BlankLineSetting::Inherited();
    EXPECT_THROW(
        (void)formatter::CompilePolicies(style),
        std::invalid_argument);
    style.DefinitionBlankLines =
        {static_cast<formatter::BlankLineMode>(255), 0};
    EXPECT_THROW(
        (void)formatter::CompilePolicies(style),
        std::invalid_argument);

    for (const auto text : {
        "NamespaceBlankLinesAfterOpen = -1\n",
        "NamespaceBlankLinesBeforeClose = Inherit\n",
        "AccessSectionBlankLinesBefore = 10001\n",
        "AccessSectionBlankLinesAfterLabel = 10001\n",
        "DefinitionBlankLines = 10001\n",
        "MemberDefinitionBlankLines = 10001\n",
        "MaxEmptyLines = 10001\n",
        "MaxEmptyLines = Inherit\n",
        "MaxEmptyLines = 9999999999999999999999999\n",
    })
    {
        SCOPED_TRACE(text);
        const TemporaryConfig invalid(text);
        EXPECT_THROW(
            (void)formatter::LoadConfigFile(invalid.Path),
            std::runtime_error);
    }
}

TEST(ConfigPolicy, CapConflictsUseFinalOverridesAndReportSettingLocations)
{
    const TemporaryConfig repaired(
        "DefinitionBlankLines = 2\nMaxEmptyLines = "
            "1\nMaxEmptyLines = 2\n");
    const auto good = formatter::LoadConfigFile(repaired.Path);
    EXPECT_EQ(formatter::CompilePolicies(good.FormattingStyle)
        .VerticalGaps.MemberDefinitionBlankLines.EmptyLines, 2u);
    const TemporaryConfig replaced(
        "MemberDefinitionBlankLines = "
            "2\nMaxEmptyLines = 1\n"
        "MemberDefinitionBlankLines = Preserve\n");
    EXPECT_NO_THROW((void)formatter::LoadConfigFile(replaced.Path));
    for (const auto key : {"NamespaceBlankLinesAfterOpen",
        "NamespaceBlankLinesBeforeClose",
        "AccessSectionBlankLinesBefore",
        "AccessSectionBlankLinesAfterLabel",
        "DefinitionBlankLines",
        "MemberDefinitionBlankLines",
    })
    {
        SCOPED_TRACE(key);
        const TemporaryConfig invalid(
            std::string(key) + " = 2\nMaxEmptyLines = 1\n");
        try {
            (void)formatter::LoadConfigFile(invalid.Path);
            FAIL() << "exact count above cap was accepted";
        } catch (const std::runtime_error& error) {
            const std::string message = error.what();
            EXPECT_NE(message.find(std::string(key) + " at " +
                invalid.Path.string() + ":1"), std::string::npos);
            EXPECT_NE(message.find("MaxEmptyLines at " +
                invalid.Path.string() + ":2"), std::string::npos);
            if (std::string_view(key) == "DefinitionBlankLines") {
                EXPECT_NE(
                    message.find("inherited from DefinitionBlankLines"),
                    std::string::npos);
            }
        }
    }
#define OFFSET_KEYS(category)                                              \
    #category "ContinuationOffset", #category "AlignmentFallbackOffset",
    for (const std::string_view key : {
        "ContinuationOffset", "AlignmentFallbackOffset",
        FORMATTER_LIST_CATEGORIES(OFFSET_KEYS)
        "AccessModifierIndentWidth",
        "MemberIndentWidth",
        "CaseLabelIndentWidth",
        "BlockBodyIndentWidth", "NamespaceIndentWidth",
        "StatementAttributeIndentWidth"})
    {
        SCOPED_TRACE(key);
        const auto similar = key == "ContinuationOffset"
            ? "CallsContinuationOffset"
            : key == "AlignmentFallbackOffset"
                ? "CallsAlignmentFallbackOffset"
                : key.ends_with("ContinuationOffset")
                    ? "ContinuationOffset"
                    : key.ends_with("AlignmentFallbackOffset")
                        ? "AlignmentFallbackOffset"
                        : "IndentWidth";
        const TemporaryConfig invalid(std::string(key) + " = 33\n" +
            similar + " = 4\n" + std::string(
                key) + " = 34\nColumnLimit = 80\n");
        try {
            (void)formatter::LoadConfigFile(invalid.Path);
            FAIL() << "invalid final offset was accepted";
        } catch (const std::runtime_error& error) {
            const std::string message = error.what();
            EXPECT_NE(message.find(std::string(key) +
                ": indentation offset must not exceed 32"), std::string::npos);
            EXPECT_NE(message.find("[" + std::string(key) + " at " +
                invalid.Path.string() + ":3]"), std::string::npos);
            EXPECT_EQ(message.find("[" + std::string(key) + " at " +
                invalid.Path.string() + ":1]"), std::string::npos);
            EXPECT_EQ(
                message.find("[" + std::string(similar) + " at "),
                std::string::npos);
            EXPECT_EQ(message.find("[ColumnLimit at "), std::string::npos);
        }
    }
#undef OFFSET_KEYS
}

TEST(ConfigPolicy, PreserveTemplateRoundTripsEveryRegisteredStyleDefault)
{
    std::ostringstream output;
    hit_fmt::PrintPreserveConfig(output);
    const auto text = output.str();
    const TemporaryConfig generated(text);
    const auto parsed = formatter::LoadConfigFile(generated.Path);
    const formatter::Style defaults;
#define CHECK_DEFAULT(type, name, initial)                                  \
    EXPECT_EQ(parsed.FormattingStyle.name, defaults.name) << #name;          \
    EXPECT_NE(text.find("\n" #name " = "), std::string::npos) << #name;
    FORMATTER_STYLE_OPTIONS(CHECK_DEFAULT)
#undef CHECK_DEFAULT
    EXPECT_NE(text.find("Preserve | 0..10000\nMaxEmptyLines = Preserve"),
        std::string::npos);
    const auto snapshot = formatter::CompilePolicies(parsed.FormattingStyle);
    EXPECT_EQ(snapshot.Enumerators.Layout, formatter::LayoutMode::Preserve);
    EXPECT_EQ(snapshot.Indentation.Namespaces.Offset, defaults.IndentWidth);
    EXPECT_EQ(
        snapshot.VerticalGaps.MemberDefinitionBlankLines.Mode,
        formatter::VerticalGapMode::Preserve);
}

TEST(ConfigPolicy, GeneratedGapsFollowOwningCombinatorNotTokenProvenance)
{
    using formatter::GapContext;
    using formatter::GapPolicy;
    using formatter::GapUsesSpace;
    for (const auto context : {GapContext::Preserved, GapContext::Rebuilt}) {
        EXPECT_FALSE(GapUsesSpace(GapPolicy::Preserve, context).has_value());
        EXPECT_EQ(GapUsesSpace(GapPolicy::Space, context), true);
        EXPECT_EQ(GapUsesSpace(GapPolicy::NoSpace, context), false);
    }
    EXPECT_FALSE(
        GapUsesSpace(GapPolicy::GeneratedSpace, GapContext::Preserved));
    EXPECT_FALSE(GapUsesSpace(
        GapPolicy::GeneratedNoSpace,
        GapContext::Preserved));
    EXPECT_EQ(
        GapUsesSpace(GapPolicy::GeneratedSpace, GapContext::Rebuilt),
        true);
    EXPECT_EQ(
        GapUsesSpace(GapPolicy::GeneratedNoSpace, GapContext::Rebuilt),
        false);

    formatter::Style style;
    style.CallsLayout = formatter::CategoryLayout::Canonical;
    const auto policies = formatter::CompilePolicies(style);
    // Rebuilding a canonical call owns its existing edge/separator gaps too.
    EXPECT_EQ(GapUsesSpace(
        policies.Spacing.SpaceInsideCallArguments,
        GapContext::Rebuilt), false);
    EXPECT_EQ(GapUsesSpace(
        policies.Spacing.CommaSpaceAfter,
        GapContext::Rebuilt), true);
    // A nested argument edit does not rebuild a preserved outer call's gaps.
    EXPECT_FALSE(GapUsesSpace(
        policies.Spacing.SpaceInsideCallArguments,
        GapContext::Preserved).has_value());
    EXPECT_FALSE(GapUsesSpace(
        policies.Spacing.CommaSpaceAfter,
        GapContext::Preserved).has_value());
}

TEST(ConfigPolicy, ProgrammaticStylesCannotBypassWidthAndOffsetLimits)
{
    formatter::Style style;
    style.IndentWidth = 0;
    EXPECT_THROW(
        (void)formatter::CompilePolicies(style),
        std::invalid_argument);
    style.IndentWidth = 32;
    style.ColumnLimit = 10001;
    EXPECT_THROW(
        (void)formatter::CompilePolicies(style),
        std::invalid_argument);
    style.ColumnLimit = 10000;
    style.CallsAlignmentFallbackOffset = 33;
    EXPECT_THROW(
        (void)formatter::CompilePolicies(style),
        std::invalid_argument);
    style.CallsAlignmentFallbackOffset = 32;
    style.NamespaceIndentWidth = 33;
    EXPECT_THROW(
        (void)formatter::CompilePolicies(style),
        std::invalid_argument);
    style.NamespaceIndentWidth = 0;
    style.EnumeratorsContinuationOffset = 33;
    EXPECT_THROW(
        (void)formatter::CompilePolicies(style),
        std::invalid_argument);
    style.EnumeratorsContinuationOffset = 0;
    const auto bounded = formatter::CompilePolicies(style);
    EXPECT_EQ(bounded.Indentation.Namespaces.Offset, 32u);
    EXPECT_EQ(bounded.Enumerators.Indent.ContinuationOffset, 32u);
    EXPECT_EQ(bounded.Indentation.AccessModifiers.Offset, 32u);
    EXPECT_EQ(bounded.Indentation.Members.Offset, 32u);
    EXPECT_EQ(bounded.Indentation.CaseLabels.Offset, 32u);
    EXPECT_EQ(bounded.Indentation.StandaloneBraces.Offset, 32u);
    EXPECT_EQ(bounded.Indentation.StatementAttributes.Offset, 32u);
}

TEST(ConfigPolicy, SchemaCompilesOwnedAnnotationsAndIndependentMetadata)
{
    const auto config = formatter::LoadConfigFile(ConfigData("compiled"));
    ASSERT_EQ(config.FormattingStyle.WhitespaceSensitiveMacros.size(), 2u);
    EXPECT_EQ(
        config.FormattingStyle.WhitespaceSensitiveMacros[0],
        "STRINGIZE_TEXT");
    ASSERT_EQ(config.FormattingStyle.CallLikeMacroNames.size(), 1u);
    EXPECT_EQ(config.FormattingStyle.CallLikeMacroNames[0], "REGISTER_NODE");
    EXPECT_EQ(
        config.FunctionDeclarationParameterNames,
        formatter::FunctionDeclarationParameterNames::Required);
    EXPECT_EQ(config.Naming.Class.Case, formatter::NameCase::PascalCase);
    ASSERT_EQ(config.Exclude.size(), 1u);
    EXPECT_TRUE(std::regex_search("generated/file.cpp", config.Exclude[0]));

    auto style = config.FormattingStyle;
    const auto annotations = formatter::CompileLibraryAnnotations(style);
    ASSERT_EQ(annotations.MessagePins().size(), 2u);
    EXPECT_EQ(annotations.MessagePins()[0].Name, "TRACE_MESSAGE");
    ASSERT_NE(annotations.FindMessagePin("WRITE_MESSAGE"), nullptr);
    EXPECT_EQ(
        annotations.FindMessagePin("WRITE_MESSAGE")->FirstArgument,
        formatter::FirstArgumentAnnotation::PinCompleteStringLiteral);
    EXPECT_EQ(annotations.FindMessagePin("WRITE_MESSAGE_IF"), nullptr);
    EXPECT_EQ(
        annotations.Chains(),
        formatter::ChainParticipation::AnnotatedRoots);
    EXPECT_TRUE(annotations.IsAnnotatedChainRoot("ns::BuildPipeline"));
    EXPECT_FALSE(annotations.IsAnnotatedChainRoot("BuildPipeline"));
    ASSERT_EQ(annotations.VisualScopes().size(), 2u);
    EXPECT_EQ(annotations.VisualScopes()[0].Begin, "StartList");
    EXPECT_EQ(annotations.VisualScopes()[0].End, "FinishSequence");
    ASSERT_NE(annotations.FindScopeBegin("StartMap"), nullptr);
    ASSERT_NE(annotations.FindScopeEnd("FinishEntries"), nullptr);
    EXPECT_EQ(annotations.FindScopeEnd("FinishEntries")->Begin, "StartMap");
    EXPECT_EQ(annotations.FindScopeBegin("StartMap")->End, "FinishEntries");
    EXPECT_EQ(annotations.FindScopeEnd("Finish"), nullptr);
    EXPECT_EQ(
        annotations.VisualScopes()[0].Layout,
        formatter::VisualScopeLayout::PreserveExistingExtraLevel);
    style.CallLineFirstStringNames.clear();
    style.ChainedCallRootNames.clear();
    style.IndentedCallScopePairs.clear();
    const auto empty = formatter::CompileLibraryAnnotations(style);
    EXPECT_EQ(empty.Chains(), formatter::ChainParticipation::AllSafe);
    EXPECT_TRUE(empty.MessagePins().empty());
    EXPECT_TRUE(empty.VisualScopes().empty());
    EXPECT_NE(annotations.FindMessagePin("WRITE_MESSAGE"), nullptr);
    EXPECT_TRUE(annotations.IsAnnotatedChainRoot("ns::BuildPipeline"));
    EXPECT_EQ(annotations.VisualScopes()[0].End, "FinishSequence");
    const auto copy = annotations;
    ASSERT_NE(copy.FindScopeEnd("FinishSequence"), nullptr);
    EXPECT_EQ(copy.FindScopeEnd("FinishSequence")->Begin, "StartList");

    for (const auto text : {
        "CallLineFirstStringNames = [\"Trace\", \"Trace\"]\n",
        "ChainedCallRootNames = [\"ns::Root\", \"ns::Root\"]\n",
        "ChainedCallRootNames = [\"ns::\"]\n",
        "IndentedCallScopePairs = [\"Start:End\", \"Start:Other\"]\n",
        "IndentedCallScopePairs = [\"Start:End\", \"End:Other\"]\n",
        "IndentedCallScopePairs = [\"Start:Start\"]\n",
        "IndentedCallScopePairs = [\"Start::End\"]\n",
    })
    {
        SCOPED_TRACE(text);
        const TemporaryConfig invalid(text);
        EXPECT_THROW(
            (void)formatter::LoadConfigFile(invalid.Path),
            std::runtime_error);
    }
    EXPECT_THROW(
        (void)formatter::LoadConfigFile(ConfigData("invalid-annotation")),
        std::runtime_error);
    style.CallLineFirstStringNames = {"Trace", "Trace"};
    EXPECT_THROW(
        (void)formatter::CompileLibraryAnnotations(style),
        std::invalid_argument);

    const auto yt = formatter::LoadConfigFile(
        std::filesystem::path(FORMATTER_TESTDATA_DIR) /
        "../../../../hit-fmt/examples/ytsaurus.hit-fmt");
    const auto ytAnnotations =
        formatter::CompileLibraryAnnotations(yt.FormattingStyle);
    EXPECT_EQ(ytAnnotations.MessagePins().size(), 17u);
    EXPECT_EQ(ytAnnotations.ChainRoots().size(), 15u);
    EXPECT_EQ(ytAnnotations.VisualScopes().size(), 3u);
    EXPECT_NE(ytAnnotations.FindMessagePin("THROW_ERROR_EXCEPTION"), nullptr);
    EXPECT_EQ(
        ytAnnotations.FindMessagePin("THROW_ERROR_EXCEPTION_IF"),
        nullptr);
    EXPECT_TRUE(ytAnnotations.IsAnnotatedChainRoot("BuildYsonFluently"));
}

TEST(ConfigPolicy, RemovedOptionsFailWithMigrationAdviceInsteadOfAliases)
{
    struct Case
    {
        std::string_view Name;
        std::string_view Advice;
    };
    for (const auto value : {
        Case{"invalid-enforce", "Enforce flags were removed"},
        Case{"invalid-merge", "canonical groups flatten"},
        Case{"invalid-width", "Soft feasibility probing was removed"},
        Case{"invalid-directive", "directives are byte-protected"},
        Case{"invalid-separator", "comment-separator rewriting is unsupported"},
    })
    {
        const auto path = ConfigData(value.Name);
        SCOPED_TRACE(path.string());
        try {
            (void)formatter::LoadConfigFile(path);
            FAIL() << "removed option was accepted";
        } catch (const std::runtime_error& error) {
            const std::string message = error.what();
            EXPECT_NE(message.find(":1:"), std::string::npos);
            EXPECT_NE(
                message.find("unknown or removed setting"),
                std::string::npos);
            EXPECT_NE(
                message.find("docs/config-migration.md"),
                std::string::npos);
            EXPECT_NE(message.find(value.Advice), std::string::npos);
        }
    }
}

TEST(ConfigPolicy, EmptyOnlyAndAlwaysMigrationAdviceIsNotAnAlias)
{
    struct Case
    {
        std::string_view Text;
        std::string_view Advice;
    };
    for (const auto value : {
        Case{"CollapseEmptyFunctionBodies = true\n", "EmptyBodyShape"},
        Case{"ExpressionLayoutCondition = Always\n", "ExpressionsBreak"},
        Case{"ChainedCallSplitCondition = Always\n", "ChainsLayout/Break"},
    })
    {
        const TemporaryConfig config(value.Text);
        try {
            (void)formatter::LoadConfigFile(config.Path);
            FAIL() << "legacy setting became a compatibility alias";
        } catch (const std::runtime_error& error) {
            EXPECT_NE(
                std::string(error.what()).find(value.Advice),
                std::string::npos);
        }
    }
}

TEST(ConfigPolicy, RemovedClosersAndInvalidValuesAreRejected)
{
    for (const auto key : {"CallArgumentSplitColumnLimit",
        "CallArgumentSplitColumnLimitNames",
    })
    {
        const auto value = std::string_view(key).ends_with("Names")
            ? "[\"EXPECT_THROW\"]"
            : "90";
        const TemporaryConfig config(std::string(key) + " = " + value + "\n");
        try {
            (void)formatter::LoadConfigFile(config.Path);
            FAIL() << "named call width option was accepted";
        } catch (const std::runtime_error& error) {
            EXPECT_NE(
                std::string(error.what()).find("global ColumnLimit"),
                std::string::npos);
        }
    }
    for (const auto key : {"BaseListsCloser", "ConstructorListsCloser"}) {
        SCOPED_TRACE(key);
        const TemporaryConfig config(std::string(key) + " = OwnLine\n");
        try {
            (void)formatter::LoadConfigFile(config.Path);
            FAIL() << "delimiterless closer option was accepted";
        } catch (const std::runtime_error& error) {
            const std::string message = error.what();
            EXPECT_NE(message.find(key), std::string::npos);
            EXPECT_NE(message.find("no closing delimiter"), std::string::npos);
            EXPECT_NE(message.find("ClassBracePlacement"), std::string::npos);
            EXPECT_NE(
                message.find("FunctionBracePlacement"),
                std::string::npos);
        }
    }
    for (const auto name : {"invalid-brace-value", "invalid-offset"}) {
        SCOPED_TRACE(name);
        EXPECT_THROW(
            (void)formatter::LoadConfigFile(ConfigData(name)),
            std::runtime_error);
    }
}

} // namespace
