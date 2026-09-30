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

struct PreserveCase
{
    std::string Name;
};

void PrintTo(const PreserveCase& value, std::ostream* output)
{
    *output << "style/" << value.Name;
}

std::vector<PreserveCase> DiscoverCases()
{
    std::vector<PreserveCase> cases;
    for (const auto& entry :
        std::filesystem::directory_iterator(FORMATTER_TESTDATA_DIR))
    {
        auto filename = entry.path().filename().string();
        if (filename.ends_with(".input.cpp") ||
            filename.ends_with(".output.cpp"))
        {
            cases.push_back({filename});
        }
    }
    std::ranges::sort(cases, {}, &PreserveCase::Name);
    return cases;
}

class PreserveTest
    : public testing::TestWithParam<PreserveCase>
{ };

TEST_P(PreserveTest, KeepsUnchanged)
{
    const auto input = ReadFile(
        std::filesystem::path(FORMATTER_TESTDATA_DIR) / GetParam().Name);
    for (auto [indent, column] : {
        std::pair<size_t, size_t>{4, 10000},
        {1, 10000},
        {32, 10000},
    })
    {
        formatter::Style style;
        style.IndentWidth = indent;
        style.ColumnLimit = column;
        ExpectUnchanged(input, style);
    }
}

INSTANTIATE_TEST_SUITE_P(Formatter, PreserveTest,
    testing::ValuesIn(DiscoverCases()),
    [] (const auto& info) {
        auto name = info.param.Name;
        std::ranges::replace(name, '.', '_');
        std::ranges::replace(name, '-', '_');
        return name;
    });

std::string FormatWithStyle(
    std::string_view source,
    const formatter::Style& style)
{
    std::string storage(source);
    storage.append(util::SourcePaddingSize, '\0');
    util::PaddedSourceView padded{.Data = storage.data(),
        .Size = source.size()};
    auto tree = parser::Parse(padded, [] (parser::ParseError) { });
    EXPECT_EQ(formatter::Format(tree, style), formatter::Format(padded, style));
    return formatter::Format(tree, style);
}

void ExpectFormattedGolden(
    const std::filesystem::path& path,
    std::string_view input,
    const formatter::Style& style)
{
    const auto actual = FormatWithStyle(input, style);
    test::golden::ExpectGolden(path, actual);
    EXPECT_EQ(FormatWithStyle(actual, style), actual);
}

struct FormatCase
{
    std::filesystem::path Directory;
    std::string Name;
    std::string Input;
    std::string Config;
    std::vector<formatter::Hunk> Hunks;
    bool Spillover;

    FormatCase(std::filesystem::path directory, std::string name,
        std::string input, std::string config = {},
        std::vector<formatter::Hunk> hunks = {}, bool spillover = true)
        : Directory(std::move(directory))
        , Name(std::move(name))
        , Input(std::move(input))
        , Config(std::move(config))
        , Hunks(std::move(hunks))
        , Spillover(spillover)
    { }
};

void PrintTo(const FormatCase& value, std::ostream* output)
{
    const auto root = (std::filesystem::path(FORMATTER_TESTDATA_DIR) /
        "..").lexically_normal();
    *output << value.Directory.lexically_normal()
        .lexically_relative(root).generic_string() << '/' << value.Name;
}

void AddCases(
    std::vector<FormatCase>& cases,
    const std::filesystem::path& directory,
    std::initializer_list<const char*> names,
    std::string input = "input.cpp")
{
    for (const auto* name : names) {
        cases.push_back({directory, name, input, {}});
    }
}

std::vector<FormatCase> FormatCases()
{
    std::vector<FormatCase> cases;
    AddCases(
        cases,
        BRACE_TESTDATA_DIR,
        {"preserve", "attach", "newline", "mixed"});
    AddCases(
        cases,
        std::filesystem::path(FORMATTER_TESTDATA_DIR) / "../include-order",
        {"sort", "composed"});
    cases.push_back({std::filesystem::path(FORMATTER_TESTDATA_DIR) /
        "../include-order", "crlf", "crlf.input.cpp", "sort"});
    AddCases(
        cases,
        BRACE_SPACING_TESTDATA_DIR,
        {"preserve", "single-space", "mixed"});
    AddCases(
        cases,
        LAMBDA_SPACING_TESTDATA_DIR,
        {"preserve", "space", "no-space"});
    AddCases(
        cases,
        CONTROL_SPACING_TESTDATA_DIR,
        {"preserve", "space", "no-space"});
    AddCases(
        cases,
        CONTROL_BRACE_TESTDATA_DIR,
        {"preserve", "same-line", "new-line", "attach-unless-multiline"});
    cases.push_back({CONTROL_BRACE_TESTDATA_DIR,
        "reflow-preferred",
        "reflow-preferred.input.cpp"});
    AddCases(
        cases,
        CONTROL_BRACE_TESTDATA_DIR,
        {"combined"},
        "combined.input.cpp");
    AddCases(
        cases,
        FUNCTION_SPACING_TESTDATA_DIR,
        {"preserve", "space", "no-space"});
    AddCases(
        cases,
        FUNCTION_SPACING_TESTDATA_DIR,
        {"reflow"},
        "reflow.input.cpp");
    AddCases(
        cases,
        FUNCTION_CALL_SPACING_TESTDATA_DIR,
        {"preserve", "space", "no-space"});
    AddCases(
        cases,
        FUNCTION_CALL_SPACING_TESTDATA_DIR,
        {"combined"},
        "combined.input.cpp");
    AddCases(
        cases,
        FUNCTION_INNER_SPACING_TESTDATA_DIR,
        {"preserve", "space", "no-space"});
    AddCases(
        cases,
        FUNCTION_INNER_SPACING_TESTDATA_DIR,
        {"reflow"},
        "reflow.input.cpp");
    AddCases(
        cases,
        CALL_INNER_SPACING_TESTDATA_DIR,
        {"preserve", "space", "no-space"});
    cases.push_back({CALL_INNER_SPACING_TESTDATA_DIR,
        "reflow-preferred",
        "reflow-preferred.input.cpp"});
    cases.push_back({CALL_INNER_SPACING_TESTDATA_DIR,
        "comma-preferred",
        "comma-preferred.input.cpp"});
    AddCases(
        cases,
        CALL_INNER_SPACING_TESTDATA_DIR,
        {"combined"},
        "combined.input.cpp");
    AddCases(
        cases,
        LAMBDA_CAPTURE_TESTDATA_DIR,
        {"preserve", "space", "no-space", "mixed", "reverse"});
    AddCases(
        cases,
        LAMBDA_INNER_PARAMETER_TESTDATA_DIR,
        {"preserve", "space", "no-space", "combined"});
    AddCases(
        cases,
        BODY_INDENT_TESTDATA_DIR,
        {"preserve", "indent", "no-indent", "mixed"});
    cases.push_back({BODY_INDENT_TESTDATA_DIR,
        "reindented-call", "reindented-call.input.cpp"});
    const auto nullStatements = std::filesystem::path(FORMATTER_TESTDATA_DIR) /
        "../null-statements";
    AddCases(cases, nullStatements, {"preserve", "remove"});
    for (const auto& [name, config] : {
        std::pair{"decl-preserve", "preserve"},
        std::pair{"decl-statements", "remove"},
        std::pair{"decl-only", "declarations"},
        std::pair{"decl-both", "both"}})
    {
        cases.push_back({nullStatements,
            name,
            "declarations.input.cpp",
            config});
    }
    AddCases(
        cases,
        BODY_INDENT_TESTDATA_DIR,
        {"grouped-preserve", "grouped-strict"},
        "grouped-statements.input.cpp");
    AddCases(
        cases,
        BODY_INDENT_TESTDATA_DIR,
        {"lambda-comments-follow", "lambda-comments-preserve"},
        "lambda-leading-comments.input.cpp");
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
    cases.push_back({closings,
        "reflow-preferred",
        "reflow-preferred.input.cpp"});
    cases.push_back({closings,
        "generated-block",
        "generated-block.input.cpp"});
    cases.push_back({closings,
        "layout-preferred",
        "layout-preferred.input.cpp"});
    cases.push_back({closings, "new-line", "new-line.input.cpp"});
    cases.push_back({closings,
        "conditional-preferred",
        "trailing-comma.input.cpp"});
    cases.push_back({closings, "remove", "trailing-comma.input.cpp"});
    cases.push_back({closings, "trailing-add", "trailing-layout.input.cpp"});
    cases.push_back({closings,
        "trailing-conditional",
        "trailing-layout.input.cpp"});
    cases.push_back({closings,
        "trailing-preferred",
        "trailing-preferred.input.cpp"});
    cases.push_back({closings,
        "trailing-removed",
        "trailing-removed.input.cpp"});
    cases.push_back({closings, "trailing-only", "trailing-layout.input.cpp"});
    cases.push_back({closings,
        "trailing-new-line",
        "trailing-layout.input.cpp"});
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
    addProfileCase("../spacing-options", "profile-case-blocks");
    cases.push_back({std::filesystem::path(FORMATTER_TESTDATA_DIR) /
        "../spacing-options", "initializer-overflow-one-per-line",
        "initializer-overflow-one-per-line.input.cpp"});
    addProfileCase("../ternary-layout", "profile");
    cases.push_back({std::filesystem::path(FORMATTER_TESTDATA_DIR) /
        "../ternary-layout", "reflow-preferred",
        "reflow-preferred.input.cpp"});
    addProfileCase("../assignment-location", "profile");
    addProfileCase("../function_parameters", "profile");
    addProfileCase("../body-indent", "profile-control-local");
    addProfileCase("../body-indent", "profile-direct-initializer");
    cases.push_back({std::filesystem::path(FORMATTER_TESTDATA_DIR) /
        "../null-statements", "profile", "input.cpp", profile});
    cases.push_back({BODY_INDENT_TESTDATA_DIR,
        "grouped-profile",
        "grouped-statements.input.cpp",
        profile});
    cases.push_back({BODY_INDENT_TESTDATA_DIR,
        "lambda-comments-profile",
        "lambda-leading-comments.input.cpp",
        profile});
    cases.push_back({BODY_INDENT_TESTDATA_DIR,
        "lambda-trailing-return",
        "lambda-trailing-return.input.cpp"});
    addProfileCase("../lambda-captures", "profile");
    addProfileCase("../call-reflow", "profile-tabbed-log");
    const auto callReflow = std::filesystem::path(FORMATTER_TESTDATA_DIR) /
        "../call-reflow";
    AddCases(
        cases,
        callReflow,
        {"sole-call-line", "sole-new-line"},
        "sole-position.input.cpp");
    cases.push_back({callReflow,
        "sole-preferred",
        "sole-preferred.input.cpp"});
    cases.push_back({callReflow,
        "first-position-reflow",
        "first-position-reflow.input.cpp"});
    cases.push_back({callReflow,
        "first-string-inline",
        "first-string-inline.input.cpp"});
    cases.push_back({std::filesystem::path(FORMATTER_TESTDATA_DIR) /
        "../function_parameters", "first-position-reflow",
        "first-position-reflow.input.cpp"});
    AddCases(cases, std::filesystem::path(FORMATTER_TESTDATA_DIR) /
        "../call-like-macro", {"selected"});
    AddCases(cases, std::filesystem::path(FORMATTER_TESTDATA_DIR) /
        "../macro-nested-calls", {"configured"});
    AddCases(cases, std::filesystem::path(FORMATTER_TESTDATA_DIR) /
        "../lambda-body", {"preserve", "inherit", "inline"});
    AddCases(cases, std::filesystem::path(FORMATTER_TESTDATA_DIR) /
        "../control-header-clauses", {"preserve",
            "split",
            "indent-width"});
    const auto controlPlacement = std::filesystem::path(
        FORMATTER_TESTDATA_DIR) / "../control-header-placement";
    AddCases(cases, controlPlacement,
        {"preserve", "first-only", "closing-only", "both"});
    cases.push_back({controlPlacement,
        "reflow", "reflow.input.cpp"});
    cases.push_back({controlPlacement,
        "reverse", "reverse.input.cpp"});
    cases.push_back({controlPlacement,
        "crlf", "crlf.input.cpp"});
    cases.push_back({std::filesystem::path(FORMATTER_TESTDATA_DIR) /
        "../lambda-body", "profile", "input.cpp", profile});
    addProfileCase("../control-header-clauses", "profile");
    addProfileCase("../whitespace-sensitive-macros", "profile");
    addProfileCase("../call-reflow", "balanced-expressions");
    addProfileCase("../call-reflow", "nested-operator-choice");
    cases.push_back({callReflow,
        "balanced-initializers",
        "balanced-initializers.input.cpp"});
    addProfileCase("../call-reflow", "profile-nested");
    addProfileCase("../chained-calls", "profile-multiline-lambda");
    const auto google = std::filesystem::path(FORMATTER_TESTDATA_DIR) /
        "../google-style-regressions";
    cases.push_back({google, "inline-record", "inline-record.input.cpp"});
    cases.push_back({google, "widths", "widths.input.cpp"});
    cases.push_back({google, "operator-spacing", "operator-spacing.input.cpp"});
    cases.push_back({google, "unbraced-switch", "unbraced-switch.input.cpp"});
    cases.push_back({google,
        "namespace-comments",
        "namespace-comments.input.cpp"});
    cases.push_back({google,
        "syntax-boundaries",
        "syntax-boundaries.input.cpp"});
    cases.push_back({google,
        "declarator-and-macro-safety",
        "declarator-and-macro-safety.input.cpp"});
    const auto disabled = std::filesystem::path(FORMATTER_TESTDATA_DIR) /
        "../format-disabled";
    cases.push_back({disabled, "regions", "regions.input.cpp"});
    cases.push_back({disabled, "unclosed", "unclosed.input.cpp", "regions"});
    cases.push_back({disabled, "crlf", "crlf.input.cpp", "regions"});
    const auto newArrays = std::filesystem::path(FORMATTER_TESTDATA_DIR) /
        "../new-array-declarators";
    AddCases(cases, newArrays, {"left", "array-space", "array-preserve"});
    AddCases(cases, google, {"punctuation-preserve",
        "punctuation-space",
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
        "../call-expression-indent", {"aligned",
            "aligned-no-call-reflow",
            "indent-width",
            "preserve"});
    AddCases(cases, INHERITANCE_TESTDATA_DIR,
        {"preserve",
            "format",
            "leading",
            "trailing",
            "overflow",
            "comma-leading",
            "no-space",
            "spaces-mixed",
            "combined"});
    AddCases(cases, INHERITANCE_TESTDATA_DIR,
        {"split-preserve",
            "one-per-line",
            "pack-wide",
            "pack-tight",
            "split-combined"}, "split.input.cpp");
    for (const auto* name : {"parsed-operators",
        "outer-conditional",
        "control-braces-after-wrap"})
    {
        cases.push_back({std::filesystem::path(FORMATTER_TESTDATA_DIR) /
            ".." / name, "configured", "input.cpp"});
    }
    cases.push_back({std::filesystem::path(FORMATTER_TESTDATA_DIR) /
        "../control-braces-after-wrap", "reflow", "reflow.input.cpp"});
    const auto strings = std::filesystem::path(FORMATTER_TESTDATA_DIR) /
        "../concat-strings";
    AddCases(cases, strings, {"overflow", "preserve"});
    cases.push_back({strings, "crlf", "crlf.input.cpp", "overflow"});
    const auto parsedOperators = std::filesystem::path(FORMATTER_TESTDATA_DIR) /
        "../parsed-operators";
    for (const auto* name : {"placement-lists", "delimiter-choice"}) {
        cases.push_back({parsedOperators,
            name,
            std::string(name) + ".input.cpp"});
    }
    // Keep safety and recovered-syntax examples in separate parse units.
    const auto spacing = std::filesystem::path(FORMATTER_TESTDATA_DIR) /
        ".." / "spacing-options";
    cases.push_back({spacing,
        "cases-preserved",
        "indented-cases.input.cpp",
        "cases-preserved"});
    for (const auto& entry : std::filesystem::directory_iterator(spacing)) {
        const auto filename = entry.path().filename().string();
        if (filename.ends_with(".input.cpp")) {
            const auto name = filename.substr(
                0,
                filename.size() - std::string_view(".input.cpp").size());
            const auto duplicate = std::ranges::any_of(cases,
                [&] (const FormatCase& value) {
                    return value.Directory.lexically_normal() ==
                        spacing.lexically_normal() && value.Name == name;
                });
            if (!duplicate) {
                cases.push_back({spacing, name, filename});
            }
        }
    }
    const auto data = std::filesystem::path(FORMATTER_TESTDATA_DIR) / "..";
    cases.push_back({data / "initializer-trailing-comma",
        "conditional-closer", "input.cpp"});
    AddCases(cases, data / "function_parameters",
        {"preserve", "pack_name", "pack_new", "one_name", "one_new",
            "pack_merge"});
    AddCases(cases, data / "namespace_comments",
        {"preserve", "remove", "add_missing", "fix", "fix_spaced",
            "fix_newline"});
    cases.push_back({data / "namespace_comments", "crlf_fix",
        "crlf.input.cpp"});
    cases.push_back({data / "namespace_comments", "composes-spacing",
        "composes-spacing.input.cpp"});

    const auto hunks = data / "changed-hunks";
    cases.push_back({hunks / "basic", "call", "input.cpp",
        "style", {{8, 8}}});
    cases.push_back({hunks / "basic", "includes", "input.cpp",
        "style", {{1, 1}}});
    cases.push_back({hunks / "basic", "includes-strict", "input.cpp",
        "style", {{1, 1}}, false});
    cases.push_back({hunks / "basic", "function", "input.cpp",
        "style", {{4, 4}}});
    cases.push_back({hunks / "spill", "default", "input.cpp",
        "style", {{3, 3}, {5, 5}}});
    cases.push_back({hunks / "spill", "strict", "input.cpp",
        "style", {{3, 3}, {5, 5}}, false});
    cases.push_back({hunks / "structural", "unsupported", "input.cpp",
        "style", {{2, 2}}});

    std::ranges::sort(cases, {}, [] (const auto& value) {
        return value.Directory.string() + value.Name;
    });
    return cases;
}

class FormatTest
    : public testing::TestWithParam<FormatCase>
{ };

TEST_P(FormatTest, Golden)
{
    const auto& value = GetParam();
    const test::golden::Fixture fixture{value.Directory, value.Name};
    const auto config = value.Config.empty() ? value.Name : value.Config;
    const auto style = formatter::LoadConfigFile(
        fixture.Directory / (config + ".hit-fmt")).FormattingStyle;
    const auto input = fixture.Input(value.Input);
    if (value.Hunks.empty()) {
        ExpectFormattedGolden(fixture.Named(".output.cpp"), input, style);
    } else {
        std::string storage(input);
        storage.append(util::SourcePaddingSize, '\0');
        const auto tree = parser::Parse(
            {.Data = storage.data(), .Size = input.size()},
            [] (parser::ParseError) { });
        fixture.Expect(formatter::FormatChanged(
            tree, style, value.Hunks, value.Spillover));
    }
}

INSTANTIATE_TEST_SUITE_P(Formatter, FormatTest,
    testing::ValuesIn(FormatCases()),
    [] (const auto& info) {
        const auto root = (std::filesystem::path(FORMATTER_TESTDATA_DIR) /
            "..").lexically_normal();
        auto name = (info.param.Directory.lexically_normal()
            .lexically_relative(root) / info.param.Name).string();
        std::ranges::replace(name, '/', '_');
        std::ranges::replace(name, '-', '_');
        return name;
    });

std::string ConfigName(const testing::TestParamInfo<std::string>& info)
{
    return info.param;
}

class ParameterNameTest
    : public testing::TestWithParam<std::string>
{ };

TEST_P(ParameterNameTest, Golden)
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
    for (const auto& diagnostic : formatter::CheckParameterNames(
        tree,
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

INSTANTIATE_TEST_SUITE_P(Formatter, ParameterNameTest,
    testing::Values("enabled", "default", "declarations_only",
        "definitions_only", "names_only", "ytsaurus"), ConfigName);

TEST(Formatter, CallArgument_ReflowsOnlySafeParsedCalls)
{
    formatter::Style style;
    style.ColumnLimit = 34;
    const auto directory = std::filesystem::path(FORMATTER_TESTDATA_DIR) /
        "../call-reflow";
    const auto input = ReadFile(directory / "input.cpp");
    EXPECT_EQ(FormatWithStyle(input, style), input);
    style.CallArgumentSplit = formatter::CallArgumentSplit::Pack;
    EXPECT_EQ(FormatWithStyle(input, style), input);
    style.EnforceCallArgumentSplit = true;
    style.ExpressionLayoutCondition =
        formatter::ExpressionLayoutCondition::Overflow;
    const auto packed = FormatWithStyle(input, style);
    test::golden::ExpectGolden(directory / "packed.output.cpp", packed);
    EXPECT_EQ(FormatWithStyle(packed, style), packed);
    style.ExpressionLayoutCondition =
        formatter::ExpressionLayoutCondition::FitOrOverflow;
    const auto merged = FormatWithStyle(packed, style);
    test::golden::ExpectGolden(directory / "merged.output.cpp", merged);
    EXPECT_EQ(FormatWithStyle(merged, style), merged);
    const auto longMultiline = ReadFile(directory / "long.input.cpp");
    EXPECT_EQ(FormatWithStyle(longMultiline, style), longMultiline);
    style.ExpressionLayoutCondition =
        formatter::ExpressionLayoutCondition::Always;
    const auto repacked = FormatWithStyle(longMultiline, style);
    test::golden::ExpectGolden(directory / "repacked.output.cpp", repacked);
    EXPECT_EQ(FormatWithStyle(repacked, style), repacked);

    style.CallArgumentSplit = formatter::CallArgumentSplit::OnePerLine;
    style.EnforceCallArgumentSplit = true;
    style.FirstCallArgumentPosition =
        formatter::FirstCallArgumentPosition::NewLine;
    const auto onePerLine = FormatWithStyle(input, style);
    test::golden::ExpectGolden(
        directory / "one-per-line.output.cpp",
        onePerLine);
    EXPECT_EQ(FormatWithStyle(onePerLine, style), onePerLine);

    style.CallArgumentSplit = formatter::CallArgumentSplit::Pack;
    style.EnforceCallArgumentSplit = true;
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
    EXPECT_EQ(
        FormatWithStyle(repositionedExpected, style),
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
    style.EnforceControlBlockBracePlacement = true;
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
    style.ExpressionLayoutCondition =
        formatter::ExpressionLayoutCondition::Overflow;
    ExpectUnchanged(
        "void f() { use(alpha +\n        beta, gamma, delta); }\n",
        style);
    ExpectUnchanged(
        "void f() { auto x = (Type)(alpha, beta, gamma, delta); }\n",
        style);
    ExpectUnchanged(
        "void f() { use(alpha, /* note */ beta, gamma); }\n",
        style);
    ExpectUnchanged("void f() { use(alpha, beta, gamma, delta); }\r\n", style);

    style.EnforceControlBlockBracePlacement = false;
    style.CallArgumentSplit = formatter::CallArgumentSplit::OnePerLine;
    style.EnforceCallArgumentSplit = true;
    style.ExpressionLayoutCondition =
        formatter::ExpressionLayoutCondition::Always;
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
    const auto packedBalanced =
        "void f() {\n"
        "    use(a, bb, ccc, dddd, eeeee,\n"
        "        ffffff);\n"
        "}\n";
    style.ColumnLimit = 32;
    style.ExpressionLayoutCondition =
        formatter::ExpressionLayoutCondition::Overflow;
    style.CallArgumentSplit = formatter::CallArgumentSplit::Pack;
    style.EnforceCallArgumentSplit = true;
    style.SpaceInsideCallArguments =
        formatter::SpaceInsideCallArguments::Preserve;
    EXPECT_EQ(FormatWithStyle(balancedInput, style), packedBalanced);
    EXPECT_EQ(FormatWithStyle(packedBalanced, style), packedBalanced);
    style.CallArgumentSplit = formatter::CallArgumentSplit::OnePerLine;
    style.EnforceCallArgumentSplit = true;
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
    style.EnforceCallArgumentSplit = false;
    ExpectUnchanged(balancedInput, style);

    formatter::Style sole;
    sole.CallArgumentSplit = formatter::CallArgumentSplit::OnePerLine;
    sole.EnforceCallArgumentSplit = true;
    sole.ExpressionLayoutCondition =
        formatter::ExpressionLayoutCondition::Overflow;
    sole.TernarySplit = formatter::TernarySplit::ThreeLines;
    sole.EnforceTernarySplit = true;
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
        formatter::MultilineSingleCallArgumentPosition::CallLine;
    ExpectUnchanged(multiline, sole);
    sole.MultilineSingleCallArgumentPosition =
        formatter::MultilineSingleCallArgumentPosition::NewLine;
    sole.EnforceMultilineSingleCallArgumentPosition = true;
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
    lambdaStyle.EnforceCallArgumentSplit = true;
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

TEST(Formatter, CallArgument_LaysOutEnclosingMacroAndNestedCalls)
{
    formatter::Style style;
    style.ExpressionLayoutCondition =
        formatter::ExpressionLayoutCondition::Always;
    style.CallArgumentSplit = formatter::CallArgumentSplit::Pack;
    style.EnforceCallArgumentSplit = true;
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
        const auto actual = FormatWithStyle(input, style);
        if (!firstOutput) {
            firstOutput = actual;
        } else {
            EXPECT_EQ(actual, *firstOutput);
        }
        EXPECT_EQ(FormatWithStyle(actual, style), actual);
    }
    // An unchanged outer call must not hide a deeper candidate.
    style.ExpressionLayoutCondition =
        formatter::ExpressionLayoutCondition::Preserve;
    style.ColumnLimit = 48;
    const auto nested =
        "void f() {\n"
        "    Outer(Middle(Inner(alpha, beta, gamma, delta, epsilon)));\n"
        "}\n";
    const auto nestedExpected =
        "void f() {\n"
        "    Outer(Middle(Inner(alpha, beta, gamma,\n"
        "        delta, epsilon)));\n"
        "}\n";
    EXPECT_EQ(FormatWithStyle(nested, style), nestedExpected);
    EXPECT_EQ(FormatWithStyle(nestedExpected, style), nestedExpected);
    style.CallArgumentSplit = formatter::CallArgumentSplit::OnePerLine;
    style.EnforceCallArgumentSplit = true;
    ExpectUnchanged("void f() { EXPECT_EQ(a, b); }\n", style);
}

TEST(Formatter, ExpressionLayout_WrapsParsedOperatorsAndPreservesUnsafeLines)
{
    formatter::Style style;
    style.ExpressionLayoutCondition =
        formatter::ExpressionLayoutCondition::Overflow;
    style.TernarySplit = formatter::TernarySplit::AsNeeded;
    style.EnforceTernarySplit = true;
    style.TernaryOperatorPlacement =
        formatter::TernaryOperatorPlacement::Leading;
    style.BinaryOperatorPlacement = formatter::BinaryOperatorPlacement::Leading;
    style.ColumnLimit = 32;

    const test::golden::Fixture fixture{
        std::filesystem::path(FORMATTER_TESTDATA_DIR) / "../parsed-operators",
        "configured"};
    ExpectUnchanged(fixture.Input(), formatter::Style{});

    style.BinaryOperatorPlacement =
        formatter::BinaryOperatorPlacement::Trailing;
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
    ExpectUnchanged("int f(int a, int b) { return a + b + a + b; }\r\n", style);
    ExpectUnchanged("#define SUM(a, b) ((a) + (b) + (a) + (b))\n", style);
    ExpectUnchanged(
        "int compute(int a, int b) { return a + b + a + b; // keep\n}\n",
        style);
    ExpectUnchanged(
        "int compute(int a, int b) { return a + b @ broken; }\n",
        style);
    ExpectUnchanged("template <int N> struct Array {};\n"
        "Array<1 + 2 + 3 + 4 + 5 + 6> value;\n", style);
}

TEST(Formatter, ExpressionLayout_PlacesAndGroupsTernaryBreaks)
{
    formatter::Style style;
    style.EnforceTernaryOperatorPlacement = true;
    style.ExpressionLayoutCondition =
        formatter::ExpressionLayoutCondition::Overflow;
    style.ColumnLimit = 40;
    const auto directory = std::filesystem::path(FORMATTER_TESTDATA_DIR) /
        "../ternary-layout";
    const auto input = ReadFile(directory / "input.cpp");
    for (auto placement : {formatter::TernaryOperatorPlacement::Leading,
        formatter::TernaryOperatorPlacement::Trailing})
    {
        style.TernaryOperatorPlacement = placement;
        style.TernarySplit = formatter::TernarySplit::AsNeeded;
        style.EnforceTernarySplit = true;
        const bool leading =
            placement == formatter::TernaryOperatorPlacement::Leading;
        const auto two = FormatWithStyle(input, style);
        test::golden::ExpectGolden(directory /
            (leading ? "two-leading.output.cpp" : "two-trailing.output.cpp"),
            two);
        EXPECT_EQ(FormatWithStyle(two, style), two);
        style.EnforceTernarySplit = false;
        EXPECT_EQ(FormatWithStyle(input, style), input);
        style.EnforceTernarySplit = true;
        style.TernarySplit = formatter::TernarySplit::ThreeLines;
        const auto three = FormatWithStyle(input, style);
        test::golden::ExpectGolden(directory /
            (leading
                ? "three-leading.output.cpp"
                : "three-trailing.output.cpp"),
            three);
        EXPECT_EQ(FormatWithStyle(three, style), three);
        ExpectUnchanged("int f() { return flag ? a : b; }\n", style);
    }

    const auto threeLeading = ReadFile(directory / "three-leading.output.cpp");
    const auto threeTrailing = ReadFile(directory /
        "three-trailing.output.cpp");

    style.ColumnLimit = 80;
    style.TernaryOperatorPlacement =
        formatter::TernaryOperatorPlacement::Leading;
    style.TernarySplit = formatter::TernarySplit::AsNeeded;
    style.EnforceTernarySplit = true;
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
    style.EnforceTernarySplit = true;
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
    style.TernaryOperatorPlacement =
        formatter::TernaryOperatorPlacement::Trailing;
    EXPECT_EQ(FormatWithStyle(corrected, style), threeTrailing);
    for (auto mode : {formatter::ExpressionLayoutCondition::FitOrOverflow,
        formatter::ExpressionLayoutCondition::Always})
    {
        style.ExpressionLayoutCondition = mode;
        EXPECT_EQ(FormatWithStyle(existing, style), input);
    }
    style.ExpressionLayoutCondition =
        formatter::ExpressionLayoutCondition::Preserve;
    style.EnforceTernarySplit = false;
    ExpectUnchanged(existing, style);
    style.EnforceTernarySplit = true;
    EXPECT_EQ(FormatWithStyle(existing, style), threeTrailing);
    style.ExpressionLayoutCondition =
        formatter::ExpressionLayoutCondition::Overflow;
    ExpectUnchanged("int f() {\r\n    return condition ?\r\n"
        "        alphaValue : betaValue;\r\n}\r\n", style);
    style.TernaryOperatorPlacement =
        formatter::TernaryOperatorPlacement::Trailing;
    style.TernarySplit = formatter::TernarySplit::ThreeLines;
    style.EnforceTernarySplit = true;
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
    preserve.EnforceTernaryOperatorPlacement = true;
    EXPECT_EQ(FormatWithStyle(existing, preserve), corrected);
    preserve.EnforceTernaryOperatorPlacement = false;
    preserve.TernaryOperatorPlacement =
        formatter::TernaryOperatorPlacement::Trailing;
    preserve.TernarySplit = formatter::TernarySplit::AsNeeded;
    preserve.EnforceTernarySplit = false;
    ExpectUnchanged(existing, preserve);
    preserve.TernarySplit = formatter::TernarySplit::ThreeLines;
    preserve.EnforceTernarySplit = true;
    EXPECT_EQ(FormatWithStyle(existing, preserve), threeTrailing);
}

TEST(Formatter, ExpressionLayout_ChecksLayoutWithoutReflowing)
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
    auto tree = parser::Parse(
        {.Data = storage.data(), .Size = length},
        [] (parser::ParseError) { });
    formatter::Style style;
    style.ExpressionLayoutCondition =
        formatter::ExpressionLayoutCondition::Overflow;
    style.ColumnLimit = 40;
    style.LineLengthCheck = formatter::LineLengthCheck::Strict;
    style.TernaryOperatorPlacement =
        formatter::TernaryOperatorPlacement::Leading;
    style.EnforceTernaryOperatorPlacement = true;
    style.TernarySplit = formatter::TernarySplit::ThreeLines;
    style.EnforceTernarySplit = true;
    const auto issues = formatter::CheckExpressionLayout(tree, style);
    ASSERT_EQ(issues.size(), 4u);
    EXPECT_EQ(issues[0].Position.Line, 2u);
    EXPECT_NE(
        issues[0].Message.find("line exceeds ColumnLimit"),
        std::string::npos);
    EXPECT_EQ(issues[1].Position.Line, 4u);
    EXPECT_EQ(issues[1].Message, "binary operator must end the preceding line");
    EXPECT_EQ(issues[2].Position.Line, 5u);
    EXPECT_EQ(issues[2].Message, "ternary ? must start the following line");
    EXPECT_EQ(issues[3].Position.Line, 6u);
    EXPECT_EQ(
        issues[3].Message,
        "ternary ':' needs a line break to match the other delimiter");
    style.EnforceTernaryOperatorPlacement = false;
    style.TernarySplit = formatter::TernarySplit::Preserve;
    style.EnforceTernarySplit = false;
    EXPECT_EQ(formatter::CheckExpressionLayout(tree, style).size(), 2u);
    style.ExpressionLayoutCondition =
        formatter::ExpressionLayoutCondition::Preserve;
    const auto widthOnly = formatter::CheckExpressionLayout(tree, style);
    ASSERT_EQ(widthOnly.size(), 1u);
    EXPECT_EQ(widthOnly[0].Position.Line, 2u);
    EXPECT_EQ(widthOnly[0].Kind, formatter::LayoutDiagnosticKind::LineWidth);
    style.LineLengthCheck = formatter::LineLengthCheck::Preserve;
    EXPECT_TRUE(formatter::CheckExpressionLayout(tree, style).empty());
    style.ExpressionLayoutCondition =
        formatter::ExpressionLayoutCondition::Overflow;
    const auto placementOnly = formatter::CheckExpressionLayout(tree, style);
    ASSERT_EQ(placementOnly.size(), 1u);
    EXPECT_EQ(
        placementOnly[0].Kind,
        formatter::LayoutDiagnosticKind::OperatorPlacement);
}

struct LayoutDiagnosticCase
{
    std::string Directory;
    std::string Name;
    std::string Input = "input.cpp";
    std::string Config;
    bool StrictLength = false;
    std::string Suffix = ".expected";
};

void PrintTo(const LayoutDiagnosticCase& value, std::ostream* output)
{
    *output << value.Directory << '/' << value.Name;
}

class LayoutDiagnosticTest
    : public testing::TestWithParam<LayoutDiagnosticCase>
{ };

TEST_P(LayoutDiagnosticTest, Golden)
{
    const auto& value = GetParam();
    const auto directory = std::filesystem::path(FORMATTER_TESTDATA_DIR) /
        ".." / value.Directory;
    auto source = ReadFile(directory / value.Input);
    const auto size = source.size();
    source.append(util::SourcePaddingSize, '\0');
    const auto tree = parser::Parse(
        {.Data = source.data(), .Size = size},
        [] (parser::ParseError) { });
    const auto config = value.Config.empty() ? value.Name : value.Config;
    auto style = formatter::LoadConfigFile(
        directory / (config + ".hit-fmt")).FormattingStyle;
    if (value.StrictLength) {
        style.LineLengthCheck = formatter::LineLengthCheck::Strict;
    }
    std::string actual;
    for (const auto& diagnostic :
        formatter::CheckExpressionLayout(tree, style))
    {
        actual += std::to_string(diagnostic.Position.Line) + ":" +
            std::to_string(diagnostic.Position.Column) + ": " +
            diagnostic.Message + "\n";
    }
    test::golden::ExpectGolden(
        directory / (value.Name + value.Suffix), actual);
}

INSTANTIATE_TEST_SUITE_P(Formatter, LayoutDiagnosticTest,
    testing::Values(
        LayoutDiagnosticCase{"format-disabled", "regions",
            "regions.input.cpp", {}, false, ".layout.expected"},
        LayoutDiagnosticCase{"line-length", "soft"},
        LayoutDiagnosticCase{"line-length", "strict", "input.cpp",
            "soft", true},
        LayoutDiagnosticCase{"assignment-location", "leading"},
        LayoutDiagnosticCase{"assignment-location", "trailing"}),
    [] (const auto& info) {
        auto name = info.param.Directory + "_" + info.param.Name;
        std::ranges::replace(name, '-', '_');
        return name;
    });

TEST(Formatter, ExpressionLayout_ChecksLineLengthWithoutReflowing)
{
    const auto directory = std::filesystem::path(FORMATTER_TESTDATA_DIR) /
        "../line-length";
    auto source = ReadFile(directory / "input.cpp");
    const auto size = source.size();
    source.append(util::SourcePaddingSize, '\0');
    const auto tree = parser::Parse(
        {.Data = source.data(), .Size = size},
        [] (parser::ParseError) { });
    auto style = formatter::LoadConfigFile(
        directory / "soft.hit-fmt").FormattingStyle;
    style.LineLengthCheck = formatter::LineLengthCheck::Strict;
    style.ExpressionLayoutCondition =
        formatter::ExpressionLayoutCondition::Preserve;
    const auto strictWithoutLayout =
        formatter::CheckExpressionLayout(tree, style);
    ASSERT_EQ(strictWithoutLayout.size(), 4u);
    style.LineLengthCheck = formatter::LineLengthCheck::Soft;
    EXPECT_TRUE(formatter::CheckExpressionLayout(tree, style).empty());
}

TEST(Formatter, ExpressionLayout_WrapsLongBinaryExpressions)
{
    formatter::Style style;
    style.ExpressionLayoutCondition =
        formatter::ExpressionLayoutCondition::Overflow;
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
    EXPECT_EQ(FormatWithStyle(input, style), greedy);
    EXPECT_EQ(FormatWithStyle(greedy, style), greedy);
    style.BinaryOperatorPlacement = formatter::BinaryOperatorPlacement::Leading;
    const auto leading =
        "void f() {\n"
        "    return a + bb + ccc + dddd + eeeee\n"
        "        + ffffff;\n"
        "}\n";
    EXPECT_EQ(FormatWithStyle(input, style), leading);
    style.BinaryOperatorPlacement =
        formatter::BinaryOperatorPlacement::Trailing;
    style.ExpressionLayoutCondition =
        formatter::ExpressionLayoutCondition::Preserve;
    ExpectUnchanged(input, style);
    style.ExpressionLayoutCondition =
        formatter::ExpressionLayoutCondition::Always;
    style.ColumnLimit = 20;
    ExpectUnchanged("void f() { bool v = alpha<beta; }\n", style);
}

TEST(Formatter, ExpressionLayout_ReflowsSafeOperatorContinuationsByPolicy)
{
    formatter::Style style;
    style.ExpressionLayoutCondition =
        formatter::ExpressionLayoutCondition::Always;
    style.TernarySplit = formatter::TernarySplit::AsNeeded;
    style.EnforceTernarySplit = true;
    style.TernaryOperatorPlacement =
        formatter::TernaryOperatorPlacement::Leading;
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
    style.ExpressionLayoutCondition =
        formatter::ExpressionLayoutCondition::FitOrOverflow;
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
    style.ExpressionLayoutCondition =
        formatter::ExpressionLayoutCondition::Overflow;
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
    style.BinaryOperatorPlacement =
        formatter::BinaryOperatorPlacement::Trailing;
    style.ColumnLimit = 30;
    style.ExpressionLayoutCondition =
        formatter::ExpressionLayoutCondition::Always;
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

TEST(Formatter, FunctionParameterSafety_ComposesWithBracePlacement)
{
    formatter::Style style;
    style.ColumnLimit = 40;
    style.FunctionParameterSplit = formatter::FunctionParameterSplit::Pack;
    style.FunctionBracePlacement = formatter::BracePlacement::NewLine;
    style.EnforceFunctionBracePlacement = false;
    const auto format = [&] (std::string_view source) {
        std::string storage(source);
        storage.append(util::SourcePaddingSize, '\0');
        return formatter::Format(
            {.Data = storage.data(), .Size = source.size()},
            style);
    };
    const auto original =
        "void Many(int alpha, int beta, int gamma, int delta) {}\n";
    EXPECT_EQ(format(original), original);
    style.EnforceFunctionParameterSplit = true;
    const std::string expected =
        "void Many(int alpha, int beta,\n"
        "    int gamma, int delta)\n"
        "{}\n";
    EXPECT_EQ(
        format("void Many(int alpha, int beta, int gamma, int delta) {}\n"),
        expected);
    EXPECT_EQ(format(expected), expected);
    EXPECT_EQ(format("void Short() {}\n"), "void Short() {}\n");
    style.EnforceFunctionBracePlacement = true;
    EXPECT_EQ(format("void Short() {}\n"), "void Short()\n{}\n");
    style.IndentationKind = formatter::IndentationKind::Tabs;
    const std::string tabbed =
        "void Many(int alpha, int beta,\n"
        "\tint gamma, int delta)\n"
        "{}\n";
    EXPECT_EQ(
        format("void Many(int alpha, int beta, int gamma, int delta) {}\n"),
        tabbed);
    EXPECT_EQ(format(tabbed), tabbed);
}

TEST(Formatter, FunctionParameterSafety_KeepsCrlfListsAndCalls)
{
    formatter::Style style;
    style.FunctionParameterSplit = formatter::FunctionParameterSplit::Pack;
    style.EnforceFunctionParameterSplit = true;
    style.ColumnLimit = 25;
    ExpectUnchanged(
        "void f(int alpha, int beta, int gamma);\r\n"
        "void g() { Use(very_long_argument, another_long_argument); }\r\n",
        style);
}

TEST(Formatter, BlockIndentation_AnchorsInlineLambdaToItsCallLine)
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

TEST(Formatter, ControlBrace_WrapsOnlyCompleteUnbracedBodies)
{
    formatter::Style style;
    style.AddMissingControlBraces = true;
    style.IndentBlockBodies = true;
    style.ControlBlockBracePlacement =
        formatter::ControlBlockBracePlacement::AttachUnlessMultiline;
    style.EnforceControlBlockBracePlacement = true;
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
        "void f() {\n    if (a);\n    if (b) Go();\n}\n",
        style),
        "void f() {\n    if (a) { }\n    if (b) Go();\n}\n");
    style.EmptyControlBody = formatter::EmptyControlBody::Preserve;
    style.AddMissingControlBraces = true;
    ExpectUnchanged("void f(bool a) { if (a) /* keep */ Go(); }\n", style);
    EXPECT_EQ(FormatWithStyle(
        "void f(bool a) { if (a)\r\n    Go(); }\r\n",
        style),
        "void f(bool a) { if (a)\r\n        Go(); }\r\n");
    ExpectUnchanged("void f(int x) { switch (x) case 0: Go(); }\n", style);
    EXPECT_EQ(FormatWithStyle(
        "void f(int x)\n{\n    switch (x) Go();\n}\n",
        style),
        "void f(int x)\n{\n    switch (x) {\n"
        "        Go();\n"
        "    }\n}\n");
    EXPECT_EQ(FormatWithStyle(
        "void f(bool a)\n{\n    if (a) Go(); Other();\n}\n",
        style),
        "void f(bool a)\n{\n"
        "    if (a) {\n"
        "        Go();\n"
        "    }\n"
        "    Other();\n"
        "}\n");
}

TEST(Formatter, EmptyFunctionBody_CollapsesWhitespaceOnlyBody)
{
    formatter::Style style;
    style.CollapseEmptyFunctionBodies = true;
    style.FunctionBracePlacement = formatter::BracePlacement::NewLine;
    style.EnforceFunctionBracePlacement = true;
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
        "void Annotated() { /* keep */ Act(); }\n",
        style),
        "void Annotated()\n{ /* keep */ Act(); }\n");
}

TEST(Formatter, OperatorSpacing_PreservesUnknownCastOrBinaryOperator)
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
        source.find("(a + b) & b"),
        std::string("(a + b) & b").size(),
        "(a + b)&b");
    EXPECT_EQ(FormatWithStyle(compact, style), source);
    const auto separatedCast = std::string(source).replace(
        source.find("(int)&a"),
        std::string("(int)&a").size(),
        "(int) & a");
    EXPECT_EQ(FormatWithStyle(separatedCast, style), source);
}

TEST(Formatter, BraceSpacing_ComposesWithParameterReflow)
{
    formatter::Style style;
    style.ColumnLimit = 40;
    style.FunctionParameterSplit = formatter::FunctionParameterSplit::Pack;
    style.EnforceFunctionParameterSplit = true;
    style.FunctionBraceSpacing = formatter::SameLineBraceSpacing::SingleSpace;
    style.EnforceFunctionBraceSpacing = true;
    const std::string input =
        "void Many(int alpha, int beta, int gamma, int delta){}\n";
    const std::string expected =
        "void Many(int alpha, int beta,\n"
        "    int gamma, int delta) {}\n";
    EXPECT_EQ(FormatWithStyle(input, style), expected);
    EXPECT_EQ(FormatWithStyle(expected, style), expected);

    style.FunctionBracePlacement = formatter::BracePlacement::Attach;
    style.EnforceFunctionBracePlacement = true;
    const std::string detached =
        "void Many(int alpha, int beta, int gamma, int delta)\n{}\n";
    EXPECT_EQ(FormatWithStyle(detached, style), expected);
}

class NamingTest
    : public testing::TestWithParam<std::string>
{ };

TEST_P(NamingTest, Golden)
{
    const auto dir = std::filesystem::path(NAMING_TESTDATA_DIR);
    const auto name = GetParam();
    const auto input = ReadFile(
        dir / (name == "rules" ? "input.cpp" : "alternate.cpp"));
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

INSTANTIATE_TEST_SUITE_P(Formatter, NamingTest,
    testing::Values("rules", "alternate", "no_exempt"), ConfigName);

TEST(Formatter, Preserve_KeepsEmptyRecoveryAndBinarySource)
{
    for (const std::string source : {
        std::string{},
        std::string{" \t\r\n\r\n"},
        std::string{"#define STR(x) #x\r\nSTR(a+b);\r\n"},
        std::string{"int f(\n  /* unfinished"},
        std::string{"int x;\0int y;\n", 14},
    })
    {
        ExpectUnchanged(source, formatter::Style{});
    }
}

TEST(Formatter, PreserveConfig_ParsesSettings)
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
         "MultilineSingleCallArgumentPosition = CallLine\n"
         "EnforceFirstCallArgumentPosition = true\n"
         "CallArgumentIndent = Continuation\n"
         "CallLineFirstStringNames = [\"YT_LOG_INFO\", \"YT_LOG_ERROR\"]\n"
         "IndentedCallScopePairs = [\"OnBeginList:OnEndList\"]\n"
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
    EXPECT_EQ(
        config.FormattingStyle.ExpressionLayoutCondition,
        formatter::ExpressionLayoutCondition::FitOrOverflow);
    EXPECT_EQ(
        config.FormattingStyle.BinaryOperatorPlacement,
        formatter::BinaryOperatorPlacement::Trailing);
    EXPECT_EQ(
        config.FormattingStyle.AssignmentOperatorPlacement,
        formatter::BinaryOperatorPlacement::Trailing);
    EXPECT_EQ(
        config.FormattingStyle.StatementAttributeIndent,
        formatter::StatementAttributeIndent::Block);
    EXPECT_EQ(config.FormattingStyle.AccessModifierIndentWidth, 1u);
    EXPECT_EQ(config.FormattingStyle.MemberIndentWidth, 3u);
    EXPECT_EQ(config.FormattingStyle.BlockBodyIndentWidth, 4u);
    EXPECT_EQ(config.FormattingStyle.CaseLabelIndentWidth, 1u);
    EXPECT_EQ(config.FormattingStyle.StatementAttributeIndentWidth, 2u);
    EXPECT_EQ(config.FormattingStyle.NamespaceCommentSpaces, 2u);
    EXPECT_EQ(
        config.FormattingStyle.CommaSpacing,
        formatter::CommaSpacing::Space);
    EXPECT_EQ(
        config.FormattingStyle.TernaryOperatorPlacement,
        formatter::TernaryOperatorPlacement::Trailing);
    EXPECT_EQ(
        config.FormattingStyle.TernarySplit,
        formatter::TernarySplit::ThreeLines);
    EXPECT_EQ(
        config.FormattingStyle.CallArgumentSplit,
        formatter::CallArgumentSplit::Pack);
    EXPECT_EQ(
        config.FormattingStyle.FirstCallArgumentPosition,
        formatter::FirstCallArgumentPosition::NewLine);
    EXPECT_EQ(
        config.FormattingStyle.MultilineSingleCallArgumentPosition,
        formatter::MultilineSingleCallArgumentPosition::CallLine);
    EXPECT_TRUE(config.FormattingStyle.EnforceFirstCallArgumentPosition);
    EXPECT_EQ(
        config.FormattingStyle.CallArgumentIndent,
        formatter::CallArgumentIndent::Continuation);
    EXPECT_EQ(
        config.FormattingStyle.CallLineFirstStringNames,
        (std::vector<std::string>{"YT_LOG_INFO", "YT_LOG_ERROR"}));
    EXPECT_EQ(
        config.FormattingStyle.IndentedCallScopePairs,
        (std::vector<std::string>{"OnBeginList:OnEndList"}));
    ASSERT_EQ(config.Exclude.size(), 2u);
    EXPECT_TRUE(std::regex_search("x]y", config.Exclude[0]));
    EXPECT_TRUE(std::regex_search("generated/source.cpp", config.Exclude[1]));
    ASSERT_TRUE(config.Naming.Class.Prefixes.has_value());
    EXPECT_EQ(
        *config.Naming.Class.Prefixes,
        (std::vector<std::string>{"S", "C"}));
    save("ClassNameCase = Unchecked\n"
         "ClassNamePrefixes = []\n"
         "SeparatorLength = 0\n"
         "InheritanceListIndent = 0\n"
         "ConstructorInitializerListIndent = 0\n"
         "ShortNamespaceLines = 0\n");
    const auto disabled = formatter::LoadConfigFile(path);
    EXPECT_EQ(disabled.Naming.Class.Case, formatter::NameCase::Unchecked);
    EXPECT_FALSE(disabled.Naming.Class.Prefixes.has_value());
    EXPECT_EQ(disabled.FormattingStyle.SeparatorLength, 0u);
    EXPECT_EQ(disabled.FormattingStyle.InheritanceListIndent, 0u);
    EXPECT_EQ(disabled.FormattingStyle.ConstructorInitializerListIndent, 0u);
    EXPECT_EQ(disabled.FormattingStyle.ShortNamespaceLines, 0u);
    save("CallArgumentSplit = Pack\nExpressionLayoutCondition = Always\n");
    const auto alternate = formatter::LoadConfigFile(path).FormattingStyle;
    EXPECT_EQ(
        alternate.ExpressionLayoutCondition,
        formatter::ExpressionLayoutCondition::Always);
    save("ExpressionLayoutCondition = Overflow\n"
         "TernaryOperatorPlacement = Leading\n"
         "TernarySplit = AsNeeded\n");
    const auto preserving = formatter::LoadConfigFile(path).FormattingStyle;
    EXPECT_EQ(
        preserving.ExpressionLayoutCondition,
        formatter::ExpressionLayoutCondition::Overflow);
    EXPECT_EQ(
        preserving.TernaryOperatorPlacement,
        formatter::TernaryOperatorPlacement::Leading);
    EXPECT_EQ(preserving.TernarySplit, formatter::TernarySplit::AsNeeded);
    EXPECT_FALSE(preserving.EnforceTernarySplit);
    std::filesystem::remove(path);
}

} // namespace
