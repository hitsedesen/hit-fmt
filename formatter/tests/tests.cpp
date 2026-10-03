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
        if (
            filename.ends_with(".input.cpp") ||
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
    FormatCase(
        std::filesystem::path directory,
        std::string name,
        std::string input,
        std::string config = {},
        std::vector<formatter::Hunk> hunks = {},
        bool spillover =
            true)
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
    const auto root =
        (std::filesystem::path(FORMATTER_TESTDATA_DIR) / "..")
            .lexically_normal();
    *output
        << value.Directory.lexically_normal().lexically_relative(root)
            .generic_string() << '/' << value.Name;
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
    const auto llvmProfile =
        (std::filesystem::path(FORMATTER_TESTDATA_DIR) /
                    "../../../../hit-fmt/examples/llvm").lexically_normal()
            .string();
    for (const auto* name : {"lf", "opaque", "no-final-newline"}) {
        cases
            .push_back(
                {
                    std::filesystem::path(FORMATTER_TESTDATA_DIR) /
                        "../example-llvm",
                    name,
                    std::string(name) + ".input.cpp",
                    llvmProfile,
                });
    }
    // Bind to the root configuration, not a fixture-local copy of its settings.
    for (const auto* name : {"repository-lf", "repository-crlf"}) {
        cases
            .push_back(
                {
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
        cases
            .push_back(
                {
                    std::filesystem::path(FORMATTER_TESTDATA_DIR) /
                        "../line-endings",
                    name,
                    std::string(name) + ".input.cpp",
                });
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
    cases
        .push_back(
            {
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
    cases
        .push_back(
            {
                std::filesystem::path(FORMATTER_TESTDATA_DIR) /
                    "../include-order",
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
    cases
        .push_back(
            {
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
    cases
        .push_back(
            {
                CALL_INNER_SPACING_TESTDATA_DIR,
                "reflow-preferred",
                "reflow-preferred.input.cpp",
            });
    cases
        .push_back(
            {
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
    cases
        .push_back(
            {
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
        cases
            .push_back(
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
    cases.push_back({chains, "structural-base", "structural-base.input.cpp"});
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
        cases
            .push_back(
                {
                    recordHead,
                    std::string("comparisons-") + profile,
                    "comparisons.input.cpp",
                    profile,
                });
    }
    for (const auto* profile : {"space", "generated-preserve"}) {
        cases
            .push_back(
                {
                    recordHead,
                    std::string("constructor-composition-") + profile,
                    "constructor-composition.input.cpp",
                    profile,
                });
    }
    cases
        .push_back(
            {closings, "reflow-preferred", "reflow-preferred.input.cpp"});
    cases
        .push_back({closings, "generated-block", "generated-block.input.cpp"});
    cases
        .push_back(
            {closings, "layout-preferred", "layout-preferred.input.cpp"});
    cases.push_back({closings, "new-line", "new-line.input.cpp"});
    cases
        .push_back(
            {closings, "conditional-preferred", "trailing-comma.input.cpp"});
    cases.push_back({closings, "remove", "trailing-comma.input.cpp"});
    cases.push_back({closings, "trailing-add", "trailing-layout.input.cpp"});
    cases
        .push_back(
            {closings, "trailing-conditional", "trailing-layout.input.cpp"});
    cases
        .push_back(
            {closings, "trailing-preferred", "trailing-preferred.input.cpp"});
    cases
        .push_back(
            {closings, "trailing-removed", "trailing-removed.input.cpp"});
    cases.push_back({closings, "trailing-only", "trailing-layout.input.cpp"});
    cases
        .push_back(
            {closings, "trailing-new-line", "trailing-layout.input.cpp"});
    AddCases(
        cases,
        std::filesystem::path(FORMATTER_TESTDATA_DIR) / "../fold-spacing",
        {"yt-like", "inherited", "group-space"});
    const auto profile =
        (std::filesystem::path(FORMATTER_TESTDATA_DIR) /
                    "../../../../hit-fmt/examples/ytsaurus").lexically_normal()
            .string();
    auto addProfileCase = [&] (const char* directory, const char* name) {
        cases
            .push_back(
                {
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
    addProfileCase("../spacing-options", "profile-initializers");
    addProfileCase("../spacing-options", "profile-overflow");
    addProfileCase("../spacing-options", "profile-case-blocks");
    cases
        .push_back(
            {
                std::filesystem::path(FORMATTER_TESTDATA_DIR) /
                    "../spacing-options",
                "initializer-overflow-one-per-line",
                "initializer-overflow-one-per-line.input.cpp",
            });
    addProfileCase("../ternary-layout", "profile");
    cases
        .push_back(
            {
                std::filesystem::path(FORMATTER_TESTDATA_DIR) /
                    "../ternary-layout",
                "reflow-preferred",
                "reflow-preferred.input.cpp",
            });
    addProfileCase("../assignment-location", "profile");
    addProfileCase("../function_parameters", "profile");
    addProfileCase("../body-indent", "profile-control-local");
    addProfileCase("../body-indent", "profile-direct-initializer");
    cases
        .push_back(
            {
                std::filesystem::path(FORMATTER_TESTDATA_DIR) /
                    "../null-statements",
                "profile",
                "input.cpp",
                profile,
            });
    cases
        .push_back(
            {
                BODY_INDENT_TESTDATA_DIR,
                "grouped-profile",
                "grouped-statements.input.cpp",
                profile,
            });
    cases
        .push_back(
            {
                BODY_INDENT_TESTDATA_DIR,
                "lambda-comments-profile",
                "lambda-leading-comments.input.cpp",
                profile,
            });
    cases
        .push_back(
            {
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
    cases
        .push_back({callReflow, "sole-preferred", "sole-preferred.input.cpp"});
    cases
        .push_back(
            {
                callReflow,
                "first-position-reflow",
                "first-position-reflow.input.cpp",
            });
    cases
        .push_back(
            {
                callReflow,
                "first-string-inline",
                "first-string-inline.input.cpp",
            });
    cases
        .push_back(
            {
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
    const auto controlPlacement =
        std::filesystem::path(FORMATTER_TESTDATA_DIR) /
            "../control-header-placement";
    AddCases(
        cases,
        controlPlacement,
        {"preserve", "first-only", "closing-only", "both"});
    cases.push_back({controlPlacement, "reflow", "reflow.input.cpp"});
    cases.push_back({controlPlacement, "reverse", "reverse.input.cpp"});
    cases.push_back({controlPlacement, "crlf", "crlf.input.cpp"});
    cases
        .push_back(
            {
                std::filesystem::path(FORMATTER_TESTDATA_DIR) /
                    "../lambda-body",
                "profile",
                "input.cpp",
                profile,
            });
    addProfileCase("../control-header-clauses", "profile");
    addProfileCase("../whitespace-sensitive-macros", "profile");
    addProfileCase("../call-reflow", "balanced-expressions");
    addProfileCase("../call-reflow", "nested-operator-choice");
    cases
        .push_back(
            {
                callReflow,
                "balanced-initializers",
                "balanced-initializers.input.cpp",
            });
    addProfileCase("../call-reflow", "profile-nested");
    addProfileCase("../chained-calls", "profile-multiline-lambda");
    const auto google =
        std::filesystem::path(FORMATTER_TESTDATA_DIR) /
            "../google-style-regressions";
    const auto googleProfile =
        (std::filesystem::path(FORMATTER_TESTDATA_DIR) /
                    "../../../../hit-fmt/examples/google").lexically_normal()
            .string();
    cases
        .push_back(
            {
                google,
                "profile-trailing-comments",
                "profile-trailing-comments.input.cpp",
                googleProfile,
            });
    cases.push_back({google, "inline-record", "inline-record.input.cpp"});
    cases.push_back({google, "widths", "widths.input.cpp"});
    cases
        .push_back({google, "operator-spacing", "operator-spacing.input.cpp"});
    cases.push_back({google, "unbraced-switch", "unbraced-switch.input.cpp"});
    cases
        .push_back(
            {google, "namespace-comments", "namespace-comments.input.cpp"});
    cases
        .push_back(
            {google, "syntax-boundaries", "syntax-boundaries.input.cpp"});
    cases
        .push_back(
            {
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
    cases
        .push_back(
            {
                std::filesystem::path(FORMATTER_TESTDATA_DIR) /
                    "../constructor-initializer-ranges",
                "canonical",
                "input.cpp",
            });
    const auto newArrays =
        std::filesystem::path(FORMATTER_TESTDATA_DIR) /
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
    cases
        .push_back(
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
        cases
            .push_back(
                {
                    std::filesystem::path(FORMATTER_TESTDATA_DIR) / ".." / name,
                    "configured",
                    "input.cpp",
                });
    }
    cases
        .push_back(
            {
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
        cases
            .push_back(
                {parsedOperators, name, std::string(name) + ".input.cpp"});
    }
    // Keep safety and recovered-syntax examples in separate parse units.
    const auto spacing =
        std::filesystem::path(FORMATTER_TESTDATA_DIR) / ".." /
            "spacing-options";
    cases
        .push_back(
            {
                spacing,
                "cases-preserved",
                "indented-cases.input.cpp",
                "cases-preserved",
            });
    for (const auto& entry : std::filesystem::directory_iterator(spacing)) {
        const auto filename = entry.path().filename().string();
        if (filename.ends_with(".input.cpp")) {
            const auto name =
                filename
                    .substr(
                        0,
                        filename.size() -
                            std::string_view(".input.cpp").size());
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
    for (const auto* name :
        {
            "layout",
            "structural",
            "comments",
            "enumerator-assignment",
            "opaque-record-owners",
            "control-ownership",
            "control-separators-braced",
            "control-separators-unbraced",
            "control-separators-nested",
            "declaration-operands",
            "declaration-operands-recovery",
            "annotation-boundaries",
            "annotation-constructors",
            "annotation-suffixes",
            "annotation-constraints",
            "terminal-constructor-template-ids",
        })
    {
        cases
            .push_back(
                {
                    data / "public-pipeline",
                    name,
                    std::string(name) + ".input.cpp",
                });
    }
    for (const auto* name : {"nested", "messages"}) {
        cases
            .push_back(
                {
                    data / "group-syntax",
                    name,
                    std::string(name) + ".input.cpp",
                });
    }
    AddCases(
        cases,
        data / "structural-expression-base",
        {"canonical", "preserved"});
    cases
        .push_back(
            {
                data / "initializer-trailing-comma",
                "conditional-closer",
                "input.cpp",
            });
    AddCases(
        cases,
        data / "function_parameters",
        {
            "preserve",
            "pack_name",
            "pack_new",
            "one_name",
            "one_new",
            "pack_merge",
        });
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
        .push_back(
            {data / "namespace_comments", "crlf_fix", "crlf.input.cpp"});
    cases
        .push_back(
            {
                data / "namespace_comments",
                "composes-spacing",
                "composes-spacing.input.cpp",
            });
    AddCases(cases, data / "qualified-record", {"canonical", "ytsaurus"});
    AddCases(cases, data / "unhinted-body-recovery", {"canonical"});
    AddCases(
        cases,
        data / "lambda-argument-ownership",
        {"canonical", "preserve"});
    AddCases(cases, data / "lambda-argument-tail-recovery", {"canonical"});
    const auto hunks = data / "changed-hunks";
    cases
        .push_back(
            {hunks / "basic", "call", "input.cpp", "style", {{8, 8}}});
    cases
        .push_back(
            {hunks / "basic", "includes", "input.cpp", "style", {{1, 1}}});
    cases
        .push_back(
            {
                hunks / "basic",
                "includes-strict",
                "input.cpp",
                "style",
                {{1, 1}},
                false,
            });
    cases
        .push_back(
            {hunks / "basic", "function", "input.cpp", "style", {{4, 4}}});
    cases
        .push_back(
            {
                hunks / "spill",
                "default",
                "input.cpp",
                "style",
                {{3, 3}, {5, 5}},
            });
    cases
        .push_back(
            {
                hunks / "spill",
                "strict",
                "input.cpp",
                "style",
                {{3, 3}, {5, 5}},
                false,
            });
    cases
        .push_back(
            {
                hunks / "structural",
                "unsupported",
                "input.cpp",
                "style",
                {{2, 2}},
            });
    cases
        .push_back(
            {
                hunks / "planned",
                "strict-independent",
                "strict-independent.input.cpp",
                "style",
                {{3, 3}, {6, 6}},
                false,
            });
    cases
        .push_back(
            {
                hunks / "planned",
                "nested",
                "nested.input.cpp",
                "style",
                {{3, 3}},
                true,
            });
    cases
        .push_back(
            {
                hunks / "planned",
                "atomic",
                "atomic.input.cpp",
                "structural",
                {{2, 5}},
                false,
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
INSTANTIATE_TEST_SUITE_P(
    Formatter,
    FormatTest,
    testing::ValuesIn(FormatCases()),
    [] (const auto& info) {
        const auto root =
            (std::filesystem::path(FORMATTER_TESTDATA_DIR) / "..")
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
TEST(Formatter, CallArgument_ReflowsOnlySafeParsedCalls)
{
    formatter::Style style;
    style.ColumnLimit = 34;
    const auto directory = std::filesystem::path(FORMATTER_TESTDATA_DIR) /
        "../call-reflow";
    const auto input = ReadFile(directory / "input.cpp");
    EXPECT_EQ(FormatWithStyle(input, style), input);
    style.CallsPacking = formatter::ListPacking::Fill;
    EXPECT_EQ(FormatWithStyle(input, style), input);
    style.CallsLayout = formatter::CategoryLayout::Canonical;
    style.CallsBreak = formatter::BreakRequirement::IfNeeded;
    const auto packed = FormatWithStyle(input, style);
    test::golden::ExpectGolden(directory / "packed.output.cpp", packed);
    EXPECT_EQ(FormatWithStyle(packed, style), packed);
    style.CallsLayout = formatter::CategoryLayout::Canonical;
    style.CallsBreak = formatter::BreakRequirement::IfNeeded;
    const auto merged = FormatWithStyle(packed, style);
    test::golden::ExpectGolden(directory / "merged.output.cpp", merged);
    EXPECT_EQ(FormatWithStyle(merged, style), merged);
    const auto longMultiline = ReadFile(directory / "long.input.cpp");
    // Canonical Fill repacks authored breaks; Preserve above retains them.
    ExpectFormattedGolden(directory / "long-packed.output.cpp",
        longMultiline, style);
    style.CallsLayout = formatter::CategoryLayout::Canonical;
    style.CallsBreak = formatter::BreakRequirement::Always;
    const auto repacked = FormatWithStyle(longMultiline, style);
    test::golden::ExpectGolden(directory / "repacked.output.cpp", repacked);
    EXPECT_EQ(FormatWithStyle(repacked, style), repacked);
    style.CallsPacking = formatter::ListPacking::VerticalWhenBroken;
    style.CallsLayout = formatter::CategoryLayout::Canonical;
    style.CallsFirstItem =
        formatter::FirstItemPlacement::NextLine;
    const auto onePerLine = FormatWithStyle(input, style);
    test::golden::ExpectGolden(
        directory / "one-per-line.output.cpp",
        onePerLine);
    EXPECT_EQ(FormatWithStyle(onePerLine, style), onePerLine);
    style.CallsPacking = formatter::ListPacking::Fill;
    style.CallsLayout = formatter::CategoryLayout::Canonical;
    style.CallsAlignment = formatter::ContinuationAlignment::Delimiter;
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
    style.CallsFirstItem =
        formatter::FirstItemPlacement::InlineIfFits;
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
    style.CallsAlignment = formatter::ContinuationAlignment::Block;
    style.ControlBracePlacement =
        formatter::BracePlacement::NextLineIfHeaderBroken;
    style.ControlHeadersLayout = formatter::CategoryLayout::Canonical;
    const auto control =
        "void f() {\n"
        "    if (check(alpha, beta, gamma, delta)) {\n"
        "        run();\n"
        "    }\n"
        "}\n";
    const auto controlExpected =
        "void f() {\n"
        "    if (check(alpha, beta,\n"
        "            gamma, delta))\n"
        "    {\n"
        "        run();\n"
        "    }\n"
        "}\n";
    EXPECT_EQ(FormatWithStyle(control, style), controlExpected);
    EXPECT_EQ(FormatWithStyle(controlExpected, style), controlExpected);
    style.CallsLayout = formatter::CategoryLayout::Canonical;
    style.CallsBreak = formatter::BreakRequirement::IfNeeded;
    ExpectUnchanged(
        "void f() { use(alpha +\n        beta, gamma, delta); }\n",
        style);
    ExpectUnchanged(
        "void f() { auto x = (Type)(alpha, beta, gamma, delta); }\n",
        style);
    // Comments and CRLF constrain the affected gaps, not the whole call.
    ExpectFormattedGolden(directory / "comment.output.cpp",
        "void f() { use(alpha, /* note */ beta, gamma); }\n", style);
    ExpectFormattedGolden(directory / "crlf.output.cpp",
        "void f() { use(alpha, beta, gamma, delta); }\r\n", style);
    style.CallsPacking = formatter::ListPacking::VerticalWhenBroken;
    style.CallsLayout = formatter::CategoryLayout::Canonical;
    style.CallsBreak = formatter::BreakRequirement::Always;
    style.SpaceInsideCallArguments = formatter::GapPolicy::Space;
    style.ColumnLimit = 25;
    const auto spaced = "void f() { use(alpha, beta, gamma, delta); }\n";
    const auto spacedExpected =
        "void f() { use( alpha,\n"
        "        beta,\n"
        "        gamma,\n"
        "        delta ); }\n";
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
    style.CallsLayout = formatter::CategoryLayout::Canonical;
    style.CallsBreak = formatter::BreakRequirement::IfNeeded;
    style.CallsPacking = formatter::ListPacking::Fill;
    style.CallsLayout = formatter::CategoryLayout::Canonical;
    style.SpaceInsideCallArguments =
        formatter::GapPolicy::Preserve;
    EXPECT_EQ(FormatWithStyle(balancedInput, style), packedBalanced);
    EXPECT_EQ(FormatWithStyle(packedBalanced, style), packedBalanced);
    style.CallsPacking = formatter::ListPacking::VerticalWhenBroken;
    style.CallsLayout = formatter::CategoryLayout::Canonical;
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
    style.CallsLayout = formatter::CategoryLayout::Preserve;
    ExpectUnchanged(balancedInput, style);
    formatter::Style sole;
    sole.CallsPacking = formatter::ListPacking::VerticalWhenBroken;
    sole.CallsLayout = formatter::CategoryLayout::Canonical;
    sole.CallsBreak = formatter::BreakRequirement::IfNeeded;
    sole.TernaryLayout = formatter::TernaryLayout::ThreePart;
    sole.ExpressionsLayout = formatter::CategoryLayout::Canonical;
    sole.TernaryOperatorSide =
        formatter::OperatorSide::Leading;
    const auto multiline =
        "void f() {\n"
        "    auto result = Select(status\n"
        "        ? first\n"
        "        : second);\n"
        "    auto callback = MakeCallback([&] {\n"
        "        Run();\n"
        "    });\n"
        "}\n";
    // ThreePart is a broken-expression shape, not an unconditional break.
    ExpectFormattedGolden(directory / "sole-fitting.output.cpp", multiline, sole);
    sole.CallsFirstItem =
        formatter::FirstItemPlacement::InlineIfFits;
    ExpectFormattedGolden(directory / "sole-fitting.output.cpp", multiline, sole);
    sole.CallsFirstItem =
        formatter::FirstItemPlacement::NextLine;
    // A multiline child makes the call broken; NextLine then applies to
    // its first item. Canonical body layout follows the emitted call position.
    sole.BodiesLayout = formatter::CategoryLayout::Canonical;
    ExpectFormattedGolden(directory / "sole-separated.output.cpp", multiline, sole);
    formatter::Style lambdaStyle;
    lambdaStyle.ColumnLimit = 32;
    lambdaStyle.CallsPacking = formatter::ListPacking::VerticalWhenBroken;
    lambdaStyle.CallsLayout = formatter::CategoryLayout::Canonical;
    lambdaStyle.BodiesLayout = formatter::CategoryLayout::Canonical;
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
    ExpectFormattedGolden(directory / "lambda-capture.output.cpp",
        "void f() {\n"
        "    CallWithLongName([first = MakeVeryLongValue(), second] {\n"
        "        Process();\n"
        "    });\n"
        "}\n", lambdaStyle);
}
TEST(Formatter, CallArgument_LaysOutEnclosingMacroAndNestedCalls)
{
    formatter::Style style;
    style.CallsLayout = formatter::CategoryLayout::Canonical;
    style.CallsBreak = formatter::BreakRequirement::Always;
    style.CallsPacking = formatter::ListPacking::Fill;
    style.CallsLayout = formatter::CategoryLayout::Canonical;
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
    style.CallsFirstItem = formatter::FirstItemPlacement::InlineIfFits;
    // An unchanged outer call must not hide a deeper candidate.
    style.CallsLayout = formatter::CategoryLayout::Canonical;
    style.CallsBreak = formatter::BreakRequirement::IfNeeded;
    style.ColumnLimit = 48;
    const auto nested =
        "void f() {\n"
        "    Outer(Middle(Inner(alpha, beta, gamma, delta, epsilon)));\n"
        "}\n";
    const auto nestedExpected =
        "void f() {\n"
        "    Outer(\n"
        "        Middle(\n"
        "            Inner(alpha, beta, gamma, delta,\n"
        "                epsilon)));\n"
        "}\n";
    EXPECT_EQ(FormatWithStyle(nested, style), nestedExpected);
    EXPECT_EQ(FormatWithStyle(nestedExpected, style), nestedExpected);
    style.CallsPacking = formatter::ListPacking::VerticalWhenBroken;
    style.CallsLayout = formatter::CategoryLayout::Canonical;
    ExpectUnchanged("void f() { EXPECT_EQ(a, b); }\n", style);
}
TEST(Formatter, ExpressionLayout_WrapsParsedOperatorsAndPreservesUnsafeLines)
{
    formatter::Style style;
    style.ExpressionsLayout = formatter::CategoryLayout::Canonical;
    style.ExpressionsBreak = formatter::BreakRequirement::IfNeeded;
    style.TernaryLayout = formatter::TernaryLayout::Flexible;
    style.TernaryOperatorSide =
        formatter::OperatorSide::Leading;
    style.BinaryOperatorSide = formatter::OperatorSide::Leading;
    style.ColumnLimit = 32;
    const test::golden::Fixture fixture{
        std::filesystem::path(FORMATTER_TESTDATA_DIR) / "../parsed-operators",
        "configured"};
    ExpectUnchanged(fixture.Input(), formatter::Style{});
    style.BinaryOperatorSide =
        formatter::OperatorSide::Trailing;
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
        "    result =\n"
        "        alpha + beta + alpha +\n"
        "            beta;\n"
        "}\n";
    EXPECT_EQ(FormatWithStyle(assigned, style), assignedExpected);
    EXPECT_EQ(FormatWithStyle(assignedExpected, style), assignedExpected);
    style.ColumnLimit = 25;
    // Canonical topology uses safe spacing fallback; request compact spelling
    // explicitly rather than relying on Preserve at rebuilt operator gaps.
    style.BinaryOperatorSpacing = formatter::GapPolicy::NoSpace;
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
    style.BinaryOperatorSpacing = formatter::GapPolicy::Preserve;
    // Canonical expressions may reflow; CRLF, comments and recovered tails
    // remain byte-exact even when an adjacent complete prefix is formatted.
    const auto expectReflow = [&](std::string_view source,
                                  std::string_view expected) {
        EXPECT_EQ(FormatWithStyle(source, style), expected);
        EXPECT_EQ(FormatWithStyle(expected, style), expected);
    };
    expectReflow("int f(int a, int b) { return a + b + a + b; }\r\n",
        "int f(int a, int b) { return a +\r\n"
        "        b + a + b; }\r\n");
    ExpectUnchanged("#define SUM(a, b) ((a) + (b) + (a) + (b))\n", style);
    expectReflow(
        "int compute(int a, int b) { return a + b + a + b; // keep\n}\n",
        "int compute(int a, int b) { return a +\n"
        "        b + a +\n        b; // keep\n}\n");
    expectReflow(
        "int compute(int a, int b) { return a + b @ broken; }\n",
        "int compute(int a, int b) { return a +\n"
        "        b @ broken; }\n");
    ExpectUnchanged("template <int N> struct Array {};\n"
        "Array<1 + 2 + 3 + 4 + 5 + 6> value;\n", style);
}
TEST(Formatter, ExpressionLayout_PlacesAndGroupsTernaryBreaks)
{
    formatter::Style style;
    style.ExpressionsLayout = formatter::CategoryLayout::Canonical;
    style.ExpressionsBreak = formatter::BreakRequirement::IfNeeded;
    style.ColumnLimit = 40;
    const auto directory = std::filesystem::path(FORMATTER_TESTDATA_DIR) /
        "../ternary-layout";
    const auto input = ReadFile(directory / "input.cpp");
    for (auto placement : {formatter::OperatorSide::Leading,
        formatter::OperatorSide::Trailing})
    {
        style.TernaryOperatorSide = placement;
        style.TernaryLayout = formatter::TernaryLayout::Flexible;
        style.ExpressionsLayout = formatter::CategoryLayout::Canonical;
        const bool leading =
            placement == formatter::OperatorSide::Leading;
        const auto two = FormatWithStyle(input, style);
        test::golden::ExpectGolden(directory /
            (leading ? "two-leading.output.cpp" : "two-trailing.output.cpp"),
            two);
        EXPECT_EQ(FormatWithStyle(two, style), two);
        style.ExpressionsLayout = formatter::CategoryLayout::Preserve;
        EXPECT_EQ(FormatWithStyle(input, style), input);
        style.ExpressionsLayout = formatter::CategoryLayout::Canonical;
        style.TernaryLayout = formatter::TernaryLayout::ThreePart;
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
    style.TernaryOperatorSide =
        formatter::OperatorSide::Leading;
    style.TernaryLayout = formatter::TernaryLayout::Flexible;
    style.ExpressionsLayout = formatter::CategoryLayout::Canonical;
    const auto existing =
        "int f(bool condition) {\n"
        "    return condition ?\n"
        "        alphaValue : betaValue;\n"
        "}\n";
    // Shape and operator-side preferences apply only when broken. Canonical
    // IfNeeded disregards old breaks when the entire group fits.
    const auto canonical =
        "int f(bool condition) {\n"
        "    return condition ? alphaValue : betaValue;\n"
        "}\n";
    EXPECT_EQ(FormatWithStyle(existing, style), canonical);
    const auto bothSides =
        "int f(bool condition) {\n"
        "    return condition\n"
        "        ?\n"
        "        alphaValue : betaValue;\n"
        "}\n";
    EXPECT_EQ(FormatWithStyle(bothSides, style), canonical);
    style.TernaryLayout = formatter::TernaryLayout::ThreePart;
    style.ExpressionsLayout = formatter::CategoryLayout::Canonical;
    EXPECT_EQ(FormatWithStyle(existing, style), input);
    EXPECT_EQ(FormatWithStyle(bothSides, style), input);
    EXPECT_EQ(FormatWithStyle(threeLeading, style), input);
    const auto chain =
        "int f() {\n"
        "    return one ? a :\n"
        "        two ? b :\n"
        "        three ? c : d;\n"
        "}\n";
    const auto flatChain =
        "int f() {\n"
        "    return one ? a : two ? b : three ? c : d;\n"
        "}\n";
    EXPECT_EQ(FormatWithStyle(chain, style), flatChain);
    EXPECT_EQ(FormatWithStyle(flatChain, style), flatChain);
    style.ExpressionsBreak = formatter::BreakRequirement::Always;
    EXPECT_EQ(FormatWithStyle(existing, style), threeLeading);
    EXPECT_EQ(FormatWithStyle(threeLeading, style), threeLeading);
    style.ExpressionsBreak = formatter::BreakRequirement::IfNeeded;
    style.TernaryOperatorSide =
        formatter::OperatorSide::Trailing;
    EXPECT_EQ(FormatWithStyle(canonical, style), input);
    for (auto mode : {formatter::BreakRequirement::IfNeeded,
        formatter::BreakRequirement::Always})
    {
        style.ExpressionsBreak = mode;
        EXPECT_EQ(FormatWithStyle(existing, style),
            mode == formatter::BreakRequirement::Always
                ? threeTrailing : input);
    }
    style.ExpressionsLayout = formatter::CategoryLayout::Preserve;
    ExpectUnchanged(existing, style);
    style.ExpressionsLayout = formatter::CategoryLayout::Canonical;
    EXPECT_EQ(FormatWithStyle(existing, style), threeTrailing);
    EXPECT_EQ(FormatWithStyle(threeTrailing, style), threeTrailing);
    style.ExpressionsLayout = formatter::CategoryLayout::Canonical;
    style.ExpressionsBreak = formatter::BreakRequirement::IfNeeded;
    const auto crlf = "int f() {\r\n"
        "    return condition ? alphaValue : betaValue;\r\n}\r\n";
    EXPECT_EQ(FormatWithStyle("int f() {\r\n    return condition ?\r\n"
        "        alphaValue : betaValue;\r\n}\r\n", style), crlf);
    EXPECT_EQ(FormatWithStyle(crlf, style), crlf);
    style.TernaryOperatorSide =
        formatter::OperatorSide::Trailing;
    style.TernaryLayout = formatter::TernaryLayout::ThreePart;
    style.ExpressionsLayout = formatter::CategoryLayout::Canonical;
    const auto commented = "int f() {\n"
        "    return condition ?\n"
        "        alphaValue /* keep */\n"
        "        :\n"
        "        betaValue;\n"
        "}\n";
    EXPECT_EQ(FormatWithStyle("int f() {\n"
        "    return condition ? alphaValue /* keep */\n"
        "        : betaValue;\n"
        "}\n", style), commented);
    EXPECT_EQ(FormatWithStyle(commented, style), commented);
    const auto nested =
        "int f() {\n"
        "    return outer ? inner ? alpha\n"
        "        : beta : gamma;\n"
        "}\n";
    const auto nestedExpected =
        "int f() {\n"
        "    return outer ? inner ? alpha : beta : gamma;\n"
        "}\n";
    EXPECT_EQ(FormatWithStyle(nested, style), nestedExpected);
    EXPECT_EQ(FormatWithStyle(nestedExpected, style), nestedExpected);
    formatter::Style preserve;
    // Explicit topology retention wins even over an Always requirement.
    preserve.ExpressionsLayout = formatter::CategoryLayout::Preserve;
    preserve.ExpressionsBreak = formatter::BreakRequirement::Always;
    preserve.ColumnLimit = 20;
    ExpectUnchanged(input, preserve);
    ExpectUnchanged(existing, preserve);
    preserve.ExpressionsLayout = formatter::CategoryLayout::Canonical;
    preserve.ExpressionsBreak = formatter::BreakRequirement::IfNeeded;
    preserve.ColumnLimit = 80;
    preserve.TernaryOperatorSide =
        formatter::OperatorSide::Leading;
    preserve.ExpressionsLayout = formatter::CategoryLayout::Canonical;
    EXPECT_EQ(FormatWithStyle(existing, preserve), canonical);
    // Retain topology explicitly; operator-side choices do not activate it.
    preserve.ExpressionsLayout = formatter::CategoryLayout::Preserve;
    preserve.TernaryOperatorSide =
        formatter::OperatorSide::Trailing;
    preserve.TernaryLayout = formatter::TernaryLayout::Flexible;
    ExpectUnchanged(existing, preserve);
    preserve.TernaryLayout = formatter::TernaryLayout::ThreePart;
    preserve.ExpressionsLayout = formatter::CategoryLayout::Canonical;
    EXPECT_EQ(FormatWithStyle(existing, preserve), input);
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
        if (
            begin < text.size() && text[begin++] == '\r' &&
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
TEST(Formatter, ExpressionLayout_WrapsLongBinaryExpressions)
{
    formatter::Style style;
    style.ExpressionsLayout = formatter::CategoryLayout::Canonical;
    style.ExpressionsBreak = formatter::BreakRequirement::IfNeeded;
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
    style.BinaryOperatorSide = formatter::OperatorSide::Leading;
    const auto leading =
        "void f() {\n"
        "    return a + bb + ccc + dddd + eeeee\n"
        "        + ffffff;\n"
        "}\n";
    EXPECT_EQ(FormatWithStyle(input, style), leading);
    style.BinaryOperatorSide =
        formatter::OperatorSide::Trailing;
    style.ExpressionsLayout = formatter::CategoryLayout::Preserve;
    ExpectUnchanged(input, style);
    style.ExpressionsLayout = formatter::CategoryLayout::Canonical;
    style.ExpressionsBreak = formatter::BreakRequirement::Always;
    style.ColumnLimit = 20;
    const auto comparison = "void f() { bool v =\n"
        "        alpha<\n"
        "            beta; }\n";
    EXPECT_EQ(FormatWithStyle("void f() { bool v = alpha<beta; }\n",
        style), comparison);
    EXPECT_EQ(FormatWithStyle(comparison, style), comparison);
}
TEST(Formatter, ExpressionLayout_ReflowsSafeOperatorContinuationsByPolicy)
{
    formatter::Style style;
    style.ExpressionsLayout = formatter::CategoryLayout::Canonical;
    style.ExpressionsBreak = formatter::BreakRequirement::IfNeeded;
    style.TernaryLayout = formatter::TernaryLayout::Flexible;
    style.ExpressionsLayout = formatter::CategoryLayout::Canonical;
    style.TernaryOperatorSide =
        formatter::OperatorSide::Leading;
    style.ColumnLimit = 80;
    // Reflow and horizontal spacing are separate policy axes.
    style.BinaryOperatorSpacing = formatter::GapPolicy::Space;
    style.AssignmentOperatorSpacing = formatter::GapPolicy::Space;
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
    style.BinaryOperatorSide = formatter::OperatorSide::Leading;
    style.ExpressionsLayout = formatter::CategoryLayout::Canonical;
    style.ExpressionsBreak = formatter::BreakRequirement::IfNeeded;
    const auto leading =
        "int f(int alpha, int beta, int gamma) {\n"
        "    return alpha + beta\n"
        "        + gamma;\n"
        "}\n";
    EXPECT_EQ(FormatWithStyle(wrapped, style), leading);
    EXPECT_EQ(FormatWithStyle(leading, style), leading);
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
        "    result =\n"
        "        alpha + beta + gamma;\n"
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
    style.ExpressionsLayout = formatter::CategoryLayout::Canonical;
    style.ExpressionsBreak = formatter::BreakRequirement::IfNeeded;
    EXPECT_EQ(FormatWithStyle(wrapped, style), joined);
    style.BinaryOperatorSide =
        formatter::OperatorSide::Trailing;
    const auto repositioned =
        "int f(int alpha, int beta, int gamma) {\n"
        "    return alpha +\n"
        "        beta +\n"
        "        gamma;\n"
        "}\n";
    EXPECT_EQ(FormatWithStyle(wrapped, style), joined);
    EXPECT_EQ(FormatWithStyle(repositioned, style), joined);
    style.BinaryOperatorSide = formatter::OperatorSide::Leading;
    EXPECT_EQ(FormatWithStyle(repositioned, style), joined);
    style.BinaryOperatorSide =
        formatter::OperatorSide::Trailing;
    style.ColumnLimit = 30;
    style.ExpressionsLayout = formatter::CategoryLayout::Canonical;
    style.ExpressionsBreak = formatter::BreakRequirement::Always;
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
    style.BinaryOperatorSide = formatter::OperatorSide::Leading;
    EXPECT_EQ(FormatWithStyle(wrapped, style), leading);
    EXPECT_EQ(FormatWithStyle(leading, style), leading);
    style.ColumnLimit = 80;
    ExpectUnchanged("void f() { call(alpha, beta, gamma); }\n", style);
    const auto commented = "void f() { int x =\n"
        "        a\n            + b; // keep\n}\n";
    EXPECT_EQ(FormatWithStyle("void f() { int x = a + b; // keep\n}\n",
        style), commented);
    EXPECT_EQ(FormatWithStyle(commented, style), commented);
    const auto crlf = "void f() { int x =\r\n"
        "        a\r\n            + b; }\r\n";
    EXPECT_EQ(FormatWithStyle("void f() { int x = a + b; }\r\n", style), crlf);
    EXPECT_EQ(FormatWithStyle(crlf, style), crlf);
    const auto ambiguous =
        "template<class... Args> void f() {\n"
        "    static_assert(TDependentFalse<Args...>, "
        "\"Wrong number of arguments\");\n"
        "}\n";
    for (auto mode : {formatter::BreakRequirement::IfNeeded,
        formatter::BreakRequirement::Always})
    {
        style.ExpressionsBreak = mode;
        style.ColumnLimit = 32;
        ExpectUnchanged(ambiguous, style);
        // The parser identifies this as a comparison, not an uncertain
        // template-id. Preserve spacing is not a veto on its topology.
        const auto comparison = mode == formatter::BreakRequirement::Always
            ? "void f() { bool v =\n        alpha\n            < beta; }\n"
            : "void f() { bool v =\n        alpha < beta; }\n";
        EXPECT_EQ(FormatWithStyle("void f() { bool v = alpha<beta; }\n",
            style), comparison);
        EXPECT_EQ(FormatWithStyle(comparison, style), comparison);
    }
}
TEST(Formatter, FunctionParameterSafety_ComposesWithBracePlacement)
{
    formatter::Style style;
    style.ColumnLimit = 40;
    style.ParametersPacking = formatter::ListPacking::Fill;
    style.FunctionBracePlacement = formatter::BracePlacement::NextLine;
    // Explicitly retain brace topology independently of parameter reflow.
    style.DeclarationHeadersLayout = formatter::CategoryLayout::Preserve;
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
    style.ParametersLayout = formatter::CategoryLayout::Canonical;
    const std::string expected =
        "void Many(int alpha, int beta,\n"
        "    int gamma, int delta) {}\n";
    EXPECT_EQ(
        format("void Many(int alpha, int beta, int gamma, int delta) {}\n"),
        expected);
    EXPECT_EQ(format(expected), expected);
    EXPECT_EQ(format("void Short() {}\n"), "void Short() {}\n");
    style.DeclarationHeadersLayout = formatter::CategoryLayout::Canonical;
    EXPECT_EQ(format("void Short() {}\n"), "void Short()\n{}\n");
    // The public pipeline uses eight-column tab stops. Request an eight-column
    // continuation so this case exercises an emitted tab, not four spaces.
    style.IndentationKind = formatter::IndentationKind::Tabs;
    style.ParametersContinuationOffset = 8;
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
    style.ParametersPacking = formatter::ListPacking::Fill;
    style.ParametersLayout = formatter::CategoryLayout::Canonical;
    style.ColumnLimit = 25;
    const auto directory = std::filesystem::path(FORMATTER_TESTDATA_DIR) /
        "../function_parameters";
    ExpectFormattedGolden(directory / "crlf.output.cpp",
        "void f(int alpha, int beta, int gamma);\r\n"
        "void g() { Use(very_long_argument, another_long_argument); }\r\n",
        style);
}
TEST(Formatter, BlockIndentation_AnchorsInlineLambdaToItsCallLine)
{
    formatter::Style style;
    style.BodiesLayout = formatter::CategoryLayout::Canonical;
    // Each enclosing call list retains its own continuation contribution,
    // even when its topology is preserved and the lambda starts inline.
    const std::string source =
        "void f()\n"
        "{\n"
        "    Visit(\n"
        "        MakeCallback([&] (auto value) {\n"
        "                if (value) {\n"
        "                    Use(value);\n"
        "                }\n"
        "            }),\n"
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
    style.BlockBodyIndentWidth = 2;
    const std::string narrowBody =
        "void f()\n"
        "{\n"
        "  Visit(\n"
        "        MakeCallback([&] (auto value) {\n"
        "            if (value) {\n"
        "              Use(value);\n"
        "            }\n"
        "          }),\n"
        "        other);\n"
        "}\n";
    EXPECT_EQ(FormatWithStyle(incorrect, style), narrowBody);
    ExpectUnchanged(narrowBody, style);
}
TEST(Formatter, ControlBrace_WrapsOnlyCompleteUnbracedBodies)
{
    formatter::Style style;
    style.AddMissingControlBraces = true;
    style.BodiesLayout = formatter::CategoryLayout::Canonical;
    style.ControlBracePlacement =
        formatter::BracePlacement::NextLineIfHeaderBroken;
    style.ControlHeadersLayout = formatter::CategoryLayout::Canonical;
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
        "    while (a)\n"
        "        ;\n"
        "    do {\n"
        "        Go();\n"
        "    } while (a);\n"
        "}\n";
    EXPECT_EQ(FormatWithStyle(input, style), expected);
    EXPECT_EQ(FormatWithStyle(expected, style), expected);
    style.EmptyControlBody = formatter::EmptyControlBody::EmptyBlock;
    style.EmptyBodyShape = formatter::BodyShape::CompactIfFits;
    style.EmptyBlockInteriorSpacing = formatter::GapPolicy::Space;
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
        "    while (a) { }\n"
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
        "void f() {\n    if (a) { }\n    if (b)\n        Go();\n}\n");
    style.EmptyControlBody = formatter::EmptyControlBody::Preserve;
    style.AddMissingControlBraces = true;
    const std::string commentedExpected =
        "void f(bool a) {\n    if (a) /* keep */ Go();\n}\n";
    EXPECT_EQ(FormatWithStyle(
        "void f(bool a) { if (a) /* keep */ Go(); }\n", style),
        commentedExpected);
    ExpectUnchanged(commentedExpected, style);
    EXPECT_EQ(FormatWithStyle(
        "void f(bool a) { if (a)\r\n    Go(); }\r\n",
        style),
        "void f(bool a) {\r\n    if (a) {\r\n"
        "        Go();\r\n    }\r\n}\r\n");
    ExpectUnchanged(
        "void f(bool a) {\r\n    if (a) {\r\n"
        "        Go();\r\n    }\r\n}\r\n", style);
    const std::string labeledExpected =
        "void f(int x) {\n    switch (x) case 0:\n    Go();\n}\n";
    EXPECT_EQ(FormatWithStyle(
        "void f(int x) { switch (x) case 0: Go(); }\n", style),
        labeledExpected);
    ExpectUnchanged(labeledExpected, style);
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
    style.BodiesLayout = formatter::CategoryLayout::Canonical;
    style.EmptyBodyShape = formatter::BodyShape::CompactIfFits;
    style.EmptyBlockInteriorSpacing = formatter::GapPolicy::Space;
    style.FunctionBracePlacement = formatter::BracePlacement::NextLine;
    style.DeclarationHeadersLayout = formatter::CategoryLayout::Canonical;
    const std::string input =
        "void Empty()\n{\n}\n"
        "void WithComment()\n{\n    // Keep this.\n}\n";
    const std::string expected =
        "void Empty()\n{ }\n"
        "void WithComment()\n{\n    // Keep this.\n}\n";
    EXPECT_EQ(FormatWithStyle(input, style), expected);
    EXPECT_EQ(FormatWithStyle(expected, style), expected);
    style.BodyShape = formatter::BodyShape::Expanded;
    EXPECT_EQ(FormatWithStyle(
        "void Annotated() { /* keep */ Act(); }\n",
        style),
        "void Annotated()\n{ /* keep */ Act();\n}\n");
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

TEST(Formatter, BraceSpacing_ComposesWithParameterReflow)
{
    formatter::Style style;
    style.ColumnLimit = 40;
    style.ParametersPacking = formatter::ListPacking::Fill;
    style.ParametersLayout = formatter::CategoryLayout::Canonical;
    style.FunctionBraceSpacing = formatter::GapPolicy::Space;
    // Explicit spacing applies independently of preserved header topology.
    const std::string input =
        "void Many(int alpha, int beta, int gamma, int delta){}\n";
    const std::string expected =
        "void Many(int alpha, int beta,\n"
        "    int gamma, int delta) {}\n";
    EXPECT_EQ(FormatWithStyle(input, style), expected);
    EXPECT_EQ(FormatWithStyle(expected, style), expected);
    style.FunctionBracePlacement = formatter::BracePlacement::Attached;
    style.DeclarationHeadersLayout = formatter::CategoryLayout::Canonical;
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
