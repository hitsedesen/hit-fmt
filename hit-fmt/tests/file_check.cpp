#include <hit-fmt/file_check.hpp>
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
        first = hit_fmt::CheckFile(input.View(), prepared, path);
        const auto withoutPath = hit_fmt::CheckFile(input.View(), prepared);
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

TEST(FileCheck, PreviewHunkModesAndUnsupportedPreparationFailClosed)
{
    for (const auto option :
        {
            "--changed-hunks",
            "--changed-hunks=-",
            "--changed-ranges",
            "--changed-ranges=file.txt",
            "--no-spillover",
        })
    {
        SCOPED_TRACE(option);
        EXPECT_THROW(
            hit_fmt::ValidatePreviewOption(option),
            std::invalid_argument);
    }
    EXPECT_NO_THROW(hit_fmt::ValidatePreviewOption("--fix"));
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
