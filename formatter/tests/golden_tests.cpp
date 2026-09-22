#include <formatter/formatter.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::string ReadFile(const std::filesystem::path& path)
{
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error(
            std::format("failed to open {}", path.string()));
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

std::string FormatSource(
    std::string_view source,
    const formatter::Style& style = {})
{
    std::string storage(source);
    storage.append(util::SourcePaddingSize, '\0');
    return formatter::Format(
        {.Data = storage.data(), .Size = source.size()}, style);
}

void SaveFile(const std::filesystem::path& path, std::string_view contents)
{
    std::ofstream file(path);
    ASSERT_TRUE(file.is_open());
    file << contents;
}

std::vector<std::string> DiscoverGoldenCases()
{
    std::vector<std::string> cases;
    constexpr std::string_view suffix = ".input.cpp";
    for (const auto& entry :
         std::filesystem::directory_iterator(FORMATTER_TESTDATA_DIR))
    {
        auto name = entry.path().filename().string();
        if (name.ends_with(suffix)) {
            name.resize(name.size() - suffix.size());
            cases.push_back(std::move(name));
        }
    }
    std::ranges::sort(cases);
    return cases;
}

formatter::Style StyleForCase(std::string_view name)
{
    formatter::Style style;
    if (name == "ConfiguresFunctionsThatKeepTheirFirstArgument") {
        style.ForbidBreakAfter = {"render", "inspect"};
    } else if (name == "FormatsContextualControlBraces") {
        style.ControlBracePlacement =
            formatter::BracePlacement::AttachUnlessMultiline;
    } else if (name == "PreservesWhitespaceSensitiveMacroAlignment") {
        style.WhitespaceSensitiveMacros = {"ALIGNED_VALUES"};
    } else if (name == "RegroupsSafeMultilineCalls") {
        style.Regroup = true;
    } else if (name == "LeavesUnbracedBodiesWhenConfigured") {
        style.InsertBraces = false;
    } else if (name == "KeepsInlineRecordsWhenConfigured") {
        style.BreakInlineRecordDeclarations = false;
    } else if (name == "SpacesTemplateParameterPacksWhenConfigured") {
        style.SpaceBeforeTemplateParameterPack = true;
    } else if (name == "UsesConfiguredDimensions") {
        style.IndentWidth = 2;
        style.ColumnLimit = 40;
    } else if (name == "UsesAlternativeStyleOptions") {
        style.FunctionBracePlacement = formatter::BracePlacement::Attach;
        style.RecordBracePlacement = formatter::BracePlacement::Attach;
        style.ControlBracePlacement = formatter::BracePlacement::NewLine;
        style.LambdaBracePlacement = formatter::BracePlacement::NewLine;
        style.NamespaceBracePlacement = formatter::BracePlacement::NewLine;
        style.SpaceBeforeParentheses = formatter::ParenthesisSpacing::Always;
        style.PointerAlignment = formatter::DeclaratorAlignment::Right;
        style.ReferenceAlignment = formatter::DeclaratorAlignment::Right;
        style.IndentCaseLabels = formatter::CaseLabelIndentation::Indent;
        style.SpaceInsideParentheses = true;
        style.SpaceInsideSquareBrackets = true;
        style.IndentNamespaces = true;
        style.KeepEmptyBlocksOnOneLine = false;
        style.AllowShortLambdasOnOneLine = false;
    } else if (name == "WrapsNestedExpressionsToConfiguredLimit") {
        style.ColumnLimit = 60;
    } else if (name == "AlignsMultilineItemsWithOpeningBracket") {
        style.OpenBracketAlignment =
            formatter::BracketAlignment::AlignWithOpening;
    } else if (name == "PreservesConfiguredBracketAlignment") {
        style.OpenBracketAlignment = formatter::BracketAlignment::Preserve;
        style.CloseBracketAlignment =
            formatter::ClosingBracketAlignment::Preserve;
    } else if (name == "PreservesConfiguredBracedInitializerSpacing") {
        style.SpaceInsideBracedInitializers =
            formatter::BracedInitializerSpacing::Preserve;
    }
    return style;
}

class StyleGoldenTest : public testing::TestWithParam<std::string>
{ };

TEST_P(StyleGoldenTest, MatchesCppOutput)
{
    const auto base =
        std::filesystem::path(FORMATTER_TESTDATA_DIR) / GetParam();
    const auto input = ReadFile(base.string() + ".input.cpp");
    const auto style = StyleForCase(GetParam());
    const auto actual = FormatSource(input, style);
    const auto outputPath = base.string() + ".output.cpp";

    EXPECT_EQ(FormatSource(actual, style), actual);
    if (std::getenv("UPDATE_GOLDEN")) {
        SaveFile(outputPath, actual);
    } else {
        EXPECT_EQ(actual, ReadFile(outputPath));
    }
}

INSTANTIATE_TEST_SUITE_P(
    Style,
    StyleGoldenTest,
    testing::ValuesIn(DiscoverGoldenCases()),
    [](const auto& info) {
        return std::string(info.param);
    });

} // namespace
