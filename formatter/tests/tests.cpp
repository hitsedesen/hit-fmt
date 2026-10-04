#include "public_pipeline.hpp"
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
    EXPECT_EQ(
        formatter::Format(
            {.Data = storage.data(), .Size = first.size()},
            style),
        first);
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
INSTANTIATE_TEST_SUITE_P(
    Formatter,
    PreserveTest,
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
    const auto actual = formatter::Format(tree, style);
    EXPECT_EQ(actual, formatter::Format(padded, style));
    return actual;
}
void ExpectFormattedGolden(
    const std::filesystem::path& path,
    std::string_view input,
    const formatter::Style& style,
    bool checkOwningMaps,
    bool readOnlyExpected)
{
    const auto actual = FormatWithStyle(input, style);
    if (readOnlyExpected) {
        // Shared oracles, including inputs, must never be regenerated here.
        EXPECT_EQ(actual, ReadFile(path)) << path;
    } else {
        test::golden::ExpectGolden(path, actual);
    }
    EXPECT_EQ(FormatWithStyle(actual, style), actual);
    if (checkOwningMaps) {
        public_pipeline::CheckFormattedResult(input, style, actual);
    }
}

struct FormatCase
{
    std::filesystem::path Directory;
    std::string Name;
    std::string Input;
    std::string Config;
    std::vector<formatter::Hunk> Hunks;
    bool Spillover;
    std::string Expected;
    bool CheckOwningMaps;

    FormatCase(
        std::filesystem::path directory,
        std::string name,
        std::string input,
        std::string config = {},
        std::vector<formatter::Hunk> hunks = {},
        bool spillover = true,
        std::string expected = {},
        bool checkOwningMaps = false)
        : Directory(std::move(directory))
        , Name(std::move(name))
        , Input(std::move(input))
        , Config(std::move(config))
        , Hunks(std::move(hunks))
        , Spillover(spillover)
        , Expected(std::move(expected))
        , CheckOwningMaps(checkOwningMaps)
    { }
};

void PrintTo(const FormatCase& value, std::ostream* output)
{
    const auto root = (std::filesystem::path(FORMATTER_TESTDATA_DIR) / "..")
        .lexically_normal();
    *output
        << value.Directory.lexically_normal().lexically_relative(
            root).generic_string() << '/' << value.Name;
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
        std::filesystem::path(FORMATTER_TESTDATA_DIR) /
        "../declaration-headers",
        {"wrap", "narrow", "wide"});
    AddCases(
        cases,
        std::filesystem::path(FORMATTER_TESTDATA_DIR) / "../vertical-grouping",
        {"preserve", "declarations", "overrides", "capped"});
    AddCases(
        cases,
        std::filesystem::path(FORMATTER_TESTDATA_DIR) / "../vertical-grouping",
        {"structural"},
        "structural.input.cpp");
    AddCases(
        cases,
        std::filesystem::path(FORMATTER_TESTDATA_DIR) / "../group-fit-boundary",
        {"fit", "overflow"});
    AddCases(
        cases,
        std::filesystem::path(FORMATTER_TESTDATA_DIR) / "../group-fit-boundary",
        {"forced"},
        "forced.input.cpp");
    AddCases(
        cases,
        std::filesystem::path(FORMATTER_TESTDATA_DIR) / "../group-fit-boundary",
        {"call-fit"},
        "call.input.cpp");
    const auto llvmProfile = (std::filesystem::path(FORMATTER_TESTDATA_DIR) /
        "../../../../hit-fmt/examples/llvm").lexically_normal().string();
    for (const auto* name : {"lf", "opaque", "no-final-newline"}) {
        cases.push_back({
            std::filesystem::path(FORMATTER_TESTDATA_DIR) / "../example-llvm",
            name,
            std::string(name) + ".input.cpp",
            llvmProfile,
        });
    }
    cases.push_back({
        std::filesystem::path(FORMATTER_TESTDATA_DIR) /
            "../control-expression-continuation",
        "repository",
        "input.cpp",
        "../../../../",
    });
    cases.push_back({
        std::filesystem::path(FORMATTER_TESTDATA_DIR) /
            "../control-expression-continuation",
        "ytsaurus",
        "input.cpp",
        "../../../../hit-fmt/examples/ytsaurus",
    });
    // Bind to the root configuration, not a fixture-local copy of its settings.
    for (const auto* name : {"repository-lf", "repository-crlf"}) {
        cases.push_back({
            std::filesystem::path(FORMATTER_TESTDATA_DIR) /
                "../example-repository",
            name,
            std::string(name) + ".input.cpp",
            "../../../../",
        });
    }
    for (const auto* name :
        {
            "lf",
            "crlf",
            "legacy",
            "canonical-lf",
            "canonical-crlf",
            "opaque-lf",
            "boundary-lf",
            "boundary-crlf",
            "boundary-nulls",
            "boundary-namespace-lf",
            "boundary-namespace-crlf",
            "composition-control-lf",
        })
    {
        cases.push_back({
            std::filesystem::path(FORMATTER_TESTDATA_DIR) / "../line-endings",
            name,
            std::string(name) + ".input.cpp",
        });
        cases.back().CheckOwningMaps = true;
    }
    AddCases(
        cases,
        std::filesystem::path(FORMATTER_TESTDATA_DIR) / "../style-checkpoint",
        {"lf", "crlf"});
    AddCases(
        cases,
        std::filesystem::path(FORMATTER_TESTDATA_DIR) /
        "../trailing-comment-count",
        {"preserve", "zero", "one", "two"});
    cases.push_back({
        std::filesystem::path(FORMATTER_TESTDATA_DIR) /
            "../trailing-comment-count",
        "independent",
        "namespace.input.cpp",
    });
    AddCases(
        cases,
        std::filesystem::path(FORMATTER_TESTDATA_DIR) /
        "../standalone-comment-gaps",
        {"preserve", "before", "after", "zero", "canonical"});
    AddCases(
        cases,
        BRACE_TESTDATA_DIR,
        {"preserve", "attach", "newline", "mixed"});
    AddCases(
        cases,
        std::filesystem::path(FORMATTER_TESTDATA_DIR) / "../include-order",
        {"sort", "composed"});
    cases.push_back({
        std::filesystem::path(FORMATTER_TESTDATA_DIR) / "../include-order",
        "crlf",
        "crlf.input.cpp",
        "sort",
    });
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
    cases.push_back({
        CONTROL_BRACE_TESTDATA_DIR,
        "reflow-preferred",
        "reflow-preferred.input.cpp",
    });
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
    cases.push_back({
        CALL_INNER_SPACING_TESTDATA_DIR,
        "reflow-preferred",
        "reflow-preferred.input.cpp",
    });
    cases.push_back({
        CALL_INNER_SPACING_TESTDATA_DIR,
        "comma-preferred",
        "comma-preferred.input.cpp",
    });
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
    cases.push_back({
        BODY_INDENT_TESTDATA_DIR,
        "reindented-call",
        "reindented-call.input.cpp",
    });
    const auto nullStatements =
        std::filesystem::path(FORMATTER_TESTDATA_DIR) / "../null-statements";
    AddCases(cases, nullStatements, {"preserve", "remove"});
    for (const auto& [name, config] :
        {
            std::pair{"decl-preserve", "preserve"},
        std::pair{"decl-statements", "remove"},
        std::pair{"decl-only", "declarations"},
        std::pair{"decl-both", "both"}
        })
    {
        cases.push_back(
            {nullStatements, name, "declarations.input.cpp", config});
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
    const auto chains =
        std::filesystem::path(FORMATTER_TESTDATA_DIR) / "../chained-calls";
    AddCases(cases, chains, {"always", "continuation", "preserve"});
    cases.push_back({chains, "overflow", "overflow.input.cpp"});
    cases.push_back({chains, "child-fitting", "child-fitting.input.cpp"});
    cases.push_back({
        chains,
        "comparison-fitting",
        "comparison-fitting.input.cpp",
        "child-fitting",
    });
    cases.push_back({chains, "structural-base", "structural-base.input.cpp"});
    AddCases(
        cases,
        chains,
        {"conventional", "conventional-preserve", "conventional-selected"},
        "conventional.input.cpp");
    AddCases(
        cases,
        chains,
        {
            "scoped-sticky",
            "scoped-only",
            "sticky-only",
            "annotations-preserve",
            "annotations-filtered",
            "annotations-narrow",
            "annotations-fitting",
            "annotations-delimiter",
        },
        "annotations.input.cpp");
    AddCases(
        cases,
        std::filesystem::path(FORMATTER_TESTDATA_DIR) / "../separator-length",
        {"preserve"});
    AddCases(
        cases,
        std::filesystem::path(FORMATTER_TESTDATA_DIR) /
        "../standalone-block-indent",
        {"preserve", "indent"});
    const auto closings =
        std::filesystem::path(FORMATTER_TESTDATA_DIR) / "../closing-delimiters";
    AddCases(cases, closings, {"same-line", "block", "preserve"});
    const auto initializerSafety =
        std::filesystem::path(FORMATTER_TESTDATA_DIR) / "../initializer-safety";
    AddCases(cases, initializerSafety, {"conditional", "never", "preserve"});
    AddCases(
        cases,
        std::filesystem::path(FORMATTER_TESTDATA_DIR) / "../declaration-gap",
        {"canonical", "preserve"});
    const auto ownerContext =
        std::filesystem::path(FORMATTER_TESTDATA_DIR) / "../owner-context";
    AddCases(
        cases,
        ownerContext,
        {
            "canonical-protected",
            "preserve-protected",
            "canonical-safe",
            "explicit-protected",
        });
    const auto recordHead =
        std::filesystem::path(FORMATTER_TESTDATA_DIR) / "../record-head-commas";
    AddCases(
        cases,
        recordHead,
        {
            "space",
            "no-space",
            "preserve",
            "generated-canonical",
            "generated-preserve",
        });
    for (const auto* profile :
        {
            "space",
            "no-space",
            "preserve",
            "generated-canonical",
            "generated-preserve",
        })
    {
        cases.push_back({
            recordHead,
            std::string("comparisons-") + profile,
            "comparisons.input.cpp",
            profile,
        });
    }
    for (const auto* profile : {"space", "generated-preserve"}) {
        cases.push_back({
            recordHead,
            std::string("constructor-composition-") + profile,
            "constructor-composition.input.cpp",
            profile,
        });
    }
    cases.push_back(
        {closings, "reflow-preferred", "reflow-preferred.input.cpp"});
    cases.push_back({closings, "generated-block", "generated-block.input.cpp"});
    cases.push_back(
        {closings, "layout-preferred", "layout-preferred.input.cpp"});
    cases.push_back({closings, "new-line", "new-line.input.cpp"});
    cases.push_back(
        {closings, "conditional-preferred", "trailing-comma.input.cpp"});
    cases.push_back({closings, "remove", "trailing-comma.input.cpp"});
    cases.push_back({closings, "trailing-add", "trailing-layout.input.cpp"});
    cases.push_back(
        {closings, "trailing-conditional", "trailing-layout.input.cpp"});
    cases.push_back(
        {closings, "trailing-preferred", "trailing-preferred.input.cpp"});
    cases.push_back(
        {closings, "trailing-removed", "trailing-removed.input.cpp"});
    cases.push_back({closings, "trailing-only", "trailing-layout.input.cpp"});
    cases.push_back(
        {closings, "trailing-new-line", "trailing-layout.input.cpp"});
    AddCases(
        cases,
        std::filesystem::path(FORMATTER_TESTDATA_DIR) / "../fold-spacing",
        {"yt-like", "inherited", "group-space"});
    const auto profile = (std::filesystem::path(FORMATTER_TESTDATA_DIR) /
        "../../../../hit-fmt/examples/ytsaurus").lexically_normal().string();
    auto addProfileCase = [&] (const char* directory, const char* name) {
        cases.push_back({
            std::filesystem::path(FORMATTER_TESTDATA_DIR) / directory,
            name,
            std::string(name) + ".input.cpp",
            profile,
        });
    };
    for (const auto* name :
        {
            "constructor-lists",
            "chains",
            "namespace-comments",
            "macro-protection",
            "annotations",
        })
    {
        addProfileCase("../example-ytsaurus", name);
    }
    addProfileCase("../preprocessing-fragments", "canonical");
    addProfileCase("../spacing-options", "profile-initializers");
    addProfileCase("../spacing-options", "profile-overflow");
    addProfileCase("../spacing-options", "profile-case-blocks");
    addProfileCase("../spacing-options", "profile-ref-qualifiers");
    AddCases(
        cases,
        std::filesystem::path(FORMATTER_TESTDATA_DIR) / "../spacing-options",
        {
            "function-cv-ref-space",
            "function-cv-ref-no-space",
            "function-cv-ref-preserve",
            "function-cv-ref-inherit",
        },
        "profile-ref-qualifiers.input.cpp");
    cases.push_back({
        std::filesystem::path(FORMATTER_TESTDATA_DIR) / "../spacing-options",
        "initializer-overflow-one-per-line",
        "initializer-overflow-one-per-line.input.cpp",
    });
    addProfileCase("../ternary-layout", "profile");
    AddCases(
        cases,
        std::filesystem::path(FORMATTER_TESTDATA_DIR) / "../ternary-layout",
        {
            "branches-preserve",
            "branches-narrow",
            "branches-indent-preserve",
            "branches-indent-narrow",
            "branches-indent-trailing",
        },
        "branches.input.cpp");
    AddCases(
        cases,
        std::filesystem::path(FORMATTER_TESTDATA_DIR) / "../ternary-layout",
        {"operators-align", "operators-indent"},
        "operators.input.cpp");
    cases.push_back({
        std::filesystem::path(FORMATTER_TESTDATA_DIR) / "../ternary-layout",
        "reflow-preferred",
        "reflow-preferred.input.cpp",
    });
    AddCases(
        cases,
        std::filesystem::path(FORMATTER_TESTDATA_DIR) / "../ternary-layout",
        {
            "initializer-shape",
            "initializer-shape-flexible",
            "initializer-shape-narrow",
        },
        "initializer-shape.input.cpp");
    addProfileCase("../assignment-location", "profile");
    addProfileCase("../function_parameters", "profile");
    addProfileCase("../body-indent", "profile-control-local");
    addProfileCase("../body-indent", "profile-direct-initializer");
    cases.push_back({
        std::filesystem::path(FORMATTER_TESTDATA_DIR) / "../null-statements",
        "profile",
        "input.cpp",
        profile,
    });
    cases.push_back({
        BODY_INDENT_TESTDATA_DIR,
        "grouped-profile",
        "grouped-statements.input.cpp",
        profile,
    });
    cases.push_back({
        BODY_INDENT_TESTDATA_DIR,
        "lambda-comments-profile",
        "lambda-leading-comments.input.cpp",
        profile,
    });
    cases.push_back({
        BODY_INDENT_TESTDATA_DIR,
        "lambda-trailing-return",
        "lambda-trailing-return.input.cpp",
    });
    addProfileCase("../lambda-captures", "profile");
    addProfileCase("../call-reflow", "profile-tabbed-log");
    const auto callReflow =
        std::filesystem::path(FORMATTER_TESTDATA_DIR) / "../call-reflow";
    AddCases(
        cases,
        callReflow,
        {"sole-call-line", "sole-new-line"},
        "sole-position.input.cpp");
    AddCases(
        cases,
        callReflow,
        {
            "sole-argument",
            "sole-argument-inherit",
            "sole-argument-next-line",
            "sole-argument-always",
            "sole-argument-preserve",
        },
        "sole-argument.input.cpp");
    cases.push_back({
        callReflow,
        "sole-argument-ytsaurus",
        "sole-argument.input.cpp",
        profile,
    });
    cases.push_back({
        callReflow,
        "sole-argument-repository",
        "sole-argument.input.cpp",
        "../../../../",
    });
    cases.push_back({callReflow, "sole-preferred", "sole-preferred.input.cpp"});
    cases.push_back({
        callReflow,
        "first-position-reflow",
        "first-position-reflow.input.cpp",
    });
    cases.push_back(
        {callReflow, "first-string-inline", "first-string-inline.input.cpp"});
    cases.push_back({
        std::filesystem::path(FORMATTER_TESTDATA_DIR) /
            "../function_parameters",
        "first-position-reflow",
        "first-position-reflow.input.cpp",
    });
    AddCases(
        cases,
        std::filesystem::path(FORMATTER_TESTDATA_DIR) / "../macro-nested-calls",
        {"configured"});
    AddCases(
        cases,
        std::filesystem::path(FORMATTER_TESTDATA_DIR) / "../lambda-body",
        {"preserve", "inherit", "inline"});
    AddCases(
        cases,
        std::filesystem::path(FORMATTER_TESTDATA_DIR) /
        "../control-header-clauses",
        {"preserve", "split", "indent-width"});
    const auto controlPlacement = std::filesystem::path(
        FORMATTER_TESTDATA_DIR) / "../control-header-placement";
    AddCases(
        cases,
        controlPlacement,
        {"preserve", "first-only", "closing-only", "both"});
    cases.push_back({controlPlacement, "reflow", "reflow.input.cpp"});
    cases.push_back({controlPlacement, "reverse", "reverse.input.cpp"});
    cases.push_back({controlPlacement, "crlf", "crlf.input.cpp"});
    cases.push_back({
        std::filesystem::path(FORMATTER_TESTDATA_DIR) / "../lambda-body",
        "profile",
        "input.cpp",
        profile,
    });
    addProfileCase("../control-header-clauses", "profile");
    addProfileCase("../whitespace-sensitive-macros", "profile");
    addProfileCase("../call-reflow", "balanced-expressions");
    addProfileCase("../call-reflow", "nested-operator-choice");
    cases.push_back({
        callReflow,
        "balanced-initializers",
        "balanced-initializers.input.cpp",
    });
    addProfileCase("../call-reflow", "profile-nested");
    addProfileCase("../chained-calls", "profile-multiline-lambda");
    addProfileCase("../chained-calls", "profile-scoped-sticky");
    addProfileCase("../chained-calls", "profile-comments");
    addProfileCase("../chained-calls", "profile-mixed-links");
    addProfileCase("../chained-calls", "profile-capture-body");
    addProfileCase("../chained-calls", "profile-conventional");
    addProfileCase("../keyword-expressions", "profile");
    AddCases(
        cases,
        std::filesystem::path(FORMATTER_TESTDATA_DIR) /
        "../keyword-expressions",
        {"canonical", "preserve", "narrow", "always"});
    const auto google = std::filesystem::path(FORMATTER_TESTDATA_DIR) /
        "../google-style-regressions";
    const auto googleProfile = (std::filesystem::path(FORMATTER_TESTDATA_DIR) /
        "../../../../hit-fmt/examples/google").lexically_normal().string();
    cases.push_back({
        google,
        "profile-trailing-comments",
        "profile-trailing-comments.input.cpp",
        googleProfile,
    });
    cases.push_back({google, "inline-record", "inline-record.input.cpp"});
    cases.push_back({google, "widths", "widths.input.cpp"});
    cases.push_back({google, "operator-spacing", "operator-spacing.input.cpp"});
    cases.push_back({google, "unbraced-switch", "unbraced-switch.input.cpp"});
    cases.push_back(
        {google, "namespace-comments", "namespace-comments.input.cpp"});
    cases.push_back(
        {google, "syntax-boundaries", "syntax-boundaries.input.cpp"});
    cases.push_back({
        google,
        "declarator-and-macro-safety",
        "declarator-and-macro-safety.input.cpp",
    });
    const auto disabled =
        std::filesystem::path(FORMATTER_TESTDATA_DIR) / "../format-disabled";
    cases.push_back({disabled, "regions", "regions.input.cpp"});
    cases.push_back({disabled, "regions-no-clang", "regions.input.cpp"});
    cases.push_back({disabled, "unclosed", "unclosed.input.cpp", "regions"});
    cases.push_back({disabled, "crlf", "crlf.input.cpp", "regions"});
    cases.push_back({
        std::filesystem::path(FORMATTER_TESTDATA_DIR) /
            "../constructor-initializer-ranges",
        "canonical",
        "input.cpp",
    });
    const auto newArrays = std::filesystem::path(FORMATTER_TESTDATA_DIR) /
        "../new-array-declarators";
    AddCases(cases, newArrays, {"left", "array-space", "array-preserve"});
    AddCases(
        cases,
        google,
        {"punctuation-preserve", "punctuation-space", "comments-preserve"},
        "punctuation-preserve.input.cpp");
    cases.push_back({google, "access-only", "access-scope.input.cpp"});
    cases.push_back({google, "scope-only", "access-scope-safe.input.cpp"});
    const auto attribute =
        std::filesystem::path(FORMATTER_TESTDATA_DIR) / "../attribute-indent";
    AddCases(cases, attribute, {"preserve", "block", "indent"});
    cases.push_back({attribute, "block-only", "block-only.input.cpp"});
    cases.push_back(
        {attribute, "independent-widths", "independent-widths.input.cpp"});
    AddCases(
        cases,
        std::filesystem::path(FORMATTER_TESTDATA_DIR) /
        "../assignment-location",
        {"leading", "trailing"});
    AddCases(
        cases,
        std::filesystem::path(FORMATTER_TESTDATA_DIR) /
        "../call-expression-indent",
        {"aligned", "aligned-no-call-reflow", "indent-width", "preserve"});
    AddCases(
        cases,
        INHERITANCE_TESTDATA_DIR,
        {
            "preserve",
            "format",
            "leading",
            "trailing",
            "overflow",
            "comma-leading",
            "no-space",
            "spaces-mixed",
            "combined",
        });
    AddCases(
        cases,
        INHERITANCE_TESTDATA_DIR,
        {
            "split-preserve",
            "one-per-line",
            "pack-wide",
            "pack-tight",
            "split-combined",
        },
        "split.input.cpp");
    for (const auto* name :
        {"parsed-operators", "outer-conditional", "control-braces-after-wrap"})
    {
        cases.push_back({
            std::filesystem::path(FORMATTER_TESTDATA_DIR) / ".." / name,
            "configured",
            "input.cpp",
        });
    }
    cases.push_back({
        std::filesystem::path(FORMATTER_TESTDATA_DIR) /
            "../control-braces-after-wrap",
        "reflow",
        "reflow.input.cpp",
    });
    const auto strings =
        std::filesystem::path(FORMATTER_TESTDATA_DIR) / "../concat-strings";
    AddCases(cases, strings, {"overflow", "preserve"});
    cases.push_back({strings, "crlf", "crlf.input.cpp", "overflow"});
    const auto parsedOperators =
        std::filesystem::path(FORMATTER_TESTDATA_DIR) / "../parsed-operators";
    for (const auto* name : {"placement-lists", "delimiter-choice"}) {
        cases.push_back(
            {parsedOperators, name, std::string(name) + ".input.cpp"});
    }
    // Keep safety and recovered-syntax examples in separate parse units.
    const auto spacing = std::filesystem::path(FORMATTER_TESTDATA_DIR) / ".." /
        "spacing-options";
    cases.push_back({
        spacing,
        "cases-preserved",
        "indented-cases.input.cpp",
        "cases-preserved",
    });
    for (const auto& entry : std::filesystem::directory_iterator(spacing)) {
        const auto filename = entry.path().filename().string();
        if (filename.ends_with(".input.cpp")) {
            const auto name = filename.substr(
                0,
                filename.size() - std::string_view(".input.cpp").size());
            const auto duplicate = std::ranges::any_of(
                cases,
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
    AddCases(cases, data / "continuation-nesting", {"shared", "tabs"});
    AddCases(
        cases,
        data / "continuation-nesting",
        {
            "argument-precedence",
            "argument-precedence-leading",
            "argument-precedence-delimiter",
        },
        "argument-precedence.input.cpp");
    cases.push_back({
        data / "continuation-nesting",
        "starter-lines",
        "starter-lines.input.cpp",
    });
    AddCases(
        cases,
        data / "continuation-nesting",
        {"line-scopes"},
        "line-scopes.input.cpp");
    // Preserve the original owning-source contracts without a second registry.
    for (const auto& [name, owningMaps] :
        {
            std::pair{"layout", true},
        {"structural", true},
        {"comments", true},
        {"enumerator-assignment", true},
        {"opaque-record-owners", true},
        {"control-ownership", true},
        {"control-separators-braced", false},
        {"control-separators-unbraced", false},
        {"control-separators-nested", false},
        {"declaration-operands", true},
        {"declaration-operands-recovery", true},
        {"annotation-boundaries", false},
        {"annotation-constructors", true},
        {"annotation-suffixes", true},
        {"annotation-constraints", true},
        {"terminal-constructor-template-ids", true},
        })
    {
        cases.push_back(
            {data / "public-pipeline", name, std::string(name) + ".input.cpp"});
        cases.back().CheckOwningMaps = owningMaps;
    }
    for (const auto* name : {"nested", "messages"}) {
        cases.push_back(
            {data / "group-syntax", name, std::string(name) + ".input.cpp"});
        cases.back().CheckOwningMaps = true;
    }
    AddCases(
        cases,
        data / "structural-expression-base",
        {"canonical", "preserved"});
    cases.push_back({
        data / "initializer-trailing-comma",
        "conditional-closer",
        "input.cpp",
    });
    AddCases(
        cases,
        data / "function_parameters",
        {"preserve", "pack_name", "pack_new", "one_name", "one_new"});
    AddCases(
        cases,
        data / "namespace_comments",
        {
            "preserve",
            "remove",
            "add_missing",
            "fix",
            "fix_spaced",
            "fix_newline",
        });
    cases
        .push_back({data / "namespace_comments", "crlf_fix", "crlf.input.cpp"});
    cases.push_back({
        data / "namespace_comments",
        "composes-spacing",
        "composes-spacing.input.cpp",
    });
    AddCases(
        cases,
        data / "attribute-parenthesis-interiors",
        {"compact", "spaced", "preserve"});
    AddCases(cases, data / "qualified-record", {"canonical", "ytsaurus"});
    AddCases(cases, data / "unhinted-body-recovery", {"canonical"});
    AddCases(
        cases,
        data / "lambda-argument-ownership",
        {"canonical", "preserve"});
    AddCases(cases, data / "lambda-argument-tail-recovery", {"canonical"});
    const auto hunks = data / "changed-hunks";
    cases.push_back({hunks / "basic", "call", "input.cpp", "style", {{8, 8}}});
    cases.push_back(
        {hunks / "basic", "includes", "input.cpp", "style", {{1, 1}}});
    cases.push_back({
        hunks / "basic",
        "includes-strict",
        "input.cpp",
        "style",
        {{1, 1}},
        false,
    });
    cases.push_back(
        {hunks / "basic", "function", "input.cpp", "style", {{4, 4}}});
    cases.push_back(
        {hunks / "spill", "default", "input.cpp", "style", {{3, 3}, {5, 5}}});
    cases.push_back({
        hunks / "spill",
        "strict",
        "input.cpp",
        "style",
        {{3, 3}, {5, 5}},
        false,
    });
    cases.push_back(
        {hunks / "structural", "unsupported", "input.cpp", "style", {{2, 2}}});
    cases.push_back({
        hunks / "planned",
        "strict-independent",
        "strict-independent.input.cpp",
        "style",
        {{3, 3}, {6, 6}},
        false,
    });
    cases.push_back({
        hunks / "planned",
        "nested",
        "nested.input.cpp",
        "style",
        {{3, 3}},
        true,
    });
    cases.push_back({
        hunks / "planned",
        "atomic",
        "atomic.input.cpp",
        "structural",
        {{2, 5}},
        false,
    });
    for (const auto* name :
        {
            "greedy-trailing",
            "compact-comparison",
            "compact-return",
            "crlf-return",
            "horizontal-and-topology",
            "leading-narrow",
            "call-preserve",
            "ambiguous-if-needed",
        })
    {
        cases.push_back(
            {parsedOperators, name, std::string(name) + ".input.cpp"});
    }
    AddCases(
        cases,
        parsedOperators,
        {"greedy-leading", "preserve"},
        "greedy-trailing.input.cpp");
    AddCases(
        cases,
        parsedOperators,
        {"always-trailing", "always-leading"},
        "leading-narrow.input.cpp");
    AddCases(
        cases,
        parsedOperators,
        {"leading-fitting", "trailing-fitting"},
        "fitting-topologies.input.cpp");
    // Name, input, config override, read-only expectation alias.
    for (const auto& [name, input, config, expected] : {
        std::array<const char*, 4>{"macro-preserve",
            "macro-preserve.input.cpp", "crlf-return", ""},
        {"boundary-comment-tail", "comment-tail.input.cpp", "crlf-return", ""},
        {"recovered-tail", "recovered-tail.input.cpp", "crlf-return", ""},
        {"template-argument-preserve", "template-argument-preserve.input.cpp",
            "crlf-return", ""},
        {"default-preserve", "input.cpp", "", "input.cpp"},
        {"partial-join", "partial-join.input.cpp", "leading-narrow", ""},
        {"always-space", "always-space.input.cpp", "always-trailing", ""},
        {"continuation-comment-tail", "continuation-comment-tail.input.cpp",
            "call-preserve", ""},
        {"crlf", "crlf.input.cpp", "call-preserve", ""},
        {"comparison-if-needed", "comparison-if-needed.input.cpp",
            "ambiguous-if-needed", ""},
        {"ambiguous-always", "ambiguous-if-needed.input.cpp", "", ""},
        {"comparison-always", "comparison-if-needed.input.cpp",
            "ambiguous-always", ""},
        {"trailing-statements", "trailing-statements.input.cpp",
            "return-trailing", ""},
    })
    {
        cases.push_back(
            {parsedOperators, name, input, config, {}, true, expected});
    }
    const auto functionParameters = data / "function_parameters";
    for (const auto* name :
        {"preserve-header", "attached-spacing", "detached-spacing"})
    {
        cases.push_back(
            {functionParameters, name, std::string(name) + ".input.cpp"});
    }
    // Name, input, config override, read-only expectation alias.
    for (const auto& [name, input, config, expected] : {
        std::array<const char*, 4>{"short-next-line",
            "short-preserved-header.input.cpp", "pack_name", ""},
        {"tabs-next-line", "preserve-header.input.cpp", "", ""},
        {"crlf", "crlf.input.cpp", "", "crlf.output.cpp"},
        {"preserved-header-definitions",
            "preserved-header-definitions.input.cpp",
            "reflow-preserved-header", ""},
    })
    {
        cases.push_back(
            {functionParameters, name, input, config, {}, true, expected});
    }
    const auto lambdaAnchors = data / "consolidated-formatter/lambda-anchor";
    cases.push_back(
        {lambdaAnchors, "authored-anchor", "authored-anchor.input.cpp"});
    // Name, input, config override, read-only expectation alias.
    for (const auto& [name, input, config, expected] : {
        std::array<const char*, 4>{"correct-anchor",
            "correct-anchor.input.cpp", "authored-anchor", ""},
        {"narrow-body", "correct-anchor.input.cpp", "", ""},
    })
    {
        cases.push_back(
            {lambdaAnchors, name, input, config, {}, true, expected});
    }
    const auto controlBodies = data / "consolidated-formatter/control-bodies";
    for (const auto* name :
        {"nested-unbraced", "empty-blocks", "empty-blocks-without-add"})
    {
        cases
            .push_back({controlBodies, name, std::string(name) + ".input.cpp"});
    }
    // Name, input, config override, read-only expectation alias.
    for (const auto& [name, input, config, expected] : {
        std::array<const char*, 4>{"crlf", "crlf.input.cpp",
            "comment-barrier", ""},
        {"labeled-body", "labeled-body.input.cpp", "comment-barrier", ""},
        {"complete-control-bodies", "complete-control-bodies.input.cpp",
            "comment-barrier", ""},
    })
    {
        cases.push_back(
            {controlBodies, name, input, config, {}, true, expected});
    }
    for (const auto* name :
        {
            "next-line-reposition",
            "delimiter-alignment",
            "control-header-composition",
            "unsafe-continuation",
            "spaced-vertical",
            "balanced-fill",
            "nested-candidate",
            "opaque-macro",
            "packed",
        })
    {
        cases.push_back({callReflow, name, std::string(name) + ".input.cpp"});
    }
    AddCases(
        cases,
        callReflow,
        {"balanced-vertical", "balanced-preserve"},
        "balanced-fill.input.cpp");
    // Name, input, config override, read-only expectation alias.
    for (const auto& [name, input, config, expected] : {
        std::array<const char*, 4>{"default-preserve", "input.cpp", "",
            "input.cpp"},
        {"always-fill", "long.input.cpp", "", "long-packed.output.cpp"},
        {"vertical-next-line", "input.cpp", "", "one-per-line.output.cpp"},
        {"cast-preserve", "cast-preserve.input.cpp", "unsafe-continuation", ""},
        {"comment-gap", "comment-gap.input.cpp", "unsafe-continuation",
            "comment.output.cpp"},
        {"crlf-call", "crlf-call.input.cpp", "unsafe-continuation",
            "crlf.output.cpp"},
        {"sole-fitting", "sole-fitting.input.cpp", "",
            "sole-fitting.output.cpp"},
        {"sole-separated", "sole-fitting.input.cpp", "",
            "sole-separated.output.cpp"},
        {"lambda-capture", "lambda-capture.input.cpp", "lambda-position", ""},
    })
    {
        cases.push_back({callReflow, name, input, config, {}, true, expected});
    }
    const auto ternaryLayout = data / "ternary-layout";
    cases.push_back({
        ternaryLayout,
        "preserve-always-narrow",
        "preserve-always-narrow.input.cpp",
    });
    AddCases(
        cases,
        ternaryLayout,
        {"preserve-always-trailing", "preserve-authored-flexible"},
        "authored-fitting.input.cpp");
    // Name, input, config override, read-only expectation alias.
    for (const auto& [name, input, config, expected] : {
        std::array<const char*, 4>{"two-leading", "input.cpp", "",
            "two-leading.output.cpp"},
        {"three-leading", "input.cpp", "", "three-leading.output.cpp"},
        {"preserve-leading", "input.cpp", "", "input.cpp"},
        {"fitting-leading", "fitting-leading.input.cpp", "three-leading", ""},
        {"two-trailing", "input.cpp", "", "two-trailing.output.cpp"},
        {"three-trailing", "input.cpp", "", "three-trailing.output.cpp"},
        {"preserve-trailing", "input.cpp", "", "input.cpp"},
        {"fitting-trailing", "fitting-leading.input.cpp", "three-trailing", ""},
        {"always-leading", "authored-fitting.input.cpp", "",
            "three-leading.output.cpp"},
        {"always-trailing", "authored-fitting.input.cpp", "",
            "three-trailing.output.cpp"},
        {"crlf-fitting", "crlf-fitting.input.cpp", "flat-trailing-fitting", ""},
        {"authored-ternary-topologies",
            "authored-ternary-topologies.input.cpp", "authored-fitting", ""},
        {"fitting-three-part", "fitting-three-part.input.cpp",
            "three-part-fitting", ""},
        {"trailing-ternary-topologies",
            "trailing-ternary-topologies.input.cpp", "flat-trailing-fitting",
            ""},
    })
    {
        cases.push_back(
            {ternaryLayout, name, input, config, {}, true, expected});
    }
    const auto emptyFunctions = data / "consolidated-formatter/empty-functions";
    cases.push_back({
        emptyFunctions,
        "empty-function-bodies",
        "empty-function-bodies.input.cpp",
        "whitespace-only",
    });
    const auto consolidatedLayout = data / "consolidated-layout";
    for (const auto* name :
        {
            "comment-directives",
            "comment-external",
            "comment-interiors",
            "comment-mixed-runs",
            "comment-sequence",
            "control-new-line",
            "control-preserved-body",
            "function-try",
            "preserve-crlf-recovery",
            "record-head-boundaries",
        })
    {
        cases.push_back(
            {consolidatedLayout, name, std::string(name) + ".input.cpp"});
    }
    AddCases(
        cases,
        consolidatedLayout,
        {"control-attached", "control-header-broken"},
        "control-arms.input.cpp");
    AddCases(
        cases,
        consolidatedLayout,
        {"control-compact", "control-forced-header"},
        "control-new-line.input.cpp");
    AddCases(
        cases,
        consolidatedLayout,
        {
            "record-head-generated-no-space-canonical",
            "record-head-generated-no-space-preserved",
            "record-head-generated-space-canonical",
            "record-head-generated-space-preserved",
            "record-head-no-space-canonical",
            "record-head-no-space-preserved",
            "record-head-preserve-canonical",
            "record-head-preserved",
            "record-head-space-canonical",
            "record-head-space-preserved",
        },
        "record-head-policies.input.cpp");
    // Name, input, config override, read-only expectation alias.
    for (const auto& [name, input, config, expected] : {
        std::array<const char*, 4>{"comment-namespace-only",
            "comment-external.input.cpp", "", ""},
        {"comment-sequence-zero", "comment-sequence-zero.input.cpp",
            "../standalone-comment-gaps/zero", ""},
        {"control-commented-else", "control-commented-else.input.cpp",
            "control-new-line", ""},
        {"control-unbraced-else", "control-unbraced-else.input.cpp",
            "control-new-line", ""},
        {"control-unbraced-if", "control-unbraced-if.input.cpp",
            "control-new-line", ""},
    })
    {
        cases.push_back(
            {consolidatedLayout, name, input, config, {}, true, expected});
    }
    AddCases(
        cases,
        data / "consolidated-pipeline/enumerator-assignment",
        {
            "space",
            "no-space",
            "preserve",
            "generated-space",
            "generated-no-space",
        });
    cases.push_back({
        data / "consolidated-pipeline",
        "enumerator-assignment-comments",
        "enumerator-assignment-comments.input.cpp",
    });
    cases.push_back({
        data / "consolidated-edits",
        "null-removal",
        "null-removal.input.cpp",
    });
    std::ranges::sort(
        cases,
        {},
        [] (const auto& value) {
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
        const auto expected = value.Expected.empty()
            ? fixture.Named(".output.cpp")
            : fixture.Directory / value.Expected;
        ExpectFormattedGolden(expected, input, style,
            value.CheckOwningMaps, !value.Expected.empty());
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
INSTANTIATE_TEST_SUITE_P(
    Formatter,
    FormatTest,
    testing::ValuesIn(FormatCases()),
    [] (const auto& info) {
        const auto root = (std::filesystem::path(FORMATTER_TESTDATA_DIR) / "..")
            .lexically_normal();
        auto name =
            (info.param.Directory.lexically_normal().lexically_relative(root) /
                info.param.Name).string();
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
INSTANTIATE_TEST_SUITE_P(
    Formatter,
    ParameterNameTest,
    testing::Values("enabled", "default", "declarations_only",
        "definitions_only", "names_only", "ytsaurus"),
    ConfigName);
TEST(Formatter, CallArgument_RejectsUnsupportedSyntaxHints)
{
    const test::golden::Fixture fixture{
        std::filesystem::path(FORMATTER_TESTDATA_DIR) / "../call-like-macro",
        "selected"};
    const auto style = formatter::LoadConfigFile(
        fixture.Named(".hit-fmt")).FormattingStyle;
    const auto input = fixture.Input();
    std::string storage(input);
    storage.append(util::SourcePaddingSize, '\0');
    const util::PaddedSourceView source{.Data = storage.data(), .Size = input.size()};
    const auto tree = parser::Parse(source, [] (parser::ParseError) { });
    for (bool parsed : {false, true}) {
        SCOPED_TRACE(parsed);
        try {
            const auto output = parsed ? formatter::Format(tree, style)
                                       : formatter::Format(source, style);
            FAIL() << "unsupported syntax hints produced output: " << output;
        } catch (const std::invalid_argument& error) {
            EXPECT_EQ(std::string_view(error.what()),
                "CallLikeMacroNames syntax hints are not yet supported by the public pipeline");
        }
    }
}
TEST(Formatter, CallArgument_RequiresOwnedBreaksAcrossArities)
{
    formatter::Style style;
    style.CallsLayout = formatter::CategoryLayout::Canonical;
    style.CallsPacking = formatter::ListPacking::Fill;
    style.ColumnLimit = 100;
    const test::golden::Fixture fixture{
        std::filesystem::path(FORMATTER_TESTDATA_DIR) / "../macro-nested-calls",
        "configured"};
    const auto input = fixture.Input();
    style.CallsBreak = formatter::BreakRequirement::IfNeeded;
    const auto adaptive = FormatWithStyle(input, style);
    EXPECT_EQ(FormatWithStyle(adaptive, style), adaptive);
    // Always is an owned-break requirement, not another activation spelling.
    style.CallsBreak = formatter::BreakRequirement::Always;
    const auto required = FormatWithStyle(input, style);
    EXPECT_NE(required, adaptive);
    EXPECT_EQ(FormatWithStyle(required, style), required);
    // First-item placement is shared across arities; a fitting singleton must
    // still supply its own boundary under Always. Empty calls cannot do so.
    style.CallsFirstItem = formatter::FirstItemPlacement::NextLine;
    for (const auto* call : {"Probe(value)", "Probe(first, second)"}) {
        SCOPED_TRACE(call);
        const auto source = std::string("void f() { ") + call + "; }\n";
        const auto broken = FormatWithStyle(source, style);
        EXPECT_NE(broken.find("Probe(\n"), std::string::npos);
        EXPECT_EQ(FormatWithStyle(broken, style), broken);
        style.CallsLayout = formatter::CategoryLayout::Preserve;
        ExpectUnchanged(source, style);
        style.CallsLayout = formatter::CategoryLayout::Canonical;
    }
    ExpectUnchanged("void f() { Probe(); }\n", style);
}

TEST(Formatter, ExpressionLayout_ChecksPipelineMismatchAndEmittedWidth)
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
    const auto length = storage.size();
    storage.append(util::SourcePaddingSize, '\0');
    const auto tree = parser::Parse(
        {.Data = storage.data(), .Size = length},
        [] (parser::ParseError) { });
    formatter::Style style;
    style.ColumnLimit = 40;
    style.WidthDiagnostics = formatter::WidthDiagnosticPolicy::Report;
    // Width belongs to the emitted result, including unavoidable atoms.
    style.ExpressionsLayout = formatter::CategoryLayout::Canonical;
    style.ExpressionsBreak = formatter::BreakRequirement::Always;
    style.BinaryOperatorSide = formatter::OperatorSide::Trailing;
    style.TernaryOperatorSide = formatter::OperatorSide::Leading;
    style.TernaryLayout = formatter::TernaryLayout::ThreePart;
    const auto result = formatter::Format(tree, formatter::PreparedFormat(
        style));
    EXPECT_NE(result.Text, input);
    EXPECT_NE(result.Text.find("left +\n"), std::string::npos);
    EXPECT_NE(result.Text.find("\n        ? alphaValue\n"), std::string::npos);
    EXPECT_NE(result.Text.find("\n        : betaValue"), std::string::npos);
    // The selected layout fits: source overflow must not leak into diagnostics.
    EXPECT_TRUE(result.Diagnostics.empty());
    style.ExpressionsLayout = formatter::CategoryLayout::Preserve;
    const auto preserved = formatter::Format(
        tree, formatter::PreparedFormat(style));
    EXPECT_EQ(preserved.Text, input);
    ASSERT_EQ(preserved.Diagnostics.size(), 1u);
    EXPECT_EQ(preserved.Diagnostics[0].Position.Line, 2u);
    EXPECT_EQ(preserved.Diagnostics[0].Kind,
        formatter::LayoutDiagnosticKind::LineWidth);
    EXPECT_NE(preserved.Diagnostics[0].Message.find("exceeds ColumnLimit"),
        std::string::npos);
    style.WidthDiagnostics = formatter::WidthDiagnosticPolicy::Off;
    const auto quiet = formatter::Format(tree, formatter::PreparedFormat(
        style));
    EXPECT_EQ(quiet.Text, input);
    EXPECT_TRUE(quiet.Diagnostics.empty());
    style.ExpressionsLayout = formatter::CategoryLayout::Canonical;
    const auto mismatchOnly = formatter::Format(
        tree, formatter::PreparedFormat(style));
    EXPECT_EQ(mismatchOnly.Text, result.Text);
    EXPECT_TRUE(mismatchOnly.Diagnostics.empty());
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
        style.WidthDiagnostics = formatter::WidthDiagnosticPolicy::Report;
    }
    // Public diagnostics describe emitted widths, not source-layout
    // feasibility.
    std::string actual;
    const auto result = formatter::Format(tree, formatter::PreparedFormat(
        style));
    for (const auto& diagnostic : result.Diagnostics)
    {
        actual += std::to_string(diagnostic.Position.Line) + ":" +
            std::to_string(diagnostic.Position.Column) + ": " +
            diagnostic.Message + "\n";
    }
    test::golden::ExpectGolden(
        directory / (value.Name + value.Suffix), actual);
}
// Check the emitted text independently of the diagnostic golden. Public
// formatting uses eight-column tab stops; CRLF is one physical terminator.
void ExpectEmittedWidthDiagnostics(
    const formatter::FormatResult& result,
    const formatter::Style& style)
{
    if (style.WidthDiagnostics == formatter::WidthDiagnosticPolicy::Off) {
        EXPECT_TRUE(result.Diagnostics.empty());
        return;
    }
    const auto columnLimit = style.ColumnLimit;
    std::vector<std::pair<uint32_t, size_t>> overflows;
    const std::string_view text = result.Text;
    uint32_t line = 1;
    size_t begin = 0;
    while (begin < text.size()) {
        auto end = text.find_first_of("\r\n", begin);
        if (end == std::string_view::npos)
            end = text.size();
        size_t width = 0;
        for (const char c : text.substr(begin, end - begin)) {
            width = c == '\t' ? (width / 8 + 1) * 8 : width + 1;
        }
        if (width > columnLimit) {
            overflows.emplace_back(line, width);
        }
        begin = end;
        if (begin < text.size() && text[begin++] == '\r' &&
            begin < text.size() && text[begin] == '\n')
        {
            ++begin;
        }
        ++line;
    }
    ASSERT_EQ(result.Diagnostics.size(), overflows.size());
    for (size_t i = 0; i < overflows.size(); ++i)
    {
        SCOPED_TRACE(i);
        const auto& diagnostic = result.Diagnostics[i];
        EXPECT_EQ(diagnostic.Kind, formatter::LayoutDiagnosticKind::LineWidth);
        EXPECT_EQ(diagnostic.Position.Line, overflows[i].first);
        EXPECT_EQ(diagnostic.Position.Column, columnLimit + 1);
        EXPECT_EQ(
            diagnostic.Message,
            "emitted line width " + std::to_string(overflows[i].second) +
            " exceeds ColumnLimit " + std::to_string(columnLimit));
    }
}
TEST_P(LayoutDiagnosticTest, ReportsEmittedWidths)
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
    if (value.StrictLength)
        style.WidthDiagnostics = formatter::WidthDiagnosticPolicy::Report;
    const auto result = formatter::Format(tree, formatter::PreparedFormat(style));
    ExpectEmittedWidthDiagnostics(result, style);
}
TEST(Formatter, WidthDiagnostics_CountsProtectedTabsAndMixedTerminators)
{
    const std::string input = "// hit-fmt off\r\nint\tlong_name;\r\n"
        "\tint long_name;\n// hit-fmt on\n";
    auto source = input;
    source.append(util::SourcePaddingSize, '\0');
    const auto tree = parser::Parse(
        {.Data = source.data(), .Size = input.size()},
        [] (parser::ParseError) { });
    formatter::Style style;
    style.ColumnLimit = 16;
    style.WidthDiagnostics = formatter::WidthDiagnosticPolicy::Report;
    style.TrailingWhitespace = formatter::TrailingWhitespace::Trim;
    const auto result = formatter::Format(tree, formatter::PreparedFormat(style));
    EXPECT_EQ(result.Text, input);
    ASSERT_EQ(result.Diagnostics.size(), 2u);
    EXPECT_EQ(result.Diagnostics[0].Position.Line, 2u);
    EXPECT_EQ(result.Diagnostics[0].Message,
        "emitted line width 18 exceeds ColumnLimit 16");
    EXPECT_EQ(result.Diagnostics[1].Position.Line, 3u);
    EXPECT_EQ(result.Diagnostics[1].Message,
        "emitted line width 22 exceeds ColumnLimit 16");
    ExpectEmittedWidthDiagnostics(result, style);
    EXPECT_EQ(FormatWithStyle(result.Text, style), result.Text);
}
INSTANTIATE_TEST_SUITE_P(Formatter, LayoutDiagnosticTest,
    testing::Values(
        LayoutDiagnosticCase{"format-disabled", "regions",
            "regions.input.cpp", {}, false, ".layout.expected"},
        LayoutDiagnosticCase{"format-disabled", "regions-no-clang",
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
TEST(Formatter, ExpressionLayout_ReportsUnavoidableWidthWithoutReflowing)
{
    const auto directory = std::filesystem::path(FORMATTER_TESTDATA_DIR) /
        "../line-length";
    const auto input = ReadFile(directory / "input.cpp");
    auto source = input;
    source.append(util::SourcePaddingSize, '\0');
    const auto tree = parser::Parse(
        {.Data = source.data(), .Size = input.size()},
        [] (parser::ParseError) { });
    auto style = formatter::LoadConfigFile(
        directory / "soft.hit-fmt").FormattingStyle;
    style.ExpressionsLayout = formatter::CategoryLayout::Preserve;
    style.WidthDiagnostics = formatter::WidthDiagnosticPolicy::Report;
    const auto reported = formatter::Format(tree, formatter::PreparedFormat(
        style));
    EXPECT_EQ(reported.Text, input);
    ASSERT_EQ(reported.Diagnostics.size(), 4u);
    for (const auto& diagnostic : reported.Diagnostics) {
        EXPECT_EQ(diagnostic.Kind, formatter::LayoutDiagnosticKind::LineWidth);
    }
    // Report includes unavoidable overflow, not just fixable layout failures.
    style.WidthDiagnostics = formatter::WidthDiagnosticPolicy::Off;
    const auto quiet = formatter::Format(tree, formatter::PreparedFormat(
        style));
    EXPECT_EQ(quiet.Text, input);
    EXPECT_TRUE(quiet.Diagnostics.empty());
}
TEST(Formatter, OperatorSpacing_PreservesUnknownCastOrBinaryOperator)
{
    formatter::Style style;
    style.BinaryOperatorSpacing = formatter::GapPolicy::Space;
    style.CastSpacing = formatter::GapPolicy::NoSpace;
    style.UnaryOperatorSpacing = formatter::GapPolicy::NoSpace;
    style.ExpressionsLayout = formatter::CategoryLayout::Canonical;
    style.ExpressionsBreak = formatter::BreakRequirement::Always;
    style.ColumnLimit = 120;
    const std::string source =
        "void f(int a, int b) {\n"
        "    auto address = (UnknownType) &a;\n"
        "    auto pointer = (UnknownType)*b;\n"
        "    auto bitwise = (a)& b;\n"
        "    auto multiply = (a)* b;\n"
        "    auto qualified = (NS::UnknownType) &a;\n"
        "}\n";
    // Canonical Always cannot manufacture authority for ambiguous operators.
    ExpectUnchanged(source, style);
    // Certain expressions remain editable. Test their horizontal policies
    // without requiring canonical Always to retain their original topology.
    style.ExpressionsLayout = formatter::CategoryLayout::Preserve;
    const std::string certain =
        "void f(int a, int b) {\n"
        "    auto certain = (a + b) & b;\n"
        "    auto knownCast = (int)&a;\n"
        "}\n";
    ExpectUnchanged(certain, style);
    const auto compact = std::string(certain).replace(
        certain.find("(a + b) & b"),
        std::string("(a + b) & b").size(),
        "(a + b)&b");
    EXPECT_EQ(FormatWithStyle(compact, style), certain);
    const auto separatedCast = std::string(certain).replace(
        certain.find("(int)&a"),
        std::string("(int)&a").size(),
        "(int) & a");
    EXPECT_EQ(FormatWithStyle(separatedCast, style), certain);
}
TEST(Formatter, SpacingOptions_ProfileOverflowReportsWidthAndKeepsTokens)
{
    const auto directory = std::filesystem::path(FORMATTER_TESTDATA_DIR) /
        "../spacing-options";
    const auto input = ReadFile(directory / "profile-overflow.input.cpp");
    const auto config = formatter::LoadConfigFile(
        (directory / "../../../../hit-fmt/examples/ytsaurus.hit-fmt")
            .lexically_normal()).FormattingStyle;
    std::string storage(input);
    storage.append(util::SourcePaddingSize, '\0');
    const auto tree = parser::Parse(
        {.Data = storage.data(), .Size = input.size()},
        [] (parser::ParseError) { });
    const auto result = formatter::Format(tree, formatter::PreparedFormat(config));
    // Width is a soft limit, not a promise to lower unsupported declaration
    // internals. A retained overflowing static_assert must still be diagnosed.
    const auto staticAssert = result.Text.find("    static_assert(!");
    ASSERT_NE(staticAssert, std::string::npos);
    const auto line = std::count(result.Text.begin(),
        result.Text.begin() + staticAssert, '\n') + 1;
    EXPECT_TRUE(std::ranges::any_of(result.Diagnostics, [&] (const auto& diagnostic) {
        return diagnostic.Kind == formatter::LayoutDiagnosticKind::LineWidth &&
            diagnostic.Position.Line == line &&
            diagnostic.Position.Column == config.ColumnLimit + 1;
    }));
    std::string output(result.Text);
    output.append(util::SourcePaddingSize, '\0');
    const auto tokens = parser::SyntaxTree::TokenizeOnly(
        {.Data = output.data(), .Size = result.Text.size()});
    ASSERT_EQ(tokens.TokenCount(), tree.TokenCount());
    for (parser::TokenIndex i = 0; i < tree.TokenCount(); ++i) {
        EXPECT_EQ(tokens.Kind(i), tree.Kind(i));
        EXPECT_EQ(tokens.TokenText(i), tree.TokenText(i));
    }
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
INSTANTIATE_TEST_SUITE_P(
    Formatter,
    NamingTest,
    testing::Values("rules", "alternate", "no_exempt"),
    ConfigName);
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
         "ExpressionsLayout = Canonical\nExpressionsBreak = IfNeeded\n"
         "BinaryOperatorSide = Trailing\nAssignmentOperatorSide = Trailing\n"
         "StatementAttributeIndent = Block\n"
         "AccessModifierIndentWidth = 1\nMemberIndentWidth = 3\n"
         "BlockBodyIndentWidth = 4\nCaseLabelIndentWidth = 1\n"
         "StatementAttributeIndentWidth = 2\nNamespaceCommentSpaces = 2\n"
         "CommaSpaceBefore = NoSpace\nCommaSpaceAfter = Space\n"
         "TernaryOperatorSide = Trailing\nTernaryLayout = ThreePart\n"
         "CallsLayout = Canonical\nCallsPacking = Fill\n"
         "CallsFirstItem = NextLine\nCallsAlignment = Delimiter\n"
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
    const auto& style = config.FormattingStyle;
    EXPECT_EQ(style.IndentWidth, 3u);
    EXPECT_EQ(style.ColumnLimit, 40u);
    EXPECT_EQ(style.ExpressionsLayout, formatter::CategoryLayout::Canonical);
    EXPECT_EQ(style.ExpressionsBreak, formatter::BreakRequirement::IfNeeded);
    EXPECT_EQ(style.BinaryOperatorSide, formatter::OperatorSide::Trailing);
    EXPECT_EQ(style.AssignmentOperatorSide, formatter::OperatorSide::Trailing);
    EXPECT_EQ(style.StatementAttributeIndent, formatter::
        StatementAttributeIndent::Block);
    EXPECT_EQ(style.AccessModifierIndentWidth, 1u);
    EXPECT_EQ(style.MemberIndentWidth, 3u);
    EXPECT_EQ(style.BlockBodyIndentWidth, 4u);
    EXPECT_EQ(style.CaseLabelIndentWidth, 1u);
    EXPECT_EQ(style.StatementAttributeIndentWidth, 2u);
    EXPECT_EQ(style.NamespaceCommentSpaces, 2u);
    EXPECT_EQ(style.CommaSpaceBefore, formatter::GapPolicy::NoSpace);
    EXPECT_EQ(style.CommaSpaceAfter, formatter::GapPolicy::Space);
    EXPECT_EQ(style.TernaryOperatorSide, formatter::OperatorSide::Trailing);
    EXPECT_EQ(style.TernaryLayout, formatter::TernaryLayout::ThreePart);
    EXPECT_EQ(style.CallsLayout, formatter::CategoryLayout::Canonical);
    EXPECT_EQ(style.CallsPacking, formatter::ListPacking::Fill);
    EXPECT_EQ(style.CallsFirstItem, formatter::FirstItemPlacement::NextLine);
    EXPECT_EQ(style.CallsAlignment, formatter::ContinuationAlignment::
        Delimiter);
    EXPECT_EQ(style.CallLineFirstStringNames,
        (std::vector<std::string>{"YT_LOG_INFO", "YT_LOG_ERROR"}));
    EXPECT_EQ(style.IndentedCallScopePairs,
        (std::vector<std::string>{"OnBeginList:OnEndList"}));
    ASSERT_EQ(config.Exclude.size(), 2u);
    EXPECT_TRUE(std::regex_search("x]y", config.Exclude[0]));
    EXPECT_TRUE(std::regex_search("generated/source.cpp", config.Exclude[1]));
    ASSERT_TRUE(config.Naming.Class.Prefixes.has_value());
    EXPECT_EQ(*config.Naming.Class.Prefixes,
        (std::vector<std::string>{"S", "C"}));
    save("ClassNameCase = Unchecked\nClassNamePrefixes = []\n"
         "BaseListsContinuationOffset = 0\n"
         "ConstructorListsContinuationOffset = 0\nShortNamespaceLines = 0\n");
    const auto disabled = formatter::LoadConfigFile(path);
    EXPECT_EQ(disabled.Naming.Class.Case, formatter::NameCase::Unchecked);
    EXPECT_FALSE(disabled.Naming.Class.Prefixes.has_value());
    const auto policies = formatter::CompilePolicies(disabled.FormattingStyle);
    EXPECT_EQ(policies.BaseLists.Items.Indent.ContinuationOffset,
        disabled.FormattingStyle.IndentWidth);
    EXPECT_EQ(policies.ConstructorLists.Items.Indent.ContinuationOffset,
        disabled.FormattingStyle.IndentWidth);
    EXPECT_EQ(disabled.FormattingStyle.ShortNamespaceLines, 0u);
    save("CallsLayout = Canonical\nCallsPacking = Fill\nCallsBreak = Always\n");
    const auto alternate = formatter::LoadConfigFile(path).FormattingStyle;
    EXPECT_EQ(alternate.CallsLayout, formatter::CategoryLayout::Canonical);
    EXPECT_EQ(alternate.CallsBreak, formatter::BreakRequirement::Always);
    save("ExpressionsLayout = Preserve\nExpressionsBreak = Always\n"
         "TernaryOperatorSide = Leading\nTernaryLayout = Flexible\n");
    const auto preserving = formatter::LoadConfigFile(path).FormattingStyle;
    EXPECT_EQ(preserving.ExpressionsLayout, formatter::CategoryLayout::
        Preserve);
    EXPECT_EQ(preserving.TernaryOperatorSide, formatter::OperatorSide::Leading);
    EXPECT_EQ(preserving.TernaryLayout, formatter::TernaryLayout::Flexible);
    const auto preserved = formatter::CompilePolicies(preserving);
    EXPECT_EQ(preserved.Expressions.Layout, formatter::LayoutMode::Preserve);
    EXPECT_EQ(preserved.Expressions.Break, formatter::BreakRequirement::Always);
    std::filesystem::remove(path);
}
} // namespace
