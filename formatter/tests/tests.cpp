#include <formatter/config.hpp>
#include <formatter/formatter.hpp>
#include <formatter/hunks.hpp>
#include <formatter/naming.hpp>
#include <formatter/parameter_names.hpp>

#include <gtest/gtest.h>

#include <tests/format_changes.hpp>
#include <tests/formatting.hpp>
#include <tests/golden.hpp>

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
using test::Input;
using test::FormatWithStyle;

using formatter::BlankLineMode;

using formatter::BodyShape;

using formatter::BracePlacement;

using formatter::BreakRequirement;

using formatter::CategoryLayout;

using formatter::CloserPlacement;

using formatter::ContinuationAlignment;

using formatter::EmptyControlBody;

using formatter::FirstItemPlacement;

using formatter::GapPolicy;

using formatter::IndentationKind;

using formatter::LayoutMode;

using formatter::LineEnding;

using formatter::ListPacking;

using formatter::NamespaceComments;

using formatter::OperatorSide;

using formatter::TernaryLayout;

using formatter::TernaryOperatorIndentation;

using formatter::TrailingCommaPolicy;

using formatter::TrailingWhitespace;

void ExpectUnchanged(std::string_view source, const formatter::Style& style)
{
    EXPECT_EQ(FormatWithStyle(source, style), source);
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
    for (const auto& entry : std::filesystem::directory_iterator(FORMATTER_TESTDATA_DIR)) {
        auto filename = entry.path().filename().string();
        if (filename.ends_with(".output.cpp")) {
            const auto input = entry.path().parent_path() /
                (filename.substr(0, filename.size() - std::string_view(".output.cpp").size()) + ".input.cpp");
            // Equal input/output bytes have one default-preservation contract.
            if (std::filesystem::exists(input) && ReadFile(input) == ReadFile(entry.path())) {
                continue;
            }
        }
        if (filename.ends_with(".input.cpp") || filename.ends_with(".output.cpp")) {
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
    const auto input = ReadFile(std::filesystem::path(FORMATTER_TESTDATA_DIR) / GetParam().Name);
    formatter::Style style;
    style.IndentWidth = 32;
    style.ColumnLimit = 10000;
    ExpectUnchanged(input, style);
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

void ExpectFormattedGolden(
    const std::filesystem::path& path,
    std::string_view input,
    const formatter::Style& style,
    bool readOnlyExpected,
    std::string_view inputFilePath = {})
{
    const auto actual = FormatWithStyle(input, style, inputFilePath);
    if (readOnlyExpected) {
        // Shared oracles, including inputs, must never be regenerated here.
        EXPECT_EQ(actual, ReadFile(path)) << path;
    } else {
        test::golden::ExpectGolden(path, actual);
    }
    EXPECT_EQ(FormatWithStyle(actual, style, inputFilePath), actual);
    const Input storage(input);
    const auto detailed = formatter::Format(
        storage.View(),
        formatter::PreparedFormat(style),
        {.InputFilePath = inputFilePath, .Changes = true});
    EXPECT_EQ(detailed.Text, actual);
    test::ExpectFormatChanges(input, detailed);
    const auto summary = formatter::Format(
        storage.View(),
        formatter::PreparedFormat(style),
        {.InputFilePath = inputFilePath, .ChangeSummary = true});
    test::ExpectChangeSummary(summary, detailed);
}

struct FormatCase
{
    std::filesystem::path Directory;
    std::string Name;
    std::string Input;
    std::string Config;
    std::vector<formatter::Hunk> Hunks;
    bool Spillover = true;
    std::string Expected;
    std::optional<formatter::Style> Style;
    std::string InputFilePath;
    std::filesystem::path GoldenPath;
    std::string TestName;
};

void PrintTo(const FormatCase& value, std::ostream* output)
{
    const auto root = (std::filesystem::path(FORMATTER_TESTDATA_DIR) / "..").lexically_normal();
    *output << value.Directory.lexically_normal().lexically_relative(root).generic_string() << '/' << value.Name;
}

void AddCases(
    std::vector<FormatCase>& cases,
    const std::filesystem::path& directory,
    std::initializer_list<const char*> names,
    std::string input = "input.cpp")
{
    for (const auto* name : names) {
        cases.push_back({directory, name, input});
    }
}

// Profile stems select styles for every input/context that uses that policy.
void ApplyProfile(
    std::vector<FormatCase>& cases,
    const std::filesystem::path& directory,
    std::string_view profile,
    const formatter::Style& style)
{
    for (auto& value : cases) {
        if (value.Directory.lexically_normal() == directory.lexically_normal() &&
            (value.Config.empty() ? value.Name : value.Config) == profile)
        {
            value.Style = style;
        }
    }
}

void ShareConfig(
    std::vector<FormatCase>& cases,
    const std::filesystem::path& directory,
    std::string_view profile,
    std::string_view shared)
{
    for (auto& value : cases) {
        if (value.Directory.lexically_normal() == directory.lexically_normal() &&
            (value.Config.empty() ? value.Name : value.Config) == profile)
        {
            value.Config = shared;
        }
    }
}

void ShareExpected(
    std::vector<FormatCase>& cases,
    const std::filesystem::path& directory,
    std::string_view original,
    std::string_view shared)
{
    for (auto& value : cases) {
        if (value.Directory.lexically_normal() == directory.lexically_normal() &&
            (value.Expected.empty() ? value.Name + ".output.cpp" : value.Expected) == original)
        {
            value.Expected = shared;
        }
    }
}

std::vector<FormatCase> FormatCases()
{
    std::vector<FormatCase> cases;
    const auto data = std::filesystem::path(FORMATTER_TESTDATA_DIR).parent_path();
    const auto examples = (data / "../../../hit-fmt/examples").lexically_normal();
    const auto corpus = examples / "pdf-to-markdown";
    for (const auto& profileEntry : std::filesystem::directory_iterator(examples)) {
        if (!profileEntry.is_regular_file() || profileEntry.path().extension() != ".hit-fmt") {
            continue;
        }
        const auto profile = profileEntry.path().stem().string();
        const auto directory = corpus / profile;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(directory)) {
            const auto extension = entry.path().extension();
            if (!entry.is_regular_file() ||
                (extension != ".cpp" && extension != ".hpp" && extension != ".cc" && extension != ".h" &&
                    extension != ".cxx" && extension != ".hxx"))
            {
                continue;
            }
            const auto relative = entry.path().lexically_relative(directory);
            FormatCase value;
            value.Directory = directory;
            value.Name = relative.generic_string();
            value.Input = relative.string();
            value.Config = (examples / profile).string();
            value.InputFilePath = relative.generic_string();
            value.GoldenPath = entry.path();
            value.TestName = "pdf_to_markdown_" + profile + "_" + relative.generic_string();
            for (auto& character : value.TestName) {
                if (character == '/' || character == '.' || character == '-') {
                    character = '_';
                }
            }
            cases.push_back(std::move(value));
        }
    }
    for (const auto* profile : {"google", "llvm", "ytsaurus"}) {
        cases.push_back({
            data / "inheritance",
            std::string("profile-") + profile,
            "profiles.input.cpp",
            (examples / profile).string(),
        });
    }
    const auto packing = data / "all-or-vertical";
    AddCases(cases, packing, {"canonical", "inline", "delimiter", "always", "preserve", "unlimited"});
    AddCases(cases, packing, {"annotations"}, "../chained-calls/annotations.input.cpp");
    AddCases(cases, packing, {"chain"}, "chain.input.cpp");
    AddCases(cases, packing, {"conditions"}, "../if-conditions-packing/input.cpp");
    ShareExpected(cases, packing, "conditions.output.cpp", "../if-conditions-packing/leading.output.cpp");
    AddCases(cases, packing, {"expressions"}, "../assignment-location/packing.input.cpp");
    ShareExpected(cases, packing, "expressions.output.cpp", "../assignment-location/packing.output.cpp");
    const auto newOptions = data / "new-options";
    AddCases(cases, newOptions, {"syntax", "unlimited", "preserve", "always"});
    AddCases(cases, newOptions, {"text", "sorted-text"}, "text.input.cpp");
    AddCases(cases, newOptions, {"crlf"}, "crlf.input.cpp");
    AddCases(cases, data / "function-body-macros", {"hinted", "unhinted"});
    AddCases(cases, data / "single-statement-body", {"single"});
    AddCases(cases, data / "single-statement-body", {"compact", "preserve", "lambda-override"}, "lambdas.input.cpp");
    AddCases(
        cases,
        data / "single-statement-body",
        {"functions", "controls", "compact-categories", "inherited", "categories-preserve"},
        "categories.input.cpp");
    ShareExpected(cases, data / "single-statement-body", "categories-preserve.output.cpp", "categories.input.cpp");
    {
        const auto directory = data / "declaration-macros";
        AddCases(cases, directory, {"canonical", "remove-null"});
        auto style = formatter::LoadConfigFile(directory / "canonical.hit-fmt").FormattingStyle;
        style.RemoveNullDeclarations = true;
        cases.back().Style = style;
        cases.back().Expected = "canonical.output.cpp";
    }
    AddCases(cases, data / "null-removal", {"canonical", "preserve"});
    AddCases(cases, data / "declarator-macro-spacing", {"space", "no-space"});
    AddCases(cases, data / "alias-assignment", {"space", "no-space", "preserve"});
    ShareExpected(cases, data / "alias-assignment", "preserve.output.cpp", "input.cpp");
    AddCases(cases, data / "class-body-macros", {"hinted", "preserve", "unhinted"});
    AddCases(cases, data / "class-body-macros", {"members"}, "members.input.cpp");
    AddCases(cases, data / "declaration-headers", {"wrap", "narrow", "wide"});
    AddCases(cases, data / "interword-spacing", {"canonical", "preserve", "headers-preserve"});
    AddCases(
        cases,
        data / "syntax-boundaries",
        {"space", "no-space", "preserve", "generated-space", "generated-no-space"});
    ShareExpected(cases, data / "interword-spacing", "preserve.output.cpp", "input.cpp");
    AddCases(
        cases,
        data / "interword-spacing",
        {
            "spacing-preserve",
            "specifier-preserve",
            "name-preserve",
            "space",
            "no-space",
            "generated-no-space",
            "reflow-preserve",
        });
    AddCases(
        cases,
        data / "declaration-headers",
        {"qualified", "qualified-always", "qualified-preserve"},
        "qualified.input.cpp");
    cases.push_back({data / "declaration-headers", "return-types", "return-types.input.cpp", "narrow", {}, true, {}});
    AddCases(
        cases,
        data / "declaration-headers",
        {"trailing-return", "trailing-return-narrow", "trailing-return-wide"},
        "trailing-return.input.cpp");
    cases.push_back({
        data / "declaration-headers",
        "trailing-return-forced",
        "trailing-return.input.cpp",
        "qualified-always",
    });
    {
        const auto directory = data / "declaration-headers";
        const auto base = formatter::LoadConfigFile(directory / "../style/repository-80.hit-fmt").FormattingStyle;
        FormatCase selfType{directory, "self-type-statements", "self-type-statements.input.cpp"};
        selfType.Style = base;
        cases.push_back(std::move(selfType));
        FormatCase fitting{directory, "fitting-prefix-rhs", "fitting-prefix-rhs.input.cpp"};
        fitting.Style = base;
        cases.push_back(std::move(fitting));
        for (const auto name :
            {
                "assignment-prefix",
                "assignment-prefix-leading",
                "assignment-prefix-next-line",
                "assignment-prefix-preserve",
            })
        {
            FormatCase value{directory, name, "assignment-prefix.input.cpp"};
            value.Style = base;
            if (std::string_view(name) == "assignment-prefix-leading") {
                value.Style->AssignmentOperatorSide = OperatorSide::Leading;
            } else if (std::string_view(name) == "assignment-prefix-next-line") {
                value.Style->AssignmentRhsPlacement = FirstItemPlacement::NextLine;
            } else if (std::string_view(name) == "assignment-prefix-preserve") {
                value.Style->DeclarationHeadersLayout = CategoryLayout::Preserve;
            }
            cases.push_back(std::move(value));
        }
    }
    AddCases(cases, data / "vertical-grouping", {"preserve", "declarations", "overrides", "capped"});
    AddCases(cases, data / "vertical-grouping", {"structural"}, "structural.input.cpp");
    AddCases(
        cases,
        data / "vertical-grouping",
        {"access-sections", "access-sections-zero", "access-sections-preserve"},
        "access-sections.input.cpp");
    ShareExpected(
        cases,
        data / "vertical-grouping",
        "access-sections-preserve.output.cpp",
        "access-sections.input.cpp");
    AddCases(
        cases,
        data / "vertical-grouping",
        {"categories", "categories-preserve", "categories-overrides"},
        "categories.input.cpp");
    AddCases(cases, data / "vertical-grouping", {"linkage", "linkage-zero", "linkage-preserve"}, "linkage.input.cpp");
    AddCases(
        cases,
        data / "linkage-indentation",
        {"canonical", "preserve", "indent", "no-indent", "width", "indent-preserve"});
    {
        const auto directory = data / "vertical-grouping";
        {
            const auto base = formatter::LoadConfigFile(directory / "preserve.hit-fmt").FormattingStyle;
            struct Policy
            {
                const char* Profile;
                formatter::BlankLineSetting NamespaceBlankLinesAfterOpen;
                formatter::BlankLineSetting NamespaceBlankLinesBeforeClose;
                formatter::BlankLineSetting DeclarationBlankLines;
                formatter::BlankLineSetting MemberDeclarationBlankLines;
                formatter::BlankLineSetting DefinitionBlankLines;
                std::optional<std::size_t> MaxEmptyLines;
            };
            for (const auto& policy :
                std::array<Policy, 3>{
                    {
                        {
                            "capped",
                            formatter::BlankLineSetting{BlankLineMode::Preserve, 0},
                            formatter::BlankLineSetting{BlankLineMode::Preserve, 0},
                            formatter::BlankLineSetting{BlankLineMode::Preserve, 0},
                            formatter::BlankLineSetting{BlankLineMode::Inherit, 0},
                            formatter::BlankLineSetting{BlankLineMode::Preserve, 0},
                            1,
                        },
                        {
                            "declarations",
                            formatter::BlankLineSetting{BlankLineMode::Exact, 1},
                            formatter::BlankLineSetting{BlankLineMode::Exact, 1},
                            formatter::BlankLineSetting{BlankLineMode::Exact, 1},
                            formatter::BlankLineSetting{BlankLineMode::Inherit, 0},
                            formatter::BlankLineSetting{BlankLineMode::Preserve, 0},
                            std::nullopt,
                        },
                        {
                            "overrides",
                            formatter::BlankLineSetting{BlankLineMode::Exact, 1},
                            formatter::BlankLineSetting{BlankLineMode::Exact, 1},
                            formatter::BlankLineSetting{BlankLineMode::Exact, 1},
                            formatter::BlankLineSetting{BlankLineMode::Exact, 0},
                            formatter::BlankLineSetting{BlankLineMode::Exact, 2},
                            std::nullopt,
                        },
                    },
                })
            {
                auto style = base;
                style.NamespaceBlankLinesAfterOpen = policy.NamespaceBlankLinesAfterOpen;
                style.NamespaceBlankLinesBeforeClose = policy.NamespaceBlankLinesBeforeClose;
                style.DeclarationBlankLines = policy.DeclarationBlankLines;
                style.MemberDeclarationBlankLines = policy.MemberDeclarationBlankLines;
                style.DefinitionBlankLines = policy.DefinitionBlankLines;
                style.MaxEmptyLines = policy.MaxEmptyLines;
                ApplyProfile(cases, directory, policy.Profile, style);
            }
        }
    }
    AddCases(cases, data / "group-fit-boundary", {"fit", "overflow"});
    AddCases(cases, data / "group-fit-boundary", {"forced"}, "forced.input.cpp");
    AddCases(cases, data / "group-fit-boundary", {"call-fit"}, "call.input.cpp");
    {
        const auto directory = data / "group-fit-boundary";
        {
            const auto base = formatter::LoadConfigFile(directory / "fit.hit-fmt").FormattingStyle;
            struct Policy
            {
                const char* Profile;
                std::size_t ColumnLimit;
                formatter::BreakRequirement ChainsBreak;
            };
            for (const auto& policy :
                std::array<Policy, 3>{
                    {
                        {"call-fit", 24, BreakRequirement::IfNeeded},
                        {"forced", 80, BreakRequirement::Always},
                        {"overflow", 32, BreakRequirement::IfNeeded},
                    },
                })
            {
                auto style = base;
                style.ColumnLimit = policy.ColumnLimit;
                style.ChainsBreak = policy.ChainsBreak;
                ApplyProfile(cases, directory, policy.Profile, style);
            }
        }
        ShareExpected(cases, directory, "call-fit.output.cpp", "call.input.cpp");
        ShareExpected(cases, directory, "fit.output.cpp", "input.cpp");
    }
    cases.push_back({data / "control-expression-continuation", "repository", "input.cpp", "../style/repository-80"});
    cases.push_back({
        data / "control-expression-continuation",
        "ytsaurus",
        "input.cpp",
        "../../../../hit-fmt/examples/ytsaurus",
    });
    for (const auto* name : {"opaque-body", "opaque-body-expanded"}) {
        cases.push_back({
            data / "control-expression-continuation",
            name,
            "opaque-body.input.cpp",
            "../../../../hit-fmt/examples/ytsaurus",
            {},
            true,
            {},
        });
        cases.back().Style =
            formatter::LoadConfigFile(cases.back().Directory / (cases.back().Config + ".hit-fmt")).FormattingStyle;
        cases.back().Style->LambdaBodiesLayout = CategoryLayout::Preserve;
        if (std::string_view(name) == "opaque-body-expanded") {
            cases.back().Style->LambdaBodiesLayout = CategoryLayout::Canonical;
            cases.back().Style->LambdaBodyShape = BodyShape::Expanded;
            // Exercise list reflow beside protected macros and expanded lambda bodies.
            cases.back().Style->InitializersLayout = CategoryLayout::Canonical;
        }
    }
    AddCases(cases, data / "line-endings", {"lf", "crlf", "legacy"});
    AddCases(cases, data / "line-endings", {"boundary-lf", "boundary-crlf"}, "boundary.input.cpp");
    AddCases(
        cases,
        data / "line-endings",
        {"boundary-namespace-lf", "boundary-namespace-crlf"},
        "boundary-namespace.input.cpp");
    for (const auto* name : {"canonical-lf", "canonical-crlf", "opaque-lf", "boundary-nulls", "composition-control-lf"})
    {
        cases.push_back({data / "line-endings", name, std::string(name) + ".input.cpp"});
    }
    {
        const auto directory = data / "line-endings";
        {
            const auto base = formatter::LoadConfigFile(directory / "lf.hit-fmt").FormattingStyle;
            struct Policy
            {
                const char* Profile;
                formatter::LayoutMode Layout;
                std::size_t IndentWidth;
                formatter::IndentationKind IndentationKind;
                formatter::LineEnding LineEnding;
                formatter::TrailingWhitespace TrailingWhitespace;
                formatter::CategoryLayout ControlHeadersLayout;
                bool RemoveNullDeclarations;
                formatter::NamespaceComments NamespaceComments;
            };
            for (const auto& policy :
                std::array<Policy, 9>{
                    {
                        {
                            "boundary-crlf",
                            LayoutMode::Preserve,
                            4,
                            IndentationKind::Preserve,
                            LineEnding::CRLF,
                            TrailingWhitespace::Trim,
                            CategoryLayout::Inherit,
                            false,
                            NamespaceComments::Preserve,
                        },
                        {
                            "boundary-lf",
                            LayoutMode::Preserve,
                            4,
                            IndentationKind::Preserve,
                            LineEnding::LF,
                            TrailingWhitespace::Trim,
                            CategoryLayout::Inherit,
                            false,
                            NamespaceComments::Preserve,
                        },
                        {
                            "boundary-namespace-crlf",
                            LayoutMode::Preserve,
                            4,
                            IndentationKind::Preserve,
                            LineEnding::CRLF,
                            TrailingWhitespace::Preserve,
                            CategoryLayout::Inherit,
                            false,
                            NamespaceComments::Fix,
                        },
                        {
                            "boundary-namespace-lf",
                            LayoutMode::Preserve,
                            4,
                            IndentationKind::Preserve,
                            LineEnding::LF,
                            TrailingWhitespace::Preserve,
                            CategoryLayout::Inherit,
                            false,
                            NamespaceComments::Fix,
                        },
                        {
                            "boundary-nulls",
                            LayoutMode::Preserve,
                            4,
                            IndentationKind::Preserve,
                            LineEnding::LF,
                            TrailingWhitespace::Preserve,
                            CategoryLayout::Inherit,
                            true,
                            NamespaceComments::Preserve,
                        },
                        {
                            "canonical-crlf",
                            LayoutMode::Canonical,
                            2,
                            IndentationKind::Spaces,
                            LineEnding::CRLF,
                            TrailingWhitespace::Preserve,
                            CategoryLayout::Inherit,
                            false,
                            NamespaceComments::Preserve,
                        },
                        {
                            "canonical-lf",
                            LayoutMode::Canonical,
                            2,
                            IndentationKind::Spaces,
                            LineEnding::LF,
                            TrailingWhitespace::Preserve,
                            CategoryLayout::Inherit,
                            false,
                            NamespaceComments::Preserve,
                        },
                        {
                            "composition-control-lf",
                            LayoutMode::Preserve,
                            4,
                            IndentationKind::Preserve,
                            LineEnding::LF,
                            TrailingWhitespace::Preserve,
                            CategoryLayout::Canonical,
                            false,
                            NamespaceComments::Preserve,
                        },
                        {
                            "opaque-lf",
                            LayoutMode::Preserve,
                            4,
                            IndentationKind::Preserve,
                            LineEnding::LF,
                            TrailingWhitespace::Preserve,
                            CategoryLayout::Inherit,
                            false,
                            NamespaceComments::Preserve,
                        },
                    },
                })
            {
                auto style = base;
                style.Layout = policy.Layout;
                style.IndentWidth = policy.IndentWidth;
                style.IndentationKind = policy.IndentationKind;
                style.LineEnding = policy.LineEnding;
                style.TrailingWhitespace = policy.TrailingWhitespace;
                style.ControlHeadersLayout = policy.ControlHeadersLayout;
                style.RemoveNullDeclarations = policy.RemoveNullDeclarations;
                style.NamespaceComments = policy.NamespaceComments;
                ApplyProfile(cases, directory, policy.Profile, style);
            }
        }
        ShareExpected(cases, directory, "boundary-namespace-lf.output.cpp", "boundary-namespace.input.cpp");
        ShareExpected(cases, directory, "legacy.output.cpp", "input.cpp");
    }
    AddCases(cases, data / "style-checkpoint", {"lf", "crlf"});
    {
        const auto directory = data / "style-checkpoint";
        {
            auto style = formatter::LoadConfigFile(directory / "lf.hit-fmt").FormattingStyle;
            style.LineEnding = LineEnding::CRLF;
            ApplyProfile(cases, directory, "crlf", style);
        }
    }
    AddCases(cases, data / "trailing-comment-count", {"preserve", "zero", "one", "two"});
    cases.push_back({data / "trailing-comment-count", "independent", "namespace.input.cpp"});
    {
        const auto directory = data / "trailing-comment-count";
        {
            const auto base = formatter::LoadConfigFile(directory / "one.hit-fmt").FormattingStyle;
            struct Policy
            {
                const char* Profile;
                formatter::NamespaceComments NamespaceComments;
                std::optional<formatter::TrailingCommentSpaceSetting>
                    TrailingCommentSpaces;
                std::size_t NamespaceCommentSpaces;
                formatter::GapPolicy TrailingCommentSpacing;
            };
            for (const auto& policy :
                std::array<Policy, 4>{
                    {
                        {
                            "independent",
                            NamespaceComments::Fix,
                            formatter::TrailingCommentSpaceSetting{2},
                            4,
                            GapPolicy::NoSpace,
                        },
                        {
                            "preserve",
                            NamespaceComments::Preserve,
                            formatter::TrailingCommentSpaceSetting{std::nullopt},
                            1,
                            GapPolicy::Space,
                        },
                        {
                            "two",
                            NamespaceComments::Preserve,
                            formatter::TrailingCommentSpaceSetting{2},
                            1,
                            GapPolicy::Space,
                        },
                        {
                            "zero",
                            NamespaceComments::Preserve,
                            formatter::TrailingCommentSpaceSetting{0},
                            1,
                            GapPolicy::Space,
                        },
                    },
                })
            {
                auto style = base;
                style.NamespaceComments = policy.NamespaceComments;
                style.TrailingCommentSpaces = policy.TrailingCommentSpaces;
                style.NamespaceCommentSpaces = policy.NamespaceCommentSpaces;
                style.TrailingCommentSpacing = policy.TrailingCommentSpacing;
                ApplyProfile(cases, directory, policy.Profile, style);
            }
        }
        ShareExpected(cases, directory, "preserve.output.cpp", "input.cpp");
    }
    AddCases(cases, data / "standalone-comment-gaps", {"preserve", "before", "after", "zero", "canonical"});
    {
        const auto directory = data / "standalone-comment-gaps";
        {
            const auto base = formatter::LoadConfigFile(directory / "zero.hit-fmt").FormattingStyle;
            struct Policy
            {
                const char* Profile;
                formatter::BlankLineSetting StandaloneCommentBlankLinesBefore;
                formatter::BlankLineSetting StandaloneCommentBlankLinesAfter;
            };
            for (const auto& policy :
                std::array<Policy, 2>{
                    {
                        {
                            "before",
                            formatter::BlankLineSetting{BlankLineMode::Exact, 1},
                            formatter::BlankLineSetting{BlankLineMode::Preserve, 0},
                        },
                        {
                            "after",
                            formatter::BlankLineSetting{BlankLineMode::Preserve, 0},
                            formatter::BlankLineSetting{BlankLineMode::Exact, 2},
                        },
                    },
                })
            {
                auto style = base;
                style.StandaloneCommentBlankLinesBefore = policy.StandaloneCommentBlankLinesBefore;
                style.StandaloneCommentBlankLinesAfter = policy.StandaloneCommentBlankLinesAfter;
                ApplyProfile(cases, directory, policy.Profile, style);
            }
        }
        ShareConfig(cases, directory, "preserve", "../closing-delimiters/preserve");
        ShareExpected(cases, directory, "preserve.output.cpp", "input.cpp");
        ShareExpected(cases, directory, "zero.output.cpp", "input.cpp");
    }
    AddCases(cases, BRACE_TESTDATA_DIR, {"preserve", "attach", "newline", "mixed"});
    AddCases(cases, BRACE_TESTDATA_DIR, {"last-line-indent"}, "last-line-indent.input.cpp");
    {
        const auto directory = std::filesystem::path(BRACE_TESTDATA_DIR);
        auto style = formatter::LoadConfigFile(directory / "last-line-indent.hit-fmt").FormattingStyle;
        style.ConstructorBracePlacement.reset();
        style.ParametersCloser = CloserPlacement::SameLine;
        style.ControlHeadersCloser = CloserPlacement::SameLine;
        style.IndentWidth = 2;
        style.ContinuationOffset = 4;
        style.IndentationKind = IndentationKind::Tabs;
        cases.push_back({directory, "last-line-indent-tabs", "last-line-indent.input.cpp"});
        cases.back().Style = style;
        style.IndentWidth = 8;
        style.IndentationKind = IndentationKind::Spaces;
        cases.push_back({directory, "last-line-indent-wide-body", "last-line-indent.input.cpp"});
        cases.back().Style = std::move(style);
        style = formatter::LoadConfigFile(directory / "last-line-indent.hit-fmt").FormattingStyle;
        style.ColumnLimit = 35;
        style.ParametersBreak = BreakRequirement::IfNeeded;
        style.ConstructorListsBreak = BreakRequirement::IfNeeded;
        style.ControlHeadersBreak = BreakRequirement::IfNeeded;
        cases.push_back({directory, "last-line-indent-reflow", "last-line-indent.input.cpp"});
        cases.back().Style = std::move(style);
    }
    AddCases(cases, BRACE_TESTDATA_DIR, {"namespace-indent"}, "namespace-indent.input.cpp");
    {
        const auto directory = std::filesystem::path(BRACE_TESTDATA_DIR);
        auto style = formatter::LoadConfigFile(directory / "namespace-indent.hit-fmt").FormattingStyle;
        style.IndentNamespaces = formatter::BodyIndentation::Preserve;
        cases.push_back({directory, "namespace-indent-preserve", "namespace-indent.input.cpp"});
        cases.back().Style = std::move(style);
    }
    AddCases(cases, data / "include-order", {"sort", "composed", "angled-first"});
    cases.push_back({data / "include-order", "crlf", "crlf.input.cpp", "sort"});
    {
        const auto directory = data / "include-order";
        for (const auto profile : {"groups-regroup", "groups-depth-one"}) {
            FormatCase value{directory, "angled-first-" + std::string(profile), "groups.input.cpp", profile};
            value.Style = formatter::LoadConfigFile(directory / (std::string(profile) + ".hit-fmt")).FormattingStyle;
            value.Style->IncludeCategoryOrder = formatter::IncludeCategoryOrder::AngledFirst;
            value.InputFilePath = "library/unit/widget.cpp";
            cases.push_back(std::move(value));
        }
        for (const auto name : {"directory", "matching", "prefixes"}) {
            for (const bool absolute : {false, true}) {
                FormatCase value{
                    directory,
                    std::string(name) + (absolute ? "-absolute-path" : "-dot-path"),
                    "directory.input.cpp",
                    name,
                };
                value.Expected = std::string(name) + ".output.cpp";
                value.InputFilePath = absolute
                    ? std::filesystem::absolute("project/engine/widget.cpp").generic_string()
                    : "./project/engine/widget.cpp";
                cases.push_back(std::move(value));
            }
        }
        for (const auto name :
            {
                "directory",
                "directory-alphabetical",
                "directory-depth-zero",
                "directory-depth-one",
                "directory-depth-two",
                "directory-depth-deep",
                "directory-preserve",
                "matching",
                "prefixes",
            })
        {
            FormatCase value{directory, name, "directory.input.cpp"};
            value.InputFilePath = "project/engine/widget.cpp";
            if (std::string_view(name) == "directory-preserve") {
                value.Expected = "directory.input.cpp";
            }
            cases.push_back(std::move(value));
        }
        AddCases(cases, directory, {"depth-zero", "depth-default", "depth-maximum"}, "depth.input.cpp");
        ShareExpected(cases, directory, "depth-default.output.cpp", "depth-maximum.output.cpp");
        AddCases(cases, directory, {"standard", "standard-preserve", "standard-adjacent"}, "standard.input.cpp");
        AddCases(cases, directory, {"standard-group"}, "standard.input.cpp");
        for (const auto profile :
            {"system-directory", "system-regroup", "system-configured", "system-adjacent", "header-names"})
        {
            FormatCase value{directory, profile, "system.input.cpp"};
            value.InputFilePath = "project/widget.cpp";
            cases.push_back(std::move(value));
        }
        for (const auto grouping : {formatter::IncludeGrouping::Directory, formatter::IncludeGrouping::Regroup}) {
            FormatCase value{
                directory,
                "standard-first-" + std::string(formatter::SerializeConfigEnum(grouping)),
                "system.input.cpp",
                "standard-first",
            };
            value.Style = formatter::LoadConfigFile(directory / "standard-first.hit-fmt").FormattingStyle;
            value.Style->IncludeGrouping = grouping;
            value.InputFilePath = "project/widget.cpp";
            cases.push_back(std::move(value));
        }
        for (const auto name : {"groups", "groups-reversed", "groups-regroup", "groups-depth-one"}) {
            FormatCase value{directory, name, "groups.input.cpp"};
            value.InputFilePath = "library/unit/widget.cpp";
            cases.push_back(std::move(value));
        }
        for (const auto name :
            {"prefix-depth-one", "prefix-depth-two", "prefix-depth-relative", "prefix-depth-reversed"})
        {
            FormatCase value{directory, name, "prefix-depth.input.cpp"};
            value.InputFilePath = "project/source/runner.cpp";
            cases.push_back(std::move(value));
        }
        for (const auto name : {"rules-nearest", "rules-alphabetical", "rules-regroup", "rules-full-suffix"}) {
            FormatCase value{directory, name, "rule-groups.input.cpp"};
            value.InputFilePath = "yt/yt/core/rpc/runner.cpp";
            cases.push_back(std::move(value));
        }
        for (const auto name :
            {"prefix-order-nearest", "prefix-order-alphabetical", "prefix-order-reversed", "prefix-order-regroup"})
        {
            FormatCase value{directory, name, "prefix-order.input.cpp"};
            value.InputFilePath = "library/z/runner.cpp";
            cases.push_back(std::move(value));
        }
        for (const auto name : {"priorities-foo", "priorities-bar"}) {
            FormatCase value{directory, name, "priorities.input.cpp", "priorities"};
            value.InputFilePath =
                std::string_view(name) == "priorities-foo" ? "foo/bar/library/converter.cpp" : "bar/libs/example.h";
            cases.push_back(std::move(value));
        }
        for (const auto grouping : {formatter::IncludeGrouping::Directory, formatter::IncludeGrouping::Regroup}) {
            for (const auto order : {formatter::IncludeGroupOrder::Directory, formatter::IncludeGroupOrder::Configured})
            {
                FormatCase value{
                    directory,
                    "priorities-" + std::string(formatter::SerializeConfigEnum(grouping)) + "-" +
                        std::string(formatter::SerializeConfigEnum(order)),
                    "prefix-order.input.cpp",
                    "prefix-order-nearest",
                };
                value.Style = formatter::LoadConfigFile(directory / "prefix-order-nearest.hit-fmt").FormattingStyle;
                value.Expected = "priorities-" + std::string(formatter::SerializeConfigEnum(grouping)) + ".output.cpp";
                value.Style->IncludeGrouping = grouping;
                value.Style->IncludeGroupOrder = order;
                value.Style->IncludeGroups[0].Priority = 2;
                value.Style->IncludeGroups[1].Priority = -1;
                value.InputFilePath = "library/z/runner.cpp";
                cases.push_back(std::move(value));
            }
        }
        for (const auto profile : {"prefix-order-nearest", "prefix-order-regroup", "matching"}) {
            FormatCase value{directory, "in-group-" + std::string(profile), "prefix-order.input.cpp", profile};
            value.Style = formatter::LoadConfigFile(directory / (std::string(profile) + ".hit-fmt")).FormattingStyle;
            value.Style->IncludeInGroupOrder = formatter::IncludeInGroupOrder::NearestFirst;
            value.InputFilePath = "library/core/runner.cpp";
            cases.push_back(std::move(value));
        }
        for (const auto profile : {"directory-relative-depth-zero", "directory-relative-depth-two"}) {
            FormatCase value{directory, "in-group-" + std::string(profile), "relative.input.cpp", profile};
            value.Style = formatter::LoadConfigFile(directory / (std::string(profile) + ".hit-fmt")).FormattingStyle;
            value.Style->IncludeInGroupOrder = formatter::IncludeInGroupOrder::NearestFirst;
            value.InputFilePath = "/project/engine/widget.cpp";
            cases.push_back(std::move(value));
        }
        for (const bool sourceRelative : {false, true}) {
            FormatCase value{
                directory,
                sourceRelative ? "in-group-roots-source" : "in-group-roots",
                "in-group-roots.input.cpp",
                "in-group-roots",
            };
            if (sourceRelative) {
                value.Style = formatter::LoadConfigFile(directory / "in-group-roots.hit-fmt").FormattingStyle;
                value.Style->IncludePathBase = formatter::IncludePathBase::SourceDirectory;
            }
            value.InputFilePath = "/project/engine/widget.cpp";
            cases.push_back(std::move(value));
        }
        for (const auto profile : {"directory", "matching"}) {
            FormatCase value{
                directory,
                "configured-" + std::string(profile),
                "directory.input.cpp",
                profile,
                {},
                true,
                std::string(profile) + ".output.cpp",
            };
            value.Style = formatter::LoadConfigFile(directory / (std::string(profile) + ".hit-fmt")).FormattingStyle;
            value.Style->IncludeGroupOrder = formatter::IncludeGroupOrder::Configured;
            // Preserve must ignore both grouping rules and their ordering.
            if (std::string_view(profile) == "matching") {
                value.Style->IncludeGroups = {{{"project/"}, 1, -1}};
            }
            value.InputFilePath = "project/engine/widget.cpp";
            cases.push_back(std::move(value));
        }
        for (const auto name : {"rule-order", "rule-order-reversed"}) {
            FormatCase value{directory, name, "rule-order.input.cpp"};
            value.InputFilePath = "source/runner.cpp";
            if (std::string_view(name) == "rule-order-reversed") {
                value.Expected = "rule-order.output.cpp";
            }
            cases.push_back(std::move(value));
        }
        for (const auto name : {"directory-relative", "directory-relative-depth-zero", "directory-relative-depth-two"})
        {
            FormatCase relative{directory, name, "relative.input.cpp"};
            relative.InputFilePath = "/project/engine/widget.cpp";
            cases.push_back(std::move(relative));
        }
        for (const auto profile : {"google", "llvm", "ytsaurus"}) {
            FormatCase value{
                directory,
                "profile-" + std::string(profile),
                "directory.input.cpp",
                "../../../../hit-fmt/examples/" + std::string(profile),
            };
            value.InputFilePath = "project/engine/widget.cpp";
            if (std::string_view(profile) != "ytsaurus") {
                value.Expected =
                    std::string_view(profile) == "google" ? "matching.output.cpp" : "directory-depth-one.output.cpp";
            }
            cases.push_back(std::move(value));
        }
        FormatCase ytsaurusRoots{
            directory,
            "profile-ytsaurus-roots",
            "rule-groups.input.cpp",
            "../../../../hit-fmt/examples/ytsaurus",
        };
        ytsaurusRoots.InputFilePath = "yt/yt/core/rpc/runner.cpp";
        cases.push_back(std::move(ytsaurusRoots));
        cases.push_back({directory, "directory-no-path", "directory.input.cpp", "directory"});
        cases.push_back({directory, "directory-crlf", "crlf.input.cpp", "directory-relative"});
        FormatCase header{directory, "directory-header", "directory.input.cpp", "directory"};
        header.InputFilePath = "project/engine/widget.hpp";
        cases.push_back(std::move(header));
        for (const bool spill : {false, true}) {
            FormatCase value{
                directory,
                spill ? "directory-spill" : "directory-strict",
                "relative.input.cpp",
                "directory-relative",
                {{1, 1}},
                spill,
            };
            value.InputFilePath = "/project/engine/widget.cpp";
            cases.push_back(std::move(value));
        }
    }
    AddCases(cases, BRACE_SPACING_TESTDATA_DIR, {"preserve", "single-space", "mixed"});
    AddCases(cases, LAMBDA_SPACING_TESTDATA_DIR, {"preserve", "space", "no-space"});
    AddCases(cases, CONTROL_SPACING_TESTDATA_DIR, {"preserve", "space", "no-space"});
    AddCases(cases, CONTROL_BRACE_TESTDATA_DIR, {"preserve", "same-line", "new-line", "attach-unless-multiline"});
    cases.push_back({CONTROL_BRACE_TESTDATA_DIR, "reflow-preferred", "reflow-preferred.input.cpp"});
    AddCases(cases, CONTROL_BRACE_TESTDATA_DIR, {"combined"}, "combined.input.cpp");
    AddCases(cases, FUNCTION_SPACING_TESTDATA_DIR, {"preserve", "space", "no-space"});
    AddCases(cases, FUNCTION_SPACING_TESTDATA_DIR, {"reflow"}, "reflow.input.cpp");
    AddCases(cases, FUNCTION_CALL_SPACING_TESTDATA_DIR, {"preserve", "space", "no-space"});
    AddCases(cases, FUNCTION_CALL_SPACING_TESTDATA_DIR, {"combined"}, "combined.input.cpp");
    AddCases(cases, FUNCTION_INNER_SPACING_TESTDATA_DIR, {"preserve", "space", "no-space"});
    AddCases(cases, FUNCTION_INNER_SPACING_TESTDATA_DIR, {"reflow"}, "reflow.input.cpp");
    AddCases(cases, CALL_INNER_SPACING_TESTDATA_DIR, {"preserve", "space", "no-space"});
    cases.push_back({CALL_INNER_SPACING_TESTDATA_DIR, "reflow-preferred", "reflow-preferred.input.cpp"});
    cases.push_back({CALL_INNER_SPACING_TESTDATA_DIR, "comma-preferred", "comma-preferred.input.cpp"});
    AddCases(cases, CALL_INNER_SPACING_TESTDATA_DIR, {"combined"}, "combined.input.cpp");
    AddCases(cases, LAMBDA_CAPTURE_TESTDATA_DIR, {"preserve", "space", "no-space", "mixed", "reverse"});
    {
        const auto directory = std::filesystem::path(LAMBDA_CAPTURE_TESTDATA_DIR);
        const auto style = formatter::LoadConfigFile(directory / "../style/repository-80.hit-fmt").FormattingStyle;
        for (const auto name : {"breaks", "breaks-always", "breaks-preserve"}) {
            FormatCase value{directory, name, "breaks.input.cpp"};
            value.Style = style;
            if (std::string_view(name) == "breaks-always") {
                value.Style->CapturesBreak = BreakRequirement::Always;
            } else if (std::string_view(name) == "breaks-preserve") {
                value.Style->CapturesLayout = CategoryLayout::Preserve;
            }
            cases.push_back(std::move(value));
        }
    }
    AddCases(cases, LAMBDA_INNER_PARAMETER_TESTDATA_DIR, {"preserve", "space", "no-space", "combined"});
    {
        const auto directory = std::filesystem::path(LAMBDA_INNER_PARAMETER_TESTDATA_DIR);
        const auto style = formatter::LoadConfigFile(directory / "../style/repository-80.hit-fmt").FormattingStyle;
        for (const auto name : {"breaks", "breaks-narrow", "breaks-preserve"}) {
            FormatCase value{directory, name, "breaks.input.cpp"};
            value.Style = style;
            if (std::string_view(name) == "breaks-narrow") {
                value.Style->ColumnLimit = 60;
            } else if (std::string_view(name) == "breaks-preserve") {
                value.Style->ParametersLayout = CategoryLayout::Preserve;
            }
            cases.push_back(std::move(value));
        }
    }
    AddCases(cases, BODY_INDENT_TESTDATA_DIR, {"preserve", "indent", "no-indent", "mixed"});
    AddCases(
        cases,
        BODY_INDENT_TESTDATA_DIR,
        {
            "namespace-preserved-indent",
            "namespace-preserved-no-indent",
            "namespace-preserved-layout",
            "namespace-preserved-vertical",
        },
        "namespace-preserved.input.cpp");
    cases.push_back({
        BODY_INDENT_TESTDATA_DIR,
        "namespace-preserved-tabs",
        "namespace-preserved-tabs.input.cpp",
        "namespace-preserved-indent",
    });
    cases.push_back({BODY_INDENT_TESTDATA_DIR, "reindented-call", "reindented-call.input.cpp"});
    cases.push_back({BODY_INDENT_TESTDATA_DIR, "namespace-preserved-lambda", "namespace-preserved-lambda.input.cpp"});
    cases.push_back({BODY_INDENT_TESTDATA_DIR, "namespace-template-lambda", "namespace-template-lambda.input.cpp"});
    const auto nullStatements = data / "null-statements";
    AddCases(cases, nullStatements, {"preserve", "remove"});
    for (const auto name :
        {"whitespace-trim", "whitespace-preserve", "whitespace-trim-crlf", "whitespace-trim-preserve-indent"})
    {
        FormatCase value{nullStatements, name, "whitespace.input.cpp", "whitespace-trim"};
        value.Style = formatter::LoadConfigFile(nullStatements / "whitespace-trim.hit-fmt").FormattingStyle;
        if (std::string_view(name) == "whitespace-preserve") {
            value.Style->TrailingWhitespace = TrailingWhitespace::Preserve;
        } else if (std::string_view(name) == "whitespace-trim-crlf") {
            value.Input = "whitespace-crlf.input.cpp";
            value.Style->LineEnding = LineEnding::CRLF;
        } else if (std::string_view(name) == "whitespace-trim-preserve-indent") {
            value.Style->IndentationKind = IndentationKind::Preserve;
        }
        cases.push_back(std::move(value));
    }
    for (const auto& [name, config] :
        {
            std::pair{"decl-preserve", "preserve"},
            std::pair{"decl-statements", "remove"},
            std::pair{"decl-only", "declarations"},
            std::pair{"decl-both", "both"},
        })
    {
        cases.push_back({nullStatements, name, "declarations.input.cpp", config});
    }
    AddCases(cases, BODY_INDENT_TESTDATA_DIR, {"grouped-preserve", "grouped-strict"}, "grouped-statements.input.cpp");
    {
        const auto directory = std::filesystem::path(BODY_INDENT_TESTDATA_DIR);
        const auto base = formatter::LoadConfigFile(directory / "grouped-preserve.hit-fmt").FormattingStyle;
        for (const auto name :
            {
                "split-scopes",
                "split-scopes-indent",
                "split-scopes-no-indent",
                "split-scopes-tabs",
                "split-scopes-generated",
            })
        {
            FormatCase value{directory, name, "split-scopes.input.cpp"};
            value.Style = base;
            const std::string_view variant = name;
            if (variant == "split-scopes-indent") {
                value.Style->IndentCaseLabels = formatter::BodyIndentation::Indent;
            } else if (variant == "split-scopes-no-indent") {
                value.Style->IndentCaseLabels = formatter::BodyIndentation::NoIndent;
            } else if (variant == "split-scopes-tabs") {
                value.Style->IndentationKind = IndentationKind::Tabs;
            } else if (variant == "split-scopes-generated") {
                value.Style->GenerateCallScopeIndentation = true;
            }
            cases.push_back(std::move(value));
        }
    }
    AddCases(
        cases,
        BODY_INDENT_TESTDATA_DIR,
        {"lambda-comments-follow", "lambda-comments-preserve"},
        "lambda-leading-comments.input.cpp");
    const auto chains = data / "chained-calls";
    AddCases(cases, chains, {"always", "continuation", "preserve"});
    cases.push_back({chains, "overflow", "overflow.input.cpp"});
    AddCases(cases, chains, {"moved-root", "moved-root-preserve", "moved-root-tabs"}, "moved-root.input.cpp");
    cases.push_back({chains, "child-fitting", "child-fitting.input.cpp"});
    for (const auto& [name, columns] : {std::pair{"field-assignment", 120}, {"field-assignment-narrow", 40}}) {
        cases.push_back({chains, name, "field-assignment.input.cpp", "child-fitting", {}, true, {}});
        cases.back().Style = formatter::LoadConfigFile(chains / "child-fitting.hit-fmt").FormattingStyle;
        cases.back().Style->ColumnLimit = columns;
        cases.back().Style->ChainsBreak = BreakRequirement::Always;
        cases.back().Style->ChainsPacking = ListPacking::VerticalWhenBroken;
    }
    cases.push_back({chains, "comparison-fitting", "comparison-fitting.input.cpp", "child-fitting"});
    cases.push_back({chains, "structural-base", "structural-base.input.cpp"});
    for (const auto* profile : {"template-roots-selected", "template-roots-forced", "template-roots-preserve"}) {
        cases.push_back({chains, profile, "template-roots.input.cpp"});
        auto style = formatter::LoadConfigFile(chains / "conventional.hit-fmt").FormattingStyle;
        style.AlwaysBreakChainRootNames.clear();
        style.ChainedCallRootNames = {"factory", "ns::Builder::Make"};
        style.ChainsBreak = BreakRequirement::Always;
        if (std::string_view(profile) == "template-roots-forced") {
            style.ChainsBreak = BreakRequirement::IfNeeded;
            style.AlwaysBreakChainRootNames = style.ChainedCallRootNames;
        } else if (std::string_view(profile) == "template-roots-preserve") {
            style.ChainsLayout = CategoryLayout::Preserve;
        }
        cases.back().Style = std::move(style);
    }
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
    AddCases(cases, data / "separator-length", {"preserve"});
    AddCases(cases, data / "standalone-block-indent", {"preserve", "indent"});
    const auto closings = data / "closing-delimiters";
    AddCases(cases, closings, {"same-line", "block", "preserve"});
    const auto initializerSafety = data / "initializer-safety";
    AddCases(cases, initializerSafety, {"conditional", "never", "preserve"});
    {
        const auto directory = data / "initializer-safety";
        {
            const auto base = formatter::LoadConfigFile(directory / "conditional.hit-fmt").FormattingStyle;
            struct Policy
            {
                const char* Profile;
                formatter::TrailingCommaPolicy InitializersTrailingComma;
            };
            for (const auto& policy :
                std::array<Policy, 2>{
                    {{"never", TrailingCommaPolicy::Never}, {"preserve", TrailingCommaPolicy::Preserve}},
                })
            {
                auto style = base;
                style.InitializersTrailingComma = policy.InitializersTrailingComma;
                ApplyProfile(cases, directory, policy.Profile, style);
            }
        }
    }
    AddCases(cases, data / "declaration-gap", {"canonical", "preserve"});
    {
        const auto directory = data / "declaration-gap";
        ShareExpected(cases, directory, "preserve.output.cpp", "input.cpp");
    }
    const auto ownerContext = data / "owner-context";
    AddCases(
        cases,
        ownerContext,
        {"canonical-protected", "preserve-protected", "canonical-safe", "explicit-protected"});
    {
        const auto directory = data / "owner-context";
        {
            const auto base = formatter::LoadConfigFile(directory / "canonical-protected.hit-fmt").FormattingStyle;
            struct Policy
            {
                const char* Profile;
                formatter::CategoryLayout CallsLayout;
                std::vector<std::string> WhitespaceSensitiveMacros;
                formatter::GapPolicy SpaceInsideCallArguments;
                formatter::GapPolicy CommaSpaceBefore;
                formatter::GapPolicy CommaSpaceAfter;
            };
            for (const auto& policy :
                std::array<Policy, 3>{
                    {
                        {
                            "canonical-safe",
                            CategoryLayout::Canonical,
                            {},
                            GapPolicy::GeneratedNoSpace,
                            GapPolicy::GeneratedNoSpace,
                            GapPolicy::GeneratedSpace,
                        },
                        {
                            "explicit-protected",
                            CategoryLayout::Canonical,
                            {"KEEP"},
                            GapPolicy::Space,
                            GapPolicy::NoSpace,
                            GapPolicy::Space,
                        },
                        {
                            "preserve-protected",
                            CategoryLayout::Preserve,
                            {"KEEP"},
                            GapPolicy::GeneratedNoSpace,
                            GapPolicy::GeneratedNoSpace,
                            GapPolicy::GeneratedSpace,
                        },
                    },
                })
            {
                auto style = base;
                style.CallsLayout = policy.CallsLayout;
                style.WhitespaceSensitiveMacros = policy.WhitespaceSensitiveMacros;
                style.SpaceInsideCallArguments = policy.SpaceInsideCallArguments;
                style.CommaSpaceBefore = policy.CommaSpaceBefore;
                style.CommaSpaceAfter = policy.CommaSpaceAfter;
                ApplyProfile(cases, directory, policy.Profile, style);
            }
        }
        ShareExpected(cases, directory, "preserve-protected.output.cpp", "input.cpp");
    }
    const auto recordHead = data / "record-head-commas";
    AddCases(cases, recordHead, {"space", "no-space", "preserve", "generated-canonical", "generated-preserve"});
    for (const auto* profile : {"space", "no-space", "preserve", "generated-canonical", "generated-preserve"}) {
        cases.push_back({recordHead, std::string("comparisons-") + profile, "comparisons.input.cpp", profile});
    }
    for (const auto* profile : {"space", "generated-preserve"}) {
        cases.push_back({
            recordHead,
            std::string("constructor-composition-") + profile,
            "constructor-composition.input.cpp",
            profile,
        });
    }
    {
        const auto directory = data / "record-head-commas";
        {
            const auto base = formatter::LoadConfigFile(directory / "space.hit-fmt").FormattingStyle;
            struct Policy
            {
                const char* Profile;
                formatter::LayoutMode Layout;
                formatter::GapPolicy CommaSpaceAfter;
            };
            for (const auto& policy :
                std::array<Policy, 4>{
                    {
                        {"generated-canonical", LayoutMode::Canonical, GapPolicy::GeneratedSpace},
                        {"generated-preserve", LayoutMode::Preserve, GapPolicy::GeneratedSpace},
                        {"no-space", LayoutMode::Canonical, GapPolicy::NoSpace},
                        {"preserve", LayoutMode::Canonical, GapPolicy::Preserve},
                    },
                })
            {
                auto style = base;
                style.Layout = policy.Layout;
                style.CommaSpaceAfter = policy.CommaSpaceAfter;
                ApplyProfile(cases, directory, policy.Profile, style);
            }
        }
        ShareExpected(cases, directory, "comparisons-space.output.cpp", "comparisons-generated-canonical.output.cpp");
        ShareExpected(cases, directory, "space.output.cpp", "generated-canonical.output.cpp");
        ShareExpected(
            cases,
            directory,
            "constructor-composition-generated-preserve.output.cpp",
            "constructor-composition.input.cpp");
        ShareExpected(cases, directory, "generated-preserve.output.cpp", "input.cpp");
        ShareExpected(cases, directory, "comparisons-generated-preserve.output.cpp", "comparisons.input.cpp");
    }
    cases.push_back({closings, "reflow-preferred", "reflow-preferred.input.cpp"});
    cases.push_back({closings, "generated-block", "generated-block.input.cpp"});
    cases.push_back({closings, "layout-preferred", "layout-preferred.input.cpp"});
    cases.push_back({closings, "new-line", "new-line.input.cpp"});
    cases.push_back({closings, "conditional-preferred", "trailing-comma.input.cpp"});
    cases.push_back({closings, "remove", "trailing-comma.input.cpp"});
    cases.push_back({closings, "trailing-add", "trailing-layout.input.cpp"});
    cases.push_back({closings, "trailing-conditional", "trailing-layout.input.cpp"});
    cases.push_back({closings, "trailing-preferred", "trailing-preferred.input.cpp"});
    cases.push_back({closings, "trailing-removed", "trailing-removed.input.cpp"});
    cases.push_back({closings, "trailing-only", "trailing-layout.input.cpp"});
    cases.push_back({closings, "trailing-new-line", "trailing-layout.input.cpp"});
    AddCases(cases, data / "fold-spacing", {"yt-like", "inherited", "group-space"});
    const auto profile = (data / "../../../hit-fmt/examples/ytsaurus").lexically_normal().string();
    auto addProfileCase = [&] (const char* directory, const char* name) {
        cases.push_back({
            std::filesystem::path(FORMATTER_TESTDATA_DIR) / directory,
            name,
            std::string(name) + ".input.cpp",
            profile,
        });
    };
    addProfileCase("../ytsaurus-corpus", "vertical");
    {
        auto continuation = cases.back();
        continuation.Name = "vertical-continuation";
        continuation.Style = formatter::LoadConfigFile(profile + ".hit-fmt").FormattingStyle;
        continuation.Style->ContinuationOffset = 6;
        cases.push_back(std::move(continuation));
    }
    addProfileCase("../ytsaurus-corpus", "includes");
    cases.back().InputFilePath = "yt/yt/core/concurrency/worker.cpp";
    addProfileCase("../ytsaurus-corpus", "expressions");
    addProfileCase("../ytsaurus-corpus", "macros");
    addProfileCase("../preprocessing-fragments", "opaque-blocks");
    addProfileCase("../preprocessing-fragments", "canonical");
    addProfileCase("../preprocessing-fragments", "shared-scopes");
    addProfileCase("../preprocessing-fragments", "case-comments");
    addProfileCase("../preprocessing-fragments", "case-blocks");
    for (const auto* name : {"case-blocks-next-line", "case-blocks-preserve"}) {
        auto value = cases.back();
        value.Name = name;
        value.Style = formatter::LoadConfigFile(profile + ".hit-fmt").FormattingStyle;
        if (value.Name == "case-blocks-next-line") {
            value.Style->CaseBlockBracePlacement = BracePlacement::NextLine;
        } else {
            value.Style->ControlHeadersLayout = CategoryLayout::Preserve;
            value.Style->BodiesLayout = CategoryLayout::Preserve;
        }
        cases.push_back(std::move(value));
    }
    addProfileCase("../spacing-options", "profile-constraints");
    for (const auto* name : {"profile-constraints-leading", "profile-constraints-preserve"}) {
        auto value = cases.back();
        value.Name = name;
        value.Style = formatter::LoadConfigFile(profile + ".hit-fmt").FormattingStyle;
        if (value.Name == "profile-constraints-leading") {
            value.Style->BinaryOperatorSide = OperatorSide::Leading;
        } else {
            value.Style = formatter::Style{};
            value.Expected = "profile-constraints.input.cpp";
        }
        cases.push_back(std::move(value));
    }
    addProfileCase("../spacing-options", "profile-initializers");
    AddCases(
        cases,
        data / "spacing-options",
        {"designated-assignment", "designated-assignment-no-space", "designated-assignment-preserve"},
        "designated-assignment.input.cpp");
    addProfileCase("../spacing-options", "profile-overflow");
    addProfileCase("../spacing-options", "profile-case-blocks");
    addProfileCase("../spacing-options", "profile-ref-qualifiers");
    addProfileCase("../spacing-options", "profile-template-punctuation");
    addProfileCase("../declaration-headers", "function-types");
    {
        auto forced = cases.back();
        auto preserved = cases.back();
        forced.Name = "function-types-forced-template";
        forced.Style = formatter::LoadConfigFile(profile + ".hit-fmt").FormattingStyle;
        forced.Style->TemplateArgumentsBreak = BreakRequirement::Always;
        cases.push_back(std::move(forced));
        preserved.Name = "function-types-preserve";
        preserved.Style = formatter::Style{};
        preserved.Expected = preserved.Input;
        cases.push_back(std::move(preserved));
    }
    cases.push_back({data / "declaration-headers", "profile", "profile.input.cpp", profile});
    AddCases(
        cases,
        data / "spacing-options",
        {"function-cv-ref-space", "function-cv-ref-no-space", "function-cv-ref-preserve", "function-cv-ref-inherit"},
        "profile-ref-qualifiers.input.cpp");
    {
        const auto directory = data / "spacing-options";
        {
            const auto base = formatter::LoadConfigFile(directory / "function-cv-ref-space.hit-fmt").FormattingStyle;
            struct Policy
            {
                const char* Profile;
                formatter::GapPolicy FunctionRefQualifierSpacing;
                std::optional<formatter::GapPolicy>
                    FunctionCvRefQualifierSpacing;
            };
            for (const auto& policy :
                std::array<Policy, 3>{
                    {
                        {"function-cv-ref-inherit", GapPolicy::Space, std::nullopt},
                        {"function-cv-ref-no-space", GapPolicy::Space, GapPolicy::NoSpace},
                        {"function-cv-ref-preserve", GapPolicy::Space, GapPolicy::Preserve},
                    },
                })
            {
                auto style = base;
                style.FunctionRefQualifierSpacing = policy.FunctionRefQualifierSpacing;
                style.FunctionCvRefQualifierSpacing = policy.FunctionCvRefQualifierSpacing;
                ApplyProfile(cases, directory, policy.Profile, style);
            }
        }
    }
    cases.push_back({
        data / "spacing-options",
        "initializer-overflow-one-per-line",
        "initializer-overflow-one-per-line.input.cpp",
    });
    addProfileCase("../ternary-layout", "profile");
    AddCases(
        cases,
        data / "ternary-layout",
        {
            "branches-preserve",
            "branches-narrow",
            "branches-indent-preserve",
            "branches-indent-narrow",
            "branches-indent-trailing",
        },
        "branches.input.cpp");
    AddCases(cases, data / "ternary-layout", {"operators-align", "operators-indent"}, "operators.input.cpp");
    cases.push_back({data / "ternary-layout", "reflow-preferred", "reflow-preferred.input.cpp"});
    AddCases(
        cases,
        data / "ternary-layout",
        {"initializer-shape", "initializer-shape-flexible", "initializer-shape-narrow"},
        "initializer-shape.input.cpp");
    addProfileCase("../assignment-location", "profile");
    addProfileCase("../function_parameters", "profile");
    addProfileCase("../body-indent", "profile-control-local");
    // Canonical child lists expose normalization and indentation in each body scope.
    for (const auto* name : {"profile-direct-initializer", "profile-leading-initializer"}) {
        addProfileCase("../body-indent", name);
        cases.back().Style = formatter::LoadConfigFile(profile + ".hit-fmt").FormattingStyle;
        cases.back().Style->InitializersLayout = CategoryLayout::Canonical;
    }
    cases.push_back({
        std::filesystem::path(BODY_INDENT_TESTDATA_DIR),
        "leading-initializer-preserve",
        "profile-leading-initializer.input.cpp",
    });
    cases.back().Style = formatter::Style{};
    cases.back().Expected = "profile-leading-initializer.input.cpp";
    addProfileCase("../body-indent", "profile-case-comments");
    {
        const auto directory = std::filesystem::path(BODY_INDENT_TESTDATA_DIR);
        const auto base = formatter::LoadConfigFile(profile + ".hit-fmt").FormattingStyle;
        for (const auto label : {formatter::BodyIndentation::NoIndent, formatter::BodyIndentation::Preserve}) {
            cases.push_back({
                directory,
                label == formatter::BodyIndentation::NoIndent ? "case-comments-no-indent" : "case-comments-preserve",
                "profile-case-comments.input.cpp",
            });
            cases.back().Style = base;
            cases.back().Style->IndentCaseLabels = label;
        }
    }
    addProfileCase("../body-indent", "profile-unknown-statements");
    {
        const auto directory = std::filesystem::path(BODY_INDENT_TESTDATA_DIR);
        const auto input = "profile-unknown-statements.input.cpp";
        cases.push_back({directory, "unknown-statements-default", input});
        cases.back().Style = formatter::Style{};
        cases.back().Expected = input;
        for (const auto indentation : {IndentationKind::Spaces, IndentationKind::Tabs}) {
            auto style = formatter::LoadConfigFile(profile + ".hit-fmt").FormattingStyle;
            style.Layout = LayoutMode::Preserve;
            style.IndentationKind = indentation;
            cases.push_back({
                directory,
                indentation == IndentationKind::Spaces ? "unknown-statements-preserved" : "unknown-statements-tabs",
                input,
            });
            cases.back().Style = style;
        }
    }
    cases.push_back({data / "null-statements", "profile", "input.cpp", profile});
    cases.push_back({BODY_INDENT_TESTDATA_DIR, "grouped-profile", "grouped-statements.input.cpp", profile});
    cases
        .push_back({BODY_INDENT_TESTDATA_DIR, "lambda-comments-profile", "lambda-leading-comments.input.cpp", profile});
    cases.push_back({BODY_INDENT_TESTDATA_DIR, "lambda-trailing-return", "lambda-trailing-return.input.cpp"});
    cases.push_back({BODY_INDENT_TESTDATA_DIR, "lambda-wrapped-parameters", "lambda-wrapped-parameters.input.cpp"});
    addProfileCase("../lambda-captures", "profile");
    for (const auto* name : {"generic-header", "generic-header-fitting", "generic-header-forced"}) {
        addProfileCase("../lambda-captures", name);
        cases.back().Input = "generic-header.input.cpp";
        auto style = formatter::LoadConfigFile(profile + ".hit-fmt").FormattingStyle;
        style.ColumnLimit = std::string_view(name) == "generic-header-fitting" ? 120 : 80;
        if (std::string_view(name) == "generic-header-forced") {
            style.CapturesBreak = BreakRequirement::Always;
        }
        cases.back().Style = style;
    }
    {
        const auto directory = data / "lambda-captures";
        const auto base = formatter::LoadConfigFile(std::filesystem::path(profile + ".hit-fmt")).FormattingStyle;
        for (const auto* name : {"call-header-compact", "call-header-expanded", "call-header-preserve"}) {
            cases.push_back({directory, name, "call-header.input.cpp"});
            auto style = base;
            style.ColumnLimit = 80;
            style.CapturesCloser = CloserPlacement::SameLine;
            style.LambdaBodiesLayout = CategoryLayout::Canonical;
            style.LambdaBodyShape.reset();
            style.BodyShape = BodyShape::CompactIfFits;
            if (std::string_view(name) == "call-header-expanded") {
                style.BodyShape = BodyShape::Expanded;
            } else if (std::string_view(name) == "call-header-preserve") {
                style.CallsLayout = CategoryLayout::Preserve;
            }
            ApplyProfile(cases, directory, name, style);
        }
    }
    addProfileCase("../call-reflow", "profile-tabbed-log");
    const auto callReflow = data / "call-reflow";
    AddCases(
        cases,
        data / "argument-comments",
        {"compact-labels", "spaced-labels", "preserve-labels", "generated", "generated-preserve"});
    cases.push_back({
        callReflow,
        "template-argument-tail",
        "template-argument-tail.input.cpp",
        "../style/repository-80",
        {},
        true,
        {},
    });
    cases.push_back({
        callReflow,
        "template-argument-tail-fill",
        "template-argument-tail.input.cpp",
        "../style/repository-80",
        {},
        true,
        {},
    });
    auto templateTailStyle = formatter::LoadConfigFile(callReflow / "../style/repository-80.hit-fmt").FormattingStyle;
    templateTailStyle.CallsPacking = ListPacking::Fill;
    cases.back().Style = templateTailStyle;
    AddCases(cases, callReflow, {"sole-call-line", "sole-new-line"}, "sole-position.input.cpp");
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
    cases.push_back({callReflow, "sole-argument-ytsaurus", "sole-argument.input.cpp", profile});
    cases.push_back({callReflow, "sole-argument-repository", "sole-argument.input.cpp", "../style/repository-80"});
    cases.push_back({callReflow, "sole-preferred", "sole-preferred.input.cpp"});
    for (const auto* name :
        {
            "comment-elements",
            "comment-elements-space",
            "comment-elements-preserve",
            "comment-elements-tabs",
            "comment-elements-delimiter",
            "comment-elements-preserve-indent",
        })
    {
        cases.push_back({callReflow, name, "comment-elements.input.cpp", "comment-elements", {}, true, {}});
        auto style = formatter::LoadConfigFile(callReflow / "comment-elements.hit-fmt").FormattingStyle;
        const std::string_view profile(name);
        if (profile == "comment-elements-space") {
            style.BraceInitializerSpacing = GapPolicy::Space;
        } else if (profile == "comment-elements-preserve") {
            style.Layout = LayoutMode::Preserve;
            style.ArgumentCommentSpacing = GapPolicy::Preserve;
        } else if (profile == "comment-elements-tabs") {
            style.IndentationKind = IndentationKind::Tabs;
        } else if (profile == "comment-elements-delimiter") {
            style.ListAlignment = ContinuationAlignment::Delimiter;
        } else if (profile == "comment-elements-preserve-indent") {
            style.IndentationKind = IndentationKind::Preserve;
        }
        cases.back().Style = std::move(style);
    }
    for (const auto* config :
        {"label-space", "label-no-space", "label-generated-space", "label-generated-no-space", "label-preserve"})
    {
        const std::string_view policy(config);
        const bool generated = policy.starts_with("label-generated-");
        const auto spacing = formatter::LoadConfigFile(
            callReflow / (std::string(config) + ".hit-fmt")).FormattingStyle.ArgumentCommentSpacing;
        for (const auto layout : {LayoutMode::Canonical, LayoutMode::Preserve}) {
            const bool preserve = layout == LayoutMode::Preserve;
            if (preserve && spacing == GapPolicy::Preserve) {
                continue;
            }
            const auto name = "comment-elements-" + std::string(config) + (preserve ? "-preserve" : "");
            std::string expected;
            if (generated) {
                expected = preserve
                    ? "comment-elements-preserve.output.cpp"
                    : policy.ends_with("no-space")
                        ? "comment-elements-label-no-space.output.cpp"
                        : "comment-elements-label-space.output.cpp";
            }
            cases.push_back({callReflow, name, "comment-elements.input.cpp", "comment-elements", {}, true, expected});
            auto style = formatter::LoadConfigFile(callReflow / "comment-elements.hit-fmt").FormattingStyle;
            style.Layout = layout;
            style.ArgumentCommentSpacing = spacing;
            cases.back().Style = std::move(style);
        }
    }
    cases.push_back({callReflow, "first-position-reflow", "first-position-reflow.input.cpp"});
    cases.push_back({callReflow, "first-string-inline", "first-string-inline.input.cpp"});
    cases.push_back({data / "function_parameters", "first-position-reflow", "first-position-reflow.input.cpp"});
    {
        const auto directory = data / "function_parameters";
        const auto base = formatter::LoadConfigFile(directory / "../style/repository-80.hit-fmt").FormattingStyle;
        for (const auto name :
            {"default-wrap", "default-leading", "default-next-line", "default-preserve", "default-aligned"})
        {
            FormatCase value{directory, name, "default.input.cpp"};
            value.Style = base;
            value.Style->ColumnLimit = 60;
            if (std::string_view(name) == "default-leading") {
                value.Style->AssignmentOperatorSide = OperatorSide::Leading;
            } else if (std::string_view(name) == "default-next-line") {
                value.Style->AssignmentRhsPlacement = FirstItemPlacement::NextLine;
            } else if (std::string_view(name) == "default-preserve") {
                value.Style->ExpressionsLayout = CategoryLayout::Preserve;
            } else if (std::string_view(name) == "default-aligned") {
                value.Style->ParametersAlignment = ContinuationAlignment::Delimiter;
                value.Style->ContinuationOffset = 6;
                value.Style->ColumnLimit = 80;
                value.Style->AssignmentRhsPlacement = FirstItemPlacement::NextLine;
            }
            cases.push_back(std::move(value));
        }
    }
    AddCases(cases, data / "macro-nested-calls", {"configured"});
    AddCases(cases, data / "lambda-body", {"preserve", "inherit", "inline"});
    {
        const auto directory = data / "lambda-body";
        const auto base = formatter::LoadConfigFile(profile + ".hit-fmt").FormattingStyle;
        for (const auto* name :
            {
                "preserved-suffix",
                "preserved-suffix-canonical",
                "preserved-suffix-tabs",
                "preserved-suffix-indent-preserve",
            })
        {
            auto style = base;
            const std::string_view variant(name);
            style.LambdaBodiesLayout = CategoryLayout::Preserve;
            style.LambdaBodyShape.reset();
            if (variant == "preserved-suffix-canonical") {
                style.LambdaBodiesLayout = CategoryLayout::Canonical;
            } else if (variant == "preserved-suffix-tabs") {
                style.IndentationKind = IndentationKind::Tabs;
            } else if (variant == "preserved-suffix-indent-preserve") {
                style.IndentationKind = IndentationKind::Preserve;
            }
            cases.push_back({directory, name, "preserved-suffix.input.cpp"});
            cases.back().Style = style;
            if (variant == "preserved-suffix-canonical") {
                cases.back().Expected = "preserved-suffix.output.cpp";
            }
        }
        cases.push_back({directory, "member-pointers", "member-pointers.input.cpp"});
        auto style = formatter::LoadConfigFile(directory / "member-pointers.hit-fmt").FormattingStyle;
        style.LambdaBodiesLayout = CategoryLayout::Canonical;
        cases.push_back({directory, "member-pointers-canonical", "member-pointers.input.cpp"});
        cases.back().Style = style;
    }
    cases.push_back({data / "lambda-body", "visual-scopes", "visual-scopes.input.cpp", {}, {}, true, {}});
    AddCases(cases, data / "control-header-clauses", {"preserve", "split", "indent-width"});
    const auto controlPlacement = data / "control-header-placement";
    AddCases(cases, controlPlacement, {"preserve", "first-only", "closing-only", "both"});
    cases.push_back({controlPlacement, "reflow", "reflow.input.cpp"});
    cases.push_back({controlPlacement, "reverse", "reverse.input.cpp"});
    cases.push_back({controlPlacement, "crlf", "crlf.input.cpp"});
    {
        const auto directory = data / "if-conditions-packing";
        AddCases(
            cases,
            directory,
            {
                "fill",
                "leading",
                "trailing",
                "next-line",
                "forced",
                "expression-forced",
                "preserve",
                "preserve-expressions",
                "delimiter",
            });
    }
    cases.push_back({data / "lambda-body", "profile", "input.cpp", profile});
    addProfileCase("../control-header-clauses", "profile");
    addProfileCase("../control-braces-after-wrap", "profile");
    {
        const auto directory = data / "control-braces-after-wrap";
        auto base = formatter::LoadConfigFile(directory / (profile + ".hit-fmt")).FormattingStyle;
        // Nested bodies still exercise forced chains outside control-header scopes.
        base.ChainsBreak = BreakRequirement::Always;
        cases.back().Style = base;
        auto narrow = base;
        narrow.ColumnLimit = 80;
        narrow.AlwaysBreakChainRootNames.push_back("event");
        cases.push_back({directory, "narrow", "profile.input.cpp"});
        cases.back().Style = narrow;
        auto leading = narrow;
        leading.BinaryOperatorSide = formatter::OperatorSide::Leading;
        cases.push_back({directory, "leading", "profile.input.cpp"});
        cases.back().Style = leading;
        auto attached = base;
        attached.ControlBracePlacement = formatter::BracePlacement::Attached;
        cases.push_back({directory, "attached", "profile.input.cpp"});
        cases.back().Style = attached;
        auto nextLine = base;
        nextLine.ControlBracePlacement = formatter::BracePlacement::NextLine;
        cases.push_back({directory, "next-line", "profile.input.cpp"});
        cases.back().Style = nextLine;
        auto preserved = base;
        preserved.ControlHeadersLayout = formatter::CategoryLayout::Preserve;
        cases.push_back({directory, "preserved", "profile.input.cpp"});
        cases.back().Style = preserved;
    }
    addProfileCase("../whitespace-sensitive-macros", "profile");
    addProfileCase("../call-reflow", "balanced-expressions");
    cases.push_back({
        callReflow,
        "braced-arguments",
        "braced-arguments.input.cpp",
        "../../../../hit-fmt/examples/ytsaurus",
        {},
        true,
        {},
    });
    // Reflow canonical initializer and forced-chain children inside call arguments.
    cases.back().Style = formatter::LoadConfigFile(profile + ".hit-fmt").FormattingStyle;
    cases.back().Style->InitializersLayout = CategoryLayout::Canonical;
    cases.back().Style->ChainsBreak = BreakRequirement::Always;
    cases.push_back({
        callReflow,
        "typed-declarations",
        "typed-declarations.input.cpp",
        "../style/repository-80",
        {},
        true,
        {},
    });
    cases.push_back({callReflow, "nested-casts-preserve", "nested-casts.input.cpp"});
    cases.push_back({
        callReflow,
        "nested-casts-canonical",
        "nested-casts.input.cpp",
        "../style/repository-80",
        {},
        true,
        "nested-casts-preserve.output.cpp",
    });
    cases.back().Style = formatter::LoadConfigFile(callReflow / "../style/repository-80.hit-fmt").FormattingStyle;
    cases.back().Style->CallsSoleArgumentFirstItem = FirstItemPlacement::NextLine;
    cases.push_back({
        callReflow,
        "nested-casts-preserve-indent",
        "nested-casts.input.cpp",
        "nested-casts-preserve",
        {},
        true,
        "nested-casts.input.cpp",
    });
    cases.back().Style = formatter::LoadConfigFile(callReflow / "nested-casts-preserve.hit-fmt").FormattingStyle;
    cases.back().Style->IndentationKind = IndentationKind::Preserve;
    addProfileCase("../call-reflow", "nested-operator-choice");
    cases.push_back({callReflow, "balanced-initializers", "balanced-initializers.input.cpp"});
    addProfileCase("../call-reflow", "profile-nested");
    addProfileCase("../chained-calls", "profile-multiline-lambda");
    // Exercise forced layout for mixed member/invocation spines.
    cases.back().Style = formatter::LoadConfigFile(profile + ".hit-fmt").FormattingStyle;
    cases.back().Style->ChainsBreak = BreakRequirement::Always;
    {
        const auto directory = data / "chained-calls";
        {
            const auto base = formatter::LoadConfigFile(directory / "scoped-sticky.hit-fmt").FormattingStyle;
            struct Policy
            {
                const char* Profile;
                formatter::LayoutMode Layout;
                std::size_t ColumnLimit;
                formatter::BreakRequirement ChainsBreak;
                formatter::ContinuationAlignment ChainsAlignment;
                std::vector<std::string> ChainedCallRootNames;
            };
            for (const auto& policy :
                std::array<Policy, 5>{
                    {
                        {
                            "annotations-delimiter",
                            LayoutMode::Canonical,
                            80,
                            BreakRequirement::Always,
                            ContinuationAlignment::Delimiter,
                            {},
                        },
                        {
                            "annotations-filtered",
                            LayoutMode::Canonical,
                            80,
                            BreakRequirement::Always,
                            ContinuationAlignment::Block,
                            {"Builder"},
                        },
                        {
                            "annotations-fitting",
                            LayoutMode::Canonical,
                            80,
                            BreakRequirement::IfNeeded,
                            ContinuationAlignment::Block,
                            {},
                        },
                        {
                            "annotations-narrow",
                            LayoutMode::Canonical,
                            40,
                            BreakRequirement::Always,
                            ContinuationAlignment::Block,
                            {},
                        },
                        {
                            "annotations-preserve",
                            LayoutMode::Preserve,
                            80,
                            BreakRequirement::Always,
                            ContinuationAlignment::Block,
                            {},
                        },
                    },
                })
            {
                auto style = base;
                style.Layout = policy.Layout;
                style.ColumnLimit = policy.ColumnLimit;
                style.ChainsBreak = policy.ChainsBreak;
                style.ChainsAlignment = policy.ChainsAlignment;
                style.ChainedCallRootNames = policy.ChainedCallRootNames;
                ApplyProfile(cases, directory, policy.Profile, style);
            }
            auto scoped = base;
            scoped.StickyChainCallPairs.clear();
            ApplyProfile(cases, directory, "scoped-only", scoped);
            auto sticky = base;
            sticky.IndentedChainScopePairs.clear();
            ApplyProfile(cases, directory, "sticky-only", sticky);
        }
        {
            const auto base = formatter::LoadConfigFile(directory / "conventional.hit-fmt").FormattingStyle;
            struct Policy
            {
                const char* Profile;
                formatter::CategoryLayout ChainsLayout;
                std::vector<std::string> ChainedCallRootNames;
            };
            for (const auto& policy :
                std::array<Policy, 2>{
                    {
                        {"conventional-preserve", CategoryLayout::Preserve, {}},
                        {"conventional-selected", CategoryLayout::Inherit, {"READ_EVENT"}},
                    },
                })
            {
                auto style = base;
                style.ChainsLayout = policy.ChainsLayout;
                style.ChainedCallRootNames = policy.ChainedCallRootNames;
                ApplyProfile(cases, directory, policy.Profile, style);
            }
        }
        ShareExpected(cases, directory, "conventional-selected.output.cpp", "conventional-preserve.output.cpp");
    }
    addProfileCase("../keyword-expressions", "profile");
    // Canonical initializer children participate in enclosing expression fitting.
    addProfileCase("../keyword-expressions", "fill-operators");
    cases.back().Style = formatter::LoadConfigFile(profile + ".hit-fmt").FormattingStyle;
    cases.back().Style->InitializersLayout = CategoryLayout::Canonical;
    cases.push_back({data / "keyword-expressions", "fill-operators-leading", "fill-operators.input.cpp", profile});
    cases.back().Style = formatter::LoadConfigFile(profile + ".hit-fmt").FormattingStyle;
    cases.back().Style->BinaryOperatorSide = OperatorSide::Leading;
    cases.back().Style->InitializersLayout = CategoryLayout::Canonical;
    AddCases(cases, data / "keyword-expressions", {"canonical", "preserve", "narrow", "always"});
    {
        const auto directory = data / "keyword-expressions";
        {
            const auto base = formatter::LoadConfigFile(directory / "canonical.hit-fmt").FormattingStyle;
            struct Policy
            {
                const char* Profile;
                std::size_t ColumnLimit;
                formatter::BreakRequirement ExpressionsBreak;
                formatter::CategoryLayout ExpressionsLayout;
            };
            for (const auto& policy :
                std::array<Policy, 3>{
                    {
                        {"always", 80, BreakRequirement::Always, CategoryLayout::Inherit},
                        {"narrow", 40, BreakRequirement::IfNeeded, CategoryLayout::Inherit},
                        {"preserve", 80, BreakRequirement::IfNeeded, CategoryLayout::Preserve},
                    },
                })
            {
                auto style = base;
                style.ColumnLimit = policy.ColumnLimit;
                style.ExpressionsBreak = policy.ExpressionsBreak;
                style.ExpressionsLayout = policy.ExpressionsLayout;
                ApplyProfile(cases, directory, policy.Profile, style);
            }
        }
    }
    const auto unicode = data / "unicode-width";
    AddCases(cases, unicode, {"fitting", "overflow"});
    const auto google = data / "google-style-regressions";
    const auto googleProfile = (data / "../../../hit-fmt/examples/google").lexically_normal().string();
    cases.push_back({google, "profile-trailing-comments", "profile-trailing-comments.input.cpp", googleProfile});
    cases.push_back({google, "inline-record", "inline-record.input.cpp"});
    cases.push_back({google, "widths", "widths.input.cpp"});
    cases.push_back({google, "operator-spacing", "operator-spacing.input.cpp"});
    cases.push_back({google, "unbraced-switch", "unbraced-switch.input.cpp"});
    cases.push_back({google, "namespace-comments", "namespace-comments.input.cpp"});
    cases.push_back({google, "syntax-boundaries", "syntax-boundaries.input.cpp"});
    cases.push_back({google, "declarator-and-macro-safety", "declarator-and-macro-safety.input.cpp"});
    const auto disabled = data / "format-disabled";
    cases.push_back({disabled, "regions", "regions.input.cpp"});
    cases.push_back({disabled, "regions-no-clang", "regions.input.cpp"});
    cases.push_back({disabled, "unclosed", "unclosed.input.cpp", "regions"});
    cases.push_back({disabled, "crlf", "crlf.input.cpp", "regions"});
    cases.push_back({data / "constructor-initializer-ranges", "canonical", "input.cpp"});
    cases.push_back({
        data / "constructor-initializer-ranges",
        "template-ytsaurus",
        "template.input.cpp",
        (data / "../../../hit-fmt/examples/ytsaurus").lexically_normal().string(),
    });
    // Braced entries exercise the same owned constructor ranges as parenthesized ones.
    cases.back().Style = formatter::LoadConfigFile(profile + ".hit-fmt").FormattingStyle;
    cases.back().Style->InitializersLayout = CategoryLayout::Canonical;
    const auto newArrays = data / "new-array-declarators";
    AddCases(cases, newArrays, {"left", "array-space", "array-preserve"});
    AddCases(
        cases,
        google,
        {"punctuation-preserve", "punctuation-space", "comments-preserve"},
        "punctuation-preserve.input.cpp");
    cases.push_back({google, "access-only", "access-scope.input.cpp"});
    cases.push_back({google, "scope-only", "access-scope-safe.input.cpp"});
    cases.push_back({google, "scope-only-canonical", "access-scope-safe.input.cpp", "scope-only"});
    auto scopeStyle = formatter::LoadConfigFile(google / "scope-only.hit-fmt").FormattingStyle;
    scopeStyle.DeclarationHeadersLayout = CategoryLayout::Canonical;
    scopeStyle.ParametersLayout = CategoryLayout::Canonical;
    scopeStyle.ColumnLimit = 48;
    cases.back().Style = std::move(scopeStyle);
    const auto attribute = data / "attribute-indent";
    AddCases(cases, attribute, {"preserve", "block", "indent"});
    cases.push_back({attribute, "block-only", "block-only.input.cpp"});
    cases.push_back({attribute, "independent-widths", "independent-widths.input.cpp"});
    {
        const auto directory = data / "attribute-indent";
        ShareConfig(cases, directory, "independent-widths", "../google-style-regressions/widths");
    }
    AddCases(cases, data / "assignment-location", {"leading", "trailing"});
    AddCases(
        cases,
        data / "assignment-location",
        {
            "packing",
            "packing-fill",
            "packing-leading",
            "packing-next-line",
            "packing-always",
            "packing-preserve",
            "packing-vertical",
        },
        "packing.input.cpp");
    {
        const auto directory = data / "assignment-location";
        cases.push_back({directory, "packing-inherit", "packing.input.cpp", {}, {}, true, "packing.output.cpp"});
    }
    AddCases(
        cases,
        data / "assignment-location",
        {"large-rhs", "large-rhs-leading", "large-rhs-next-line"},
        "large-rhs.input.cpp");
    AddCases(cases, data / "call-expression-indent", {"aligned", "aligned-no-call-reflow", "indent-width", "preserve"});
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
        {"split-preserve", "one-per-line", "pack-wide", "pack-tight", "split-combined"},
        "split.input.cpp");
    for (const auto* name : {"parsed-operators", "outer-conditional", "control-braces-after-wrap"}) {
        cases.push_back({data / name, "configured", "input.cpp"});
    }
    cases.push_back({data / "control-braces-after-wrap", "reflow", "reflow.input.cpp"});
    const auto strings = data / "concat-strings";
    AddCases(cases, strings, {"overflow", "preserve"});
    AddCases(
        cases,
        strings,
        {
            "fragments-vertical",
            "fragments-fill",
            "fragments-all-or-vertical",
            "fragments-preserve",
            "fragments-always",
            "fragments-disabled",
            "fragments-offsets",
            "fragments-preserve-offsets",
            "fragments-next-line",
            "fragments-next-line-always",
        },
        "fragments.input.cpp");
    cases.push_back({strings, "crlf", "crlf.input.cpp", "overflow"});
    const auto parsedOperators = data / "parsed-operators";
    for (const auto* name : {"placement-lists", "delimiter-choice"}) {
        cases.push_back({parsedOperators, name, std::string(name) + ".input.cpp"});
    }
    // Keep safety and recovered-syntax examples in separate parse units.
    const auto spacing = data / "spacing-options";
    AddCases(
        cases,
        spacing,
        {"initializer-designators-preserve", "initializer-designators-delimiter"},
        "initializer-designators.input.cpp");
    ShareExpected(cases, spacing, "initializer-designators-preserve.output.cpp", "initializer-designators.input.cpp");
    cases.push_back({spacing, "cases-preserved", "indented-cases.input.cpp", "cases-preserved"});
    for (const auto& [name, indentation] :
        {
            std::pair{"case-regions-indent", formatter::BodyIndentation::Indent},
            std::pair{"case-regions-no-indent", formatter::BodyIndentation::NoIndent},
        })
    {
        auto style = formatter::LoadConfigFile(spacing / "case-regions.hit-fmt").FormattingStyle;
        style.IndentCaseLabels = indentation;
        style.CaseLabelIndentWidth = 1;
        cases.push_back({spacing, name, "case-regions.input.cpp", "case-regions"});
        cases.back().Style = std::move(style);
    }
    {
        auto style = formatter::LoadConfigFile(spacing / "case-regions.hit-fmt").FormattingStyle;
        style.IndentCaseLabels = formatter::BodyIndentation::NoIndent;
        style.CaseBlockBracePlacement = formatter::BracePlacement::Attached;
        cases.push_back({spacing, "case-regions-attached", "case-regions.input.cpp", "case-regions"});
        cases.back().Style = std::move(style);
    }
    AddCases(
        cases,
        spacing,
        {"formats-audited-syntax-boundaries--variant-02"},
        "formats-audited-syntax-boundaries.input.cpp");
    AddCases(
        cases,
        spacing,
        {"formats-directive-indent-and-trailing-comments--variant-02"},
        "formats-directive-indent-and-trailing-comments.input.cpp");
    AddCases(
        cases,
        spacing,
        {"formats-empty-lists-and-delimiter-prefixes--variant-02"},
        "formats-empty-lists-and-delimiter-prefixes.input.cpp");
    AddCases(
        cases,
        spacing,
        {"formats-empty-lists-and-delimiter-prefixes--variant-05"},
        "formats-empty-lists-and-delimiter-prefixes--variant-04.input.cpp");
    AddCases(
        cases,
        spacing,
        {"formats-only-safe-syntactic-gaps--variant-04"},
        "formats-only-safe-syntactic-gaps--variant-03.input.cpp");
    AddCases(
        cases,
        spacing,
        {
            "mirrors-inheritance-list-layout-policies--variant-02",
            "mirrors-inheritance-list-layout-policies--variant-03",
            "mirrors-inheritance-list-layout-policies--variant-07",
            "mirrors-inheritance-list-layout-policies--variant-08",
        },
        "mirrors-inheritance-list-layout-policies.input.cpp");
    AddCases(
        cases,
        spacing,
        {"spaces-attribute-new-delete-and-pack-prefixes--variant-02"},
        "spaces-attribute-new-delete-and-pack-prefixes.input.cpp");
    AddCases(
        cases,
        spacing,
        {"spaces-attributes-named-casts-and-placement-new--variant-02"},
        "spaces-attributes-named-casts-and-placement-new.input.cpp");
    AddCases(
        cases,
        spacing,
        {"spaces-block-interiors-and-declaration-terminators--variant-02"},
        "spaces-block-interiors-and-declaration-terminators.input.cpp");
    AddCases(
        cases,
        spacing,
        {
            "spaces-function-qualifiers-and-nested-declarators--variant-02",
            "spaces-function-qualifiers-and-nested-declarators--variant-03",
        },
        "spaces-function-qualifiers-and-nested-declarators.input.cpp");
    AddCases(
        cases,
        spacing,
        {"spaces-grouping-conditions-and-selected-colons--variant-02"},
        "spaces-grouping-conditions-and-selected-colons.input.cpp");
    AddCases(
        cases,
        spacing,
        {"spaces-keyword-parentheses-only-in-parsed-constructs--variant-02"},
        "spaces-keyword-parentheses-only-in-parsed-constructs.input.cpp");
    AddCases(
        cases,
        spacing,
        {"spaces-new-arrays-empty-attributes-and-co-await--variant-02"},
        "spaces-new-arrays-empty-attributes-and-co-await.input.cpp");
    AddCases(
        cases,
        spacing,
        {"spaces-new-initializers-and-delete-operands--variant-02"},
        "spaces-new-initializers-and-delete-operands.input.cpp");
    AddCases(
        cases,
        spacing,
        {
            "spaces-only-parsed-colon-kinds--left",
            "spaces-only-parsed-colon-kinds--right",
            "spaces-only-parsed-colon-kinds--left-and-right",
            "spaces-only-parsed-colon-kinds--variant-01",
        },
        "spaces-only-parsed-colon-kinds.input.cpp");
    AddCases(
        cases,
        spacing,
        {"spaces-template-types-empty-initializers-and-attributes--variant-02"},
        "spaces-template-types-empty-initializers-and-attributes.input.cpp");
    AddCases(
        cases,
        spacing,
        {"spaces-template-types-empty-initializers-and-attributes--variant-09"},
        "spaces-template-types-empty-initializers-and-attributes--variant-08.input.cpp");
    AddCases(
        cases,
        spacing,
        {"trims-only-whitespace-outside-sensitive-tokens--variant-02"},
        "trims-only-whitespace-outside-sensitive-tokens.input.cpp");
    for (const auto& entry : std::filesystem::directory_iterator(spacing)) {
        const auto filename = entry.path().filename().string();
        if (filename.ends_with(".input.cpp")) {
            const auto name = filename.substr(0, filename.size() - std::string_view(".input.cpp").size());
            const auto duplicate = std::ranges::any_of(
                cases,
                [&] (const FormatCase& value) {
                    return value.Directory.lexically_normal() == spacing.lexically_normal() && value.Name == name;
                });
            if (!duplicate) {
                cases.push_back({spacing, name, filename});
            }
        }
    }
    AddCases(cases, data / "continuation-nesting", {"shared", "tabs"});
    AddCases(
        cases,
        data / "continuation-nesting",
        {"argument-precedence", "argument-precedence-leading", "argument-precedence-delimiter"},
        "argument-precedence.input.cpp");
    cases.push_back({data / "continuation-nesting", "starter-lines", "starter-lines.input.cpp"});
    AddCases(cases, data / "continuation-nesting", {"line-scopes"}, "line-scopes.input.cpp");
    {
        const auto directory = data / "continuation-nesting";
        {
            auto style = formatter::LoadConfigFile(directory / "shared.hit-fmt").FormattingStyle;
            style.IndentationKind = IndentationKind::Tabs;
            ApplyProfile(cases, directory, "tabs", style);
        }
        {
            const auto base = formatter::LoadConfigFile(directory / "argument-precedence.hit-fmt").FormattingStyle;
            struct Policy
            {
                const char* Profile;
                formatter::CategoryLayout CallsLayout;
                std::optional<formatter::BreakRequirement> CallsBreak;
                std::optional<formatter::FirstItemPlacement> CallsFirstItem;
                std::optional<formatter::ContinuationAlignment> CallsAlignment;
                formatter::OperatorSide BinaryOperatorSide;
            };
            for (const auto& policy :
                std::array<Policy, 2>{
                    {
                        {
                            "argument-precedence-delimiter",
                            CategoryLayout::Preserve,
                            std::nullopt,
                            std::nullopt,
                            ContinuationAlignment::Delimiter,
                            OperatorSide::Trailing,
                        },
                        {
                            "argument-precedence-leading",
                            CategoryLayout::Inherit,
                            BreakRequirement::Always,
                            FirstItemPlacement::NextLine,
                            std::nullopt,
                            OperatorSide::Leading,
                        },
                    },
                })
            {
                auto style = base;
                style.CallsLayout = policy.CallsLayout;
                style.CallsBreak = policy.CallsBreak;
                style.CallsFirstItem = policy.CallsFirstItem;
                style.CallsAlignment = policy.CallsAlignment;
                style.BinaryOperatorSide = policy.BinaryOperatorSide;
                ApplyProfile(cases, directory, policy.Profile, style);
            }
        }
        ShareConfig(cases, directory, "line-scopes", "../ternary-layout/branches-preserve");
        ShareExpected(cases, directory, "argument-precedence.output.cpp", "argument-precedence-delimiter.output.cpp");
    }
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
        cases.push_back({data / "public-pipeline", name, std::string(name) + ".input.cpp"});
    }
    {
        const auto directory = data / "public-pipeline";
        {
            const auto base = formatter::LoadConfigFile(directory / "annotation-boundaries.hit-fmt").FormattingStyle;
            struct Policy
            {
                const char* Profile;
                std::size_t ColumnLimit;
            };
            for (const auto& policy :
                std::array<Policy, 3>{
                    {{"annotation-constraints", 10000}, {"annotation-constructors", 100}, {"annotation-suffixes", 100}},
                })
            {
                auto style = base;
                style.ColumnLimit = policy.ColumnLimit;
                ApplyProfile(cases, directory, policy.Profile, style);
            }
        }
        ShareConfig(cases, directory, "control-separators-nested", "control-separators-braced");
        ShareConfig(cases, directory, "control-separators-unbraced", "control-separators-braced");
        ShareConfig(cases, directory, "declaration-operands-recovery", "declaration-operands");
        ShareExpected(
            cases,
            directory,
            "declaration-operands-recovery.output.cpp",
            "declaration-operands-recovery.input.cpp");
    }
    for (const auto* name : {"nested", "messages"}) {
        cases.push_back({data / "group-syntax", name, std::string(name) + ".input.cpp"});
    }
    for (const bool anyArgument : {false, true}) {
        const auto directory = data / (anyArgument ? "attached-argument-overflow" : "attached-string-overflow");
        for (const auto overflow :
            {
                formatter::AttachedStringOverflow::Wrap,
                formatter::AttachedStringOverflow::BreakBefore,
                formatter::AttachedStringOverflow::KeepInline,
            })
        {
            auto style = formatter::LoadConfigFile(data / "group-syntax/messages.hit-fmt").FormattingStyle;
            if (anyArgument) {
                style.CallLineFirstArgumentNames = style.CallLineFirstStringNames;
                style.CallLineFirstStringNames.clear();
                style.CallLineFirstArgumentOverflow = overflow;
            } else {
                style.CallLineFirstStringOverflow = overflow;
            }
            const auto name = std::string(formatter::SerializeConfigEnum(overflow));
            cases.push_back({directory, name, "input.cpp"});
            cases.back().Style = style;
            style.CallsBreak = BreakRequirement::Always;
            cases.push_back({directory, name + "-always", "input.cpp"});
            cases.back().Style = style;
            if (overflow == formatter::AttachedStringOverflow::KeepInline) {
                cases.back().Expected = "Wrap-always.output.cpp";
            }
            style.CallsLayout = CategoryLayout::Preserve;
            cases.push_back({directory, name + "-preserve", "input.cpp"});
            cases.back().Style = style;
            if (overflow != formatter::AttachedStringOverflow::Wrap) {
                cases.back().Expected = "Wrap-preserve.output.cpp";
            }
            style.CallsLayout = CategoryLayout::Canonical;
            style.CallsBreak = BreakRequirement::IfNeeded;
            style.ColumnLimit = 0;
            cases.push_back({directory, name + "-unlimited", "input.cpp"});
            cases.back().Style = style;
            if (overflow != formatter::AttachedStringOverflow::Wrap) {
                cases.back().Expected = "Wrap-unlimited.output.cpp";
            }
        }
    }
    {
        auto style = formatter::LoadConfigFile(data / "group-syntax/messages.hit-fmt").FormattingStyle;
        cases.push_back({data / "attached-argument-overflow", "string-only", "input.cpp"});
        cases.back().Style = style;
        // Delimiter alignment must not lose the attached first argument.
        style.CallLineFirstArgumentNames = style.CallLineFirstStringNames;
        style.CallLineFirstStringNames.clear();
        style.CallsAlignment = ContinuationAlignment::Delimiter;
        cases.push_back({data / "attached-argument-overflow", "delimiter", "input.cpp"});
        cases.back().Style = style;
    }
    AddCases(cases, data / "structural-expression-base", {"canonical", "preserved"});
    {
        const auto directory = data / "structural-expression-base";
        {
            auto style = formatter::LoadConfigFile(directory / "canonical.hit-fmt").FormattingStyle;
            style.Layout = LayoutMode::Preserve;
            ApplyProfile(cases, directory, "preserved", style);
        }
        ShareExpected(cases, directory, "preserved.output.cpp", "canonical.output.cpp");
    }
    cases.push_back({data / "initializer-trailing-comma", "conditional-closer", "input.cpp"});
    for (const auto* name : {"comments", "comments-preserve"}) {
        cases.push_back({data / "initializer-trailing-comma", name, "comments.input.cpp"});
    }
    for (const auto* name : {"spacing", "spacing-preserve", "spacing-compact", "spacing-layout-preserve"}) {
        cases.push_back({data / "initializer-trailing-comma", name, "spacing.input.cpp"});
    }
    AddCases(cases, data / "function_parameters", {"preserve", "pack_name", "pack_new", "one_name", "one_new"});
    AddCases(
        cases,
        data / "namespace_comments",
        {"preserve", "remove", "add_missing", "fix", "fix_spaced", "fix_newline"});
    cases.push_back({data / "namespace_comments", "crlf_fix", "crlf.input.cpp"});
    cases.push_back({data / "namespace_comments", "composes-spacing", "composes-spacing.input.cpp"});
    for (const auto* profile :
        {"semicolon-fix", "semicolon-add", "semicolon-preserve", "semicolon-add-remove", "semicolon-remove"})
    {
        cases.push_back({data / "namespace_comments", profile, "semicolon.input.cpp", {}, {}, true, {}});
    }
    for (const auto* profile : {"reindented", "reindented-add"}) {
        cases.push_back({data / "namespace_comments", profile, "reindented.input.cpp", {}, {}, true, {}});
    }
    AddCases(cases, data / "attribute-parenthesis-interiors", {"compact", "spaced", "preserve", "canonical"});
    {
        const auto directory = data / "attribute-parenthesis-interiors";
        {
            const auto base = formatter::LoadConfigFile(directory / "preserve.hit-fmt").FormattingStyle;
            struct Policy
            {
                const char* Profile;
                formatter::GapPolicy AttributeInteriorSpacing;
                formatter::GapPolicy AttributeSpaceAfterSpecifier;
                formatter::GapPolicy AttributeSpaceBeforeSpecifier;
                formatter::GapPolicy ControlBlockConditionSpacing;
                formatter::GapPolicy SpaceInsideFunctionParameters;
                formatter::GapPolicy SpaceInsideLambdaParameters;
            };
            for (const auto& policy :
                std::array<Policy, 2>{
                    {
                        {
                            "compact",
                            GapPolicy::Space,
                            GapPolicy::Space,
                            GapPolicy::Space,
                            GapPolicy::NoSpace,
                            GapPolicy::NoSpace,
                            GapPolicy::NoSpace,
                        },
                        {
                            "spaced",
                            GapPolicy::NoSpace,
                            GapPolicy::NoSpace,
                            GapPolicy::NoSpace,
                            GapPolicy::Space,
                            GapPolicy::Space,
                            GapPolicy::Space,
                        },
                    },
                })
            {
                auto style = base;
                style.AttributeInteriorSpacing = policy.AttributeInteriorSpacing;
                style.AttributeSpaceAfterSpecifier = policy.AttributeSpaceAfterSpecifier;
                style.AttributeSpaceBeforeSpecifier = policy.AttributeSpaceBeforeSpecifier;
                style.ControlBlockConditionSpacing = policy.ControlBlockConditionSpacing;
                style.SpaceInsideFunctionParameters = policy.SpaceInsideFunctionParameters;
                style.SpaceInsideLambdaParameters = policy.SpaceInsideLambdaParameters;
                ApplyProfile(cases, directory, policy.Profile, style);
            }
        }
    }
    AddCases(cases, data / "qualified-record", {"canonical", "ytsaurus"});
    AddCases(cases, data / "elaborated-initializers", {"canonical", "vertical"});
    AddCases(cases, data / "uncertain-layout", {"canonical", "preserve"});
    AddCases(cases, data / "uncertain-layout", {"additional"}, "additional.input.cpp");
    cases.push_back({data / "uncertain-layout", "additional-wide", "additional.input.cpp"});
    cases.push_back({
        data / "uncertain-layout",
        "additional-preserve",
        "additional.input.cpp",
        "preserve",
        {},
        true,
        "additional.input.cpp",
    });
    {
        auto style = formatter::LoadConfigFile(data / "uncertain-layout/additional.hit-fmt").FormattingStyle;
        style.ColumnLimit = 10000;
        style.TemplateArgumentsBreak = BreakRequirement::IfNeeded;
        ApplyProfile(cases, data / "uncertain-layout", "additional-wide", style);
    }
    ShareExpected(cases, data / "uncertain-layout", "preserve.output.cpp", "input.cpp");
    AddCases(cases, data / "template-arguments", {"inherit", "vertical", "fill", "delimiter", "preserve", "tabs"});
    cases.push_back({
        data / "template-arguments",
        "function-types",
        "function-types.input.cpp",
        "../style/repository-80",
        {},
        true,
    });
    cases.push_back({data / "template-arguments", "break-priority", "break-priority.input.cpp", {}, {}, true, {}});
    cases.push_back({
        data / "template-arguments",
        "repository-priority",
        "break-priority.input.cpp",
        "../style/repository-80",
        {},
        true,
        {},
    });
    cases.push_back({
        data / "template-arguments",
        "forced-priority",
        "break-priority.input.cpp",
        "break-priority",
        {},
        true,
        {},
    });
    auto forcedTemplates =
        formatter::LoadConfigFile(data / "template-arguments/break-priority.hit-fmt").FormattingStyle;
    forcedTemplates.TemplateArgumentsBreak = formatter::BreakRequirement::Always;
    forcedTemplates.TemplateParametersBreak = formatter::BreakRequirement::Always;
    cases.back().Style = std::move(forcedTemplates);
    {
        const auto directory = data / "template-arguments";
        const auto base = formatter::LoadConfigFile(directory / "trailing-return-space.hit-fmt").FormattingStyle;
        struct Policy
        {
            const char* Name;
            LayoutMode Layout;
            GapPolicy Comma;
            const char* Expected;
        };
        for (const auto& policy :
            std::array<Policy, 5>{
                {
                    {"space", LayoutMode::Canonical, GapPolicy::Space, "trailing-return-space.output.cpp"},
                    {"no-space", LayoutMode::Canonical, GapPolicy::NoSpace, "trailing-return-no-space.output.cpp"},
                    {"preserve", LayoutMode::Preserve, GapPolicy::Preserve, "trailing-return.input.cpp"},
                    {"generated", LayoutMode::Canonical, GapPolicy::GeneratedSpace, "trailing-return-space.output.cpp"},
                    {
                        "generated-preserve",
                        LayoutMode::Preserve,
                        GapPolicy::GeneratedSpace,
                        "trailing-return.input.cpp",
                    },
                },
            })
        {
            cases.push_back({
                directory,
                std::string("trailing-return-") + policy.Name,
                "trailing-return.input.cpp",
                "trailing-return-space",
                {},
                true,
                policy.Expected,
            });
            auto style = base;
            style.Layout = policy.Layout;
            style.CommaSpaceAfter = policy.Comma;
            cases.back().Style = std::move(style);
        }
    }
    AddCases(cases, data / "template-parameters", {"inherit", "vertical", "fill", "delimiter", "preserve", "tabs"});
    for (const auto* profile : {"google", "llvm", "ytsaurus"}) {
        cases.push_back({
            data / "template-parameters",
            profile,
            "input.cpp",
            (data / "../../../hit-fmt/examples" / profile).lexically_normal().string(),
        });
    }
    AddCases(
        cases,
        data / "requires-clause",
        {
            "own-line",
            "own-line-no-indent",
            "own-line-with-brace",
            "with-preceding",
            "with-following",
            "single-line",
            "narrow",
            "narrow-own-line",
            "preserve",
            "headers-preserve",
            "brace-next-line",
        });
    AddCases(cases, data / "requires-lambdas", {"canonical", "narrow", "preserve"});
    cases.push_back({data / "requires-lambdas", "continuation", "continuation.input.cpp"});
    cases.push_back({data / "requires-lambdas", "continuation-own-line", "continuation.input.cpp"});
    {
        const auto directory = data / "requires-lambdas";
        const auto parserData = std::filesystem::path(PARSER_TESTDATA_DIR);
        for (const auto* name : {"forms", "forms-narrow"}) {
            cases.push_back({directory, name, (parserData / "requires_forms.in").string(), name, {}, true, {}});
        }
        cases.push_back({
            directory,
            "forms-recovery",
            (parserData / "requires_forms_recovery.in").string(),
            "canonical",
            {},
            true,
            {},
        });
        for (auto& value : cases) {
            if (value.Directory == directory) { }
        }
        auto style = formatter::LoadConfigFile(directory / "canonical.hit-fmt").FormattingStyle;
        ApplyProfile(cases, directory, "forms", style);
        style.ColumnLimit = 60;
        ApplyProfile(cases, directory, "narrow", style);
        ApplyProfile(cases, directory, "forms-narrow", style);
        style.Layout = LayoutMode::Preserve;
        style.TemplateParametersLayout = CategoryLayout::Preserve;
        style.LambdaBodiesLayout = CategoryLayout::Preserve;
        ApplyProfile(cases, directory, "preserve", style);
    }
    AddCases(cases, data / "unhinted-body-recovery", {"canonical"});
    AddCases(cases, data / "macro-generated-cases", {"canonical", "preserve"});
    cases.push_back({
        data / "macro-generated-cases",
        "type-tuples",
        "type-tuples.input.cpp",
        (data / "../../../hit-fmt/examples/ytsaurus").lexically_normal().string(),
        {},
        true,
        {},
    });
    cases.push_back({
        data / "macro-generated-cases",
        "ytsaurus",
        "input.cpp",
        (data / "../../../hit-fmt/examples/ytsaurus").lexically_normal().string(),
        {},
        true,
        {},
    });
    cases.push_back({
        data / "macro-generated-cases",
        "type-tuples-preserve",
        "type-tuples.input.cpp",
        "preserve",
        {},
        true,
        {},
    });
    AddCases(cases, data / "lambda-argument-ownership", {"canonical", "preserve"});
    AddCases(cases, data / "lambda-argument-tail-recovery", {"canonical"});
    {
        const auto directory = data / "lambda-argument-ownership";
        {
            auto style = formatter::LoadConfigFile(directory / "canonical.hit-fmt").FormattingStyle;
            style.LambdaBodiesLayout = CategoryLayout::Preserve;
            ApplyProfile(cases, directory, "preserve", style);
        }
    }
    {
        const auto directory = data / "lambda-argument-tail-recovery";
        ShareConfig(cases, directory, "canonical", "../lambda-argument-ownership/canonical");
    }
    const auto hunks = data / "changed-hunks";
    cases.push_back({hunks / "basic", "call", "input.cpp", "style", {{8, 8}}});
    cases.push_back({hunks / "basic", "includes", "input.cpp", "style", {{1, 1}}});
    cases.push_back({hunks / "basic", "includes-strict", "input.cpp", "style", {{1, 1}}, false});
    cases.push_back({hunks / "basic", "function", "input.cpp", "style", {{4, 4}}});
    cases.push_back({hunks / "spill", "default", "input.cpp", "style", {{3, 3}, {5, 5}}});
    cases.push_back({hunks / "spill", "strict", "input.cpp", "style", {{3, 3}, {5, 5}}, false});
    cases.push_back({hunks / "structural", "unsupported", "input.cpp", "style", {{2, 2}}});
    cases.push_back({
        hunks / "planned",
        "strict-independent",
        "strict-independent.input.cpp",
        "style",
        {{3, 3}, {6, 6}},
        false,
    });
    cases.push_back({hunks / "planned", "nested", "nested.input.cpp", "style", {{3, 3}}, true});
    cases.push_back({hunks / "planned", "atomic", "atomic.input.cpp", "structural", {{2, 5}}, false});
    for (const bool spillover : {false, true}) {
        cases.push_back({
            hunks / "planned",
            spillover ? "canonical-spill" : "canonical-strict",
            "canonical.input.cpp",
            "canonical",
            {{2, 2}, {6, 6}},
            spillover,
            "canonical.input.cpp",
        });
    }
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
        cases.push_back({parsedOperators, name, std::string(name) + ".input.cpp"});
    }
    AddCases(cases, parsedOperators, {"greedy-leading", "preserve"}, "greedy-trailing.input.cpp");
    AddCases(cases, parsedOperators, {"always-trailing", "always-leading"}, "leading-narrow.input.cpp");
    AddCases(cases, parsedOperators, {"leading-fitting", "trailing-fitting"}, "fitting-topologies.input.cpp");
    // Name, input, config override, read-only expectation alias.
    for (const auto& [name, input, config, expected] :
        {
            std::array<const char*, 4>{"macro-preserve", "macro-preserve.input.cpp", "crlf-return", ""},
            {"boundary-comment-tail", "comment-tail.input.cpp", "crlf-return", ""},
            {"recovered-tail", "recovered-tail.input.cpp", "crlf-return", ""},
            {"template-argument-preserve", "template-argument-preserve.input.cpp", "crlf-return", ""},
            {"default-preserve", "input.cpp", "", "input.cpp"},
            {"partial-join", "partial-join.input.cpp", "leading-narrow", ""},
            {"always-space", "always-space.input.cpp", "always-trailing", ""},
            {"continuation-comment-tail", "continuation-comment-tail.input.cpp", "call-preserve", ""},
            {"crlf", "crlf.input.cpp", "call-preserve", ""},
            {"comparison-if-needed", "comparison-if-needed.input.cpp", "ambiguous-if-needed", ""},
            {"ambiguous-always", "ambiguous-if-needed.input.cpp", "", ""},
            {"comparison-always", "comparison-if-needed.input.cpp", "ambiguous-always", ""},
            {"trailing-statements", "trailing-statements.input.cpp", "return-trailing", ""},
        })
    {
        cases.push_back({parsedOperators, name, input, config, {}, true, expected});
    }
    {
        const auto directory = data / "parsed-operators";
        {
            const auto base = formatter::LoadConfigFile(directory / "greedy-trailing.hit-fmt").FormattingStyle;
            struct Policy
            {
                const char* Profile;
                std::size_t ColumnLimit;
                formatter::BreakRequirement ExpressionsBreak;
                formatter::CategoryLayout ExpressionsLayout;
                formatter::OperatorSide BinaryOperatorSide;
                formatter::GapPolicy BinaryOperatorSpacing;
                formatter::GapPolicy AssignmentOperatorSpacing;
            };
            for (const auto& policy :
                std::array<Policy, 15>{
                    {
                        {
                            "always-leading",
                            30,
                            BreakRequirement::Always,
                            CategoryLayout::Canonical,
                            OperatorSide::Leading,
                            GapPolicy::Space,
                            GapPolicy::Space,
                        },
                        {
                            "always-trailing",
                            30,
                            BreakRequirement::Always,
                            CategoryLayout::Canonical,
                            OperatorSide::Trailing,
                            GapPolicy::Space,
                            GapPolicy::Space,
                        },
                        {
                            "ambiguous-always",
                            32,
                            BreakRequirement::Always,
                            CategoryLayout::Canonical,
                            OperatorSide::Leading,
                            GapPolicy::Space,
                            GapPolicy::Space,
                        },
                        {
                            "ambiguous-if-needed",
                            32,
                            BreakRequirement::IfNeeded,
                            CategoryLayout::Canonical,
                            OperatorSide::Leading,
                            GapPolicy::Space,
                            GapPolicy::Space,
                        },
                        {
                            "call-preserve",
                            80,
                            BreakRequirement::Always,
                            CategoryLayout::Canonical,
                            OperatorSide::Leading,
                            GapPolicy::Space,
                            GapPolicy::Space,
                        },
                        {
                            "compact-comparison",
                            20,
                            BreakRequirement::Always,
                            CategoryLayout::Canonical,
                            OperatorSide::Trailing,
                            GapPolicy::Preserve,
                            GapPolicy::Preserve,
                        },
                        {
                            "compact-return",
                            25,
                            BreakRequirement::IfNeeded,
                            CategoryLayout::Canonical,
                            OperatorSide::Trailing,
                            GapPolicy::NoSpace,
                            GapPolicy::Preserve,
                        },
                        {
                            "crlf-return",
                            25,
                            BreakRequirement::IfNeeded,
                            CategoryLayout::Canonical,
                            OperatorSide::Trailing,
                            GapPolicy::Preserve,
                            GapPolicy::Preserve,
                        },
                        {
                            "greedy-leading",
                            40,
                            BreakRequirement::IfNeeded,
                            CategoryLayout::Canonical,
                            OperatorSide::Leading,
                            GapPolicy::Preserve,
                            GapPolicy::Preserve,
                        },
                        {
                            "horizontal-and-topology",
                            80,
                            BreakRequirement::IfNeeded,
                            CategoryLayout::Canonical,
                            OperatorSide::Trailing,
                            GapPolicy::Space,
                            GapPolicy::Space,
                        },
                        {
                            "leading-fitting",
                            40,
                            BreakRequirement::IfNeeded,
                            CategoryLayout::Canonical,
                            OperatorSide::Leading,
                            GapPolicy::Space,
                            GapPolicy::Space,
                        },
                        {
                            "leading-narrow",
                            30,
                            BreakRequirement::IfNeeded,
                            CategoryLayout::Canonical,
                            OperatorSide::Leading,
                            GapPolicy::Space,
                            GapPolicy::Space,
                        },
                        {
                            "preserve",
                            40,
                            BreakRequirement::IfNeeded,
                            CategoryLayout::Preserve,
                            OperatorSide::Trailing,
                            GapPolicy::Preserve,
                            GapPolicy::Preserve,
                        },
                        {
                            "return-trailing",
                            32,
                            BreakRequirement::IfNeeded,
                            CategoryLayout::Canonical,
                            OperatorSide::Trailing,
                            GapPolicy::Preserve,
                            GapPolicy::Preserve,
                        },
                        {
                            "trailing-fitting",
                            40,
                            BreakRequirement::IfNeeded,
                            CategoryLayout::Canonical,
                            OperatorSide::Trailing,
                            GapPolicy::Space,
                            GapPolicy::Space,
                        },
                    },
                })
            {
                auto style = base;
                style.ColumnLimit = policy.ColumnLimit;
                style.ExpressionsBreak = policy.ExpressionsBreak;
                style.ExpressionsLayout = policy.ExpressionsLayout;
                style.BinaryOperatorSide = policy.BinaryOperatorSide;
                style.BinaryOperatorSpacing = policy.BinaryOperatorSpacing;
                style.AssignmentOperatorSpacing = policy.AssignmentOperatorSpacing;
                ApplyProfile(cases, directory, policy.Profile, style);
            }
        }
        ShareConfig(cases, directory, "default-preserve", "../closing-delimiters/preserve");
        ShareExpected(cases, directory, "call-preserve.output.cpp", "call-preserve.input.cpp");
        ShareExpected(cases, directory, "preserve.output.cpp", "greedy-trailing.input.cpp");
        ShareExpected(
            cases,
            directory,
            "template-argument-preserve.output.cpp",
            "template-argument-preserve.input.cpp");
        ShareExpected(cases, directory, "leading-narrow.output.cpp", "always-leading.output.cpp");
        ShareExpected(cases, directory, "ambiguous-always.output.cpp", "ambiguous-if-needed.input.cpp");
        ShareExpected(cases, directory, "ambiguous-if-needed.output.cpp", "ambiguous-if-needed.input.cpp");
        ShareExpected(cases, directory, "trailing-fitting.output.cpp", "leading-fitting.output.cpp");
        ShareExpected(cases, directory, "macro-preserve.output.cpp", "macro-preserve.input.cpp");
    }
    const auto functionParameters = data / "function_parameters";
    for (const auto* name : {"preserve-header", "attached-spacing", "detached-spacing"}) {
        cases.push_back({functionParameters, name, std::string(name) + ".input.cpp"});
    }
    // Name, input, config override, read-only expectation alias.
    for (const auto& [name, input, config, expected] :
        {
            std::array<const char*, 4>{"short-next-line", "short-preserved-header.input.cpp", "pack_name", ""},
            {"tabs-next-line", "preserve-header.input.cpp", "", ""},
            {"crlf", "crlf.input.cpp", "", "crlf.output.cpp"},
            {"preserved-header-definitions", "preserved-header-definitions.input.cpp", "reflow-preserved-header", ""},
        })
    {
        cases.push_back({functionParameters, name, input, config, {}, true, expected});
    }
    {
        const auto directory = data / "function_parameters";
        {
            const auto base = formatter::LoadConfigFile(directory / "attached-spacing.hit-fmt").FormattingStyle;
            struct Policy
            {
                const char* Profile;
                std::size_t ColumnLimit;
                formatter::CategoryLayout DeclarationHeadersLayout;
                formatter::GapPolicy FunctionBraceSpacing;
            };
            for (const auto& policy :
                std::array<Policy, 2>{
                    {
                        {"detached-spacing", 40, CategoryLayout::Canonical, GapPolicy::Space},
                        {"crlf", 25, CategoryLayout::Inherit, GapPolicy::GeneratedSpace},
                    },
                })
            {
                auto style = base;
                style.ColumnLimit = policy.ColumnLimit;
                style.DeclarationHeadersLayout = policy.DeclarationHeadersLayout;
                style.FunctionBraceSpacing = policy.FunctionBraceSpacing;
                ApplyProfile(cases, directory, policy.Profile, style);
            }
        }
        {
            const auto base = formatter::LoadConfigFile(directory / "preserve-header.hit-fmt").FormattingStyle;
            struct Policy
            {
                const char* Profile;
                formatter::IndentationKind IndentationKind;
                formatter::CategoryLayout ParametersLayout;
                std::optional<std::size_t> ParametersContinuationOffset;
                formatter::CategoryLayout DeclarationHeadersLayout;
            };
            for (const auto& policy :
                std::array<Policy, 2>{
                    {
                        {
                            "reflow-preserved-header",
                            IndentationKind::Preserve,
                            CategoryLayout::Canonical,
                            std::nullopt,
                            CategoryLayout::Preserve,
                        },
                        {
                            "tabs-next-line",
                            IndentationKind::Tabs,
                            CategoryLayout::Canonical,
                            8,
                            CategoryLayout::Canonical,
                        },
                    },
                })
            {
                auto style = base;
                style.IndentationKind = policy.IndentationKind;
                style.ParametersLayout = policy.ParametersLayout;
                style.ParametersContinuationOffset = policy.ParametersContinuationOffset;
                style.DeclarationHeadersLayout = policy.DeclarationHeadersLayout;
                ApplyProfile(cases, directory, policy.Profile, style);
            }
        }
        ShareExpected(cases, directory, "preserve-header.output.cpp", "preserve-header.input.cpp");
        ShareExpected(cases, directory, "detached-spacing.output.cpp", "attached-spacing.output.cpp");
    }
    const auto lambdaAnchors = data / "consolidated-formatter/lambda-anchor";
    {
        const auto directory = data / "lambda-idempotence";
        for (const auto* profileName : {"google", "llvm"}) {
            const auto base = formatter::LoadConfigFile(
                data / ("../../../hit-fmt/examples/" + std::string(profileName) + ".hit-fmt")).FormattingStyle;
            for (const auto* variant : {"flat", "multiline", "narrow", "preserve"}) {
                const auto name = std::string(profileName) + "-" + variant;
                cases.push_back({
                    directory,
                    name,
                    std::string(profileName) +
                        (std::string_view(variant) == "multiline" ? "-multiline.input.cpp" : "-flat.input.cpp"),
                    {},
                    {},
                    true,
                    std::string_view(variant) == "multiline" ? std::string(profileName) + "-flat.output.cpp" : "",
                });
                auto style = base;
                if (std::string_view(variant) == "narrow") {
                    style.ColumnLimit = 60;
                } else if (std::string_view(variant) == "preserve") {
                    style.LambdaBodiesLayout = CategoryLayout::Preserve;
                    style.CapturesLayout = CategoryLayout::Preserve;
                    style.ParametersLayout = CategoryLayout::Preserve;
                }
                cases.back().Style = style;
            }
        }
    }
    cases.push_back({lambdaAnchors, "authored-anchor", "authored-anchor.input.cpp"});
    for (const auto* name : {"attributed-blocks", "attributed-blocks-canonical", "attributed-blocks-tabs"}) {
        cases.push_back({lambdaAnchors, name, "attributed-blocks.input.cpp"});
    }
    // Name, input, config override, read-only expectation alias.
    for (const auto& [name, input, config, expected] :
        {
            std::array<const char*, 4>{"correct-anchor", "correct-anchor.input.cpp", "authored-anchor", ""},
            {"narrow-body", "correct-anchor.input.cpp", "", ""},
        })
    {
        cases.push_back({lambdaAnchors, name, input, config, {}, true, expected});
    }
    {
        const auto directory = data / "consolidated-formatter/lambda-anchor";
        {
            auto style = formatter::LoadConfigFile(directory / "attributed-blocks.hit-fmt").FormattingStyle;
            style.LambdaBodiesLayout = formatter::CategoryLayout::Canonical;
            ApplyProfile(cases, directory, "attributed-blocks-canonical", style);
            style.LambdaBodiesLayout = formatter::CategoryLayout::Preserve;
            style.IndentationKind = formatter::IndentationKind::Tabs;
            ApplyProfile(cases, directory, "attributed-blocks-tabs", style);
        }
        {
            auto style = formatter::LoadConfigFile(directory / "authored-anchor.hit-fmt").FormattingStyle;
            style.BlockBodyIndentWidth = 2;
            ApplyProfile(cases, directory, "narrow-body", style);
        }
        ShareExpected(cases, directory, "correct-anchor.output.cpp", "authored-anchor.input.cpp");
        ShareExpected(cases, directory, "authored-anchor.output.cpp", "authored-anchor.input.cpp");
    }
    {
        const auto base = formatter::LoadConfigFile(std::filesystem::path(profile + ".hit-fmt")).FormattingStyle;
        for (const auto* name : {"protected-fitting", "protected-captures", "protected-parameters"}) {
            cases.push_back({lambdaAnchors, name, "protected-body.input.cpp", {}, {}, true, {}});
            auto style = base;
            if (std::string_view(name) == "protected-captures") {
                style.CapturesBreak = BreakRequirement::Always;
                style.CapturesCloser = CloserPlacement::SameLine;
            } else if (std::string_view(name) == "protected-parameters") {
                style.ParametersBreak = BreakRequirement::Always;
            }
            ApplyProfile(cases, lambdaAnchors, name, style);
        }
    }
    const auto controlBodies = data / "consolidated-formatter/control-bodies";
    for (const auto* name : {"nested-unbraced", "empty-blocks", "empty-blocks-without-add"}) {
        cases.push_back({controlBodies, name, std::string(name) + ".input.cpp"});
    }
    // Name, input, config override, read-only expectation alias.
    for (const auto& [name, input, config, expected] :
        {
            std::array<const char*, 4>{"crlf", "crlf.input.cpp", "comment-barrier", ""},
            {"complete-control-bodies", "complete-control-bodies.input.cpp", "comment-barrier", ""},
        })
    {
        cases.push_back({controlBodies, name, input, config, {}, true, expected});
    }
    {
        const auto directory = data / "consolidated-formatter/control-bodies";
        {
            const auto base = formatter::LoadConfigFile(directory / "comment-barrier.hit-fmt").FormattingStyle;
            struct Policy
            {
                const char* Profile;
                formatter::BodyShape EmptyBodyShape;
                bool AddMissingControlBraces;
                formatter::EmptyControlBody EmptyControlBody;
                formatter::GapPolicy EmptyBlockInteriorSpacing;
            };
            for (const auto& policy :
                std::array<Policy, 3>{
                    {
                        {
                            "empty-blocks-without-add",
                            BodyShape::CompactIfFits,
                            false,
                            EmptyControlBody::EmptyBlock,
                            GapPolicy::Space,
                        },
                        {
                            "empty-blocks",
                            BodyShape::CompactIfFits,
                            true,
                            EmptyControlBody::EmptyBlock,
                            GapPolicy::Space,
                        },
                        {
                            "nested-unbraced",
                            BodyShape::Expanded,
                            true,
                            EmptyControlBody::Preserve,
                            GapPolicy::GeneratedNoSpace,
                        },
                    },
                })
            {
                auto style = base;
                style.EmptyBodyShape = policy.EmptyBodyShape;
                style.AddMissingControlBraces = policy.AddMissingControlBraces;
                style.EmptyControlBody = policy.EmptyControlBody;
                style.EmptyBlockInteriorSpacing = policy.EmptyBlockInteriorSpacing;
                ApplyProfile(cases, directory, policy.Profile, style);
            }
        }
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
    AddCases(cases, callReflow, {"balanced-vertical", "balanced-preserve"}, "balanced-fill.input.cpp");
    // Name, input, config override, read-only expectation alias.
    for (const auto& [name, input, config, expected] :
        {
            std::array<const char*, 4>{"default-preserve", "input.cpp", "", "input.cpp"},
            {"always-fill", "long.input.cpp", "", "long-packed.output.cpp"},
            {"vertical-next-line", "input.cpp", "", "one-per-line.output.cpp"},
            {"crlf-call", "crlf-call.input.cpp", "unsafe-continuation", "crlf.output.cpp"},
            {"sole-fitting", "sole-fitting.input.cpp", "", "sole-fitting.output.cpp"},
            {"sole-separated", "sole-fitting.input.cpp", "", "sole-separated.output.cpp"},
            {"lambda-capture", "lambda-capture.input.cpp", "lambda-position", ""},
        })
    {
        cases.push_back({callReflow, name, input, config, {}, true, expected});
    }
    {
        const auto directory = data / "call-reflow";
        {
            const auto base = formatter::LoadConfigFile(directory / "sole-argument.hit-fmt").FormattingStyle;
            struct Policy
            {
                const char* Profile;
                std::optional<formatter::BreakRequirement> CallsBreak;
                std::optional<formatter::FirstItemPlacement> CallsFirstItem;
                std::optional<formatter::FirstItemPlacement>
                    CallsSoleArgumentFirstItem;
            };
            for (const auto& policy :
                std::array<Policy, 3>{
                    {
                        {
                            "sole-argument-always",
                            BreakRequirement::Always,
                            FirstItemPlacement::NextLine,
                            FirstItemPlacement::InlineIfFits,
                        },
                        {"sole-argument-inherit", std::nullopt, FirstItemPlacement::NextLine, std::nullopt},
                        {
                            "sole-argument-next-line",
                            std::nullopt,
                            FirstItemPlacement::InlineIfFits,
                            FirstItemPlacement::NextLine,
                        },
                    },
                })
            {
                auto style = base;
                style.CallsBreak = policy.CallsBreak;
                style.CallsFirstItem = policy.CallsFirstItem;
                style.CallsSoleArgumentFirstItem = policy.CallsSoleArgumentFirstItem;
                ApplyProfile(cases, directory, policy.Profile, style);
            }
        }
        {
            const auto base = formatter::LoadConfigFile(directory / "unsafe-continuation.hit-fmt").FormattingStyle;
            struct Policy
            {
                const char* Profile;
                std::size_t ColumnLimit;
                formatter::CategoryLayout CallsLayout;
                std::optional<formatter::BreakRequirement> CallsBreak;
                std::optional<formatter::ListPacking> CallsPacking;
                std::optional<formatter::FirstItemPlacement> CallsFirstItem;
                std::optional<formatter::ContinuationAlignment> CallsAlignment;
                formatter::CategoryLayout ControlHeadersLayout;
                formatter::BracePlacement ControlBracePlacement;
                formatter::GapPolicy SpaceInsideCallArguments;
            };
            for (const auto& policy :
                std::array<Policy, 7>{
                    {
                        {
                            "balanced-fill",
                            32,
                            CategoryLayout::Canonical,
                            BreakRequirement::IfNeeded,
                            ListPacking::Fill,
                            std::nullopt,
                            ContinuationAlignment::Block,
                            CategoryLayout::Canonical,
                            BracePlacement::NextLineIfHeaderBroken,
                            GapPolicy::Preserve,
                        },
                        {
                            "balanced-preserve",
                            32,
                            CategoryLayout::Preserve,
                            BreakRequirement::IfNeeded,
                            ListPacking::VerticalWhenBroken,
                            std::nullopt,
                            ContinuationAlignment::Block,
                            CategoryLayout::Canonical,
                            BracePlacement::NextLineIfHeaderBroken,
                            GapPolicy::Preserve,
                        },
                        {
                            "balanced-vertical",
                            32,
                            CategoryLayout::Canonical,
                            BreakRequirement::IfNeeded,
                            ListPacking::VerticalWhenBroken,
                            std::nullopt,
                            ContinuationAlignment::Block,
                            CategoryLayout::Canonical,
                            BracePlacement::NextLineIfHeaderBroken,
                            GapPolicy::Preserve,
                        },
                        {
                            "control-header-composition",
                            32,
                            CategoryLayout::Canonical,
                            BreakRequirement::Always,
                            ListPacking::Fill,
                            std::nullopt,
                            ContinuationAlignment::Block,
                            CategoryLayout::Canonical,
                            BracePlacement::NextLineIfHeaderBroken,
                            GapPolicy::GeneratedNoSpace,
                        },
                        {
                            "delimiter-alignment",
                            32,
                            CategoryLayout::Canonical,
                            BreakRequirement::Always,
                            ListPacking::Fill,
                            std::nullopt,
                            ContinuationAlignment::Delimiter,
                            CategoryLayout::Inherit,
                            BracePlacement::Attached,
                            GapPolicy::GeneratedNoSpace,
                        },
                        {
                            "next-line-reposition",
                            25,
                            CategoryLayout::Canonical,
                            BreakRequirement::Always,
                            ListPacking::Fill,
                            FirstItemPlacement::NextLine,
                            ContinuationAlignment::Delimiter,
                            CategoryLayout::Inherit,
                            BracePlacement::Attached,
                            GapPolicy::GeneratedNoSpace,
                        },
                        {
                            "spaced-vertical",
                            25,
                            CategoryLayout::Canonical,
                            BreakRequirement::Always,
                            ListPacking::VerticalWhenBroken,
                            std::nullopt,
                            ContinuationAlignment::Block,
                            CategoryLayout::Canonical,
                            BracePlacement::NextLineIfHeaderBroken,
                            GapPolicy::Space,
                        },
                    },
                })
            {
                auto style = base;
                style.ColumnLimit = policy.ColumnLimit;
                style.CallsLayout = policy.CallsLayout;
                style.CallsBreak = policy.CallsBreak;
                style.CallsPacking = policy.CallsPacking;
                style.CallsFirstItem = policy.CallsFirstItem;
                style.CallsAlignment = policy.CallsAlignment;
                style.ControlHeadersLayout = policy.ControlHeadersLayout;
                style.ControlBracePlacement = policy.ControlBracePlacement;
                style.SpaceInsideCallArguments = policy.SpaceInsideCallArguments;
                ApplyProfile(cases, directory, policy.Profile, style);
            }
        }
        {
            const auto base = formatter::LoadConfigFile(directory / "packed.hit-fmt").FormattingStyle;
            struct Policy
            {
                const char* Profile;
                formatter::CategoryLayout CallsLayout;
                std::optional<formatter::BreakRequirement> CallsBreak;
                std::optional<formatter::ListPacking> CallsPacking;
                std::optional<formatter::FirstItemPlacement> CallsFirstItem;
            };
            for (const auto& policy :
                std::array<Policy, 3>{
                    {
                        {"default-preserve", CategoryLayout::Inherit, std::nullopt, std::nullopt, std::nullopt},
                        {
                            "always-fill",
                            CategoryLayout::Canonical,
                            BreakRequirement::Always,
                            ListPacking::Fill,
                            std::nullopt,
                        },
                        {
                            "vertical-next-line",
                            CategoryLayout::Canonical,
                            BreakRequirement::Always,
                            ListPacking::VerticalWhenBroken,
                            FirstItemPlacement::NextLine,
                        },
                    },
                })
            {
                auto style = base;
                style.CallsLayout = policy.CallsLayout;
                style.CallsBreak = policy.CallsBreak;
                style.CallsPacking = policy.CallsPacking;
                style.CallsFirstItem = policy.CallsFirstItem;
                ApplyProfile(cases, directory, policy.Profile, style);
            }
        }
        {
            auto style = formatter::LoadConfigFile(directory / "nested-candidate.hit-fmt").FormattingStyle;
            style.CallsPacking = ListPacking::VerticalWhenBroken;
            ApplyProfile(cases, directory, "opaque-macro", style);
        }
        {
            auto style = formatter::LoadConfigFile(directory / "sole-fitting.hit-fmt").FormattingStyle;
            style.CallsFirstItem = FirstItemPlacement::NextLine;
            style.BodiesLayout = CategoryLayout::Canonical;
            ApplyProfile(cases, directory, "sole-separated", style);
        }
        ShareExpected(cases, directory, "sole-argument-preserve.output.cpp", "sole-argument.input.cpp");
        ShareExpected(cases, directory, "balanced-preserve.output.cpp", "balanced-fill.input.cpp");
        ShareExpected(cases, directory, "opaque-macro.output.cpp", "opaque-macro.input.cpp");
    }
    const auto ternaryLayout = data / "ternary-layout";
    cases.push_back({ternaryLayout, "preserve-always-narrow", "preserve-always-narrow.input.cpp"});
    AddCases(
        cases,
        ternaryLayout,
        {"preserve-always-trailing", "preserve-authored-flexible"},
        "authored-fitting.input.cpp");
    // Name, input, config override, read-only expectation alias.
    for (const auto& [name, input, config, expected] :
        {
            std::array<const char*, 4>{"two-leading", "input.cpp", "", "two-leading.output.cpp"},
            {"three-leading", "input.cpp", "", "three-leading.output.cpp"},
            {"preserve-leading", "input.cpp", "", "input.cpp"},
            {"fitting-leading", "fitting-leading.input.cpp", "three-leading", ""},
            {"two-trailing", "input.cpp", "", "two-trailing.output.cpp"},
            {"three-trailing", "input.cpp", "", "three-trailing.output.cpp"},
            {"preserve-trailing", "input.cpp", "", "input.cpp"},
            {"fitting-trailing", "fitting-leading.input.cpp", "three-trailing", ""},
            {"always-leading", "authored-fitting.input.cpp", "", "three-leading.output.cpp"},
            {"always-trailing", "authored-fitting.input.cpp", "", "three-trailing.output.cpp"},
            {"crlf-fitting", "crlf-fitting.input.cpp", "flat-trailing-fitting", ""},
            {"authored-ternary-topologies", "authored-ternary-topologies.input.cpp", "authored-fitting", ""},
            {"fitting-three-part", "fitting-three-part.input.cpp", "three-part-fitting", ""},
            {"trailing-ternary-topologies", "trailing-ternary-topologies.input.cpp", "flat-trailing-fitting", ""},
        })
    {
        cases.push_back({ternaryLayout, name, input, config, {}, true, expected});
    }
    {
        const auto directory = data / "ternary-layout";
        {
            const auto base = formatter::LoadConfigFile(directory / "two-leading.hit-fmt").FormattingStyle;
            struct Policy
            {
                const char* Profile;
                std::size_t ColumnLimit;
                formatter::BreakRequirement ExpressionsBreak;
                formatter::CategoryLayout ExpressionsLayout;
                formatter::OperatorSide TernaryOperatorSide;
                formatter::TernaryLayout TernaryLayout;
            };
            for (const auto& policy :
                std::array<Policy, 13>{
                    {
                        {
                            "always-leading",
                            80,
                            BreakRequirement::Always,
                            CategoryLayout::Canonical,
                            OperatorSide::Leading,
                            TernaryLayout::ThreePart,
                        },
                        {
                            "always-trailing",
                            80,
                            BreakRequirement::Always,
                            CategoryLayout::Canonical,
                            OperatorSide::Trailing,
                            TernaryLayout::ThreePart,
                        },
                        {
                            "authored-fitting",
                            80,
                            BreakRequirement::IfNeeded,
                            CategoryLayout::Canonical,
                            OperatorSide::Leading,
                            TernaryLayout::Flexible,
                        },
                        {
                            "flat-trailing-fitting",
                            80,
                            BreakRequirement::IfNeeded,
                            CategoryLayout::Canonical,
                            OperatorSide::Trailing,
                            TernaryLayout::ThreePart,
                        },
                        {
                            "preserve-always-narrow",
                            20,
                            BreakRequirement::Always,
                            CategoryLayout::Preserve,
                            OperatorSide::Leading,
                            TernaryLayout::Flexible,
                        },
                        {
                            "preserve-always-trailing",
                            80,
                            BreakRequirement::Always,
                            CategoryLayout::Preserve,
                            OperatorSide::Trailing,
                            TernaryLayout::ThreePart,
                        },
                        {
                            "preserve-authored-flexible",
                            80,
                            BreakRequirement::IfNeeded,
                            CategoryLayout::Preserve,
                            OperatorSide::Trailing,
                            TernaryLayout::Flexible,
                        },
                        {
                            "preserve-leading",
                            40,
                            BreakRequirement::IfNeeded,
                            CategoryLayout::Preserve,
                            OperatorSide::Leading,
                            TernaryLayout::Flexible,
                        },
                        {
                            "preserve-trailing",
                            40,
                            BreakRequirement::IfNeeded,
                            CategoryLayout::Preserve,
                            OperatorSide::Trailing,
                            TernaryLayout::Flexible,
                        },
                        {
                            "three-leading",
                            40,
                            BreakRequirement::IfNeeded,
                            CategoryLayout::Canonical,
                            OperatorSide::Leading,
                            TernaryLayout::ThreePart,
                        },
                        {
                            "three-part-fitting",
                            80,
                            BreakRequirement::IfNeeded,
                            CategoryLayout::Canonical,
                            OperatorSide::Leading,
                            TernaryLayout::ThreePart,
                        },
                        {
                            "three-trailing",
                            40,
                            BreakRequirement::IfNeeded,
                            CategoryLayout::Canonical,
                            OperatorSide::Trailing,
                            TernaryLayout::ThreePart,
                        },
                        {
                            "two-trailing",
                            40,
                            BreakRequirement::IfNeeded,
                            CategoryLayout::Canonical,
                            OperatorSide::Trailing,
                            TernaryLayout::Flexible,
                        },
                    },
                })
            {
                auto style = base;
                style.ColumnLimit = policy.ColumnLimit;
                style.ExpressionsBreak = policy.ExpressionsBreak;
                style.ExpressionsLayout = policy.ExpressionsLayout;
                style.TernaryOperatorSide = policy.TernaryOperatorSide;
                style.TernaryLayout = policy.TernaryLayout;
                ApplyProfile(cases, directory, policy.Profile, style);
            }
        }
        {
            const auto base = formatter::LoadConfigFile(directory / "branches-preserve.hit-fmt").FormattingStyle;
            struct Policy
            {
                const char* Profile;
                formatter::LayoutMode Layout;
                std::size_t ColumnLimit;
                formatter::OperatorSide TernaryOperatorSide;
                formatter::TernaryLayout TernaryLayout;
                formatter::TernaryOperatorIndentation OperatorIndentation;
            };
            for (const auto& policy :
                std::array<Policy, 4>{
                    {
                        {
                            "branches-indent-narrow",
                            LayoutMode::Canonical,
                            40,
                            OperatorSide::Leading,
                            TernaryLayout::ThreePart,
                            TernaryOperatorIndentation::Indent,
                        },
                        {
                            "branches-indent-preserve",
                            LayoutMode::Preserve,
                            80,
                            OperatorSide::Leading,
                            TernaryLayout::Flexible,
                            TernaryOperatorIndentation::Indent,
                        },
                        {
                            "branches-indent-trailing",
                            LayoutMode::Canonical,
                            40,
                            OperatorSide::Trailing,
                            TernaryLayout::ThreePart,
                            TernaryOperatorIndentation::Indent,
                        },
                        {
                            "branches-narrow",
                            LayoutMode::Canonical,
                            40,
                            OperatorSide::Leading,
                            TernaryLayout::ThreePart,
                            TernaryOperatorIndentation::Align,
                        },
                    },
                })
            {
                auto style = base;
                style.Layout = policy.Layout;
                style.ColumnLimit = policy.ColumnLimit;
                style.TernaryOperatorSide = policy.TernaryOperatorSide;
                style.TernaryLayout = policy.TernaryLayout;
                style.TernaryOperatorIndentation = policy.OperatorIndentation;
                ApplyProfile(cases, directory, policy.Profile, style);
            }
        }
        {
            auto style = formatter::LoadConfigFile(directory / "operators-align.hit-fmt").FormattingStyle;
            style.TernaryOperatorIndentation = TernaryOperatorIndentation::Indent;
            ApplyProfile(cases, directory, "operators-indent", style);
        }
        {
            const auto base = formatter::LoadConfigFile(directory / "initializer-shape.hit-fmt").FormattingStyle;
            struct Policy
            {
                const char* Profile;
                std::size_t ColumnLimit;
                formatter::TernaryLayout TernaryLayout;
            };
            for (const auto& policy :
                std::array<Policy, 2>{
                    {
                        {"initializer-shape-flexible", 72, TernaryLayout::Flexible},
                        {"initializer-shape-narrow", 40, TernaryLayout::ThreePart},
                    },
                })
            {
                auto style = base;
                style.ColumnLimit = policy.ColumnLimit;
                style.TernaryLayout = policy.TernaryLayout;
                ApplyProfile(cases, directory, policy.Profile, style);
            }
        }
        ShareExpected(cases, directory, "branches-narrow.output.cpp", "branches-indent-narrow.output.cpp");
        ShareExpected(cases, directory, "preserve-always-narrow.output.cpp", "preserve-always-narrow.input.cpp");
        ShareExpected(cases, directory, "fitting-leading.output.cpp", "fitting-leading.input.cpp");
        ShareExpected(cases, directory, "fitting-trailing.output.cpp", "fitting-leading.input.cpp");
        ShareExpected(cases, directory, "preserve-always-trailing.output.cpp", "authored-fitting.input.cpp");
        ShareExpected(cases, directory, "preserve-authored-flexible.output.cpp", "authored-fitting.input.cpp");

    }
    const auto emptyFunctions = data / "consolidated-formatter/empty-functions";
    cases.push_back({emptyFunctions, "empty-function-bodies", "empty-function-bodies.input.cpp", "whitespace-only"});
    const auto consolidatedLayout = data / "consolidated-layout";
    for (const auto* name : {"multiline-comments", "multiline-comments-tabs"}) {
        cases.push_back({consolidatedLayout, name, "multiline-comments.input.cpp", {}, {}, true, {}});
    }
    cases.push_back({
        consolidatedLayout,
        "multiline-comments-crlf",
        "multiline-comments-crlf.input.cpp",
        "multiline-comments",
        {},
        true,
        {},
    });
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
        cases.push_back({consolidatedLayout, name, std::string(name) + ".input.cpp"});
    }
    AddCases(cases, consolidatedLayout, {"control-attached", "control-header-broken"}, "control-arms.input.cpp");
    AddCases(cases, consolidatedLayout, {"control-compact", "control-forced-header"}, "control-new-line.input.cpp");
    AddCases(cases, consolidatedLayout, {"control-compact-fit"}, "control-compact-fit.input.cpp");
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
    for (const auto& [name, input, config, expected] :
        {
            std::array<const char*, 4>{"comment-namespace-only", "comment-external.input.cpp", "", ""},
            {"comment-sequence-zero", "comment-sequence-zero.input.cpp", "../standalone-comment-gaps/zero", ""},
        })
    {
        cases.push_back({consolidatedLayout, name, input, config, {}, true, expected});
    }
    {
        const auto directory = data / "consolidated-layout";
        {
            const auto base =
                formatter::LoadConfigFile(directory / "record-head-space-canonical.hit-fmt").FormattingStyle;
            struct Policy
            {
                const char* Profile;
                formatter::LayoutMode Layout;
                formatter::GapPolicy CommaSpaceAfter;
            };
            for (const auto& policy :
                std::array<Policy, 10>{
                    {
                        {"record-head-boundaries", LayoutMode::Canonical, GapPolicy::Space},
                        {
                            "record-head-generated-no-space-canonical",
                            LayoutMode::Canonical,
                            GapPolicy::GeneratedNoSpace,
                        },
                        {"record-head-generated-no-space-preserved", LayoutMode::Preserve, GapPolicy::GeneratedNoSpace},
                        {"record-head-generated-space-canonical", LayoutMode::Canonical, GapPolicy::GeneratedSpace},
                        {"record-head-generated-space-preserved", LayoutMode::Preserve, GapPolicy::GeneratedSpace},
                        {"record-head-no-space-canonical", LayoutMode::Canonical, GapPolicy::NoSpace},
                        {"record-head-no-space-preserved", LayoutMode::Preserve, GapPolicy::NoSpace},
                        {"record-head-preserve-canonical", LayoutMode::Canonical, GapPolicy::Preserve},
                        {"record-head-preserved", LayoutMode::Preserve, GapPolicy::Preserve},
                        {"record-head-space-preserved", LayoutMode::Preserve, GapPolicy::Space},
                    },
                })
            {
                auto style = base;
                style.Layout = policy.Layout;
                style.CommaSpaceAfter = policy.CommaSpaceAfter;
                ApplyProfile(cases, directory, policy.Profile, style);
            }
        }
        {
            const auto base = formatter::LoadConfigFile(directory / "control-new-line.hit-fmt").FormattingStyle;
            struct Policy
            {
                const char* Profile;
                std::optional<formatter::BreakRequirement> ControlHeadersBreak;
                std::optional<formatter::FirstItemPlacement>
                    ControlHeadersFirstItem;
                std::optional<formatter::CloserPlacement> ControlHeadersCloser;
                formatter::CategoryLayout BodiesLayout;
                formatter::BodyShape BodyShape;
                formatter::BracePlacement ControlBracePlacement;
            };
            for (const auto& policy :
                std::array<Policy, 5>{
                    {
                        {
                            "control-attached",
                            std::nullopt,
                            std::nullopt,
                            std::nullopt,
                            CategoryLayout::Inherit,
                            BodyShape::Expanded,
                            BracePlacement::Attached,
                        },
                        {
                            "control-compact",
                            std::nullopt,
                            std::nullopt,
                            std::nullopt,
                            CategoryLayout::Inherit,
                            BodyShape::CompactIfFits,
                            BracePlacement::NextLine,
                        },
                        {
                            "control-forced-header",
                            BreakRequirement::Always,
                            FirstItemPlacement::NextLine,
                            CloserPlacement::OwnLine,
                            CategoryLayout::Inherit,
                            BodyShape::Expanded,
                            BracePlacement::NextLineIfHeaderBroken,
                        },
                        {
                            "control-header-broken",
                            std::nullopt,
                            std::nullopt,
                            std::nullopt,
                            CategoryLayout::Inherit,
                            BodyShape::Expanded,
                            BracePlacement::NextLineIfHeaderBroken,
                        },
                        {
                            "control-preserved-body",
                            std::nullopt,
                            std::nullopt,
                            std::nullopt,
                            CategoryLayout::Preserve,
                            BodyShape::Expanded,
                            BracePlacement::NextLine,
                        },
                    },
                })
            {
                auto style = base;
                style.ControlHeadersBreak = policy.ControlHeadersBreak;
                style.ControlHeadersFirstItem = policy.ControlHeadersFirstItem;
                style.ControlHeadersCloser = policy.ControlHeadersCloser;
                style.BodiesLayout = policy.BodiesLayout;
                style.BodyShape = policy.BodyShape;
                style.ControlBracePlacement = policy.ControlBracePlacement;
                ApplyProfile(cases, directory, policy.Profile, style);
            }
        }
        ShareConfig(cases, directory, "comment-sequence", "comment-directives");
        ShareConfig(cases, directory, "preserve-crlf-recovery", "../closing-delimiters/preserve");
        ShareExpected(
            cases,
            directory,
            "record-head-generated-space-preserved.output.cpp",
            "record-head-policies.input.cpp");
        ShareExpected(cases, directory, "record-head-preserved.output.cpp", "record-head-policies.input.cpp");
        ShareExpected(
            cases,
            directory,
            "record-head-generated-no-space-preserved.output.cpp",
            "record-head-policies.input.cpp");
        ShareExpected(cases, directory, "comment-sequence-zero.output.cpp", "comment-sequence.input.cpp");
        ShareExpected(cases, directory, "comment-sequence.output.cpp", "comment-sequence-zero.input.cpp");
        ShareExpected(
            cases,
            directory,
            "record-head-no-space-canonical.output.cpp",
            "record-head-generated-no-space-canonical.output.cpp");
        ShareExpected(cases, directory, "control-header-broken.output.cpp", "control-attached.output.cpp");
        ShareExpected(cases, directory, "preserve-crlf-recovery.output.cpp", "preserve-crlf-recovery.input.cpp");
        ShareExpected(
            cases,
            directory,
            "record-head-space-canonical.output.cpp",
            "record-head-generated-space-canonical.output.cpp");
    }
    AddCases(
        cases,
        data / "consolidated-pipeline/enumerator-assignment",
        {"space", "no-space", "preserve", "generated-space", "generated-no-space"});
    {
        // Enumerator assignment shares oracles, not decoded policy configs.
        const auto directory = data / "consolidated-pipeline/enumerator-assignment";
        ShareExpected(cases, directory, "generated-space.output.cpp", "input.cpp");
        ShareExpected(cases, directory, "preserve.output.cpp", "input.cpp");
        ShareExpected(cases, directory, "generated-no-space.output.cpp", "input.cpp");
    }
    cases.push_back({
        data / "consolidated-pipeline",
        "enumerator-assignment-comments",
        "enumerator-assignment-comments.input.cpp",
    });
    cases.push_back({data / "consolidated-edits", "null-removal", "null-removal.input.cpp"});
    {
        const auto directory = data / "consolidated-edits";
        ShareConfig(cases, directory, "null-removal", "../null-statements/both");
    }
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
    const auto style = value.Style
        ? *value.Style
        : formatter::LoadConfigFile(fixture.Directory / (config + ".hit-fmt")).FormattingStyle;
    const auto input = fixture.Input(value.Input);
    if (value.Hunks.empty()) {
        const auto expected = !value.GoldenPath.empty()
            ? value.GoldenPath
            : value.Expected.empty() ? fixture.Named(".output.cpp") : fixture.Directory / value.Expected;
        ExpectFormattedGolden(expected, input, style, !value.Expected.empty(), value.InputFilePath);
    } else {
        const Input storage(input);
        const auto tree = parser::Parse(storage.View(), [] (parser::ParseError) { });
        const auto formatted = formatter::FormatChanged(
            tree,
            formatter::PreparedFormat(style),
            value.Hunks,
            value.Spillover,
            {.InputFilePath = value.InputFilePath}).Result.Text;
        if (value.Expected.empty()) {
            fixture.Expect(formatted);
        } else {
            EXPECT_EQ(formatted, ReadFile(fixture.Directory / value.Expected));
        }
        const auto detailed = formatter::FormatChanged(
            tree,
            formatter::PreparedFormat(style),
            value.Hunks,
            value.Spillover,
            {.InputFilePath = value.InputFilePath, .Changes = true}).Result;
        EXPECT_EQ(detailed.Text, formatted);
        test::ExpectFormatChanges(input, detailed);
        const auto summary = formatter::FormatChanged(
            tree,
            formatter::PreparedFormat(style),
            value.Hunks,
            value.Spillover,
            {.InputFilePath = value.InputFilePath, .ChangeSummary = true}).Result;
        test::ExpectChangeSummary(summary, detailed);
    }
}
INSTANTIATE_TEST_SUITE_P(
    Formatter,
    FormatTest,
    testing::ValuesIn(FormatCases()),
    [] (const auto& info) {
        if (!info.param.TestName.empty()) {
            return info.param.TestName;
        }
        const auto root = (std::filesystem::path(FORMATTER_TESTDATA_DIR) / "..").lexically_normal();
        auto name =
            (info.param.Directory.lexically_normal().lexically_relative(root) / info.param.Name).generic_string();
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
    const auto directory = std::filesystem::path(FORMATTER_TESTDATA_DIR) / "../parameter_names";
    const auto input = ReadFile(directory / "input.cpp");
    const bool ytsaurus = GetParam() == "ytsaurus";
    const auto configPath = ytsaurus
        ? (directory / "../../../../hit-fmt/examples/ytsaurus.hit-fmt").lexically_normal()
        : directory / (GetParam() + ".hit-fmt");
    const auto config = formatter::LoadConfigFile(configPath);
    const Input storage(input);
    const auto tree = parser::Parse(storage.View(), [] (parser::ParseError) { });
    std::string actual;
    for (const auto& diagnostic : formatter::CheckParameterNames(tree, config)) {
        actual += std::to_string(diagnostic.Position.Line) + ":" + std::to_string(diagnostic.Position.Column) + " [" +
            std::to_string(diagnostic.SignatureBeginLine) + "-" + std::to_string(diagnostic.SignatureEndLine) + "]: " +
            diagnostic.Function + ", parameter #" + std::to_string(diagnostic.ParameterNumber) +
            (diagnostic.AllowCommentedName ? " (comment allowed)" : "") + "\n";
    }
    test::golden::ExpectGolden(directory / (ytsaurus ? "enabled.expected" : GetParam() + ".expected"), actual);
}

INSTANTIATE_TEST_SUITE_P(
    Formatter,
    ParameterNameTest,
    testing::Values("enabled", "default", "declarations_only", "definitions_only", "names_only", "ytsaurus"),
    ConfigName);

TEST(Formatter, CallArgument_RejectsUnsupportedSyntaxHints)
{
    const test::golden::Fixture fixture{
        std::filesystem::path(FORMATTER_TESTDATA_DIR) / "../call-like-macro",
        "selected",
    };
    const auto style = formatter::LoadConfigFile(fixture.Named(".hit-fmt")).FormattingStyle;
    const auto input = fixture.Input();
    const Input storage(input);
    const auto source = storage.View();
    const auto tree = parser::Parse(source, [] (parser::ParseError) { });
    for (bool parsed : {false, true}) {
        SCOPED_TRACE(parsed);
        try {
            const auto output = parsed ? formatter::Format(tree, style) : formatter::Format(source, style);
            FAIL() << "unsupported syntax hints produced output: " << output;
        } catch (const std::invalid_argument& error) {
            EXPECT_EQ(
                std::string_view(error.what()),
                "CallLikeMacroNames syntax hints are not yet supported " "by the public pipeline");
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
        "configured",
    };
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

TEST(Formatter, RequiresLambdasFitAfterBreakingHeadersAndBodies)
{
    const auto directory = std::filesystem::path(FORMATTER_TESTDATA_DIR) / "../requires-lambdas";
    auto style = formatter::LoadConfigFile(directory / "canonical.hit-fmt").FormattingStyle;
    style.WidthDiagnostics = formatter::WidthDiagnosticPolicy::Report;
    for (const auto& path : {directory / "input.cpp", std::filesystem::path(PARSER_TESTDATA_DIR) / "requires_forms.in"})
    {
        SCOPED_TRACE(path);
        const Input source(ReadFile(path));
        const auto input = source.View();
        const auto tree = parser::Parse(input, [] (parser::ParseError) { });
        for (const auto columns : {80u, 60u}) {
            SCOPED_TRACE(columns);
            style.ColumnLimit = columns;
            const auto result = formatter::Format(tree, formatter::PreparedFormat(style));
            EXPECT_TRUE(result.Diagnostics.empty());
            if (path.filename() == "input.cpp") {
                EXPECT_NE(result.Text.find("[]<\n"), std::string::npos);
                EXPECT_NE(result.Text.find("\n        return combine("), std::string::npos);
            }
        }
    }
}

TEST(Formatter, ExpressionLayout_ChecksPipelineMismatchAndEmittedWidth)
{
    const auto input = "int f() {\n" "    auto description = someVeryLongIdentifierName;\n" "    auto x = left\n"
        "        + right;\n" "    return condition ?\n" "        alphaValue : betaValue;\n" "}\n";
    const Input storage(input);
    const auto tree = parser::Parse(storage.View(), [] (parser::ParseError) { });
    formatter::Style style;
    style.ColumnLimit = 40;
    style.WidthDiagnostics = formatter::WidthDiagnosticPolicy::Report;
    // Width belongs to the emitted result, including unavoidable atoms.
    style.ExpressionsLayout = formatter::CategoryLayout::Canonical;
    style.ExpressionsBreak = formatter::BreakRequirement::Always;
    style.BinaryOperatorSide = formatter::OperatorSide::Trailing;
    style.TernaryOperatorSide = formatter::OperatorSide::Leading;
    style.TernaryLayout = formatter::TernaryLayout::ThreePart;
    const auto result = formatter::Format(tree, formatter::PreparedFormat(style));
    EXPECT_NE(result.Text, input);
    EXPECT_NE(result.Text.find("left +\n"), std::string::npos);
    EXPECT_NE(result.Text.find("\n        ? alphaValue\n"), std::string::npos);
    EXPECT_NE(result.Text.find("\n        : betaValue"), std::string::npos);
    // The selected layout fits: source overflow must not leak into diagnostics.
    EXPECT_TRUE(result.Diagnostics.empty());
    style.ExpressionsLayout = formatter::CategoryLayout::Preserve;
    const auto preserved = formatter::Format(tree, formatter::PreparedFormat(style));
    EXPECT_EQ(preserved.Text, input);
    ASSERT_EQ(preserved.Diagnostics.size(), 1u);
    EXPECT_EQ(preserved.Diagnostics[0].Position.Line, 2u);
    EXPECT_EQ(preserved.Diagnostics[0].Kind, formatter::LayoutDiagnosticKind::LineWidth);
    EXPECT_NE(preserved.Diagnostics[0].Message.find("exceeds ColumnLimit"), std::string::npos);
    style.WidthDiagnostics = formatter::WidthDiagnosticPolicy::Off;
    const auto quiet = formatter::Format(tree, formatter::PreparedFormat(style));
    EXPECT_EQ(quiet.Text, input);
    EXPECT_TRUE(quiet.Diagnostics.empty());
    style.ExpressionsLayout = formatter::CategoryLayout::Canonical;
    const auto mismatchOnly = formatter::Format(tree, formatter::PreparedFormat(style));
    EXPECT_EQ(mismatchOnly.Text, result.Text);
    EXPECT_TRUE(mismatchOnly.Diagnostics.empty());
}

TEST(Formatter, InitializedDeclaratorBreaksResolveWidthDiagnostics)
{
    const auto directory = std::filesystem::path(FORMATTER_TESTDATA_DIR) / "../declaration-headers";
    const Input source(ReadFile(directory / "assignment-prefix.input.cpp"));
    const auto tree = parser::Parse(source.View(), [] (parser::ParseError) { });
    auto style = formatter::LoadConfigFile(directory / "../style/repository-80.hit-fmt").FormattingStyle;
    for (const auto side : {OperatorSide::Leading, OperatorSide::Trailing}) {
        style.AssignmentOperatorSide = side;
        for (const auto placement : {FirstItemPlacement::InlineIfFits, FirstItemPlacement::NextLine}) {
            style.AssignmentRhsPlacement = placement;
            const auto result = formatter::Format(tree, formatter::PreparedFormat(style));
            EXPECT_TRUE(result.Diagnostics.empty()) << result.Text;
        }
    }
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
    const auto directory = std::filesystem::path(FORMATTER_TESTDATA_DIR) / ".." / value.Directory;
    const Input source(ReadFile(directory / value.Input));
    const auto tree = parser::Parse(source.View(), [] (parser::ParseError) { });
    const auto config = value.Config.empty() ? value.Name : value.Config;
    auto style = formatter::LoadConfigFile(directory / (config + ".hit-fmt")).FormattingStyle;
    if (value.StrictLength) {
        style.WidthDiagnostics = formatter::WidthDiagnosticPolicy::Report;
    }
    // Public diagnostics describe emitted widths, not source-layout
    // feasibility.
    std::string actual;
    const auto result = formatter::Format(tree, formatter::PreparedFormat(style));
    for (const auto& diagnostic : result.Diagnostics) {
        actual += std::to_string(diagnostic.Position.Line) + ":" + std::to_string(diagnostic.Position.Column) + ": " +
            diagnostic.Message + "\n";
    }
    test::golden::ExpectGolden(directory / (value.Name + value.Suffix), actual);
}

// Check the emitted text independently of the diagnostic golden. Public
// formatting uses eight-column tab stops; CRLF is one physical terminator.

TEST(Formatter, WidthDiagnostics_CountsProtectedTabsAndMixedTerminators)
{
    const std::string input = "// hit-fmt off\r\nint\tlong_name;\r\n" "\tint long_name;\n" "// é界😀é界😀é界😀é界😀é\r\n"
        "// é界😀é界😀é界😀é界😀é界\r" "// é界😀\té界😀é界😀é界\n" "// é界😀\té界😀é界😀é界😀\n" + std::string("// ") + std::string(13, 'a') +
        "\x80\xe2\x82é\n" + "// hit-fmt on\n";
    const Input source(input);
    const auto tree = parser::Parse(source.View(), [] (parser::ParseError) { });
    formatter::Style style;
    style.ColumnLimit = 16;
    style.WidthDiagnostics = formatter::WidthDiagnosticPolicy::Report;
    style.TrailingWhitespace = formatter::TrailingWhitespace::Trim;
    const auto result = formatter::Format(tree, formatter::PreparedFormat(style));
    EXPECT_EQ(result.Text, input);
    ASSERT_EQ(result.Diagnostics.size(), 5u);
    EXPECT_EQ(result.Diagnostics[0].Position.Line, 2u);
    EXPECT_EQ(result.Diagnostics[0].Message, "emitted line width 18 exceeds ColumnLimit 16");
    EXPECT_EQ(result.Diagnostics[1].Position.Line, 3u);
    EXPECT_EQ(result.Diagnostics[1].Message, "emitted line width 22 exceeds ColumnLimit 16");
    EXPECT_EQ(result.Diagnostics[2].Position.Line, 5u);
    EXPECT_EQ(result.Diagnostics[3].Position.Line, 7u);
    for (size_t index = 2; index < 4; ++index) {
        EXPECT_EQ(result.Diagnostics[index].Message, "emitted line width 17 exceeds ColumnLimit 16");
    }
    EXPECT_EQ(result.Diagnostics[4].Position.Line, 8u);
    EXPECT_EQ(result.Diagnostics[4].Message, "emitted line width 20 exceeds ColumnLimit 16");
    EXPECT_EQ(FormatWithStyle(result.Text, style), result.Text);
}
INSTANTIATE_TEST_SUITE_P(
    Formatter,
    LayoutDiagnosticTest,
    testing::Values(
        LayoutDiagnosticCase{"format-disabled", "regions", "regions.input.cpp", {}, false, ".layout.expected"},
        LayoutDiagnosticCase{"format-disabled", "regions-no-clang", "regions.input.cpp", {}, false, ".layout.expected"},
        LayoutDiagnosticCase{"line-length", "soft"},
        LayoutDiagnosticCase{"line-length", "strict", "input.cpp", "soft", true},
        LayoutDiagnosticCase{"assignment-location", "leading"},
        LayoutDiagnosticCase{"assignment-location", "trailing"},
        LayoutDiagnosticCase{
            "call-reflow",
            "typed-declarations",
            "typed-declarations.input.cpp",
            "../style/repository-80",
            true,
            ".layout.expected",
        }),
    [] (const auto& info) {
        auto name = info.param.Directory + "_" + info.param.Name;
        std::ranges::replace(name, '-', '_');
        return name;
    });
TEST(Formatter, ExpressionLayout_ReportsUnavoidableWidthWithoutReflowing)
{
    const auto directory = std::filesystem::path(FORMATTER_TESTDATA_DIR) / "../line-length";
    const auto input = ReadFile(directory / "input.cpp");
    const Input source(input);
    const auto tree = parser::Parse(source.View(), [] (parser::ParseError) { });
    auto style = formatter::LoadConfigFile(directory / "soft.hit-fmt").FormattingStyle;
    style.ExpressionsLayout = formatter::CategoryLayout::Preserve;
    style.WidthDiagnostics = formatter::WidthDiagnosticPolicy::Report;
    const auto reported = formatter::Format(tree, formatter::PreparedFormat(style));
    EXPECT_EQ(reported.Text, input);
    ASSERT_EQ(reported.Diagnostics.size(), 4u);
    for (const auto& diagnostic : reported.Diagnostics) {
        EXPECT_EQ(diagnostic.Kind, formatter::LayoutDiagnosticKind::LineWidth);
    }
    // Report includes unavoidable overflow, not just fixable layout failures.
    style.WidthDiagnostics = formatter::WidthDiagnosticPolicy::Off;
    const auto quiet = formatter::Format(tree, formatter::PreparedFormat(style));
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
    const std::string source = "void f(int a, int b) {\n" "    auto address = (UnknownType) &a;\n"
        "    auto pointer = (UnknownType)*b;\n" "    auto qualified = (NS::UnknownType) &a;\n" "}\n";
    // Canonical Always cannot manufacture authority for ambiguous operators.
    ExpectUnchanged(source, style);
    // Certain expressions remain editable. Test their horizontal policies
    // without requiring canonical Always to retain their original topology.
    style.ExpressionsLayout = formatter::CategoryLayout::Preserve;
    const std::string certain =
        "void f(int a, int b) {\n" "    auto certain = (a + b) & b;\n" "    auto knownCast = (int)&a;\n" "}\n";
    ExpectUnchanged(certain, style);
    const auto compact =
        std::string(certain).replace(certain.find("(a + b) & b"), std::string("(a + b) & b").size(), "(a + b)&b");
    EXPECT_EQ(FormatWithStyle(compact, style), certain);
    const auto separatedCast =
        std::string(certain).replace(certain.find("(int)&a"), std::string("(int)&a").size(), "(int) & a");
    EXPECT_EQ(FormatWithStyle(separatedCast, style), certain);
}

TEST(Formatter, SpacingOptions_ProfileOverflowHasNoDiagnostics)
{
    const auto directory = std::filesystem::path(FORMATTER_TESTDATA_DIR) / "../spacing-options";
    const auto input = ReadFile(directory / "profile-overflow.input.cpp");
    const auto config = formatter::LoadConfigFile(
        (directory / "../../../../hit-fmt/examples/ytsaurus.hit-fmt").lexically_normal()).FormattingStyle;
    const auto result = test::FormatInput(input, formatter::PreparedFormat(config));
    EXPECT_TRUE(result.Diagnostics.empty());
}

class NamingTest
    : public testing::TestWithParam<std::string>
{ };

TEST_P(NamingTest, Golden)
{
    const auto dir = std::filesystem::path(NAMING_TESTDATA_DIR);
    const auto name = GetParam();
    const auto input = ReadFile(dir / (name == "rules" ? "input.cpp" : "alternate.cpp"));
    const auto config = formatter::LoadConfigFile(dir / (name + ".hit-fmt"));
    const Input storage(input);
    const auto tree = parser::Parse(storage.View(), [] (parser::ParseError) { });
    std::string actual;
    for (const auto& diagnostic : formatter::CheckNames(tree, config)) {
        actual += std::to_string(diagnostic.Position.Line) + ":" + std::to_string(diagnostic.Position.Column) + ": " +
            diagnostic.Message + "\n";
    }
    test::golden::ExpectGolden(dir / (name + ".expected"), actual);
    EXPECT_EQ(formatter::Format(tree, config.FormattingStyle), input);
    EXPECT_TRUE(formatter::CheckNames(tree, {}).empty());
}

INSTANTIATE_TEST_SUITE_P(Formatter, NamingTest, testing::Values("rules", "alternate", "no_exempt"), ConfigName);

TEST(Formatter, Preserve_KeepsEmptyRecoveryAndBinarySource)
{
    for (const std::string source :
        {
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
    const auto path = std::filesystem::temp_directory_path() / "hit-fmt-preserve-config-test.hit-fmt";
    auto save = [&] (std::string_view contents) {
        std::ofstream output(path);
        ASSERT_TRUE(output.is_open());
        output << contents;
    };
    save("IndentWidth = 2\nColumnLimit = 40\n" "ExpressionsLayout = Canonical\nExpressionsBreak = IfNeeded\n"
        "BinaryOperatorSide = Trailing\nAssignmentOperatorSide = Trailing\n" "StatementAttributeIndent = Block\n"
        "AccessModifierIndentWidth = 1\nMemberIndentWidth = 3\n" "BlockBodyIndentWidth = 4\nCaseLabelIndentWidth = 1\n"
        "StatementAttributeIndentWidth = 2\nNamespaceCommentSpaces = 2\n"
        "CommaSpaceBefore = NoSpace\nCommaSpaceAfter = Space\n"
        "TernaryOperatorSide = Trailing\nTernaryLayout = ThreePart\n" "CallsLayout = Canonical\nCallsPacking = Fill\n"
        "CallsFirstItem = NextLine\nCallsAlignment = Delimiter\n"
        "CallLineFirstStringNames = [\"YT_LOG_INFO\", \"YT_LOG_ERROR\"]\n"
        "IndentedCallScopePairs = [\"OnBeginList:OnEndList\"]\n" "Exclude = [ # list comment\n"
        "  \"x\\\\]y\", # bracket in a quoted item\n" "  \"(^|/)generated(/|$)\"\n" "]\n" "ClassNamePrefixes = [\n"
        "  \"S\", # prefix comment\n" "  \"C\"\n" "]\n" "IndentWidth = 3\n");
    const auto config = formatter::LoadConfigFile(path);
    const auto& style = config.FormattingStyle;
    EXPECT_EQ(style.IndentWidth, 3u);
    EXPECT_EQ(style.ColumnLimit, 40u);
    EXPECT_EQ(style.ExpressionsLayout, formatter::CategoryLayout::Canonical);
    EXPECT_EQ(style.ExpressionsBreak, formatter::BreakRequirement::IfNeeded);
    EXPECT_EQ(style.BinaryOperatorSide, formatter::OperatorSide::Trailing);
    EXPECT_EQ(style.AssignmentOperatorSide, formatter::OperatorSide::Trailing);
    EXPECT_EQ(
        style.StatementAttributeIndent,
        formatter::
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
    EXPECT_EQ(
        style.CallsAlignment,
        formatter::ContinuationAlignment::
        Delimiter);
    EXPECT_EQ(style.CallLineFirstStringNames, (std::vector<std::string>{"YT_LOG_INFO", "YT_LOG_ERROR"}));
    EXPECT_EQ(style.IndentedCallScopePairs, (std::vector<std::string>{"OnBeginList:OnEndList"}));
    ASSERT_EQ(config.Exclude.size(), 2u);
    EXPECT_TRUE(std::regex_search("x]y", config.Exclude[0]));
    EXPECT_TRUE(std::regex_search("generated/source.cpp", config.Exclude[1]));
    ASSERT_TRUE(config.Naming.Class.Prefixes.has_value());
    EXPECT_EQ(*config.Naming.Class.Prefixes, (std::vector<std::string>{"S", "C"}));
    save("ClassNameCase = Unchecked\nClassNamePrefixes = []\n" "BaseListsContinuationOffset = 0\n"
        "ConstructorListsContinuationOffset = 0\nShortNamespaceLines = 0\n");
    const auto disabled = formatter::LoadConfigFile(path);
    EXPECT_EQ(disabled.Naming.Class.Case, formatter::NameCase::Unchecked);
    EXPECT_FALSE(disabled.Naming.Class.Prefixes.has_value());
    const auto policies = formatter::CompilePolicies(disabled.FormattingStyle);
    EXPECT_EQ(policies.BaseLists.Items.Indent.ContinuationOffset, disabled.FormattingStyle.IndentWidth);
    EXPECT_EQ(policies.ConstructorLists.Items.Indent.ContinuationOffset, disabled.FormattingStyle.IndentWidth);
    EXPECT_EQ(disabled.FormattingStyle.ShortNamespaceLines, 0u);
    save("CallsLayout = Canonical\nCallsPacking = Fill\nCallsBreak = Always\n");
    const auto alternate = formatter::LoadConfigFile(path).FormattingStyle;
    EXPECT_EQ(alternate.CallsLayout, formatter::CategoryLayout::Canonical);
    EXPECT_EQ(alternate.CallsBreak, formatter::BreakRequirement::Always);
    save("ExpressionsLayout = Preserve\nExpressionsBreak = Always\n"
        "TernaryOperatorSide = Leading\nTernaryLayout = Flexible\n");
    const auto preserving = formatter::LoadConfigFile(path).FormattingStyle;
    EXPECT_EQ(
        preserving.ExpressionsLayout,
        formatter::CategoryLayout::
        Preserve);
    EXPECT_EQ(preserving.TernaryOperatorSide, formatter::OperatorSide::Leading);
    EXPECT_EQ(preserving.TernaryLayout, formatter::TernaryLayout::Flexible);
    const auto preserved = formatter::CompilePolicies(preserving);
    EXPECT_EQ(preserved.Expressions.Layout, formatter::LayoutMode::Preserve);
    EXPECT_EQ(preserved.Expressions.Break, formatter::BreakRequirement::Always);
    std::filesystem::remove(path);
}

} // namespace
