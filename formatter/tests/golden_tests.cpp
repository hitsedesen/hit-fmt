#include <formatter/config.hpp>
#include <formatter/formatter.hpp>
#include <formatter/naming.hpp>
#include <formatter/parameter_names.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

std::string ReadFile(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot read fixture: " + path.string());
    }
    return {std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()};
}

void ExpectUnchanged(std::string_view source, const formatter::Style& style)
{
    std::string storage(source);
    storage.append(util::SourcePaddingSize, '\0');
    util::PaddedSourceView padded{.Data = storage.data(),
        .Size = source.size()};
    auto tree = parser::Parse(padded, [] (parser::ParseError) { });
    EXPECT_EQ(formatter::Format(tree, style), source);
    const auto first = formatter::Format(padded, style);
    EXPECT_EQ(first, source);
    storage.assign(first);
    storage.append(util::SourcePaddingSize, '\0');
    EXPECT_EQ(formatter::Format({.Data = storage.data(),
        .Size = first.size()}, style), first);
}

std::vector<std::filesystem::path> DiscoverCases()
{
    std::vector<std::filesystem::path> cases;
    for (const auto& entry :
        std::filesystem::directory_iterator(FORMATTER_TESTDATA_DIR))
    {
        auto filename = entry.path().filename().string();
        if (filename.ends_with(".input.cpp") ||
            filename.ends_with(".output.cpp"))
        {
            cases.push_back(entry.path());
        }
    }
    std::ranges::sort(cases);
    return cases;
}

class PreserveGoldenTest : public testing::TestWithParam<std::filesystem::path>
{ };

TEST_P(PreserveGoldenTest, KeepsEverySourceByteUnderAllWidths)
{
    const auto input = ReadFile(GetParam());
    for (auto [indent, column] : {
        std::pair<size_t, size_t>{4, 120},
        {1, 1},
        {32, 10000},
    }) {
        formatter::Style style;
        style.IndentWidth = indent;
        style.ColumnLimit = column;
        ExpectUnchanged(input, style);
    }
}

INSTANTIATE_TEST_SUITE_P(Preserve, PreserveGoldenTest,
    testing::ValuesIn(DiscoverCases()),
    [] (const auto& info) {
        auto name = info.param.filename().string();
        std::ranges::replace(name, '.', '_');
        std::ranges::replace(name, '-', '_');
        return name;
    });

std::string FormatWithStyle(
    std::string_view source, const formatter::Style& style)
{
    std::string storage(source);
    storage.append(util::SourcePaddingSize, '\0');
    util::PaddedSourceView padded{.Data = storage.data(),
        .Size = source.size()};
    auto tree = parser::Parse(padded, [] (parser::ParseError) { });
    EXPECT_EQ(formatter::Format(tree, style),
        formatter::Format(padded, style));
    return formatter::Format(tree, style);
}

class BracePlacementGoldenTest : public testing::TestWithParam<std::string>
{ };

TEST_P(BracePlacementGoldenTest, FormatsOnlySelectedDeclarationBraces)
{
    const auto directory = std::filesystem::path(BRACE_TESTDATA_DIR);
    const auto input = ReadFile(directory / "input.cpp");
    const auto expected = ReadFile(directory / (GetParam() + ".output.cpp"));
    const auto style = formatter::LoadConfigFile(
        directory / (GetParam() + ".hit-fmt")).FormattingStyle;

    EXPECT_EQ(FormatWithStyle(input, style), expected);
    EXPECT_EQ(FormatWithStyle(expected, style), expected);
}

INSTANTIATE_TEST_SUITE_P(Configurations, BracePlacementGoldenTest,
    testing::Values("preserve", "attach", "newline", "mixed"));

class FunctionParameterGoldenTest : public testing::TestWithParam<std::string>
{ };

TEST_P(FunctionParameterGoldenTest, MatchesConfiguredOutput)
{
    const auto directory = std::filesystem::path(FORMATTER_TESTDATA_DIR) /
        "../function_parameters";
    const auto input = ReadFile(directory / "input.cpp");
    const auto config = formatter::LoadConfigFile(
        directory / (GetParam() + ".hit-fmt"));
    const auto expected = ReadFile(directory / (GetParam() + ".output.cpp"));

    std::string storage(input);
    storage.append(util::SourcePaddingSize, '\0');
    const auto actual = formatter::Format(
        {.Data = storage.data(), .Size = input.size()},
        config.FormattingStyle);
    EXPECT_EQ(actual, expected);

    // A second pass must not change either packing or indentation.
    storage.assign(actual);
    storage.append(util::SourcePaddingSize, '\0');
    EXPECT_EQ(formatter::Format(
        {.Data = storage.data(), .Size = actual.size()},
        config.FormattingStyle), actual);
}

INSTANTIATE_TEST_SUITE_P(FunctionParameters, FunctionParameterGoldenTest,
    testing::Values("preserve", "pack_name", "pack_new", "one_name",
        "one_new", "pack_merge"));

class NamespaceCommentGoldenTest : public testing::TestWithParam<std::string>
{ };

TEST_P(NamespaceCommentGoldenTest, MatchesConfiguredOutput)
{
    const auto directory = std::filesystem::path(FORMATTER_TESTDATA_DIR) /
        "../namespace_comments";
    const auto input = ReadFile(directory /
        (GetParam() == "crlf_required" ? "crlf.input.cpp" : "input.cpp"));
    const auto expected = ReadFile(directory / (GetParam() + ".output.cpp"));
    const auto style = formatter::LoadConfigFile(
        directory / (GetParam() + ".hit-fmt")).FormattingStyle;

    auto format = [&style] (std::string_view source) {
        std::string storage(source);
        storage.append(util::SourcePaddingSize, '\0');
        util::PaddedSourceView padded{.Data = storage.data(),
            .Size = source.size()};
        auto tree = parser::Parse(padded, [] (parser::ParseError) { });
        const auto result = formatter::Format(tree, style);
        EXPECT_EQ(result, formatter::Format(padded, style));
        return result;
    };
    EXPECT_EQ(format(input), expected);
    EXPECT_EQ(format(expected), expected);
}

INSTANTIATE_TEST_SUITE_P(Configurations, NamespaceCommentGoldenTest,
    testing::Values("preserve", "remove", "required", "required_newline",
        "crlf_required"));

class IncludeOrderGoldenTest : public testing::TestWithParam<std::string>
{ };

TEST_P(IncludeOrderGoldenTest, SortsOnlySafeIncludeGroups)
{
    const auto directory = std::filesystem::path(FORMATTER_TESTDATA_DIR) /
        "../include-order";
    const auto input = ReadFile(directory /
        (GetParam() == "crlf" ? "crlf.input.cpp" : "input.cpp"));
    const auto expected = ReadFile(directory / (GetParam() + ".output.cpp"));
    const auto style = formatter::LoadConfigFile(directory /
        (GetParam() == "crlf" ? "sort.hit-fmt" :
            GetParam() + ".hit-fmt")).FormattingStyle;

    EXPECT_EQ(FormatWithStyle(input, style), expected);
    EXPECT_EQ(FormatWithStyle(expected, style), expected);
}

INSTANTIATE_TEST_SUITE_P(Configurations, IncludeOrderGoldenTest,
    testing::Values("sort", "composed", "crlf"));

class ParameterNameGoldenTest : public testing::TestWithParam<std::string>
{ };

TEST_P(ParameterNameGoldenTest, ReportsConfiguredMissingNames)
{
    const auto directory = std::filesystem::path(FORMATTER_TESTDATA_DIR) /
        "../parameter_names";
    const auto input = ReadFile(directory / "input.cpp");
    const auto expected = ReadFile(directory / (GetParam() + ".expected"));
    const auto config = formatter::LoadConfigFile(
        directory / (GetParam() + ".hit-fmt"));
    std::string storage(input);
    storage.append(util::SourcePaddingSize, '\0');
    const auto tree = parser::Parse(
        {.Data = storage.data(), .Size = input.size()},
        [] (parser::ParseError) { });
    std::string actual;
    for (const auto& diagnostic : formatter::CheckParameterNames(tree,
        config))
    {
        actual += std::to_string(diagnostic.Position.Line) + ":" +
            std::to_string(diagnostic.Position.Column) + " [" +
            std::to_string(diagnostic.SignatureBeginLine) + "-" +
            std::to_string(diagnostic.SignatureEndLine) + "]: " +
            diagnostic.Function + ", parameter #" +
            std::to_string(diagnostic.ParameterNumber) +
            (diagnostic.AllowCommentedName ? " (comment allowed)" : "") +
            "\n";
    }
    EXPECT_EQ(actual, expected);
}

INSTANTIATE_TEST_SUITE_P(Configurations, ParameterNameGoldenTest,
    testing::Values("enabled", "default", "declarations_only",
        "definitions_only", "names_only"));

TEST(FunctionParameterSafetyTest, ComposesWithBracePlacement)
{
    formatter::Style style;
    style.ColumnLimit = 40;
    style.FunctionParameterSplit = formatter::FunctionParameterSplit::Pack;
    style.FunctionBracePlacement = formatter::BracePlacement::NewLine;
    const auto format = [&] (std::string_view source) {
        std::string storage(source);
        storage.append(util::SourcePaddingSize, '\0');
        return formatter::Format({.Data = storage.data(), .Size = source.size()},
            style);
    };
    const std::string expected =
        "void Many(int alpha, int beta,\n"
        "    int gamma, int delta)\n"
        "{}\n";
    EXPECT_EQ(format("void Many(int alpha, int beta, int gamma, int delta) {}\n"),
        expected);
    EXPECT_EQ(format(expected), expected);
}

TEST(FunctionParameterSafetyTest, KeepsCrlfListsAndCalls)
{
    formatter::Style style;
    style.FunctionParameterSplit = formatter::FunctionParameterSplit::Pack;
    style.ColumnLimit = 25;
    ExpectUnchanged(
        "void f(int alpha, int beta, int gamma);\r\n"
        "void g() { Use(very_long_argument, another_long_argument); }\r\n",
        style);
}

class BraceSpacingGoldenTest : public testing::TestWithParam<std::string>
{ };

TEST_P(BraceSpacingGoldenTest, FormatsOnlySameLineDeclarationGaps)
{
    const auto directory = std::filesystem::path(BRACE_SPACING_TESTDATA_DIR);
    const auto input = ReadFile(directory / "input.cpp");
    const auto expected = ReadFile(directory / (GetParam() + ".output.cpp"));
    const auto style = formatter::LoadConfigFile(
        directory / (GetParam() + ".hit-fmt")).FormattingStyle;

    EXPECT_EQ(FormatWithStyle(input, style), expected);
    EXPECT_EQ(FormatWithStyle(expected, style), expected);
}

INSTANTIATE_TEST_SUITE_P(Configurations, BraceSpacingGoldenTest,
    testing::Values("preserve", "single-space", "mixed"));

class LambdaSpacingGoldenTest : public testing::TestWithParam<std::string>
{ };

TEST_P(LambdaSpacingGoldenTest, FormatsOnlyLambdaCaptureParameterGaps)
{
    const auto directory = std::filesystem::path(LAMBDA_SPACING_TESTDATA_DIR);
    const auto input = ReadFile(directory / "input.cpp");
    const auto expected = ReadFile(directory / (GetParam() + ".output.cpp"));
    const auto style = formatter::LoadConfigFile(
        directory / (GetParam() + ".hit-fmt")).FormattingStyle;

    EXPECT_EQ(FormatWithStyle(input, style), expected);
    EXPECT_EQ(FormatWithStyle(expected, style), expected);
}

INSTANTIATE_TEST_SUITE_P(Configurations, LambdaSpacingGoldenTest,
    testing::Values("preserve", "space", "no-space"));

class ControlKeywordSpacingGoldenTest : public testing::TestWithParam<std::string>
{ };

TEST_P(ControlKeywordSpacingGoldenTest, FormatsOnlyParsedControlHeaders)
{
    const auto directory = std::filesystem::path(CONTROL_SPACING_TESTDATA_DIR);
    const auto input = ReadFile(directory / "input.cpp");
    const auto expected = ReadFile(directory / (GetParam() + ".output.cpp"));
    const auto style = formatter::LoadConfigFile(
        directory / (GetParam() + ".hit-fmt")).FormattingStyle;

    EXPECT_EQ(FormatWithStyle(input, style), expected);
    EXPECT_EQ(FormatWithStyle(expected, style), expected);
}

INSTANTIATE_TEST_SUITE_P(Configurations, ControlKeywordSpacingGoldenTest,
    testing::Values("preserve", "space", "no-space"));

class ControlBracePlacementGoldenTest : public testing::TestWithParam<std::string>
{ };

TEST_P(ControlBracePlacementGoldenTest, PlacesOnlyControlBodyBraces)
{
    const auto directory = std::filesystem::path(CONTROL_BRACE_TESTDATA_DIR);
    const auto input = ReadFile(directory /
        (GetParam() == "combined" ? "combined.input.cpp" : "input.cpp"));
    const auto expected = ReadFile(directory / (GetParam() + ".output.cpp"));
    const auto style = formatter::LoadConfigFile(
        directory / (GetParam() + ".hit-fmt")).FormattingStyle;

    EXPECT_EQ(FormatWithStyle(input, style), expected);
    EXPECT_EQ(FormatWithStyle(expected, style), expected);
}

INSTANTIATE_TEST_SUITE_P(Configurations, ControlBracePlacementGoldenTest,
    testing::Values("preserve", "same-line", "new-line",
        "attach-unless-multiline", "combined"));

class FunctionSpacingGoldenTest : public testing::TestWithParam<std::string>
{ };

TEST_P(FunctionSpacingGoldenTest, FormatsOnlyFunctionParameterGaps)
{
    const auto directory = std::filesystem::path(FUNCTION_SPACING_TESTDATA_DIR);
    const auto input = ReadFile(directory /
        (GetParam() == "reflow" ? "reflow.input.cpp" : "input.cpp"));
    const auto expected = ReadFile(directory / (GetParam() + ".output.cpp"));
    const auto style = formatter::LoadConfigFile(
        directory / (GetParam() + ".hit-fmt")).FormattingStyle;

    EXPECT_EQ(FormatWithStyle(input, style), expected);
    EXPECT_EQ(FormatWithStyle(expected, style), expected);
}

INSTANTIATE_TEST_SUITE_P(Configurations, FunctionSpacingGoldenTest,
    testing::Values("preserve", "space", "no-space", "reflow"));

class FunctionCallSpacingGoldenTest : public testing::TestWithParam<std::string>
{ };

TEST_P(FunctionCallSpacingGoldenTest, FormatsOnlyParsedCallGaps)
{
    const auto directory =
        std::filesystem::path(FUNCTION_CALL_SPACING_TESTDATA_DIR);
    const auto input = ReadFile(directory /
        (GetParam() == "combined" ? "combined.input.cpp" : "input.cpp"));
    const auto expected = ReadFile(directory / (GetParam() + ".output.cpp"));
    const auto style = formatter::LoadConfigFile(
        directory / (GetParam() + ".hit-fmt")).FormattingStyle;

    EXPECT_EQ(FormatWithStyle(input, style), expected);
    EXPECT_EQ(FormatWithStyle(expected, style), expected);
}

INSTANTIATE_TEST_SUITE_P(Configurations, FunctionCallSpacingGoldenTest,
    testing::Values("preserve", "space", "no-space", "combined"));

class FunctionInnerSpacingGoldenTest : public testing::TestWithParam<std::string>
{ };

TEST_P(FunctionInnerSpacingGoldenTest, FormatsOnlyParameterListEdges)
{
    const auto directory =
        std::filesystem::path(FUNCTION_INNER_SPACING_TESTDATA_DIR);
    const auto input = ReadFile(directory /
        (GetParam() == "reflow" ? "reflow.input.cpp" : "input.cpp"));
    const auto expected = ReadFile(directory / (GetParam() + ".output.cpp"));
    const auto style = formatter::LoadConfigFile(
        directory / (GetParam() + ".hit-fmt")).FormattingStyle;

    EXPECT_EQ(FormatWithStyle(input, style), expected);
    EXPECT_EQ(FormatWithStyle(expected, style), expected);
}

INSTANTIATE_TEST_SUITE_P(Configurations, FunctionInnerSpacingGoldenTest,
    testing::Values("preserve", "space", "no-space", "reflow"));

class CallInnerSpacingGoldenTest : public testing::TestWithParam<std::string>
{ };

TEST_P(CallInnerSpacingGoldenTest, FormatsOnlyUnambiguousCallEdges)
{
    const auto directory = std::filesystem::path(CALL_INNER_SPACING_TESTDATA_DIR);
    const auto input = ReadFile(directory /
        (GetParam() == "combined" ? "combined.input.cpp" : "input.cpp"));
    const auto expected = ReadFile(directory / (GetParam() + ".output.cpp"));
    const auto style = formatter::LoadConfigFile(
        directory / (GetParam() + ".hit-fmt")).FormattingStyle;

    EXPECT_EQ(FormatWithStyle(input, style), expected);
    EXPECT_EQ(FormatWithStyle(expected, style), expected);
}

INSTANTIATE_TEST_SUITE_P(Configurations, CallInnerSpacingGoldenTest,
    testing::Values("preserve", "space", "no-space", "combined"));

class LambdaCaptureGoldenTest : public testing::TestWithParam<std::string>
{ };

TEST_P(LambdaCaptureGoldenTest, FormatsListAndAssignments)
{
    const auto directory = std::filesystem::path(LAMBDA_CAPTURE_TESTDATA_DIR);
    const auto input = ReadFile(directory / "input.cpp");
    const auto expected = ReadFile(directory / (GetParam() + ".output.cpp"));
    const auto style = formatter::LoadConfigFile(
        directory / (GetParam() + ".hit-fmt")).FormattingStyle;

    EXPECT_EQ(FormatWithStyle(input, style), expected);
    EXPECT_EQ(FormatWithStyle(expected, style), expected);
}

INSTANTIATE_TEST_SUITE_P(Configurations, LambdaCaptureGoldenTest,
    testing::Values("preserve", "space", "no-space", "mixed", "reverse"));

class LambdaInnerParameterGoldenTest : public testing::TestWithParam<std::string>
{ };

TEST_P(LambdaInnerParameterGoldenTest, FormatsOnlyLambdaParameterEdges)
{
    const auto directory =
        std::filesystem::path(LAMBDA_INNER_PARAMETER_TESTDATA_DIR);
    const auto input = ReadFile(directory / "input.cpp");
    const auto expected = ReadFile(directory / (GetParam() + ".output.cpp"));
    const auto style = formatter::LoadConfigFile(
        directory / (GetParam() + ".hit-fmt")).FormattingStyle;

    EXPECT_EQ(FormatWithStyle(input, style), expected);
    EXPECT_EQ(FormatWithStyle(expected, style), expected);
}

INSTANTIATE_TEST_SUITE_P(Configurations, LambdaInnerParameterGoldenTest,
    testing::Values("preserve", "space", "no-space", "combined"));

class BodyIndentGoldenTest : public testing::TestWithParam<std::string>
{ };

TEST_P(BodyIndentGoldenTest, IndentsOnlyRecordAndEnumMembers)
{
    const auto directory = std::filesystem::path(BODY_INDENT_TESTDATA_DIR);
    const auto input = ReadFile(directory / "input.cpp");
    const auto expected = ReadFile(directory / (GetParam() + ".output.cpp"));
    const auto style = formatter::LoadConfigFile(
        directory / (GetParam() + ".hit-fmt")).FormattingStyle;

    EXPECT_EQ(FormatWithStyle(input, style), expected);
    EXPECT_EQ(FormatWithStyle(expected, style), expected);
}

INSTANTIATE_TEST_SUITE_P(Configurations, BodyIndentGoldenTest,
    testing::Values("preserve", "indent", "no-indent", "mixed"));

class InheritanceGoldenTest : public testing::TestWithParam<std::string>
{ };

TEST_P(InheritanceGoldenTest, PositionsOnlyParsedBaseClauses)
{
    const auto directory = std::filesystem::path(INHERITANCE_TESTDATA_DIR);
    const auto input = ReadFile(directory / "input.cpp");
    const auto expected = ReadFile(directory / (GetParam() + ".output.cpp"));
    const auto style = formatter::LoadConfigFile(
        directory / (GetParam() + ".hit-fmt")).FormattingStyle;

    EXPECT_EQ(FormatWithStyle(input, style), expected);
    EXPECT_EQ(FormatWithStyle(expected, style), expected);
}

INSTANTIATE_TEST_SUITE_P(Configurations, InheritanceGoldenTest,
    testing::Values("preserve", "format", "leading",
        "trailing", "overflow", "comma-leading", "no-space",
        "spaces-mixed", "combined"));

class InheritanceSplitGoldenTest : public testing::TestWithParam<std::string>
{ };

TEST_P(InheritanceSplitGoldenTest, SplitsOnlySafeBaseLists)
{
    const auto directory = std::filesystem::path(INHERITANCE_TESTDATA_DIR);
    const auto input = ReadFile(directory / "split.input.cpp");
    const auto expected = ReadFile(directory / (GetParam() + ".output.cpp"));
    const auto style = formatter::LoadConfigFile(
        directory / (GetParam() + ".hit-fmt")).FormattingStyle;

    EXPECT_EQ(FormatWithStyle(input, style), expected);
    EXPECT_EQ(FormatWithStyle(expected, style), expected);
}

INSTANTIATE_TEST_SUITE_P(Configurations, InheritanceSplitGoldenTest,
    testing::Values("split-preserve", "one-per-line", "pack-wide",
        "pack-tight", "split-combined"));

TEST(BraceSpacingTest, ComposesWithParameterReflow)
{
    formatter::Style style;
    style.ColumnLimit = 40;
    style.FunctionParameterSplit = formatter::FunctionParameterSplit::Pack;
    style.FunctionBraceSpacing = formatter::SameLineBraceSpacing::SingleSpace;
    const std::string input =
        "void Many(int alpha, int beta, int gamma, int delta){}\n";
    const std::string expected =
        "void Many(int alpha, int beta,\n"
        "    int gamma, int delta) {}\n";
    EXPECT_EQ(FormatWithStyle(input, style), expected);
    EXPECT_EQ(FormatWithStyle(expected, style), expected);

    style.FunctionBracePlacement = formatter::BracePlacement::Attach;
    const std::string detached =
        "void Many(int alpha, int beta, int gamma, int delta)\n{}\n";
    EXPECT_EQ(FormatWithStyle(detached, style), expected);
}

class NamingGoldenTest : public testing::TestWithParam<std::string> { };

TEST_P(NamingGoldenTest, ReportsOnlyConfiguredUnambiguousNames)
{
    const auto dir = std::filesystem::path(NAMING_TESTDATA_DIR);
    const auto name = GetParam();
    const auto input = ReadFile(dir / (name == "rules" ? "input.cpp" :
        "alternate.cpp"));
    const auto expected = ReadFile(dir / (name + ".expected"));
    const auto config = formatter::LoadConfigFile(dir / (name + ".hit-fmt"));
    std::string storage = input;
    storage.append(util::SourcePaddingSize, '\0');
    const auto tree = parser::Parse({.Data = storage.data(),
        .Size = input.size()}, [] (parser::ParseError) { });
    std::string actual;
    for (const auto& diagnostic : formatter::CheckNames(tree, config.Naming)) {
        actual += std::to_string(diagnostic.Position.Line) + ":" +
            std::to_string(diagnostic.Position.Column) + ": " +
            diagnostic.Message + "\n";
    }
    EXPECT_EQ(actual, expected);
    EXPECT_EQ(formatter::Format(tree, config.FormattingStyle), input);
    EXPECT_TRUE(formatter::CheckNames(tree, {}).empty());
}

INSTANTIATE_TEST_SUITE_P(Configurations, NamingGoldenTest,
    testing::Values("rules", "alternate", "no_exempt"));

TEST(PreserveFormatterTest, KeepsEmptyRecoveryAndBinarySource)
{
    for (const std::string source : {
        std::string{},
        std::string{" \t\r\n\r\n"},
        std::string{"#define STR(x) #x\r\nSTR(a+b);\r\n"},
        std::string{"int f(\n  /* unfinished"},
        std::string{"int x;\0int y;\n", 14},
    }) {
        ExpectUnchanged(source, formatter::Style{});
    }
}

TEST(PreserveConfigTest, RejectsInvalidSettings)
{
    const auto path = std::filesystem::temp_directory_path() /
        "hit-fmt-preserve-config-test.hit-fmt";
    auto save = [&] (std::string_view contents) {
        std::ofstream output(path);
        ASSERT_TRUE(output.is_open());
        output << contents;
    };
    save("IndentWidth = 2\nColumnLimit = 40\n"
         "Exclude = [\"(^|/)generated(/|$)\"]\n");
    const auto config = formatter::LoadConfigFile(path);
    EXPECT_EQ(config.FormattingStyle.IndentWidth, 2u);
    EXPECT_EQ(config.FormattingStyle.ColumnLimit, 40u);
    ASSERT_EQ(config.Exclusions.size(), 1u);
    EXPECT_TRUE(std::regex_search("generated/source.cpp", config.Exclusions[0]));
    for (auto setting : {
        "Regroup = true\n",
        "InsertBraces = false\n",
        "WhitespaceSensitiveMacros = []\n",
        "ColumnLimit = 0\n",
        "IndentWidth = 0\n",
        "IndentAccessModifiers = Unknown\n",
        "IndentMembers = Unknown\n",
        "InheritanceList = Unknown\n",
        "InheritanceListColonPlacement = Unknown\n",
        "InheritanceListColonSpaceBefore = Unknown\n",
        "InheritanceListColonSpaceAfter = Unknown\n",
        "InheritanceListCommaPlacement = Unknown\n",
        "InheritanceListCommaSpaceBefore = Unknown\n",
        "InheritanceListCommaSpaceAfter = Unknown\n",
        "InheritanceListStart = Unknown\n",
        "InheritanceListSplit = Unknown\n",
        "InheritanceListIndent = 0\n",
        "InheritanceListIndent = 33\n",
        "InheritanceListStart = Attach\n",
        "InheritanceListSplit = Preserve\n",
        "InheritanceListMerge = true\n",
        "InheritanceListPunctuation = WithAncestor\n",
        "FunctionParameterSplit = Together\n",
        "FirstFunctionParameterPosition = BeforeName\n",
        "EnforceFirstFunctionParameterPosition = maybe\n",
        "FunctionParameterIndent = Tabs\n",
        "MergeFunctionParameters = yes\n",
        "LambdaSpaceBeforeParameters = Unknown\n",
        "SpaceAroundCaptureList = Unknown\n",
        "SpaceAroundCaptureAssignment = Unknown\n",
        "LambdaImplicitCaptureSpacing = Space\n",
        "LambdaExplicitCaptureSpacing = Space\n",
        "FunctionSpaceBeforeParameters = Unknown\n",
        "FunctionCallSpaceBeforeParameters = Unknown\n",
        "SpaceInsideFunctionParameters = Unknown\n",
        "SpaceInsideLambdaParameters = Unknown\n",
        "SpaceInsideCallArguments = Unknown\n",
        "ControlBlockKeywordSpacing = Unknown\n",
        "ControlBlockBracePlacement = Unknown\n",
        "Exclude = [\"[\"]\n",
        "StructNameCase = lowercase\n",
        "ClassNamePrefixes = []\n",
        "EnumNamePrefixes = [\"bad prefix\"]\n",
        "NamingNamespaceExemptions = [\"std::\"]\n",
        "NamingNamespaceExemptions = [\"::std\"]\n",
        "NamingNamespaceExemptions = [\"std:chrono\"]\n",
        "NamespaceNamePrefixes = [\"bad\\nname\"]\n",
    }) {
        save(setting);
        EXPECT_THROW(formatter::LoadConfigFile(path), std::runtime_error);
    }
    std::filesystem::remove(path);
}

} // namespace
