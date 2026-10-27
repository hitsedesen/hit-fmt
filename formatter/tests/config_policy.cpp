#include <formatter/config.hpp>
#include <formatter/include_policy.hpp>
#include <formatter/policy.hpp>

#include <gtest/gtest.h>

#include <hit-fmt/config_template.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace {

std::filesystem::path ConfigData(std::string_view name)
{
    return std::filesystem::path(FORMATTER_TESTDATA_DIR).parent_path() / "config-policy" /
        (std::string(name) + ".hit-fmt");
}

constexpr std::pair<std::string_view, formatter::BlankLineSetting formatter::Style::*> VerticalCountSettings[] = {
    {"NamespaceBlankLinesAfterOpen", &formatter::Style::NamespaceBlankLinesAfterOpen},
    {"NamespaceBlankLinesBeforeClose", &formatter::Style::NamespaceBlankLinesBeforeClose},
    {"AccessSectionBlankLinesBefore", &formatter::Style::AccessSectionBlankLinesBefore},
    {"AccessSectionBlankLinesAfterLabel", &formatter::Style::AccessSectionBlankLinesAfterLabel},
    {"DeclarationBlankLines", &formatter::Style::DeclarationBlankLines},
    {"MemberDeclarationBlankLines", &formatter::Style::MemberDeclarationBlankLines},
    {"DefinitionBlankLines", &formatter::Style::DefinitionBlankLines},
    {"MemberDefinitionBlankLines", &formatter::Style::MemberDefinitionBlankLines},
    {"StandaloneCommentBlankLinesBefore", &formatter::Style::StandaloneCommentBlankLinesBefore},
    {"StandaloneCommentBlankLinesAfter", &formatter::Style::StandaloneCommentBlankLinesAfter},
    {"RulerBlankLinesBefore", &formatter::Style::RulerBlankLinesBefore},
    {"RulerBlankLinesAfter", &formatter::Style::RulerBlankLinesAfter},
};

struct TemporaryConfig
{
    std::filesystem::path Path = std::filesystem::temp_directory_path() /
        ("hit-fmt-policy-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".hit-fmt");

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

struct TemporaryConfigTree
{
    std::filesystem::path Path = std::filesystem::temp_directory_path() /
        ("hit-fmt-includes-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));

    void Write(const std::filesystem::path& name, std::string_view contents)
    {
        std::filesystem::create_directories((Path / name).parent_path());
        std::ofstream output(Path / name);
        output << contents;
        if (!output) {
            throw std::runtime_error("cannot write test config");
        }
    }

    ~TemporaryConfigTree()
    {
        std::error_code error;
        std::filesystem::remove_all(Path, error);
    }
};

TEST(ConfigPolicy, IncludesMergeBeforeLocalSettingsAndValidateFinalValues)
{
    TemporaryConfigTree tree;
    tree.Write(
        "base",
        "ColumnLimit = 80\nIndentWidth = 2\n" "ClassNameCase = PascalCase\nExclude = [\"generated\"]\n"
        "CallLineFirstArgumentNames = [\"Trace\"]\n" "DefinitionBlankLines = 2\nMaxEmptyLines = 1\n");
    tree.Write("nested/middle", "include = \"../base\"\nIndentWidth = 3\n");
    for (const auto contents :
        {"ColumnLimit = 100\ninclude = \"nested/middle\"\n", "include = \"nested/middle\"\nColumnLimit = 100\n"})
    {
        tree.Write(
            "child",
            std::string(contents) + "MaxEmptyLines = 2\n" "Exclude = []\nCallLineFirstArgumentNames = [\"Log\"]\n");
        const auto config = formatter::LoadConfigFile(tree.Path / "child");
        EXPECT_EQ(config.FormattingStyle.ColumnLimit, 100u);
        EXPECT_EQ(config.FormattingStyle.IndentWidth, 3u);
        EXPECT_EQ(config.Naming.Class.Case, formatter::NameCase::PascalCase);
        EXPECT_TRUE(config.Exclude.empty());
        EXPECT_EQ(config.FormattingStyle.CallLineFirstArgumentNames, (std::vector<std::string>{"Log"}));
    }
    tree.Write("child", "include = \"" + (tree.Path / "base").generic_string() + "\"\nMaxEmptyLines = 2\n");
    EXPECT_EQ(formatter::LoadConfigFile(tree.Path / "child").FormattingStyle.IndentWidth, 2u);
    tree.Write("child", "include = \"base\"\n");
    try {
        (void)formatter::LoadConfigFile(tree.Path / "child");
        FAIL() << "inherited cap conflict was accepted";
    } catch (const std::runtime_error& error) {
        EXPECT_NE(
            std::string(error.what()).find("DefinitionBlankLines at " + (tree.Path / "base").string() + ":6"),
            std::string::npos);
    }
}

TEST(ConfigPolicy, RepositoryIncludesFindNearestGitOrArcRoot)
{
    for (const auto marker : {".git", ".arc"}) {
        for (const bool directory : {false, true}) {
            TemporaryConfigTree tree;
            tree.Write(".git", "gitdir: outer\n");
            tree.Write("base", "IndentWidth = 7\n");
            if (directory) {
                std::filesystem::create_directories(tree.Path / "repo" / marker);
            } else {
                tree.Write(std::filesystem::path("repo") / marker, "repository marker\n");
            }
            tree.Write("repo/base", "IndentWidth = 2\n");
            tree.Write("repo/nested/base", "include = \"@/base\"\nColumnLimit = 90\n");
            tree.Write("repo/nested/deeper/child", "include = \"../base\"\n");
            const auto style = formatter::LoadConfigFile(tree.Path / "repo/nested/deeper/child").FormattingStyle;
            EXPECT_EQ(style.IndentWidth, 2u);
            EXPECT_EQ(style.ColumnLimit, 90u);
        }
    }
    TemporaryConfigTree tree;
    tree.Write(".git", "gitdir: outer\n");
    tree.Write("settings", "IndentWidth = 7\n");
    tree.Write("configs/nested/.git", "gitdir: nested\n");
    tree.Write("configs/nested/settings", "IndentWidth = 2\n");
    tree.Write("configs/base", "include = \"@/settings\"\n");
    tree.Write("child", "include = \"configs/nested/../base\"\n");
    EXPECT_EQ(formatter::LoadConfigFile(tree.Path / "child").FormattingStyle.IndentWidth, 7u);
}

TEST(ConfigPolicy, InvalidIncludesReportIncludingFileAndLine)
{
    TemporaryConfigTree tree;
    tree.Write("cycle", "include = \"nested/../child\"\n");
    std::filesystem::create_directories(tree.Path / "nested");
    for (const auto value :
        {
            "unquoted",
            "[]",
            "\"\"",
            "\"missing\"",
            "\"@/missing\"",
            "\"cycle\"",
            "\"child\"",
            "\"cycle\"\ninclude = \"cycle\"",
        })
    {
        tree.Write("child", std::string("# config\ninclude = ") + value + "\n");
        try {
            (void)formatter::LoadConfigFile(tree.Path / "child");
            FAIL() << "invalid include was accepted: " << value;
        } catch (const std::runtime_error& error) {
            EXPECT_NE(std::string(error.what()).find((tree.Path / "child").string() + ":"), std::string::npos);
        }
    }
    for (size_t index = 0; index < 65; ++index) {
        tree.Write(
            std::to_string(index),
            index == 64 ? "IndentWidth = 2\n" : "include = \"" + std::to_string(index + 1) + "\"\n");
    }
    EXPECT_NO_THROW((void)formatter::LoadConfigFile(tree.Path / "1"));
    EXPECT_THROW((void)formatter::LoadConfigFile(tree.Path / "0"), std::runtime_error);
}

TEST(ConfigPolicy, CompilesCategoryInheritanceWithoutSourceOwnedSentinels)
{
    auto style = formatter::LoadConfigFile(ConfigData("compiled")).FormattingStyle;
    const auto policies = formatter::CompilePolicies(style);
    EXPECT_EQ(policies.Calls.Layout, formatter::LayoutMode::Canonical);
    EXPECT_EQ(policies.Parameters.Layout, formatter::LayoutMode::Preserve);
    EXPECT_EQ(policies.Initializers.Layout, formatter::LayoutMode::Canonical);
    EXPECT_EQ(policies.Calls.Packing, formatter::ListPacking::VerticalWhenBroken);
    EXPECT_EQ(policies.Parameters.Packing, formatter::ListPacking::Fill);
    EXPECT_EQ(policies.Calls.FirstItem, formatter::FirstItemPlacement::NextLine);
    EXPECT_EQ(policies.CallsSoleArgumentFirstItem, formatter::FirstItemPlacement::InlineIfFits);
    EXPECT_EQ(policies.Calls.Indent.Alignment, formatter::ContinuationAlignment::Delimiter);
    EXPECT_EQ(policies.Calls.Indent.ContinuationOffset, 4u);
    EXPECT_EQ(policies.Calls.Indent.FallbackOffset, 2u);
    EXPECT_EQ(policies.Parameters.Indent.ContinuationOffset, 3u);
    EXPECT_EQ(policies.ControlHeaders.Indent.FallbackOffset, 6u);
    EXPECT_EQ(policies.IfConditionsPacking, formatter::ListPacking::VerticalWhenBroken);
    EXPECT_EQ(policies.Bodies.Layout, formatter::LayoutMode::Preserve);
    EXPECT_EQ(policies.LambdaBodies.Layout, formatter::LayoutMode::Preserve);
    EXPECT_EQ(policies.LambdaBodies.Shape, formatter::BodyShape::CompactIfFits);
    EXPECT_EQ(policies.Expressions.TernaryShape, formatter::TernaryLayout::ThreePart);
    EXPECT_EQ(policies.InitializersTrailingComma, formatter::TrailingCommaPolicy::WhenMultiline);
    EXPECT_EQ(policies.Braces.Control, formatter::BracePlacement::NextLineIfHeaderBroken);
    EXPECT_EQ(policies.Indentation.AccessModifiers.Mode, formatter::BodyIndentation::Indent);
    EXPECT_EQ(policies.Indentation.AccessModifiers.Offset, 1u);
    EXPECT_EQ(policies.Indentation.Members.Mode, formatter::BodyIndentation::NoIndent);
    EXPECT_EQ(policies.Indentation.Members.Offset, 2u);
    EXPECT_EQ(policies.Indentation.CaseLabels.Mode, formatter::BodyIndentation::Indent);
    EXPECT_EQ(policies.Indentation.CaseLabels.Offset, 3u);
    EXPECT_EQ(policies.Indentation.StandaloneBraces.Mode, formatter::StandaloneBlockBraceIndentation::Indent);
    EXPECT_EQ(policies.Indentation.StandaloneBraces.Offset, 2u);
    EXPECT_EQ(policies.Indentation.StatementAttributes.Mode, formatter::StatementAttributeIndent::Indent);
    EXPECT_EQ(policies.Indentation.StatementAttributes.Offset, 4u);
    EXPECT_EQ(policies.PointerReferenceDeclaratorLocation, formatter::PointerReferenceDeclaratorLocation::Right);
    EXPECT_EQ(policies.IndentationKind, formatter::IndentationKind::Tabs);
    EXPECT_EQ(policies.TrailingWhitespace, formatter::TrailingWhitespace::Trim);
    EXPECT_EQ(policies.Calls.Break, formatter::BreakRequirement::IfNeeded);
    EXPECT_EQ(policies.Parameters.Break, formatter::BreakRequirement::Always);
    EXPECT_EQ(policies.Expressions.Break, formatter::BreakRequirement::Always);
    EXPECT_EQ(policies.Expressions.Packing, formatter::ListPacking::VerticalWhenBroken);
    EXPECT_EQ(policies.Chains.Break, formatter::BreakRequirement::Always);
    EXPECT_EQ(policies.Bodies.Shape, formatter::BodyShape::Expanded);
    EXPECT_EQ(policies.Bodies.EmptyShape, formatter::BodyShape::CompactIfFits);
    EXPECT_EQ(policies.LambdaBodies.EmptyShape, formatter::BodyShape::CompactIfFits);
    EXPECT_EQ(policies.Indentation.Namespaces.Mode, formatter::BodyIndentation::NoIndent);
    EXPECT_EQ(policies.Indentation.Namespaces.Offset, 2u);
    EXPECT_EQ(policies.Braces.Enum, formatter::BracePlacement::NextLineIfHeaderBroken);
    EXPECT_EQ(policies.Enumerators.Layout, formatter::LayoutMode::Canonical);
    EXPECT_EQ(policies.Enumerators.Break, formatter::BreakRequirement::Always);
    EXPECT_EQ(policies.Enumerators.Packing, formatter::ListPacking::VerticalWhenBroken);
    EXPECT_EQ(policies.Enumerators.FirstItem, formatter::FirstItemPlacement::NextLine);
    EXPECT_EQ(policies.Enumerators.Closer, formatter::CloserPlacement::OwnLine);
    EXPECT_EQ(policies.Enumerators.Indent.Alignment, formatter::ContinuationAlignment::Block);
    EXPECT_EQ(policies.Enumerators.Indent.ContinuationOffset, 2u);
    EXPECT_EQ(policies.Enumerators.Indent.FallbackOffset, 7u);
    EXPECT_EQ(policies.VerticalGaps.MemberDefinitionBlankLines.Mode, formatter::VerticalGapMode::Exact);
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
    style.StandaloneBlockBraceIndentation = formatter::StandaloneBlockBraceIndentation::Preserve;
    style.StatementAttributeIndent = formatter::StatementAttributeIndent::Block;
    style.StatementAttributeIndentWidth = 0;
    style.PointerReferenceDeclaratorLocation = formatter::PointerReferenceDeclaratorLocation::Left;
    style.IndentationKind = formatter::IndentationKind::Spaces;
    style.TrailingWhitespace = formatter::TrailingWhitespace::Preserve;
    style.ListBreak = formatter::BreakRequirement::IfNeeded;
    style.CallsSoleArgumentFirstItem = formatter::FirstItemPlacement::NextLine;
    style.IndentNamespaces = formatter::BodyIndentation::Indent;
    style.NamespaceIndentWidth = 3;
    style.EmptyBodyShape = formatter::BodyShape::Expanded;
    style.EnumeratorsLayout = formatter::CategoryLayout::Preserve;
    style.IfConditionsPacking = formatter::ListPacking::Fill;
    style.DefinitionBlankLines = {formatter::BlankLineMode::Exact, 1};
    const auto changed = formatter::CompilePolicies(style);
    EXPECT_EQ(changed.Parameters.Break, formatter::BreakRequirement::IfNeeded);
    EXPECT_EQ(changed.CallsSoleArgumentFirstItem, formatter::FirstItemPlacement::NextLine);
    EXPECT_EQ(policies.CallsSoleArgumentFirstItem, formatter::FirstItemPlacement::InlineIfFits);
    EXPECT_EQ(changed.Indentation.Namespaces.Offset, 3u);
    EXPECT_EQ(changed.Bodies.EmptyShape, formatter::BodyShape::Expanded);
    EXPECT_EQ(changed.LambdaBodies.EmptyShape, formatter::BodyShape::Expanded);
    EXPECT_EQ(changed.Enumerators.Layout, formatter::LayoutMode::Preserve);
    EXPECT_EQ(changed.IfConditionsPacking, formatter::ListPacking::Fill);
    EXPECT_EQ(policies.IfConditionsPacking, formatter::ListPacking::VerticalWhenBroken);
    EXPECT_EQ(changed.Enumerators.Break, formatter::BreakRequirement::IfNeeded);
    EXPECT_EQ(changed.VerticalGaps.MemberDefinitionBlankLines.EmptyLines, 1u);
    EXPECT_EQ(policies.Parameters.Break, formatter::BreakRequirement::Always);
    EXPECT_EQ(policies.Indentation.Namespaces.Offset, 2u);
    EXPECT_EQ(policies.Bodies.EmptyShape, formatter::BodyShape::CompactIfFits);
    EXPECT_EQ(policies.Enumerators.Layout, formatter::LayoutMode::Canonical);
    EXPECT_EQ(policies.VerticalGaps.MemberDefinitionBlankLines.EmptyLines, 2u);
    EXPECT_EQ(changed.Indentation.AccessModifiers.Mode, formatter::BodyIndentation::NoIndent);
    EXPECT_EQ(changed.Indentation.AccessModifiers.Offset, 8u);
    EXPECT_EQ(changed.Indentation.Members.Mode, formatter::BodyIndentation::Indent);
    EXPECT_EQ(changed.Indentation.Members.Offset, 5u);
    EXPECT_EQ(changed.Indentation.CaseLabels.Mode, formatter::BodyIndentation::NoIndent);
    EXPECT_EQ(changed.Indentation.CaseLabels.Offset, 8u);
    EXPECT_EQ(changed.Indentation.StandaloneBraces.Mode, formatter::StandaloneBlockBraceIndentation::Preserve);
    EXPECT_EQ(changed.Indentation.StandaloneBraces.Offset, 8u);
    EXPECT_EQ(changed.Indentation.StatementAttributes.Mode, formatter::StatementAttributeIndent::Block);
    EXPECT_EQ(changed.Indentation.StatementAttributes.Offset, 8u);
    EXPECT_EQ(changed.PointerReferenceDeclaratorLocation, formatter::PointerReferenceDeclaratorLocation::Left);
    EXPECT_EQ(changed.IndentationKind, formatter::IndentationKind::Spaces);
    EXPECT_EQ(changed.TrailingWhitespace, formatter::TrailingWhitespace::Preserve);
    EXPECT_EQ(policies.Calls.Layout, formatter::LayoutMode::Canonical);
    EXPECT_EQ(policies.Calls.Indent.FallbackOffset, 2u);
    EXPECT_EQ(policies.Indentation.AccessModifiers.Offset, 1u);
    EXPECT_EQ(policies.Indentation.Members.Mode, formatter::BodyIndentation::NoIndent);
    EXPECT_EQ(policies.Indentation.Members.Offset, 2u);
    EXPECT_EQ(policies.Indentation.CaseLabels.Mode, formatter::BodyIndentation::Indent);
    EXPECT_EQ(policies.Indentation.CaseLabels.Offset, 3u);
    EXPECT_EQ(policies.Indentation.StandaloneBraces.Mode, formatter::StandaloneBlockBraceIndentation::Indent);
    EXPECT_EQ(policies.Indentation.StandaloneBraces.Offset, 2u);
    EXPECT_EQ(policies.Indentation.StatementAttributes.Mode, formatter::StatementAttributeIndent::Indent);
    EXPECT_EQ(policies.Indentation.StatementAttributes.Offset, 4u);
    EXPECT_EQ(policies.PointerReferenceDeclaratorLocation, formatter::PointerReferenceDeclaratorLocation::Right);
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
    // Explicit packing is still only a shape preference, not layout activation.
    style.CallsPacking = formatter::ListPacking::Fill;
    style.IfConditionsPacking = formatter::ListPacking::VerticalWhenBroken;
    const auto explicitPacking = formatter::CompilePolicies(style);
    EXPECT_EQ(explicitPacking.Calls.Layout, policies.Calls.Layout);
    EXPECT_EQ(explicitPacking.Calls.Packing, formatter::ListPacking::Fill);
    EXPECT_EQ(policies.IfConditionsPacking, formatter::ListPacking::Fill);
    EXPECT_EQ(explicitPacking.ControlHeaders.Layout, formatter::LayoutMode::Preserve);
    EXPECT_EQ(policies.Bodies.Layout, formatter::LayoutMode::Preserve);
    EXPECT_EQ(policies.DeclarationHeaders, formatter::LayoutMode::Preserve);
    EXPECT_EQ(policies.Calls.Break, formatter::BreakRequirement::IfNeeded);
    EXPECT_EQ(policies.Chains.Break, formatter::BreakRequirement::IfNeeded);
    EXPECT_EQ(policies.Expressions.Break, formatter::BreakRequirement::IfNeeded);
    EXPECT_EQ(policies.Expressions.Packing, formatter::ListPacking::Fill);
    EXPECT_EQ(policies.Bodies.EmptyShape, formatter::BodyShape::Expanded);
    EXPECT_EQ(policies.LambdaBodies.EmptyShape, formatter::BodyShape::Expanded);
    EXPECT_EQ(policies.Indentation.Namespaces.Mode, formatter::BodyIndentation::Preserve);
    EXPECT_EQ(policies.Indentation.Namespaces.Offset, 4u);
    EXPECT_EQ(policies.Braces.Enum, formatter::BracePlacement::Attached);
    EXPECT_EQ(policies.Enumerators.Layout, formatter::LayoutMode::Preserve);
    EXPECT_EQ(policies.Enumerators.Break, formatter::BreakRequirement::IfNeeded);
    EXPECT_EQ(policies.VerticalGaps.NamespaceBlankLinesAfterOpen.Mode, formatter::VerticalGapMode::Preserve);
    EXPECT_EQ(policies.VerticalGaps.MemberDefinitionBlankLines.Mode, formatter::VerticalGapMode::Preserve);
    EXPECT_FALSE(policies.VerticalGaps.MaxEmptyLines.Enabled);
    EXPECT_EQ(policies.Expressions.Layout, formatter::LayoutMode::Canonical);
    EXPECT_EQ(policies.Calls.Indent.ContinuationOffset, 4u);
    EXPECT_EQ(policies.Calls.Indent.FallbackOffset, 4u);
    EXPECT_EQ(policies.WidthDiagnostics, formatter::WidthDiagnosticPolicy::Off);
    EXPECT_EQ(policies.Indentation.AccessModifiers.Mode, formatter::BodyIndentation::Preserve);
    EXPECT_EQ(policies.Indentation.AccessModifiers.Offset, 4u);
    EXPECT_EQ(policies.Indentation.Members.Mode, formatter::BodyIndentation::Preserve);
    EXPECT_EQ(policies.Indentation.Members.Offset, 4u);
    EXPECT_EQ(policies.Indentation.CaseLabels.Mode, formatter::BodyIndentation::Preserve);
    EXPECT_EQ(policies.Indentation.CaseLabels.Offset, 4u);
    EXPECT_EQ(policies.Indentation.StandaloneBraces.Mode, formatter::StandaloneBlockBraceIndentation::Preserve);
    EXPECT_EQ(policies.Indentation.StandaloneBraces.Offset, 4u);
    EXPECT_EQ(policies.Indentation.StatementAttributes.Mode, formatter::StatementAttributeIndent::Preserve);
    EXPECT_EQ(policies.Indentation.StatementAttributes.Offset, 4u);
    EXPECT_EQ(policies.PointerReferenceDeclaratorLocation, formatter::PointerReferenceDeclaratorLocation::Preserve);
    EXPECT_EQ(policies.IndentationKind, formatter::IndentationKind::Preserve);
    EXPECT_EQ(policies.TrailingWhitespace, formatter::TrailingWhitespace::Preserve);
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
    EXPECT_EQ(policies.Expressions.Break, formatter::BreakRequirement::IfNeeded);
    EXPECT_EQ(policies.Bodies.Shape, formatter::BodyShape::CompactIfFits);
    EXPECT_EQ(policies.Bodies.EmptyShape, formatter::BodyShape::Expanded);
    EXPECT_EQ(policies.LambdaBodies.Shape, formatter::BodyShape::Expanded);
    EXPECT_EQ(policies.LambdaBodies.EmptyShape, formatter::BodyShape::Expanded);
    style.EmptyBodyShape = formatter::BodyShape::CompactIfFits;
    const auto compactEmpty = formatter::CompilePolicies(style);
    EXPECT_EQ(compactEmpty.LambdaBodies.Shape, formatter::BodyShape::Expanded);
    EXPECT_EQ(compactEmpty.LambdaBodies.EmptyShape, formatter::BodyShape::CompactIfFits);
    style.LambdaEmptyBodyShape = formatter::BodyShape::Expanded;
    const auto overridden = formatter::CompilePolicies(style);
    EXPECT_EQ(overridden.Bodies.EmptyShape, formatter::BodyShape::CompactIfFits);
    EXPECT_EQ(overridden.LambdaBodies.EmptyShape, formatter::BodyShape::Expanded);
}

TEST(ConfigPolicy, SoleArgumentPlacementInheritsFinalCallsFirstItem)
{
    for (const auto firstItem : {"InlineIfFits", "NextLine"}) {
        SCOPED_TRACE(firstItem);
        for (const auto inherited :
            {"", "CallsSoleArgumentFirstItem = InlineIfFits\n" "CallsSoleArgumentFirstItem = Inherit\n"})
        {
            const TemporaryConfig config(
                std::string("ListFirstItem = NextLine\nCallsFirstItem = ") + firstItem + "\n" + inherited);
            const auto style = formatter::LoadConfigFile(config.Path).FormattingStyle;
            EXPECT_FALSE(style.CallsSoleArgumentFirstItem);
            const auto policies = formatter::CompilePolicies(style);
            EXPECT_EQ(policies.CallsSoleArgumentFirstItem, policies.Calls.FirstItem);
            EXPECT_EQ(policies.Parameters.FirstItem, formatter::FirstItemPlacement::NextLine);
        }
    }
}

TEST(ConfigPolicy, VerticalCountsResolveZeroAndMemberOverrides)
{
    const TemporaryConfig config("DeclarationBlankLines = 1\n" "MemberDeclarationBlankLines = 0\n"
        "DefinitionBlankLines = 1\n" "MemberDefinitionBlankLines = 0\n" "NamespaceBlankLinesAfterOpen = 0\n"
        "NamespaceBlankLinesBeforeClose = Preserve\n" "MaxEmptyLines = 1\n");
    const auto style = formatter::LoadConfigFile(config.Path).FormattingStyle;
    const auto policies = formatter::CompilePolicies(style);
    EXPECT_EQ(policies.Layout, formatter::LayoutMode::Preserve);
    EXPECT_EQ(policies.VerticalGaps.DeclarationBlankLines.EmptyLines, 1u);
    EXPECT_EQ(policies.VerticalGaps.MemberDeclarationBlankLines.Mode, formatter::VerticalGapMode::Exact);
    EXPECT_EQ(policies.VerticalGaps.MemberDeclarationBlankLines.EmptyLines, 0u);
    EXPECT_EQ(policies.VerticalGaps.DefinitionBlankLines.EmptyLines, 1u);
    EXPECT_EQ(policies.VerticalGaps.MemberDefinitionBlankLines.Mode, formatter::VerticalGapMode::Exact);
    EXPECT_EQ(policies.VerticalGaps.MemberDefinitionBlankLines.EmptyLines, 0u);
    EXPECT_EQ(policies.VerticalGaps.NamespaceBlankLinesAfterOpen.Mode, formatter::VerticalGapMode::Exact);
    EXPECT_EQ(policies.VerticalGaps.NamespaceBlankLinesBeforeClose.Mode, formatter::VerticalGapMode::Preserve);
    auto changed = style;
    changed.MemberDefinitionBlankLines = {};
    const auto explicitPreserve = formatter::CompilePolicies(changed);
    EXPECT_EQ(explicitPreserve.VerticalGaps.MemberDefinitionBlankLines.Mode, formatter::VerticalGapMode::Preserve);
    changed.MemberDefinitionBlankLines = formatter::BlankLineSetting::Inherited();
    const auto inherited = formatter::CompilePolicies(changed);
    EXPECT_EQ(inherited.VerticalGaps.MemberDefinitionBlankLines.EmptyLines, 1u);
    changed.MemberDeclarationBlankLines = formatter::BlankLineSetting::Inherited();
    EXPECT_EQ(formatter::CompilePolicies(changed).VerticalGaps.MemberDeclarationBlankLines.EmptyLines, 1u);
    changed.MemberDeclarationBlankLines = {};
    EXPECT_EQ(
        formatter::CompilePolicies(changed).VerticalGaps.MemberDeclarationBlankLines.Mode,
        formatter::VerticalGapMode::Preserve);
    changed.DeclarationBlankLines = {};
    changed.DefinitionBlankLines = {};
    changed.MaxEmptyLines = 0;
    const auto zeroCap = formatter::CompilePolicies(changed);
    EXPECT_TRUE(zeroCap.VerticalGaps.MaxEmptyLines.Enabled);
    EXPECT_EQ(zeroCap.VerticalGaps.MaxEmptyLines.EmptyLines, 0u);
    EXPECT_EQ(zeroCap.VerticalGaps.MemberDefinitionBlankLines.Mode, formatter::VerticalGapMode::Preserve);
}

TEST(ConfigPolicy, VerticalBoundsCannotBeBypassedByProgrammaticStyles)
{
    for (const auto& [key, member] : VerticalCountSettings) {
        SCOPED_TRACE(key);
        formatter::Style style;
        style.*member = {formatter::BlankLineMode::Exact, 10001};
        EXPECT_THROW((void)formatter::CompilePolicies(style), std::invalid_argument);
        style.*member = {formatter::BlankLineMode::Exact, 10000};
        style.MaxEmptyLines = 10000;
        EXPECT_NO_THROW((void)formatter::CompilePolicies(style));
        style.MaxEmptyLines = 9999;
        EXPECT_THROW((void)formatter::CompilePolicies(style), std::invalid_argument);
    }
    formatter::Style style;
    style.MaxEmptyLines = 10001;
    EXPECT_THROW((void)formatter::CompilePolicies(style), std::invalid_argument);
    style.MaxEmptyLines.reset();
    style.DefinitionBlankLines = formatter::BlankLineSetting::Inherited();
    EXPECT_THROW((void)formatter::CompilePolicies(style), std::invalid_argument);
    style.DefinitionBlankLines = {static_cast<formatter::BlankLineMode>(255), 0};
    EXPECT_THROW((void)formatter::CompilePolicies(style), std::invalid_argument);

    for (const auto text :
        {
            "NamespaceBlankLinesAfterOpen = -1\n",
            "NamespaceBlankLinesBeforeClose = Inherit\n",
            "AccessSectionBlankLinesBefore = 10001\n",
            "AccessSectionBlankLinesAfterLabel = 10001\n",
            "DeclarationBlankLines = 10001\n",
            "DeclarationBlankLines = Inherit\n",
            "MemberDeclarationBlankLines = 10001\n",
            "DefinitionBlankLines = 10001\n",
            "MemberDefinitionBlankLines = 10001\n",
            "RulerBlankLinesBefore = Inherit\n",
            "RulerBlankLinesAfter = 10001\n",
            "RulerLength = 2\n",
            "RulerLength = 10001\n",
            "AssignmentRhsPlacement = Unsupported\n",
            "AssignmentsPacking = Unsupported\n",
            "DestructorBracePlacement = Unsupported\n",
            "MaxEmptyLines = 10001\n",
            "MaxEmptyLines = Inherit\n",
            "MaxEmptyLines = 9999999999999999999999999\n",
        })
    {
        SCOPED_TRACE(text);
        const TemporaryConfig invalid(text);
        EXPECT_THROW((void)formatter::LoadConfigFile(invalid.Path), std::runtime_error);
    }
}

TEST(ConfigPolicy, CapConflictsUseFinalOverridesAndReportSettingLocations)
{
    const TemporaryConfig repaired("DefinitionBlankLines = 2\nMaxEmptyLines = " "1\nMaxEmptyLines = 2\n");
    const auto good = formatter::LoadConfigFile(repaired.Path);
    EXPECT_EQ(formatter::CompilePolicies(good.FormattingStyle).VerticalGaps.MemberDefinitionBlankLines.EmptyLines, 2u);
    const TemporaryConfig replaced(
        "MemberDefinitionBlankLines = " "2\nMaxEmptyLines = 1\n" "MemberDefinitionBlankLines = Preserve\n");
    EXPECT_NO_THROW((void)formatter::LoadConfigFile(replaced.Path));
    for (const auto& setting : VerticalCountSettings) {
        const auto key = setting.first;
        SCOPED_TRACE(key);
        const TemporaryConfig invalid(std::string(key) + " = 2\nMaxEmptyLines = 1\n");
        try {
            (void)formatter::LoadConfigFile(invalid.Path);
            FAIL() << "exact count above cap was accepted";
        } catch (const std::runtime_error& error) {
            const std::string message = error.what();
            EXPECT_NE(message.find(std::string(key) + " at " + invalid.Path.string() + ":1"), std::string::npos);
            EXPECT_NE(message.find("MaxEmptyLines at " + invalid.Path.string() + ":2"), std::string::npos);
            if (std::string_view(key) == "DefinitionBlankLines") {
                EXPECT_NE(message.find("inherited from DefinitionBlankLines"), std::string::npos);
            }
        }
    }
#define OFFSET_KEYS(category)                                              \
    #category "ContinuationOffset", #category "AlignmentFallbackOffset",
    for (const std::string_view key :
        {
            "ContinuationOffset",
            "AlignmentFallbackOffset",
            FORMATTER_LIST_CATEGORIES(OFFSET_KEYS)
        "AccessModifierIndentWidth",
        "MemberIndentWidth",
        "CaseLabelIndentWidth",
        "BlockBodyIndentWidth", "NamespaceIndentWidth",
        "StatementAttributeIndentWidth"
        })
    {
        SCOPED_TRACE(key);
        const auto similar = key == "ContinuationOffset"
            ? "CallsContinuationOffset"
            : key == "AlignmentFallbackOffset"
                ? "CallsAlignmentFallbackOffset"
                : key.ends_with("ContinuationOffset")
                    ? "ContinuationOffset"
                    : key.ends_with("AlignmentFallbackOffset") ? "AlignmentFallbackOffset" : "IndentWidth";
        const TemporaryConfig invalid(
            std::string(key) + " = 33\n" + similar + " = 4\n" + std::string(key) + " = 34\nColumnLimit = 80\n");
        try {
            (void)formatter::LoadConfigFile(invalid.Path);
            FAIL() << "invalid final offset was accepted";
        } catch (const std::runtime_error& error) {
            const std::string message = error.what();
            EXPECT_NE(message.find(std::string(key) + ": indentation offset must not exceed 32"), std::string::npos);
            EXPECT_NE(message.find("[" + std::string(key) + " at " + invalid.Path.string() + ":3]"), std::string::npos);
            EXPECT_EQ(message.find("[" + std::string(key) + " at " + invalid.Path.string() + ":1]"), std::string::npos);
            EXPECT_EQ(message.find("[" + std::string(similar) + " at "), std::string::npos);
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
    if constexpr (!std::is_same_v<type, \
        std::optional<formatter::TrailingCommentSpaceSetting>>) { \
        EXPECT_NE(text.find("\n" #name " = "), std::string::npos) << #name; \
    }
    FORMATTER_STYLE_OPTIONS(CHECK_DEFAULT)
#undef CHECK_DEFAULT
    for (const auto expected : {
        "Preserve | 0..10000\nMaxEmptyLines = Preserve",
        "FunctionCvRefQualifierSpacing = Inherit\n",
        "TernaryOperatorIndentation = Align\n",
        "LineEnding = Preserve\n",
        "ArgumentCommentSpacing = Preserve\n",
        "IfConditionsPacking = Fill\n",
        "CallLineFirstArgumentNames = []\n",
        "CallLineFirstArgumentOverflow = Wrap\n",
        "CallLineFirstStringOverflow = Wrap\n",
        "Wrap | BreakBefore | KeepInline",
        "top-level &&/|| operands",
        "first newline",
        "# TrailingCommentSpaces = Preserve",
    })
    {
        EXPECT_NE(text.find(expected), std::string::npos) << expected;
    }
    EXPECT_EQ(text.find("\nTrailingCommentSpaces = "), std::string::npos);
}

TEST(ConfigPolicy, CvRefQualifierSpacingSeparatesInheritanceAndPreservation)
{
    using formatter::GapPolicy;
    formatter::Style style;
    style.FunctionRefQualifierSpacing = GapPolicy::Space;
    const auto inherited = formatter::CompilePolicies(style);
    EXPECT_EQ(inherited.Spacing.FunctionCvRefQualifierSpacing, GapPolicy::Space);
    style.FunctionRefQualifierSpacing = GapPolicy::NoSpace;
    EXPECT_EQ(formatter::CompilePolicies(style).Spacing.FunctionCvRefQualifierSpacing, GapPolicy::NoSpace);
    EXPECT_EQ(inherited.Spacing.FunctionCvRefQualifierSpacing, GapPolicy::Space);

    for (const auto policy :
        {
            GapPolicy::Space,
            GapPolicy::NoSpace,
            GapPolicy::Preserve,
            GapPolicy::GeneratedSpace,
            GapPolicy::GeneratedNoSpace,
        })
    {
        SCOPED_TRACE(formatter::SerializeConfigEnum(policy));
        const TemporaryConfig config("FunctionRefQualifierSpacing = Space\n" "FunctionCvRefQualifierSpacing = " +
            std::string(formatter::SerializeConfigEnum(policy)) + "\n");
        auto loaded = formatter::LoadConfigFile(config.Path).FormattingStyle;
        ASSERT_EQ(loaded.FunctionCvRefQualifierSpacing, policy);
        const auto compiled = formatter::CompilePolicies(loaded);
        loaded.FunctionRefQualifierSpacing = GapPolicy::NoSpace;
        loaded.FunctionCvRefQualifierSpacing.reset();
        EXPECT_EQ(compiled.Spacing.FunctionCvRefQualifierSpacing, policy);
        EXPECT_EQ(compiled.Spacing.FunctionRefQualifierSpacing, GapPolicy::Space);
    }
    const TemporaryConfig reset("FunctionRefQualifierSpacing = NoSpace\n" "FunctionCvRefQualifierSpacing = Space\n"
        "FunctionCvRefQualifierSpacing = Inherit\n");
    const auto resetStyle = formatter::LoadConfigFile(reset.Path).FormattingStyle;
    EXPECT_FALSE(resetStyle.FunctionCvRefQualifierSpacing);
    EXPECT_EQ(formatter::CompilePolicies(resetStyle).Spacing.FunctionCvRefQualifierSpacing, GapPolicy::NoSpace);

    const TemporaryConfig invalid("FunctionCvRefQualifierSpacing = Compact\n");
    EXPECT_THROW((void)formatter::LoadConfigFile(invalid.Path), std::runtime_error);
    style.FunctionCvRefQualifierSpacing = static_cast<GapPolicy>(255);
    EXPECT_THROW((void)formatter::CompilePolicies(style), std::invalid_argument);
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
    EXPECT_FALSE(GapUsesSpace(GapPolicy::GeneratedSpace, GapContext::Preserved));
    EXPECT_FALSE(GapUsesSpace(GapPolicy::GeneratedNoSpace, GapContext::Preserved));
    EXPECT_EQ(GapUsesSpace(GapPolicy::GeneratedSpace, GapContext::Rebuilt), true);
    EXPECT_EQ(GapUsesSpace(GapPolicy::GeneratedNoSpace, GapContext::Rebuilt), false);

    formatter::Style style;
    style.CallsLayout = formatter::CategoryLayout::Canonical;
    const auto policies = formatter::CompilePolicies(style);
    // Rebuilding a canonical call owns its existing edge/separator gaps too.
    EXPECT_EQ(GapUsesSpace(policies.Spacing.SpaceInsideCallArguments, GapContext::Rebuilt), false);
    EXPECT_EQ(GapUsesSpace(policies.Spacing.CommaSpaceAfter, GapContext::Rebuilt), true);
    // A nested argument edit does not rebuild a preserved outer call's gaps.
    EXPECT_FALSE(GapUsesSpace(policies.Spacing.SpaceInsideCallArguments, GapContext::Preserved).has_value());
    EXPECT_FALSE(GapUsesSpace(policies.Spacing.CommaSpaceAfter, GapContext::Preserved).has_value());
}

TEST(ConfigPolicy, ProgrammaticStylesCannotBypassWidthAndOffsetLimits)
{
    formatter::Style style;
    style.IndentWidth = 0;
    EXPECT_THROW((void)formatter::CompilePolicies(style), std::invalid_argument);
    style.IndentWidth = 32;
    style.ColumnLimit = 10001;
    EXPECT_THROW((void)formatter::CompilePolicies(style), std::invalid_argument);
    style.ColumnLimit = 10000;
    style.CallsAlignmentFallbackOffset = 33;
    EXPECT_THROW((void)formatter::CompilePolicies(style), std::invalid_argument);
    style.CallsAlignmentFallbackOffset = 32;
    style.NamespaceIndentWidth = 33;
    EXPECT_THROW((void)formatter::CompilePolicies(style), std::invalid_argument);
    style.NamespaceIndentWidth = 0;
    style.EnumeratorsContinuationOffset = 33;
    EXPECT_THROW((void)formatter::CompilePolicies(style), std::invalid_argument);
    style.EnumeratorsContinuationOffset = 0;
    style.CallsSoleArgumentFirstItem = static_cast<formatter::FirstItemPlacement>(255);
    EXPECT_THROW((void)formatter::CompilePolicies(style), std::invalid_argument);
    style.CallsSoleArgumentFirstItem.reset();
    const auto bounded = formatter::CompilePolicies(style);
    EXPECT_EQ(bounded.Indentation.Namespaces.Offset, 32u);
    EXPECT_EQ(bounded.Enumerators.Indent.ContinuationOffset, 32u);
    EXPECT_EQ(bounded.Indentation.AccessModifiers.Offset, 32u);
    EXPECT_EQ(bounded.Indentation.Members.Offset, 32u);
    EXPECT_EQ(bounded.Indentation.CaseLabels.Offset, 32u);
    EXPECT_EQ(bounded.Indentation.StandaloneBraces.Offset, 32u);
    EXPECT_EQ(bounded.Indentation.StatementAttributes.Offset, 32u);
}

TEST(ConfigPolicy, AttachedArgumentOverflowCompilesIntoOwnedAnnotations)
{
    for (const bool anyArgument : {false, true}) {
        const std::string prefix = anyArgument ? "CallLineFirstArgument" : "CallLineFirstString";
        SCOPED_TRACE(prefix);
        for (const auto overflow :
            {
                formatter::AttachedStringOverflow::Wrap,
                formatter::AttachedStringOverflow::BreakBefore,
                formatter::AttachedStringOverflow::KeepInline,
            })
        {
            const TemporaryConfig input(prefix + "Names = [\"message\"]\n" + prefix + "Overflow = " +
                std::string(formatter::SerializeConfigEnum(overflow)) + "\n");
            auto style = formatter::LoadConfigFile(input.Path).FormattingStyle;
            EXPECT_EQ(anyArgument ? style.CallLineFirstArgumentOverflow : style.CallLineFirstStringOverflow, overflow);
            const auto annotations = formatter::CompileLibraryAnnotations(style);
            style.CallLineFirstArgumentOverflow = formatter::AttachedStringOverflow::Wrap;
            style.CallLineFirstStringOverflow = formatter::AttachedStringOverflow::Wrap;
            style.CallLineFirstArgumentNames.clear();
            style.CallLineFirstStringNames.clear();
            ASSERT_NE(annotations.FindMessagePin("message"), nullptr);
            EXPECT_EQ(annotations.FindMessagePin("message")->Overflow, overflow);
            EXPECT_EQ(
                annotations.FindMessagePin("message")->FirstArgument,
                anyArgument
                ? formatter::FirstArgumentAnnotation::PinAnyArgument
                : formatter::FirstArgumentAnnotation::PinCompleteStringLiteral);
        }
        const TemporaryConfig invalid(prefix + "Overflow = Unknown\n");
        EXPECT_THROW((void)formatter::LoadConfigFile(invalid.Path), std::runtime_error);
    }
}

TEST(ConfigPolicy, GeneralArgumentPinOverridesOverlappingStringPin)
{
    formatter::Style style;
    style.CallLineFirstArgumentNames = {"shared", "general"};
    style.CallLineFirstStringNames = {"string", "shared"};
    style.CallLineFirstArgumentOverflow = formatter::AttachedStringOverflow::KeepInline;
    style.CallLineFirstStringOverflow = formatter::AttachedStringOverflow::BreakBefore;
    const auto annotations = formatter::CompileLibraryAnnotations(style);
    ASSERT_EQ(annotations.MessagePins().size(), 3u);
    EXPECT_EQ(annotations.MessagePins()[0].Name, "general");
    EXPECT_EQ(annotations.MessagePins()[1].Name, "shared");
    EXPECT_EQ(annotations.MessagePins()[1].FirstArgument, formatter::FirstArgumentAnnotation::PinAnyArgument);
    EXPECT_EQ(annotations.MessagePins()[1].Overflow, formatter::AttachedStringOverflow::KeepInline);
    EXPECT_EQ(annotations.MessagePins()[2].Name, "string");
    EXPECT_EQ(annotations.MessagePins()[2].FirstArgument, formatter::FirstArgumentAnnotation::PinCompleteStringLiteral);
    EXPECT_EQ(annotations.MessagePins()[2].Overflow, formatter::AttachedStringOverflow::BreakBefore);
}

TEST(ConfigPolicy, SchemaCompilesOwnedAnnotationsAndIndependentMetadata)
{
    const auto config = formatter::LoadConfigFile(ConfigData("compiled"));
    ASSERT_EQ(config.FormattingStyle.WhitespaceSensitiveMacros.size(), 2u);
    EXPECT_EQ(config.FormattingStyle.WhitespaceSensitiveMacros[0], "STRINGIZE_TEXT");
    ASSERT_EQ(config.FormattingStyle.CallLikeMacroNames.size(), 1u);
    EXPECT_EQ(config.FormattingStyle.CallLikeMacroNames[0], "REGISTER_NODE");
    EXPECT_EQ(config.FunctionDeclarationParameterNames, formatter::FunctionDeclarationParameterNames::Required);
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
    EXPECT_EQ(annotations.Chains(), formatter::ChainParticipation::AnnotatedRoots);
    EXPECT_TRUE(annotations.IsAnnotatedChainRoot("ns::BuildPipeline"));
    EXPECT_FALSE(annotations.IsAnnotatedChainRoot("BuildPipeline"));
    EXPECT_TRUE(annotations.AlwaysBreakChainRoot("WRITE_EVENT"));
    EXPECT_TRUE(annotations.AlwaysBreakChainRoot("ns::BuildPipeline"));
    EXPECT_FALSE(annotations.AlwaysBreakChainRoot("BuildPipeline"));
    EXPECT_FALSE(annotations.AlwaysBreakChainRoot("WRITE_EVENT_IF"));
    ASSERT_EQ(annotations.VisualScopes().size(), 2u);
    EXPECT_EQ(annotations.VisualScopes()[0].Begin, "StartList");
    EXPECT_EQ(annotations.VisualScopes()[0].End, "FinishSequence");
    ASSERT_NE(annotations.FindScopeBegin("StartMap"), nullptr);
    ASSERT_NE(annotations.FindScopeEnd("FinishEntries"), nullptr);
    EXPECT_EQ(annotations.FindScopeEnd("FinishEntries")->Begin, "StartMap");
    EXPECT_EQ(annotations.FindScopeBegin("StartMap")->End, "FinishEntries");
    EXPECT_EQ(annotations.FindScopeEnd("Finish"), nullptr);
    EXPECT_EQ(annotations.VisualScopes()[0].Layout, formatter::VisualScopeLayout::PreserveExistingExtraLevel);
    ASSERT_EQ(annotations.ChainScopes().size(), 2u);
    EXPECT_EQ(annotations.ChainScopes()[0].First, "OpenChain");
    EXPECT_EQ(annotations.ChainScopes()[0].Second, "CloseChain");
    EXPECT_TRUE(annotations.IsStickyChainPair("Choose", "Write"));
    EXPECT_TRUE(annotations.IsStickyChainPair("Choose", "OpenChain"));
    EXPECT_TRUE(annotations.IsStickyChainPair("Append", "Append"));
    EXPECT_FALSE(annotations.IsStickyChainPair("Write", "Choose"));
    EXPECT_FALSE(annotations.IsStickyChainPair("Choose", "Writes"));
    style.CallLineFirstStringNames.clear();
    style.ChainedCallRootNames.clear();
    style.AlwaysBreakChainRootNames.clear();
    style.IndentedCallScopePairs.clear();
    style.IndentedChainScopePairs.clear();
    style.StickyChainCallPairs.clear();
    const auto empty = formatter::CompileLibraryAnnotations(style);
    EXPECT_EQ(empty.Chains(), formatter::ChainParticipation::AllSafe);
    EXPECT_TRUE(empty.MessagePins().empty());
    EXPECT_TRUE(empty.AlwaysBreakChainRoots().empty());
    EXPECT_TRUE(annotations.AlwaysBreakChainRoot("WRITE_EVENT"));
    EXPECT_TRUE(empty.VisualScopes().empty());
    EXPECT_TRUE(empty.ChainScopes().empty());
    EXPECT_TRUE(empty.StickyChainPairs().empty());
    EXPECT_EQ(annotations.ChainScopes()[0].Second, "CloseChain");
    EXPECT_TRUE(annotations.IsStickyChainPair("Choose", "Write"));
    EXPECT_NE(annotations.FindMessagePin("WRITE_MESSAGE"), nullptr);
    EXPECT_TRUE(annotations.IsAnnotatedChainRoot("ns::BuildPipeline"));
    EXPECT_EQ(annotations.VisualScopes()[0].End, "FinishSequence");
    const auto copy = annotations;
    ASSERT_NE(copy.FindScopeEnd("FinishSequence"), nullptr);
    EXPECT_EQ(copy.FindScopeEnd("FinishSequence")->Begin, "StartList");
    EXPECT_EQ(copy.ChainScopes()[0].Second, "CloseChain");
    EXPECT_TRUE(copy.IsStickyChainPair("Choose", "Write"));
    EXPECT_TRUE(copy.AlwaysBreakChainRoot("WRITE_EVENT"));

    for (const auto text :
        {
            "CallLineFirstArgumentNames = [\"Trace\", \"Trace\"]\n",
            "CallLineFirstArgumentNames = [\"ns::Trace\"]\n",
            "CallLineFirstArgumentNames = [\"\"]\n",
            "CallLineFirstStringNames = [\"Trace\", \"Trace\"]\n",
            "ChainedCallRootNames = [\"ns::Root\", \"ns::Root\"]\n",
            "ChainedCallRootNames = [\"ns::\"]\n",
            "AlwaysBreakChainRootNames = [\"Trace\", \"Trace\"]\n",
            "AlwaysBreakChainRootNames = [\"ns::\"]\n",
            "IndentedCallScopePairs = [\"Start:End\", \"Start:Other\"]\n",
            "IndentedCallScopePairs = [\"Start:End\", \"End:Other\"]\n",
            "IndentedCallScopePairs = [\"Start:Start\"]\n",
            "IndentedCallScopePairs = [\"Start::End\"]\n",
            "IndentedChainScopePairs = [\"Start:End\", \"Start:Other\"]\n",
            "IndentedChainScopePairs = [\"Start:End\", \"Other:End\"]\n",
            "IndentedChainScopePairs = [\"Start:End\", \"End:Other\"]\n",
            "IndentedChainScopePairs = [\"Start:Start\"]\n",
            "IndentedChainScopePairs = [\"Start::End\"]\n",
            "StickyChainCallPairs = [\"Item:Value\", \"Item:Value\"]\n",
            "StickyChainCallPairs = [\"Item\"]\n",
            "StickyChainCallPairs = [\"Item:ns::Value\"]\n",
            "StickyChainCallPairs = [\":Value\"]\n",
        })
    {
        SCOPED_TRACE(text);
        const TemporaryConfig invalid(text);
        EXPECT_THROW((void)formatter::LoadConfigFile(invalid.Path), std::runtime_error);
    }
    EXPECT_THROW((void)formatter::LoadConfigFile(ConfigData("invalid-annotation")), std::runtime_error);
    style.AlwaysBreakChainRootNames = {"ns::"};
    EXPECT_THROW((void)formatter::CompileLibraryAnnotations(style), std::invalid_argument);
    style.AlwaysBreakChainRootNames.clear();
    style.IndentedChainScopePairs = {"Start:Start"};
    EXPECT_THROW((void)formatter::CompileLibraryAnnotations(style), std::invalid_argument);
    style.IndentedChainScopePairs.clear();
    style.StickyChainCallPairs = {"Item:Value", "Item:Value"};
    EXPECT_THROW((void)formatter::CompileLibraryAnnotations(style), std::invalid_argument);
    style.StickyChainCallPairs.clear();
    style.CallLineFirstArgumentNames = {"Trace", "Trace"};
    EXPECT_THROW((void)formatter::CompileLibraryAnnotations(style), std::invalid_argument);
    style.CallLineFirstArgumentNames.clear();
    style.CallLineFirstStringNames = {"Trace", "Trace"};
    EXPECT_THROW((void)formatter::CompileLibraryAnnotations(style), std::invalid_argument);

    const auto yt = formatter::LoadConfigFile(
        std::filesystem::path(FORMATTER_TESTDATA_DIR) / "../../../../hit-fmt/examples/ytsaurus.hit-fmt");
    const auto ytAnnotations = formatter::CompileLibraryAnnotations(yt.FormattingStyle);
    EXPECT_EQ(yt.FormattingStyle.FunctionRefQualifierSpacing, formatter::GapPolicy::Space);
    EXPECT_EQ(yt.FormattingStyle.FunctionCvRefQualifierSpacing, formatter::GapPolicy::Space);
    EXPECT_EQ(yt.FormattingStyle.CapturesCloser, formatter::CloserPlacement::OwnLine);
    EXPECT_EQ(ytAnnotations.MessagePins().size(), 17u);
    EXPECT_TRUE(ytAnnotations.ChainRoots().empty());
    EXPECT_EQ(ytAnnotations.Chains(), formatter::ChainParticipation::AllSafe);
    EXPECT_EQ(ytAnnotations.VisualScopes().size(), 3u);
    EXPECT_TRUE(ytAnnotations.AlwaysBreakChainRoot("TError"));
    EXPECT_TRUE(ytAnnotations.AlwaysBreakChainRoot("YT_TLOG_TRACE"));
    EXPECT_FALSE(ytAnnotations.AlwaysBreakChainRoot("YT_TLOG_TRACE_IF"));
    EXPECT_EQ(ytAnnotations.ChainScopes().size(), 3u);
    EXPECT_TRUE(ytAnnotations.IsStickyChainPair("Item", "Value"));
    EXPECT_TRUE(ytAnnotations.IsStickyChainPair("Item", "BeginMap"));
    EXPECT_TRUE(ytAnnotations.IsStickyChainPair("registrar", "Parameter"));
    EXPECT_NE(ytAnnotations.FindMessagePin("THROW_ERROR_EXCEPTION"), nullptr);
    EXPECT_EQ(ytAnnotations.FindMessagePin("THROW_ERROR_EXCEPTION_IF"), nullptr);
    EXPECT_FALSE(ytAnnotations.IsAnnotatedChainRoot("BuildYsonFluently"));
}

TEST(ConfigPolicy, UnknownSettingsReportKeyAndSourceLocation)
{
    for (const auto key : {"UnknownOption", "columnlimit", "CallsPackng"}) {
        SCOPED_TRACE(key);
        const TemporaryConfig config("# configuration\nColumnLimit = 80\n" + std::string(key) + " = true\n");
        try {
            (void)formatter::LoadConfigFile(config.Path);
            FAIL() << "unknown setting was accepted";
        } catch (const std::runtime_error& error) {
            const std::string message = error.what();
            EXPECT_NE(message.find(config.Path.string() + ":3:"), std::string::npos);
            EXPECT_NE(message.find("unknown setting '" + std::string(key) + "'"), std::string::npos);
            EXPECT_NE(message.find("--help-config"), std::string::npos);
        }
    }
}

TEST(ConfigPolicy, InvalidEnumValuesAndOffsetsAreRejected)
{
    for (const auto name : {"invalid-brace-value", "invalid-offset"}) {
        SCOPED_TRACE(name);
        EXPECT_THROW((void)formatter::LoadConfigFile(ConfigData(name)), std::runtime_error);
    }
    const TemporaryConfig invalid("IfConditionsPacking = Inherit\n");
    EXPECT_THROW((void)formatter::LoadConfigFile(invalid.Path), std::runtime_error);
    formatter::Style style;
    style.IfConditionsPacking = static_cast<formatter::ListPacking>(255);
    EXPECT_THROW((void)formatter::CompilePolicies(style), std::invalid_argument);
    style.IfConditionsPacking = formatter::ListPacking::Fill;
    style.AssignmentsPacking = static_cast<formatter::ListPacking>(255);
    EXPECT_THROW((void)formatter::CompilePolicies(style), std::invalid_argument);
}

TEST(ConfigPolicy, StagedOptionsResolveFinalInheritanceAndOwnScalarSnapshot)
{
    const TemporaryConfig config("FunctionBracePlacement = NextLine\n" "ConstructorBracePlacement = Inherit\n"
        "FunctionBracePlacement = NextLineIfHeaderBroken\n" "StandaloneCommentBlankLinesBefore = 0\n"
        "StandaloneCommentBlankLinesAfter = 2\n" "MaxEmptyLines = 2\n" "BreakAfterTemplateHeader = true\n"
        "ExpressionsAlignment = Operand\n" "IncludeMatchingHeader = First\n" "IncludeGrouping = Regroup\n"
        "IncludeGroups = [{Prefix = \"engine/\", Depth = 2}, " "{Prefix = \"engine/detail/\", Depth = 1}]\n"
        "IncludeGroupOrder = Configured\n" "IncludeDirectoryOrder = Alphabetical\n"
        "IncludeInGroupOrder = NearestFirst\n" "IncludePathBase = ProjectRoot\n" "IncludeStandardHeaders = Last\n");
    auto style = formatter::LoadConfigFile(config.Path).FormattingStyle;
    const auto policies = formatter::CompilePolicies(style);
    EXPECT_EQ(policies.Braces.Constructor, formatter::BracePlacement::NextLineIfHeaderBroken);
    EXPECT_EQ(policies.VerticalGaps.StandaloneCommentBlankLinesBefore.Mode, formatter::VerticalGapMode::Exact);
    EXPECT_EQ(policies.VerticalGaps.StandaloneCommentBlankLinesBefore.EmptyLines, 0u);
    EXPECT_EQ(policies.VerticalGaps.StandaloneCommentBlankLinesAfter.EmptyLines, 2u);
    EXPECT_EQ(policies.Expressions.Alignment, formatter::ExpressionAlignment::Operand);
    EXPECT_EQ(policies.Expressions.Indent.Alignment, formatter::ContinuationAlignment::Block);
    EXPECT_EQ(policies.Expressions.Indent.ContinuationOffset, style.IndentWidth);
    EXPECT_TRUE(policies.BreakAfterTemplateHeader);
    EXPECT_EQ(policies.Includes.MatchingHeader, formatter::IncludeMatchingHeader::First);
    EXPECT_EQ(policies.Includes.Grouping, formatter::IncludeGrouping::Regroup);
    const formatter::PreparedFormat prepared(style);
    style.IncludeGroups.clear();
    style.IncludeGroupOrder = formatter::IncludeGroupOrder::Directory;
    style.IncludeInGroupOrder = formatter::IncludeInGroupOrder::Alphabetical;
    EXPECT_EQ(policies.Includes.InGroupOrder, formatter::IncludeInGroupOrder::NearestFirst);
    EXPECT_EQ(prepared.Structural.InGroupOrder, formatter::IncludeInGroupOrder::NearestFirst);
    EXPECT_EQ(policies.Includes.GroupOrder, formatter::IncludeGroupOrder::Configured);
    EXPECT_EQ(prepared.Structural.GroupOrder, formatter::IncludeGroupOrder::Configured);
    EXPECT_EQ(prepared.IncludeGroups.Depth(0), 2u);
    EXPECT_EQ(prepared.IncludeGroups.Depth(1), 1u);
    style.IncludeDirectoryOrder = formatter::IncludeDirectoryOrder::NearestFirst;
    style.IncludePathBase = formatter::IncludePathBase::SourceDirectory;
    style.IncludeStandardHeaders = formatter::IncludeStandardHeaders::Preserve;
    EXPECT_EQ(prepared.Structural.StandardHeaders, formatter::IncludeStandardHeaders::Last);
    EXPECT_EQ(policies.Includes.StandardHeaders, formatter::IncludeStandardHeaders::Last);
    EXPECT_EQ(prepared.Structural.DirectoryOrder, formatter::IncludeDirectoryOrder::Alphabetical);
    EXPECT_EQ(prepared.Structural.PathBase, formatter::IncludePathBase::ProjectRoot);
    EXPECT_EQ(prepared.IncludeGroups.Classify("engine/part.h").Group, 0u);
    EXPECT_EQ(prepared.IncludeGroups.GroupCount(), 2u);
    style.FunctionBracePlacement = formatter::BracePlacement::Attached;
    style.ConstructorBracePlacement = formatter::BracePlacement::NextLine;
    style.StandaloneCommentBlankLinesAfter = {};
    const auto changed = formatter::CompilePolicies(style);
    EXPECT_EQ(changed.Braces.Constructor, formatter::BracePlacement::NextLine);
    EXPECT_EQ(changed.Braces.Function, formatter::BracePlacement::Attached);
    EXPECT_EQ(policies.Braces.Constructor, formatter::BracePlacement::NextLineIfHeaderBroken);
    EXPECT_EQ(policies.VerticalGaps.StandaloneCommentBlankLinesAfter.EmptyLines, 2u);
    for (const auto placement :
        {
            formatter::BracePlacement::Attached,
            formatter::BracePlacement::NextLine,
            formatter::BracePlacement::NextLineIfHeaderBroken,
        })
    {
        style.ConstructorBracePlacement.reset();
        style.FunctionBracePlacement = placement;
        EXPECT_EQ(formatter::CompilePolicies(style).Braces.Constructor, placement);
    }
}

TEST(ConfigPolicy, IncludeGroupingCapsValidateBeforePreparation)
{
    for (const auto layout : {formatter::LayoutMode::Preserve, formatter::LayoutMode::Canonical}) {
        for (const auto order : {formatter::IncludeOrder::Preserve, formatter::IncludeOrder::Sort}) {
            for (const auto grouping : {formatter::IncludeGrouping::Regroup, formatter::IncludeGrouping::Directory}) {
                formatter::Style style;
                style.Layout = layout;
                style.IncludeOrder = order;
                style.IncludeGrouping = grouping;
                style.MaxEmptyLines = 0;
                try {
                    (void)formatter::PreparedFormat(style);
                    FAIL() << "Grouping cap zero was accepted";
                } catch (const std::invalid_argument& error) {
                    const std::string message = error.what();
                    const auto spelling = formatter::SerializeConfigEnum(grouping);
                    EXPECT_NE(message.find("IncludeGrouping = " + std::string(spelling)), std::string::npos);
                    EXPECT_NE(message.find("MaxEmptyLines = 0"), std::string::npos);
                    EXPECT_EQ(message.find("staged"), std::string::npos);
                }
                style.MaxEmptyLines = 1;
                EXPECT_NO_THROW((void)formatter::CompilePolicies(style));
                EXPECT_NO_THROW((void)formatter::PreparedFormat(style));
                style.MaxEmptyLines.reset();
                EXPECT_NO_THROW((void)formatter::CompilePolicies(style));
                style.IncludeGrouping = formatter::IncludeGrouping::Preserve;
                style.MaxEmptyLines = 0;
                style.IncludeGroups = {{{"engine/"}, 0}};
                EXPECT_NO_THROW((void)formatter::PreparedFormat(style));
            }
        }
    }
    formatter::Style invalidGroupOrder;
    invalidGroupOrder.IncludeGroupOrder = static_cast<formatter::IncludeGroupOrder>(255);
    EXPECT_THROW((void)formatter::PreparedFormat(invalidGroupOrder), std::invalid_argument);
    const TemporaryConfig badGroupOrder("IncludeGroupOrder = Reverse\n");
    EXPECT_THROW((void)formatter::LoadConfigFile(badGroupOrder.Path), std::runtime_error);
    formatter::Style invalidOrder;
    invalidOrder.IncludeDirectoryOrder = static_cast<formatter::IncludeDirectoryOrder>(255);
    EXPECT_THROW((void)formatter::PreparedFormat(invalidOrder), std::invalid_argument);
    formatter::Style invalidInGroupOrder;
    invalidInGroupOrder.IncludeInGroupOrder = static_cast<formatter::IncludeInGroupOrder>(255);
    EXPECT_THROW((void)formatter::PreparedFormat(invalidInGroupOrder), std::invalid_argument);
    const TemporaryConfig badInGroupOrder("IncludeInGroupOrder = Reverse\n");
    EXPECT_THROW((void)formatter::LoadConfigFile(badInGroupOrder.Path), std::runtime_error);
    formatter::Style invalidBase;
    invalidBase.IncludePathBase = static_cast<formatter::IncludePathBase>(255);
    EXPECT_THROW((void)formatter::PreparedFormat(invalidBase), std::invalid_argument);
    const TemporaryConfig badOrder("IncludeDirectoryOrder = Reverse\n");
    EXPECT_THROW((void)formatter::LoadConfigFile(badOrder.Path), std::runtime_error);
    const TemporaryConfig badBase("IncludePathBase = Guess\n");
    EXPECT_THROW((void)formatter::LoadConfigFile(badBase.Path), std::runtime_error);
    formatter::Style invalidStandard;
    invalidStandard.IncludeStandardHeaders = static_cast<formatter::IncludeStandardHeaders>(255);
    EXPECT_THROW((void)formatter::PreparedFormat(invalidStandard), std::invalid_argument);
    const TemporaryConfig badStandard("IncludeStandardHeaders = First\n");
    EXPECT_THROW((void)formatter::LoadConfigFile(badStandard.Path), std::runtime_error);
    const TemporaryConfig finalGood("IncludeGrouping = Regroup\nMaxEmptyLines = 0\nMaxEmptyLines = 1\n");
    EXPECT_NO_THROW((void)formatter::LoadConfigFile(finalGood.Path));
    const TemporaryConfig invalid("IncludeGrouping = Regroup\nMaxEmptyLines = 0\n");
    try {
        (void)formatter::LoadConfigFile(invalid.Path);
        FAIL() << "Regroup cap conflict was accepted";
    } catch (const std::runtime_error& error) {
        const std::string message = error.what();
        EXPECT_NE(message.find("IncludeGrouping at " + invalid.Path.string() + ":1"), std::string::npos);
        EXPECT_NE(message.find("MaxEmptyLines at " + invalid.Path.string() + ":2"), std::string::npos);
    }
}

TEST(ConfigPolicy, SupportedOptionsKeepExistingRejections)
{
    for (const auto text :
        {
            "StandaloneCommentBlankLinesBefore = 0\n",
            "StandaloneCommentBlankLinesAfter = 1\n",
            "StandaloneCommentBlankLinesAfter = 0\n",
            "BreakAfterTemplateHeader = true\n",
            "ExpressionsAlignment = Operand\n",
            "IncludeMatchingHeader = First\n",
            "IncludeGrouping = Regroup\n",
            "ConstructorBracePlacement = Attached\n",
        })
    {
        SCOPED_TRACE(text);
        const TemporaryConfig config(text);
        auto style = formatter::LoadConfigFile(config.Path).FormattingStyle;
        EXPECT_NO_THROW((void)formatter::CompilePolicies(style));
        EXPECT_NO_THROW((void)formatter::PreparedFormat(style));
        style.CallLikeMacroNames = {"REGISTER_ITEM"};
        try {
            (void)formatter::PreparedFormat(style);
            FAIL() << "existing CallLike rejection disappeared";
        } catch (const std::invalid_argument& error) {
            EXPECT_NE(std::string(error.what()).find("CallLikeMacroNames syntax hints"), std::string::npos);
        }
        style.CallLikeMacroNames.clear();
        style.AmbiguousTemplateDeclarationSpacing = formatter::GapPolicy::Space;
        try {
            (void)formatter::PreparedFormat(style);
            FAIL() << "existing ambiguous-template rejection disappeared";
        } catch (const std::invalid_argument& error) {
            EXPECT_EQ(std::string(error.what()), "AmbiguousTemplateDeclarationSpacing is unsupported");
        }
    }
    formatter::Style style;
    style.ColumnLimit = 0;
    EXPECT_NO_THROW((void)formatter::PreparedFormat(style));
    for (const auto key : {"StandaloneCommentBlankLinesBefore", "StandaloneCommentBlankLinesAfter"}) {
        const TemporaryConfig invalid(std::string(key) + " = Inherit\n");
        EXPECT_THROW((void)formatter::LoadConfigFile(invalid.Path), std::runtime_error);
    }
}

TEST(ConfigPolicy, ClassMacroHintsRejectMalformedOrAmbiguousPairs)
{
    const TemporaryConfig config("ClassMacroNames = [\"OPEN_CLASS:CLOSE_CLASS\", " "\"BEGIN_NODE:END_NODE\"]\n");
    auto style = formatter::LoadConfigFile(config.Path).FormattingStyle;
    EXPECT_NO_THROW((void)formatter::PreparedFormat(style));
    style.ClassMacroNames.clear();
    EXPECT_NO_THROW((void)formatter::PreparedFormat(style));
    for (const auto& pairs :
        std::vector<std::vector<std::string>>{
            {"OPEN_CLASS"},
            {"OPEN_CLASS:"},
            {":CLOSE_CLASS"},
            {"OPEN_CLASS:CLOSE_CLASS:OTHER"},
            {"scope::OPEN_CLASS:CLOSE_CLASS"},
            {"OPEN_CLASS:OPEN_CLASS"},
            {"OPEN_CLASS:CLOSE_CLASS", "OPEN_CLASS:END_NODE"},
            {"OPEN_CLASS:CLOSE_CLASS", "BEGIN_NODE:CLOSE_CLASS"},
            {"OPEN_CLASS:CLOSE_CLASS", "CLOSE_CLASS:END_NODE"},
            {"OPEN_CLASS:CLOSE_CLASS", "OPEN_CLASS:CLOSE_CLASS"},
        })
    {
        style.ClassMacroNames = pairs;
        std::string text = "ClassMacroNames = [";
        for (size_t i = 0; i < pairs.size(); ++i) {
            text += (i ? ", \"" : "\"") + pairs[i] + "\"";
        }
        text += "]\n";
        SCOPED_TRACE(text);
        EXPECT_THROW((void)formatter::PreparedFormat(style), std::invalid_argument);
        const TemporaryConfig invalid(text);
        EXPECT_THROW((void)formatter::LoadConfigFile(invalid.Path), std::runtime_error);
    }
}

TEST(ConfigPolicy, OrderedGroupIndexPreservesEarliestRule)
{
    const formatter::PreparedIncludeGroups groups(std::vector<formatter::IncludeGroupRule>{
        {{"engine/", "mirror/engine/"}, 2, 7}, {{"engine/detail/"}, 1, -3}, {{"platform/"}, 0}, {{"eng/"}, 3},});
    EXPECT_EQ(groups.Classify("engine/detail/item.h").Group, 0u);
    const auto alias = groups.Classify("mirror/engine/detail/item.h");
    EXPECT_EQ(alias.Group, 0u);
    EXPECT_EQ(groups.Depth(alias.Group), 2u);
    EXPECT_EQ(groups.Priority(alias.Group), 7);
    EXPECT_EQ(groups.Priority(groups.GroupCount()), 0);
    EXPECT_EQ(groups.PrefixDepth(alias.Prefix), 2u);
    EXPECT_EQ(groups.CanonicalPrefix(alias.Group), "engine/");
    EXPECT_EQ(groups.Classify("platform/item.h").Group, 2u);
    EXPECT_EQ(groups.Classify("engine/item\\name.h").Group, 0u);
    EXPECT_EQ(groups.Classify("engine/..\\name.h").Group, 0u);
    EXPECT_EQ(groups.Classify("eng/item.h").Group, 3u);
    EXPECT_EQ(groups.Classify("engineer/item.h").Group, groups.GroupCount());
    for (const auto path :
        {
            "/engine/item.h",
            "../engine/item.h",
            "engine/../item.h",
            "engine//item.h",
            "engine/./item.h",
            "engine",
            "",
            "engine\\item.h",
        })
    {
        SCOPED_TRACE(path);
        EXPECT_EQ(groups.Classify(path).Group, groups.GroupCount());
    }
    const formatter::PreparedIncludeGroups reversed(
        std::vector<formatter::IncludeGroupRule>{{{"engine/detail/"}, 1}, {{"engine/"}, 2}});
    EXPECT_EQ(reversed.Classify("engine/detail/item.h").Group, 0u);
    EXPECT_EQ(reversed.Classify("engine/item.h").Group, 1u);
    const formatter::PreparedIncludeGroups fallback(
        std::vector<formatter::IncludeGroupRule>{{{"engine/"}, 2}, {{""}, 1}});
    EXPECT_EQ(fallback.Classify("engine/item.h").Group, 0u);
    EXPECT_EQ(fallback.Classify("../engine/item.h").Group, 1u);
    EXPECT_EQ(fallback.Classify("/engine/item.h").Group, 1u);
    const formatter::PreparedIncludeGroups empty(std::vector<formatter::IncludeGroupRule>{});
    EXPECT_EQ(empty.Classify("engine/item.h").Group, empty.GroupCount());
    // Exercise branching and long shared prefixes in the compact index.
    std::vector<formatter::IncludeGroupRule> many;
    for (int index = 255; index >= 1; --index) {
        if (index == '/' || index == '\\' || index == '.') {
            continue;
        }
        many.push_back({{"base/" + std::string(1, static_cast<char>(index)) + "/"}, 0});
    }
    const formatter::PreparedIncludeGroups wide(many);
    for (std::size_t index = 0; index < many.size(); ++index) {
        EXPECT_EQ(wide.Classify(many[index].Prefixes.front() + "item.h").Group, index);
    }
}

TEST(ConfigPolicy, PrefixValidationIsFinalOrderedAndCannotBeBypassed)
{
    for (const auto prefixes :
        {
            std::vector<std::string>{},
            {"", "engine/"},
            {"engine"},
            {"/engine/"},
            {"engine//"},
            {"./engine/"},
            {"engine/../"},
            {"engine\\detail/"},
            {"engine/", "engine/"},
        })
    {
        formatter::Style style;
        style.IncludeGroups = {{prefixes, 0}};
        EXPECT_THROW((void)formatter::CompilePolicies(style), std::invalid_argument);
        EXPECT_THROW((void)formatter::PreparedIncludeGroups(style.IncludeGroups), std::invalid_argument);
        EXPECT_THROW((void)formatter::PreparedFormat(style), std::invalid_argument);
    }
    formatter::Style invalidDepth;
    invalidDepth.IncludeGroups = {{{"engine/"}, -1}};
    EXPECT_THROW((void)formatter::CompilePolicies(invalidDepth), std::invalid_argument);
    EXPECT_THROW((void)formatter::PreparedIncludeGroups(invalidDepth.IncludeGroups), std::invalid_argument);
    EXPECT_THROW((void)formatter::PreparedFormat(invalidDepth), std::invalid_argument);
    const TemporaryConfig repaired("IncludeGroups = [{Prefix = \"bad\"}]\n" "IncludeGroups = [{Prefix = \"engine/\"}, "
        "{Prefix = \"engine/detail/\"}]\n");
    EXPECT_NO_THROW((void)formatter::LoadConfigFile(repaired.Path));
    const TemporaryConfig duplicate("IncludeGroups = [{Prefix = \"engine/\"}, " "{Prefixes = [\"engine/\"]}]\n");
    try {
        (void)formatter::LoadConfigFile(duplicate.Path);
        FAIL() << "duplicate prefixes were accepted";
    } catch (const std::runtime_error& error) {
        EXPECT_NE(
            std::string(error.what()).find("IncludeGroups at " + duplicate.Path.string() + ":1"),
            std::string::npos);
    }
    const TemporaryConfig serialized(std::string("ExpressionsAlignment = ") +
        std::string(formatter::SerializeConfigEnum(formatter::ExpressionAlignment::Operand)) +
        "\nIncludeMatchingHeader = " +
        std::string(formatter::SerializeConfigEnum(formatter::IncludeMatchingHeader::First)) + "\nIncludeGrouping = " +
        std::string(formatter::SerializeConfigEnum(formatter::IncludeGrouping::Regroup)) +
        "\nConstructorBracePlacement = " +
        std::string(formatter::SerializeConfigEnum(formatter::BracePlacement::NextLine)) + "\n");
    const auto parsed = formatter::LoadConfigFile(serialized.Path).FormattingStyle;
    EXPECT_EQ(formatter::CompilePolicies(parsed).Braces.Constructor, formatter::BracePlacement::NextLine);
    EXPECT_EQ(formatter::CompilePolicies(parsed).Includes.Grouping, formatter::IncludeGrouping::Regroup);
    EXPECT_EQ(formatter::CompilePolicies(parsed).Includes.MatchingHeader, formatter::IncludeMatchingHeader::First);
    EXPECT_EQ(formatter::CompilePolicies(parsed).Expressions.Alignment, formatter::ExpressionAlignment::Operand);
}

} // namespace

TEST(ConfigPolicy, IncludeGroupRulesParseNestedArraysAndRejectAmbiguity)
{
    const TemporaryConfig nested("IncludeGroups = [\n" "  { Prefixes = [\n"
        "      \"engine]/\", # brackets inside strings are not terminators\n" "      \"mirror#root/\"\n"
        "    ], Depth = 2, Priority = 127 },\n" "  { Priority = -128, Depth = 0, Prefix = \"other/\" },\n"
        "  { Depth = 127, Prefix = \"deep/\", Priority = -1 },\n" "  { Prefix = \"\" }\n" "]\n");
    const auto style = formatter::LoadConfigFile(nested.Path).FormattingStyle;
    EXPECT_EQ(
        style.IncludeGroups,
        (std::vector<formatter::IncludeGroupRule>{
            {{"engine]/", "mirror#root/"}, 2, 127},
            {{"other/"}, 0, -128},
            {{"deep/"}, 127, -1},
            {{""}, 127},
        }));
    const formatter::PreparedIncludeGroups prepared(style.IncludeGroups);
    EXPECT_EQ(prepared.Classify("engine]/item.h").Group, 0u);
    EXPECT_EQ(prepared.Classify("mirror#root/item.h").Group, 0u);
    EXPECT_EQ(prepared.Classify("../other/item.h").Group, 3u);
    for (const auto text :
        {
            "IncludeGroups = [\"engine/\"]\n",
            "IncludeGroups = [{}]\n",
            "IncludeGroups = [{Prefixes = []}]\n",
            "IncludeGroups = [{Prefix = \"engine/\" \"mirror/\"}]\n",
            "IncludeGroups = [{Prefix = \"engine/\", Depth = }]\n",
            "IncludeGroups = [{Prefix = \"engine/\", " "Prefixes = [\"mirror/\"]}]\n",
            "IncludeGroups = [{Prefix = \"engine/\", Depth = 1, Depth = 2}]\n",
            "IncludeGroups = [{Prefix = \"engine/\", Depth = -1}]\n",
            "IncludeGroups = [{Prefix = \"engine/\", Depth = -2}]\n",
            "IncludeGroups = [{Prefix = \"engine/\", Depth = 128}]\n",
            "IncludeGroups = [{Prefix = \"engine/\", Depth = -129}]\n",
            "IncludeGroups = [{Prefix = \"engine/\", Depth = 1.5}]\n",
            "IncludeGroups = [{Prefix = \"engine/\", " "Depth = 99999999999999999999999}]\n",
            "IncludeGroups = [{Prefix = \"engine/\", Priority = }]\n",
            "IncludeGroups = [{Prefix = \"engine/\", " "Priority = 1, Priority = 2}]\n",
            "IncludeGroups = [{Prefix = \"engine/\", Priority = 128}]\n",
            "IncludeGroups = [{Prefix = \"engine/\", Priority = -129}]\n",
            "IncludeGroups = [{Prefix = \"engine/\", Priority = 1.5}]\n",
            "IncludeGroups = [{Prefix = \"engine/\", Priority = \"1\"}]\n",
            "IncludeGroups = [{Prefix = \"engine/\", Priority = 1junk}]\n",
            "IncludeGroups = [{Prefix = \"engine/\", " "Priority = 99999999999999999999999}]\n",
            "IncludeGroups = [{Prefix = \"engine/\", Typo = 1}]\n",
            "IncludeGroups = [{Prefix = \"engine/\"}\n",
            "IncludeGroups = [{Prefix = \"engine/\"}] junk\n",
            "IncludeGroups = [{Prefixes = [\"engine/\"],\n",
            "IncludeGroupPrefixes = []\n",
            "IncludeGroupDepth = 1\n",
        })
    {
        SCOPED_TRACE(text);
        const TemporaryConfig invalid(text);
        EXPECT_THROW((void)formatter::LoadConfigFile(invalid.Path), std::runtime_error);
    }
}

TEST(ConfigPolicy, TernaryOperatorIndentationRoundTripsAndValidates)
{
    formatter::Style style;
    for (const auto indentation :
        {formatter::TernaryOperatorIndentation::Align, formatter::TernaryOperatorIndentation::Indent})
    {
        SCOPED_TRACE(formatter::SerializeConfigEnum(indentation));
        const TemporaryConfig config(
            "TernaryOperatorIndentation = " + std::string(formatter::SerializeConfigEnum(indentation)) + "\n");
        auto loaded = formatter::LoadConfigFile(config.Path).FormattingStyle;
        const formatter::PreparedFormat prepared(loaded);
        loaded.TernaryOperatorIndentation = formatter::TernaryOperatorIndentation::Align;
        EXPECT_EQ(prepared.Policies.Expressions.TernaryIndentation, indentation);
    }
    const TemporaryConfig invalid("TernaryOperatorIndentation = Shared\n");
    EXPECT_THROW((void)formatter::LoadConfigFile(invalid.Path), std::runtime_error);
    style.TernaryOperatorIndentation = static_cast<formatter::TernaryOperatorIndentation>(255);
    EXPECT_THROW((void)formatter::PreparedFormat(style), std::invalid_argument);
}

TEST(ConfigPolicy, LineEndingRoundTripsAndValidatesPreparedSnapshot)
{
    formatter::Style style;
    for (const auto ending : {formatter::LineEnding::Preserve, formatter::LineEnding::LF, formatter::LineEnding::CRLF})
    {
        SCOPED_TRACE(formatter::SerializeConfigEnum(ending));
        style.LineEnding = ending;
        const formatter::PreparedFormat prepared(style);
        style.LineEnding = formatter::LineEnding::Preserve;
        EXPECT_EQ(prepared.Policies.LineEnding, ending);
        TemporaryConfig config("LineEnding = " + std::string(formatter::SerializeConfigEnum(ending)) + "\n");
        EXPECT_EQ(formatter::LoadConfigFile(config.Path).FormattingStyle.LineEnding, ending);
    }
    style.LineEnding = static_cast<formatter::LineEnding>(255);
    EXPECT_THROW((void)formatter::PreparedFormat(style), std::invalid_argument);
    TemporaryConfig bad("LineEnding = Native\n");
    EXPECT_THROW((void)formatter::LoadConfigFile(bad.Path), std::runtime_error);
}

namespace {

TEST(ConfigPolicy, TrailingCommentSpacesPreserveBoundsAndOwnSnapshot)
{
    const TemporaryConfig preserve("TrailingCommentSpaces = Preserve\n");
    const auto preserved = formatter::LoadConfigFile(preserve.Path).FormattingStyle;
    ASSERT_TRUE(preserved.TrailingCommentSpaces);
    EXPECT_FALSE(preserved.TrailingCommentSpaces->Count);
    for (const auto count : {0u, 1u, 2u, 10000u}) {
        SCOPED_TRACE(count);
        const TemporaryConfig config("TrailingCommentSpaces = " + std::to_string(count) + "\nMaxEmptyLines = 0\n");
        auto style = formatter::LoadConfigFile(config.Path).FormattingStyle;
        const formatter::PreparedFormat prepared(style);
        style.TrailingCommentSpaces = formatter::TrailingCommentSpaceSetting{};
        ASSERT_TRUE(prepared.Policies.TrailingCommentSpaces);
        EXPECT_EQ(prepared.Policies.TrailingCommentSpaces->Count, count);
        EXPECT_EQ(prepared.Policies.Spacing.TrailingCommentSpacing, formatter::GapPolicy::Preserve);
    }
    for (const auto value : {"10001", "-1", "+2", "Inherit", "Space", "1.5", "999999999999999999999999999999"}) {
        SCOPED_TRACE(value);
        const TemporaryConfig invalid(std::string("TrailingCommentSpaces = ") + value + "\n");
        EXPECT_THROW((void)formatter::LoadConfigFile(invalid.Path), std::runtime_error);
    }
    formatter::Style invalid;
    invalid.TrailingCommentSpaces = formatter::TrailingCommentSpaceSetting{10001};
    EXPECT_THROW((void)formatter::PreparedFormat(invalid), std::invalid_argument);
    invalid.TrailingCommentSpaces = formatter::TrailingCommentSpaceSetting{10000};
    EXPECT_NO_THROW((void)formatter::PreparedFormat(invalid));
}

} // namespace
