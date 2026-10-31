#include <hit-fmt/file_check.hpp>
#include <hit-fmt/formatting_diagnostics.hpp>
#include <hit-fmt/options.hpp>
#include <hit-fmt/source_excerpt.hpp>
#include <hit-fmt/terminal.hpp>

#include <gtest/gtest.h>

#include <tests/golden.hpp>
#include <tests/input.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

using test::Input;

formatter::Configuration Configuration()
{
    formatter::Configuration configuration;
    auto& style = configuration.FormattingStyle;
    style.Layout = formatter::LayoutMode::Canonical;
    style.IndentationKind = formatter::IndentationKind::Spaces;
    style.CommaSpaceAfter = formatter::GapPolicy::Space;
    style.FunctionCallSpaceBeforeParameters = formatter::GapPolicy::NoSpace;
    style.FunctionSpaceBeforeParameters = formatter::GapPolicy::NoSpace;
    style.ControlBlockKeywordSpacing = formatter::GapPolicy::Space;
    return configuration;
}

TEST(FileCheck, OneResultServesCheckAndFixWithoutCountingAppliedFormatting)
{
    const hit_fmt::PreparedConfiguration configuration(Configuration());
    Input input("void Good(int value){invoke(left,right);}\n");
    const auto checked = hit_fmt::FormatAndCheckFile(input.View(), configuration);
    ASSERT_TRUE(checked.FormattingMismatch);
    EXPECT_TRUE(checked.HasFailures(false));
    EXPECT_FALSE(checked.HasFailures(true));
    EXPECT_NE(checked.Formatting.Text.find("left, right"), std::string::npos);
    EXPECT_TRUE(checked.Semantic.empty());
    std::ostringstream checkSummary, fixSummary;
    hit_fmt::PrintFileSummaryIssues(checkSummary, checked, false);
    hit_fmt::PrintFileSummaryIssues(fixSummary, checked, true);
    EXPECT_EQ(checkSummary.str(), "formatting mismatch");
    EXPECT_TRUE(fixSummary.str().empty());
    const Input applied(checked.Formatting.Text);
    const auto clean = hit_fmt::FormatAndCheckFile(applied.View(), configuration);
    EXPECT_EQ(clean.Formatting.Text, checked.Formatting.Text);
    EXPECT_FALSE(clean.HasFailures(false));
    EXPECT_FALSE(clean.HasFailures(true));
}

TEST(FileCheck, PartialSelectionNeverChangesCanonicalFormatting)
{
    auto raw = Configuration();
    raw.FormattingStyle.ColumnLimit = 40;
    raw.FormattingStyle.CallsLayout = formatter::CategoryLayout::Canonical;
    const hit_fmt::PreparedConfiguration configuration(raw);
    for (const std::string_view source :
        {
            "void f() { outer(first, child(left, right), last); }\n",
            "void f() { if (ready) { first(); } else { second(); } }\n",
            "struct Widget { void function(int first, int second, " "int third); };\n",
            "void f() { auto value = first + second + third + fourth " "+ fifth; }\n",
            "void f() { outer(first, [] (int value) { return value + 1; }, " "last); }\n",
            "void f() { object.First(first).Second(second).Third(third); }\n",
            "void f() { if (first && second && third) { work(); } }\n",
            "void f() { auto value = condition ? first : second; }\n",
            "class Widget { public: void first(); void second(); " "private: int value; };\n",
            "void function(int first, int second, int third) " "{ work(first, second, third); }\n",
            "void f() { int values[] = {first, second, third, fourth, " "fifth}; }\n",
            "void f() { for (int index = 0; index < limit; ++index) " "{ work(index); } }\n",
            "Widget::Widget(int value) : First(value), Second(value), " "Third(value) {}\n",
        })
    {
        const Input input(source);
        const auto full = hit_fmt::FormatAndCheckFile(input.View(), configuration);
        const Input canonical(full.Formatting.Text);
        ASSERT_FALSE(hit_fmt::FormatAndCheckFile(canonical.View(), configuration).FormattingMismatch)
            << full.Formatting.Text;
        const auto count = std::count(full.Formatting.Text.begin(), full.Formatting.Text.end(), '\n');
        for (const std::string_view outside : {"", "void outside(){work(a,b);}\n"}) {
            const Input partial(full.Formatting.Text + std::string(outside));
            for (size_t line = 1; line <= count; ++line) {
                const formatter::Hunk selected[] = {{line, line}};
                for (const bool spillover : {false, true}) {
                    const auto checked = hit_fmt::FormatAndCheckFile(
                        partial.View(),
                        configuration,
                        {},
                        hit_fmt::ChangedFileScope{selected, spillover});
                    EXPECT_FALSE(checked.FormattingMismatch) << "line " << line << " spillover " << spillover << "\n"
                        << full.Formatting.Text << "\n" << checked.Formatting.Text;
                    EXPECT_TRUE(checked.RequiredChanges.empty());
                }
            }
        }
    }
}

TEST(FileCheck, IncludeOrderIsIndependentOfInputPathFormAndHunks)
{
    formatter::Configuration raw;
    auto& style = raw.FormattingStyle;
    style.IncludeOrder = formatter::IncludeOrder::Sort;
    style.IncludeMatchingHeader = formatter::IncludeMatchingHeader::First;
    style.IncludeGrouping = formatter::IncludeGrouping::Directory;
    style.IncludePathBase = formatter::IncludePathBase::ProjectRoot;
    const hit_fmt::PreparedConfiguration prepared(raw);
    const Input input("#include \"project/engine/widget.h\"\n\n" "#include \"project/engine/detail.h\"\n\n"
        "#include \"project/base.h\"\n");
    const formatter::Hunk selected[] = {{1, 5}};
    const auto relative = std::filesystem::path("project/engine/widget.cpp");
    for (const auto& path : {relative, std::filesystem::absolute(relative)}) {
        const auto name = path.generic_string();
        for (const bool hunks : {false, true}) {
            const auto checked = hit_fmt::FormatAndCheckFile(
                input.View(),
                prepared,
                name,
                hunks ? std::optional(hit_fmt::ChangedFileScope{selected}) : std::nullopt);
            EXPECT_FALSE(checked.FormattingMismatch) << name;
            EXPECT_TRUE(checked.Formatting.Summary->IncludeOrderChanges.empty()) << name;
        }
    }
}

TEST(FileCheck, IncludeOrderDiagnosticGoldenIsOneErrorForTheFile)
{
    const test::golden::Fixture fixture{
        std::filesystem::path(__FILE__).parent_path() / "testdata/file-check",
        "include-order",
    };
    const Input input(fixture.InputNamed(".input.cpp"));
    for (const bool mixed : {false, true}) {
        for (const bool indent : {false, true}) {
            formatter::Configuration raw;
            auto& style = raw.FormattingStyle;
            style.IncludeOrder = formatter::IncludeOrder::Sort;
            style.IncludeMatchingHeader = formatter::IncludeMatchingHeader::First;
            style.IncludeGrouping = formatter::IncludeGrouping::Directory;
            style.IncludeStandardHeaders = formatter::IncludeStandardHeaders::Last;
            style.IndentPreprocessingDirectives =
                indent ? formatter::BodyIndentation::Indent : formatter::BodyIndentation::Preserve;
            if (mixed) {
                style.SpaceInsideFunctionParameters = formatter::GapPolicy::NoSpace;
            }
            const hit_fmt::PreparedConfiguration prepared(std::move(raw));
            const auto checked = hit_fmt::FormatAndCheckFile(input.View(), prepared, "project/sample.cpp");
            const auto detailed = hit_fmt::FormatAndCheckFile(
                input.View(),
                prepared,
                "project/sample.cpp",
                std::nullopt,
                hit_fmt::DiagnosticDetail::Full);
            EXPECT_FALSE(checked.Formatting.Changes);
            EXPECT_TRUE(checked.Formatting.Map.empty());
            EXPECT_TRUE(checked.Formatting.StructuralOperations.empty());
            ASSERT_EQ(checked.Formatting.Summary->IncludeOrderChanges.size(), 2u);
            EXPECT_EQ(
                detailed.Formatting.Summary->IncludeOrderChanges.size(),
                checked.Formatting.Summary->IncludeOrderChanges.size());
            EXPECT_EQ(detailed.Formatting.Summary->OtherChanges, checked.Formatting.Summary->OtherChanges);
            EXPECT_EQ(checked.Formatting.Summary->OtherChanges, mixed);
            EXPECT_TRUE(checked.HasFailures(false));
            EXPECT_FALSE(checked.HasFailures(true));
            for (const auto& change : checked.Formatting.Summary->IncludeOrderChanges) {
                const auto before =
                    input.View().View().substr(change.Original.Begin, change.Original.End - change.Original.Begin);
                const auto after = std::string_view(checked.Formatting.Text)
                    .substr(change.Output.Begin, change.Output.End - change.Output.Begin);
                EXPECT_TRUE(before.ends_with('\n'));
                EXPECT_TRUE(after.ends_with('\n'));
                EXPECT_EQ(before.find("int "), std::string_view::npos);
                EXPECT_EQ(after.find("int "), std::string_view::npos);
            }
            for (const bool fix : {false, true}) {
                std::ostringstream output;
                hit_fmt::PrintFileIssues(output, "sample.cpp", input.View().View(), checked, fix);
                output << "summary: ";
                hit_fmt::PrintFileSummaryIssues(output, checked, fix);
                output << '\n';
                fixture.Expect(output.str(), fix ? ".fix.txt" : mixed ? ".mixed-check.txt" : ".check.txt");
                std::ostringstream formatting;
                hit_fmt::PrintFormattingDiagnostics(
                    formatting,
                    "sample.cpp",
                    input.View().View(),
                    detailed.Formatting,
                    hit_fmt::Terminal{},
                    fix);
                if (mixed) {
                    fixture.Expect(formatting.str(), fix ? ".format-fix.txt" : ".format-check.txt");
                } else {
                    EXPECT_TRUE(formatting.str().empty());
                }
            }
            const Input applied(checked.Formatting.Text);
            const auto clean = hit_fmt::FormatAndCheckFile(applied.View(), prepared, "project/sample.cpp");
            EXPECT_FALSE(clean.FormattingMismatch);
            EXPECT_TRUE(clean.Formatting.Summary->IncludeOrderChanges.empty());
            const formatter::Hunk selected[] = {{1, 1}};
            const auto strict = hit_fmt::FormatAndCheckFile(
                input.View(),
                prepared,
                "project/sample.cpp",
                hit_fmt::ChangedFileScope{selected, false});
            EXPECT_TRUE(strict.Formatting.Summary->IncludeOrderChanges.empty());
            EXPECT_FALSE(strict.HasFailures(false));
            EXPECT_FALSE(strict.Incomplete.empty());
        }
    }
}

TEST(FileCheck, FunctionBodyMacroHintsApplyToTheInitialParse)
{
    auto raw = Configuration();
    raw.FormattingStyle.FunctionBodyMacroNames = {"define_case"};
    raw.FormattingStyle.BinaryOperatorSpacing = formatter::GapPolicy::Space;
    raw.Naming.Function.Case = formatter::NameCase::PascalCase;
    raw.FunctionDefinitionParameterNames = formatter::FunctionDefinitionParameterNames::NameRequired;
    const hit_fmt::PreparedConfiguration configuration(std::move(raw));
    const Input input("define_case(Suite, Case) {auto sum=left+right; Use(sum);}\n");
    const auto checked = hit_fmt::FormatAndCheckFile(input.View(), configuration);
    EXPECT_TRUE(checked.FormattingMismatch);
    EXPECT_TRUE(checked.Semantic.empty());
    EXPECT_NE(checked.Formatting.Text.find("left + right"), std::string::npos);
    const Input applied(checked.Formatting.Text);
    EXPECT_FALSE(hit_fmt::FormatAndCheckFile(applied.View(), configuration).FormattingMismatch);
}

TEST(FileCheck, DiagnosticGoldenUsesOriginalCheckAndFinalFixCoordinates)
{
    const test::golden::Fixture fixture{
        std::filesystem::path(__FILE__).parent_path() / "testdata/file-check",
        "mapped",
    };
    auto configuration = Configuration();
    configuration.FormattingStyle.AddMissingControlBraces = true;
    configuration.FormattingStyle.NamespaceComments = formatter::NamespaceComments::AddMissing;
    configuration.FormattingStyle.NamespaceCommentSpaces = 7;
    configuration.FormattingStyle.ColumnLimit = 24;
    configuration.FormattingStyle.WidthDiagnostics = formatter::WidthDiagnosticPolicy::Report;
    configuration.Naming.Function.Case = formatter::NameCase::PascalCase;
    configuration.FunctionDeclarationParameterNames = formatter::FunctionDeclarationParameterNames::Required;
    configuration.FunctionDefinitionParameterNames = formatter::FunctionDefinitionParameterNames::NameRequired;
    const hit_fmt::PreparedConfiguration prepared(std::move(configuration));
    const Input input(fixture.InputNamed(".input.cpp"));
    const auto checked =
        hit_fmt::FormatAndCheckFile(input.View(), prepared, {}, std::nullopt, hit_fmt::DiagnosticDetail::Full);
    for (const auto action :
        {formatter::StructuralAction::AddMissingControlBraces, formatter::StructuralAction::NamespaceComments})
    {
        EXPECT_TRUE(std::ranges::any_of(
            checked.Formatting.Changes->Edits,
            [action] (const auto& change) {
                return change.Action == action;
            }));
    }
    ASSERT_EQ(checked.Semantic.size(), 4u);
    EXPECT_TRUE(checked.HasFailures(false));
    EXPECT_TRUE(checked.HasFailures(true));
    EXPECT_GT(checked.Semantic.back().Emitted.Line, checked.Semantic.back().Original.Line);
    ASSERT_FALSE(checked.Formatting.Diagnostics.empty());
    const auto& width = checked.Formatting.Diagnostics.back();
    const auto close = checked.Formatting.Text.find("// namespace test");
    ASSERT_NE(close, std::string::npos);
    EXPECT_EQ(
        width.Position.Line,
        1u + std::count(checked.Formatting.Text.begin(), checked.Formatting.Text.begin() + close, '\n'));
    for (const auto terminalWidth : {size_t(80), size_t(32)}) {
        for (const auto fix : {false, true}) {
            SCOPED_TRACE(fix ? "fix" : "check");
            SCOPED_TRACE(terminalWidth);
            const hit_fmt::Terminal terminal(false, terminalWidth);
            std::ostringstream output;
            hit_fmt::PrintFileIssues(output, "sample.cpp", input.View().View(), checked, fix, terminal);
            output << "summary: ";
            hit_fmt::PrintFileSummaryIssues(output, checked, fix);
            output << '\n';
            fixture.Expect(
                output.str(),
                terminalWidth == 80
                ? (fix ? ".fix.txt" : ".check.txt")
                : (fix ? ".fix-narrow.txt" : ".check-narrow.txt"));
            std::ostringstream formatting;
            hit_fmt::PrintFormattingDiagnostics(
                formatting,
                "sample.cpp",
                input.View().View(),
                checked.Formatting,
                terminal,
                fix);
            fixture.Expect(
                formatting.str(),
                terminalWidth == 80
                ? (fix ? ".format-fix.txt" : ".format-check.txt")
                : (fix ? ".format-fix-narrow.txt" : ".format-check-narrow.txt"));
        }
    }
}

TEST(FileCheck, FormattingDiagnosticsUseTerminalWidth)
{
    const test::golden::Fixture fixture{
        std::filesystem::path(__FILE__).parent_path() / "testdata/file-check",
        "terminal",
    };
    const std::string_view source = "void Good() { invoke(first_argument,second_value,third_argument); }\n";
    formatter::Style style;
    style.CommaSpaceAfter = formatter::GapPolicy::Space;
    const Input input(source);
    const auto formatted = formatter::Format(input.View(), formatter::PreparedFormat(style), {.Changes = true});
    for (const auto width : {size_t(80), size_t(32)}) {
        for (const bool fixed : {false, true}) {
            SCOPED_TRACE(width);
            SCOPED_TRACE(fixed);
            std::ostringstream output;
            hit_fmt::PrintFormattingDiagnostics(
                output,
                "sample.cpp",
                source,
                formatted,
                hit_fmt::Terminal(false, width),
                fixed);
            fixture.Expect(
                output.str(),
                width == 80 ? (fixed ? ".fix.txt" : ".check.txt") : (fixed ? ".fix-narrow.txt" : ".check-narrow.txt"));
        }
    }
}

TEST(FileCheck, TokenEndpointAndUnicodePositionsMapWithoutReparsingOutput)
{
    auto raw = Configuration();
    raw.Naming.Function.Case = formatter::NameCase::PascalCase;
    raw.FunctionDeclarationParameterNames = formatter::FunctionDeclarationParameterNames::Required;
    raw.FunctionDefinitionParameterNames = formatter::FunctionDefinitionParameterNames::NameRequired;
    const hit_fmt::PreparedConfiguration prepared(raw);
    const std::string source =
        "const char* text=\"é\"; void bad_name(int  , " "void (*)(int)){}\nvoid GoodProto(int);\n";
    const Input input(source);
    const auto checked = hit_fmt::FormatAndCheckFile(input.View(), prepared);
    const auto summary =
        hit_fmt::FormatAndCheckFile(input.View(), prepared, {}, std::nullopt, hit_fmt::DiagnosticDetail::Summary);
    EXPECT_EQ(summary.Semantic.size(), checked.Semantic.size());
    EXPECT_TRUE(summary.Formatting.OriginalTokens.empty());
    for (const bool fix : {false, true}) {
        std::ostringstream fullOutput, summaryOutput;
        hit_fmt::PrintFileSummaryIssues(fullOutput, checked, fix);
        hit_fmt::PrintFileSummaryIssues(summaryOutput, summary, fix);
        EXPECT_EQ(summaryOutput.str(), fullOutput.str());
    }
    ASSERT_EQ(checked.Semantic.size(), 4u);
    EXPECT_EQ(checked.Semantic[0].Anchor.Offset, source.find("bad_name"));
    EXPECT_EQ(checked.Semantic[1].Anchor.Offset, source.find("int  ,") + 3);
    EXPECT_EQ(checked.Semantic[2].Anchor.Offset, source.find("(*)") + 2);
    EXPECT_NE(checked.Semantic.back().Message.find("function declaration"), std::string::npos);
    EXPECT_EQ(
        checked.Semantic[0].Original.Column,
        source.find("bad_name") /* one extra UTF8 byte cancels one-based column */);
    const auto emitted = checked.Formatting.Text.find("bad_name");
    const auto lineStart = checked.Formatting.Text.rfind('\n', emitted);
    const auto begin = lineStart == std::string::npos ? 0 : lineStart + 1;
    EXPECT_EQ(
        checked.Semantic[0].Emitted.Column,
        1u +
        std::count_if(
            checked.Formatting.Text.begin() + begin,
            checked.Formatting.Text.begin() + emitted,
            [] (unsigned char c) {
                return (c & 0xc0) != 0x80;
            }));
    const auto& insertion = checked.Semantic[1];
    EXPECT_EQ(insertion.Emitted.Line, checked.Semantic[0].Emitted.Line);
    EXPECT_EQ(insertion.Emitted.Column, checked.Semantic[0].Emitted.Column + std::string_view("bad_name(int").size());
    const auto& pointerInsertion = checked.Semantic[2];
    EXPECT_EQ(pointerInsertion.Emitted.Line, insertion.Emitted.Line);
    EXPECT_GT(pointerInsertion.Emitted.Column, insertion.Emitted.Column);

    // Invalid UTF8 in a protected literal must not shift later name locations.
    const std::string malformed = std::string("const char* text=\"") + '\x80' + "\";\tvoid another_bad(){}\n";
    const Input recovery(malformed);
    const auto recovered = hit_fmt::FormatAndCheckFile(recovery.View(), prepared);
    ASSERT_EQ(recovered.Semantic.size(), 1u);
    EXPECT_EQ(recovered.Semantic[0].Original.Column, malformed.find("another_bad") + 1);
    const auto recoveredName = recovered.Formatting.Text.find("another_bad");
    const auto recoveredStart = recovered.Formatting.Text.rfind('\n', recoveredName);
    EXPECT_EQ(
        recovered.Semantic[0].Emitted.Column,
        recoveredName - (recoveredStart == std::string::npos ? 0 : recoveredStart + 1) + 1);
}

TEST(FileCheck, UnicodeWidthDiagnosticGoldenMapsCodePointsAndTabs)
{
    const test::golden::Fixture fixture{
        std::filesystem::path(__FILE__).parent_path() / "testdata/file-check",
        "unicode-width",
    };
    formatter::Configuration raw;
    raw.FormattingStyle.ColumnLimit = 12;
    raw.FormattingStyle.WidthDiagnostics = formatter::WidthDiagnosticPolicy::Report;
    const hit_fmt::PreparedConfiguration prepared(raw);
    const Input input(fixture.InputNamed(".input.cpp"));
    const auto checked = hit_fmt::FormatAndCheckFile(input.View(), prepared);
    EXPECT_FALSE(checked.FormattingMismatch);
    ASSERT_EQ(checked.Formatting.Diagnostics.size(), 2u);
    EXPECT_EQ(checked.Formatting.Diagnostics[0].Position.Line, 2u);
    EXPECT_EQ(checked.Formatting.Diagnostics[1].Position.Line, 4u);
    EXPECT_EQ(checked.Formatting.Diagnostics[0].Original->Column, 13u);
    EXPECT_EQ(checked.Formatting.Diagnostics[1].Original->Column, 12u);
    const formatter::Hunk selected[] = {{2, 2}, {4, 4}};
    const auto scoped =
        hit_fmt::FormatAndCheckFile(input.View(), prepared, {}, hit_fmt::ChangedFileScope{selected, false});
    ASSERT_EQ(scoped.Formatting.Diagnostics.size(), checked.Formatting.Diagnostics.size());
    for (size_t i = 0; i < checked.Formatting.Diagnostics.size(); ++i) {
        EXPECT_EQ(scoped.Formatting.Diagnostics[i].Offset, checked.Formatting.Diagnostics[i].Offset);
        EXPECT_EQ(scoped.Formatting.Diagnostics[i].CodePointColumn, checked.Formatting.Diagnostics[i].CodePointColumn);
        EXPECT_EQ(
            scoped.Formatting.Diagnostics[i].Original->Column,
            checked.Formatting.Diagnostics[i].Original->Column);
    }
    const auto summary =
        hit_fmt::FormatAndCheckFile(input.View(), prepared, {}, std::nullopt, hit_fmt::DiagnosticDetail::Summary);
    ASSERT_EQ(summary.Formatting.Diagnostics.size(), checked.Formatting.Diagnostics.size());
    EXPECT_TRUE(summary.Formatting.OriginalTokens.empty());
    for (const auto& diagnostic : summary.Formatting.Diagnostics) {
        EXPECT_FALSE(diagnostic.Original);
    }
    EXPECT_EQ(summary.HasFailures(false), checked.HasFailures(false));
    EXPECT_EQ(summary.HasFailures(true), checked.HasFailures(true));
    for (const bool fix : {false, true}) {
        std::ostringstream output;
        hit_fmt::PrintFileIssues(output, "unicode.cpp", input.View().View(), checked, fix);
        fixture.Expect(output.str(), fix ? ".fix.txt" : ".check.txt");
    }

    // A tab itself can contain the first column exceeding the limit.
    raw.FormattingStyle.ColumnLimit = 6;
    const hit_fmt::PreparedConfiguration narrow(raw);
    const Input tab("// é界😀\t\n");
    const auto tabbed = hit_fmt::FormatAndCheckFile(tab.View(), narrow);
    ASSERT_EQ(tabbed.Formatting.Diagnostics.size(), 1u);
    EXPECT_EQ(tabbed.Formatting.Diagnostics[0].Original->Column, 7u);
    EXPECT_EQ(tabbed.Formatting.Diagnostics[0].Offset, tab.View().View().find('\t'));
    EXPECT_EQ(tabbed.Formatting.Diagnostics[0].CodePointColumn, 7u);
}

TEST(SourceExcerpt, ReusesCellsForUnicodeTabsAndCarriageReturns)
{
    const auto rendered = hit_fmt::RenderLine("\té界\rx\tz", 3);
    const std::array<size_t, 9> carets{0, 0, 5, 6, 7, 7, 8, 13, 14};
    for (size_t column = 0; column < carets.size(); ++column) {
        EXPECT_EQ(rendered.Caret(column), carets[column]) << column;
    }
}

TEST(FileCheck, ActualProtectedOverflowRemainsFailureInFixMode)
{
    auto raw = Configuration();
    raw.FormattingStyle.ColumnLimit = 10;
    raw.FormattingStyle.WidthDiagnostics = formatter::WidthDiagnosticPolicy::Report;
    const hit_fmt::PreparedConfiguration prepared(raw);
    const Input input("// hit-fmt off\r\nint " "unavoidable_long_name;\r\n// hit-fmt on\r\n");
    const auto checked = hit_fmt::FormatAndCheckFile(input.View(), prepared);
    EXPECT_EQ(checked.Formatting.Text, input.View().View());
    EXPECT_FALSE(checked.FormattingMismatch);
    EXPECT_TRUE(checked.HasFailures(false));
    EXPECT_TRUE(checked.HasFailures(true));
    ASSERT_GE(checked.Formatting.Diagnostics.size(), 2u);
    EXPECT_EQ(checked.Formatting.Diagnostics[1].Position.Line, 2u);
}

TEST(FileCheck, ChangedHunkOptionsSupportAliasesAndRejectMixedInputs)
{
    for (const auto option : {"--changed-hunks", "--changed-ranges"}) {
        const char* argv[] = {"hit-fmt", "--fix", "--check", option, "-", "--no-spillover", "--threads=2", "--summary"};
        const auto parsed = hit_fmt::ParseOptions(argv);
        ASSERT_TRUE(parsed.HunkInput);
        EXPECT_EQ(*parsed.HunkInput, "-");
        EXPECT_TRUE(parsed.Paths.empty());
        EXPECT_FALSE(parsed.Spillover);
        EXPECT_TRUE(parsed.Fix);
        EXPECT_TRUE(parsed.SummaryOnly);
        EXPECT_EQ(parsed.Threads, 2u);
    }
    for (const auto option : {"--changed-hunks=ranges.txt", "--changed-ranges=ranges.txt"}) {
        const char* argv[] = {"hit-fmt", option};
        const auto parsed = hit_fmt::ParseOptions(argv);
        ASSERT_TRUE(parsed.HunkInput);
        EXPECT_EQ(*parsed.HunkInput, "ranges.txt");
        EXPECT_TRUE(parsed.Spillover);
    }
    for (const auto& argv :
        std::vector<std::vector<const char*>>{
            {"hit-fmt", "--changed-hunks"},
            {"hit-fmt", "--changed-ranges="},
            {"hit-fmt", "--changed-hunks=-", "source.cpp"},
            {"hit-fmt", "--changed-hunks=-", "--changed-ranges=-"},
            {"hit-fmt", "--no-spillover", "source.cpp"},
            {"hit-fmt", "--unknown"},
            {"hit-fmt", "--help=1"},
            {"hit-fmt", "--threads"},
            {"hit-fmt", "--threads="},
            {"hit-fmt", "--threads=0"},
            {"hit-fmt", "-j=2"},
            {"hit-fmt", "-j2x"},
            {"hit-fmt", "-j", "source.cpp"},
            {"hit-fmt", "-j", ""},
            {"hit-fmt", "-j", "-"},
            {"hit-fmt", "--config"},
            {"hit-fmt", "--config-extra=style"},
            {"hit-fmt", "--config=style", "--no-config"},
            {"hit-fmt", "--exclude"},
            {"hit-fmt", "--exclude=["},
        })
    {
        EXPECT_THROW((void)hit_fmt::ParseOptions(argv), hit_fmt::UsageError);
    }
    const char* positional[] = {"hit-fmt", "--", "--changed-hunks", "--no-spillover"};
    const auto parsed = hit_fmt::ParseOptions(positional);
    EXPECT_FALSE(parsed.HunkInput);
    EXPECT_EQ(parsed.Paths.size(), 2u);
}

TEST(FileCheck, ValueFlagsSupportAttachedAndSeparatedArguments)
{
    for (const auto& args :
        std::vector<std::vector<const char*>>{
            {"hit-fmt", "-j3", "--config=style.hit-fmt", "--exclude=generated", "source.cpp"},
            {"hit-fmt", "--threads", "3", "--config", "style.hit-fmt", "--exclude", "generated", "source.cpp"},
            {"hit-fmt", "-j", "3", "--config=style.hit-fmt", "--exclude", "generated", "source.cpp"},
        })
    {
        const auto parsed = hit_fmt::ParseOptions(args);
        EXPECT_EQ(parsed.Threads, 3u);
        ASSERT_TRUE(parsed.ConfigPath);
        EXPECT_EQ(*parsed.ConfigPath, "style.hit-fmt");
        ASSERT_EQ(parsed.Exclusions.size(), 1u);
        EXPECT_TRUE(std::regex_search("generated.cpp", parsed.Exclusions.front()));
        EXPECT_FALSE(std::regex_search("source.cpp", parsed.Exclusions.front()));
        EXPECT_EQ(parsed.Paths, std::vector<std::filesystem::path>{"source.cpp"});
    }
}

TEST(FileCheck, OptionalThreadCountStopsAtFlags)
{
    for (const auto& args :
        std::vector<std::vector<const char*>>{
            {"hit-fmt", "-j", "--summary", "source.cpp", "-j"},
            {"hit-fmt", "--summary", "-j", "--", "source.cpp"},
        })
    {
        const auto parsed = hit_fmt::ParseOptions(args);
        EXPECT_GT(parsed.Threads, 0u);
        EXPECT_TRUE(parsed.SummaryOnly);
        EXPECT_EQ(parsed.Paths, std::vector<std::filesystem::path>{"source.cpp"});
    }
}

TEST(FileCheck, UsageErrorDiagnosticGolden)
{
    std::ostringstream output;
    for (const auto& args :
        std::vector<std::vector<const char*>>{
            {"hit-fmt", "--unknown"},
            {"hit-fmt", "--generate-config=missing"},
            {"hit-fmt", "-j", "aboba", "--other-flag"},
        })
    {
        try {
            (void)hit_fmt::ParseOptions(args);
            FAIL() << "Expected a usage error";
        }
        catch (const hit_fmt::UsageError& error) {
            output << "plain:\n";
            hit_fmt::PrintError(output, error.what(), hit_fmt::Terminal());
            output << "colored:\n";
            hit_fmt::PrintError(output, error.what(), hit_fmt::Terminal(true));
        }
    }
    test::golden::ExpectGolden(std::filesystem::path(HIT_FMT_TESTDATA_DIR) / "usage_error.out", output.str());
}

TEST(FileCheck, GenerateConfigOptionsSelectOptionalPresets)
{
    using hit_fmt::ConfigPreset;
    const char* defaultArgs[] = {"hit-fmt", "--generate-config", "--summary"};
    const auto defaults = hit_fmt::ParseOptions(defaultArgs);
    EXPECT_EQ(defaults.GenerateConfig, ConfigPreset::Default);
    EXPECT_TRUE(defaults.SummaryOnly);
    for (const auto& [name, preset] :
        {
            std::pair{"ytsaurus", ConfigPreset::YTsaurus},
            std::pair{"google", ConfigPreset::Google},
            std::pair{"llvm", ConfigPreset::LLVM},
        })
    {
        SCOPED_TRACE(name);
        const char* separated[] = {"hit-fmt", "--generate-config", name};
        const auto parsed = hit_fmt::ParseOptions(separated);
        EXPECT_EQ(parsed.GenerateConfig, preset);
        EXPECT_TRUE(parsed.Paths.empty());
        const auto option = "--generate-config=" + std::string(name);
        const char* attached[] = {"hit-fmt", option.c_str()};
        EXPECT_EQ(hit_fmt::ParseOptions(attached).GenerateConfig, preset);
    }
    for (const auto& args :
        std::vector<std::vector<const char*>>{
            {"hit-fmt", "--generate-config", "missing"},
            {"hit-fmt", "--generate-config="},
            {"hit-fmt", "--generate-config", "google", "source.cpp"},
            {"hit-fmt", "source.cpp", "--generate-config"},
        })
    {
        EXPECT_THROW((void)hit_fmt::ParseOptions(args), hit_fmt::UsageError);
    }
}

TEST(FileCheck, EmptyHunkScopeNeverFallsBackToWholeFile)
{
    auto raw = Configuration();
    raw.Naming.Function.Case = formatter::NameCase::PascalCase;
    raw.FunctionDeclarationParameterNames = formatter::FunctionDeclarationParameterNames::Required;
    raw.FormattingStyle.ColumnLimit = 10;
    raw.FormattingStyle.WidthDiagnostics = formatter::WidthDiagnosticPolicy::Report;
    const hit_fmt::PreparedConfiguration prepared(raw);
    const Input input("void bad_name(int);\n");
    const auto empty = hit_fmt::FormatAndCheckFile(input.View(), prepared, {}, hit_fmt::ChangedFileScope{});
    EXPECT_EQ(empty.Formatting.Text, input.View().View());
    EXPECT_FALSE(empty.HasFailures(false));
    EXPECT_FALSE(empty.HasFailures(true));
    EXPECT_TRUE(empty.Incomplete.empty());
    EXPECT_TRUE(hit_fmt::FormatAndCheckFile(input.View(), prepared).HasFailures(true));
    const formatter::Hunk invalid[] = {{2, 2}};
    EXPECT_THROW(
        hit_fmt::FormatAndCheckFile(input.View(), prepared, {}, hit_fmt::ChangedFileScope{invalid}),
        std::invalid_argument);
}

TEST(FileCheck, HunkDiagnosticGoldenUsesOriginalSelectionAndMappedOutput)
{
    const test::golden::Fixture fixture{std::filesystem::path(__FILE__).parent_path() / "testdata/file-check", "hunks"};
    auto raw = Configuration();
    raw.Naming.Function.Case = formatter::NameCase::PascalCase;
    raw.FunctionDeclarationParameterNames = formatter::FunctionDeclarationParameterNames::Required;
    raw.FunctionDefinitionParameterNames = formatter::FunctionDefinitionParameterNames::NameRequired;
    raw.FormattingStyle.IncludeOrder = formatter::IncludeOrder::Sort;
    raw.FormattingStyle.AddMissingControlBraces = true;
    raw.FormattingStyle.ColumnLimit = 40;
    raw.FormattingStyle.WidthDiagnostics = formatter::WidthDiagnosticPolicy::Report;
    const hit_fmt::PreparedConfiguration prepared(raw);
    const Input input(fixture.InputNamed(".input.cpp"));
    const formatter::Hunk selected[] = {{13, 13}, {11, 11}, {9, 9}, {4, 5}, {1, 1}, {5, 5}};
    for (const bool spillover : {false, true}) {
        const auto checked = hit_fmt::FormatAndCheckFile(
            input.View(),
            prepared,
            "sample.cpp",
            hit_fmt::ChangedFileScope{selected, spillover});
        const auto summary = hit_fmt::FormatAndCheckFile(
            input.View(),
            prepared,
            "sample.cpp",
            hit_fmt::ChangedFileScope{selected, spillover},
            hit_fmt::DiagnosticDetail::Summary);
        ASSERT_EQ(summary.Formatting.Diagnostics.size(), checked.Formatting.Diagnostics.size());
        for (size_t i = 0; i < checked.Formatting.Diagnostics.size(); ++i) {
            ASSERT_TRUE(summary.Formatting.Diagnostics[i].Original);
            EXPECT_EQ(
                summary.Formatting.Diagnostics[i].Original->Line,
                checked.Formatting.Diagnostics[i].Original->Line);
        }
        for (const bool fix : {false, true}) {
            std::ostringstream fullOutput, summaryOutput;
            hit_fmt::PrintFileSummaryIssues(fullOutput, checked, fix);
            hit_fmt::PrintFileSummaryIssues(summaryOutput, summary, fix);
            EXPECT_EQ(summaryOutput.str(), fullOutput.str());
        }
        ASSERT_EQ(checked.Semantic.size(), 4u);
        for (const auto& diagnostic : checked.Semantic) {
            EXPECT_NE(diagnostic.Original.Line, 3u);
            EXPECT_NE(diagnostic.Original.Line, 6u);
        }
        ASSERT_FALSE(checked.Formatting.Diagnostics.empty());
        for (const auto& diagnostic : checked.Formatting.Diagnostics) {
            const auto position = *diagnostic.Original;
            EXPECT_NE(position.Line, 3u);
            EXPECT_NE(position.Line, 14u);
        }
        EXPECT_EQ(
            std::any_of(
                checked.Formatting.Diagnostics.begin(),
                checked.Formatting.Diagnostics.end(),
                [] (const auto& diagnostic) {
                    return diagnostic.Original->Line == 10;
                }),
            spillover);
        EXPECT_TRUE(checked.FormattingMismatch);
        EXPECT_TRUE(checked.HasFailures(false));
        EXPECT_TRUE(checked.HasFailures(true));
        EXPECT_EQ(checked.RequiredChanges.empty(), spillover);
        EXPECT_EQ(checked.Formatting.Text.starts_with("#include \"a.h\""), spillover);
        EXPECT_NE(checked.Formatting.Text.find("void bad_before(int);"), std::string::npos);
        EXPECT_NE(
            checked.Formatting.Text.find("const char* Outside = " "\"unavoidable_outside_literal_over_the_limit\";"),
            std::string::npos);
        for (const bool fix : {false, true}) {
            std::ostringstream output;
            hit_fmt::PrintFileIssues(output, "sample.cpp", input.View().View(), checked, fix);
            output << "summary: ";
            hit_fmt::PrintFileSummaryIssues(output, checked, fix);
            output << '\n';
            EXPECT_EQ(output.str().find("note:"), std::string::npos);
            if (!fix) {
                EXPECT_EQ(output.str().find("cannot"), std::string::npos);
            }
            fixture.Expect(
                output.str(),
                spillover
                ? (fix ? ".spill-fix.txt" : ".spill-check.txt")
                : (fix ? ".strict-fix.txt" : ".strict-check.txt"));
        }
    }
}

TEST(FileCheck, IncompleteHunkCoverageAndRequiredChangesStayDistinct)
{
    auto raw = Configuration();
    raw.FormattingStyle.CallsLayout = formatter::CategoryLayout::Preserve;
    const hit_fmt::PreparedConfiguration prepared(raw);
    const Input input("void Good() {\n    call(1,\n        2);\n}\n");
    const formatter::Hunk selected[] = {{3, 3}};
    const auto checked =
        hit_fmt::FormatAndCheckFile(input.View(), prepared, {}, hit_fmt::ChangedFileScope{selected, false});
    EXPECT_FALSE(checked.FormattingMismatch);
    EXPECT_FALSE(checked.HasFailures(false));
    EXPECT_FALSE(checked.HasFailures(true));
    EXPECT_FALSE(checked.Incomplete.empty());
    for (const bool fix : {false, true}) {
        std::ostringstream output, summary;
        hit_fmt::PrintFileIssues(output, "sample.cpp", input.View().View(), checked, fix);
        hit_fmt::PrintFileSummaryIssues(summary, checked, fix);
        EXPECT_TRUE(output.str().empty());
        EXPECT_TRUE(summary.str().empty());
    }

    raw.FormattingStyle.AddMissingControlBraces = true;
    const hit_fmt::PreparedConfiguration braces(raw);
    const Input blocked("void Good(bool ready) {\n  if (ready)\n" "    consume(\n      ready);\n}\n");
    const auto required =
        hit_fmt::FormatAndCheckFile(blocked.View(), braces, {}, hit_fmt::ChangedFileScope{selected, false});
    ASSERT_EQ(required.RequiredChanges.size(), 1u);
    EXPECT_TRUE(required.Semantic.empty());
    EXPECT_TRUE(required.Formatting.Diagnostics.empty());
    EXPECT_TRUE(required.HasFailures(false));
    EXPECT_TRUE(required.HasFailures(true));
    for (const bool fix : {false, true}) {
        std::ostringstream output, summary;
        hit_fmt::PrintFileIssues(output, "sample.cpp", blocked.View().View(), required, fix);
        hit_fmt::PrintFileSummaryIssues(summary, required, fix);
        for (const auto& text : {output.str(), summary.str()}) {
            EXPECT_NE(text.find("fail: control body requires braces"), std::string::npos);
            EXPECT_EQ(text.find("cannot automatically fix") != std::string::npos, fix);
            EXPECT_EQ(text.find("note:"), std::string::npos);
        }
    }
}

TEST(FileCheck, UnsupportedPreparationFailsClosed)
{
    auto raw = Configuration();
    raw.FormattingStyle.CallLikeMacroNames = {"CALL_HINT"};
    EXPECT_THROW(hit_fmt::PreparedConfiguration{raw}, std::invalid_argument);
    raw.FormattingStyle.CallLikeMacroNames.clear();
    raw.FormattingStyle.AmbiguousTemplateDeclarationSpacing = formatter::GapPolicy::Space;
    EXPECT_THROW(hit_fmt::PreparedConfiguration{raw}, std::invalid_argument);
}

} // namespace
