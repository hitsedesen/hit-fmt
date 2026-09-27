#include <formatter/config.hpp>
#include <formatter/formatter.hpp>
#include <formatter/hunks.hpp>
#include <formatter/naming.hpp>
#include <formatter/parameter_names.hpp>
#include <tests/golden.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using test::golden::ReadFile;

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

void ExpectFormattedGolden(const std::filesystem::path& path,
    std::string_view input, const formatter::Style& style)
{
    const auto actual = FormatWithStyle(input, style);
    test::golden::ExpectGolden(path, actual);
    EXPECT_EQ(FormatWithStyle(actual, style), actual);
}

struct FormattedCase
{
    std::filesystem::path Directory;
    std::string Name;
    std::string Input = "input.cpp";
    std::string Config;
};

void AddCases(std::vector<FormattedCase>& cases,
    const std::filesystem::path& directory,
    std::initializer_list<const char*> names,
    std::string input = "input.cpp")
{
    for (const auto* name : names) {
        cases.push_back({directory, name, input, {}});
    }
}

std::vector<FormattedCase> FormattedCases()
{
    std::vector<FormattedCase> cases;
    AddCases(cases, BRACE_TESTDATA_DIR,
        {"preserve", "attach", "newline", "mixed"});
    AddCases(cases,
        std::filesystem::path(FORMATTER_TESTDATA_DIR) / "../include-order",
        {"sort", "composed"});
    cases.push_back({std::filesystem::path(FORMATTER_TESTDATA_DIR) /
        "../include-order", "crlf", "crlf.input.cpp", "sort"});
    AddCases(cases, BRACE_SPACING_TESTDATA_DIR,
        {"preserve", "single-space", "mixed"});
    AddCases(cases, LAMBDA_SPACING_TESTDATA_DIR,
        {"preserve", "space", "no-space"});
    AddCases(cases, CONTROL_SPACING_TESTDATA_DIR,
        {"preserve", "space", "no-space"});
    AddCases(cases, CONTROL_BRACE_TESTDATA_DIR,
        {"preserve", "same-line", "new-line", "attach-unless-multiline"});
    AddCases(cases, CONTROL_BRACE_TESTDATA_DIR, {"combined"},
        "combined.input.cpp");
    AddCases(cases, FUNCTION_SPACING_TESTDATA_DIR,
        {"preserve", "space", "no-space"});
    AddCases(cases, FUNCTION_SPACING_TESTDATA_DIR, {"reflow"},
        "reflow.input.cpp");
    AddCases(cases, FUNCTION_CALL_SPACING_TESTDATA_DIR,
        {"preserve", "space", "no-space"});
    AddCases(cases, FUNCTION_CALL_SPACING_TESTDATA_DIR, {"combined"},
        "combined.input.cpp");
    AddCases(cases, FUNCTION_INNER_SPACING_TESTDATA_DIR,
        {"preserve", "space", "no-space"});
    AddCases(cases, FUNCTION_INNER_SPACING_TESTDATA_DIR, {"reflow"},
        "reflow.input.cpp");
    AddCases(cases, CALL_INNER_SPACING_TESTDATA_DIR,
        {"preserve", "space", "no-space"});
    AddCases(cases, CALL_INNER_SPACING_TESTDATA_DIR, {"combined"},
        "combined.input.cpp");
    AddCases(cases, LAMBDA_CAPTURE_TESTDATA_DIR,
        {"preserve", "space", "no-space", "mixed", "reverse"});
    AddCases(cases, LAMBDA_INNER_PARAMETER_TESTDATA_DIR,
        {"preserve", "space", "no-space", "combined"});
    AddCases(cases, BODY_INDENT_TESTDATA_DIR,
        {"preserve", "indent", "no-indent", "mixed"});
    const auto chains = std::filesystem::path(FORMATTER_TESTDATA_DIR) /
        "../chained-calls";
    AddCases(cases, chains, {"always", "continuation", "preserve"});
    cases.push_back({chains, "overflow", "overflow.input.cpp"});
    AddCases(cases, std::filesystem::path(FORMATTER_TESTDATA_DIR) /
        "../separator-length", {"normalize", "preserve"});
    AddCases(cases, std::filesystem::path(FORMATTER_TESTDATA_DIR) /
        "../standalone-block-indent", {"preserve", "indent"});
    const auto closings = std::filesystem::path(FORMATTER_TESTDATA_DIR) /
        "../closing-delimiters";
    AddCases(cases, closings, {"same-line", "block", "preserve"});
    cases.push_back({closings, "new-line", "new-line.input.cpp"});
    cases.push_back({closings, "keep", "trailing-comma.input.cpp"});
    cases.push_back({closings, "remove", "trailing-comma.input.cpp"});
    AddCases(cases, std::filesystem::path(FORMATTER_TESTDATA_DIR) /
        "../fold-spacing", {"yt-like", "inherited", "group-space"});
    const auto profile = (std::filesystem::path(FORMATTER_TESTDATA_DIR) /
        "../../../../hit-fmt/examples/ytsaurus").lexically_normal().string();
    auto addProfileCase = [&] (const char* directory, const char* name) {
        cases.push_back({std::filesystem::path(FORMATTER_TESTDATA_DIR) /
            directory, name, std::string(name) + ".input.cpp", profile});
    };
    addProfileCase("../spacing-options", "profile-initializers");
    addProfileCase("../spacing-options", "profile-overflow");
    addProfileCase("../ternary-layout", "profile");
    addProfileCase("../assignment-location", "profile");
    addProfileCase("../function_parameters", "profile");
    addProfileCase("../body-indent", "profile-control-local");
    addProfileCase("../lambda-captures", "profile");
    addProfileCase("../call-reflow", "profile-tabbed-log");
    AddCases(cases, std::filesystem::path(FORMATTER_TESTDATA_DIR) /
        "../call-like-macro", {"selected"});
    AddCases(cases, std::filesystem::path(FORMATTER_TESTDATA_DIR) /
        "../lambda-body", {"preserve", "inherit", "inline"});
    AddCases(cases, std::filesystem::path(FORMATTER_TESTDATA_DIR) /
        "../control-header-clauses", {"preserve", "split",
            "indent-width"});
    cases.push_back({std::filesystem::path(FORMATTER_TESTDATA_DIR) /
        "../lambda-body", "profile", "input.cpp", profile});
    addProfileCase("../control-header-clauses", "profile");
    addProfileCase("../whitespace-sensitive-macros", "profile");
    addProfileCase("../call-reflow", "balanced-expressions");
    addProfileCase("../call-reflow", "profile-nested");
    addProfileCase("../chained-calls", "profile-multiline-lambda");
    const auto google = std::filesystem::path(FORMATTER_TESTDATA_DIR) /
        "../google-style-regressions";
    cases.push_back({google, "inline-record", "inline-record.input.cpp"});
    cases.push_back({google, "widths", "widths.input.cpp"});
    cases.push_back({google, "operator-spacing", "operator-spacing.input.cpp"});
    cases.push_back({google, "unbraced-switch", "unbraced-switch.input.cpp"});
    cases.push_back({google, "namespace-comments",
        "namespace-comments.input.cpp"});
    cases.push_back({google, "syntax-boundaries",
        "syntax-boundaries.input.cpp"});
    cases.push_back({google, "declarator-and-macro-safety",
        "declarator-and-macro-safety.input.cpp"});
    const auto disabled = std::filesystem::path(FORMATTER_TESTDATA_DIR) /
        "../format-disabled";
    cases.push_back({disabled, "regions", "regions.input.cpp"});
    cases.push_back({disabled, "unclosed", "unclosed.input.cpp", "regions"});
    cases.push_back({disabled, "crlf", "crlf.input.cpp", "regions"});
    const auto newArrays = std::filesystem::path(FORMATTER_TESTDATA_DIR) /
        "../new-array-declarators";
    AddCases(cases, newArrays, {"left", "array-space", "array-preserve"});
    AddCases(cases, google, {"punctuation-preserve", "punctuation-space",
        "comments-preserve"}, "punctuation-preserve.input.cpp");
    cases.push_back({google, "access-only", "access-scope.input.cpp"});
    cases.push_back({google, "scope-only", "access-scope-safe.input.cpp"});
    const auto attribute = std::filesystem::path(FORMATTER_TESTDATA_DIR) /
        "../attribute-indent";
    AddCases(cases, attribute, {"preserve", "block", "indent"});
    cases.push_back({attribute, "block-only", "block-only.input.cpp"});
    AddCases(cases, std::filesystem::path(FORMATTER_TESTDATA_DIR) /
        "../assignment-location", {"leading", "trailing"});
    AddCases(cases, std::filesystem::path(FORMATTER_TESTDATA_DIR) /
        "../call-expression-indent", {"aligned", "aligned-no-call-reflow",
            "indent-width", "preserve"});
    AddCases(cases, INHERITANCE_TESTDATA_DIR,
        {"preserve", "format", "leading", "trailing", "overflow",
            "comma-leading", "no-space", "spaces-mixed", "combined"});
    AddCases(cases, INHERITANCE_TESTDATA_DIR,
        {"split-preserve", "one-per-line", "pack-wide", "pack-tight",
            "split-combined"}, "split.input.cpp");
    for (const auto* name : {"parsed-operators", "outer-conditional",
        "control-braces-after-wrap"}) {
        cases.push_back({std::filesystem::path(FORMATTER_TESTDATA_DIR) /
            ".." / name, "configured"});
    }
    // Keep safety and recovered-syntax examples in separate parse units.
    const auto spacing = std::filesystem::path(FORMATTER_TESTDATA_DIR) /
        ".." / "spacing-options";
    cases.push_back({spacing, "cases-preserved", "indented-cases.input.cpp",
        "cases-preserved"});
    for (const auto& entry : std::filesystem::directory_iterator(spacing)) {
        const auto filename = entry.path().filename().string();
        if (filename.ends_with(".input.cpp")) {
            const auto name = filename.substr(0,
                filename.size() - std::string_view(".input.cpp").size());
            const auto duplicate = std::ranges::any_of(cases,
                [&] (const FormattedCase& value) {
                    return value.Directory.lexically_normal() ==
                        spacing.lexically_normal() && value.Name == name;
                });
            if (!duplicate) cases.push_back({spacing, name, filename});
        }
    }
    std::ranges::sort(cases, {}, [] (const auto& value) {
        return value.Directory.string() + value.Name;
    });
    return cases;
}

class FormattedGoldenTest : public testing::TestWithParam<FormattedCase> { };

TEST_P(FormattedGoldenTest, MatchesConfiguredOutput)
{
    const auto& value = GetParam();
    const test::golden::Fixture fixture{value.Directory, value.Name};
    const auto config = value.Config.empty() ? value.Name : value.Config;
    const auto style = formatter::LoadConfigFile(
        fixture.Directory / (config + ".hit-fmt")).FormattingStyle;
    ExpectFormattedGolden(fixture.Named(".output.cpp"),
        fixture.Input(value.Input), style);
}

INSTANTIATE_TEST_SUITE_P(Configurations, FormattedGoldenTest,
    testing::ValuesIn(FormattedCases()),
    [] (const auto& info) {
        auto name = info.param.Directory.filename().string() + "_" +
            info.param.Name;
        std::ranges::replace(name, '-', '_');
        return name;
    });

struct ChangedHunkCase
{
    std::string Directory;
    std::string Name;
    std::vector<formatter::Hunk> Hunks;
    bool Spillover;
};

class ChangedHunkGoldenTest : public testing::TestWithParam<ChangedHunkCase>
{ };

TEST_P(ChangedHunkGoldenTest, MatchesScopedOutput)
{
    const auto& value = GetParam();
    const auto directory = std::filesystem::path(FORMATTER_TESTDATA_DIR) /
        "../changed-hunks" / value.Directory;
    const test::golden::Fixture fixture{directory, value.Name};
    const auto input = fixture.Input();
    const auto style = formatter::LoadConfigFile(
        directory / "style.hit-fmt").FormattingStyle;
    std::string storage(input);
    storage.append(util::SourcePaddingSize, '\0');
    const auto tree = parser::Parse(
        {.Data = storage.data(), .Size = input.size()},
        [] (parser::ParseError) { });
    const auto actual = formatter::FormatChanged(tree, style, value.Hunks,
        value.Spillover);
    fixture.Expect(actual);
}

INSTANTIATE_TEST_SUITE_P(Scoped, ChangedHunkGoldenTest,
    testing::Values(
        ChangedHunkCase{"basic", "call", {{8, 8}}, true},
        ChangedHunkCase{"basic", "includes", {{1, 1}}, true},
        ChangedHunkCase{"basic", "includes-strict", {{1, 1}}, false},
        ChangedHunkCase{"basic", "function", {{4, 4}}, true},
        ChangedHunkCase{"spill", "default", {{3, 3}, {5, 5}}, true},
        ChangedHunkCase{"spill", "strict", {{3, 3}, {5, 5}}, false},
        ChangedHunkCase{"structural", "unsupported", {{2, 2}}, true}),
    [] (const auto& info) {
        auto name = info.param.Directory + "_" + info.param.Name;
        std::ranges::replace(name, '-', '_');
        return name;
    });

class FunctionParameterGoldenTest : public testing::TestWithParam<std::string>
{ };

TEST_P(FunctionParameterGoldenTest, MatchesConfiguredOutput)
{
    const auto directory = std::filesystem::path(FORMATTER_TESTDATA_DIR) /
        "../function_parameters";
    const auto input = ReadFile(directory / "input.cpp");
    const auto config = formatter::LoadConfigFile(
        directory / (GetParam() + ".hit-fmt"));

    std::string storage(input);
    storage.append(util::SourcePaddingSize, '\0');
    const auto actual = formatter::Format(
        {.Data = storage.data(), .Size = input.size()},
        config.FormattingStyle);
    test::golden::ExpectGolden(directory / (GetParam() + ".output.cpp"),
        actual);

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
        (GetParam() == "crlf_fix" ? "crlf.input.cpp" : "input.cpp"));
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
    const auto actual = format(input);
    test::golden::ExpectGolden(directory / (GetParam() + ".output.cpp"), actual);
    EXPECT_EQ(format(actual), actual);
}

INSTANTIATE_TEST_SUITE_P(Configurations, NamespaceCommentGoldenTest,
    testing::Values("preserve", "remove", "add_missing", "fix",
        "fix_spaced", "fix_newline", "crlf_fix"));

class ParameterNameGoldenTest : public testing::TestWithParam<std::string>
{ };

TEST_P(ParameterNameGoldenTest, ReportsConfiguredMissingNames)
{
    const auto directory = std::filesystem::path(FORMATTER_TESTDATA_DIR) /
        "../parameter_names";
    const auto input = ReadFile(directory / "input.cpp");
    const bool ytsaurus = GetParam() == "ytsaurus";
    const auto configPath = ytsaurus
        ? (directory / "../../../../hit-fmt/examples/ytsaurus.hit-fmt")
              .lexically_normal()
        : directory / (GetParam() + ".hit-fmt");
    const auto config = formatter::LoadConfigFile(configPath);
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
    test::golden::ExpectGolden(directory /
        (ytsaurus ? "enabled.expected" : GetParam() + ".expected"), actual);
}

INSTANTIATE_TEST_SUITE_P(Configurations, ParameterNameGoldenTest,
    testing::Values("enabled", "default", "declarations_only",
        "definitions_only", "names_only", "ytsaurus"));

TEST(CallArgumentTest, ReflowsOnlySafeParsedCalls)
{
    formatter::Style style;
    style.ColumnLimit = 34;
    const auto directory = std::filesystem::path(FORMATTER_TESTDATA_DIR) /
        "../call-reflow";
    const auto input = ReadFile(directory / "input.cpp");
    EXPECT_EQ(FormatWithStyle(input, style), input);
    style.CallArgumentSplit = formatter::CallArgumentSplit::Pack;
    style.ExpressionLayoutCondition = formatter::ExpressionLayoutCondition::Overflow;
    const auto packed = FormatWithStyle(input, style);
    test::golden::ExpectGolden(directory / "packed.output.cpp",
        packed);
    EXPECT_EQ(FormatWithStyle(packed, style), packed);
    style.ExpressionLayoutCondition = formatter::ExpressionLayoutCondition::FitOrOverflow;
    const auto merged = FormatWithStyle(packed, style);
    test::golden::ExpectGolden(directory / "merged.output.cpp",
        merged);
    EXPECT_EQ(FormatWithStyle(merged, style), merged);
    const auto longMultiline = ReadFile(directory / "long.input.cpp");
    EXPECT_EQ(FormatWithStyle(longMultiline, style), longMultiline);
    style.ExpressionLayoutCondition = formatter::ExpressionLayoutCondition::Always;
    const auto repacked = FormatWithStyle(longMultiline, style);
    test::golden::ExpectGolden(directory / "repacked.output.cpp",
        repacked);
    EXPECT_EQ(FormatWithStyle(repacked, style), repacked);

    style.CallArgumentSplit = formatter::CallArgumentSplit::OnePerLine;
    style.FirstCallArgumentPosition =
        formatter::FirstCallArgumentPosition::NewLine;
    const auto onePerLine = FormatWithStyle(input, style);
    test::golden::ExpectGolden(directory / "one-per-line.output.cpp",
        onePerLine);
    EXPECT_EQ(FormatWithStyle(onePerLine, style), onePerLine);

    style.CallArgumentSplit = formatter::CallArgumentSplit::Pack;
    style.EnforceFirstCallArgumentPosition = true;
    style.CallArgumentIndent = formatter::CallArgumentIndent::Continuation;
    style.ColumnLimit = 25;
    const auto repositioned =
        "void f() {\n"
        "    use(alpha,\n"
        "        beta, gamma);\n"
        "}\n";
    const auto repositionedExpected =
        "void f() {\n"
        "    use(\n"
        "        alpha, beta,\n"
        "        gamma);\n"
        "}\n";
    EXPECT_EQ(FormatWithStyle(repositioned, style), repositionedExpected);
    EXPECT_EQ(FormatWithStyle(repositionedExpected, style),
        repositionedExpected);
    style.FirstCallArgumentPosition =
        formatter::FirstCallArgumentPosition::CallLine;
    style.ColumnLimit = 32;
    const auto aligned =
        "void f() {\n"
        "    longCall(alpha, beta, gamma, delta);\n"
        "}\n";
    const auto alignedExpected =
        "void f() {\n"
        "    longCall(alpha, beta, gamma,\n"
        "             delta);\n"
        "}\n";
    EXPECT_EQ(FormatWithStyle(aligned, style), alignedExpected);
    EXPECT_EQ(FormatWithStyle(alignedExpected, style), alignedExpected);
    style.CallArgumentIndent = formatter::CallArgumentIndent::IndentWidth;
    style.ControlBlockBracePlacement =
        formatter::ControlBlockBracePlacement::AttachUnlessMultiline;
    const auto control =
        "void f() {\n"
        "    if (check(alpha, beta, gamma, delta)) {\n"
        "        run();\n"
        "    }\n"
        "}\n";
    const auto controlExpected =
        "void f() {\n"
        "    if (check(alpha, beta,\n"
        "        gamma, delta))\n"
        "    {\n"
        "        run();\n"
        "    }\n"
        "}\n";
    EXPECT_EQ(FormatWithStyle(control, style), controlExpected);
    EXPECT_EQ(FormatWithStyle(controlExpected, style), controlExpected);
    style.ExpressionLayoutCondition = formatter::ExpressionLayoutCondition::Overflow;
    ExpectUnchanged("void f() { use(alpha +\n        beta, gamma, delta); }\n",
        style);
    ExpectUnchanged("void f() { auto x = (Type)(alpha, beta, gamma, delta); }\n",
        style);
    ExpectUnchanged("void f() { use(alpha, /* note */ beta, gamma); }\n",
        style);
    ExpectUnchanged("void f() { use(alpha, beta, gamma, delta); }\r\n",
        style);

    style.ControlBlockBracePlacement =
        formatter::ControlBlockBracePlacement::Preserve;
    style.CallArgumentSplit = formatter::CallArgumentSplit::OnePerLine;
    style.ExpressionLayoutCondition = formatter::ExpressionLayoutCondition::Always;
    style.SpaceInsideCallArguments = formatter::SpaceInsideCallArguments::Space;
    style.ColumnLimit = 25;
    const auto spaced = "void f() { use(alpha, beta, gamma, delta); }\n";
    const auto spacedExpected =
        "void f() { use( alpha,\n"
        "    beta,\n"
        "    gamma,\n"
        "    delta ); }\n";
    EXPECT_EQ(FormatWithStyle(spaced, style), spacedExpected);
    EXPECT_EQ(FormatWithStyle(spacedExpected, style), spacedExpected);

    const auto balancedInput =
        "void f() {\n"
        "    use(a, bb, ccc, dddd, eeeee, ffffff);\n"
        "}\n";
    const auto balanced =
        "void f() {\n"
        "    use(a, bb, ccc, dddd,\n"
        "        eeeee, ffffff);\n"
        "}\n";
    style.ColumnLimit = 32;
    style.ExpressionLayoutCondition = formatter::ExpressionLayoutCondition::Overflow;
    style.CallArgumentSplit = formatter::CallArgumentSplit::Pack;
    style.SpaceInsideCallArguments = formatter::SpaceInsideCallArguments::Preserve;
    style.ExpressionLayoutAlgorithm =
        formatter::ExpressionLayoutAlgorithm::DynamicProgramming;
    EXPECT_EQ(FormatWithStyle(balancedInput, style), balanced);
    EXPECT_EQ(FormatWithStyle(balanced, style), balanced);
    style.CallArgumentSplit = formatter::CallArgumentSplit::OnePerLine;
    const auto onePerLineBalanced =
        "void f() {\n"
        "    use(a,\n"
        "        bb,\n"
        "        ccc,\n"
        "        dddd,\n"
        "        eeeee,\n"
        "        ffffff);\n"
        "}\n";
    EXPECT_EQ(FormatWithStyle(balancedInput, style), onePerLineBalanced);
    style.CallArgumentSplit = formatter::CallArgumentSplit::Preserve;
    ExpectUnchanged(balancedInput, style);

    formatter::Style sole;
    sole.CallArgumentSplit = formatter::CallArgumentSplit::OnePerLine;
    sole.ExpressionLayoutCondition =
        formatter::ExpressionLayoutCondition::Overflow;
    sole.TernarySplit = formatter::TernarySplit::ThreeLines;
    sole.TernaryOperatorPlacement =
        formatter::TernaryOperatorPlacement::Leading;
    const auto multiline =
        "void f() {\n"
        "    auto result = Select(status\n"
        "        ? first\n"
        "        : second);\n"
        "    auto callback = MakeCallback([&] {\n"
        "        Run();\n"
        "    });\n"
        "}\n";
    ExpectUnchanged(multiline, sole);
    sole.MultilineSingleCallArgumentPosition =
        formatter::MultilineSingleCallArgumentPosition::NewLine;
    const auto separated =
        "void f() {\n"
        "    auto result = Select(\n"
        "        status\n"
        "            ? first\n"
        "            : second);\n"
        "    auto callback = MakeCallback([&] {\n"
        "        Run();\n"
        "    });\n"
        "}\n";
    EXPECT_EQ(FormatWithStyle(multiline, sole), separated);
    EXPECT_EQ(FormatWithStyle(separated, sole), separated);

    formatter::Style lambdaStyle;
    lambdaStyle.ColumnLimit = 32;
    lambdaStyle.CallArgumentSplit = formatter::CallArgumentSplit::OnePerLine;
    lambdaStyle.IndentBlockBodies = true;
    const auto lambdaInput =
        "void f() {\n"
        "    CallWithLongName([first, second] {\n"
        "        Process();\n"
        "    });\n"
        "}\n";
    const auto lambdaExpected =
        "void f() {\n"
        "    CallWithLongName(\n"
        "        [first, second] {\n"
        "            Process();\n"
        "        });\n"
        "}\n";
    EXPECT_EQ(FormatWithStyle(lambdaInput, lambdaStyle), lambdaExpected);
    EXPECT_EQ(FormatWithStyle(lambdaExpected, lambdaStyle), lambdaExpected);
    ExpectUnchanged(
        "void f() {\n"
        "    CallWithLongName([first = MakeVeryLongValue(), second] {\n"
        "        Process();\n"
        "    });\n"
        "}\n", lambdaStyle);
}

TEST(CallArgumentTest, LaysOutEnclosingMacroAndNestedCalls)
{
    formatter::Style style;
    style.ExpressionLayoutCondition = formatter::ExpressionLayoutCondition::Always;
    style.CallArgumentSplit = formatter::CallArgumentSplit::Pack;
    style.ColumnLimit = 100;
    const test::golden::Fixture fixture{
        std::filesystem::path(FORMATTER_TESTDATA_DIR) / "../macro-nested-calls",
        "configured"};
    const auto input = fixture.Input();
    std::optional<std::string> firstOutput;
    for (auto mode : {formatter::ExpressionLayoutCondition::Overflow,
        formatter::ExpressionLayoutCondition::FitOrOverflow,
        formatter::ExpressionLayoutCondition::Always})
    {
        style.ExpressionLayoutCondition = mode;
        for (auto algorithm : {formatter::ExpressionLayoutAlgorithm::Greedy,
            formatter::ExpressionLayoutAlgorithm::DynamicProgramming})
        {
            style.ExpressionLayoutAlgorithm = algorithm;
            const auto actual = FormatWithStyle(input, style);
            if (!firstOutput) {
                firstOutput = actual;
                fixture.Expect(actual);
            } else {
                EXPECT_EQ(actual, *firstOutput);
            }
            EXPECT_EQ(FormatWithStyle(actual, style), actual);
        }
    }
    style.CallArgumentSplit = formatter::CallArgumentSplit::OnePerLine;
    ExpectUnchanged("void f() { EXPECT_EQ(a, b); }\n", style);
}

TEST(ExpressionLayoutTest, WrapsParsedOperatorsAndPreservesUnsafeLines)
{
    formatter::Style style;
    style.ExpressionLayoutCondition = formatter::ExpressionLayoutCondition::Overflow;
    style.TernarySplit = formatter::TernarySplit::AsNeeded;
    style.TernaryOperatorPlacement = formatter::TernaryOperatorPlacement::Leading;
    style.BinaryOperatorPlacement = formatter::BinaryOperatorPlacement::Leading;
    style.ColumnLimit = 32;

    const test::golden::Fixture fixture{
        std::filesystem::path(FORMATTER_TESTDATA_DIR) / "../parsed-operators",
        "configured"};
    ExpectUnchanged(fixture.Input(), formatter::Style{});

    style.BinaryOperatorPlacement = formatter::BinaryOperatorPlacement::Trailing;
    const auto trailingInput =
        "int sum(int alpha, int beta) {\n"
        "    return alpha + beta + alpha + beta;\n"
        "}\n";
    const auto trailingExpected =
        "int sum(int alpha, int beta) {\n"
        "    return alpha + beta +\n"
        "        alpha + beta;\n"
        "}\n";
    EXPECT_EQ(FormatWithStyle(trailingInput, style), trailingExpected);
    EXPECT_EQ(FormatWithStyle(trailingExpected, style), trailingExpected);
    const auto assigned =
        "void f(int alpha, int beta, int& result) {\n"
        "    result = alpha + beta + alpha + beta;\n"
        "}\n";
    const auto assignedExpected =
        "void f(int alpha, int beta, int& result) {\n"
        "    result\n"
        "        = alpha + beta + alpha +\n"
        "        beta;\n"
        "}\n";
    EXPECT_EQ(FormatWithStyle(assigned, style), assignedExpected);
    EXPECT_EQ(FormatWithStyle(assignedExpected, style), assignedExpected);
    style.ColumnLimit = 25;
    const auto compact =
        "int sum(int alpha, int beta) {\n"
        "    return alpha+beta+alpha+beta;\n"
        "}\n";
    const auto compactExpected =
        "int sum(int alpha, int beta) {\n"
        "    return alpha+beta+\n"
        "        alpha+beta;\n"
        "}\n";
    EXPECT_EQ(FormatWithStyle(compact, style), compactExpected);
    EXPECT_EQ(FormatWithStyle(compactExpected, style), compactExpected);
    ExpectUnchanged("int f(int a, int b) { return a + b + a + b; }\r\n",
        style);
    ExpectUnchanged("#define SUM(a, b) ((a) + (b) + (a) + (b))\n",
        style);
    ExpectUnchanged("int compute(int a, int b) { return a + b + a + b; // keep\n}\n",
        style);
    ExpectUnchanged("int compute(int a, int b) { return a + b @ broken; }\n",
        style);
    ExpectUnchanged("template <int N> struct Array {};\n"
        "Array<1 + 2 + 3 + 4 + 5 + 6> value;\n", style);
}

TEST(ExpressionLayoutTest, PlacesAndGroupsTernaryBreaks)
{
    formatter::Style style;
    style.ExpressionLayoutCondition = formatter::ExpressionLayoutCondition::Overflow;
    style.ColumnLimit = 40;
    const auto directory = std::filesystem::path(FORMATTER_TESTDATA_DIR) /
        "../ternary-layout";
    const auto input = ReadFile(directory / "input.cpp");
    std::array<std::optional<std::string>, 4> firstOutputs;
    const auto verify = [&] (size_t index, const char* filename,
        std::string_view actual) {
        if (firstOutputs[index]) {
            EXPECT_EQ(actual, *firstOutputs[index]);
        } else {
            firstOutputs[index] = actual;
            test::golden::ExpectGolden(directory / filename, actual);
        }
    };
    for (auto algorithm : {formatter::ExpressionLayoutAlgorithm::Greedy,
        formatter::ExpressionLayoutAlgorithm::DynamicProgramming})
    {
        style.ExpressionLayoutAlgorithm = algorithm;
        for (auto placement : {formatter::TernaryOperatorPlacement::Leading,
            formatter::TernaryOperatorPlacement::Trailing})
        {
            style.TernaryOperatorPlacement = placement;
            style.TernarySplit = formatter::TernarySplit::AsNeeded;
            const bool leading =
                placement == formatter::TernaryOperatorPlacement::Leading;
            const auto two = FormatWithStyle(input, style);
            verify(leading ? 0 : 1,
                leading ? "two-leading.output.cpp" : "two-trailing.output.cpp",
                two);
            EXPECT_EQ(FormatWithStyle(two, style), two);
            style.TernarySplit = formatter::TernarySplit::ThreeLines;
            const auto three = FormatWithStyle(input, style);
            verify(leading ? 2 : 3,
                leading ? "three-leading.output.cpp" : "three-trailing.output.cpp",
                three);
            EXPECT_EQ(FormatWithStyle(three, style), three);
            ExpectUnchanged("int f() { return flag ? a : b; }\n", style);
        }
    }

    const auto threeLeading = ReadFile(directory / "three-leading.output.cpp");
    const auto threeTrailing = ReadFile(directory / "three-trailing.output.cpp");

    style.ColumnLimit = 80;
    style.TernaryOperatorPlacement = formatter::TernaryOperatorPlacement::Leading;
    style.TernarySplit = formatter::TernarySplit::AsNeeded;
    const auto existing =
        "int f(bool condition) {\n"
        "    return condition ?\n"
        "        alphaValue : betaValue;\n"
        "}\n";
    const auto corrected =
        "int f(bool condition) {\n"
        "    return condition\n"
        "        ? alphaValue : betaValue;\n"
        "}\n";
    EXPECT_EQ(FormatWithStyle(existing, style), corrected);
    const auto bothSides =
        "int f(bool condition) {\n"
        "    return condition\n"
        "        ?\n"
        "        alphaValue : betaValue;\n"
        "}\n";
    EXPECT_EQ(FormatWithStyle(bothSides, style), corrected);
    style.TernarySplit = formatter::TernarySplit::ThreeLines;
    EXPECT_EQ(FormatWithStyle(existing, style), threeLeading);
    EXPECT_EQ(FormatWithStyle(bothSides, style), threeLeading);
    EXPECT_EQ(FormatWithStyle(threeLeading, style), threeLeading);
    const auto chain =
        "int f() {\n"
        "    return one ? a :\n"
        "        two ? b :\n"
        "        three ? c : d;\n"
        "}\n";
    const auto alignedChain =
        "int f() {\n"
        "    return one\n"
        "        ? a\n"
        "        : two\n"
        "            ? b\n"
        "            : three ? c : d;\n"
        "}\n";
    EXPECT_EQ(FormatWithStyle(chain, style), alignedChain);
    EXPECT_EQ(FormatWithStyle(alignedChain, style), alignedChain);
    style.TernaryOperatorPlacement = formatter::TernaryOperatorPlacement::Trailing;
    EXPECT_EQ(FormatWithStyle(corrected, style), threeTrailing);
    for (auto mode : {formatter::ExpressionLayoutCondition::FitOrOverflow,
        formatter::ExpressionLayoutCondition::Always})
    {
        style.ExpressionLayoutCondition = mode;
        EXPECT_EQ(FormatWithStyle(existing, style), input);
    }
    style.ExpressionLayoutCondition = formatter::ExpressionLayoutCondition::Preserve;
    ExpectUnchanged(existing, style);
    style.ExpressionLayoutCondition = formatter::ExpressionLayoutCondition::Overflow;
    ExpectUnchanged("int f() {\r\n    return condition ?\r\n"
        "        alphaValue : betaValue;\r\n}\r\n", style);
    style.TernaryOperatorPlacement = formatter::TernaryOperatorPlacement::Trailing;
    style.TernarySplit = formatter::TernarySplit::ThreeLines;
    ExpectUnchanged("int f() {\n"
        "    return condition ? alphaValue /* keep */\n"
        "        : betaValue;\n"
        "}\n", style);
    const auto nested =
        "int f() {\n"
        "    return outer ? inner ? alpha\n"
        "        : beta : gamma;\n"
        "}\n";
    const auto nestedExpected =
        "int f() {\n"
        "    return outer ? inner ?\n"
        "        alpha :\n"
        "        beta : gamma;\n"
        "}\n";
    EXPECT_EQ(FormatWithStyle(nested, style), nestedExpected);
    EXPECT_EQ(FormatWithStyle(nestedExpected, style), nestedExpected);

    formatter::Style preserve;
    preserve.ExpressionLayoutCondition =
        formatter::ExpressionLayoutCondition::Always;
    preserve.ColumnLimit = 20;
    ExpectUnchanged(input, preserve);
    ExpectUnchanged(existing, preserve);
    preserve.ExpressionLayoutCondition =
        formatter::ExpressionLayoutCondition::Overflow;
    preserve.ColumnLimit = 80;
    preserve.TernaryOperatorPlacement =
        formatter::TernaryOperatorPlacement::Leading;
    EXPECT_EQ(FormatWithStyle(existing, preserve), corrected);
    preserve.TernaryOperatorPlacement =
        formatter::TernaryOperatorPlacement::Preserve;
    preserve.TernarySplit = formatter::TernarySplit::AsNeeded;
    ExpectUnchanged(existing, preserve);
    preserve.TernarySplit = formatter::TernarySplit::ThreeLines;
    EXPECT_EQ(FormatWithStyle(existing, preserve), threeTrailing);
}

TEST(ExpressionLayoutTest, ChecksLayoutWithoutReflowing)
{
    const auto input =
        "int f() {\n"
        "    auto description = someVeryLongIdentifierName;\n"
        "    auto x = left\n"
        "        + right;\n"
        "    return condition ?\n"
        "        alphaValue : betaValue;\n"
        "}\n";
    std::string storage(input);
    auto length = storage.size();
    storage.append(util::SourcePaddingSize, '\0');
    auto tree = parser::Parse({.Data = storage.data(), .Size = length},
        [] (parser::ParseError) { });
    formatter::Style style;
    style.ExpressionLayoutCondition = formatter::ExpressionLayoutCondition::Overflow;
    style.ColumnLimit = 40;
    style.TernaryOperatorPlacement = formatter::TernaryOperatorPlacement::Leading;
    style.TernarySplit = formatter::TernarySplit::ThreeLines;
    const auto issues = formatter::CheckExpressionLayout(tree, style);
    ASSERT_EQ(issues.size(), 4u);
    EXPECT_EQ(issues[0].Position.Line, 2u);
    EXPECT_NE(issues[0].Message.find("line exceeds ColumnLimit"),
        std::string::npos);
    EXPECT_EQ(issues[1].Position.Line, 4u);
    EXPECT_EQ(issues[1].Message, "binary operator must end the preceding line");
    EXPECT_EQ(issues[2].Position.Line, 5u);
    EXPECT_EQ(issues[2].Message, "ternary ? must start the following line");
    EXPECT_EQ(issues[3].Position.Line, 6u);
    EXPECT_EQ(issues[3].Message,
        "ternary ':' needs a line break to match the other delimiter");
    style.TernaryOperatorPlacement = formatter::TernaryOperatorPlacement::Preserve;
    style.TernarySplit = formatter::TernarySplit::Preserve;
    EXPECT_EQ(formatter::CheckExpressionLayout(tree, style).size(), 2u);
    style.ExpressionLayoutCondition =
        formatter::ExpressionLayoutCondition::Preserve;
    EXPECT_TRUE(formatter::CheckExpressionLayout(tree, style).empty());
}

TEST(ExpressionLayoutTest, SkipsFormatDisabledRegionsAgainstGolden)
{
    const auto directory = std::filesystem::path(FORMATTER_TESTDATA_DIR) /
        "../format-disabled";
    auto source = ReadFile(directory / "regions.input.cpp");
    const auto size = source.size();
    source.append(util::SourcePaddingSize, '\0');
    const auto tree = parser::Parse(
        {.Data = source.data(), .Size = size},
        [] (parser::ParseError) { });
    const auto style = formatter::LoadConfigFile(
        directory / "regions.hit-fmt").FormattingStyle;
    std::string actual;
    for (const auto& diagnostic :
        formatter::CheckExpressionLayout(tree, style))
    {
        actual += std::to_string(diagnostic.Position.Line) + ":" +
            std::to_string(diagnostic.Position.Column) + ": " +
            diagnostic.Message + "\n";
    }
    test::golden::ExpectGolden(directory / "regions.layout.expected", actual);
}

TEST(ExpressionLayoutTest, ChecksAssignmentPlacementAgainstGolden)
{
    const auto directory = std::filesystem::path(FORMATTER_TESTDATA_DIR) /
        "../assignment-location";
    auto source = ReadFile(directory / "input.cpp");
    const auto size = source.size();
    source.append(util::SourcePaddingSize, '\0');
    const auto tree = parser::Parse(
        {.Data = source.data(), .Size = size},
        [] (parser::ParseError) { });
    for (const auto* placement : {"leading", "trailing"}) {
        const auto style = formatter::LoadConfigFile(
            directory / (std::string(placement) + ".hit-fmt"))
            .FormattingStyle;
        std::string actual;
        for (const auto& diagnostic :
            formatter::CheckExpressionLayout(tree, style))
        {
            actual += std::to_string(diagnostic.Position.Line) + ":" +
                std::to_string(diagnostic.Position.Column) + ": " +
                diagnostic.Message + "\n";
        }
        test::golden::ExpectGolden(
            directory / (std::string(placement) + ".expected"), actual);
    }
}

TEST(ExpressionLayoutTest, PrefersOuterConditionalBreaks)
{
    formatter::Style style;
    style.ExpressionLayoutCondition = formatter::ExpressionLayoutCondition::Overflow;
    style.ControlBlockBracePlacement =
        formatter::ControlBlockBracePlacement::AttachUnlessMultiline;
    style.ColumnLimit = 120;
    const test::golden::Fixture fixture{
        std::filesystem::path(FORMATTER_TESTDATA_DIR) / "../outer-conditional",
        "configured"};
    style.ExpressionLayoutAlgorithm =
        formatter::ExpressionLayoutAlgorithm::DynamicProgramming;
    ExpectFormattedGolden(fixture.Named(".output.cpp"),
        fixture.Input(), style);
}

TEST(ExpressionLayoutTest, ComparesGreedyAndBoundedDynamicProgramming)
{
    formatter::Style style;
    style.ExpressionLayoutCondition = formatter::ExpressionLayoutCondition::Overflow;
    style.ColumnLimit = 40;
    const auto input =
        "void f() {\n"
        "    return a + bb + ccc + dddd + eeeee + ffffff;\n"
        "}\n";
    const auto greedy =
        "void f() {\n"
        "    return a + bb + ccc + dddd + eeeee +\n"
        "        ffffff;\n"
        "}\n";
    const auto balanced =
        "void f() {\n"
        "    return a + bb + ccc + dddd +\n"
        "        eeeee + ffffff;\n"
        "}\n";
    EXPECT_EQ(FormatWithStyle(input, style), greedy);
    style.ExpressionLayoutAlgorithm =
        formatter::ExpressionLayoutAlgorithm::DynamicProgramming;
    EXPECT_EQ(FormatWithStyle(input, style), balanced);
    EXPECT_EQ(FormatWithStyle(balanced, style), balanced);
    style.BinaryOperatorPlacement = formatter::BinaryOperatorPlacement::Leading;
    const auto leading =
        "void f() {\n"
        "    return a + bb + ccc + dddd\n"
        "        + eeeee + ffffff;\n"
        "}\n";
    EXPECT_EQ(FormatWithStyle(input, style), leading);
    style.BinaryOperatorPlacement = formatter::BinaryOperatorPlacement::Trailing;
    style.ExpressionLayoutCondition = formatter::ExpressionLayoutCondition::Always;
    EXPECT_EQ(FormatWithStyle(greedy, style), balanced);
    std::string many = "void f() {\n    return a";
    for (int i = 0; i < 100; ++i) {
        many += " + a";
    }
    many += ";\n}\n";
    style.ExpressionLayoutCondition = formatter::ExpressionLayoutCondition::Overflow;
    style.ExpressionLayoutAlgorithm = formatter::ExpressionLayoutAlgorithm::Greedy;
    const auto fallback = FormatWithStyle(many, style);
    style.ExpressionLayoutAlgorithm =
        formatter::ExpressionLayoutAlgorithm::DynamicProgramming;
    EXPECT_EQ(FormatWithStyle(many, style), fallback);
    style.ExpressionLayoutCondition = formatter::ExpressionLayoutCondition::Preserve;
    ExpectUnchanged(input, style);
    style.ExpressionLayoutCondition = formatter::ExpressionLayoutCondition::Always;
    style.ColumnLimit = 20;
    ExpectUnchanged("void f() { bool v = alpha<beta; }\n", style);
}

TEST(ExpressionLayoutTest, ReflowsSafeOperatorContinuationsByPolicy)
{
    formatter::Style style;
    style.ExpressionLayoutCondition = formatter::ExpressionLayoutCondition::Always;
    style.TernarySplit = formatter::TernarySplit::AsNeeded;
    style.TernaryOperatorPlacement = formatter::TernaryOperatorPlacement::Leading;
    style.ColumnLimit = 80;
    const auto input =
        "void f() {\n"
        "    a=b+c;\n"
        "    return flag\n"
        "        ? a + b\n"
        "        : a * c;\n"
        "}\n";
    const auto flat =
        "void f() {\n"
        "    a = b + c;\n"
        "    return flag ? a + b : a * c;\n"
        "}\n";
    EXPECT_EQ(FormatWithStyle(input, style), flat);
    EXPECT_EQ(FormatWithStyle(flat, style), flat);

    const auto wrapped =
        "int f(int alpha, int beta, int gamma) {\n"
        "    return alpha\n"
        "        + beta\n"
        "        + gamma;\n"
        "}\n";
    style.ColumnLimit = 30;
    style.BinaryOperatorPlacement = formatter::BinaryOperatorPlacement::Leading;
    style.ExpressionLayoutCondition = formatter::ExpressionLayoutCondition::FitOrOverflow;
    EXPECT_EQ(FormatWithStyle(wrapped, style), wrapped);
    const auto mixed =
        "void f(int alpha, int beta, int gamma) {\n"
        "    int result = alpha\n"
        "        + beta;\n"
        "    result = alpha\n"
        "        + beta\n"
        "        + gamma;\n"
        "}\n";
    const auto partiallyJoined =
        "void f(int alpha, int beta, int gamma) {\n"
        "    int result = alpha + beta;\n"
        "    result = alpha\n"
        "        + beta\n"
        "        + gamma;\n"
        "}\n";
    EXPECT_EQ(FormatWithStyle(mixed, style), partiallyJoined);
    EXPECT_EQ(FormatWithStyle(partiallyJoined, style), partiallyJoined);
    style.ColumnLimit = 40;
    const auto joined =
        "int f(int alpha, int beta, int gamma) {\n"
        "    return alpha + beta + gamma;\n"
        "}\n";
    EXPECT_EQ(FormatWithStyle(wrapped, style), joined);
    EXPECT_EQ(FormatWithStyle(joined, style), joined);
    style.ExpressionLayoutCondition = formatter::ExpressionLayoutCondition::Overflow;
    EXPECT_EQ(FormatWithStyle(wrapped, style), wrapped);
    style.BinaryOperatorPlacement =
        formatter::BinaryOperatorPlacement::Trailing;
    const auto repositioned =
        "int f(int alpha, int beta, int gamma) {\n"
        "    return alpha +\n"
        "        beta +\n"
        "        gamma;\n"
        "}\n";
    EXPECT_EQ(FormatWithStyle(wrapped, style), repositioned);
    EXPECT_EQ(FormatWithStyle(repositioned, style), repositioned);
    style.BinaryOperatorPlacement = formatter::BinaryOperatorPlacement::Leading;
    EXPECT_EQ(FormatWithStyle(repositioned, style), wrapped);
    style.BinaryOperatorPlacement = formatter::BinaryOperatorPlacement::Trailing;
    style.ColumnLimit = 30;
    style.ExpressionLayoutCondition = formatter::ExpressionLayoutCondition::Always;
    const auto trailing =
        "int f(int alpha, int beta, int gamma) {\n"
        "    return alpha + beta +\n"
        "        gamma;\n"
        "}\n";
    EXPECT_EQ(FormatWithStyle(wrapped, style), trailing);
    EXPECT_EQ(FormatWithStyle(trailing, style), trailing);
    const auto compact =
        "int f(int alpha, int beta, int gamma) {\n"
        "    return alpha+beta+gamma+alpha;\n"
        "}\n";
    const auto spaced =
        "int f(int alpha, int beta, int gamma) {\n"
        "    return alpha + beta +\n"
        "        gamma + alpha;\n"
        "}\n";
    EXPECT_EQ(FormatWithStyle(compact, style), spaced);
    EXPECT_EQ(FormatWithStyle(spaced, style), spaced);
    style.BinaryOperatorPlacement = formatter::BinaryOperatorPlacement::Leading;
    const auto leading =
        "int f(int alpha, int beta, int gamma) {\n"
        "    return alpha + beta\n"
        "        + gamma;\n"
        "}\n";
    EXPECT_EQ(FormatWithStyle(wrapped, style), leading);
    EXPECT_EQ(FormatWithStyle(leading, style), leading);
    style.ColumnLimit = 80;
    ExpectUnchanged("void f() { call(alpha, beta, gamma); }\n", style);
    ExpectUnchanged("void f() { int x = a + b; // keep\n}\n", style);
    ExpectUnchanged("void f() { int x = a + b; }\r\n", style);
    const auto ambiguous =
        "template<class... Args> void f() {\n"
        "    static_assert(TDependentFalse<Args...>, "
        "\"Wrong number of arguments\");\n"
        "}\n";
    for (auto mode : {formatter::ExpressionLayoutCondition::Overflow,
        formatter::ExpressionLayoutCondition::FitOrOverflow,
        formatter::ExpressionLayoutCondition::Always})
    {
        style.ExpressionLayoutCondition = mode;
        style.ColumnLimit = 32;
        ExpectUnchanged(ambiguous, style);
        ExpectUnchanged("void f() { bool v = alpha<beta; }\n", style);
    }
}

TEST(ExpressionLayoutTest, ReconsidersControlBracesAfterWrapping)
{
    formatter::Style style;
    style.ExpressionLayoutCondition = formatter::ExpressionLayoutCondition::Overflow;
    style.ControlBlockBracePlacement =
        formatter::ControlBlockBracePlacement::AttachUnlessMultiline;
    style.ColumnLimit = 32;
    style.ExpressionLayoutCondition = formatter::ExpressionLayoutCondition::Always;
    style.ColumnLimit = 80;
    const auto multiline =
        "void f(bool alpha, bool beta) {\n"
        "    if (alpha &&\n"
        "        beta)\n"
        "    {\n"
        "        run();\n"
        "    }\n"
        "}\n";
    const auto attached =
        "void f(bool alpha, bool beta) {\n"
        "    if (alpha && beta) {\n"
        "        run();\n"
        "    }\n"
        "}\n";
    EXPECT_EQ(FormatWithStyle(multiline, style), attached);
    EXPECT_EQ(FormatWithStyle(attached, style), attached);
}

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
    style.IndentationKind = formatter::IndentationKind::Tabs;
    const std::string tabbed =
        "void Many(int alpha, int beta,\n"
        "\tint gamma, int delta)\n"
        "{}\n";
    EXPECT_EQ(format("void Many(int alpha, int beta, int gamma, int delta) {}\n"),
        tabbed);
    EXPECT_EQ(format(tabbed), tabbed);
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

TEST(BlockIndentationTest, AnchorsInlineLambdaToItsCallLine)
{
    formatter::Style style;
    style.IndentBlockBodies = true;
    const std::string source =
        "void f()\n"
        "{\n"
        "    Visit(\n"
        "        MakeCallback([&] (auto value) {\n"
        "            if (value) {\n"
        "                Use(value);\n"
        "            }\n"
        "        }),\n"
        "        other);\n"
        "}\n";
    ExpectUnchanged(source, style);

    const std::string incorrect =
        "void f()\n"
        "{\n"
        "    Visit(\n"
        "        MakeCallback([&] (auto value) {\n"
        "        if (value) {\n"
        "            Use(value);\n"
        "        }\n"
        "    }),\n"
        "        other);\n"
        "}\n";
    EXPECT_EQ(FormatWithStyle(incorrect, style), source);
}

TEST(BlockIndentationTest, ReflowsCallsAfterReindentingTheirStatements)
{
    formatter::Style style;
    style.IndentWidth = 4;
    style.IndentationKind = formatter::IndentationKind::Spaces;
    style.IndentBlockBodies = true;
    style.ColumnLimit = 72;
    style.ExpressionLayoutCondition =
        formatter::ExpressionLayoutCondition::Overflow;
    style.CallArgumentSplit = formatter::CallArgumentSplit::OnePerLine;
    style.FirstCallArgumentPosition =
        formatter::FirstCallArgumentPosition::NewLine;
    const std::string input =
        "void f() {\n"
        "\tLog(veryLongFirstArgument, veryLongSecondArgument, "
        "veryLongThirdArgument);\n"
        "}\n";
    const std::string expected =
        "void f() {\n"
        "    Log(\n"
        "        veryLongFirstArgument,\n"
        "        veryLongSecondArgument,\n"
        "        veryLongThirdArgument);\n"
        "}\n";
    EXPECT_EQ(FormatWithStyle(input, style), expected);
    EXPECT_EQ(FormatWithStyle(expected, style), expected);
}

TEST(ControlBraceTest, WrapsOnlyCompleteUnbracedBodies)
{
    formatter::Style style;
    style.AddMissingControlBraces = true;
    style.IndentBlockBodies = true;
    style.ControlBlockBracePlacement =
        formatter::ControlBlockBracePlacement::AttachUnlessMultiline;
    const std::string input =
        "void f(bool a, bool b) {\n"
        "    if (a) if (b) Go(); else Stop();\n"
        "    for (;;) Go();\n"
        "    while (a) Go();\n"
        "    while (a);\n"
        "    do Go(); while (a);\n"
        "}\n";
    const std::string expected =
        "void f(bool a, bool b) {\n"
        "    if (a) {\n"
        "        if (b) {\n"
        "            Go();\n"
        "        } else {\n"
        "            Stop();\n"
        "        }\n"
        "    }\n"
        "    for (;;) {\n"
        "        Go();\n"
        "    }\n"
        "    while (a) {\n"
        "        Go();\n"
        "    }\n"
        "    while (a);\n"
        "    do {\n"
        "        Go();\n"
        "    } while (a);\n"
        "}\n";
    EXPECT_EQ(FormatWithStyle(input, style), expected);
    EXPECT_EQ(FormatWithStyle(expected, style), expected);
    style.EmptyControlBody = formatter::EmptyControlBody::EmptyBlock;
    style.EmptyBlockInteriorSpacing = formatter::TokenSpacing::Space;
    const std::string emptyInput =
        "void spin(bool a) {\n"
        "    while (a);\n"
        "    while (\n        a);\n"
        "    for (; a; );\n"
        "    do; while (a);\n"
        "    if (a); else Stop();\n"
        "    if (a) /* keep */ ;\n"
        "}\n";
    const std::string emptyExpected =
        "void spin(bool a) {\n"
        "    while (a) { }\n"
        "    while (\n        a)\n    { }\n"
        "    for (; a; ) { }\n"
        "    do { } while (a);\n"
        "    if (a) { } else {\n"
        "        Stop();\n"
        "    }\n"
        "    if (a) /* keep */ ;\n"
        "}\n";
    EXPECT_EQ(FormatWithStyle(emptyInput, style), emptyExpected);
    EXPECT_EQ(FormatWithStyle(emptyExpected, style), emptyExpected);
    style.AddMissingControlBraces = false;
    EXPECT_EQ(FormatWithStyle(
        "void f() {\n    if (a);\n    if (b) Go();\n}\n", style),
        "void f() {\n    if (a) { }\n    if (b) Go();\n}\n");
    style.EmptyControlBody = formatter::EmptyControlBody::Preserve;
    style.AddMissingControlBraces = true;
    ExpectUnchanged("void f(bool a) { if (a) /* keep */ Go(); }\n", style);
    EXPECT_EQ(FormatWithStyle(
        "void f(bool a) { if (a)\r\n    Go(); }\r\n", style),
        "void f(bool a) { if (a)\r\n        Go(); }\r\n");
    ExpectUnchanged("void f(int x) { switch (x) case 0: Go(); }\n", style);
    EXPECT_EQ(FormatWithStyle(
        "void f(int x)\n{\n    switch (x) Go();\n}\n", style),
        "void f(int x)\n{\n    switch (x) {\n"
        "        Go();\n"
        "    }\n}\n");
    EXPECT_EQ(FormatWithStyle(
        "void f(bool a)\n{\n    if (a) Go(); Other();\n}\n", style),
        "void f(bool a)\n{\n"
        "    if (a) {\n"
        "        Go();\n"
        "    }\n"
        "    Other();\n"
        "}\n");
}

TEST(EmptyFunctionBodyTest, CollapsesWhitespaceOnlyBody)
{
    formatter::Style style;
    style.CollapseEmptyFunctionBodies = true;
    style.FunctionBracePlacement = formatter::BracePlacement::NewLine;
    const std::string input =
        "void Empty()\n{\n}\n"
        "void WithComment()\n{\n    // Keep this.\n}\n";
    const std::string expected =
        "void Empty()\n{ }\n"
        "void WithComment()\n{\n    // Keep this.\n}\n";
    EXPECT_EQ(FormatWithStyle(input, style), expected);
    EXPECT_EQ(FormatWithStyle(expected, style), expected);
    style.NonEmptyBlockBodyLayout =
        formatter::NonEmptyBlockBodyLayout::Multiline;
    EXPECT_EQ(FormatWithStyle(
        "void Annotated() { /* keep */ Act(); }\n", style),
        "void Annotated()\n{ /* keep */ Act(); }\n");
}

TEST(CallLayoutTest, KeepsFirstStringOnSelectedCallLine)
{
    formatter::Style style;
    style.ColumnLimit = 65;
    style.CallArgumentSplit = formatter::CallArgumentSplit::OnePerLine;
    style.ExpressionLayoutCondition =
        formatter::ExpressionLayoutCondition::Overflow;
    style.FirstCallArgumentPosition =
        formatter::FirstCallArgumentPosition::NewLine;
    style.EnforceFirstCallArgumentPosition = true;
    style.CallLineFirstStringNames = {"YT_LOG_INFO", "YT_TLOG_ERROR"};
    const std::string input =
        "void f() {\n"
        "    YT_LOG_INFO(\"A long enough message to require wrapping\", "
        "firstArgument, secondArgument);\n"
        "    YT_TLOG_ERROR(error, \"Failure (%v)\", firstArgument);\n"
        "    Other(\"A long enough message to require wrapping\", "
        "firstArgument, secondArgument);\n"
        "    YT_LOG_INFO_CUSTOM(\"A long enough message to require wrapping\", "
        "firstArgument, secondArgument);\n"
        "    YT_LOG_INFO_IF(enabled, \"A message requiring wrapping\", "
        "firstArgument, secondArgument);\n"
        "}\n";
    const std::string expected =
        "void f() {\n"
        "    YT_LOG_INFO(\"A long enough message to require wrapping\",\n"
        "        firstArgument,\n"
        "        secondArgument);\n"
        "    YT_TLOG_ERROR(error, \"Failure (%v)\", firstArgument);\n"
        "    Other(\n"
        "        \"A long enough message to require wrapping\",\n"
        "        firstArgument,\n"
        "        secondArgument);\n"
        "    YT_LOG_INFO_CUSTOM(\n"
        "        \"A long enough message to require wrapping\",\n"
        "        firstArgument,\n"
        "        secondArgument);\n"
        "    YT_LOG_INFO_IF(\n"
        "        enabled,\n"
        "        \"A message requiring wrapping\",\n"
        "        firstArgument,\n"
        "        secondArgument);\n"
        "}\n";
    EXPECT_EQ(FormatWithStyle(input, style), expected);
    EXPECT_EQ(FormatWithStyle(expected, style), expected);
    const std::string split =
        "void f() { YT_LOG_INFO(\n"
        "    \"Message\",\n"
        "    count); }\n";
    const std::string anchored =
        "void f() { YT_LOG_INFO(\"Message\",\n"
        "    count); }\n";
    EXPECT_EQ(FormatWithStyle(split, style), anchored);
    EXPECT_EQ(FormatWithStyle(
        "void f() { YT_LOG_INFO(\n    \"Message\"); }\n", style),
        "void f() { YT_LOG_INFO(\"Message\"); }\n");
}

TEST(OperatorSpacingTest, PreservesUnknownCastOrBinaryOperator)
{
    formatter::Style style;
    style.BinaryOperatorSpacing = formatter::TokenSpacing::Space;
    style.CastSpacing = formatter::TokenSpacing::NoSpace;
    style.UnaryOperatorSpacing = formatter::TightOperatorSpacing::NoSpace;
    style.ExpressionLayoutCondition =
        formatter::ExpressionLayoutCondition::Always;
    style.ColumnLimit = 120;
    const std::string source =
        "void f(int a, int b) {\n"
        "    auto address = (UnknownType) &a;\n"
        "    auto pointer = (UnknownType)*b;\n"
        "    auto bitwise = (a)& b;\n"
        "    auto multiply = (a)* b;\n"
        "    auto qualified = (NS::UnknownType) &a;\n"
        "    auto certain = (a + b) & b;\n"
        "    auto knownCast = (int)&a;\n"
        "}\n";
    ExpectUnchanged(source, style);
    const auto compact = std::string(source).replace(
        source.find("(a + b) & b"), std::string("(a + b) & b").size(),
        "(a + b)&b");
    EXPECT_EQ(FormatWithStyle(compact, style), source);
    const auto separatedCast = std::string(source).replace(
        source.find("(int)&a"), std::string("(int)&a").size(),
        "(int) & a");
    EXPECT_EQ(FormatWithStyle(separatedCast, style), source);
}

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
    const auto config = formatter::LoadConfigFile(dir / (name + ".hit-fmt"));
    std::string storage = input;
    storage.append(util::SourcePaddingSize, '\0');
    const auto tree = parser::Parse({.Data = storage.data(),
        .Size = input.size()}, [] (parser::ParseError) { });
    std::string actual;
    for (const auto& diagnostic : formatter::CheckNames(tree, config)) {
        actual += std::to_string(diagnostic.Position.Line) + ":" +
            std::to_string(diagnostic.Position.Column) + ": " +
            diagnostic.Message + "\n";
    }
    test::golden::ExpectGolden(dir / (name + ".expected"), actual);
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
         "ExpressionLayoutCondition = FitOrOverflow\n"
         "ExpressionLayoutAlgorithm = DynamicProgramming\n"
         "BinaryOperatorPlacement = Trailing\n"
         "AssignmentOperatorPlacement = Trailing\n"
         "StatementAttributeIndent = Block\n"
         "AccessModifierIndentWidth = 1\n"
         "MemberIndentWidth = 3\n"
         "BlockBodyIndentWidth = 4\n"
         "CaseLabelIndentWidth = 1\n"
         "StatementAttributeIndentWidth = 2\n"
         "NamespaceCommentSpaces = 2\n"
         "CommaSpacing = Space\n"
         "TernaryOperatorPlacement = Trailing\n"
         "TernarySplit = ThreeLines\n"
         "CallArgumentSplit = Pack\nFirstCallArgumentPosition = NewLine\n"
         "EnforceFirstCallArgumentPosition = true\n"
         "CallArgumentIndent = Continuation\n"
         "CallLineFirstStringNames = [\"YT_LOG_INFO\", \"YT_LOG_ERROR\"]\n"
         "Exclude = [ # list comment\n"
         "  \"x]y\", # bracket in a quoted item\n"
         "  \"(^|/)generated(/|$)\"\n"
         "]\n"
         "ClassNamePrefixes = [\n"
         "  \"S\", # prefix comment\n"
         "  \"C\"\n"
         "]\n"
         "IndentWidth = 3\n");
    const auto config = formatter::LoadConfigFile(path);
    EXPECT_EQ(config.FormattingStyle.IndentWidth, 3u);
    EXPECT_EQ(config.FormattingStyle.ColumnLimit, 40u);
    EXPECT_EQ(config.FormattingStyle.ExpressionLayoutCondition,
        formatter::ExpressionLayoutCondition::FitOrOverflow);
    EXPECT_EQ(config.FormattingStyle.ExpressionLayoutAlgorithm,
        formatter::ExpressionLayoutAlgorithm::DynamicProgramming);
    EXPECT_EQ(config.FormattingStyle.BinaryOperatorPlacement,
        formatter::BinaryOperatorPlacement::Trailing);
    EXPECT_EQ(config.FormattingStyle.AssignmentOperatorPlacement,
        formatter::BinaryOperatorPlacement::Trailing);
    EXPECT_EQ(config.FormattingStyle.StatementAttributeIndent,
        formatter::StatementAttributeIndent::Block);
    EXPECT_EQ(config.FormattingStyle.AccessModifierIndentWidth, 1u);
    EXPECT_EQ(config.FormattingStyle.MemberIndentWidth, 3u);
    EXPECT_EQ(config.FormattingStyle.BlockBodyIndentWidth, 4u);
    EXPECT_EQ(config.FormattingStyle.CaseLabelIndentWidth, 1u);
    EXPECT_EQ(config.FormattingStyle.StatementAttributeIndentWidth, 2u);
    EXPECT_EQ(config.FormattingStyle.NamespaceCommentSpaces, 2u);
    EXPECT_EQ(config.FormattingStyle.CommaSpacing, formatter::CommaSpacing::Space);
    EXPECT_EQ(config.FormattingStyle.TernaryOperatorPlacement,
        formatter::TernaryOperatorPlacement::Trailing);
    EXPECT_EQ(config.FormattingStyle.TernarySplit,
        formatter::TernarySplit::ThreeLines);
    EXPECT_EQ(config.FormattingStyle.CallArgumentSplit,
        formatter::CallArgumentSplit::Pack);
    EXPECT_EQ(config.FormattingStyle.FirstCallArgumentPosition,
        formatter::FirstCallArgumentPosition::NewLine);
    EXPECT_TRUE(config.FormattingStyle.EnforceFirstCallArgumentPosition);
    EXPECT_EQ(config.FormattingStyle.CallArgumentIndent,
        formatter::CallArgumentIndent::Continuation);
    EXPECT_EQ(config.FormattingStyle.CallLineFirstStringNames,
        (std::vector<std::string>{"YT_LOG_INFO", "YT_LOG_ERROR"}));
    ASSERT_EQ(config.Exclude.size(), 2u);
    EXPECT_TRUE(std::regex_search("x]y", config.Exclude[0]));
    EXPECT_TRUE(std::regex_search("generated/source.cpp", config.Exclude[1]));
    ASSERT_TRUE(config.Naming.Class.Prefixes.has_value());
    EXPECT_EQ(*config.Naming.Class.Prefixes,
        (std::vector<std::string>{"S", "C"}));
    save("ClassNameCase = Unchecked\n");
    EXPECT_EQ(formatter::LoadConfigFile(path).Naming.Class.Case,
        formatter::NameCase::Unchecked);
    save("CallArgumentSplit = Pack\nExpressionLayoutCondition = Always\n");
    const auto alternate = formatter::LoadConfigFile(path).FormattingStyle;
    EXPECT_EQ(alternate.ExpressionLayoutCondition, formatter::ExpressionLayoutCondition::Always);
    save("ExpressionLayoutCondition = Overflow\n"
         "TernaryOperatorPlacement = Preserve\n"
         "TernarySplit = Preserve\n");
    const auto preserving = formatter::LoadConfigFile(path).FormattingStyle;
    EXPECT_EQ(preserving.ExpressionLayoutCondition,
        formatter::ExpressionLayoutCondition::Overflow);
    EXPECT_EQ(preserving.TernaryOperatorPlacement,
        formatter::TernaryOperatorPlacement::Preserve);
    EXPECT_EQ(preserving.TernarySplit, formatter::TernarySplit::Preserve);
    for (auto setting : {
        "Regroup = true\n",
        "InsertBraces = false\n",
        "UnknownMacroNames = []\n",
        "CallLineFirstStringNames = [\"YT_LOG_\" , \"NS::Call\"]\n",
        "CallLineFirstStringNames = [\"\"]\n",
        "ColumnLimit = 0\n",
        "ColumnLimit = 10001\n",
        "ShortNamespaceLines = 10001\n",
        "NamespaceComments = Required\n",
        "NamespaceCommentSpaces = 33\n",
        "AccessModifierIndentWidth = 33\n",
        "MemberIndentWidth = 33\n",
        "BlockBodyIndentWidth = 33\n",
        "CaseLabelIndentWidth = 33\n",
        "StatementAttributeIndentWidth = 33\n",
        "IndentWidth = 0\n",
        "IndentWidth = 33\n",
        "IndentAccessModifiers = Unknown\n",
        "IndentMembers = Unknown\n",
        "IndentCaseLabels = Unknown\n",
        "StatementAttributeIndent = Unknown\n",
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
        "ConstructorInitializerListIndent = 33\n",
        "ConstructorInitializerListIndent = 0\n",
        "ConstructorInitializerListStart = Attach\n",
        "ConstructorInitializerListColonPlacement = Left\n",
        "CastArgumentSpacing = Middle\n",
        "GroupingInteriorSpacing = Middle\n",
        "BlockInteriorSpacing = Middle\n",
        "EmptyBlockInteriorSpacing = Middle\n",
        "ControlBlockSpaceBeforeBrace = Middle\n",
        "DeclarationSemicolonSpacing = Middle\n",
        "ControlBlockConditionSpacing = Middle\n",
        "AccessModifierColonSpacing = Middle\n",
        "RangeBasedForColonSpacing = Middle\n",
        "BitFieldColonSpacing = Space\n",
        "EnumUnderlyingTypeColonSpacing = Space\n",
        "LabelColonSpacing = Space\n",
        "InheritanceListStart = Attach\n",
        "InheritanceListSplit = Preserve\n",
        "InheritanceListMerge = true\n",
        "InheritanceListPunctuation = WithAncestor\n",
        "FunctionParameterSplit = Together\n",
        "FirstFunctionParameterPosition = NameLineUnlessLong\n",
        "FunctionParameterNewLineIndent = 33\n",
        "InitializerListSplit = Together\n",
        "InitializerClosingBrace = Attach\n",
        "ControlConditionIndent = NameLine\n",
        "ClassBracePlacement = attach\n",
        "ExpressionWrap = Always\n",
        "ExpressionLayoutCondition = Unknown\n",
        "ExpressionLayoutAlgorithm = BruteForce\n",
        "BinaryOperatorPlacement = Middle\n",
        "AssignmentOperatorPlacement = Middle\n",
        "TernaryOperatorPlacement = Middle\n",
        "TernarySplit = TwoLines\n",
        "CallArgumentSplit = Together\n",
        "FirstCallArgumentPosition = BeforeCall\n",
        "EnforceFirstCallArgumentPosition = maybe\n",
        "CallArgumentIndent = Tabs\n",
        "MergeCallArguments = Underflow\n",
        "ExpressionLayoutCondition = OnOverflow\n",
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
        "CommaSpacing = Unknown\n",
        "LoopSemicolonSpacing = After\n",
        "ConditionInitSemicolonSpacing = After\n",
        "ControlBlockSemicolonSpacing = Space\n",
        "TrailingWhitespace = Space\n",
        "IndentationKind = Mixed\n",
        "PreprocessorIndentation = Indent\n",
        "TrailingCommentSpacing = After\n",
        "TemplateKeywordSpacing = After\n",
        "TemplateTypeArgumentInteriorSpacing = After\n",
        "TemplateAngleBracketsSpacing = Space\n",
        "TemplateTypeArgumentSpacing = After\n",
        "EmptyBraceInitializerInteriorSpacing = After\n",
        "OperatorOverloadOperatorSpacing = Space\n",
        "AttributeInteriorSpacing = After\n",
        "AttributeSpaceAfterSpecifier = After\n",
        "NamedCastSpaceBeforeTypeArguments = After\n",
        "PlacementNewSpaceBeforeArguments = After\n",
        "AttributeSpaceBeforeSpecifier = After\n",
        "NewInitializerSpaceBeforeParentheses = After\n",
        "DeleteArraySpaceBeforeBrackets = After\n",
        "SizeofPackSpaceBeforeParentheses = After\n",
        "NewInitializerSpaceBeforeBraces = After\n",
        "NewInitializerParenthesisInteriorSpacing = After\n",
        "DeleteArraySpaceAfterBrackets = After\n",
        "PlacementNewSpaceAfterArguments = After\n",
        "NewArraySpaceBeforeBrackets = After\n",
        "NewArrayInteriorSpacing = After\n",
        "EmptyAttributeInteriorSpacing = After\n",
        "CoAwaitSpaceBeforeParentheses = After\n",
        "NewInitializerBraceInteriorSpacing = After\n",
        "PlacementNewArgumentInteriorSpacing = After\n",
        "EmptyArrayDeclarationInteriorSpacing = After\n",
        "EmptyNewArrayInteriorSpacing = After\n",
        "NoexceptSpaceBeforeSpecifier = After\n",
        "NamedCastSpaceBeforeOperandParentheses = After\n",
        "ReturnSpaceBeforeParentheses = After\n",
        "ThrowSpaceBeforeParentheses = After\n",
        "CoReturnSpaceBeforeParentheses = After\n",
        "FunctionSuffixSpecifierSpacing = After\n",
        "RecordBodyInteriorSpacing = After\n",
        "NamespaceBodyInteriorSpacing = After\n",
        "EnumBodyInteriorSpacing = After\n",
        "RequirementBodyInteriorSpacing = After\n",
        "FunctionQualifierSpacing = After\n",
        "FunctionRefQualifierSpacing = After\n",
        "NestedDeclaratorInteriorSpacing = After\n",
        "NestedDeclaratorPointerSpacing = After\n",
        "MemberPointerDeclaratorSpacing = After\n",
        "NoexceptSpaceBeforeParentheses = After\n",
        "AlignasSpaceBeforeParentheses = After\n",
        "AlignofSpaceBeforeParentheses = After\n",
        "SizeofSpaceBeforeParentheses = After\n",
        "DecltypeSpaceBeforeParentheses = After\n",
        "TypeidSpaceBeforeParentheses = After\n",
        "RequiresSpaceBeforeParentheses = After\n",
        "StaticAssertSpaceBeforeParentheses = After\n",
        "StatementSemicolonSpacing = Space\n",
        "MemberAccessOperatorsSpacing = Space\n",
        "ScopeResolutionSpacing = Space\n",
        "InitializerBracesSpacing = Middle\n",
        "UnaryOperatorSpacing = Space\n",
        "PostfixOperatorSpacing = Space\n",
        "TernaryOperatorSpacing = NoSpace\n",
        "ArrayDeclarationInteriorSpacing = Middle\n",
        "SubscriptOperatorInteriorSpacing = Middle\n",
        "SubscriptBracketSpacing = Space\n",
        "ArrayDeclarationBracketSpacing = Space\n",
        "ReturnArrowSpacing = Space\n",
        "EmptyFunctionCallArgumentsSpacing = Middle\n",
        "EmptyFunctionParametersSpacing = Middle\n",
        "EmptyLambdaParametersSpacing = Middle\n",
        "EmptyLambdaCaptureSpacing = Middle\n",
        "SubscriptOperatorSpacing = Middle\n",
        "ArrayDeclarationSpacing = Middle\n",
        "BraceInitializerSpacing = Middle\n",
        "StructuredBindingBracketSpacing = Middle\n",
        "SemicolonSpacing = Space\n",
        "ControlBlockBracePlacement = Unknown\n",
        "Exclude = [\"[\"]\n",
        "Exclude = [\n  \"foo\",\n",
        "Exclude = [\n  \"foo\"\n  \"bar\"\n]\n",
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
    save("Exclude = [\n  \"foo\"\n]\nUnknown = true\n");
    try {
        (void)formatter::LoadConfigFile(path);
        FAIL() << "expected unknown setting";
    } catch (const std::runtime_error& error) {
        EXPECT_NE(std::string_view(error.what()).find(":4: unknown setting"),
            std::string_view::npos);
    }
    save("ClassNameCase = CamelCase\n");
    try {
        (void)formatter::LoadConfigFile(path);
        FAIL() << "expected invalid enum spelling";
    } catch (const std::runtime_error& error) {
        EXPECT_NE(std::string_view(error.what()).find(
            "expected Unchecked, camelCase, PascalCase, snake_case or "
            "SCREAMING_SNAKE_CASE"), std::string_view::npos);
    }
    std::filesystem::remove(path);
}

} // namespace
