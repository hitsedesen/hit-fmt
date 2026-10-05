#include <hit-fmt/file_check.hpp>
#include <hit-fmt/options.hpp>
#include <tests/golden.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

struct Input
{
    std::string Storage;
    size_t Size;

    explicit Input(std::string_view text)
        : Storage(text)
        , Size(text.size())
    {
        Storage.append(util::SourcePaddingSize, '\0');
    }

    util::PaddedSourceView View() const
    {
        return {Storage.data(), Size};
    }
};
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
    const hit_fmt::PreparedFileConfiguration configuration(Configuration());
    Input input("void Good(int value){invoke(left,right);}\n");
    const auto checked = hit_fmt::CheckFile(input.View(), configuration);
    ASSERT_TRUE(checked.FormattingMismatch);
    EXPECT_TRUE(checked.HasFailures(false));
    EXPECT_FALSE(checked.HasFailures(true));
    EXPECT_NE(checked.Formatting.Text.find("left, right"), std::string::npos);
    EXPECT_TRUE(checked.Semantic.empty());
    std::ostringstream checkSummary, fixSummary;
    hit_fmt::PrintFileSummaryIssues(checkSummary, checked, false);
    hit_fmt::PrintFileSummaryIssues(fixSummary, checked, true);
    EXPECT_EQ(checkSummary.str(), "formatting mismatch");
    EXPECT_TRUE(fixSummary
        .str()
        .empty());
    const Input applied(checked.Formatting.Text);
    const auto clean = hit_fmt::CheckFile(applied.View(), configuration);
    EXPECT_EQ(clean.Formatting.Text, checked.Formatting.Text);
    EXPECT_FALSE(clean.HasFailures(false));
    EXPECT_FALSE(clean.HasFailures(true));
}

TEST(FileCheck, FunctionBodyMacroHintsApplyToTheInitialParse)
{
    auto raw = Configuration();
    raw.FormattingStyle.FunctionBodyMacroNames = {"define_case"};
    raw.FormattingStyle.BinaryOperatorSpacing = formatter::GapPolicy::Space;
    raw.Naming.Function.Case = formatter::NameCase::PascalCase;
    raw.FunctionDefinitionParameterNames =
        formatter::FunctionDefinitionParameterNames::NameRequired;
    const hit_fmt::PreparedFileConfiguration configuration(std::move(raw));
    const Input input(
        "define_case(Suite, Case) {auto sum=left+right; Use(sum);}\n");
    const auto checked = hit_fmt::CheckFile(input.View(), configuration);
    EXPECT_TRUE(checked.FormattingMismatch);
    EXPECT_TRUE(checked.Semantic.empty());
    EXPECT_NE(checked.Formatting.Text.find("left + right"), std::string::npos);
    const Input applied(checked.Formatting.Text);
    EXPECT_FALSE(
        hit_fmt::CheckFile(applied.View(), configuration).FormattingMismatch);
}

TEST(FileCheck, DiagnosticGoldenUsesOriginalCheckAndFinalFixCoordinates)
{
    const test::golden::Fixture fixture{
        std::filesystem::path(__FILE__).parent_path() / "testdata/file-check",
        "mapped",
    };
    auto configuration = Configuration();
    configuration.FormattingStyle.AddMissingControlBraces = true;
    configuration.FormattingStyle.NamespaceComments =
        formatter::NamespaceComments::AddMissing;
    configuration.FormattingStyle.NamespaceCommentSpaces = 7;
    configuration.FormattingStyle.ColumnLimit = 24;
    configuration.FormattingStyle.WidthDiagnostics =
        formatter::WidthDiagnosticPolicy::Report;
    configuration.Naming.Function.Case = formatter::NameCase::PascalCase;
    configuration.FunctionDeclarationParameterNames =
        formatter::FunctionDeclarationParameterNames::Required;
    configuration.FunctionDefinitionParameterNames =
        formatter::FunctionDefinitionParameterNames::NameRequired;
    const hit_fmt::PreparedFileConfiguration prepared(std::move(configuration));
    const Input input(fixture.InputNamed(".input.cpp"));
    const auto checked = hit_fmt::CheckFile(input.View(), prepared);
    ASSERT_EQ(checked.Semantic.size(), 4u);
    EXPECT_TRUE(checked.HasFailures(false));
    EXPECT_TRUE(checked.HasFailures(true));
    EXPECT_GT(
        checked.Semantic.back().Emitted.Line,
        checked.Semantic.back().Original.Line);
    ASSERT_FALSE(checked.Formatting.Diagnostics.empty());
    const auto& width = checked.Formatting.Diagnostics.back();
    const auto close = checked.Formatting.Text.find("// namespace test");
    ASSERT_NE(close, std::string::npos);
    EXPECT_EQ(
        width.Position.Line,
        1u +
        std::count(
            checked.Formatting.Text.begin(),
            checked.Formatting.Text.begin() + close,
            '\n'));
    for (const auto terminalWidth : {size_t(80), size_t(32)}) {
        for (const auto fix : {false, true}) {
            SCOPED_TRACE(fix ? "fix" : "check");
            SCOPED_TRACE(terminalWidth);
            std::ostringstream output;
            hit_fmt::PrintFileIssues(
                output,
                "sample.cpp",
                input
                    .View()
                    .View(),
                checked,
                fix,
                terminalWidth);
            output << "summary: ";
            hit_fmt::PrintFileSummaryIssues(output, checked, fix);
            output << '\n';
            fixture.Expect(
                output.str(),
                terminalWidth == 80
                ? (fix ? ".fix.txt" : ".check.txt")
                : (fix ? ".fix-narrow.txt" : ".check-narrow.txt"));
        }
    }
}

TEST(FileCheck, PreparedConfigurationOwnsSettingsAndResultsOutliveInputs)
{
    auto raw = Configuration();
    raw.Naming.Function.Case = formatter::NameCase::PascalCase;
    raw.NamingNamespaceExemptions = {"ignored"};
    const hit_fmt::PreparedFileConfiguration prepared(raw);
    raw.NamingNamespaceExemptions = {"different"};
    raw.FormattingStyle.CommaSpaceAfter = formatter::GapPolicy::NoSpace;
    raw.Naming.Function.Case = formatter::NameCase::Unchecked;
    hit_fmt::CheckedFile first;
    {
        const Input input("void bad_name(){invoke(a,b);}\n");
        std::string path = "missing/directory with spaces/original.cpp";
        const formatter::Hunk selected[] = {{1, 1}};
        const hit_fmt::ChangedFileScope scope{selected};
        first = hit_fmt::CheckFile(input.View(), prepared, path, scope);
        const auto withoutPath =
            hit_fmt::CheckFile(input.View(), prepared, {}, scope);
        EXPECT_EQ(first.Formatting.Text, withoutPath.Formatting.Text);
        ASSERT_EQ(first.Semantic.size(), withoutPath.Semantic.size());
        for (size_t i = 0; i < first.Semantic.size(); ++i) {
            EXPECT_EQ(
                first.Semantic[i].Message,
                withoutPath.Semantic[i].Message);
            EXPECT_EQ(
                first.Semantic[i].Original.Line,
                withoutPath.Semantic[i].Original.Line);
            EXPECT_EQ(
                first.Semantic[i].Original.Column,
                withoutPath.Semantic[i].Original.Column);
            EXPECT_EQ(
                first.Semantic[i].Emitted.Line,
                withoutPath.Semantic[i].Emitted.Line);
            EXPECT_EQ(
                first.Semantic[i].Emitted.Column,
                withoutPath.Semantic[i].Emitted.Column);
        }
        path.assign(path.size(), '?');
    }
    const Input second("void other_bad(){invoke(c,d);}\n");
    const auto other = hit_fmt::CheckFile(second.View(), prepared, {});
    EXPECT_NE(first.Formatting.Text.find("a, b"), std::string::npos);
    EXPECT_NE(other.Formatting.Text.find("c, d"), std::string::npos);
    ASSERT_EQ(first.Semantic.size(), 1u);
    ASSERT_EQ(other.Semantic.size(), 1u);
    EXPECT_TRUE(first.HasFailures(true));
    EXPECT_TRUE(other.HasFailures(true));
    const Input exempt("namespace ignored {void third_bad(){invoke(e,f);}}\n");
    const auto exempted = hit_fmt::CheckFile(exempt.View(), prepared);
    EXPECT_TRUE(exempted.Semantic.empty());
    EXPECT_NE(exempted.Formatting.Text.find("e, f"), std::string::npos);
}

TEST(FileCheck, TokenEndpointAndUnicodePositionsMapWithoutReparsingOutput)
{
    auto raw = Configuration();
    raw.Naming.Function.Case = formatter::NameCase::PascalCase;
    raw.FunctionDeclarationParameterNames =
        formatter::FunctionDeclarationParameterNames::Required;
    raw.FunctionDefinitionParameterNames =
        formatter::FunctionDefinitionParameterNames::NameRequired;
    const hit_fmt::PreparedFileConfiguration prepared(raw);
    const std::string source = "const char* text=\"é\"; void bad_name(int  , "
        "void (*)(int)){}\nvoid GoodProto(int);\n";
    const Input input(source);
    const auto checked = hit_fmt::CheckFile(input.View(), prepared);
    ASSERT_EQ(checked.Semantic.size(), 4u);
    EXPECT_NE(
        checked
            .Semantic
            .back()
            .Message
            .find("function declaration"),
        std::string::npos);
    EXPECT_EQ(
        checked.Semantic[0].Original.Column,
        source.find(
            "bad_name") /* one extra UTF8 byte cancels one-based column */);
    const auto emitted = checked.Formatting.Text.find("bad_name");
    const auto lineStart = checked.Formatting.Text.rfind('\n', emitted);
    const auto begin = lineStart == std::string::npos ? 0 : lineStart + 1;
    EXPECT_EQ(
        checked.Semantic[0].Emitted.Column,
        1u +
        std::count_if(
            checked.Formatting.Text.begin() + begin,
            checked.Formatting.Text.begin() + emitted,
            [] (unsigned char c) { return (c & 0xc0) != 0x80; }));
    const auto& insertion = checked.Semantic[1];
    EXPECT_EQ(insertion.Emitted.Line, checked.Semantic[0].Emitted.Line);
    EXPECT_EQ(
        insertion.Emitted.Column,
        checked.Semantic[0].Emitted.Column +
        std::string_view("bad_name(int").size());
    const auto& pointerInsertion = checked.Semantic[2];
    EXPECT_EQ(pointerInsertion.Emitted.Line, insertion.Emitted.Line);
    EXPECT_GT(pointerInsertion.Emitted.Column, insertion.Emitted.Column);

    // Invalid UTF8 in a protected literal must not shift later name locations.
    const std::string malformed = std::string("const char* text=\"") + '\x80' +
        "\";\tvoid another_bad(){}\n";
    const Input recovery(malformed);
    const auto recovered = hit_fmt::CheckFile(recovery.View(), prepared);
    ASSERT_EQ(recovered.Semantic.size(), 1u);
    EXPECT_EQ(
        recovered.Semantic[0].Original.Column,
        malformed.find("another_bad") + 1);
    const auto recoveredName = recovered.Formatting.Text.find("another_bad");
    const auto recoveredStart =
        recovered.Formatting.Text.rfind('\n', recoveredName);
    EXPECT_EQ(
        recovered.Semantic[0].Emitted.Column,
        recoveredName -
        (recoveredStart == std::string::npos ? 0 : recoveredStart + 1) + 1);
}

TEST(FileCheck, UnicodeWidthDiagnosticGoldenMapsCodePointsAndTabs)
{
    const test::golden::Fixture fixture{
        std::filesystem::path(__FILE__).parent_path() / "testdata/file-check",
        "unicode-width",
    };
    formatter::Configuration raw;
    raw.FormattingStyle.ColumnLimit = 12;
    raw.FormattingStyle.WidthDiagnostics =
        formatter::WidthDiagnosticPolicy::Report;
    const hit_fmt::PreparedFileConfiguration prepared(raw);
    const Input input(fixture.InputNamed(".input.cpp"));
    const auto checked = hit_fmt::CheckFile(input.View(), prepared);
    EXPECT_FALSE(checked.FormattingMismatch);
    ASSERT_EQ(checked.Formatting.Diagnostics.size(), 2u);
    ASSERT_EQ(checked.OriginalWidthPositions.size(), 2u);
    EXPECT_EQ(checked.Formatting.Diagnostics[0].Position.Line, 2u);
    EXPECT_EQ(checked.Formatting.Diagnostics[1].Position.Line, 4u);
    EXPECT_EQ(checked.OriginalWidthPositions[0].Column, 13u);
    EXPECT_EQ(checked.OriginalWidthPositions[1].Column, 12u);
    for (const bool fix : {false, true}) {
        std::ostringstream output;
        hit_fmt::PrintFileIssues(
            output,
            "unicode.cpp",
            input
                .View()
                .View(),
            checked,
            fix,
            80);
        fixture.Expect(output.str(), fix ? ".fix.txt" : ".check.txt");
    }

    // A tab itself can contain the first column exceeding the limit.
    raw.FormattingStyle.ColumnLimit = 6;
    const hit_fmt::PreparedFileConfiguration narrow(raw);
    const Input tab("// é界😀\t\n");
    const auto tabbed = hit_fmt::CheckFile(tab.View(), narrow);
    ASSERT_EQ(tabbed.Formatting.Diagnostics.size(), 1u);
    ASSERT_EQ(tabbed.OriginalWidthPositions.size(), 1u);
    EXPECT_EQ(tabbed.OriginalWidthPositions[0].Column, 7u);
}

TEST(FileCheck, ActualProtectedOverflowRemainsFailureInFixMode)
{
    auto raw = Configuration();
    raw.FormattingStyle.ColumnLimit = 10;
    raw.FormattingStyle.WidthDiagnostics =
        formatter::WidthDiagnosticPolicy::Report;
    const hit_fmt::PreparedFileConfiguration prepared(raw);
    const Input input(
        "// hit-fmt off\r\nint " "unavoidable_long_name;\r\n// hit-fmt on\r\n");
    const auto checked = hit_fmt::CheckFile(input.View(), prepared);
    EXPECT_EQ(
        checked.Formatting.Text,
        input
            .View()
            .View());
    EXPECT_FALSE(checked.FormattingMismatch);
    EXPECT_TRUE(checked.HasFailures(false));
    EXPECT_TRUE(checked.HasFailures(true));
    ASSERT_GE(checked.Formatting.Diagnostics.size(), 2u);
    EXPECT_EQ(checked.Formatting.Diagnostics[1].Position.Line, 2u);
}

TEST(FileCheck, ChangedHunkOptionsSupportAliasesAndRejectMixedInputs)
{
    for (const auto option : {"--changed-hunks", "--changed-ranges"}) {
        const char* argv[] = {
            "hit-fmt",
            "--fix",
            option,
            "-",
            "--no-spillover",
            "--threads=2",
            "--summary",
        };
        const auto parsed = hit_fmt::ParseOptions(std::size(argv), argv);
        ASSERT_TRUE(parsed.HunkInput);
        EXPECT_EQ(*parsed.HunkInput, "-");
        EXPECT_TRUE(parsed.Paths.empty());
        EXPECT_FALSE(parsed.Spillover);
        EXPECT_TRUE(parsed.Fix);
        EXPECT_TRUE(parsed.SummaryOnly);
        EXPECT_EQ(parsed.Threads, 2u);
    }
    for (const auto option :
        {"--changed-hunks=ranges.txt", "--changed-ranges=ranges.txt"})
    {
        const char* argv[] = {"hit-fmt", option};
        const auto parsed = hit_fmt::ParseOptions(std::size(argv), argv);
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
        })
    {
        EXPECT_THROW(
            hit_fmt::ParseOptions(argv.size(), argv.data()),
            std::runtime_error);
    }
    const char* positional[] =
        {"hit-fmt", "--", "--changed-hunks", "--no-spillover"};
    const auto parsed =
        hit_fmt::ParseOptions(std::size(positional), positional);
    EXPECT_FALSE(parsed.HunkInput);
    EXPECT_EQ(parsed.Paths.size(), 2u);
}

TEST(FileCheck, EmptyHunkScopeNeverFallsBackToWholeFile)
{
    auto raw = Configuration();
    raw.Naming.Function.Case = formatter::NameCase::PascalCase;
    raw.FunctionDeclarationParameterNames =
        formatter::FunctionDeclarationParameterNames::Required;
    raw.FormattingStyle.ColumnLimit = 10;
    raw.FormattingStyle.WidthDiagnostics =
        formatter::WidthDiagnosticPolicy::Report;
    const hit_fmt::PreparedFileConfiguration prepared(raw);
    const Input input("void bad_name(int);\n");
    const auto empty = hit_fmt::CheckFile(
        input.View(),
        prepared,
        {},
        hit_fmt::ChangedFileScope{});
    EXPECT_EQ(
        empty.Formatting.Text,
        input
            .View()
            .View());
    EXPECT_FALSE(empty.HasFailures(false));
    EXPECT_FALSE(empty.HasFailures(true));
    EXPECT_TRUE(empty.Incomplete.empty());
    EXPECT_TRUE(hit_fmt::CheckFile(input.View(), prepared).HasFailures(true));
    const formatter::Hunk invalid[] = {{2, 2}};
    EXPECT_THROW(
        hit_fmt::CheckFile(
            input.View(),
            prepared,
            {},
            hit_fmt::ChangedFileScope{invalid}),
        std::invalid_argument);
}

TEST(FileCheck, HunkDiagnosticGoldenUsesOriginalSelectionAndMappedOutput)
{
    const test::golden::Fixture fixture{
        std::filesystem::path(__FILE__).parent_path() / "testdata/file-check",
        "hunks",
    };
    auto raw = Configuration();
    raw.Naming.Function.Case = formatter::NameCase::PascalCase;
    raw.FunctionDeclarationParameterNames =
        formatter::FunctionDeclarationParameterNames::Required;
    raw.FunctionDefinitionParameterNames =
        formatter::FunctionDefinitionParameterNames::NameRequired;
    raw.FormattingStyle.IncludeOrder = formatter::IncludeOrder::Sort;
    raw.FormattingStyle.AddMissingControlBraces = true;
    raw.FormattingStyle.ColumnLimit = 40;
    raw.FormattingStyle.WidthDiagnostics =
        formatter::WidthDiagnosticPolicy::Report;
    const hit_fmt::PreparedFileConfiguration prepared(raw);
    const Input input(fixture.InputNamed(".input.cpp"));
    const formatter::Hunk selected[] =
        {{13, 13}, {11, 11}, {9, 9}, {4, 5}, {1, 1}, {5, 5}};
    for (const bool spillover : {false, true}) {
        const auto checked = hit_fmt::CheckFile(
            input.View(),
            prepared,
            "sample.cpp",
            hit_fmt::ChangedFileScope{selected, spillover});
        ASSERT_EQ(checked.Semantic.size(), 4u);
        for (const auto& diagnostic : checked.Semantic) {
            EXPECT_NE(diagnostic.Original.Line, 3u);
            EXPECT_NE(diagnostic.Original.Line, 6u);
        }
        ASSERT_FALSE(checked.Formatting.Diagnostics.empty());
        for (const auto position : checked.OriginalWidthPositions) {
            EXPECT_NE(position.Line, 3u);
            EXPECT_NE(position.Line, 14u);
        }
        EXPECT_EQ(
            std::any_of(
                checked.OriginalWidthPositions.begin(),
                checked.OriginalWidthPositions.end(),
                [] (auto position) {
                    return position.Line == 10;
                }),
            spillover);
        EXPECT_TRUE(checked.FormattingMismatch);
        EXPECT_TRUE(checked.HasFailures(false));
        EXPECT_TRUE(checked.HasFailures(true));
        EXPECT_EQ(checked.RequiredChanges.empty(), spillover);
        EXPECT_EQ(
            checked.Formatting.Text.starts_with("#include \"a.h\""),
            spillover);
        EXPECT_NE(
            checked.Formatting.Text.find("void bad_before(int);"),
            std::string::npos);
        EXPECT_NE(
            checked.Formatting.Text.find("const char* Outside = "
                "\"unavoidable_outside_literal_over_the_limit\";"),
            std::string::npos);
        for (const bool fix : {false, true}) {
            std::ostringstream output;
            hit_fmt::PrintFileIssues(
                output,
                "sample.cpp",
                input
                    .View()
                    .View(),
                checked,
                fix);
            output << "summary: ";
            hit_fmt::PrintFileSummaryIssues(output, checked, fix);
            output << '\n';
            EXPECT_EQ(
                output
                    .str()
                    .find("note:"),
                std::string::npos);
            if (!fix) {
                EXPECT_EQ(
                    output
                        .str()
                        .find("cannot"),
                    std::string::npos);
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
    const hit_fmt::PreparedFileConfiguration prepared(raw);
    const Input input("void Good() {\n    call(1,\n        2);\n}\n");
    const formatter::Hunk selected[] = {{3, 3}};
    const auto checked = hit_fmt::CheckFile(
        input.View(),
        prepared,
        {},
        hit_fmt::ChangedFileScope{selected, false});
    EXPECT_FALSE(checked.FormattingMismatch);
    EXPECT_FALSE(checked.HasFailures(false));
    EXPECT_FALSE(checked.HasFailures(true));
    EXPECT_FALSE(checked.Incomplete.empty());
    for (const bool fix : {false, true}) {
        std::ostringstream output, summary;
        hit_fmt::PrintFileIssues(
            output,
            "sample.cpp",
            input
                .View()
                .View(),
            checked,
            fix);
        hit_fmt::PrintFileSummaryIssues(summary, checked, fix);
        EXPECT_TRUE(output
            .str()
            .empty());
        EXPECT_TRUE(summary
            .str()
            .empty());
    }

    raw.FormattingStyle.AddMissingControlBraces = true;
    const hit_fmt::PreparedFileConfiguration braces(raw);
    const Input blocked("void Good(bool ready) {\n  if (ready)\n"
        "    consume(\n      ready);\n}\n");
    const auto required = hit_fmt::CheckFile(
        blocked.View(),
        braces,
        {},
        hit_fmt::ChangedFileScope{selected, false});
    ASSERT_EQ(required.RequiredChanges.size(), 1u);
    EXPECT_TRUE(required.Semantic.empty());
    EXPECT_TRUE(required.Formatting.Diagnostics.empty());
    EXPECT_TRUE(required.HasFailures(false));
    EXPECT_TRUE(required.HasFailures(true));
    for (const bool fix : {false, true}) {
        std::ostringstream output, summary;
        hit_fmt::PrintFileIssues(
            output,
            "sample.cpp",
            blocked
                .View()
                .View(),
            required,
            fix);
        hit_fmt::PrintFileSummaryIssues(summary, required, fix);
        for (const auto& text : {output.str(), summary.str()}) {
            EXPECT_NE(
                text.find("fail: control body requires braces"),
                std::string::npos);
            EXPECT_EQ(
                text.find("cannot automatically fix") != std::string::npos,
                fix);
            EXPECT_EQ(text.find("note:"), std::string::npos);
        }
    }
}

TEST(FileCheck, UnsupportedPreparationFailsClosed)
{
    auto raw = Configuration();
    raw.FormattingStyle.CallLikeMacroNames = {"CALL_HINT"};
    EXPECT_THROW(
        hit_fmt::PreparedFileConfiguration{raw},
        std::invalid_argument);
    raw.FormattingStyle.CallLikeMacroNames.clear();
    raw.FormattingStyle.AmbiguousTemplateDeclarationSpacing =
        formatter::GapPolicy::Space;
    EXPECT_THROW(
        hit_fmt::PreparedFileConfiguration{raw},
        std::invalid_argument);
}

} // namespace
