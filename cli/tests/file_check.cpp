#include <util/memory.hpp>
#include <util/types.hpp>

#include <cli/arguments.hpp>
#include <cli/file_check.hpp>
#include <cli/formatting_diagnostics.hpp>
#include <cli/source_excerpt.hpp>
#include <cli/terminal.hpp>

#include <gtest/gtest.h>

#include <tests/golden.hpp>
#include <tests/input.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <memory_resource>
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
    auto configurationResult = cli::PrepareConfiguration(Configuration());
    ASSERT_FALSE(configurationResult.Error) << configurationResult.Error;
    const auto& configuration = configurationResult.Value;
    Input input("void Good(int value){invoke(left,right);}\n");
    auto checkedResult = cli::FormatAndCheckFile(input.View(), configuration);
    ASSERT_FALSE(checkedResult.Error) << checkedResult.Error;
    const auto& checked = checkedResult.Value;
    ASSERT_TRUE(checked.FormattingMismatch);
    EXPECT_TRUE(checked.HasFailures(false));
    EXPECT_FALSE(checked.HasFailures(true));
    EXPECT_NE(checked.Formatting.Text.find("left, right"), std::string::npos);
    EXPECT_TRUE(checked.Semantic.empty());
    std::ostringstream checkSummary, fixSummary;
    cli::PrintFileSummaryIssues(checkSummary, checked, false);
    cli::PrintFileSummaryIssues(fixSummary, checked, true);
    EXPECT_EQ(checkSummary.str(), "formatting mismatch");
    EXPECT_TRUE(fixSummary.str().empty());
    const Input applied(checked.Formatting.Text);
    auto cleanResult = cli::FormatAndCheckFile(applied.View(), configuration);
    ASSERT_FALSE(cleanResult.Error) << cleanResult.Error;
    const auto& clean = cleanResult.Value;
    EXPECT_EQ(clean.Formatting.Text, checked.Formatting.Text);
    EXPECT_FALSE(clean.HasFailures(false));
    EXPECT_FALSE(clean.HasFailures(true));

    for (auto detail :
        {cli::DiagnosticDetail::Quick, cli::DiagnosticDetail::Summary, cli::DiagnosticDetail::Detailed})
    {
        for (bool fix : {false, true}) {
            std::ostringstream output;
            EXPECT_FALSE(cli::PrintCheckResult(output, "sample.cpp", applied.View().View(), clean, detail, fix));
            EXPECT_TRUE(output.str().empty());
        }
    }
}

TEST(FileCheck, ASCIICommentsGolden)
{
    const test::golden::Fixture fixture{
        std::filesystem::path(__FILE__).parent_path() / "testdata/file-check",
        "ascii-comments",
    };
    const Input input(fixture.InputNamed(".input.cpp"));
    auto raw = Configuration();
    auto disabled = cli::PrepareConfiguration(raw);
    ASSERT_FALSE(disabled.Error) << disabled.Error;
    auto unchecked = cli::FormatAndCheckFile(input.View(), disabled.Value);
    ASSERT_FALSE(unchecked.Error) << unchecked.Error;
    EXPECT_TRUE(unchecked.Value.Semantic.empty());
    raw.CommentsASCIIOnly = true;
    auto prepared = cli::PrepareConfiguration(raw);
    ASSERT_FALSE(prepared.Error) << prepared.Error;

    for (bool fix : {false, true}) {
        for (auto detail :
            {cli::DiagnosticDetail::Quick, cli::DiagnosticDetail::Summary, cli::DiagnosticDetail::Detailed})
        {
            auto result = cli::FormatAndCheckFile(
                input.View(),
                prepared.Value,
                {},
                std::nullopt,
                detail,
                false,
                nullptr,
                fix);
            ASSERT_FALSE(result.Error) << result.Error;
            EXPECT_TRUE(result.Value.HasFailures(fix));

            if (detail == cli::DiagnosticDetail::Quick && !fix) {
                EXPECT_TRUE(result.Value.QuickFailure);
                continue;
            }

            ASSERT_EQ(result.Value.Semantic.size(), 3u);
            std::ostringstream output;

            if (detail == cli::DiagnosticDetail::Quick) {
                cli::PrintFileSummaryIssues(output, result.Value, fix);
                fixture.Expect(output.str(), ".summary.txt");
                continue;
            }

            cli::PrintFileIssues(output, "sample.cpp", input.View().View(), result.Value, fix);
            fixture.Expect(output.str(), fix ? ".fix.txt" : ".check.txt");
        }
    }

    const std::array hunks{formatter::Hunk{5, 5}};
    auto scoped = cli::FormatAndCheckFile(input.View(), prepared.Value, {}, cli::ChangedFileScope{hunks});
    ASSERT_FALSE(scoped.Error) << scoped.Error;
    ASSERT_EQ(scoped.Value.Semantic.size(), 1u);
    EXPECT_EQ(scoped.Value.Semantic.front().Original.Line, 5u);

    const Input ascii("// English, identifiers_123, symbols: <=> and bonjour.\n" "const char* text = \"// 日本語\";\n");
    auto clean = cli::FormatAndCheckFile(ascii.View(), prepared.Value);
    ASSERT_FALSE(clean.Error) << clean.Error;
    EXPECT_TRUE(clean.Value.Semantic.empty());
}

TEST(FileCheck, DiagnosticModesGolden)
{
    using D = cli::DiagnosticDetail;
    const test::golden::Fixture fixture{std::filesystem::path(__FILE__).parent_path() / "testdata/file-check", "modes"};
    auto raw = Configuration();
    raw.Naming.Function.Case = formatter::NameCase::PascalCase;
    raw.FunctionDefinitionParameterNames = formatter::FunctionDefinitionParameterNames::NameRequired;
    raw.FormattingStyle.ColumnLimit = 20;
    raw.FormattingStyle.WidthDiagnostics = formatter::WidthDiagnosticPolicy::Report;
    auto prepared = cli::PrepareConfiguration(raw);
    ASSERT_FALSE(prepared.Error) << prepared.Error;
    const Input input("const char* Text=\"é\";\nvoid bad_name(int,int){invoke(left,right);}\n"
        "// hit-fmt off\nint unavoidable_long_name;\n// hit-fmt on\n");

    for (bool fix : {false, true}) {
        for (auto detail : {D::Quick, D::Summary, D::Detailed}) {
            SCOPED_TRACE(fix);
            SCOPED_TRACE(static_cast<int>(detail));
            auto result = cli::FormatAndCheckFile(
                input.View(),
                prepared.Value,
                "sample.cpp",
                std::nullopt,
                detail,
                false,
                nullptr,
                fix);
            ASSERT_FALSE(result.Error) << result.Error;
            const auto& checked = result.Value;
            EXPECT_TRUE(checked.HasFailures(fix));

            if (!(detail == D::Quick && !fix)) {
                EXPECT_TRUE(checked.FormattingMismatch);
                EXPECT_EQ(checked.Semantic.size(), 3u);
            }

            std::ostringstream output;
            EXPECT_FALSE(cli::PrintCheckResult(output, "sample.cpp", input.View().View(), checked, detail, fix));

            if (detail == D::Quick && checked.HasFailures(fix)) {
                cli::PrintQuickHint(output, fix);
            }

            const auto suffix = detail == D::Quick
                ? (fix ? ".quick-fix.txt" : ".quick-check.txt")
                : detail == D::Summary
                    ? (fix ? ".summary-fix.txt" : ".summary-check.txt")
                    : (fix ? ".full-fix.txt" : ".full-check.txt");
            fixture.Expect(output.str(), suffix);
        }
    }
}

TEST(FileCheck, QuickStopsAfterConfirmedNamingAndParameterFailures)
{
    auto raw = Configuration();
    raw.Naming.Namespace.Case = formatter::NameCase::PascalCase;
    raw.Naming.GlobalVariable.Case = formatter::NameCase::CamelCase;
    raw.Naming.LocalVariable.Case = formatter::NameCase::CamelCase;
    raw.Naming.Function.Case = formatter::NameCase::PascalCase;
    raw.FunctionDefinitionParameterNames = formatter::FunctionDefinitionParameterNames::NameRequired;
    auto prepared = cli::PrepareConfiguration(raw);
    ASSERT_FALSE(prepared.Error) << prepared.Error;

    for (std::string_view source :
        {
            "void bad_name(int,int){}\nvoid another_bad(){}\n",
            "void Good(int,int){}\nvoid Another(int){}\n",
            "int BadName, AnotherBadName;\n",
            "void Good(){auto [BadName,AnotherBadName]=Make();}\n",
            "namespace bad::another_bad { void Good() {} }\n",
        })
    {
        const Input input(source);
        auto quick = cli::FormatAndCheckFile(
            input.View(),
            prepared.Value,
            {},
            std::nullopt,
            cli::DiagnosticDetail::Quick);
        auto full = cli::FormatAndCheckFile(
            input.View(),
            prepared.Value,
            {},
            std::nullopt,
            cli::DiagnosticDetail::Detailed);
        ASSERT_FALSE(quick.Error) << quick.Error;
        ASSERT_FALSE(full.Error) << full.Error;
        EXPECT_TRUE(quick.Value.HasFailures(false));
        EXPECT_TRUE(quick.Value.QuickFailure);
        EXPECT_TRUE(quick.Value.Semantic.empty());
        EXPECT_GT(full.Value.Semantic.size(), 1u);
        EXPECT_TRUE(quick.Value.Formatting.Text.empty());
        EXPECT_FALSE(quick.Value.Formatting.Summary);
    }
}

TEST(FileCheck, QuickAndCompleteChecksAgreeOnFormattingWidthAndHunkScope)
{
    auto raw = Configuration();
    raw.FormattingStyle.IncludeOrder = formatter::IncludeOrder::Sort;
    raw.Naming.Function.Case = formatter::NameCase::PascalCase;
    raw.FormattingStyle.ColumnLimit = 20;
    raw.FormattingStyle.WidthDiagnostics = formatter::WidthDiagnosticPolicy::Report;
    auto prepared = cli::PrepareConfiguration(raw);
    ASSERT_FALSE(prepared.Error) << prepared.Error;
    const std::array hunks{formatter::Hunk{2, 2}};

    for (std::string_view source :
        {
            "void Good() {}\n// tail\n",
            "void Good(){invoke(left,right);}\n// tail\n",
            "// hit-fmt off\nint unavoidable_long_name;\nint another_unavoidable_long_name;\n// hit-fmt on\n",
            "void bad_name(){}\nvoid Good() {}\n",
            "#include <z>\n#include <a>\n",
        })
    {
        const Input input(source);

        for (auto scope :
            {
                std::optional<cli::ChangedFileScope>{},
                std::optional{cli::ChangedFileScope{hunks, false}},
                std::optional{cli::ChangedFileScope{{}, false}},
            })
        {
            auto quick = cli::FormatAndCheckFile(input.View(), prepared.Value, {}, scope, cli::DiagnosticDetail::Quick);
            auto full = cli::FormatAndCheckFile(
                input.View(),
                prepared.Value,
                {},
                scope,
                cli::DiagnosticDetail::Detailed);
            ASSERT_FALSE(quick.Error) << quick.Error;
            ASSERT_FALSE(full.Error) << full.Error;
            EXPECT_EQ(quick.Value.HasFailures(false), full.Value.HasFailures(false)) << source;

            if (!scope) {
                EXPECT_LE(quick.Value.Formatting.Diagnostics.size(), 1u);

                if (!quick.Value.Formatting.Diagnostics.empty()) {
                    EXPECT_EQ(quick.Value.Formatting.Diagnostics.front().Position.Line, 0u);
                    EXPECT_EQ(quick.Value.Formatting.Diagnostics.front().Position.Column, 0u);
                    EXPECT_TRUE(quick.Value.Formatting.Diagnostics.front().Message.empty());
                }
            }
        }
    }
}

TEST(FileCheck, DiagnosticMetadataMatchesTheRequestedCoordinateSpace)
{
    using D = cli::DiagnosticDetail;
    auto raw = Configuration();
    raw.Naming.Function.Case = formatter::NameCase::PascalCase;
    raw.FormattingStyle.ColumnLimit = 20;
    raw.FormattingStyle.WidthDiagnostics = formatter::WidthDiagnosticPolicy::Report;
    auto prepared = cli::PrepareConfiguration(raw);
    ASSERT_FALSE(prepared.Error) << prepared.Error;
    const Input input("void bad_name(){invoke(left,right); }\n");

    for (bool fix : {false, true}) {
        for (auto detail : {D::Summary, D::Detailed}) {
            auto result = cli::FormatAndCheckFile(
                input.View(),
                prepared.Value,
                {},
                std::nullopt,
                detail,
                false,
                nullptr,
                fix);
            ASSERT_FALSE(result.Error) << result.Error;
            const auto& checked = result.Value;
            ASSERT_FALSE(checked.Semantic.empty());
            EXPECT_EQ(checked.Semantic.front().Emitted.Line != 0, fix);

            if (detail == D::Summary && fix) {
                EXPECT_FALSE(checked.Formatting.Changes);
                EXPECT_FALSE(checked.Formatting.Summary);
            } else {
                ASSERT_TRUE(checked.Formatting.Changes);
                EXPECT_GT(checked.Formatting.Changes->OriginalLines.Count(), 0u);
                EXPECT_EQ(checked.Formatting.Changes->OutputLines.Count() != 0, fix);
            }

            EXPECT_EQ(checked.Formatting.OriginalTokens.empty(), !fix);
        }
    }

    const Input includes("#include <z>\n#include <a>\n");
    raw.FormattingStyle.IncludeOrder = formatter::IncludeOrder::Sort;
    auto includePrepared = cli::PrepareConfiguration(raw);
    ASSERT_FALSE(includePrepared.Error) << includePrepared.Error;
    auto quick = cli::FormatAndCheckFile(includes.View(), includePrepared.Value, {}, std::nullopt, D::Quick);
    ASSERT_FALSE(quick.Error) << quick.Error;
    EXPECT_TRUE(quick.Value.FormattingMismatch);
    EXPECT_FALSE(quick.Value.Formatting.Changes);
    EXPECT_FALSE(quick.Value.Formatting.Summary);
    EXPECT_TRUE(quick.Value.Formatting.Map.empty());
    EXPECT_TRUE(quick.Value.Formatting.OriginalTokens.empty());
}

TEST(FileCheck, CompactRequirementsUseStatementSequenceSpacing)
{
    auto directory = std::filesystem::path(FORMATTER_TESTDATA_DIR).parent_path() / "requires-body-shape";
    auto raw = formatter::LoadConfigFile(directory / "spacing-space.hit-fmt");
    auto configurationResult = cli::PrepareConfiguration(raw);
    ASSERT_FALSE(configurationResult.Error) << configurationResult.Error;
    const auto& configuration = configurationResult.Value;
    auto expected = test::golden::ReadFile(directory / "spacing-space.output.cpp");
    const Input correct(expected);
    auto correctResult = cli::FormatAndCheckFile(correct.View(), configuration);
    ASSERT_FALSE(correctResult.Error) << correctResult.Error;
    EXPECT_FALSE(correctResult.Value.HasFailures(false));

    auto missingSpace = expected;
    auto boundary = missingSpace.find("; value.second");
    ASSERT_NE(boundary, std::string::npos);
    missingSpace.erase(boundary + 1, 1);
    const Input incorrect(missingSpace);
    auto checkedResult = cli::FormatAndCheckFile(incorrect.View(), configuration);
    ASSERT_FALSE(checkedResult.Error) << checkedResult.Error;
    const auto& checked = checkedResult.Value;
    EXPECT_TRUE(checked.FormattingMismatch);
    EXPECT_TRUE(checked.HasFailures(false));
    EXPECT_EQ(checked.Formatting.Text, expected);
}

TEST(FileCheck, WrappedInitializerPassesCheckAfterFix)
{
    auto directory = std::filesystem::path(FORMATTER_TESTDATA_DIR) / "../initializer-wrapping";
    auto configurationResult = cli::PrepareConfiguration(formatter::LoadConfigFile(directory / "canonical.hit-fmt"));
    ASSERT_FALSE(configurationResult.Error) << configurationResult.Error;
    const auto& configuration = configurationResult.Value;
    const Input input(test::golden::ReadFile(directory / "input.cpp"));

    for (auto detail : {cli::DiagnosticDetail::Detailed, cli::DiagnosticDetail::Summary}) {
        auto checkedResult = cli::FormatAndCheckFile(input.View(), configuration, {}, std::nullopt, detail);
        ASSERT_FALSE(checkedResult.Error) << checkedResult.Error;
        const auto& checked = checkedResult.Value;
        EXPECT_TRUE(checked.FormattingMismatch);
        EXPECT_TRUE(checked.HasFailures(false));
        EXPECT_FALSE(checked.HasFailures(true));
        EXPECT_TRUE(checked.Formatting.Diagnostics.empty());
        const Input applied(checked.Formatting.Text);
        auto cleanResult = cli::FormatAndCheckFile(applied.View(), configuration, {}, std::nullopt, detail);
        ASSERT_FALSE(cleanResult.Error) << cleanResult.Error;
        const auto& clean = cleanResult.Value;
        EXPECT_EQ(clean.Formatting.Text, checked.Formatting.Text);
        EXPECT_FALSE(clean.HasFailures(false));
        EXPECT_FALSE(clean.HasFailures(true));
    }
}

TEST(FileCheck, DebugExplainsProtectionAndParserRecoveryWithoutChangingResults)
{
    const test::golden::Fixture fixture{std::filesystem::path(__FILE__).parent_path() / "testdata/file-check", "debug"};
    const char* args[] = {"hit-fmt", "--debug", "--fix", "--summary", "sample.cpp"};
    auto argumentsResult = cli::ParseArguments(args);
    ASSERT_FALSE(argumentsResult.Error) << argumentsResult.Error;
    const auto& arguments = argumentsResult.Value;
    auto raw = Configuration();
    raw.FormattingStyle.AddMissingControlBraces = true;
    raw.FormattingStyle.WhitespaceSensitiveMacros = {"KEEP"};
    raw.FormattingStyle.HonorClangFormatDirectives = true;
    auto configurationResult = cli::PrepareConfiguration(raw);
    ASSERT_FALSE(configurationResult.Error) << configurationResult.Error;
    const auto& configuration = configurationResult.Value;
    const Input input(fixture.InputNamed(".input.cpp"));
    auto ordinaryResult = cli::FormatAndCheckFile(input.View(), configuration);
    ASSERT_FALSE(ordinaryResult.Error) << ordinaryResult.Error;
    const auto& ordinary = ordinaryResult.Value;
    EXPECT_TRUE(ordinary.Debug.empty());
    EXPECT_TRUE(ordinary.Formatting.InputProtection.empty());
    EXPECT_NE(ordinary.Formatting.Text.find("if (ready) {"), std::string::npos);

    for (auto detail : {cli::DiagnosticDetail::Detailed, cli::DiagnosticDetail::Summary}) {
        auto checkedResult = cli::FormatAndCheckFile(
            input.View(),
            configuration,
            "sample.cpp",
            std::nullopt,
            detail,
            arguments.Debug);
        ASSERT_FALSE(checkedResult.Error) << checkedResult.Error;
        const auto& checked = checkedResult.Value;
        EXPECT_EQ(checked.Formatting.Text, ordinary.Formatting.Text);
        EXPECT_EQ(checked.HasFailures(false), ordinary.HasFailures(false));
        EXPECT_EQ(checked.HasFailures(arguments.Fix), ordinary.HasFailures(arguments.Fix));
        ASSERT_FALSE(checked.Debug.empty());
        std::ostringstream output;
        cli::PrintFileDebug(output, "sample.cpp", checked);
        fixture.Expect(output.str(), ".debug.txt");
    }

    raw.FormattingStyle.HonorClangFormatDirectives = false;
    auto ignoredResult = cli::FormatAndCheckFile(
        input.View(),
        cli::PrepareConfiguration(raw).Value,
        {},
        std::nullopt,
        cli::DiagnosticDetail::Summary,
        true);
    ASSERT_FALSE(ignoredResult.Error) << ignoredResult.Error;
    const auto& ignored = ignoredResult.Value;
    EXPECT_TRUE(std::ranges::none_of(
        ignored.Formatting.InputProtection,
        [&] (const auto& range) {
            auto source = input.View().View().substr(range.Source.Begin, range.Source.End - range.Source.Begin);
            return range.Reasons == formatter::ProtectionReason::Disabled && source.find("clang_value") != source.npos;
        }));
}

TEST(FileCheck, DebugExplainsHunkScopeAndUsesOriginalCoordinates)
{
    const test::golden::Fixture fixture{
        std::filesystem::path(__FILE__).parent_path() / "testdata/file-check",
        "debug-hunks",
    };
    const Input input("void first(){work(a,b);}\nvoid second(){work(c,d);}\n");
    auto configurationResult = cli::PrepareConfiguration(Configuration());
    ASSERT_FALSE(configurationResult.Error) << configurationResult.Error;
    const auto& configuration = configurationResult.Value;
    const formatter::Hunk hunks[] = {{1, 1}};
    cli::ChangedFileScope scope{hunks, false};
    auto ordinaryResult = cli::FormatAndCheckFile(input.View(), configuration, {}, scope);
    ASSERT_FALSE(ordinaryResult.Error) << ordinaryResult.Error;
    const auto& ordinary = ordinaryResult.Value;
    auto checkedResult = cli::FormatAndCheckFile(
        input.View(),
        configuration,
        {},
        scope,
        cli::DiagnosticDetail::Detailed,
        true);
    ASSERT_FALSE(checkedResult.Error) << checkedResult.Error;
    const auto& checked = checkedResult.Value;
    EXPECT_EQ(checked.Formatting.Text, ordinary.Formatting.Text);
    EXPECT_NE(checked.Formatting.Text, input.View().View());
    EXPECT_EQ(checked.HasFailures(false), ordinary.HasFailures(false));
    EXPECT_EQ(checked.HasFailures(true), ordinary.HasFailures(true));
    std::ostringstream output;
    cli::PrintFileDebug(output, "sample.cpp", checked);
    fixture.Expect(output.str(), ".debug.txt");

    const Input clean("int value;\n");
    auto unrestrictedResult = cli::FormatAndCheckFile(
        clean.View(),
        configuration,
        {},
        std::nullopt,
        cli::DiagnosticDetail::Summary,
        true);
    ASSERT_FALSE(unrestrictedResult.Error) << unrestrictedResult.Error;
    const auto& unrestricted = unrestrictedResult.Value;
    EXPECT_FALSE(unrestricted.HasFailures(false));
    output.str("");
    cli::PrintFileDebug(output, "clean.cpp", unrestricted);
    fixture.Expect(output.str(), ".clean.debug.txt");
}

TEST(FileCheck, NamingRolesRemainFailuresAfterApplyingFormatting)
{
    const test::golden::Fixture fixture{
        std::filesystem::path(__FILE__).parent_path() / "testdata/file-check",
        "naming-roles",
    };
    auto raw = Configuration();
    raw.Naming.LocalVariable.Case = formatter::NameCase::CamelCase;
    raw.Naming.PrivateMemberVariable.Case = formatter::NameCase::PascalCase;
    raw.Naming.PrivateMemberVariable.Suffixes = std::vector<std::string>{"_"};
    raw.Naming.Enumerator.Case = formatter::NameCase::PascalCase;
    raw.NamingAcronyms = {"IO"};
    auto preparedResult = cli::PrepareConfiguration(std::move(raw));
    ASSERT_FALSE(preparedResult.Error) << preparedResult.Error;
    const auto& prepared = preparedResult.Value;
    const Input input(fixture.InputNamed(".input.cpp"));
    auto checkedResult = cli::FormatAndCheckFile(input.View(), prepared);
    ASSERT_FALSE(checkedResult.Error) << checkedResult.Error;
    const auto& checked = checkedResult.Value;
    EXPECT_TRUE(checked.FormattingMismatch);
    EXPECT_TRUE(checked.HasFailures(true));
    fixture.Expect(checked.Formatting.Text);
    const Input applied(checked.Formatting.Text);
    auto fixedResult = cli::FormatAndCheckFile(applied.View(), prepared);
    ASSERT_FALSE(fixedResult.Error) << fixedResult.Error;
    const auto& fixed = fixedResult.Value;
    EXPECT_FALSE(fixed.FormattingMismatch);
    EXPECT_TRUE(fixed.HasFailures(true));
    ASSERT_EQ(fixed.Semantic.size(), checked.Semantic.size());
    std::string diagnostics;

    for (us i = 0; i < fixed.Semantic.size(); ++i) {
        EXPECT_EQ(fixed.Semantic[i].Message, checked.Semantic[i].Message);
        diagnostics += fixed.Semantic[i].Message + "\n";
    }

    fixture.Expect(diagnostics, ".diagnostics.txt");
}

TEST(FileCheck, ExplanatoryNamespaceCommentsKeepMissingLabelDiagnostics)
{
    const test::golden::Fixture fixture{
        std::filesystem::path(__FILE__).parent_path() / "testdata/file-check",
        "namespace-comments",
    };
    const Input input(fixture.InputNamed(".input.cpp"));

    for (auto policy : {formatter::NamespaceComments::Fix, formatter::NamespaceComments::AddMissing}) {
        auto raw = Configuration();
        raw.FormattingStyle.NamespaceComments = policy;
        raw.FormattingStyle.RemoveNullDeclarations = true;
        auto preparedResult = cli::PrepareConfiguration(raw);
        ASSERT_FALSE(preparedResult.Error) << preparedResult.Error;
        const auto& prepared = preparedResult.Value;

        for (auto detail : {cli::DiagnosticDetail::Summary, cli::DiagnosticDetail::Detailed}) {
            auto checkedResult = cli::FormatAndCheckFile(
                input.View(),
                prepared,
                {},
                std::nullopt,
                detail,
                false,
                nullptr,
                true);
            ASSERT_FALSE(checkedResult.Error) << checkedResult.Error;
            const auto& checked = checkedResult.Value;
            ASSERT_EQ(checked.RequiredChanges.size(), 4u);
            EXPECT_TRUE(checked.HasFailures(false));
            EXPECT_TRUE(checked.HasFailures(true));
            const Input applied(checked.Formatting.Text);
            auto repeatedResult = cli::FormatAndCheckFile(applied.View(), prepared);
            ASSERT_FALSE(repeatedResult.Error) << repeatedResult.Error;
            const auto& repeated = repeatedResult.Value;
            EXPECT_EQ(repeated.Formatting.Text, checked.Formatting.Text);
            EXPECT_FALSE(repeated.FormattingMismatch);
            EXPECT_EQ(repeated.RequiredChanges.size(), 4u);
            std::ostringstream summary;
            cli::PrintFileSummaryIssues(summary, checked, true);
            fixture.Expect(summary.str(), ".summary.txt");

            {
                for (bool fix : {false, true}) {
                    std::ostringstream output;
                    cli::PrintFileIssues(output, "sample.cpp", input.View().View(), checked, fix);
                    fixture.Expect(output.str(), fix ? ".fix.txt" : ".check.txt");
                }
            }
        }

        const formatter::Hunk selected[] = {{1, 1}};
        auto scopedResult = cli::FormatAndCheckFile(input.View(), prepared, {}, cli::ChangedFileScope{selected, false});
        ASSERT_FALSE(scopedResult.Error) << scopedResult.Error;
        const auto& scoped = scopedResult.Value;
        ASSERT_EQ(scoped.RequiredChanges.size(), 1u);
        EXPECT_EQ(scoped.RequiredChanges.front().Original.Line, 1u);
        const std::span<const formatter::Hunk> none;
        auto unselectedResult = cli::FormatAndCheckFile(input.View(), prepared, {}, cli::ChangedFileScope{none, false});
        ASSERT_FALSE(unselectedResult.Error) << unselectedResult.Error;
        const auto& unselected = unselectedResult.Value;
        EXPECT_TRUE(unselected.RequiredChanges.empty());
        EXPECT_FALSE(unselected.HasFailures(false));

        raw.FormattingStyle.ShortNamespaceLines = 1;
        auto exemptResult = cli::FormatAndCheckFile(input.View(), cli::PrepareConfiguration(raw).Value);
        ASSERT_FALSE(exemptResult.Error) << exemptResult.Error;
        const auto& exempt = exemptResult.Value;
        EXPECT_TRUE(exempt.RequiredChanges.empty());
        EXPECT_FALSE(exempt.HasFailures(true));
    }
}

TEST(FileCheck, PartialSelectionNeverChangesCanonicalFormatting)
{
    auto raw = Configuration();
    raw.FormattingStyle.ColumnLimit = 40;
    raw.FormattingStyle.CallsLayout = formatter::CategoryLayout::Canonical;
    auto configurationResult = cli::PrepareConfiguration(raw);
    ASSERT_FALSE(configurationResult.Error) << configurationResult.Error;
    const auto& configuration = configurationResult.Value;

    for (std::string_view source :
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
        auto fullResult = cli::FormatAndCheckFile(input.View(), configuration);
        ASSERT_FALSE(fullResult.Error) << fullResult.Error;
        const auto& full = fullResult.Value;
        const Input canonical(full.Formatting.Text);
        ASSERT_FALSE(cli::FormatAndCheckFile(canonical.View(), configuration).Value.FormattingMismatch)
            << full.Formatting.Text;
        auto count = std::count(full.Formatting.Text.begin(), full.Formatting.Text.end(), '\n');

        for (std::string_view outside : {"", "void outside(){work(a,b);}\n"}) {
            const Input partial(full.Formatting.Text + std::string(outside));

            for (us line = 1; line <= count; ++line) {
                const formatter::Hunk selected[] = {{line, line}};

                for (bool spillover : {false, true}) {
                    auto checkedResult = cli::FormatAndCheckFile(
                        partial.View(),
                        configuration,
                        {},
                        cli::ChangedFileScope{selected, spillover});
                    ASSERT_FALSE(checkedResult.Error) << checkedResult.Error;
                    const auto& checked = checkedResult.Value;
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
    auto preparedResult = cli::PrepareConfiguration(raw);
    ASSERT_FALSE(preparedResult.Error) << preparedResult.Error;
    const auto& prepared = preparedResult.Value;
    const Input input("#include \"project/engine/widget.h\"\n\n" "#include \"project/engine/detail.h\"\n\n"
        "#include \"project/base.h\"\n");
    const formatter::Hunk selected[] = {{1, 5}};
    auto relative = std::filesystem::path("project/engine/widget.cpp");

    for (const auto& path : {relative, std::filesystem::absolute(relative)}) {
        auto name = path.generic_string();

        for (bool hunks : {false, true}) {
            auto checkedResult = cli::FormatAndCheckFile(
                input.View(),
                prepared,
                name,
                hunks ? std::optional(cli::ChangedFileScope{selected}) : std::nullopt);
            ASSERT_FALSE(checkedResult.Error) << checkedResult.Error;
            const auto& checked = checkedResult.Value;
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

    for (bool mixed : {false, true}) {
        for (bool indent : {false, true}) {
            formatter::Configuration raw;
            auto& style = raw.FormattingStyle;
            style.IncludeOrder = formatter::IncludeOrder::Sort;
            style.IncludeMatchingHeader = formatter::IncludeMatchingHeader::First;
            style.IncludeGrouping = formatter::IncludeGrouping::Directory;
            style.IncludeStandardHeaders = formatter::IncludeStandardHeaders::Last;
            style.IndentPreprocessingDirectives = indent
                ? formatter::BodyIndentation::Indent
                : formatter::BodyIndentation::Preserve;

            if (mixed) {
                style.SpaceInsideFunctionParameters = formatter::GapPolicy::NoSpace;
            }

            auto preparedResult = cli::PrepareConfiguration(std::move(raw));
            ASSERT_FALSE(preparedResult.Error) << preparedResult.Error;
            const auto& prepared = preparedResult.Value;
            auto checkedResult = cli::FormatAndCheckFile(
                input.View(),
                prepared,
                "project/sample.cpp",
                std::nullopt,
                cli::DiagnosticDetail::Summary);
            ASSERT_FALSE(checkedResult.Error) << checkedResult.Error;
            const auto& checked = checkedResult.Value;
            auto detailedResult = cli::FormatAndCheckFile(
                input.View(),
                prepared,
                "project/sample.cpp",
                std::nullopt,
                cli::DiagnosticDetail::Detailed);
            ASSERT_FALSE(detailedResult.Error) << detailedResult.Error;
            const auto& detailed = detailedResult.Value;

            if (!mixed && !indent) {
                std::ostringstream plain;
                EXPECT_FALSE(cli::PrintCheckResult(
                    plain,
                    "project/sample.cpp",
                    input.View().View(),
                    detailed,
                    cli::DiagnosticDetail::Summary,
                    false));
                fixture.Expect(plain.str(), ".summary-check.txt");
            }

            EXPECT_TRUE(checked.Formatting.Changes);
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
                auto before = input.View()
                    .View()
                    .substr(change.Original.Begin, change.Original.End - change.Original.Begin);
                auto after = std::string_view(
                    checked.Formatting.Text).substr(change.Output.Begin, change.Output.End - change.Output.Begin);
                EXPECT_TRUE(before.ends_with('\n'));
                EXPECT_TRUE(after.ends_with('\n'));
                EXPECT_EQ(before.find("int "), std::string_view::npos);
                EXPECT_EQ(after.find("int "), std::string_view::npos);
            }

            for (bool fix : {false, true}) {
                std::ostringstream output;
                cli::PrintFileIssues(output, "sample.cpp", input.View().View(), checked, fix);
                output << "summary: ";
                cli::PrintFileSummaryIssues(output, checked, fix);
                output << '\n';
                fixture.Expect(output.str(), fix ? ".fix.txt" : mixed ? ".mixed-check.txt" : ".check.txt");
                std::ostringstream formatting;
                EXPECT_FALSE(cli::PrintFormattingDiagnostics(
                    formatting,
                    "sample.cpp",
                    input.View().View(),
                    detailed.Formatting,
                    cli::Terminal{},
                    fix));

                if (mixed) {
                    fixture.Expect(formatting.str(), fix ? ".format-fix.txt" : ".format-check.txt");
                } else {
                    EXPECT_TRUE(formatting.str().empty());
                }
            }

            const Input applied(checked.Formatting.Text);
            auto cleanResult = cli::FormatAndCheckFile(applied.View(), prepared, "project/sample.cpp");
            ASSERT_FALSE(cleanResult.Error) << cleanResult.Error;
            const auto& clean = cleanResult.Value;
            EXPECT_FALSE(clean.FormattingMismatch);
            EXPECT_TRUE(clean.Formatting.Summary->IncludeOrderChanges.empty());
            const formatter::Hunk selected[] = {{1, 1}};
            auto strictResult = cli::FormatAndCheckFile(
                input.View(),
                prepared,
                "project/sample.cpp",
                cli::ChangedFileScope{selected, false});
            ASSERT_FALSE(strictResult.Error) << strictResult.Error;
            const auto& strict = strictResult.Value;
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
    auto configurationResult = cli::PrepareConfiguration(std::move(raw));
    ASSERT_FALSE(configurationResult.Error) << configurationResult.Error;
    const auto& configuration = configurationResult.Value;
    const Input input("define_case(Suite, Case) {auto sum=left+right; Use(sum);}\n");
    auto checkedResult = cli::FormatAndCheckFile(input.View(), configuration);
    ASSERT_FALSE(checkedResult.Error) << checkedResult.Error;
    const auto& checked = checkedResult.Value;
    EXPECT_TRUE(checked.FormattingMismatch);
    EXPECT_TRUE(checked.Semantic.empty());
    EXPECT_NE(checked.Formatting.Text.find("left + right"), std::string::npos);
    const Input applied(checked.Formatting.Text);
    EXPECT_FALSE(cli::FormatAndCheckFile(applied.View(), configuration).Value.FormattingMismatch);
}

TEST(FileCheck, FunctionBodyMacrosAfterHelpersSatisfyWidthLimit)
{
    formatter::Configuration raw;
    raw.FormattingStyle.Layout = formatter::LayoutMode::Canonical;
    raw.FormattingStyle.ColumnLimit = 40;
    raw.FormattingStyle.FunctionBodyMacroNames = {"TEST"};
    raw.FormattingStyle.WidthDiagnostics = formatter::WidthDiagnosticPolicy::Report;
    auto configurationResult = cli::PrepareConfiguration(std::move(raw));
    ASSERT_FALSE(configurationResult.Error) << configurationResult.Error;
    const auto& configuration = configurationResult.Value;
    const Input input("SUITE(Example) {\n" "    int Helper() { return 0; }\n" "    TEST(Case) {\n"
        "        Run(first_argument, second_argument, third_argument, fourth_argument);\n" "    }\n" "}\n");
    auto checkedResult = cli::FormatAndCheckFile(input.View(), configuration);
    ASSERT_FALSE(checkedResult.Error) << checkedResult.Error;
    const auto& checked = checkedResult.Value;
    EXPECT_TRUE(checked.FormattingMismatch);
    EXPECT_TRUE(checked.HasFailures(false));
    EXPECT_FALSE(checked.HasFailures(true));
    EXPECT_TRUE(checked.Formatting.Diagnostics.empty());
    const Input applied(checked.Formatting.Text);
    EXPECT_FALSE(cli::FormatAndCheckFile(applied.View(), configuration).Value.HasFailures(false));
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
    auto preparedResult = cli::PrepareConfiguration(std::move(configuration));
    ASSERT_FALSE(preparedResult.Error) << preparedResult.Error;
    const auto& prepared = preparedResult.Value;
    const Input input(fixture.InputNamed(".input.cpp"));
    auto checkedResult = cli::FormatAndCheckFile(
        input.View(),
        prepared,
        {},
        std::nullopt,
        cli::DiagnosticDetail::Detailed,
        false,
        nullptr,
        true);
    ASSERT_FALSE(checkedResult.Error) << checkedResult.Error;
    const auto& checked = checkedResult.Value;

    for (auto action :
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
    auto close = checked.Formatting.Text.find("// namespace test");
    ASSERT_NE(close, std::string::npos);
    EXPECT_EQ(
        width.Position.Line,
        1u + std::count(checked.Formatting.Text.begin(), checked.Formatting.Text.begin() + close, '\n'));

    for (auto terminalWidth : {us(80), us(32)}) {
        for (auto fix : {false, true}) {
            SCOPED_TRACE(fix ? "fix" : "check");
            SCOPED_TRACE(terminalWidth);
            const cli::Terminal terminal(false, terminalWidth);
            std::ostringstream output;
            cli::PrintFileIssues(output, "sample.cpp", input.View().View(), checked, fix, terminal);
            output << "summary: ";
            cli::PrintFileSummaryIssues(output, checked, fix);
            output << '\n';
            fixture.Expect(
                output.str(),
                terminalWidth == 80
                ? (fix ? ".fix.txt" : ".check.txt")
                : (fix ? ".fix-narrow.txt" : ".check-narrow.txt"));
            std::ostringstream formatting;
            EXPECT_FALSE(cli::PrintFormattingDiagnostics(
                formatting,
                "sample.cpp",
                input.View().View(),
                checked.Formatting,
                terminal,
                fix));
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
    std::ostringstream missingMetadata;
    auto error = cli::PrintFormattingDiagnostics(
        missingMetadata,
        "sample.cpp",
        {},
        formatter::FormatResult{},
        cli::Terminal(),
        false);
    EXPECT_EQ(std::get<std::string_view>(error.Detail), "formatting diagnostics require FormatRequest::Changes");
    EXPECT_TRUE(missingMetadata.str().empty());

    const test::golden::Fixture fixture{
        std::filesystem::path(__FILE__).parent_path() / "testdata/file-check",
        "terminal",
    };
    std::string_view source = "void Good() { invoke(first_argument,second_value,third_argument); }\n";
    formatter::Style style;
    style.CommaSpaceAfter = formatter::GapPolicy::Space;
    const Input input(source);
    auto formatted = formatter::Format(input.View(), formatter::PreparedFormat(style), {.Changes = true});

    std::ostringstream plain;
    EXPECT_FALSE(cli::PrintFormattingLocations(plain, "sample.cpp", source, formatted));
    fixture.Expect(plain.str(), ".summary.txt");

    for (auto width : {us(80), us(32)}) {
        for (bool fixed : {false, true}) {
            SCOPED_TRACE(width);
            SCOPED_TRACE(fixed);
            std::ostringstream output;
            EXPECT_FALSE(cli::PrintFormattingDiagnostics(
                output,
                "sample.cpp",
                source,
                formatted,
                cli::Terminal(false, width),
                fixed));
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
    auto preparedResult = cli::PrepareConfiguration(raw);
    ASSERT_FALSE(preparedResult.Error) << preparedResult.Error;
    const auto& prepared = preparedResult.Value;
    const std::string source = "const char* text=\"é\"; void bad_name(int  , "
        "void (*)(int)){}\nvoid GoodProto(int);\n";
    const Input input(source);
    auto checkedResult = cli::FormatAndCheckFile(
        input.View(),
        prepared,
        {},
        std::nullopt,
        cli::DiagnosticDetail::Summary,
        false,
        nullptr,
        true);
    ASSERT_FALSE(checkedResult.Error) << checkedResult.Error;
    const auto& checked = checkedResult.Value;
    auto summaryResult = cli::FormatAndCheckFile(
        input.View(),
        prepared,
        {},
        std::nullopt,
        cli::DiagnosticDetail::Summary,
        false,
        nullptr,
        true);
    ASSERT_FALSE(summaryResult.Error) << summaryResult.Error;
    const auto& summary = summaryResult.Value;
    EXPECT_EQ(summary.Semantic.size(), checked.Semantic.size());
    EXPECT_FALSE(summary.Formatting.OriginalTokens.empty());

    for (bool fix : {false, true}) {
        std::ostringstream fullOutput, summaryOutput;
        cli::PrintFileSummaryIssues(fullOutput, checked, fix);
        cli::PrintFileSummaryIssues(summaryOutput, summary, fix);
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
    auto emitted = checked.Formatting.Text.find("bad_name");
    auto lineStart = checked.Formatting.Text.rfind('\n', emitted);
    auto begin = lineStart == std::string::npos ? 0 : lineStart + 1;
    EXPECT_EQ(
        checked.Semantic[0].Emitted.Column,
        1u +
        std::count_if(
            checked.Formatting.Text.begin() + begin,
            checked.Formatting.Text.begin() + emitted,
            [] (u8 c) {
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
    auto recoveredResult = cli::FormatAndCheckFile(
        recovery.View(),
        prepared,
        {},
        std::nullopt,
        cli::DiagnosticDetail::Summary,
        false,
        nullptr,
        true);
    ASSERT_FALSE(recoveredResult.Error) << recoveredResult.Error;
    const auto& recovered = recoveredResult.Value;
    ASSERT_EQ(recovered.Semantic.size(), 1u);
    EXPECT_EQ(recovered.Semantic[0].Original.Column, malformed.find("another_bad") + 1);
    auto recoveredName = recovered.Formatting.Text.find("another_bad");
    auto recoveredStart = recovered.Formatting.Text.rfind('\n', recoveredName);
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
    auto preparedResult = cli::PrepareConfiguration(raw);
    ASSERT_FALSE(preparedResult.Error) << preparedResult.Error;
    const auto& prepared = preparedResult.Value;
    const Input input(fixture.InputNamed(".input.cpp"));
    auto checkedResult = cli::FormatAndCheckFile(
        input.View(),
        prepared,
        {},
        std::nullopt,
        cli::DiagnosticDetail::Detailed,
        false,
        nullptr,
        true);
    ASSERT_FALSE(checkedResult.Error) << checkedResult.Error;
    const auto& checked = checkedResult.Value;
    EXPECT_FALSE(checked.FormattingMismatch);
    ASSERT_EQ(checked.Formatting.Diagnostics.size(), 2u);
    EXPECT_EQ(checked.Formatting.Diagnostics[0].Position.Line, 2u);
    EXPECT_EQ(checked.Formatting.Diagnostics[1].Position.Line, 4u);
    EXPECT_EQ(checked.Formatting.Diagnostics[0].Original->Column, 13u);
    EXPECT_EQ(checked.Formatting.Diagnostics[1].Original->Column, 12u);
    const formatter::Hunk selected[] = {{2, 2}, {4, 4}};
    auto scopedResult = cli::FormatAndCheckFile(input.View(), prepared, {}, cli::ChangedFileScope{selected, false});
    ASSERT_FALSE(scopedResult.Error) << scopedResult.Error;
    const auto& scoped = scopedResult.Value;
    ASSERT_EQ(scoped.Formatting.Diagnostics.size(), checked.Formatting.Diagnostics.size());

    for (us i = 0; i < checked.Formatting.Diagnostics.size(); ++i) {
        EXPECT_EQ(scoped.Formatting.Diagnostics[i].Offset, checked.Formatting.Diagnostics[i].Offset);
        EXPECT_EQ(scoped.Formatting.Diagnostics[i].CodePointColumn, checked.Formatting.Diagnostics[i].CodePointColumn);
        EXPECT_EQ(
            scoped.Formatting.Diagnostics[i].Original->Column,
            checked.Formatting.Diagnostics[i].Original->Column);
    }

    auto summaryResult = cli::FormatAndCheckFile(
        input.View(),
        prepared,
        {},
        std::nullopt,
        cli::DiagnosticDetail::Summary,
        false,
        nullptr,
        true);
    ASSERT_FALSE(summaryResult.Error) << summaryResult.Error;
    const auto& summary = summaryResult.Value;
    ASSERT_EQ(summary.Formatting.Diagnostics.size(), checked.Formatting.Diagnostics.size());
    EXPECT_TRUE(summary.Formatting.OriginalTokens.empty());

    for (const auto& diagnostic : summary.Formatting.Diagnostics) {
        EXPECT_FALSE(diagnostic.Original);
    }

    EXPECT_EQ(summary.HasFailures(false), checked.HasFailures(false));
    EXPECT_EQ(summary.HasFailures(true), checked.HasFailures(true));

    for (bool fix : {false, true}) {
        std::ostringstream output;
        cli::PrintFileIssues(output, "unicode.cpp", input.View().View(), checked, fix);
        fixture.Expect(output.str(), fix ? ".fix.txt" : ".check.txt");
        std::ostringstream summaryOutput;
        EXPECT_FALSE(cli::PrintCheckResult(
            summaryOutput,
            "unicode.cpp",
            input.View().View(),
            fix ? summary : checked,
            cli::DiagnosticDetail::Summary,
            fix));
        fixture.Expect(summaryOutput.str(), fix ? ".summary-fix.txt" : ".summary-check.txt");
    }

    // A tab itself can contain the first column exceeding the limit.
    raw.FormattingStyle.ColumnLimit = 6;
    auto narrowResult = cli::PrepareConfiguration(raw);
    ASSERT_FALSE(narrowResult.Error) << narrowResult.Error;
    const auto& narrow = narrowResult.Value;
    const Input tab("// é界😀\t\n");
    auto tabbedResult = cli::FormatAndCheckFile(tab.View(), narrow);
    ASSERT_FALSE(tabbedResult.Error) << tabbedResult.Error;
    const auto& tabbed = tabbedResult.Value;
    ASSERT_EQ(tabbed.Formatting.Diagnostics.size(), 1u);
    EXPECT_EQ(tabbed.Formatting.Diagnostics[0].Original->Column, 7u);
    EXPECT_EQ(tabbed.Formatting.Diagnostics[0].Offset, tab.View().View().find('\t'));
    EXPECT_EQ(tabbed.Formatting.Diagnostics[0].CodePointColumn, 7u);
}

TEST(SourceExcerpt, ReusesCellsForUnicodeTabsAndCarriageReturns)
{
    Arena scratch;
    auto rendered = cli::RenderLine("\té界\rx\tz", 3, &scratch);
    const std::array<us, 9> carets{0, 0, 5, 6, 7, 7, 8, 13, 14};

    for (us column = 0; column < carets.size(); ++column) {
        EXPECT_EQ(rendered.Caret(column), carets[column]) << column;
    }
}

TEST(FileCheck, ActualProtectedOverflowRemainsFailureInFixMode)
{
    auto raw = Configuration();
    raw.FormattingStyle.ColumnLimit = 10;
    raw.FormattingStyle.WidthDiagnostics = formatter::WidthDiagnosticPolicy::Report;
    auto preparedResult = cli::PrepareConfiguration(raw);
    ASSERT_FALSE(preparedResult.Error) << preparedResult.Error;
    const auto& prepared = preparedResult.Value;
    const Input input("// hit-fmt off\r\nint " "unavoidable_long_name;\r\n// hit-fmt on\r\n");
    auto checkedResult = cli::FormatAndCheckFile(input.View(), prepared);
    ASSERT_FALSE(checkedResult.Error) << checkedResult.Error;
    const auto& checked = checkedResult.Value;
    EXPECT_EQ(checked.Formatting.Text, input.View().View());
    EXPECT_FALSE(checked.FormattingMismatch);
    EXPECT_TRUE(checked.HasFailures(false));
    EXPECT_TRUE(checked.HasFailures(true));
    ASSERT_GE(checked.Formatting.Diagnostics.size(), 2u);
    EXPECT_EQ(checked.Formatting.Diagnostics[1].Position.Line, 2u);
}

TEST(FileCheck, DiagnosticFlagsUseTerminalDefaultAndLastFlagWins)
{
    using D = cli::DiagnosticDetail;

    for (bool terminal : {false, true}) {
        SCOPED_TRACE(terminal);

        for (const auto& [args, detail] :
            Vector<std::pair<Vector<const char*>, D>>{
                {{"hit-fmt", "sample.cpp"}, terminal ? D::Detailed : D::Quick},
                {{"hit-fmt", "--check", "sample.cpp"}, terminal ? D::Detailed : D::Quick},
                {{"hit-fmt", "--fix", "sample.cpp"}, terminal ? D::Detailed : D::Summary},
                {{"hit-fmt", "--summary", "sample.cpp"}, D::Summary},
                {{"hit-fmt", "--quick", "sample.cpp"}, D::Quick},
                {{"hit-fmt", "--details", "sample.cpp"}, D::Detailed},
                {{"hit-fmt", "--details", "--summary", "sample.cpp"}, D::Summary},
                {{"hit-fmt", "--summary", "--quick", "sample.cpp"}, D::Quick},
                {{"hit-fmt", "--summary", "--details", "sample.cpp"}, D::Detailed},
                {{"hit-fmt", "--quick", "--fix", "sample.cpp"}, D::Quick},
                {{"hit-fmt", "--fix", "--summary", "sample.cpp"}, D::Summary},
                {{"hit-fmt", "--details", "--fix", "sample.cpp"}, D::Detailed},
            })
        {
            SCOPED_TRACE(::testing::PrintToString(args));
            auto parsed = cli::ParseArguments(args, std::pmr::get_default_resource(), terminal);
            ASSERT_FALSE(parsed.Error) << parsed.Error;
            EXPECT_EQ(parsed.Value.Diagnostics, detail);
            EXPECT_EQ(parsed.Value.Paths, std::vector<fs::Path>{"sample.cpp"});
        }
    }
}

TEST(FileCheck, ChangedHunkArgumentsSupportAliasesAndRejectMixedInputs)
{
    for (auto option : {"--changed-hunks", "--changed-ranges"}) {
        const char* argv[] = {"hit-fmt", "--fix", "--check", option, "-", "--no-spillover", "--threads=2", "--summary"};
        auto parsedResult = cli::ParseArguments(argv);
        ASSERT_FALSE(parsedResult.Error) << parsedResult.Error;
        const auto& parsed = parsedResult.Value;
        ASSERT_TRUE(parsed.HunkInput);
        EXPECT_EQ(*parsed.HunkInput, "-");
        EXPECT_TRUE(parsed.Paths.empty());
        EXPECT_FALSE(parsed.Spillover);
        EXPECT_TRUE(parsed.Fix);
        EXPECT_EQ(parsed.Diagnostics, cli::DiagnosticDetail::Summary);
        EXPECT_EQ(parsed.Threads, 2u);
    }

    for (auto option : {"--changed-hunks=ranges.txt", "--changed-ranges=ranges.txt"}) {
        const char* argv[] = {"hit-fmt", option};
        auto parsedResult = cli::ParseArguments(argv);
        ASSERT_FALSE(parsedResult.Error) << parsedResult.Error;
        const auto& parsed = parsedResult.Value;
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
            {"hit-fmt", "--summary=1"},
            {"hit-fmt", "--details=1"},
            {"hit-fmt", "--quick=1"},
            {"hit-fmt", "--plain"},
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
        Arena memory;
        auto invalid = cli::ParseArguments(argv, &memory);
        EXPECT_TRUE(invalid.Error);
    }

    const char* positional[] = {"hit-fmt", "--", "--changed-hunks", "--no-spillover"};
    auto parsedResult = cli::ParseArguments(positional);
    ASSERT_FALSE(parsedResult.Error) << parsedResult.Error;
    const auto& parsed = parsedResult.Value;
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
        auto parsedResult = cli::ParseArguments(args);
        ASSERT_FALSE(parsedResult.Error) << parsedResult.Error;
        const auto& parsed = parsedResult.Value;
        EXPECT_EQ(parsed.Threads, 3u);
        ASSERT_TRUE(parsed.ConfigPath);
        EXPECT_EQ(*parsed.ConfigPath, "style.hit-fmt");
        ASSERT_TRUE(parsed.Exclusions);
        EXPECT_TRUE(std::regex_search(std::filesystem::path("generated.cpp").native(), *parsed.Exclusions));
        EXPECT_FALSE(std::regex_search(std::filesystem::path("source.cpp").native(), *parsed.Exclusions));
        EXPECT_EQ(parsed.Paths, std::vector<fs::Path>{"source.cpp"});
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
        auto parsedResult = cli::ParseArguments(args);
        ASSERT_FALSE(parsedResult.Error) << parsedResult.Error;
        const auto& parsed = parsedResult.Value;
        EXPECT_GT(parsed.Threads, 0u);
        EXPECT_EQ(parsed.Diagnostics, cli::DiagnosticDetail::Summary);
        EXPECT_EQ(parsed.Paths, std::vector<fs::Path>{"source.cpp"});
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
        cli::Error error;
        {
            Arena memory(std::pmr::null_memory_resource());
            auto parsed = cli::ParseArguments(args, &memory);
            ASSERT_TRUE(parsed.Error);
            error = std::move(parsed.Error);
        }

        output << "plain:\n";
        cli::PrintError(output, error, cli::Terminal());
        output << "colored:\n";
        cli::PrintError(output, error, cli::Terminal(true));
    }

    test::golden::ExpectGolden(std::filesystem::path(HIT_FMT_TESTDATA_DIR) / "usage_error.out", output.str());
}

TEST(FileCheck, GenerateConfigArgumentsSelectOptionalPresets)
{
    using cli::ConfigPreset;
    const char* defaultArgs[] = {"hit-fmt", "--generate-config", "--summary"};
    auto defaultsResult = cli::ParseArguments(defaultArgs);
    ASSERT_FALSE(defaultsResult.Error) << defaultsResult.Error;
    const auto& defaults = defaultsResult.Value;
    EXPECT_EQ(defaults.GenerateConfig, ConfigPreset::Default);
    EXPECT_EQ(defaults.Diagnostics, cli::DiagnosticDetail::Summary);

    for (const auto& [name, preset] :
        {
            std::pair{"ytsaurus", ConfigPreset::YTsaurus},
            std::pair{"google", ConfigPreset::Google},
            std::pair{"llvm", ConfigPreset::LLVM},
        })
    {
        SCOPED_TRACE(name);
        const char* separated[] = {"hit-fmt", "--generate-config", name};
        auto parsedResult = cli::ParseArguments(separated);
        ASSERT_FALSE(parsedResult.Error) << parsedResult.Error;
        const auto& parsed = parsedResult.Value;
        EXPECT_EQ(parsed.GenerateConfig, preset);
        EXPECT_TRUE(parsed.Paths.empty());
        auto option = "--generate-config=" + std::string(name);
        const char* attached[] = {"hit-fmt", option.c_str()};
        auto attachedArguments = cli::ParseArguments(attached);
        ASSERT_FALSE(attachedArguments.Error) << attachedArguments.Error;
        EXPECT_EQ(attachedArguments.Value.GenerateConfig, preset);
    }

    for (const auto& args :
        std::vector<std::vector<const char*>>{
            {"hit-fmt", "--generate-config", "missing"},
            {"hit-fmt", "--generate-config="},
            {"hit-fmt", "--generate-config", "google", "source.cpp"},
            {"hit-fmt", "source.cpp", "--generate-config"},
        })
    {
        EXPECT_TRUE(cli::ParseArguments(args).Error);
    }
}

TEST(FileCheck, EmptyHunkScopeNeverFallsBackToWholeFile)
{
    auto raw = Configuration();
    raw.Naming.Function.Case = formatter::NameCase::PascalCase;
    raw.FunctionDeclarationParameterNames = formatter::FunctionDeclarationParameterNames::Required;
    raw.FormattingStyle.ColumnLimit = 10;
    raw.FormattingStyle.WidthDiagnostics = formatter::WidthDiagnosticPolicy::Report;
    auto preparedResult = cli::PrepareConfiguration(raw);
    ASSERT_FALSE(preparedResult.Error) << preparedResult.Error;
    const auto& prepared = preparedResult.Value;
    const Input input("void bad_name(int);\n");
    auto emptyResult = cli::FormatAndCheckFile(input.View(), prepared, {}, cli::ChangedFileScope{});
    ASSERT_FALSE(emptyResult.Error) << emptyResult.Error;
    const auto& empty = emptyResult.Value;
    EXPECT_EQ(empty.Formatting.Text, input.View().View());
    EXPECT_FALSE(empty.HasFailures(false));
    EXPECT_FALSE(empty.HasFailures(true));
    EXPECT_TRUE(empty.Incomplete.empty());
    EXPECT_TRUE(cli::FormatAndCheckFile(input.View(), prepared).Value.HasFailures(true));
    const formatter::Hunk invalid[] = {{2, 2}};
    Arena memory;
    auto invalidScope = cli::FormatAndCheckFile(
        input.View(),
        prepared,
        {},
        cli::ChangedFileScope{invalid},
        cli::DiagnosticDetail::Summary,
        false,
        &memory);
    EXPECT_TRUE(invalidScope.Error);
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
    auto preparedResult = cli::PrepareConfiguration(raw);
    ASSERT_FALSE(preparedResult.Error) << preparedResult.Error;
    const auto& prepared = preparedResult.Value;
    const Input input(fixture.InputNamed(".input.cpp"));
    const formatter::Hunk selected[] = {{13, 13}, {11, 11}, {9, 9}, {4, 5}, {1, 1}, {5, 5}};

    for (bool spillover : {false, true}) {
        auto checkedResult = cli::FormatAndCheckFile(
            input.View(),
            prepared,
            "sample.cpp",
            cli::ChangedFileScope{selected, spillover},
            cli::DiagnosticDetail::Detailed,
            false,
            nullptr,
            true);
        ASSERT_FALSE(checkedResult.Error) << checkedResult.Error;
        const auto& checked = checkedResult.Value;
        auto summaryResult = cli::FormatAndCheckFile(
            input.View(),
            prepared,
            "sample.cpp",
            cli::ChangedFileScope{selected, spillover},
            cli::DiagnosticDetail::Summary);
        ASSERT_FALSE(summaryResult.Error) << summaryResult.Error;
        const auto& summary = summaryResult.Value;
        ASSERT_EQ(summary.Formatting.Diagnostics.size(), checked.Formatting.Diagnostics.size());

        for (us i = 0; i < checked.Formatting.Diagnostics.size(); ++i) {
            ASSERT_TRUE(summary.Formatting.Diagnostics[i].Original);
            EXPECT_EQ(
                summary.Formatting.Diagnostics[i].Original->Line,
                checked.Formatting.Diagnostics[i].Original->Line);
        }

        for (bool fix : {false, true}) {
            std::ostringstream fullOutput, summaryOutput;
            cli::PrintFileSummaryIssues(fullOutput, checked, fix);
            cli::PrintFileSummaryIssues(summaryOutput, summary, fix);
            EXPECT_EQ(summaryOutput.str(), fullOutput.str());
        }

        ASSERT_EQ(checked.Semantic.size(), 4u);

        for (const auto& diagnostic : checked.Semantic) {
            EXPECT_NE(diagnostic.Original.Line, 3u);
            EXPECT_NE(diagnostic.Original.Line, 6u);
        }

        ASSERT_FALSE(checked.Formatting.Diagnostics.empty());

        for (const auto& diagnostic : checked.Formatting.Diagnostics) {
            auto position = *diagnostic.Original;
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

        for (bool fix : {false, true}) {
            std::ostringstream output;
            cli::PrintFileIssues(output, "sample.cpp", input.View().View(), checked, fix);
            output << "summary: ";
            cli::PrintFileSummaryIssues(output, checked, fix);
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
    auto preparedResult = cli::PrepareConfiguration(raw);
    ASSERT_FALSE(preparedResult.Error) << preparedResult.Error;
    const auto& prepared = preparedResult.Value;
    const Input input("void Good() {\n    call(1,\n        2);\n}\n");
    const formatter::Hunk selected[] = {{3, 3}};
    auto checkedResult = cli::FormatAndCheckFile(input.View(), prepared, {}, cli::ChangedFileScope{selected, false});
    ASSERT_FALSE(checkedResult.Error) << checkedResult.Error;
    const auto& checked = checkedResult.Value;
    EXPECT_FALSE(checked.FormattingMismatch);
    EXPECT_FALSE(checked.HasFailures(false));
    EXPECT_FALSE(checked.HasFailures(true));
    EXPECT_FALSE(checked.Incomplete.empty());

    for (bool fix : {false, true}) {
        std::ostringstream output, summary;
        cli::PrintFileIssues(output, "sample.cpp", input.View().View(), checked, fix);
        cli::PrintFileSummaryIssues(summary, checked, fix);
        EXPECT_TRUE(output.str().empty());
        EXPECT_TRUE(summary.str().empty());
    }

    raw.FormattingStyle.AddMissingControlBraces = true;
    auto bracesResult = cli::PrepareConfiguration(raw);
    ASSERT_FALSE(bracesResult.Error) << bracesResult.Error;
    const auto& braces = bracesResult.Value;
    const Input blocked("void Good(bool ready) {\n  if (ready)\n" "    consume(\n      ready);\n}\n");
    auto requiredResult = cli::FormatAndCheckFile(blocked.View(), braces, {}, cli::ChangedFileScope{selected, false});
    ASSERT_FALSE(requiredResult.Error) << requiredResult.Error;
    const auto& required = requiredResult.Value;
    ASSERT_EQ(required.RequiredChanges.size(), 1u);
    EXPECT_TRUE(required.Semantic.empty());
    EXPECT_TRUE(required.Formatting.Diagnostics.empty());
    EXPECT_TRUE(required.HasFailures(false));
    EXPECT_TRUE(required.HasFailures(true));

    for (bool fix : {false, true}) {
        std::ostringstream output, summary;
        cli::PrintFileIssues(output, "sample.cpp", blocked.View().View(), required, fix);
        cli::PrintFileSummaryIssues(summary, required, fix);

        for (const auto& text : {output.str(), summary.str()}) {
            EXPECT_NE(text.find("fail: control body requires braces"), std::string::npos);
            EXPECT_EQ(text.find("cannot automatically fix") != std::string::npos, fix);
            EXPECT_EQ(text.find("note:"), std::string::npos);
        }
    }
}

TEST(FileCheck, ParserLimitationsDoNotFailStyleChecks)
{
    auto preparedResult = cli::PrepareConfiguration(Configuration());
    ASSERT_FALSE(preparedResult.Error) << preparedResult.Error;
    const auto& prepared = preparedResult.Value;

    for (bool styleIssue : {false, true}) {
        const Input input(
            std::string(styleIssue ? "void Extra(){Call(left,right);}\n" : "") + "void Open() {\n    Work();\n");

        for (auto detail :
            {cli::DiagnosticDetail::Quick, cli::DiagnosticDetail::Summary, cli::DiagnosticDetail::Detailed})
        {
            for (bool debug : {false, true}) {
                auto checkedResult = cli::FormatAndCheckFile(input.View(), prepared, {}, std::nullopt, detail, debug);
                ASSERT_FALSE(checkedResult.Error) << checkedResult.Error;
                const auto& checked = checkedResult.Value;
                EXPECT_EQ(checked.FormattingMismatch, styleIssue);
                EXPECT_EQ(checked.HasFailures(false), styleIssue);
                EXPECT_FALSE(checked.HasFailures(true));
                EXPECT_TRUE(checked.RequiredChanges.empty());
                EXPECT_EQ(
                    std::ranges::any_of(
                        checked.Debug,
                        [] (const auto& diagnostic) {
                            return diagnostic.Message.find("formatting checking is incomplete") != std::string::npos;
                        }),
                    debug);

                for (bool fix : {false, true}) {
                    std::ostringstream output, summary;
                    cli::PrintFileIssues(output, "sample.cpp", input.View().View(), checked, fix);
                    cli::PrintFileSummaryIssues(summary, checked, fix);
                    EXPECT_TRUE(output.str().empty());
                    EXPECT_EQ(summary.str(), styleIssue && !fix ? "formatting mismatch" : "");
                }
            }
        }
    }
}

TEST(FileCheck, UnsupportedPreparationFailsClosed)
{
    auto raw = Configuration();
    raw.FormattingStyle.CallLikeMacroNames = {"CALL_HINT"};
    EXPECT_FALSE(cli::PrepareConfiguration(raw).Error);
    raw.FormattingStyle.CallLikeMacroNames.clear();
    raw.FormattingStyle.AmbiguousTemplateDeclarationSpacing = formatter::GapPolicy::Space;
    auto invalid = cli::PrepareConfiguration(raw);
    EXPECT_TRUE(invalid.Error);
}

TEST(FileCheck, SharedConditionalBodiesAreCheckedWhenCompatible)
{
    auto settings = Configuration();
    settings.FormattingStyle.FunctionBodyMacroNames = {"UTEST"};
    auto configurationResult = cli::PrepareConfiguration(std::move(settings));
    ASSERT_FALSE(configurationResult.Error) << configurationResult.Error;
    const auto& configuration = configurationResult.Value;
    std::ostringstream diagnostics;

    for (auto header : {"UTEST(Poll, Read)", "void Read()", "struct Read"}) {
        const Input input(std::string("#if DISABLED\n") + header + " {\n#else\n" + header +
            " {\n#endif\n    auto value=ReadValue();\n    EXPECT_EQ(value,1);\n}\n");
        auto checkedResult = cli::FormatAndCheckFile(input.View(), configuration);
        ASSERT_FALSE(checkedResult.Error) << checkedResult.Error;
        const auto& checked = checkedResult.Value;
        bool compatible = std::string_view(header).starts_with("void");
        EXPECT_EQ(checked.HasFailures(false), compatible);
        EXPECT_FALSE(checked.HasFailures(true));
        EXPECT_TRUE(checked.RequiredChanges.empty());

        for (bool fix : {false, true}) {
            std::ostringstream summary, details;
            cli::PrintFileSummaryIssues(summary, checked, fix);
            cli::PrintFileIssues(details, "sample.cpp", input.View().View(), checked, fix);
            EXPECT_EQ(summary.str().find("cannot be checked safely"), std::string::npos);
            EXPECT_EQ(details.str().find("cannot be checked safely"), std::string::npos);
            diagnostics << header << (fix ? " fix\n" : " check\n") << details.str() << "summary:"
                << (summary.str().empty() ? "" : " ") << summary.str() << '\n';
        }
    }

    const test::golden::Fixture fixture{HIT_FMT_TESTDATA_DIR, "shared_conditional_body"};
    fixture.Expect(diagnostics.str(), ".diagnostics.txt");
    const Input wrapped("NAMESPACE_BEGIN\n#if FIRST\nUTEST(Poll, First) {\n"
        "#else\nUTEST(Poll, Second) {\n#endif\n    Work();\n}\nNAMESPACE_END\n");
    auto recoveredResult = cli::FormatAndCheckFile(wrapped.View(), configuration);
    ASSERT_FALSE(recoveredResult.Error) << recoveredResult.Error;
    const auto& recovered = recoveredResult.Value;
    EXPECT_FALSE(recovered.HasFailures(false));
    EXPECT_FALSE(recovered.HasFailures(true));
    EXPECT_TRUE(recovered.RequiredChanges.empty());
    auto debuggedResult = cli::FormatAndCheckFile(
        wrapped.View(),
        configuration,
        {},
        std::nullopt,
        cli::DiagnosticDetail::Detailed,
        true);
    ASSERT_FALSE(debuggedResult.Error) << debuggedResult.Error;
    const auto& debugged = debuggedResult.Value;
    EXPECT_EQ(debugged.Formatting.Text, recovered.Formatting.Text);
    EXPECT_FALSE(debugged.HasFailures(false));
    EXPECT_FALSE(debugged.HasFailures(true));
    EXPECT_TRUE(debugged.RequiredChanges.empty());
    EXPECT_TRUE(std::ranges::any_of(
        debugged.Debug,
        [] (const auto& diagnostic) {
            return diagnostic.Message.find("cannot be checked safely") != std::string::npos;
        }));
    const Input complete("#if FIRST\nvoid First() { Work(); }\n#else\nvoid Second() { Other(); }\n#endif\n");
    EXPECT_TRUE(cli::FormatAndCheckFile(complete.View(), configuration).Value.RequiredChanges.empty());
    const formatter::Hunk outside[] = {{1, 1}};
    const Input shared("#if FIRST\nvoid First() {\n#else\nvoid Second() {\n#endif\n    Work();\n}\n");
    auto scopedResult = cli::FormatAndCheckFile(
        shared.View(),
        configuration,
        {},
        cli::ChangedFileScope{outside, false});
    ASSERT_FALSE(scopedResult.Error) << scopedResult.Error;
    const auto& scoped = scopedResult.Value;
    EXPECT_TRUE(scoped.RequiredChanges.empty());
}

} // namespace

TEST(PathRules, CombinedRulesMatchNativePathsAndRejectBackreferences)
{
    Arena scratch;
    const std::vector<std::string> empty;
    auto noRules = cli::CompilePathRules(empty, &scratch);
    ASSERT_FALSE(noRules.Error);
    EXPECT_FALSE(noRules.Value);
    const std::vector<std::string> patterns{"(^|/)generated(/|$)", R"(\.tmp$)"};
    auto rules = cli::CompilePathRules(patterns, &scratch);
    scratch.release();
    ASSERT_FALSE(rules.Error) << rules.Error;
    ASSERT_TRUE(rules.Value);
    auto matches = [&] (const std::filesystem::path& path) {
        return std::regex_search(path.native(), *rules.Value);
    };
    EXPECT_TRUE(matches(std::filesystem::path("src") / "generated" / "file.cpp"));
    EXPECT_TRUE(matches(std::filesystem::path("src") / "file.tmp"));
    EXPECT_FALSE(matches(std::filesystem::path("src") / "generated-file.cpp"));
    EXPECT_FALSE(matches(std::filesystem::path("src") / "file.cpp"));
    EXPECT_TRUE(cli::CompilePathRules(std::vector<std::string>{R"((.)\1)"}, &scratch).Error);
    EXPECT_TRUE(cli::CompilePathRules(std::vector<std::string>{"["}, &scratch).Error);
    auto invalidPattern = cli::CompilePathRules(std::vector<std::string>{"*"}, &scratch);
    EXPECT_TRUE(invalidPattern.Error);
    formatter::Configuration configuration;
    configuration.Exclude = {"(^|/)generated(/|$)", R"(\.tmp$)"};
    auto preparedResult = cli::PrepareConfiguration(std::move(configuration));
    ASSERT_FALSE(preparedResult.Error) << preparedResult.Error;
    const auto& prepared = preparedResult.Value;
    ASSERT_TRUE(prepared.Exclusions);
    EXPECT_TRUE(std::regex_search(std::filesystem::path("generated/file.cpp").native(), *prepared.Exclusions));
    formatter::Configuration invalid;
    invalid.Exclude = {"["};
    auto invalidConfig = cli::PrepareConfiguration(std::move(invalid));
    EXPECT_TRUE(invalidConfig.Error);

    const char* args[] = {"hit-fmt", "--exclude", "generated", "--exclude", R"(\.tmp$)"};
    auto argumentsResult = cli::ParseArguments(args);
    ASSERT_FALSE(argumentsResult.Error) << argumentsResult.Error;
    const auto& arguments = argumentsResult.Value;
    ASSERT_TRUE(arguments.Exclusions);
    EXPECT_TRUE(std::regex_search(std::filesystem::path("generated/file.cpp").native(), *arguments.Exclusions));
    EXPECT_TRUE(std::regex_search(std::filesystem::path("file.tmp").native(), *arguments.Exclusions));
}
