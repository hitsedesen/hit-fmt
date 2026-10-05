#include <formatter/config.hpp>
#include <formatter/syntax_layout.hpp>

#include <gtest/gtest.h>

#include <parser/parser.hpp>

#include <tests/golden.hpp>

#include <algorithm>
#include <array>
#include <memory_resource>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
namespace {

struct Output
{
    std::string Text;
    std::vector<document::Mapping> Map;
    std::vector<uint8_t> Broken;
};
Output Lower(
    std::string input,
    const formatter::Style& style,
    const formatter::SyntaxLayoutOptions& options = {},
    const formatter::SourceAnalysisOptions& protection = {},
    std::pmr::memory_resource* fileUpstream = std::pmr::get_default_resource(),
    std::pmr::memory_resource* scratchUpstream =
    std::pmr::get_default_resource())
{
    const auto size = input.size();
    input.append(util::SourcePaddingSize, '\0');
    auto tree = parser::Parse({input.data(), size}, [] (parser::ParseError) {});
    std::pmr::monotonic_buffer_resource file(fileUpstream);
    std::pmr::monotonic_buffer_resource scratch(scratchUpstream);
    const auto policies = formatter::CompilePolicies(style);
    const auto annotations = formatter::CompileLibraryAnnotations(style);
    auto safety =
        formatter::BuildSourceAnalysis(tree, protection, file, scratch);
    auto spacing =
        formatter::BuildSyntaxSpacing(tree, safety, policies, file, scratch);
    document::Document document(tree.Source(), &file);
    const auto status = formatter::BuildSyntaxDocument(
        tree,
        policies,
        annotations,
        safety,
        spacing,
        document,
        scratch,
        options);
    EXPECT_TRUE(status) << "lowering error " << int(status.Code) << " token "
        << status.Token << " core " << int(status.Core.Code);
    if (!status) {
        throw std::runtime_error("failed syntax lowering");
    }
    scratch.release(); // The document must not retain planner storage.
    const auto rendered = document
        .Render(formatter::SyntaxRenderPolicy(policies, options), &file);
    EXPECT_TRUE(rendered.State) << "core " << int(rendered.State.Code);
    if (!rendered.State) {
        throw std::runtime_error("failed rendering");
    }
    std::vector<bool> seen(tree.TokenCount());
    for (const auto& map : rendered.Map) {
        if (map.Source.Identity == document::NoIdentity) {
            continue;
        }
        EXPECT_LT(map.Source.Identity, tree.TokenCount());
        const auto id = parser::TokenIndex(map.Source.Identity);
        EXPECT_FALSE(seen[id]);
        seen[id] = true;
        EXPECT_TRUE(map.Exact);
        EXPECT_EQ(
            std::string_view(rendered.Text)
                .substr(map.OutputBegin, map.OutputEnd - map.OutputBegin),
            tree.TokenText(id));
    }
    for (parser::TokenIndex i = 0; i < tree.TokenCount(); ++i) {
        if (!seen[i]) {
            EXPECT_EQ(tree.Kind(i), tokenizer::TokenKind::Operator_Comma);
            EXPECT_NE(
                style.InitializersTrailingComma,
                formatter::TrailingCommaPolicy::Preserve);
        }
    }
    return {
        std::string(rendered.Text),
        {rendered.Map.begin(), rendered.Map.end()},
        {rendered.Broken.begin(), rendered.Broken.end()},
    };
}
void ExpectSplitGapMap(
    const Output& output,
    size_t sourceBegin,
    size_t indentBegin,
    size_t sourceEnd,
    std::string_view prefix,
    std::string_view indent)
{
    bool found = false;
    for (size_t i = 0; i + 1 < output.Map.size(); ++i) {
        const auto& generated = output.Map[i];
        const auto& borrowed = output.Map[i + 1];
        if (generated.Source.Identity != document::NoIdentity ||
            generated.Exact || generated.Source.Begin != sourceBegin ||
            generated.Source.End != sourceEnd)
        {
            continue;
        }
        EXPECT_EQ(
            std::string_view(output.Text).substr(
                generated.OutputBegin,
                generated.OutputEnd - generated.OutputBegin),
            prefix);
        EXPECT_TRUE(borrowed.Exact);
        EXPECT_EQ(borrowed.Source.Identity, document::NoIdentity);
        EXPECT_EQ(borrowed.Source.Begin, indentBegin);
        EXPECT_EQ(borrowed.Source.End, sourceEnd);
        EXPECT_EQ(generated.OutputEnd, borrowed.OutputBegin);
        EXPECT_EQ(
            std::string_view(output.Text).substr(
                borrowed.OutputBegin,
                borrowed.OutputEnd - borrowed.OutputBegin),
            indent);
        found = true;
    }
    EXPECT_TRUE(found) << "missing generated-prefix / exact-indent association";
}
formatter::Style Canonical(size_t width = 40)
{
    formatter::Style style;
    style.Layout = formatter::LayoutMode::Canonical;
    style.ColumnLimit = width;
    style.IndentWidth = 2;
    style.IndentationKind = formatter::IndentationKind::Spaces;
    style.BinaryOperatorSpacing = formatter::GapPolicy::GeneratedSpace;
    style.AssignmentOperatorSpacing = formatter::GapPolicy::GeneratedSpace;
    style.TernaryOperatorSpacing = formatter::GapPolicy::GeneratedSpace;
    style.MemberAccessOperatorsSpacing = formatter::GapPolicy::GeneratedNoSpace;
    style.FunctionCallSpaceBeforeParameters =
        formatter::GapPolicy::GeneratedNoSpace;
    style.FunctionSpaceBeforeParameters =
        formatter::GapPolicy::GeneratedNoSpace;
    style.StatementSemicolonSpacing = formatter::GapPolicy::GeneratedNoSpace;
    style.DeclarationSemicolonSpacing = formatter::GapPolicy::GeneratedNoSpace;
    return style;
}
TEST(SyntaxLayout, ControlSeparatorsWideFiniteRenderingOwnsOnlyDirectBoundaries)
{
    auto style = Canonical(10000);
    style.BodyShape = formatter::BodyShape::CompactIfFits;
    style.ControlBlockKeywordSpacing = formatter::GapPolicy::Space;
    const std::string source =
        "void f(){if(a)if(b)left();else right();else last();"
            "do tick();while(a);while(b)other();}\n";
    const auto output = Lower(source, style);
    EXPECT_NE(
        output.Text.find("left(); else right(); else last();"),
        std::string::npos);
    EXPECT_NE(
        output.Text.find("tick(); while (a); while (b)"),
        std::string::npos);
    EXPECT_EQ(Lower(output.Text, style).Text, output.Text);
}

TEST(SyntaxLayout, PartialIndentationRetainsFixedTopologyAndSplitMaps)
{
    const std::string input = "void f() {\n      work();\n}\n";
    const auto begin = parser::SourceOffset(input.find("      work"));
    const auto end = parser::SourceOffset(input.find("work"));
    const parser::SourceRange ranges[] = {{begin, end}};
    formatter::SourceAnalysisOptions scope;
    scope.EditScope = ranges;
    auto style = Canonical(80);
    for (const auto shape :
        {formatter::BodyShape::Expanded, formatter::BodyShape::CompactIfFits})
    {
        style.BodyShape = shape;
        const auto output = Lower(input, style, {}, scope);
        EXPECT_EQ(output.Text, "void f() {\n  work();\n}\n");
        bool exactPrefix = false;
        bool generatedIndent = false;
        for (const auto& map : output.Map) {
            if (map.Source.Identity != document::NoIdentity) {
                continue;
            }
            if (map.Source.Begin == begin - 1 && map.Source.End == begin) {
                EXPECT_TRUE(map.Exact);
                EXPECT_TRUE(map.Protected);
                EXPECT_EQ(
                    std::string_view(output.Text).substr(
                        map.OutputBegin,
                        map.OutputEnd - map.OutputBegin),
                    "\n");
                exactPrefix = true;
            }
            if (map.Source.Begin == begin && map.Source.End == end) {
                EXPECT_FALSE(map.Exact);
                EXPECT_FALSE(map.Protected);
                EXPECT_EQ(
                    std::string_view(output.Text).substr(
                        map.OutputBegin,
                        map.OutputEnd - map.OutputBegin),
                    "  ");
                generatedIndent = true;
            }
        }
        EXPECT_TRUE(exactPrefix);
        EXPECT_TRUE(generatedIndent);
    }
}

TEST(SyntaxLayout, PartialCRLFIndentationMapsBorrowedTerminatorExactly)
{
    const std::string input = "void f() {\r\n      work();\r\n}\r\n";
    const auto begin = parser::SourceOffset(input.find("      work"));
    const parser::SourceRange ranges[] = {{begin, begin + 6}};
    formatter::SourceAnalysisOptions scope;
    scope.EditScope = ranges;
    const auto output = Lower(input, Canonical(80), {}, scope);
    EXPECT_EQ(output.Text, "void f() {\r\n  work();\r\n}\r\n");
    bool split = false;
    for (size_t i = 0; i + 1 < output.Map.size(); ++i) {
        const auto& prefix = output.Map[i];
        const auto& indent = output.Map[i + 1];
        if (prefix.Source.Begin == begin - 2 && prefix.Source.End == begin) {
            EXPECT_TRUE(prefix.Exact);
            EXPECT_TRUE(prefix.Protected);
            EXPECT_EQ(
                std::string_view(output.Text).substr(
                    prefix.OutputBegin,
                    prefix.OutputEnd - prefix.OutputBegin),
                "\r\n");
            EXPECT_FALSE(indent.Exact);
            EXPECT_FALSE(indent.Protected);
            EXPECT_EQ(indent.Source.Identity, document::NoIdentity);
            EXPECT_EQ(indent.Source.Begin, begin);
            EXPECT_EQ(indent.Source.End, begin + 6);
            EXPECT_EQ(prefix.OutputEnd, indent.OutputBegin);
            split = true;
        }
    }
    EXPECT_TRUE(split);
}

TEST(SyntaxLayout, PartialTrimDoesNotRewriteUnselectedBlankSuffix)
{
    const std::string input = "void f() {\n  work();  \r\n \n\t \r\n}\n";
    const auto begin = parser::SourceOffset(input.find("  \r\n"));
    const parser::SourceRange ranges[] = {{begin, begin + 2}};
    formatter::SourceAnalysisOptions scope;
    scope.EditScope = ranges;
    auto style = Canonical(80);
    style.TrailingWhitespace = formatter::TrailingWhitespace::Trim;
    const auto output = Lower(input, style, {}, scope);
    auto expected = input;
    expected.erase(begin, 2);
    EXPECT_EQ(output.Text, expected);
    for (const auto& map : output.Map) {
        EXPECT_TRUE(map.Exact);
        EXPECT_FALSE(map.Source.Begin < begin + 2 && map.Source.End > begin);
    }
    style.TrailingWhitespace = formatter::TrailingWhitespace::Preserve;
    EXPECT_EQ(Lower(input, style, {}, scope).Text, input);
}

TEST(SyntaxLayout, PartialGapPoliciesAndAnchorsGolden)
{
    std::string records;
    const auto record =
        [&] (
            std::string_view name,
            const std::string& input,
            const formatter::Style& style,
            std::span<const parser::SourceRange> ranges,
            const formatter::SyntaxLayoutOptions& options = {}) {
                formatter::SourceAnalysisOptions scope;
                scope.EditScope = ranges;
                records += "=== " + std::string(name) + " ===\n";
                records += Lower(input, style, options, scope).Text;
            };
    auto style = Canonical(80);
    const std::string line = "void f() {\n      work();\n}\n";
    const auto begin = parser::SourceOffset(line.find("      work"));
    const auto end = parser::SourceOffset(line.find("work"));
    const parser::SourceRange indent[] = {{begin, end}};
    record("expanded", line, style, indent);
    style.BodyShape = formatter::BodyShape::CompactIfFits;
    record("compact fixed topology", line, style, indent);
    style.IndentationKind = formatter::IndentationKind::Preserve;
    record("indent preserve canonical", line, style, indent);
    style.BodiesLayout = formatter::CategoryLayout::Preserve;
    record("indent preserve body preserve", line, style, indent);
    style.IndentationKind = formatter::IndentationKind::Spaces;
    record("body preserve explicit indentation", line, style, indent);
    record("all preserve", line, {}, indent);
    const std::string mixed = "void f() {  \r\n \n\t \r\n      work();\n}\n";
    const auto first = parser::SourceOffset(mixed.find("  \r\n"));
    const auto second = parser::SourceOffset(mixed.find("\t \r\n"));
    const auto final = parser::SourceOffset(mixed.find("      work"));
    const parser::SourceRange selected[] =
        {{first, first + 2}, {second, second + 2}, {final, final + 6}};
    style = Canonical(80);
    style.TrailingWhitespace = formatter::TrailingWhitespace::Trim;
    style.MaxEmptyLines = 0; // Partial permissions cannot collapse topology.
    formatter::SyntaxLayoutOptions crlf;
    crlf.LineEnding = document::Newline::CRLF;
    record("mixed trim plus indentation", mixed, style, selected, crlf);
    style.TrailingWhitespace = formatter::TrailingWhitespace::Preserve;
    record("mixed whitespace preserve", mixed, style, selected);
    style.IndentationKind = formatter::IndentationKind::Preserve;
    style.TrailingWhitespace = formatter::TrailingWhitespace::Trim;
    record("mixed indentation preserve", mixed, style, selected);
    const std::string comment = "void f() {\n  // note  \r\n      work();\n}\n";
    const auto commentIndent = parser::SourceOffset(comment.find("      work"));
    const parser::SourceRange commentSelection[] = {
        {
            parser::SourceOffset(comment.find("note") + 4),
            parser::SourceOffset(comment.find("note") + 6),
        },
        {commentIndent, commentIndent + 6},
    };
    style.IndentationKind = formatter::IndentationKind::Spaces;
    record("comment owns spaces and CR", comment, style, commentSelection);
    const std::string call = "void f() {\n  call(alpha,\n          beta);\n}\n";
    const auto betaIndent = parser::SourceOffset(call.find("          beta"));
    const parser::SourceRange betaSelection[] = {{betaIndent, betaIndent + 10}};
    style = Canonical(13);
    style.ListAlignment = formatter::ContinuationAlignment::Delimiter;
    style.ContinuationOffset = 2;
    style.AlignmentFallbackOffset = 2;
    record("delimiter equality fits suffix", call, style, betaSelection);
    style.ColumnLimit = 12;
    record("delimiter suffix fallback", call, style, betaSelection);
    style.IndentationKind = formatter::IndentationKind::Tabs;
    formatter::SyntaxLayoutOptions tabs;
    tabs.TabWidth = 4;
    record("tabs fallback", call, style, betaSelection, tabs);
    const std::string closing = "void f() {\n  work();\n      }\n";
    const auto closer = parser::SourceOffset(closing.find("      }"));
    const parser::SourceRange closingSelection[] = {{closer, closer + 6}};
    record("closing block anchor", closing, Canonical(80), closingSelection);
    const std::string access = "class C {\n      public:\n  int value;\n};\n";
    const auto accessIndent = parser::SourceOffset(access.find("      public"));
    const parser::SourceRange accessSelection[] =
        {{accessIndent, accessIndent + 6}};
    style = Canonical(80);
    style.IndentAccessModifiers = formatter::BodyIndentation::NoIndent;
    record("override access anchor", access, style, accessSelection);
    const std::string member = "class C {\n      int value;\n};\n";
    const auto memberIndent = parser::SourceOffset(member.find("      int"));
    const parser::SourceRange memberSelection[] =
        {{memberIndent, memberIndent + 6}};
    style.BodiesLayout = formatter::CategoryLayout::Preserve;
    style.IndentationKind = formatter::IndentationKind::Preserve;
    style.IndentMembers = formatter::BodyIndentation::Indent;
    style.MemberIndentWidth = 4;
    record("explicit member control", member, style, memberSelection);
    const std::string members =
        "class C {\n      int first;\n      int second;\n};\n";
    const auto firstMember = parser::SourceOffset(members.find("      int"));
    const auto secondMember =
        parser::SourceOffset(members.find("      int", firstMember + 6));
    const parser::SourceRange membersSelection[] =
        {{firstMember, firstMember + 6}, {secondMember, secondMember + 6}};
    const std::string namespaced =
        "namespace N {\n      int first;\n      int second;\n}\n";
    const auto firstNamespace =
        parser::SourceOffset(namespaced.find("      int"));
    const auto secondNamespace =
        parser::SourceOffset(namespaced.find("      int", firstNamespace + 6));
    const parser::SourceRange namespaceSelection[] = {
        {firstNamespace, firstNamespace + 6},
        {secondNamespace, secondNamespace + 6},
    };
    style = Canonical(80);
    style.IndentationKind = formatter::IndentationKind::Preserve;
    record(
        "canonical members general preserve",
        members,
        style,
        membersSelection);
    record(
        "canonical namespace general preserve",
        namespaced,
        style,
        namespaceSelection);
    style.IndentMembers = formatter::BodyIndentation::Indent;
    style.MemberIndentWidth = 4;
    style.IndentNamespaces = formatter::BodyIndentation::Indent;
    style.NamespaceIndentWidth = 3;
    record("canonical member indent", members, style, membersSelection);
    record("canonical namespace indent", namespaced, style, namespaceSelection);
    style.IndentMembers = formatter::BodyIndentation::NoIndent;
    style.IndentNamespaces = formatter::BodyIndentation::NoIndent;
    record("canonical member no indent", members, style, membersSelection);
    record(
        "canonical namespace no indent",
        namespaced,
        style,
        namespaceSelection);
    style.BodyShape = formatter::BodyShape::CompactIfFits;
    record(
        "compact canonical member no indent",
        members,
        style,
        membersSelection);
    record(
        "compact canonical namespace no indent",
        namespaced,
        style,
        namespaceSelection);
    style.IndentMembers = formatter::BodyIndentation::Indent;
    style.IndentNamespaces = formatter::BodyIndentation::Indent;
    record("compact canonical member indent", members, style, membersSelection);
    record(
        "compact canonical namespace indent",
        namespaced,
        style,
        namespaceSelection);
    const test::golden::Fixture fixture{
        std::filesystem::path(__FILE__).parent_path() /
            "testdata/partial-gap-lowering",
        "policies",
    };
    fixture.Expect(records, ".golden");
}

TEST(SyntaxLayout, PartialInsertionPointsCutsAndTailRemainFenced)
{
    auto style = Canonical(80);
    style.TrailingWhitespace = formatter::TrailingWhitespace::Trim;
    const std::string input = "void f() {\nwork();\n}\n";
    const auto point = parser::SourceOffset(input.find("work"));
    const parser::SourceRange owned[] = {{point, point + 1}};
    formatter::SourceAnalysisOptions scope;
    scope.EditScope = owned;
    const auto output = Lower(input, style, {}, scope);
    EXPECT_EQ(output.Text, "void f() {\n  work();\n}\n");
    bool generatedPoint = false;
    for (const auto& map : output.Map) {
        if (!map.Exact && map.Source.Begin == point && map.Source.End == point)
        {
            EXPECT_EQ(map.Source.Identity, document::NoIdentity);
            EXPECT_EQ(map.OutputEnd - map.OutputBegin, 2u);
            generatedPoint = true;
        }
    }
    EXPECT_TRUE(generatedPoint);
    const parser::SourceRange unowned[] = {{point - 1, point}};
    scope.EditScope = unowned;
    EXPECT_EQ(Lower(input, style, {}, scope).Text, input);
    scope.EditScope = std::span<const parser::SourceRange>{};
    EXPECT_EQ(Lower(input, style, {}, scope).Text, input);
    for (const std::string whitespace :
        {"    ", "\t  ", "\r  ", "\f  ", "\v  "})
    {
        const std::string cut = "void f() {\n" + whitespace + "work();\n}\n";
        const auto end = parser::SourceOffset(cut.find("work"));
        const parser::SourceRange suffix[] = {{end - 1, end}};
        scope.EditScope = suffix;
        EXPECT_EQ(Lower(cut, style, {}, scope).Text, cut);
    }
    const std::string cutTrim = "void f() {\n  work(); \t \r\n}\n";
    const auto trim = parser::SourceOffset(cutTrim.find(" \t "));
    const parser::SourceRange cutRun[] = {{trim + 1, trim + 3}};
    scope.EditScope = cutRun;
    EXPECT_EQ(Lower(cutTrim, style, {}, scope).Text, cutTrim);
    const std::string tail = "void f() {\n  work();\n}\n    ";
    const parser::SourceRange tailIndent[] = {
        {
            parser::SourceOffset(tail.size() - 4),
            parser::SourceOffset(tail.size()),
        },
    };
    scope.EditScope = tailIndent;
    EXPECT_EQ(Lower(tail, style, {}, scope).Text, tail);
    const std::string eof = "void f() {\n  work();\n}\n";
    const parser::SourceRange eofLF[] = {
        {
            parser::SourceOffset(eof.size() - 1),
            parser::SourceOffset(eof.size()),
        },
    };
    scope.EditScope = eofLF;
    EXPECT_EQ(Lower(eof, style, {}, scope).Text, eof);
}

TEST(SyntaxLayout, PartialFixedLinesCannotSatisfyAlwaysOwnedBreak)
{
    const std::string input = "void f() {\n  call(\n      alpha, beta);\n}\n";
    const auto indent = parser::SourceOffset(input.find("      alpha"));
    const auto beta = parser::SourceOffset(input.find("beta"));
    const parser::SourceRange ranges[] =
        {{indent, indent + 6}, {beta - 1, beta}};
    formatter::SourceAnalysisOptions scope;
    scope.EditScope = ranges;
    auto style = Canonical(100);
    style.CallsBreak = formatter::BreakRequirement::Always;
    const auto output = Lower(input, style, {}, scope);
    EXPECT_NE(
        output.Text.find("call(\n    alpha,\n    beta);"),
        std::string::npos) << output.Text;
    EXPECT_TRUE(std::any_of(
        output.Broken.begin(),
        output.Broken.end(),
        [] (uint8_t value) { return value != 0; }));
    scope.EditScope = std::span(ranges).first(1);
    EXPECT_EQ(
        Lower(input, style, {}, scope).Text,
        "void f() {\n  call(\n    alpha, beta);\n}\n");
}

TEST(SyntaxLayout, PartialPermissionsDoNotBypassRealOrUncertainProtection)
{
    auto style = Canonical(80);
    style.TrailingWhitespace = formatter::TrailingWhitespace::Trim;
    const std::string input = "void f() {\n      work();\n}\n";
    const auto indent = parser::SourceOffset(input.find("      work"));
    const parser::SourceRange selected[] = {{indent, indent + 6}};
    const parser::SourceRange immutable[] = {{indent + 2, indent + 3}};
    formatter::SourceAnalysisOptions scope;
    scope.EditScope = selected;
    scope.ImmutableRanges = immutable;
    EXPECT_EQ(Lower(input, style, {}, scope).Text, input);
    const std::string_view macros[] = {"VERBATIM"};
    const std::vector<std::string> protectedInputs = {
        "void f() {\n// hit-fmt off\n      work();\n// hit-fmt on\n}\n",
        "void f() { VERBATIM(\n      work()); }\n",
        "void f() { broken(,\n      @); }\n",
        "void f() { unknown *\n      value; }\n",
        "void f() {\n  call(decltype(\n      value));\n}\n",
        "void f() {\n flow.Begin();\n      work();\n other.End();\n}\n",
    };
    style.IndentedCallScopePairs = {"Begin:End"};
    for (const auto& protectedInput : protectedInputs) {
        const auto begin = parser::SourceOffset(protectedInput.find("      "));
        const parser::SourceRange ranges[] = {{begin, begin + 6}};
        scope.EditScope = ranges;
        scope.ImmutableRanges = {};
        scope.WhitespaceSensitiveMacros = macros;
        EXPECT_EQ(Lower(protectedInput, style, {}, scope).Text, protectedInput);
    }
    // An explicit island in one function must not erase unrelated grammar
    // context in another, whose tokens are merely outside the edit scope.
    const std::string separate =
        "void first() { blocked(); }\nvoid second() {\n      work();\n}\n";
    const auto blocked = parser::SourceOffset(separate.find("blocked"));
    const auto work = parser::SourceOffset(separate.find("      work"));
    const parser::SourceRange explicitIsland[] = {{blocked, blocked + 7}};
    const parser::SourceRange secondIndent[] = {{work, work + 6}};
    scope.EditScope = secondIndent;
    scope.ImmutableRanges = explicitIsland;
    auto expected = separate;
    expected.replace(work, 6, "  ");
    EXPECT_EQ(Lower(separate, style, {}, scope).Text, expected);
}

TEST(SyntaxLayout, ScopedSyntaxContextCannotAuthorizeExcludedCommaRemoval)
{
    const std::string input = "void f() { consume({alpha  ,  }); }\n";
    const auto comma = parser::SourceOffset(input.find(','));
    const parser::SourceRange ranges[] =
        {{comma - 2, comma}, {comma + 1, comma + 3}};
    formatter::SourceAnalysisOptions scope;
    scope.EditScope = ranges;
    auto style = Canonical(80);
    for (const auto policy :
        {
            formatter::TrailingCommaPolicy::Never,
            formatter::TrailingCommaPolicy::WhenMultiline,
        })
    {
        style.InitializersTrailingComma = policy;
        const auto expectIdentity = [&] (const Output& output) {
            EXPECT_NE(output.Text.find(','), std::string::npos);
            EXPECT_TRUE(std::any_of(
                output.Map.begin(),
                output.Map.end(),
                [&] (const auto& map) {
                    return map.Exact && map.Source.Begin == comma &&
                        map.Source.End == comma + 1 &&
                        map.Source.Identity != document::NoIdentity;
                }));
        };
        expectIdentity(Lower(input, style, {}, scope));
        const parser::SourceRange selected[] = {{comma - 2, comma + 3}};
        scope.EditScope = selected;
        EXPECT_EQ(
            Lower(input, style, {}, scope).Text.find(','),
            std::string::npos);
        const formatter::ProtectedSourceRange protectedComma[] = {
            {
                {comma, comma + 1},
                formatter::ProtectionReason::Explicit |
                    formatter::ProtectionReason::OutsideEditScope,
            },
        };
        scope.InheritedProtection = protectedComma;
        expectIdentity(Lower(input, style, {}, scope));
        scope.InheritedProtection = {};
        scope.EditScope = ranges;
    }
}

TEST(SyntaxLayout, ScopedCloserDoesNotAuthorizeCommaInsertionBeforeComment)
{
    auto style = Canonical(80);
    style.InitializersTrailingComma =
        formatter::TrailingCommaPolicy::WhenMultiline;
    style.ListCloser = formatter::CloserPlacement::OwnLine;
    style.ListBreak = formatter::BreakRequirement::Always;
    formatter::SourceAnalysisOptions scope;
    const std::string horizontal =
        "void f() { consume({alpha /*note*/  }); }\n";
    const auto horizontalClose = parser::SourceOffset(horizontal.find("  }"));
    const parser::SourceRange horizontalRange[] =
        {{horizontalClose, horizontalClose + 2}};
    scope.EditScope = horizontalRange;
    EXPECT_EQ(
        Lower(horizontal, style, {}, scope).Text.find(','),
        std::string::npos);
    EXPECT_NE(Lower(horizontal, style).Text.find(','), std::string::npos);
    // The comment boundary must already be multiline to render broken.
    for (const std::string input :
        {
            "void f() { consume({alpha /*note*/\n  }); }\n",
            "void f() { consume({alpha /*note*/\r\n  }); }\n",
        })
    {
        SCOPED_TRACE(input);
        const auto itemEnd = parser::SourceOffset(input.find("alpha") + 5);
        const auto commentEnd = parser::SourceOffset(input.find("*/") + 2);
        const auto closer = parser::SourceOffset(input.find('}'));
        // Own the complete closer gap and its final indentation point, not
        // the actual insertion point before the intervening comment.
        const parser::SourceRange excluded[] = {{commentEnd, closer + 1}};
        scope.EditScope = excluded;
        const auto denied = Lower(input, style, {}, scope);
        EXPECT_EQ(denied.Text.find(','), std::string::npos) << denied.Text;
        EXPECT_NE(Lower(input, style).Text.find(','), std::string::npos);
        const parser::SourceRange owned[] =
            {{itemEnd, itemEnd + 1}, {commentEnd, closer + 1}};
        scope.EditScope = owned;
        const auto allowed = Lower(input, style, {}, scope);
        EXPECT_NE(allowed.Text.find("alpha, /*note*/"), std::string::npos)
            << allowed.Text;
        EXPECT_TRUE(std::any_of(
            allowed.Map.begin(),
            allowed.Map.end(),
            [&] (const auto& map) {
                return !map.Exact && !map.Protected &&
                    map.Source.Identity == document::NoIdentity &&
                    map.Source.Begin == itemEnd && map.Source.End == itemEnd &&
                    std::string_view(allowed.Text)
                        .substr(
                            map.OutputBegin,
                            map.OutputEnd - map.OutputBegin) == ",";
            }));
        const parser::SourceRange atEnd[] =
            {{itemEnd - 1, itemEnd}, {commentEnd, closer + 1}};
        scope.EditScope = atEnd;
        EXPECT_EQ(
            Lower(input, style, {}, scope).Text.find(','),
            std::string::npos);
        const parser::SourceRange protectedPoint[] = {{itemEnd, itemEnd + 1}};
        scope.EditScope = owned;
        scope.ImmutableRanges = protectedPoint;
        EXPECT_EQ(
            Lower(input, style, {}, scope).Text.find(','),
            std::string::npos);
        scope.ImmutableRanges = {};
    }
    const std::string existing =
        "void f() { consume({alpha /*note*/,\n  }); }\n";
    const auto itemEnd = parser::SourceOffset(existing.find("alpha") + 5);
    const auto commentEnd = parser::SourceOffset(existing.find("*/") + 2);
    const auto comma = parser::SourceOffset(existing.find(','));
    const auto closer = parser::SourceOffset(existing.find('}'));
    const parser::SourceRange selectedComma[] = {{commentEnd, closer + 1}};
    scope.EditScope = selectedComma;
    const auto retained = Lower(existing, style, {}, scope);
    EXPECT_NE(retained.Text.find("alpha /*note*/,"), std::string::npos)
        << retained.Text;
    EXPECT_TRUE(std::any_of(
        retained.Map.begin(),
        retained.Map.end(),
        [&] (const auto& map) {
            return map.Exact && map.Source.Begin == comma &&
                map.Source.End == comma + 1 &&
                map.Source.Identity != document::NoIdentity;
        }));
    const parser::SourceRange relocate[] =
        {{itemEnd, itemEnd + 1}, {commentEnd, closer + 1}};
    scope.EditScope = relocate;
    const auto moved = Lower(existing, style, {}, scope);
    EXPECT_NE(moved.Text.find("alpha, /*note*/"), std::string::npos)
        << moved.Text;
    EXPECT_FALSE(std::any_of(
        moved.Map.begin(),
        moved.Map.end(),
        [&] (const auto& map) {
            return map.Exact && map.Source.Begin == comma &&
                map.Source.End == comma + 1 &&
                map.Source.Identity != document::NoIdentity;
        }));
    // Never only deletes; it does not need ownership of the earlier point.
    style.InitializersTrailingComma = formatter::TrailingCommaPolicy::Never;
    scope.EditScope = selectedComma;
    EXPECT_EQ(
        Lower(existing, style, {}, scope).Text.find(','),
        std::string::npos);
}

TEST(SyntaxLayout, UnscopedAndFullySelectedLoweringAgree)
{
    const std::string input = "void f(){ call(alpha,beta); work(); }\n";
    const parser::SourceRange all[] = {{0, parser::SourceOffset(input.size())}};
    formatter::SourceAnalysisOptions scope;
    scope.EditScope = all;
    for (const auto style : {formatter::Style{}, Canonical(80), Canonical(18)})
    {
        EXPECT_EQ(
            Lower(input, style, {}, scope).Text,
            Lower(input, style).Text);
    }
}

TEST(SyntaxLayout, CanonicalListTopologyIsSourceIndependent)
{
    auto style = Canonical(24);
    style.ListPacking = formatter::ListPacking::VerticalWhenBroken;
    style.ListFirstItem = formatter::FirstItemPlacement::NextLine;
    style.ListCloser = formatter::CloserPlacement::OwnLine;
    const auto a = Lower(
        "void f(){ consume(alpha, nested(beta, gamma), delta); }\n",
        style).Text;
    const auto b = Lower(
        "void f(){ consume(\n alpha,\n nested( "
        "beta,\n gamma ), delta\n); }\n",
        style).Text;
    EXPECT_EQ(a, b);
    EXPECT_EQ(Lower(a, style).Text, a);
    EXPECT_NE(a.find("nested("), std::string::npos);
    EXPECT_NE(a.find("\n"), std::string::npos);
}

TEST(SyntaxLayout, HeaderDecisionEndsBeforeExpandedBody)
{
    auto style = Canonical(80);
    style.FunctionBracePlacement =
        formatter::BracePlacement::NextLineIfHeaderBroken;
    const std::string fitting = "void f(int x) {\n  work();\n}\n";
    for (const auto header : {"void f", "void\nf", "void\t f"}) {
        EXPECT_EQ(
            Lower(std::string(header) + "(int x) { work(); }\n", style).Text,
            fitting);
    }
    const auto output =
        Lower("void f(int x) { first(); second(); }\n", style).Text;
    EXPECT_NE(output.find("void f(int x) {\n"), std::string::npos);
    style.ColumnLimit = 18;
    style.ListPacking = formatter::ListPacking::VerticalWhenBroken;
    style.ListFirstItem = formatter::FirstItemPlacement::NextLine;
    const auto broken =
        Lower("void longer(int first, int second) { first(); }\n", style).Text;
    EXPECT_NE(broken.find(")\n{"), std::string::npos);
    style.ColumnLimit = 80;
    style.ParametersLayout = formatter::CategoryLayout::Preserve;
    style.ExpressionsLayout = formatter::CategoryLayout::Preserve;
    const auto preserved = Lower(
        "void\nf(int x,\n  int y = alpha\n+ beta) { work(); }\n",
        style).Text;
    EXPECT_NE(preserved.find("void f(int x,\n"), std::string::npos)
        << preserved;
    EXPECT_NE(preserved.find("alpha\n    + beta"), std::string::npos)
        << preserved;
    EXPECT_NE(preserved.find(")\n{"), std::string::npos);
    EXPECT_EQ(Lower(preserved, style).Text, preserved);
    EXPECT_NE(
        Lower("struct X { int\nf() const\nnoexcept { return 0; } };\n", style)
            .Text.find("int f() const noexcept {"),
        std::string::npos);
    const auto attribute = Lower(
        "[[deprecated(\n\"old\"\n)]] void\nf() { work(); }\n",
        style).Text;
    EXPECT_NE(attribute.find("[[deprecated(\n\"old\"\n)]]"), std::string::npos)
        << attribute;
    EXPECT_NE(attribute.find("void f()\n{"), std::string::npos) << attribute;
    const parser::SourceRange immutable[] = {{4, 5}};
    formatter::SourceAnalysisOptions safety;
    safety.ImmutableRanges = immutable;
    EXPECT_NE(
        Lower("void\nf() { work(); }\n", style, {}, safety).Text
            .find("void\nf()\n{"),
        std::string::npos);
}

TEST(SyntaxLayout, FunctionTryHeaderSpacingIsSourceIndependent)
{
    auto style = Canonical(80);
    style.FunctionBracePlacement =
        formatter::BracePlacement::NextLineIfHeaderBroken;
    const auto expected = Lower(
        "void f() try { work(); } catch (...) { recover(); }\n",
        style).Text;
    for (const auto header : {"void\nf()\ntry", "void\tf()\ttry"}) {
        const auto output = Lower(
            std::string(header) + " { work(); } catch (...) { recover(); }\n",
            style).Text;
        EXPECT_EQ(output, expected);
        EXPECT_EQ(Lower(output, style).Text, output);
    }
}

TEST(SyntaxLayout, ControlledArmSeparatorsAreSourceIndependent)
{
    for (const auto placement :
        {
            formatter::BracePlacement::Attached,
            formatter::BracePlacement::NextLineIfHeaderBroken,
        })
    {
        auto style = Canonical(80);
        style.ControlBracePlacement = placement;
        EXPECT_EQ(
            Lower(
                "void f(){ if (ready) { first(); }\nelse { second(); } }\n",
                style).Text,
            Lower(
                "void f(){ if (ready) { first(); } else { second(); } }\n",
                style).Text);
    }
}

TEST(SyntaxLayout, ControlledArmProtectionAndTryHeaderPolicies)
{
    auto style = Canonical(80);
    style.ControlBracePlacement = formatter::BracePlacement::NextLine;
    for (const auto condition : {"ready", "meta::matches<Tag>(entry)"}) {
        const std::string header = "if (" + std::string(condition) + ")";
        const std::string protectedInput =
            "void f(){ " + header + " { first(); } else { second(); } }\n";
        for (const bool protectOpening : {false, true}) {
            const auto space = protectOpening
                ? protectedInput.find(" { first")
                : protectedInput.find("else") + 4;
            const parser::SourceRange immutable[] =
                {{uint32_t(space), uint32_t(space + 1)}};
            formatter::SourceAnalysisOptions safety;
            safety.ImmutableRanges = immutable;
            const auto protectedArm =
                Lower(protectedInput, style, {}, safety).Text;
            EXPECT_NE(
                protectedArm.find(header + (protectOpening ? " {" : "\n  {")),
                std::string::npos) << protectedArm;
            EXPECT_NE(
                protectedArm.find(protectOpening ? "else\n  {" : "else {"),
                std::string::npos) << protectedArm;
        }
    }

    // A statement try header follows control policy, not declaration policy.
    style.DeclarationHeadersLayout = formatter::CategoryLayout::Preserve;
    const std::string tryInput =
        "void f() { try { work(); } catch (...) { recover(); } }\n";
    const std::string tryExpected =
        "void f() {\n  try\n  {\n    work();\n  } catch (...)\n"
            "  {\n    recover();\n  }\n}\n";
    EXPECT_EQ(Lower(tryInput, style).Text, tryExpected);
    EXPECT_EQ(Lower(tryExpected, style).Text, tryExpected);
    style.ControlHeadersLayout = formatter::CategoryLayout::Preserve;
    style.DeclarationHeadersLayout = formatter::CategoryLayout::Canonical;
    const auto preservedTry = Lower(tryInput, style).Text;
    EXPECT_NE(preservedTry.find("try {"), std::string::npos) << preservedTry;
    EXPECT_EQ(Lower(preservedTry, style).Text, preservedTry);
}

TEST(SyntaxLayout, ControlledArmsSkipDescendantElseSeparators)
{
    for (const bool outerBraced : {false, true}) {
        for (const bool innerBraced : {false, true}) {
            for (const bool innerElse : {false, true}) {
                const std::string outer = outerBraced ? "{}" : "work();";
                const std::string inner = innerBraced ? "{}" : "act();";
                const std::string source = "void run() {\n if (outer) " +
                    outer + "\n#if FEATURE\n if (first) " + inner +
                    "\n#else\n if (second) " + inner +
                    (innerElse ? " else /* inner */ if (third) " + inner : "") +
                    "\n#endif\n /* parent */ else " + outer + "\n}\n";
                SCOPED_TRACE(source);
                for (const auto placement :
                    {
                        formatter::BracePlacement::Attached,
                        formatter::BracePlacement::NextLine,
                        formatter::BracePlacement::NextLineIfHeaderBroken,
                    })
                {
                    auto style = Canonical(80);
                    style.ControlBracePlacement = placement;
                    const auto output = Lower(source, style);
                    EXPECT_EQ(Lower(output.Text, style).Text, output.Text);
                    EXPECT_NE(
                        output.Text.find("/* parent */ else"),
                        std::string::npos);
                    EXPECT_NE(output.Text.find("#endif"), std::string::npos);
                }
            }
        }
    }
}

TEST(SyntaxLayout, DanglingElseRemainsOwnedByNearestIf)
{
    const std::string source =
        "void run(){ if (outer) if (inner) act(); else recover(); }\n";
    std::string storage = source;
    storage.append(util::SourcePaddingSize, '\0');
    size_t errors = 0;
    const auto tree = parser::Parse(
        {storage.data(), source.size()},
        [&] (parser::ParseError) { ++errors; });
    EXPECT_EQ(errors, 0u);
    const parser::Node* outer = nullptr;
    const parser::Node* inner = nullptr;
    std::vector<const parser::Node *> pending{&tree.Root()};
    while (!pending.empty()) {
        const auto* node = pending.back();
        pending.pop_back();
        if (node->Kind == parser::NodeKind::IfStatement) {
            if (!outer) {
                outer = node;
            } else {
                inner = node;
            }
        }
        for (auto* child = node->FirstChild; child; child = child->NextSibling)
        {
            pending.push_back(child);
        }
    }
    ASSERT_NE(outer, nullptr);
    ASSERT_NE(inner, nullptr);
    EXPECT_EQ(inner->Tokens.End, outer->Tokens.End);
    bool containsElse = false;
    for (auto t = inner->Tokens.Begin; t < inner->Tokens.End; ++t) {
        containsElse |= tree.Kind(t) == tokenizer::TokenKind::Keyword_else;
    }
    EXPECT_TRUE(containsElse);
    for (const auto* child = outer->FirstChild;
        child;
        child = child->NextSibling)
    {
        if (child->Kind == parser::NodeKind::IfStatement) {
            EXPECT_EQ(child, inner);
        }
    }
    auto style = Canonical(80);
    const auto output = Lower(source, style);
    EXPECT_EQ(Lower(output.Text, style).Text, output.Text);
}

TEST(SyntaxLayout, MixedPreserveDoesNotActivateOuterGeneratedGaps)
{
    auto style = Canonical(18);
    style.CallsLayout = formatter::CategoryLayout::Preserve;
    style.ExpressionsLayout = formatter::CategoryLayout::Canonical;
    const auto output = Lower(
        "void f(){ outer(  alpha + beta + gamma,\n   delta  ); }\n",
        style).Text;
    EXPECT_NE(output.find("outer(  "), std::string::npos);
    EXPECT_NE(output.find(",\n"), std::string::npos);
    EXPECT_NE(output.find("delta  )"), std::string::npos);
    EXPECT_EQ(Lower(output, style).Text, output);
}

TEST(SyntaxLayout, ThreePartTernaryAndOperatorSides)
{
    auto style = Canonical(22);
    style.TernaryLayout = formatter::TernaryLayout::ThreePart;
    style.BinaryOperatorSide = formatter::OperatorSide::Leading;
    style.AssignmentOperatorSide = formatter::OperatorSide::Leading;
    const auto output = Lower(
        "void f(){ value = condition ? left + middle " ": right + tail; }\n",
        style).Text;
    EXPECT_NE(output.find("\n"), std::string::npos);
    EXPECT_NE(output.find("? left"), std::string::npos);
    EXPECT_NE(output.find(": right"), std::string::npos);
    EXPECT_EQ(Lower(output, style).Text, output);
    const auto flat = Lower("void f(){ x = a ? b : c; }\n", Canonical(80)).Text;
    EXPECT_NE(flat.find("x = a ? b : c;"), std::string::npos);
}

TEST(SyntaxLayout, FluentCalleeSpineIncludesRepeatedInvocation)
{
    auto style = Canonical(26);
    style.ChainsPacking = formatter::ListPacking::VerticalWhenBroken;
    style.ChainedCallRootNames = {"factory"};
    const auto output = Lower(
        "void f(){ factory()(alpha)(nested(beta, "
        "gamma)).finish()->result(); }\n",
        style).Text;
    EXPECT_NE(output.find("\n    ("), std::string::npos) << output;
    EXPECT_NE(output.find(".finish()"), std::string::npos);
    EXPECT_NE(output.find("->result()"), std::string::npos);
    EXPECT_EQ(Lower(output, style).Text, output);
    style.ColumnLimit = 20;
    auto ordinary = style;
    ordinary.ChainedCallRootNames = {"other"};
    EXPECT_NE(
        Lower("void f(){ factory()(alpha)(beta); }\n", style).Text,
        Lower("void f(){ factory()(alpha)(beta); }\n", ordinary).Text);
}

TEST(SyntaxLayout, ChainStructuralBaseDropsOnlyPendingExpressionIndent)
{
    // Block-aligned chain indentation is independent of the RHS width;
    // enclosing call lists still provide the structural base.
    for (const auto expressions :
        {
            formatter::CategoryLayout::Canonical,
            formatter::CategoryLayout::Preserve,
        })
    {
        for (const size_t offset : {size_t(4), size_t(6)}) {
            auto style = Canonical(10000);
            style.IndentWidth = 4;
            style.ContinuationOffset = offset;
            style.ExpressionsLayout = expressions;
            style.ChainsBreak = formatter::BreakRequirement::Always;
            style.ChainsPacking = formatter::ListPacking::VerticalWhenBroken;
            style.ChainsAlignment = formatter::ContinuationAlignment::Block;
            for (const std::string prefix :
                {
                    "",
                    "auto result = ",
                    "auto considerablyLongerResult = ",
                    "result = ",
                    "result = first + ",
                })
            {
                for (const std::string receiver : {"engine", "longerReceiver"})
                {
                    SCOPED_TRACE(prefix + receiver);
                    SCOPED_TRACE(int(expressions));
                    SCOPED_TRACE(offset);
                    const auto output = Lower(
                        "void f() { " + prefix + receiver +
                        "().Open().Finish(); }\n",
                        style).Text;
                    const auto continuation =
                        "\n" + std::string(4 + offset, ' ');
                    EXPECT_NE(
                        output.find(continuation + ".Open()"),
                        std::string::npos) << output;
                    EXPECT_NE(
                        output.find(continuation + ".Finish()"),
                        std::string::npos) << output;
                    EXPECT_EQ(Lower(output, style).Text, output);
                }
            }
            style.CallsBreak = formatter::BreakRequirement::Always;
            style.CallsFirstItem = formatter::FirstItemPlacement::NextLine;
            for (const std::string prefix : {"", "auto result = "}) {
                for (const std::string argumentPrefix : {"", "first + "}) {
                    SCOPED_TRACE(prefix + argumentPrefix);
                    const auto nested = Lower(
                        "void f() { " + prefix + "outer(inner(" +
                        argumentPrefix + "engine().Open().Finish())); }\n",
                        style).Text;
                    const auto continuation =
                        "\n" + std::string(4 + 3 * offset, ' ');
                    EXPECT_NE(
                        nested.find(continuation + ".Open()"),
                        std::string::npos) << nested;
                    EXPECT_NE(
                        nested.find(continuation + ".Finish()"),
                        std::string::npos) << nested;
                    EXPECT_EQ(Lower(nested, style).Text, nested);
                }
            }
        }
    }
}

TEST(SyntaxLayout, ChainStructuralBaseRespectsPoliciesAndRealizedRhsLines)
{
    auto style = Canonical(10000);
    style.IndentWidth = 4;
    style.ContinuationOffset = 4;
    style.ChainsBreak = formatter::BreakRequirement::IfNeeded;
    style.ChainsPacking = formatter::ListPacking::VerticalWhenBroken;
    const std::string flat =
        "void f() { auto value = engine().Open().Finish(); }\n";
    EXPECT_NE(
        Lower(flat, style).Text.find("engine().Open().Finish()"),
        std::string::npos);
    style.Layout = formatter::LayoutMode::Preserve;
    style.ChainsLayout = formatter::CategoryLayout::Preserve;
    const std::string authored = "void f() {\n    auto value = engine()\n"
        "            .Open()\n            .Finish();\n}\n";
    EXPECT_EQ(Lower(authored, style).Text, authored);
    style.ChainsLayout = formatter::CategoryLayout::Canonical;
    style.ChainsBreak = formatter::BreakRequirement::Always;
    const std::string realized =
        "void f() {\n    auto value =\n        engine()\n"
            "            .Open()\n            .Finish();\n}\n";
    EXPECT_EQ(Lower(realized, style).Text, realized);
    style.ExpressionsLayout = formatter::CategoryLayout::Canonical;
    style.ExpressionsBreak = formatter::BreakRequirement::Always;
    const auto broken = Lower(flat, style).Text;
    EXPECT_NE(
        broken.find("=\n        engine()\n            .Open()"),
        std::string::npos) << broken;
    EXPECT_EQ(Lower(broken, style).Text, broken);
    style.ExpressionsLayout = formatter::CategoryLayout::Preserve;
    style.ChainsAlignment = formatter::ContinuationAlignment::Delimiter;
    const auto aligned = Lower(authored, style).Text;
    EXPECT_NE(aligned.find("\n                 .Open()"), std::string::npos)
        << aligned;
    style.ColumnLimit = 20;
    const auto fallback = Lower(authored, style).Text;
    EXPECT_NE(fallback.find("\n        .Open()"), std::string::npos)
        << fallback;
}

TEST(SyntaxLayout, MessagePinAcceptsOnlyCompleteStringLiteralSequence)
{
    auto style = Canonical(32);
    style.CallLineFirstStringNames = {"message"};
    style.ListFirstItem = formatter::FirstItemPlacement::NextLine;
    style.ListPacking = formatter::ListPacking::VerticalWhenBroken;
    const auto output = Lower(
        "void f(){ message(\"left\" \"right\", " "alpha, beta, gamma); }\n",
        style).Text;
    EXPECT_NE(output.find("message(\"left\" \"right\","), std::string::npos);
    const auto nonliteral = Lower(
        "void f(){ message(\"text\" + value, alpha, beta); }\n",
        style).Text;
    EXPECT_EQ(nonliteral.find("message(\"text\""), std::string::npos);
    style.ColumnLimit = 12;
    const auto wide = Lower(
        "void f(){ message(\"a string longer than "
        "the column limit\", arg); }\n",
        style).Text;
    EXPECT_EQ(wide.find("message(\"a string"), std::string::npos);
}

TEST(SyntaxLayout, MatchedScopesRetainOnlyExistingExtraLevel)
{
    auto style = Canonical(80);
    style.IndentedCallScopePairs = {"Begin:End"};
    const auto nested = Lower(
        "void f(){\n  flow.Begin();\n    work();\n  flow.End();\n}\n",
        style).Text;
    EXPECT_NE(nested.find("\n    work();"), std::string::npos);
    const auto flat = Lower(
        "void f(){\n  flow.Begin();\n  work();\n  flow.End();\n}\n",
        style).Text;
    EXPECT_NE(flat.find("\n  work();"), std::string::npos);
    const auto unmatched =
        Lower("void f(){\n flow.Begin();\n     work(  arg );\n}\n", style).Text;
    EXPECT_NE(unmatched.find("\n     work(  arg );"), std::string::npos);
    const auto different = Lower(
        "void f(){\n left.Begin();\n     work(  arg " ");\n right.End();\n}\n",
        style).Text;
    EXPECT_NE(different.find("\n     work(  arg );"), std::string::npos);
}

TEST(SyntaxLayout, CRLFCommentOwnsCRAndGapStartsWithLF)
{
    auto style = Canonical(18);
    formatter::SyntaxLayoutOptions options;
    options.LineEnding = document::Newline::CRLF;
    const auto output = Lower(
        "void f(){ call(alpha, // note\r\n beta, gamma); }\r\n",
        style,
        options);
    EXPECT_EQ(output.Text.find("\r\r\n"), std::string::npos);
    EXPECT_NE(output.Text.find("// note\r\n"), std::string::npos);
    for (const auto& map : output.Map) {
        if (map.Source.Identity != document::NoIdentity && map.Exact &&
            output
                .Text
                .substr(map.OutputBegin, map.OutputEnd - map.OutputBegin)
                .starts_with("//"))
        {
            EXPECT_EQ(
                output.Text
                    .substr(map.OutputBegin, map.OutputEnd - map.OutputBegin),
                "// note\r");
        }
    }
}

TEST(SyntaxLayout,
     TopologyRebuiltPreserveUsesStableFallbackAndFlatOnlyRetainsTabs)
{
    auto style = Canonical(18);
    style.CommaSpaceAfter = formatter::GapPolicy::Preserve;
    const auto output =
        Lower("void f(){ call(alpha,\tbeta,\tgamma); }\n", style).Text;
    EXPECT_EQ(output.find("\t\n"), std::string::npos);
    EXPECT_EQ(Lower(output, style).Text, output);
    style.ColumnLimit = 80;
    EXPECT_EQ(
        Lower("void f(){ call(alpha,\tbeta); }\n", style).Text.find(",\tbeta"),
        std::string::npos);
    style.CommaSpaceBefore = formatter::GapPolicy::Preserve;
    EXPECT_NE(
        Lower("void f(){ call(alpha\t,beta); }\n", style).Text.find("alpha\t,"),
        std::string::npos);
    style.CallsLayout = formatter::CategoryLayout::Preserve;
    EXPECT_NE(
        Lower("void f(){ call(alpha,\tbeta); }\n", style).Text.find(",\tbeta"),
        std::string::npos);
}

TEST(SyntaxLayout, InitializerCommaFollowsOutputShapeNotInputComma)
{
    auto style = Canonical(80);
    style.InitializersTrailingComma =
        formatter::TrailingCommaPolicy::WhenMultiline;
    style.InitializersCloser = formatter::CloserPlacement::OwnLine;
    const auto a = Lower("int values[] = {alpha, beta,};\n", style).Text;
    const auto b = Lower("int values[] = {\nalpha, beta\n};\n", style).Text;
    EXPECT_EQ(a, b);
    EXPECT_EQ(a.find("beta,"), std::string::npos);
    style.ColumnLimit = 16;
    const auto fitting = Lower("int values[] = {alpha, beta,};\n", style).Text;
    // Breaking the assignment keeps the list flat at the same line cost,
    // so fewer multiline groups win and the conditional comma disappears.
    EXPECT_EQ(fitting.find("beta,"), std::string::npos);
    EXPECT_EQ(Lower(fitting, style).Text, fitting);
    style.ColumnLimit = 15;
    const auto c = Lower("int values[] = {alpha, beta,};\n", style).Text;
    const std::string source = "int values[] = {alpha, beta};\n";
    const auto mapped = Lower(source, style);
    const auto& d = mapped.Text;
    EXPECT_EQ(c, d);
    ASSERT_NE(c.find("beta,"), std::string::npos);
    const auto comma = d.find("beta,") + 4;
    bool found = false;
    for (const auto& map : mapped.Map) {
        if (map.OutputBegin == comma && map.OutputEnd == comma + 1) {
            found = true;
            EXPECT_EQ(map.Source.Begin, source.find("beta") + 4);
            EXPECT_EQ(map.Source.End, map.Source.Begin);
            EXPECT_EQ(map.Source.Identity, document::NoIdentity);
            EXPECT_FALSE(map.Exact);
        }
    }
    EXPECT_TRUE(found);
    // Designators belong to the complete element; normalization must use the
    // value endpoint, not the designator or the original trailing comma.
    for (const std::string input :
        {
            "Pair value = {.first = alpha, .second = beta,};\n",
            "int values[] = {[0] = alpha, [1] = beta,};\n",
        })
    {
        SCOPED_TRACE(input);
        style.ColumnLimit = 80;
        const auto flat = Lower(input, style).Text;
        EXPECT_EQ(flat.find("beta,"), std::string::npos);
        EXPECT_EQ(Lower(flat, style).Text, flat);
        style.ColumnLimit = 16;
        const auto broken = Lower(input, style);
        EXPECT_NE(broken.Text.find("beta,"), std::string::npos);
        const auto endpoint = input.find("beta") + 4;
        EXPECT_TRUE(std::any_of(
            broken.Map.begin(),
            broken.Map.end(),
            [&] (const auto& map) {
                return !map.Exact &&
                    map.Source.Identity == document::NoIdentity &&
                    map.Source.Begin == endpoint &&
                    map.Source.End == endpoint &&
                    std::string_view(broken.Text)
                        .substr(
                            map.OutputBegin,
                            map.OutputEnd - map.OutputBegin) == ",";
            }));
        EXPECT_EQ(Lower(broken.Text, style).Text, broken.Text);
        style.InitializersTrailingComma = formatter::TrailingCommaPolicy::Never;
        const auto deleted = Lower(input, style).Text;
        EXPECT_EQ(deleted.find("beta,"), std::string::npos);
        EXPECT_EQ(Lower(deleted, style).Text, deleted);
        style.InitializersTrailingComma =
            formatter::TrailingCommaPolicy::WhenMultiline;
    }
}

TEST(SyntaxLayout, IncompleteInitializerTailCannotAuthorizeCommaEdits)
{
    auto style = Canonical(80);
    style.InitializersBreak = formatter::BreakRequirement::Always;
    style.InitializersCloser = formatter::CloserPlacement::OwnLine;
    for (const std::string input :
        {
            "void f() { for (auto value : {box.member{1, 2}}) use(value); }\n",
            "void f() { for (auto value : {box.member{1, 2}, "
                "box.member{3, 4},}) use(value); }\n",
            "auto values = {box.member{1, 2}, box.member{3, 4} /*tail*/};\n",
            "void f() { use({box.member{1, 2}, box.member{3, 4},}); }\n",
            "auto values = {box.member{inner{1, 2}}, "
                "box.member{inner{3, 4}},};\n",
        })
    {
        SCOPED_TRACE(input);
        for (const auto policy :
            {
                formatter::TrailingCommaPolicy::Never,
                formatter::TrailingCommaPolicy::
                                      WhenMultiline,
            })
        {
            style.InitializersTrailingComma = policy;
            const auto output = Lower(input, style);
            size_t retained = 0;
            for (const auto& map : output.Map) {
                if (std::string_view(output.Text)
                    .substr(map.OutputBegin, map.OutputEnd - map.OutputBegin) !=
                    ",")
                {
                    continue;
                }
                EXPECT_TRUE(map.Exact);
                EXPECT_NE(map.Source.Identity, document::NoIdentity);
                ++retained;
            }
            EXPECT_EQ(retained, std::count(input.begin(), input.end(), ','));
        }
    }
}

TEST(SyntaxLayout, NestedInitializerCommaUsesCompleteElementEndpoint)
{
    auto style = Canonical(80);
    style.InitializersBreak = formatter::BreakRequirement::Always;
    style.InitializersCloser = formatter::CloserPlacement::OwnLine;
    style.InitializersTrailingComma =
        formatter::TrailingCommaPolicy::WhenMultiline;
    const std::string input = "int rows[][2] = {{1, 2}, {3, 4} /*tail*/};\n";
    const auto endpoint = parser::SourceOffset(input.find("} /*tail*/") + 1);
    const auto output = Lower(input, style);
    bool outerComma = false;
    size_t generated = 0;
    for (const auto& map : output.Map) {
        if (map.Source.Identity == document::NoIdentity &&
            std::string_view(output.Text)
                .substr(map.OutputBegin, map.OutputEnd - map.OutputBegin) ==
                ",")
        {
            EXPECT_FALSE(map.Exact);
            EXPECT_EQ(map.Source.Begin, map.Source.End);
            EXPECT_TRUE(map.Source.Begin == input.find("2}") + 1 ||
                map.Source.Begin == input.find("4}") + 1 ||
                map.Source.Begin == endpoint);
            ++generated;
            if (map.Source.Begin == endpoint) {
                outerComma = true;
            }
        }
    }
    EXPECT_TRUE(outerComma);
    EXPECT_EQ(generated, 3u);
    const auto expectNoOuterComma = [&] (const Output& denied) {
        for (const auto& map : denied.Map) {
            if (map.Source.Identity == document::NoIdentity &&
                map.Source.Begin == endpoint && map.Source.End == endpoint)
            {
                EXPECT_NE(
                    std::string_view(denied.Text).substr(
                        map.OutputBegin,
                        map.OutputEnd - map.OutputBegin),
                    ",");
            }
        }
    };
    // Tail ownership must not bypass insertion-point scope/protection checks.
    const parser::SourceRange excluded[] =
        {{endpoint + 1, parser::SourceOffset(input.size())}};
    formatter::SourceAnalysisOptions scope;
    scope.EditScope = excluded;
    expectNoOuterComma(Lower(input, style, {}, scope));
    const parser::SourceRange protectedPoint[] = {{endpoint, endpoint + 1}};
    scope.EditScope = {};
    scope.ImmutableRanges = protectedPoint;
    expectNoOuterComma(Lower(input, style, {}, scope));
}

TEST(SyntaxLayout, CommaDeletionRetainsIncompatibleGapUnits)
{
    auto style = Canonical(24);
    style.InitializersTrailingComma = formatter::TrailingCommaPolicy::Never;
    formatter::SyntaxLayoutOptions options;
    options.LineEnding = document::Newline::CRLF;
    const auto comment =
        Lower("int values[] = {alpha // note\r\n,};\r\n", style, options).Text;
    EXPECT_NE(comment.find("// note\r\n"), std::string::npos) << comment;
    EXPECT_NE(comment.find(",}"), std::string::npos) << comment;
    EXPECT_EQ(comment.find("\r\r\n"), std::string::npos);
    EXPECT_EQ(Lower(comment, style, options).Text, comment);
    const std::string protectedSource = "int values[] = {alpha  ,};\n";
    const auto comma = protectedSource.find(',');
    const parser::SourceRange immutable[] =
        {{uint32_t(comma - 2), uint32_t(comma)}};
    formatter::SourceAnalysisOptions safety;
    safety.ImmutableRanges = immutable;
    EXPECT_NE(
        Lower(protectedSource, style, {}, safety).Text.find("alpha  ,"),
        std::string::npos);
    formatter::Style preserved;
    preserved.InitializersTrailingComma = formatter::TrailingCommaPolicy::Never;
    const std::string topology = "int values[] = {alpha\n  ,};\n";
    EXPECT_EQ(Lower(topology, preserved).Text, topology);
    style.ColumnLimit = 80;
    EXPECT_EQ(
        Lower("int values[] = {alpha,};\n", style).Text.find("alpha,"),
        std::string::npos);
}

TEST(SyntaxLayout, ExplicitImmutableRegionWinsWithoutFreezingOtherStatements)
{
    auto style = Canonical(20);
    const std::string source =
        "void f(){ raw(  alpha, beta ); next(alpha, beta, gamma); }\n";
    const auto begin = source.find("raw");
    const auto end = source.find(";", begin) + 1;
    const parser::SourceRange immutable[] = {{uint32_t(begin), uint32_t(end)}};
    formatter::SourceAnalysisOptions protection;
    protection.ImmutableRanges = immutable;
    const auto output = Lower(source, style, {}, protection).Text;
    EXPECT_NE(output.find("raw(  alpha, beta );"), std::string::npos);
    EXPECT_NE(output.find("next(alpha,"), std::string::npos);
    EXPECT_EQ(output.find("next(alpha, beta, gamma)"), std::string::npos);
}

TEST(SyntaxLayout, DelimiterFallbackIncludesCloserAndSemicolonSuffix)
{
    auto style = Canonical(18);
    style.ListAlignment = formatter::ContinuationAlignment::Delimiter;
    style.ContinuationOffset = 2;
    style.AlignmentFallbackOffset = 2;
    const auto output =
        Lower("void f(){ lengthy(alpha, betabetabeta); }\n", style).Text;
    EXPECT_NE(output.find("\n    betabetabeta);"), std::string::npos) << output;
    EXPECT_EQ(Lower(output, style).Text, output);
    style.IndentationKind = formatter::IndentationKind::Tabs;
    formatter::SyntaxLayoutOptions tabs;
    tabs.TabWidth = 4;
    EXPECT_NE(
        Lower("void f(){ lengthy(alpha, betabetabeta); }\n", style, tabs).Text
            .find("\n\tbetabetabeta);"),
        std::string::npos);
}

TEST(SyntaxLayout, ListsHeadersLambdasAndControlClausesCompose)
{
    auto style = Canonical(28);
    style.ListPacking = formatter::ListPacking::VerticalWhenBroken;
    style.BaseListsCommaSide = formatter::OperatorSide::Leading;
    style.ConstructorListsColonSide = formatter::OperatorSide::Trailing;
    style.ClassBracePlacement =
        formatter::BracePlacement::NextLineIfHeaderBroken;
    style.LambdaBracePlacement =
        formatter::BracePlacement::NextLineIfHeaderBroken;
    style.ControlBracePlacement =
        formatter::BracePlacement::NextLineIfHeaderBroken;
    const std::string input = "struct Base {}; struct Extra {};\n"
        "struct Derived : Base, Extra { int value; " "Derived(int alpha, int "
        "beta):value(alpha+beta){} "
        "int method(int first, int second){ for(int "
        "index=0;index<first;++index) { "
        "auto fn=[first,second](int extra){return first?second+extra:0;}; "
        "consume(first,fn(second),factory()(first).fi"
        "nish()); } return value; } };\n";
    const auto output = Lower(input, style).Text;
    EXPECT_EQ(Lower(output, style).Text, output) << output;
    EXPECT_NE(output.find("for ("), std::string::npos);
    EXPECT_NE(output.find("[first,"), std::string::npos) << output;
    EXPECT_NE(output.find("value("), std::string::npos);
    const auto directory = std::filesystem::path(FORMATTER_TESTDATA_DIR) /
        ".." / "qualified-record";
    const auto qualified = test::golden::ReadFile(directory / "input.cpp");
    formatter::Style minimal;
    minimal.Layout = formatter::LayoutMode::Canonical;
    const auto ytsaurus = formatter::LoadConfigFile(
        directory / "ytsaurus.hit-fmt").FormattingStyle;
    for (const auto& policy : {minimal, ytsaurus}) {
        const auto rendered = Lower(qualified, policy).Text;
        EXPECT_EQ(Lower(rendered, policy).Text, rendered);
        // Protect only one constructor header, not its record or the file.
        const auto begin = qualified.find("Pointer(int* v)");
        const auto end = begin + std::string_view("Pointer(int* v)").size();
        const parser::SourceRange immutable[] =
            {{parser::SourceOffset(begin), parser::SourceOffset(end)}};
        formatter::SourceAnalysisOptions protection;
        protection.ImmutableRanges = immutable;
        const auto protectedOutput = Lower(qualified, policy, {}, protection);
        EXPECT_NE(
            protectedOutput.Text.find("Pointer(int* v)"),
            std::string::npos);
        for (const auto& map : protectedOutput.Map) {
            if (map.Source.Begin >= begin && map.Source.End <= end &&
                map.Source.Begin < map.Source.End)
            {
                EXPECT_TRUE(map.Exact);
                EXPECT_TRUE(map.Protected);
            }
        }
    }
}

TEST(SyntaxLayout, ExpressionContinuationDoesNotBecomeChildBlockBase)
{
    for (const auto parent :
        {formatter::LayoutMode::Preserve, formatter::LayoutMode::Canonical})
    {
        SCOPED_TRACE(int(parent));
        auto style = Canonical(80);
        style.Layout = parent;
        style.IndentWidth = 4;
        style.CallsLayout = formatter::CategoryLayout::Canonical;
        style.CallsBreak = formatter::BreakRequirement::Always;
        style.CallsFirstItem = formatter::FirstItemPlacement::NextLine;
        style.CallsPacking = formatter::ListPacking::VerticalWhenBroken;
        style.CallsCloser = formatter::CloserPlacement::OwnLine;
        style.LambdaBodiesLayout = formatter::CategoryLayout::Canonical;
        for (const auto prefix : {"", "auto value = "}) {
            SCOPED_TRACE(prefix);
            const std::string call =
                "void f() {\n    " + std::string(prefix) + "take(1, 2);\n}\n";
            const auto output = Lower(call, style).Text;
            EXPECT_NE(
                output.find("take(\n        1,\n        2\n    )"),
                std::string::npos) << output;
            EXPECT_EQ(Lower(output, style).Text, output);
            const std::string lambda = "void f() {\n    " +
                std::string(prefix) + "[]() { return 1; };\n}\n";
            const auto body = Lower(lambda, style).Text;
            EXPECT_NE(
                body.find("[]() {\n        return 1;\n    }"),
                std::string::npos) << body;
            EXPECT_EQ(Lower(body, style).Text, body);
        }
    }
}

TEST(SyntaxLayout, ExpressionsShareRealizedStructuralContinuation)
{
    auto style = Canonical(80);
    style.IndentWidth = 4;
    style.ContinuationOffset = 7;
    style.ControlHeadersContinuationOffset = 3;
    style.ControlHeadersBreak = formatter::BreakRequirement::Always;
    style.ControlHeadersFirstItem = formatter::FirstItemPlacement::NextLine;
    style.ExpressionsBreak = formatter::BreakRequirement::Always;
    const std::string input =
        "void f(){ if(alpha || beta){ return left | right; } }\n";
    for (const auto alignment :
        {
            formatter::ContinuationAlignment::Block,
            formatter::ContinuationAlignment::Delimiter,
        })
    {
        style.ControlHeadersAlignment = alignment;
        for (const auto side :
            {
                formatter::OperatorSide::Leading,
                formatter::OperatorSide::Trailing,
            })
        {
            style.BinaryOperatorSide = side;
            const auto output = Lower(input, style).Text;
            const auto lineIndent = [&] (std::string_view token) {
                const auto position = output.find(token);
                EXPECT_NE(position, std::string::npos) << output;
                const auto start = output.rfind('\n', position) + 1;
                const auto first = output.find_first_not_of(' ', start);
                return first - start;
            };
            const auto headerIndent =
                alignment == formatter::ContinuationAlignment::Block ? 7u : 8u;
            EXPECT_EQ(lineIndent("alpha"), headerIndent) << output;
            EXPECT_EQ(lineIndent("beta"), headerIndent) << output;
            EXPECT_EQ(lineIndent("right"), 15u) << output;
            EXPECT_EQ(Lower(output, style).Text, output);
        }
    }
}

TEST(SyntaxLayout, InlineControlPrefixFitsBeforeIndependentExpressionBreaks)
{
    auto style = Canonical(80);
    style.IndentWidth = 4;
    style.ControlHeadersLayout = formatter::CategoryLayout::Canonical;
    style.ControlHeadersFirstItem = formatter::FirstItemPlacement::InlineIfFits;
    style.ControlHeadersAlignment = formatter::ContinuationAlignment::Delimiter;
    style.ControlBlockKeywordSpacing = formatter::GapPolicy::Space;
    style.ControlBlockConditionSpacing = formatter::GapPolicy::NoSpace;
    style.ControlBracePlacement =
        formatter::BracePlacement::NextLineIfHeaderBroken;
    style.ExpressionsBreak = formatter::BreakRequirement::Always;
    const std::string input =
        "void f(){ if(alpha || beta){ return left | right; } }\n";
    for (const auto side :
        {formatter::OperatorSide::Leading, formatter::OperatorSide::Trailing})
    {
        style.BinaryOperatorSide = side;
        const auto output = Lower(input, style).Text;
        EXPECT_NE(output.find("if (alpha"), std::string::npos) << output;
        EXPECT_NE(output.find(")\n    {"), std::string::npos) << output;
        EXPECT_NE(
            output.find(side == formatter::OperatorSide::Leading
                ? "\n        || beta"
                : "\n        beta"),
            std::string::npos) << output;
        EXPECT_NE(
            output.find(side == formatter::OperatorSide::Leading
                ? "\n            | right"
                : "\n            right"),
            std::string::npos) << output;
        EXPECT_EQ(Lower(output, style).Text, output);
    }

    style.ColumnLimit = 42;
    style.BinaryOperatorSide = formatter::OperatorSide::Trailing;
    const auto fallback = Lower(
        "void f(){ if(ready){} else if("
        "LongConditionRequiresFallback || other){} }\n",
        style).Text;
    EXPECT_NE(fallback.find("else if (\n"), std::string::npos) << fallback;
    EXPECT_NE(
        fallback.find("\n        LongConditionRequiresFallback"),
        std::string::npos) << fallback;
    EXPECT_EQ(Lower(fallback, style).Text, fallback);
}

TEST(SyntaxLayout, RealizedExpressionLineRetainsChildContinuationBase)
{
    auto style = Canonical(80);
    style.Layout = formatter::LayoutMode::Preserve;
    style.IndentWidth = 4;
    style.CallsLayout = formatter::CategoryLayout::Canonical;
    style.CallsBreak = formatter::BreakRequirement::Always;
    style.CallsFirstItem = formatter::FirstItemPlacement::NextLine;
    const std::string input =
        "void f() {\n    auto value =\n        take(1);\n}\n";
    for (const auto kind :
        {
            formatter::IndentationKind::Preserve,
            formatter::IndentationKind::Spaces,
        })
    {
        style.IndentationKind = kind;
        const auto output = Lower(input, style).Text;
        EXPECT_NE(
            output.find("=\n        take(\n            1)"),
            std::string::npos) << output;
        EXPECT_EQ(Lower(output, style).Text, output);
    }
    for (const std::string_view newline : {"\n", "\r\n"}) {
        for (const auto kind :
            {
                formatter::IndentationKind::Spaces,
                formatter::IndentationKind::Tabs,
            })
        {
            SCOPED_TRACE(newline == "\n" ? "LF" : "CRLF");
            SCOPED_TRACE(int(kind));
            const std::string continued = "void f() {" + std::string(newline) +
                "    auto value =" + std::string(newline) + "        take(" +
                std::string(newline) + "            item);" +
                std::string(newline) + "}" + std::string(newline);
            const auto begin =
                parser::SourceOffset(continued.find("            item"));
            const parser::SourceRange ranges[] = {{begin, begin + 12}};
            formatter::SourceAnalysisOptions protection;
            protection.EditScope = ranges;
            style.IndentationKind = kind;
            formatter::SyntaxLayoutOptions options;
            options.TabWidth = 4;
            const auto output = Lower(continued, style, options, protection);
            auto expected = continued;
            if (kind == formatter::IndentationKind::Tabs) {
                expected.replace(begin, 12, "\t\t\t");
            }
            EXPECT_EQ(output.Text, expected);
            const parser::SourceRange repeatedRanges[] = {
                {
                    begin,
                    begin +
                        (kind == formatter::IndentationKind::Tabs ? 3u : 12u),
                },
            };
            formatter::SourceAnalysisOptions repeatedScope;
            repeatedScope.EditScope = repeatedRanges;
            EXPECT_EQ(
                Lower(expected, style, options, repeatedScope).Text,
                expected);
            const auto parentBegin = continued.find('=') + 1;
            const auto parentEnd = continued.find("take(");
            bool exactParent = false;
            bool exactChildPrefix = false;
            bool selectedIndent = false;
            for (const auto& map : output.Map) {
                if (!map.Exact) {
                    EXPECT_GE(map.Source.Begin, begin);
                    EXPECT_LE(map.Source.End, begin + 12);
                }
                if (map.Source.Identity != document::NoIdentity) {
                    continue;
                }
                const auto text = std::string_view(output.Text)
                    .substr(map.OutputBegin, map.OutputEnd - map.OutputBegin);
                if (map.Source.Begin == parentBegin &&
                    map.Source.End == parentEnd)
                {
                    EXPECT_TRUE(map.Exact);
                    EXPECT_TRUE(map.Protected);
                    EXPECT_EQ(
                        text,
                        std::string_view(continued)
                            .substr(parentBegin, parentEnd - parentBegin));
                    exactParent = true;
                }
                if (map.Source.Begin == begin - newline.size() &&
                    map.Source.End == begin)
                {
                    EXPECT_TRUE(map.Exact);
                    EXPECT_TRUE(map.Protected);
                    EXPECT_EQ(text, newline);
                    exactChildPrefix = true;
                }
                if (map.Source.Begin == begin && map.Source.End == begin + 12) {
                    EXPECT_FALSE(map.Exact);
                    EXPECT_FALSE(map.Protected);
                    const std::string_view indent =
                        kind == formatter::IndentationKind::Tabs
                        ? "\t\t\t"
                        : "            ";
                    EXPECT_EQ(text, indent);
                    selectedIndent = true;
                }
            }
            EXPECT_TRUE(exactParent);
            EXPECT_TRUE(exactChildPrefix);
            EXPECT_TRUE(selectedIndent);
        }
    }
    for (const std::string_view newline : {"\n", "\r\n"}) {
        for (const auto trailing :
            {
                formatter::TrailingWhitespace::Preserve,
                formatter::TrailingWhitespace::Trim,
            })
        {
            for (const bool selectParentIndent : {false, true}) {
                SCOPED_TRACE(newline == "\n" ? "LF" : "CRLF");
                SCOPED_TRACE(int(trailing));
                SCOPED_TRACE(selectParentIndent);
                const std::string continued =
                    "void f() {" + std::string(newline) + "    auto value =  " +
                        std::string(newline) + "        take(" +
                        std::string(newline) + "            item);" +
                        std::string(newline) + "}" + std::string(newline);
                const auto trimBegin =
                    parser::SourceOffset(continued.find('=') + 1);
                const auto parentEnd =
                    parser::SourceOffset(continued.find("take("));
                const auto childBegin =
                    parser::SourceOffset(continued.find("            item"));
                const std::array<parser::SourceRange, 3> ranges = {
                    parser::SourceRange{trimBegin, trimBegin + 2},
                    selectParentIndent
                        ? parser::SourceRange{parentEnd - 8, parentEnd}
                        : parser::SourceRange{childBegin, childBegin + 12},
                    parser::SourceRange{childBegin, childBegin + 12},
                };
                formatter::SourceAnalysisOptions protection;
                protection.EditScope =
                    std::span(ranges).first(selectParentIndent ? 3 : 2);
                style.IndentationKind = formatter::IndentationKind::Spaces;
                style.TrailingWhitespace = trailing;
                const auto output = Lower(continued, style, {}, protection);
                auto expected = continued;
                const bool trim =
                    trailing == formatter::TrailingWhitespace::Trim;
                if (trim) {
                    expected.erase(trimBegin, 2);
                }
                EXPECT_EQ(output.Text, expected);
                auto repeatedRanges = ranges;
                for (auto& range : repeatedRanges) {
                    if (trim && range.Begin > trimBegin) {
                        range.Begin -= 2;
                        range.End -= 2;
                    }
                }
                formatter::SourceAnalysisOptions repeatedScope;
                repeatedScope.EditScope = std::span(repeatedRanges).subspan(
                    trim ? 1 : 0,
                    (selectParentIndent ? 3 : 2) - (trim ? 1 : 0));
                EXPECT_EQ(
                    Lower(expected, style, {}, repeatedScope).Text,
                    expected);
                bool exactParent = false;
                bool exactChildPrefix = false;
                bool selectedChild = false;
                bool selectedParent = false;
                for (const auto& map : output.Map) {
                    if (!map.Exact) {
                        EXPECT_TRUE(std::any_of(
                            protection.EditScope->begin(),
                            protection.EditScope->end(),
                            [&] (const auto range) {
                                return map.Source.Begin >= range.Begin &&
                                    map.Source.End <= range.End;
                            }));
                    }
                    if (map.Source.Identity != document::NoIdentity) {
                        continue;
                    }
                    const auto text = std::string_view(output.Text).substr(
                        map.OutputBegin,
                        map.OutputEnd - map.OutputBegin);
                    const auto prefixBegin = trimBegin + (trim ? 2 : 0);
                    const auto prefixEnd =
                        parentEnd - (selectParentIndent ? 8 : 0);
                    if (map.Source.Begin == prefixBegin &&
                        map.Source.End == prefixEnd)
                    {
                        EXPECT_TRUE(map.Exact);
                        EXPECT_TRUE(map.Protected);
                        EXPECT_EQ(
                            text,
                            std::string_view(continued)
                                .substr(prefixBegin, prefixEnd - prefixBegin));
                        exactParent = true;
                    }
                    if (map.Source.Begin == childBegin - newline.size() &&
                        map.Source.End == childBegin)
                    {
                        EXPECT_TRUE(map.Exact);
                        EXPECT_TRUE(map.Protected);
                        EXPECT_EQ(text, newline);
                        exactChildPrefix = true;
                    }
                    if (map.Source.Begin == childBegin &&
                        map.Source.End == childBegin + 12)
                    {
                        EXPECT_FALSE(map.Exact);
                        EXPECT_FALSE(map.Protected);
                        EXPECT_EQ(text, std::string_view("            "));
                        selectedChild = true;
                    }
                    if (selectParentIndent &&
                        map.Source.Begin == parentEnd - 8 &&
                        map.Source.End == parentEnd)
                    {
                        EXPECT_FALSE(map.Exact);
                        EXPECT_FALSE(map.Protected);
                        EXPECT_EQ(text, std::string_view("        "));
                        selectedParent = true;
                    }
                }
                EXPECT_TRUE(exactParent);
                EXPECT_TRUE(exactChildPrefix);
                EXPECT_TRUE(selectedChild);
                EXPECT_EQ(selectedParent, selectParentIndent);
            }
        }
    }
    // A retained prefix before a selected trim already contains an owned LF;
    // the final indentation can remain borrowed rather than generated.
    const std::string blankLine =
        "void f() {\n    auto value =\n  \n        take(\n"
            "            item);\n}\n";
    const auto blankBegin = parser::SourceOffset(blankLine.find("  \n"));
    const auto itemBegin =
        parser::SourceOffset(blankLine.find("            item"));
    const parser::SourceRange blankRanges[] =
        {{blankBegin, blankBegin + 2}, {itemBegin, itemBegin + 12}};
    formatter::SourceAnalysisOptions blankScope;
    blankScope.EditScope = blankRanges;
    auto blankExpected = blankLine;
    blankExpected.erase(blankBegin, 2);
    EXPECT_EQ(Lower(blankLine, style, {}, blankScope).Text, blankExpected);
    style.IndentationKind = formatter::IndentationKind::Preserve;
    EXPECT_EQ(Lower(blankLine, style).Text, blankExpected);
    // With indentation Preserve, the parent final-indent descriptor is ignored;
    // its retained LF still realizes the base for a newly broken child call.
    const std::string ignoredIndent =
        "void f() {\n    auto value =\n        take(item);\n}\n";
    const auto ignoredBegin =
        parser::SourceOffset(ignoredIndent.find("        take"));
    const auto ignoredEnd = parser::SourceOffset(ignoredIndent.find(");") + 1);
    const parser::SourceRange ignoredRanges[] = {{ignoredBegin, ignoredEnd}};
    formatter::SourceAnalysisOptions ignoredScope;
    ignoredScope.EditScope = ignoredRanges;
    const auto ignored = Lower(ignoredIndent, style, {}, ignoredScope);
    EXPECT_EQ(
        ignored.Text,
        "void f() {\n    auto value =\n        take(\n            item);\n}\n");
    for (const auto& map : ignored.Map) {
        if (!map.Exact) {
            EXPECT_GE(map.Source.Begin, ignoredBegin);
            EXPECT_LE(map.Source.End, ignoredEnd);
        }
    }
    // An opaque comment LF is not a current-expression whitespace gap line.
    const std::string comment =
        "void f() {\n    auto value = /*\n    opaque */ take(\n"
            "            item);\n}\n";
    const auto commentItem =
        parser::SourceOffset(comment.find("            item"));
    const parser::SourceRange commentRanges[] =
        {{commentItem, commentItem + 12}};
    formatter::SourceAnalysisOptions commentScope;
    commentScope.EditScope = commentRanges;
    style.IndentationKind = formatter::IndentationKind::Spaces;
    auto commentExpected = comment;
    commentExpected.replace(commentItem, 12, "        ");
    EXPECT_EQ(Lower(comment, style, {}, commentScope).Text, commentExpected);
    style.Layout = formatter::LayoutMode::Canonical;
    style.IndentationKind = formatter::IndentationKind::Spaces;
    const auto flat = Lower(input, style).Text;
    EXPECT_NE(flat.find("= take(\n        1)"), std::string::npos) << flat;
}

TEST(SyntaxLayout, StructuralConsumersRetainListAndVisualScopeNesting)
{
    auto style = Canonical(80);
    style.IndentWidth = 4;
    style.CallsBreak = formatter::BreakRequirement::Always;
    style.CallsFirstItem = formatter::FirstItemPlacement::NextLine;
    const auto nested =
        Lower("void f() { auto value = outer(1 + inner(2)); }\n", style).Text;
    EXPECT_NE(
        nested.find("outer(\n        1 + inner(\n            2)"),
        std::string::npos) << nested;
    EXPECT_EQ(Lower(nested, style).Text, nested);
    style.IndentedCallScopePairs = {"Begin:End"};
    const auto scoped = Lower(
        "void f() {\n    flow.Begin();\n        auto value = take(1);\n"
        "    flow.End();\n}\n",
        style).Text;
    EXPECT_NE(
        scoped.find("\n        auto value = take(\n            1)"),
        std::string::npos) << scoped;
    EXPECT_EQ(Lower(scoped, style).Text, scoped);
}

TEST(SyntaxLayout, ContinuationScopesReindentOnlySelectedHorizontalBytes)
{
    for (const auto newline : {std::string("\n"), std::string("\r\n")}) {
        for (const auto indentation :
            {
                formatter::IndentationKind::Spaces,
                formatter::IndentationKind::Tabs,
            })
        {
            const auto input = "void f() {" + newline + "    bool ready =" +
                newline + "        left ||" + newline +
                "                right;" + newline + "}" + newline;
            const auto begin =
                parser::SourceOffset(input.find("                right"));
            const std::array scope{parser::SourceRange{begin, begin + 16}};
            formatter::SourceAnalysisOptions protection;
            protection.EditScope = scope;
            formatter::SyntaxLayoutOptions options;
            options.TabWidth = 4;
            formatter::Style style;
            style.IndentationKind = indentation;
            const auto output = Lower(input, style, options, protection);
            const auto indent = indentation == formatter::IndentationKind::Tabs
                ? std::string(3, '\t')
                : std::string(12, ' ');
            auto expected = input;
            expected.replace(begin, 16, indent);
            EXPECT_EQ(output.Text, expected);
            const std::array repeatedScope{
                parser::SourceRange{
                    begin,
                    begin + parser::SourceOffset(indent.size()),
                },
            };
            protection.EditScope = repeatedScope;
            EXPECT_EQ(
                Lower(output.Text, style, options, protection).Text,
                output.Text);
            for (const auto& map : output.Map) {
                if (!map.Exact) {
                    EXPECT_GE(map.Source.Begin, begin);
                    EXPECT_LE(map.Source.End, begin + 16);
                }
            }
            const auto callInput = "void f() {" + newline + "    if (large) {" +
                newline + "        throw Failure(" + newline +
                "            render(" + newline + "            first," +
                newline + "            second));" + newline + "    }" +
                newline + "}" + newline;
            const auto first =
                parser::SourceOffset(callInput.find("            first"));
            const auto second =
                parser::SourceOffset(callInput.find("            second"));
            const std::array callScope{
                parser::SourceRange{first, first + 12},
                parser::SourceRange{second, second + 12},
            };
            protection.EditScope = callScope;
            const auto callOutput =
                Lower(callInput, style, options, protection);
            const auto callIndent =
                indentation == formatter::IndentationKind::Tabs
                ? std::string(4, '\t')
                : std::string(16, ' ');
            auto callExpected = callInput;
            callExpected.replace(second, 12, callIndent);
            callExpected.replace(first, 12, callIndent);
            EXPECT_EQ(callOutput.Text, callExpected);
            const auto repeatedSecond =
                parser::SourceOffset(callExpected.find(callIndent + "second"));
            const auto indentSize = parser::SourceOffset(callIndent.size());
            const std::array repeatedCallScope{
                parser::SourceRange{first, first + indentSize},
                parser::SourceRange{
                    repeatedSecond,
                    repeatedSecond + indentSize,
                },
            };
            protection.EditScope = repeatedCallScope;
            EXPECT_EQ(
                Lower(callOutput.Text, style, options, protection).Text,
                callOutput.Text);
            for (const auto& map : callOutput.Map) {
                if (!map.Exact) {
                    EXPECT_TRUE((map.Source.Begin >= first &&
                        map.Source.End <= first + 12) ||
                        (map.Source.Begin >= second &&
                            map.Source.End <= second + 12));
                }
            }
            const auto ternaryInput = "void f() {" + newline +
                "    auto result =" + newline + "        condition" + newline +
                "        ? left +" + newline + "                right" +
                newline + "        : otherLeft +" + newline +
                "        otherRight;" + newline + "}" + newline;
            const auto branchBegin = parser::SourceOffset(
                ternaryInput.find("                right"));
            const std::array branchScope{
                parser::SourceRange{branchBegin, branchBegin + 16},
            };
            protection.EditScope = branchScope;
            const auto branchOutput =
                Lower(ternaryInput, style, options, protection);
            const auto branchIndent =
                indentation == formatter::IndentationKind::Tabs
                ? std::string(3, '\t')
                : std::string(12, ' ');
            auto branchExpected = ternaryInput;
            branchExpected.replace(branchBegin, 16, branchIndent);
            EXPECT_EQ(branchOutput.Text, branchExpected);
            const std::array repeatedBranchScope{
                parser::SourceRange{
                    branchBegin,
                    branchBegin + parser::SourceOffset(branchIndent.size()),
                },
            };
            protection.EditScope = repeatedBranchScope;
            EXPECT_EQ(
                Lower(branchOutput.Text, style, options, protection).Text,
                branchOutput.Text);
            for (const auto& map : branchOutput.Map) {
                if (!map.Exact) {
                    EXPECT_GE(map.Source.Begin, branchBegin);
                    EXPECT_LE(map.Source.End, branchBegin + 16);
                }
            }
            style.TernaryOperatorIndentation =
                formatter::TernaryOperatorIndentation::Indent;
            protection.EditScope = branchScope;
            // Protected markers keep their actual column, even when a new
            // operator level was requested but could not be realized.
            EXPECT_EQ(
                Lower(ternaryInput, style, options, protection).Text,
                branchExpected);
            const auto questionBegin =
                parser::SourceOffset(ternaryInput.find("        ? left"));
            const auto colonBegin =
                parser::SourceOffset(ternaryInput.find("        : otherLeft"));
            const std::array operatorScope{
                parser::SourceRange{questionBegin, questionBegin + 8},
                parser::SourceRange{branchBegin, branchBegin + 16},
                parser::SourceRange{colonBegin, colonBegin + 8},
            };
            protection.EditScope = operatorScope;
            const auto indentedOutput =
                Lower(ternaryInput, style, options, protection);
            const auto armIndent =
                indentation == formatter::IndentationKind::Tabs
                ? std::string(3, '\t')
                : std::string(12, ' ');
            const auto valueIndent =
                indentation == formatter::IndentationKind::Tabs
                ? std::string(4, '\t')
                : std::string(16, ' ');
            auto indentedExpected = ternaryInput;
            indentedExpected.replace(colonBegin, 8, armIndent);
            indentedExpected.replace(branchBegin, 16, valueIndent);
            indentedExpected.replace(questionBegin, 8, armIndent);
            EXPECT_EQ(indentedOutput.Text, indentedExpected);
            const auto repeatedQuestion = parser::SourceOffset(
                indentedExpected.find(armIndent + "? left"));
            const auto repeatedArm = parser::SourceOffset(
                indentedExpected.find(valueIndent + "right"));
            const auto repeatedColon = parser::SourceOffset(
                indentedExpected.find(armIndent + ": otherLeft"));
            const std::array repeatedOperatorScope{
                parser::SourceRange{
                    repeatedQuestion,
                    repeatedQuestion + parser::SourceOffset(armIndent.size()),
                },
                parser::SourceRange{
                    repeatedArm,
                    repeatedArm + parser::SourceOffset(valueIndent.size()),
                },
                parser::SourceRange{
                    repeatedColon,
                    repeatedColon + parser::SourceOffset(armIndent.size()),
                },
            };
            protection.EditScope = repeatedOperatorScope;
            EXPECT_EQ(
                Lower(indentedOutput.Text, style, options, protection).Text,
                indentedOutput.Text);
            for (const auto& map : indentedOutput.Map) {
                if (!map.Exact) {
                    EXPECT_TRUE(std::any_of(
                        operatorScope.begin(),
                        operatorScope.end(),
                        [&] (const auto range) {
                            return map.Source.Begin >= range.Begin &&
                                map.Source.End <= range.End;
                        }));
                }
            }
        }
    }
}

TEST(SyntaxLayout,
    StructuralBaseHandlesDelimiterFallbackTabsAndScopedProtection)
{
    auto style = Canonical(80);
    style.Layout = formatter::LayoutMode::Preserve;
    style.IndentWidth = 4;
    style.CallsLayout = formatter::CategoryLayout::Canonical;
    style.CallsBreak = formatter::BreakRequirement::Always;
    style.CallsFirstItem = formatter::FirstItemPlacement::NextLine;
    style.CallsAlignment = formatter::ContinuationAlignment::Delimiter;
    const std::string input = "void f() {\n    auto value = take(item);\n}\n";
    const auto aligned = Lower(input, style).Text;
    EXPECT_NE(
        aligned.find("take(\n                      item)"),
        std::string::npos) << aligned;
    style.ColumnLimit = 15;
    const auto fallback = Lower(input, style).Text;
    EXPECT_NE(fallback.find("take(\n        item)"), std::string::npos)
        << fallback;
    style.CallsAlignment = formatter::ContinuationAlignment::Block;
    style.IndentationKind = formatter::IndentationKind::Tabs;
    formatter::SyntaxLayoutOptions options;
    options.TabWidth = 4;
    const auto tabs = Lower(
        "void f() {\r\n\tauto value = take(item);\r\n}\r\n",
        style,
        options).Text;
    EXPECT_NE(tabs.find("take(\n\t\titem)"), std::string::npos) << tabs;
    const std::string scoped =
        "void f() {\r\n    auto value = take(\r\n            item);\r\n}\r\n";
    const auto begin = parser::SourceOffset(scoped.find("            item"));
    const parser::SourceRange range[] = {{begin, begin + 12}};
    formatter::SourceAnalysisOptions protection;
    protection.EditScope = range;
    style.IndentationKind = formatter::IndentationKind::Spaces;
    const auto partial = Lower(scoped, style, {}, protection);
    auto expected = scoped;
    // The source '=' is inline: the fixed call gap cannot realize assignment
    // continuation. Only this selected indentation range may be generated.
    expected.replace(begin, 12, "        ");
    EXPECT_EQ(partial.Text, expected);
    bool exactPrefix = false;
    for (const auto& map : partial.Map) {
        if (map.Source.Begin == begin - 2 && map.Source.End == begin) {
            EXPECT_TRUE(map.Exact);
            EXPECT_TRUE(map.Protected);
            exactPrefix = true;
        }
    }
    EXPECT_TRUE(exactPrefix);
    protection.EditScope = {};
    protection.ImmutableRanges = range;
    const auto fixed = Lower(scoped, style, {}, protection).Text;
    EXPECT_NE(fixed.find("\r\n            item"), std::string::npos) << fixed;
}

TEST(SyntaxLayout, IndependentBodyAndIndentControls)
{
    auto style = Canonical(80);
    style.BodyShape = formatter::BodyShape::CompactIfFits;
    const auto compact = Lower("void f(){ first(); second(); }\n", style).Text;
    EXPECT_NE(compact.find("{ first(); second(); }"), std::string::npos)
        << compact;
    style.BodyShape = formatter::BodyShape::Expanded;
    style.LambdaBodiesLayout = formatter::CategoryLayout::Preserve;
    const auto lambda =
        Lower("void f(){ auto fn=[](){  return 1;  }; }\n", style).Text;
    EXPECT_NE(lambda.find("{  return 1;  }"), std::string::npos);
    style.IndentMembers = formatter::BodyIndentation::Indent;
    style.MemberIndentWidth = 4;
    style.IndentAccessModifiers = formatter::BodyIndentation::NoIndent;
    const auto record = Lower("class X { public: int field; };\n", style).Text;
    EXPECT_NE(record.find("\npublic:"), std::string::npos) << record;
    EXPECT_NE(record.find("\n    int field;"), std::string::npos) << record;
    style.StandaloneBlockBraceIndentation =
        formatter::StandaloneBlockBraceIndentation::Indent;
    const auto block = Lower("void f(){ { work(); } }\n", style).Text;
    EXPECT_NE(block.find("\n    {"), std::string::npos) << block;
}

TEST(SyntaxLayout, CaseBraceAndLabelIndentAreIndependentOfFollowingBody)
{
    auto style = Canonical(80);
    style.IndentCaseLabels = formatter::BodyIndentation::NoIndent;
    style.CaseBlockBracePlacement = formatter::BracePlacement::NextLine;
    const auto output = Lower(
        "void f(){ switch(value){ case 1: { first(); "
        "second(); } default: break; } }\n",
        style).Text;
    EXPECT_NE(output.find("\n  case 1:\n    {"), std::string::npos) << output;
    EXPECT_EQ(Lower(output, style).Text, output);

    // A sole block supplies its own level; unbraced cases need one too.
    EXPECT_NE(
        output.find("\n      first();\n      second();"),
        std::string::npos) << output;
    EXPECT_NE(output.find("default:\n    break;"), std::string::npos) << output;
    for (const auto label :
        {
            formatter::BodyIndentation::Indent,
            formatter::BodyIndentation::NoIndent,
        })
    {
        style.IndentWidth = 4;
        style.BlockBodyIndentWidth = 3;
        style.IndentCaseLabels = label;
        style.CaseLabelIndentWidth = 1;
        const auto contents = Lower(
            "void f(){ switch(value){ case 0: case 1: first(); second(); "
            "break; case 2: { third(); } fourth(); break; default: } }\n",
            style).Text;
        // Mixed and unbraced contents use the label base plus one body width.
        const std::string labelIndent =
            label == formatter::BodyIndentation::Indent ? "    " : "   ";
        const std::string contentIndent = labelIndent + "   ";
        EXPECT_NE(
            contents.find("\n" + labelIndent + "case 0:"),
            std::string::npos) << contents;
        EXPECT_NE(
            contents.find("case 1:\n" + contentIndent + "first();\n" +
                contentIndent + "second();\n" + contentIndent + "break;"),
            std::string::npos) << contents;
        EXPECT_NE(
            contents.find("case 2:\n" + contentIndent + "{\n" + contentIndent +
                "   third();\n" + contentIndent + "}\n" + contentIndent +
                "fourth();\n" + contentIndent + "break;"),
            std::string::npos) << contents;
        EXPECT_EQ(Lower(contents, style).Text, contents);
    }

    style.IndentWidth = 4;
    style.BlockBodyIndentWidth = 0;
    style.IndentCaseLabels = formatter::BodyIndentation::NoIndent;
    const auto nested = Lower(
        "void f(int value){ switch(value){ case 1: while(value){ "
        "case 2: third(); } break; } }\n",
        style).Text;
    // NoIndent labels stay at their containing statement's base. Each
    // following unbraced statement adds just one body level.
    EXPECT_NE(
        nested.find("case 1:\n        while (value) {\n"
            "        case 2:\n            third();\n"
            "        }\n        break;"),
        std::string::npos) << nested;
    EXPECT_EQ(Lower(nested, style).Text, nested);

    // This parser bounds an unbraced controlled label at its colon. It does
    // not own the following statement/else as that arm: do not project it.
    const auto incompleteArm = Lower(
        "void f(int value){ switch(value){ if(value) case 0: first(); "
        "else second(); default: break; } }\n",
        style).Text;
    EXPECT_NE(
        incompleteArm
            .find("if(value) case 0:\n" "        first(); else second();"),
        std::string::npos) << incompleteArm;
}

TEST(SyntaxLayout, CaseLabelPreserveRetainsOnlyExistingIndentation)
{
    auto style = Canonical(120);
    style.IndentWidth = 4;
    style.IndentCaseLabels = formatter::BodyIndentation::Preserve;
    const std::string input =
        "void f(){\n switch(value){\ncase 0: first();\n  case 1: second();\n"
            "\tdefault: break;\n }\n}\n";
    const auto output = Lower(input, style);
    EXPECT_NE(
        output.Text.find("\ncase 0:\n            first();"),
        std::string::npos) << output.Text;
    EXPECT_NE(
        output.Text.find("\n  case 1:\n            second();"),
        std::string::npos) << output.Text;
    EXPECT_NE(
        output.Text.find("\n\tdefault:\n            break;"),
        std::string::npos) << output.Text;
    EXPECT_EQ(Lower(output.Text, style).Text, output.Text);
    for (const std::string_view name : {"case 0", "case 1", "default"}) {
        const auto end = input.find(name);
        const auto begin = input.rfind('\n', end) + 1;
        bool exactIndent = false;
        for (const auto& map : output.Map) {
            if (map.Source.Begin == begin && map.Source.End == end && map.Exact)
            {
                EXPECT_EQ(
                    std::string_view(output.Text).substr(
                        map.OutputBegin,
                        map.OutputEnd - map.OutputBegin),
                    std::string_view(input).substr(begin, end - begin));
                exactIndent = true;
            }
        }
        EXPECT_TRUE(exactIndent) << name;
    }
    const auto generated = Lower(
        "void f(){ switch(value){ case 0: first(); default: break; } }\n",
        style).Text;
    EXPECT_NE(
        generated.find("\n        case 0:\n            first();"),
        std::string::npos) << generated;
    EXPECT_EQ(Lower(generated, style).Text, generated);

    // Selecting indentation without its LF must not override label Preserve.
    for (const auto shape :
        {formatter::BodyShape::Expanded, formatter::BodyShape::CompactIfFits})
    {
        style.BodyShape = shape;
        const auto begin = parser::SourceOffset(input.find("  case 1"));
        const parser::SourceRange ranges[] = {{begin, begin + 2}};
        formatter::SourceAnalysisOptions scope;
        scope.EditScope = ranges;
        const auto partial = Lower(input, style, {}, scope);
        EXPECT_EQ(partial.Text, input);
        bool exact = false;
        for (const auto& map : partial.Map) {
            if (map.Source.Begin <= begin && map.Source.End >= begin + 2) {
                EXPECT_TRUE(map.Exact);
                EXPECT_EQ(
                    std::string_view(partial.Text).substr(
                        map.OutputBegin,
                        map.OutputEnd - map.OutputBegin),
                    std::string_view(input).substr(
                        map.Source.Begin,
                        map.Source.End - map.Source.Begin));
                exact = true;
            }
        }
        EXPECT_TRUE(exact);
    }

    style.BodyShape = formatter::BodyShape::Expanded;
    // Token-internal LF and an inline token column are not indentation
    // evidence.
    for (const std::string_view comment : {"/* note */", "/* note\n */"}) {
        const auto inlineLabel = Lower(
            "void f(){ switch(value){ " + std::string(comment) +
            " case 0: first(); } }\n",
            style).Text;
        // The comment attachment vetoes a new label boundary; its LF must
        // neither create indentation provenance nor leak into the body base.
        EXPECT_NE(
            inlineLabel
                .find(std::string(comment) + " case 0:\n            first();"),
            std::string::npos) << inlineLabel;
        EXPECT_EQ(Lower(inlineLabel, style).Text, inlineLabel);
    }

    // Independent immutable and disabled bytes still veto the new indentation.
    const std::string protectedInput =
        "void f(){ switch(value){\n  case 0: first();\n"
            "// clang-format off\n case 1:   second();\n// clang-format on\n"
            "default: break; } }\n";
    const auto begin = parser::SourceOffset(protectedInput.find("  case 0"));
    const auto end = parser::SourceOffset(protectedInput.find("\n", begin));
    const parser::SourceRange ranges[] = {{begin, end}};
    formatter::SourceAnalysisOptions protection;
    protection.ImmutableRanges = ranges;
    protection.HonorClangFormatDirectives = true;
    const auto protectedOutput = Lower(protectedInput, style, {}, protection);
    EXPECT_NE(
        protectedOutput.Text.find("  case 0: first();"),
        std::string::npos) << protectedOutput.Text;
    EXPECT_NE(
        protectedOutput.Text.find(
            "// clang-format off\n case 1:   second();\n// clang-format on"),
        std::string::npos) << protectedOutput.Text;
    for (const auto& map : protectedOutput.Map) {
        if (map.Protected) {
            EXPECT_TRUE(map.Exact);
            EXPECT_EQ(
                std::string_view(protectedOutput.Text)
                    .substr(map.OutputBegin, map.OutputEnd - map.OutputBegin),
                std::string_view(protectedInput).substr(
                    map.Source.Begin,
                    map.Source.End - map.Source.Begin));
        }
    }
}

TEST(SyntaxLayout, AlwaysRequiresOwnedBoundariesButPreserveAndEmptyWin)
{
    auto style = Canonical(100);
    style.CallsBreak = formatter::BreakRequirement::Always;
    style.ChainsBreak = formatter::BreakRequirement::Always;
    style.ExpressionsBreak = formatter::BreakRequirement::Always;
    style.CallLineFirstStringNames = {"message"};
    style.ChainsPacking = formatter::ListPacking::VerticalWhenBroken;
    const auto output = Lower(
        "void f(){ call(item); empty(); "
        "message(\"literal\"); factory()(item)(other); " "x=first+second; }\n",
        style).Text;
    EXPECT_NE(output.find("call(\n"), std::string::npos) << output;
    EXPECT_NE(output.find("empty()"), std::string::npos);
    EXPECT_NE(output.find("message(\n"), std::string::npos);
    const auto literalChild =
        Lower("void f(){ message(\"left\" \"right\"); }\n", style).Text;
    EXPECT_NE(literalChild.find("message(\n"), std::string::npos)
        << literalChild;
    EXPECT_EQ(literalChild.find("\"left\" \"right\""), std::string::npos);
    EXPECT_EQ(Lower(literalChild, style).Text, literalChild);
    EXPECT_NE(output.find("factory()\n"), std::string::npos);
    EXPECT_NE(output.find("first +\n"), std::string::npos);
    EXPECT_EQ(Lower(output, style).Text, output);
    style.CallsLayout = formatter::CategoryLayout::Preserve;
    EXPECT_NE(
        Lower("void f(){ call(item); }\n", style).Text.find("call(item)"),
        std::string::npos);
}

TEST(SyntaxLayout, EmptyShapeNamespaceIndentAndEnumListAreIndependent)
{
    auto style = Canonical(80);
    style.EmptyBodyShape = formatter::BodyShape::CompactIfFits;
    style.IndentNamespaces = formatter::BodyIndentation::NoIndent;
    style.EnumeratorsLayout = formatter::CategoryLayout::Canonical;
    style.EnumeratorsBreak = formatter::BreakRequirement::Always;
    style.EnumeratorsPacking = formatter::ListPacking::VerticalWhenBroken;
    style.EnumeratorsFirstItem = formatter::FirstItemPlacement::NextLine;
    style.EmptyLambdaBodySpacing = formatter::GapPolicy::GeneratedNoSpace;
    style.EnumeratorsCloser = formatter::CloserPlacement::OwnLine;
    style.EnumBracePlacement =
        formatter::BracePlacement::NextLineIfHeaderBroken;
    const auto output = Lower(
        "namespace Demo { void empty(){} auto "
        "fn=[](){}; enum State { First, Second, }; " "enum Empty {}; }\n",
        style).Text;
    EXPECT_NE(output.find("\nvoid empty() {}"), std::string::npos) << output;
    EXPECT_NE(output.find("[]() {}"), std::string::npos);
    EXPECT_NE(output.find("enum State {\n"), std::string::npos) << output;
    EXPECT_NE(output.find("Second,"), std::string::npos);
    EXPECT_NE(output.find("enum Empty {}"), std::string::npos);
    EXPECT_EQ(Lower(output, style).Text, output);
    style.BodiesLayout = formatter::CategoryLayout::Preserve;
    const auto preservedBody =
        Lower("enum State { First, Second, };\n", style).Text;
    EXPECT_NE(preservedBody.find("\n"), std::string::npos);
    EXPECT_EQ(preservedBody.find("First, Second"), std::string::npos);
    style.IndentNamespaces = formatter::BodyIndentation::Indent;
    style.NamespaceIndentWidth = 3;
    EXPECT_NE(
        Lower("namespace A::B {\nvoid work(){ task(); }\n}\n", style).Text
            .find("\n   void"),
        std::string::npos);
    // Preserved source geometry is not a validated 1..32 style offset.
    style.IndentNamespaces = formatter::BodyIndentation::Preserve;
    style.BodiesLayout = formatter::CategoryLayout::Canonical;
    const std::string wide = "namespace Wide {\n" + std::string(300, ' ') +
        "void work(){ task(); }\n}\n";
    const auto kept = Lower(wide, style).Text;
    EXPECT_NE(
        kept.find("\n" + std::string(300, ' ') + "void"),
        std::string::npos) << kept;
    EXPECT_EQ(Lower(kept, style).Text, kept);
}

TEST(SyntaxLayout, ExpressionPackingCoOwnsOnlySamePrecedenceSpine)
{
    auto style = Canonical(18);
    style.ExpressionsPacking = formatter::ListPacking::VerticalWhenBroken;
    const auto output =
        Lower("void f(){ return alpha+beta*gamma+delta; }\n", style).Text;
    EXPECT_NE(output.find("alpha +\n"), std::string::npos) << output;
    EXPECT_NE(output.find("beta * gamma +\n"), std::string::npos) << output;
    EXPECT_EQ(Lower(output, style).Text, output);
    style.ExpressionsBreak = formatter::BreakRequirement::Always;
    style.ColumnLimit = 80;
    const auto parenthesized =
        Lower("void f(){ return alpha+(beta+gamma); }\n", style).Text;
    EXPECT_NE(parenthesized.find("("), std::string::npos);
    EXPECT_NE(parenthesized.find("beta +\n"), std::string::npos);
    EXPECT_EQ(Lower(parenthesized, style).Text, parenthesized);
}

TEST(SyntaxLayout, DeclarationAssignmentsAndPreservedEnumeratorOwners)
{
    auto style = Canonical(22);
    style.ExpressionsBreak = formatter::BreakRequirement::Always;
    const auto declaration =
        Lower("void f(){ int value=alpha+beta; }\n", style).Text;
    EXPECT_NE(declaration.find("value =\n"), std::string::npos) << declaration;
    EXPECT_EQ(Lower(declaration, style).Text, declaration);
    style.EnumeratorsLayout = formatter::CategoryLayout::Preserve;
    style.EnumeratorsBreak = formatter::BreakRequirement::Always;
    style.IndentationKind = formatter::IndentationKind::Preserve;
    EXPECT_EQ(
        Lower("enum E { A  ,  B  };\n", style).Text,
        "enum E { A  ,  B  };\n");
}

TEST(SyntaxLayout, ExactVerticalGapsCapAndPreservedIndentation)
{
    formatter::Style style;
    style.NamespaceBlankLinesAfterOpen = {formatter::BlankLineMode::Exact, 1};
    style.NamespaceBlankLinesBeforeClose = {formatter::BlankLineMode::Exact, 1};
    style.DefinitionBlankLines = {formatter::BlankLineMode::Exact, 1};
    style.MemberDefinitionBlankLines = {formatter::BlankLineMode::Exact, 0};
    style.AccessSectionBlankLinesBefore = {formatter::BlankLineMode::Exact, 1};
    style.AccessSectionBlankLinesAfterLabel =
        {formatter::BlankLineMode::Exact, 1};
    style.MaxEmptyLines = 1;
    style.IndentAccessModifiers = formatter::BodyIndentation::NoIndent;
    const std::string source =
        "namespace Demo {\r\n\tint first;\r\n\r\n\r\n\r\n\tvoid f(){}\r\n"
            "\t/// documented\r\n\t/// "
            "complete\r\n\t[[nodiscard]] int g(){return " "0;}\r\n}\r\n";
    formatter::SyntaxLayoutOptions options;
    options.LineEnding = document::Newline::CRLF;
    const auto mapped = Lower(source, style, options);
    const auto& output = mapped.Text;
    const auto firstIndent = source.find('\t');
    ExpectSplitGapMap(
        mapped,
        firstIndent - 2,
        firstIndent,
        firstIndent + 1,
        "\r\n\r\n",
        "\t");
    const auto close = source.rfind('}');
    ExpectSplitGapMap(mapped, close - 2, close, close, "\r\n\r\n", "");
    EXPECT_NE(output.find("{\r\n\r\n\tint first;"), std::string::npos)
        << output;
    EXPECT_NE(output.find("first;\r\n\r\n\tvoid f()"), std::string::npos);
    EXPECT_NE(output.find("f(){}\r\n\r\n\t/// documented"), std::string::npos);
    EXPECT_NE(output.find("complete\r\n\t[[nodiscard]]"), std::string::npos);
    EXPECT_EQ(output.find("\r\r\n"), std::string::npos);
    EXPECT_EQ(Lower(output, style, options).Text, output);
    const auto record = Lower(
        "class X { public: int first; private: int second; };\n",
        style).Text;
    EXPECT_NE(record.find("public:\n\n"), std::string::npos) << record;
    EXPECT_NE(record.find("first;\n\nprivate:"), std::string::npos);
    auto zero = style;
    zero.NamespaceBlankLinesAfterOpen.EmptyLines = 0;
    zero.NamespaceBlankLinesBeforeClose.EmptyLines = 0;
    EXPECT_EQ(
        Lower("namespace Inline { int field; }\n", zero).Text,
        "namespace Inline { int field; }\n");
    auto empty = Canonical(80);
    empty.EmptyBodyShape = formatter::BodyShape::CompactIfFits;
    empty.NamespaceBodyInteriorSpacing = formatter::GapPolicy::GeneratedNoSpace;
    empty.NamespaceBlankLinesAfterOpen = {formatter::BlankLineMode::Exact, 1};
    EXPECT_EQ(
        Lower("namespace Empty {}\n", empty).Text,
        "namespace Empty {}\n");
}

TEST(SyntaxLayout, StandaloneCommentProtectedAndScopeEndGapsRemainExact)
{
    formatter::Style style;
    style.StandaloneCommentBlankLinesBefore =
        {formatter::BlankLineMode::Exact, 1};
    style.StandaloneCommentBlankLinesAfter =
        {formatter::BlankLineMode::Exact, 1};
    const std::string source = "int x;\n// note\nint y;\n";
    const auto comment = parser::SourceOffset(source.find("//"));
    const auto next = parser::SourceOffset(source.find("int y"));
    const std::array immutable{parser::SourceRange{comment - 1, next}};
    formatter::SourceAnalysisOptions protection;
    protection.ImmutableRanges = immutable;
    EXPECT_EQ(Lower(source, style, {}, protection).Text, source);
    const std::array scope{parser::SourceRange{0, comment}};
    protection.ImmutableRanges = {};
    protection.EditScope = scope;
    EXPECT_EQ(Lower(source, style, {}, protection).Text, source);
}

TEST(SyntaxLayout, PrefixedDelimiterAnchorsUseRenderedColonAndRebasedFallback)
{
    auto style = Canonical(17);
    style.IndentNamespaces = formatter::BodyIndentation::Indent;
    style.NamespaceIndentWidth = 2;
    style.BaseListsAlignment = formatter::ContinuationAlignment::Delimiter;
    style.BaseListsContinuationOffset = 6;
    style.BaseListsAlignmentFallbackOffset = 2;
    style.BaseListsBreak = formatter::BreakRequirement::Always;
    style.BaseListsPacking = formatter::ListPacking::VerticalWhenBroken;
    style.BaseListsFirstItem = formatter::FirstItemPlacement::NextLine;
    style.ClassBracePlacement = formatter::BracePlacement::NextLine;
    const std::string source = "struct Base {}; struct Extra {}; namespace "
        "N { class Derived : Base, Extra {}; }\n";
    const auto equal = Lower(source, style).Text;
    EXPECT_NE(equal.find("\n        : Base"), std::string::npos) << equal;
    EXPECT_NE(equal.find("\n          , Extra"), std::string::npos) << equal;
    style.ColumnLimit = 16;
    const auto fallback = Lower(source, style).Text;
    EXPECT_NE(fallback.find("\n    , Extra"), std::string::npos) << fallback;
    EXPECT_EQ(Lower(fallback, style).Text, fallback);
    style.ColumnLimit = 20;
    style.BaseListsColonSide = formatter::OperatorSide::Trailing;
    const auto trailing = Lower(source, style).Text;
    EXPECT_NE(trailing.find("Derived:\n    Base"), std::string::npos)
        << trailing;
    style.ConstructorListsAlignment =
        formatter::ContinuationAlignment::Delimiter;
    style.ConstructorListsContinuationOffset = 6;
    style.ConstructorListsAlignmentFallbackOffset = 2;
    style.ConstructorListsBreak = formatter::BreakRequirement::Always;
    style.ConstructorListsPacking = formatter::ListPacking::VerticalWhenBroken;
    style.ConstructorListsFirstItem = formatter::FirstItemPlacement::NextLine;
    style.ConstructorListsColonSide = formatter::OperatorSide::Trailing;
    style.FunctionBracePlacement = formatter::BracePlacement::NextLine;
    style.ColumnLimit = 19;
    const auto constructor = Lower(
        "namespace N { class X { int widefield; int "
        "other; X():widefield(1),other(2){} }; }\n",
        style).Text;
    EXPECT_NE(constructor.find("\n      widefield("), std::string::npos)
        << constructor;
    EXPECT_EQ(Lower(constructor, style).Text, constructor);
}

TEST(SyntaxLayout,
     GeneratedAdjacencyMetadataDoesNotActivateUnrelatedPreservedEdges)
{
    const std::string source = "void f(){ call(  item,  other  ); }\n";
    EXPECT_EQ(Lower(source, {}).Text, source);
    const formatter::GapId generated[] = {7};
    formatter::SyntaxLayoutOptions options;
    options.GeneratedAdjacencies = generated;
    EXPECT_EQ(
        Lower(source, {}, options).Text,
        "void f(){ call(item,  other  ); }\n");
}

TEST(SyntaxLayout, NestedVisualScopesAndWrapperContextRespectBarriers)
{
    auto style = Canonical(80);
    style.IndentedCallScopePairs = {"Begin:End"};
    const auto flatNested = Lower(
        "void f(){\n  flow.Begin();\n    " "flow.Begin();\n    work();\n    "
        "flow.End();\n  flow.End();\n}\n",
        style).Text;
    EXPECT_NE(flatNested.find("\n    work();"), std::string::npos);
    EXPECT_EQ(flatNested.find("\n      work();"), std::string::npos);
    const auto multi = Lower(
        "void f(){\n  wrap(tag, flow.Begin());\n    "
        "work();\n  wrap(tag, flow.End());\n}\n",
        style).Text;
    EXPECT_EQ(
        multi,
        "void f() {\n  wrap(tag, flow.Begin());\n    "
        "work();\n  wrap(tag, flow.End());\n}\n");
    EXPECT_EQ(Lower(multi, style).Text, multi);
    for (const auto& context :
        std::vector<std::pair<std::string, std::string>>{
            {"wrap(flow.Begin(), tag)", "wrap(flow.End(), tag)"},
            {"wrap(tag, flow.Begin(), tail)", "wrap(tag, flow.End(), tail)"},
            {
                "outer(tag, inner(left, flow.Begin(), right), tail)",
                "outer(tag, inner(left, flow.End(), right), tail)",
            },
            {"wrap(tag, (flow.Begin()))", "wrap(tag, (flow.End()))"},
        })
    {
        const std::string input = "void f(){\n  " + context.first +
            ";\n    work();\n  " + context.second + ";\n}\n";
        const std::string expected = "void f() {\n  " + context.first +
            ";\n    work();\n  " + context.second + ";\n}\n";
        EXPECT_EQ(Lower(input, style).Text, expected);
        EXPECT_EQ(Lower(expected, style).Text, expected);
        const auto flat = Lower(
            "void f(){\n  " + context.first + ";\n  work();\n  " +
            context.second + ";\n}\n",
            style).Text;
        EXPECT_NE(flat.find("\n  work();"), std::string::npos) << flat;
    }
    for (const auto& context :
        std::vector<std::pair<std::string, std::string>>{
            {"wrap(tag, flow.Begin())", "wrap(other, flow.End())"},
            {"wrap(tag, flow.Begin())", "wrap(tag, other.End())"},
            {"wrap(tag, flow.Begin())", "other(tag, flow.End())"},
            {"wrap(flow.Begin(), tag)", "wrap(tag, flow.End())"},
            {
                "wrap(flow.Begin(), other.Begin())",
                "wrap(flow.End(), other.End())",
            },
        })
    {
        const auto uncertainRegion = Lower(
            "void f(){\n " + context.first + ";\n     work(  item );\n " +
            context.second + ";\n}\nvoid later(){ task(); }\n",
            style).Text;
        EXPECT_NE(
            uncertainRegion.find("\n     work(  item );"),
            std::string::npos) << uncertainRegion;
        EXPECT_NE(
            uncertainRegion.find("void later() {\n  task();\n}"),
            std::string::npos) << uncertainRegion;
        EXPECT_EQ(Lower(uncertainRegion, style).Text, uncertainRegion);
    }
    for (const auto& context :
        std::vector<std::pair<std::string, std::string>>{
            {"wrap([](){ flow.Begin(); })", "wrap([](){ flow.End(); })"},
            {"wrap(sizeof(flow.Begin()))", "wrap(sizeof(flow.End()))"},
            {"wrap(noexcept(flow.Begin()))", "wrap(noexcept(flow.End()))"},
        })
    {
        const auto excluded = Lower(
            "void f(){\n  " + context.first + ";\n    work();\n  " +
            context.second + ";\n}\n",
            style).Text;
        EXPECT_NE(excluded.find("\n  work();"), std::string::npos) << excluded;
    }
    const auto nestedWrappers = Lower(
        "void f(){\n  wrap(outer, flow.Begin());\n   "
        " wrap(inner, flow.Begin());\n      "
        "work();\n    wrap(inner, flow.End());\n  "
        "wrap(outer, flow.End());\n}\n",
        style).Text;
    EXPECT_NE(nestedWrappers.find("\n      work();"), std::string::npos)
        << nestedWrappers;
    EXPECT_EQ(Lower(nestedWrappers, style).Text, nestedWrappers);
    const auto executionBarrier = Lower(
        "void f(){\n  wrap(tag, flow.Begin());\n    "
        "before();\nlabel:\n    after();\n  " "wrap(tag, flow.End());\n}\n",
        style).Text;
    EXPECT_NE(executionBarrier.find("\n    after();"), std::string::npos)
        << executionBarrier;
    const auto wrapper = Lower(
        "void f(){\n  wrap(flow.Begin());\n    "
        "work();\n  wrap(flow.End());\n}\n",
        style).Text;
    EXPECT_NE(wrapper.find("\n    work();"), std::string::npos);
    const auto uncertain = Lower(
        "void f(){\n wrap(flow.Begin());\n     work( "
        " item );\n other(flow.End());\n}\n",
        style).Text;
    EXPECT_NE(uncertain.find("\n     work(  item );"), std::string::npos);
    const auto barrier = Lower(
        "void f(){\n flow.Begin();\n     work(  item "
        ");\n#define VALUE 1\n flow.End();\n}\n",
        style).Text;
    EXPECT_NE(barrier.find("\n     work(  item );"), std::string::npos);
    EXPECT_EQ(Lower(wrapper, style).Text, wrapper);
    const std::string protectedInput =
        "void f(){\n  wrap(tag, flow.Begin());\n    "
            "before();\n    blocked();\n    after();\n  "
            "wrap(tag, flow.End());\n}\nvoid later(){ " "task(); }\n";
    const auto blocked = protectedInput.find("blocked()");
    const parser::SourceRange immutable[] =
        {{uint32_t(blocked), uint32_t(blocked + 9)}};
    formatter::SourceAnalysisOptions safety;
    safety.ImmutableRanges = immutable;
    const auto protectedScope = Lower(protectedInput, style, {}, safety).Text;
    EXPECT_NE(protectedScope.find("\n    before();"), std::string::npos);
    EXPECT_NE(protectedScope.find("\n    after();"), std::string::npos);
    EXPECT_NE(
        protectedScope.find("void later() {\n  task();\n}"),
        std::string::npos);
    const auto afterDirective = Lower(
        "void f(){\n  wrap(tag, flow.Begin());\n    "
        "before();\n#define VALUE 1\n    after();\n  "
        "wrap(tag, flow.End());\n}\n",
        style).Text;
    EXPECT_NE(afterDirective.find("\n    after();"), std::string::npos)
        << afterDirective;
}

TEST(SyntaxLayout, MacroProtectionWinsOverBreakSpacingAndCommaActions)
{
    auto style = Canonical(18);
    style.ListBreak = formatter::BreakRequirement::Always;
    style.InitializersTrailingComma = formatter::TrailingCommaPolicy::Never;
    const std::string_view macros[] = {"VERBATIM"};
    formatter::SourceAnalysisOptions protection;
    protection.WhitespaceSensitiveMacros = macros;
    const auto output = Lower(
        "void f(){ VERBATIM({ alpha,  beta, }); " "consume({alpha,beta,}); }\n",
        style,
        {},
        protection).Text;
    EXPECT_NE(output.find("VERBATIM({ alpha,  beta, })"), std::string::npos)
        << output;
    EXPECT_EQ(output.rfind("beta,"), output.find("beta,"));
    EXPECT_EQ(Lower(output, style, {}, protection).Text, output);
    // Protection inside an argument prevents the outer list combinator from
    // being planned. Its generated edges/separators must remain preserved,
    // while the flat-only callee prefix and safe child/neighbor owners still
    // have their independent contexts.
    formatter::Style calls;
    calls.CallsLayout = formatter::CategoryLayout::Canonical;
    calls.FunctionCallSpaceBeforeParameters =
        formatter::GapPolicy::GeneratedNoSpace;
    const std::string input =
        "void demo(){ outer  (  child(  a , b  ) , VERBATIM(x,  y) ,\n"
            "    right  ); side  (  c , d  ); }\n";
    const auto mapped = Lower(input, calls, {}, protection);
    EXPECT_NE(
        mapped.Text
            .find("outer(  child(a, b) , VERBATIM(x,  y) ,\n    right  )"),
        std::string::npos) << mapped.Text;
    EXPECT_NE(mapped.Text.find("side(c, d)"), std::string::npos);
    EXPECT_EQ(Lower(mapped.Text, calls, {}, protection).Text, mapped.Text);
    const auto macroBegin = input.find("VERBATIM");
    const auto macroEnd = input.find(')', macroBegin) + 1;
    size_t protectedBytes = 0;
    for (const auto& map : mapped.Map) {
        if (map.Source.Begin >= macroBegin && map.Source.End <= macroEnd &&
            map.Source.Begin < map.Source.End)
        {
            EXPECT_TRUE(map.Exact);
            EXPECT_TRUE(map.Protected);
            protectedBytes += map.Source.End - map.Source.Begin;
        }
    }
    EXPECT_EQ(protectedBytes, macroEnd - macroBegin);
    calls.CallsLayout = formatter::CategoryLayout::Preserve;
    EXPECT_EQ(Lower(input, calls, {}, protection).Text, input);
    calls.CallsLayout = formatter::CategoryLayout::Canonical;
    const auto safe = Lower(input, calls).Text;
    EXPECT_NE(
        safe.find("outer(child(a, b), VERBATIM(x, y), right)"),
        std::string::npos) << safe;
    EXPECT_EQ(safe.find(",\n"), std::string::npos);
    // Explicit spacing is not a generated-only policy and remains authorized
    // on editable horizontal gaps even without the surrounding combinator.
    calls.SpaceInsideCallArguments = formatter::GapPolicy::Space;
    calls.CommaSpaceBefore = formatter::GapPolicy::NoSpace;
    calls.CommaSpaceAfter = formatter::GapPolicy::Space;
    const auto explicitOutput = Lower(input, calls, {}, protection).Text;
    EXPECT_NE(
        explicitOutput
            .find("outer( child( a, b ), VERBATIM(x,  y),\n    right )"),
        std::string::npos) << explicitOutput;
    // An excluded macro/list does not grant authority outside the selected
    // neighboring call, nor block its legitimate canonical spacing.
    calls.SpaceInsideCallArguments = formatter::GapPolicy::GeneratedNoSpace;
    calls.CommaSpaceBefore = formatter::GapPolicy::GeneratedNoSpace;
    calls.CommaSpaceAfter = formatter::GapPolicy::GeneratedSpace;
    const auto neighbor = parser::SourceOffset(input.find("side"));
    const auto neighborEnd = parser::SourceOffset(input.find(';', neighbor));
    const parser::SourceRange selected[] = {{neighbor, neighborEnd}};
    protection.EditScope = selected;
    const auto scoped = Lower(input, calls, {}, protection).Text;
    EXPECT_EQ(scoped.substr(0, neighbor), input.substr(0, neighbor));
    EXPECT_NE(scoped.find("side(c, d)"), std::string::npos);
    protection.EditScope = {};
    // The same unplanned-list rule applies to parameters, without changing
    // their separately owned flat-only function opener.
    calls.ParametersLayout = formatter::CategoryLayout::Canonical;
    calls.FunctionSpaceBeforeParameters =
        formatter::GapPolicy::GeneratedNoSpace;
    const std::string declaration =
        "void declared  (  int value = VERBATIM(x,  y) , int other  );\n";
    EXPECT_EQ(
        Lower(declaration, calls, {}, protection).Text,
        "void declared(  int value = VERBATIM(x,  y) , int other  );\n");
}

TEST(SyntaxLayout, VerticalCountsAfterCommentCRAndDirectiveBarriersStaySafe)
{
    formatter::Style style;
    style.DefinitionBlankLines = {formatter::BlankLineMode::Exact, 1};
    style.MaxEmptyLines = 1;
    formatter::SyntaxLayoutOptions options;
    options.LineEnding = document::Newline::CRLF;
    const std::string source = "void first(){} // note\r\nvoid "
        "second(){}\r\n#define RAW(x) x ## x\r\nvoid " "third(){}\r\n";
    const auto mapped = Lower(source, style, options);
    const auto& output = mapped.Text;
    const auto second = source.find("void second");
    ExpectSplitGapMap(mapped, second - 1, second, second, "\n\r\n", "");
    EXPECT_NE(output.find("// note\r\n\r\nvoid second"), std::string::npos)
        << output;
    EXPECT_NE(
        output.find("#define RAW(x) x ## x\r\nvoid third"),
        std::string::npos);
    EXPECT_EQ(output.find("\r\r\n"), std::string::npos);
    EXPECT_EQ(Lower(output, style, options).Text, output);
}

class CountingResource
    : public std::pmr::memory_resource
{
public:
    size_t Current = 0;
    size_t Peak = 0;
    size_t Allocations = 0;
private:
    void* do_allocate(size_t bytes, size_t alignment) override
    {
        Current += bytes;
        Peak = std::max(Peak, Current);
        ++Allocations;
        return std::pmr::new_delete_resource()->allocate(bytes, alignment);
    }

    void do_deallocate(void* pointer, size_t bytes, size_t alignment) override
    {
        Current -= bytes;
        std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
    }

    bool do_is_equal(
        const std::pmr::memory_resource& other) const noexcept override
    {
        return this == &other;
    }
};

TEST(SyntaxLayout, FileAndScratchArenasReleaseAndScaleWithWideInput)
{
    size_t smallFile = 0;
    size_t smallScratch = 0;
    for (const int count : {1000, 2000}) {
        std::string input = "void f(){ auto result = call(";
        for (int i = 0; i < count; ++i) {
            if (i) {
                input += ',';
            }
            input += "item";
        }
        input += "); }\n";
        CountingResource file;
        CountingResource scratch;
        const auto output =
            Lower(input, Canonical(40), {}, {}, &file, &scratch);
        EXPECT_EQ(file.Current, 0u);
        EXPECT_EQ(scratch.Current, 0u);
        EXPECT_LT(file.Peak, size_t(count) * 2048);
        EXPECT_LT(scratch.Peak, size_t(count) * 2048);
        EXPECT_LT(file.Allocations, 100u);
        EXPECT_LT(scratch.Allocations, 100u);
        RecordProperty("file_peak_" + std::to_string(count), int(file.Peak));
        RecordProperty(
            "scratch_peak_" + std::to_string(count),
            int(scratch.Peak));
        RecordProperty(
            "file_allocations_" + std::to_string(count),
            int(file.Allocations));
        RecordProperty(
            "scratch_allocations_" + std::to_string(count),
            int(scratch.Allocations));
        if (count == 1000) {
            smallFile = file.Peak;
            smallScratch = scratch.Peak;
        } else {
            EXPECT_LT(file.Peak, smallFile * 3);
            EXPECT_LT(scratch.Peak, smallScratch * 3);
        }
        EXPECT_LT(output.Text.size(), input.size() * 3);
    }
}

TEST(SyntaxLayout, SparsePartialPermissionsReleaseScratchAndScale)
{
    size_t smallFile = 0;
    size_t smallScratch = 0;
    for (const int count : {1000, 2000}) {
        std::string input = "void f() {\n";
        std::string expected = input;
        std::vector<parser::SourceRange> ranges;
        ranges.reserve(size_t(count) * 2);
        for (int i = 0; i < count; ++i) {
            const auto begin = parser::SourceOffset(input.size());
            input += "      work();  \n";
            ranges.push_back({begin, begin + 6});
            const auto trim = parser::SourceOffset(input.size() - 3);
            ranges.push_back({trim, trim + 2});
            expected += "  work();\n";
        }
        input += "}\n";
        expected += "}\n";
        formatter::SourceAnalysisOptions scope;
        scope.EditScope = ranges;
        auto style = Canonical(80);
        style.TrailingWhitespace = formatter::TrailingWhitespace::Trim;
        CountingResource file;
        CountingResource scratch;
        const auto output = Lower(input, style, {}, scope, &file, &scratch);
        EXPECT_EQ(output.Text, expected);
        EXPECT_EQ(file.Current, 0u);
        EXPECT_EQ(scratch.Current, 0u);
        EXPECT_LT(file.Peak, size_t(count) * 8192);
        EXPECT_LT(scratch.Peak, size_t(count) * 8192);
        EXPECT_LT(file.Allocations, 100u);
        EXPECT_LT(scratch.Allocations, 100u);
        RecordProperty(
            "partial_file_peak_" + std::to_string(count),
            int(file.Peak));
        RecordProperty(
            "partial_scratch_peak_" + std::to_string(count),
            int(scratch.Peak));
        RecordProperty(
            "partial_file_allocations_" + std::to_string(count),
            int(file.Allocations));
        RecordProperty(
            "partial_scratch_allocations_" + std::to_string(count),
            int(scratch.Allocations));
        if (count == 1000) {
            smallFile = file.Peak;
            smallScratch = scratch.Peak;
        } else {
            EXPECT_LT(file.Peak, smallFile * 3);
            EXPECT_LT(scratch.Peak, smallScratch * 3);
        }
    }
}

TEST(SyntaxLayout, WideAndDeepGroupsHaveBoundedIterativeConstruction)
{
    auto style = Canonical(60);
    std::string wide = "void f(){ call(";
    for (int i = 0; i < 3000; ++i) {
        if (i) {
            wide += ',';
        }
        wide += "item";
    }
    wide += "); }\n";
    const auto output = Lower(wide, style).Text;
    EXPECT_LT(output.size(), wide.size() * 3);
    EXPECT_EQ(Lower(output, style).Text, output);
    std::string deep = "void f(){ auto result = ";
    for (int i = 0; i < 100; ++i) {
        deep += "call(";
    }
    deep += "item";
    deep.append(100, ')');
    deep += "; }\n";
    EXPECT_LT(Lower(deep, style).Text.size(), deep.size() * 40);
}

} // namespace

TEST(SyntaxLayout, RecordHeadCompositionSpacingRespectsEditAuthority)
{
    // A qualified specialization combines record-head children, constructor
    // injected scope and delimiter-isolated parameter defaults in one input.
    const auto composition = test::golden::ReadFile(
        std::filesystem::path(FORMATTER_TESTDATA_DIR) /
        "../record-head-commas/constructor-composition.input.cpp");
    auto composedStyle = Canonical(200);
    composedStyle.CommaSpaceAfter = formatter::GapPolicy::Space;
    const auto point = parser::SourceOffset(composition.find(">,2>") + 2);
    const parser::SourceRange selected[] = {{point, point + 1}};
    formatter::SourceAnalysisOptions composedProtection;
    composedProtection.EditScope = selected;
    auto scopedExpected = composition;
    scopedExpected.insert(point, " ");
    EXPECT_EQ(
        Lower(composition, composedStyle, {}, composedProtection).Text,
        scopedExpected);
    const parser::SourceRange immutable[] = {{point - 1, point + 1}};
    composedProtection.ImmutableRanges = immutable;
    EXPECT_EQ(
        Lower(composition, composedStyle, {}, composedProtection).Text,
        composition);
}

TEST(SyntaxLayout, RecordHeadCommaPreferenceIsClassifiedAndLocallyAuthorized)
{
    const std::string source = "template<> struct Pair<int,long> {};\n";
    std::string storage(source);
    storage.append(util::SourcePaddingSize, '\0');
    const auto tree = parser::Parse(
        {storage.data(), source.size()},
        [] (parser::ParseError) {});
    auto style = Canonical(200);
    style.CommaSpaceAfter = formatter::GapPolicy::Space;
    std::pmr::monotonic_buffer_resource file;
    std::pmr::monotonic_buffer_resource scratch;
    const auto safety = formatter::BuildSourceAnalysis(tree, {}, file, scratch);
    const auto policies = formatter::CompilePolicies(style);
    const auto spacing =
        formatter::BuildSyntaxSpacing(tree, safety, policies, file, scratch);
    const auto* record = tree.Root().FirstChild->FirstChild->NextSibling;
    ASSERT_EQ(record->Kind, parser::NodeKind::RecordDeclaration);
    size_t separators = 0;
    formatter::GapId afterComma = 0;
    for (parser::TokenIndex i = 0; i < tree.TokenCount(); ++i) {
        if (tree.Kind(i) != tokenizer::TokenKind::Operator_Comma) {
            continue;
        }
        ++separators;
        afterComma = i + 1;
        EXPECT_EQ(tree.Role(i), parser::PunctuationRole::ListSeparator);
        const auto& gap = spacing.Gaps()[i + 1];
        EXPECT_EQ(gap.Field, formatter::SpacingField::CommaSpaceAfter);
        EXPECT_EQ(gap.Policy, formatter::GapPolicy::Space);
        EXPECT_EQ(gap.Category, formatter::SpacingOwner::TemplateArguments);
        ASSERT_NE(gap.Owner, formatter::NoRegion);
        const auto& owner = safety.Regions()[gap.Owner];
        EXPECT_EQ(owner.Kind, parser::NodeKind::TemplateArgumentList);
        EXPECT_EQ(owner.Tokens.Begin, record->FirstChild->Tokens.Begin);
        EXPECT_EQ(owner.Tokens.End, record->FirstChild->Tokens.End);
        EXPECT_EQ(gap.Priority, 3);
        EXPECT_EQ(
            spacing.SpacePreference(
                i + 1,
                formatter::GapContext::Preserved,
                safety),
            true);
    }
    EXPECT_EQ(separators, 1u);
    style.Layout = formatter::LayoutMode::Preserve;
    style.CommaSpaceAfter = formatter::GapPolicy::GeneratedSpace;
    formatter::SyntaxLayoutOptions inserted;
    inserted.GeneratedAdjacencies = {&afterComma, 1};
    EXPECT_EQ(Lower(source, style).Text, source);
    EXPECT_EQ(
        Lower(source, style, inserted).Text,
        "template<> struct Pair<int, long> {};\n");
    style.Layout = formatter::LayoutMode::Canonical;
    style.CommaSpaceAfter = formatter::GapPolicy::Space;

    const auto comma = parser::SourceOffset(source.find(','));
    formatter::SourceAnalysisOptions protection;
    const parser::SourceRange selected[] = {{comma, comma + 1}};
    protection.EditScope = selected;
    EXPECT_EQ(Lower(source, style, {}, protection).Text, source);
    const parser::SourceRange insertion[] = {{comma + 1, comma + 2}};
    protection.EditScope = insertion;
    EXPECT_EQ(
        Lower(source, style, {}, protection).Text,
        "template<> struct Pair<int, long> {};\n");
    protection.EditScope = {};
    const parser::SourceRange immutable[] = {{comma, comma + 2}};
    protection.ImmutableRanges = immutable;
    const auto protectedOutput = Lower(source, style, {}, protection);
    EXPECT_NE(protectedOutput.Text.find("Pair<int,long>"), std::string::npos);

    const std::string_view macros[] = {"OPAQUE"};
    protection.ImmutableRanges = {};
    protection.WhitespaceSensitiveMacros = macros;
    const std::string macroInput =
        "template<> struct Pair<OPAQUE(a,   b),long> {};\n";
    const auto macroOutput = Lower(macroInput, style, {}, protection);
    EXPECT_NE(
        macroOutput.Text.find("Pair<OPAQUE(a,   b), long>"),
        std::string::npos);
    const auto begin = macroInput.find("OPAQUE");
    const auto end = macroInput.find(')', begin) + 1;
    size_t protectedBytes = 0;
    for (const auto& map : macroOutput.Map) {
        if (map.Source.Begin >= begin && map.Source.End <= end &&
            map.Source.Begin < map.Source.End)
        {
            EXPECT_TRUE(map.Exact);
            EXPECT_TRUE(map.Protected);
            protectedBytes += map.Source.End - map.Source.Begin;
        }
    }
    EXPECT_EQ(protectedBytes, end - begin);

    style.Layout = formatter::LayoutMode::Preserve;
    const std::pair<std::string_view, std::string_view> operands[] = {
        {"template<auto, auto> struct Pair;\n", "(1<2)"},
        {
            "template<auto, class> struct Wrap;\n"
                "template<class, auto> struct Pair;\n",
            "Wrap<(1>0),int>",
        },
    };
    for (const auto& [declarations, operand] : operands) {
        const auto head = "Pair<" + std::string(operand) + ",";
        const auto input = std::string(declarations) + "template<> struct " +
            head + "0> {};\n";
        SCOPED_TRACE(input);
        const auto outer = parser::SourceOffset(input.rfind(','));
        protection = {};
        const parser::SourceRange commaOnly[] = {{outer, outer + 1}};
        protection.EditScope = commaOnly;
        EXPECT_EQ(Lower(input, style, {}, protection).Text, input);
        const parser::SourceRange followingToken[] = {{outer + 1, outer + 2}};
        protection.EditScope = followingToken;
        const auto edited = Lower(input, style, {}, protection);
        EXPECT_EQ(
            edited.Text,
            std::string(declarations) + "template<> struct " + head +
            " 0> {};\n");
        protection.EditScope = {};
        const parser::SourceRange blocked[] = {{outer, outer + 2}};
        protection.ImmutableRanges = blocked;
        auto expected = input;
        const auto nestedComma = expected.find(",int>");
        if (nestedComma != std::string::npos) {
            expected.insert(nestedComma + 1, " ");
        }
        EXPECT_EQ(Lower(input, style, {}, protection).Text, expected);
    }

    protection = {};
    protection.WhitespaceSensitiveMacros = macros;
    const std::string comparisonMacro = "template<auto, class> struct Wrap;\n"
        "template<class, auto> struct Pair;\n"
        "template<> struct Pair<Wrap<(OPAQUE(1>0)),int>,0> {};\n";
    EXPECT_EQ(
        Lower(comparisonMacro, style, {}, protection).Text,
        "template<auto, class> struct Wrap;\n"
        "template<class, auto> struct Pair;\n"
        "template<> struct Pair<Wrap<(OPAQUE(1>0)), int>, 0> {};\n");

    protection = {};
    const std::string malformed = "template<auto,auto> struct Pair;\n"
        "template<> struct Pair<(1<2],0> {};\n";
    EXPECT_NE(
        Lower(malformed, style, {}, protection).Text.find("Pair<(1<2],0>"),
        std::string::npos);
}
