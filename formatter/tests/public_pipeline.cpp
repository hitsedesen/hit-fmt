#include <formatter/config.hpp>
#include <formatter/formatter.hpp>
#include <tests/golden.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace public_pipeline
{
namespace
{
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
        return { Storage.data(), Size };
    }
};
formatter::FormatResult FormatInput(std::string_view source,
    const formatter::PreparedFormat& prepared,
    formatter::FormatRequest request = { .SourceMap = true })
{
    Input input(source);
    return formatter::Format(input.View(), prepared, request);
}
formatter::Style Canonical()
{
    formatter::Style style;
    style.Layout = formatter::LayoutMode::Canonical;
    style.CommaSpaceAfter = formatter::GapPolicy::GeneratedSpace;
    style.CommaSpaceBefore = formatter::GapPolicy::GeneratedNoSpace;
    style.ControlBlockKeywordSpacing = formatter::GapPolicy::Space;
    return style;
}
void CheckIdentities(
    std::string_view source, const formatter::FormatResult& result)
{
    Input input(source);
    auto tree = parser::Parse(input.View(), [](parser::ParseError) {});
    ASSERT_EQ(result.OriginalTokens.size(), tree.TokenCount());
    for (parser::TokenIndex i = 0; i < tree.TokenCount(); ++i)
    {
        const auto map = result.OriginalTokens[i];
        if (map.Present)
            EXPECT_EQ(std::string_view(result.Text)
                          .substr(map.Output.Begin,
                              map.Output.End - map.Output.Begin),
                tree.TokenText(i))
                << i;
    }
    for (const auto& map : result.Map)
    {
        EXPECT_LE(map.Output.Begin, map.Output.End);
        EXPECT_LE(map.Output.End, result.Text.size());
        if (map.Exact && map.HasOriginal)
            EXPECT_EQ(std::string_view(result.Text)
                          .substr(map.Output.Begin,
                              map.Output.End - map.Output.Begin),
                source.substr(
                    map.Original.Begin, map.Original.End - map.Original.Begin));
        if (map.Action != formatter::NoStructuralAction)
        {
            ASSERT_LT(map.Action, result.StructuralOperations.size());
            EXPECT_LE(result.StructuralOperations[map.Action].Owner.End,
                tree.TokenCount());
        }
        if (map.OriginalToken != formatter::layout::NoIdentity)
        {
            ASSERT_LT(map.OriginalToken, result.OriginalTokens.size());
            EXPECT_TRUE(result.OriginalTokens[map.OriginalToken].Present);
            EXPECT_FALSE(map.Generated);
        }
    }
}

// Maps are evidence of exact preservation, not edit authorization. Every
// excluded original byte must survive once, even through moved/deleted pieces.
void CheckExcludedCopies(std::string_view source,
    const formatter::FormatResult& result,
    std::span<const parser::SourceRange> selected)
{
    std::vector<unsigned> copies(source.size(), 0);
    for (const auto& map : result.Map)
        if (map.Exact && map.HasOriginal)
            for (auto i = map.Original.Begin; i < map.Original.End; ++i)
            {
                ASSERT_LT(i, copies.size());
                ++copies[i];
            }
    size_t range = 0;
    for (size_t i = 0; i < source.size(); ++i)
    {
        while (range < selected.size() && selected[range].End <= i)
            ++range;
        if (range == selected.size() || i < selected[range].Begin)
            EXPECT_EQ(copies[i], 1u) << "excluded original byte " << i;
    }
}

TEST(PublicPipeline, StandaloneCommentCountsSupportOwningMapsAndScopedEdges)
{
    auto style = Canonical();
    style.BodyShape = formatter::BodyShape::Expanded;
    style.EmptyBodyShape = formatter::BodyShape::Expanded;
    style.StandaloneCommentBlankLinesBefore = {formatter::BlankLineMode::Exact, 1};
    style.StandaloneCommentBlankLinesAfter = {formatter::BlankLineMode::Exact, 1};
    const formatter::PreparedFormat prepared(style);
    const std::string source = "namespace N {\n// note\nint x;\n// end\n}\n";
    const auto result = FormatInput(source, prepared);
    EXPECT_NE(result.Text.find("{\n\n// note\n\n"), std::string::npos);
    EXPECT_NE(result.Text.find("x;\n\n// end\n\n}"), std::string::npos);
    CheckIdentities(source, result);
    EXPECT_EQ(FormatInput(result.Text, prepared).Text, result.Text);
    Input input(source);
    auto tree = parser::Parse(input.View(), [](parser::ParseError) {});
    EXPECT_EQ(formatter::Format(tree, prepared).Text, result.Text);

    const auto comment = parser::SourceOffset(source.find("// note"));
    const std::array selected{parser::SourceRange{0, comment}};
    formatter::FormatRequest request{.SourceMap = true};
    request.EditScope = selected;
    const auto limited = FormatInput(source, prepared, request);
    EXPECT_EQ(limited.Text, source);
    CheckExcludedCopies(source, limited, selected);
    const std::array immutable{parser::SourceRange{comment - 1,
        parser::SourceOffset(source.find("int x"))}};
    request.EditScope = {};
    request.ImmutableRanges = immutable;
    const auto protectedResult = FormatInput(source, prepared, request);
    EXPECT_NE(protectedResult.Text.find("{\n// note\nint x;"), std::string::npos);
    CheckIdentities(source, protectedResult);

    style.MaxEmptyLines = 0;
    EXPECT_THROW((void)formatter::PreparedFormat(style), std::invalid_argument);
}

#ifdef HIT_FMT_PIPELINE_CHECKS
// The full test target registers these fixtures in its existing FormatTest.
class FormatTest : public testing::TestWithParam<const char*>
{
};
TEST_P(FormatTest, Golden)
{
    const auto relative = std::filesystem::path(GetParam());
    const test::golden::Fixture fixture {
        std::filesystem::path(__FILE__).parent_path() / "testdata" /
            relative.parent_path(),
        relative.filename().string()
    };
    auto style
        = formatter::LoadConfigFile(fixture.Named(".hit-fmt")).FormattingStyle;
    const formatter::PreparedFormat prepared(style);
    const auto source = fixture.InputNamed(".input.cpp");
    auto result = FormatInput(source, prepared);
    fixture.Expect(result.Text);
    CheckIdentities(source, result);
    EXPECT_EQ(FormatInput(result.Text, prepared).Text, result.Text);
}
INSTANTIATE_TEST_SUITE_P(Formatter, FormatTest,
    testing::Values("public-pipeline/layout", "public-pipeline/structural",
        "public-pipeline/comments", "public-pipeline/enumerator-assignment",
        "group-syntax/nested",
        "group-syntax/messages"),
    [](const auto& info)
    {
        const auto path = std::filesystem::path(info.param);
        auto name
            = path.parent_path().string() + "_" + path.filename().string();
        std::replace(name.begin(), name.end(), '-', '_');
        return name;
    });
#endif

TEST(PublicPipeline, FinalNamespaceWidthDiagnosticsGolden)
{
    const test::golden::Fixture fixture {
        std::filesystem::path(__FILE__).parent_path() /
            "testdata/public-pipeline",
        "comments"
    };
    const formatter::PreparedFormat prepared(
        formatter::LoadConfigFile(fixture.Named(".hit-fmt")).FormattingStyle);
    const auto result = FormatInput(fixture.InputNamed(".input.cpp"), prepared);
    std::string diagnostics;
    for (const auto& diagnostic : result.Diagnostics)
    {
        diagnostics += std::to_string(diagnostic.Position.Line) + ":" +
            std::to_string(diagnostic.Position.Column) + " " +
            diagnostic.Message + "\n";
    }
    fixture.Expect(diagnostics, ".diagnostics.txt");
}

TEST(PublicPipeline, DeclarationFallbackRequiresRebuiltOwnerAndEditableGap)
{
    const std::string source = "template<class T> struct Box {};\n"
        "Box<int>  make(){return {};}\n"
        "Box<int>  flat();\n";
    auto style = formatter::Style{};
    style.DeclarationHeadersLayout = formatter::CategoryLayout::Canonical;
    const formatter::PreparedFormat prepared(style);
    const auto gapBegin = uint32_t(source.find("  make"));
    const auto gapEnd = gapBegin + 2;
    const auto result = FormatInput(source, prepared);
    EXPECT_NE(result.Text.find("Box<int> make()"), std::string::npos);
    EXPECT_NE(result.Text.find("Box<int>  flat()"), std::string::npos);
    CheckIdentities(source, result);
    EXPECT_EQ(FormatInput(result.Text, prepared).Text, result.Text);

    const std::array<parser::SourceRange, 1> selected{{{gapBegin, gapEnd}}};
    const auto scoped = FormatInput(source, prepared,
        {.SourceMap = true, .EditScope = selected});
    EXPECT_NE(scoped.Text.find("Box<int> make()"), std::string::npos);
    CheckIdentities(source, scoped);
    CheckExcludedCopies(source, scoped, selected);
    const std::array<parser::SourceRange, 1> partial{{{gapBegin, gapEnd - 1}}};
    const auto unownedEnd = FormatInput(source, prepared,
        {.SourceMap = true, .EditScope = partial});
    EXPECT_NE(unownedEnd.Text.find("Box<int>  make()"), std::string::npos);
    CheckExcludedCopies(source, unownedEnd, partial);
    const auto protectedGap = FormatInput(source, prepared,
        {.ImmutableRanges = selected, .SourceMap = true});
    EXPECT_NE(protectedGap.Text.find("Box<int>  make()"), std::string::npos);
    CheckIdentities(source, protectedGap);

    style.DeclarationHeadersLayout = formatter::CategoryLayout::Preserve;
    style.Layout = formatter::LayoutMode::Canonical;
    const auto preserved = FormatInput(source, formatter::PreparedFormat(style));
    EXPECT_NE(preserved.Text.find("Box<int>  make()"), std::string::npos);
}

TEST(PublicPipeline, NoEditTreeSourceParityAndResultLifetime)
{
    const std::string source = "void f(){ invoke(left,right); }\n";
    formatter::FormatResult result;
    {
        auto style = Canonical();
        const formatter::PreparedFormat prepared(style);
        style.CommaSpaceAfter = formatter::GapPolicy::NoSpace;
        Input input(source);
        auto tree = parser::Parse(input.View(), [](parser::ParseError) {});
        std::string path = "missing/directory with spaces/original.cpp";
        const formatter::FormatRequest request{
            .SourceMap = true, .InputFilePath = path};
        result = formatter::Format(tree, prepared, request);
        const auto fromSource = formatter::Format(input.View(), prepared, request);
        EXPECT_EQ(result.Text, fromSource.Text);
        CheckIdentities(source, fromSource);
        EXPECT_EQ(result.Text, formatter::Format(input.View(), prepared).Text);
        path.assign(path.size(), '?');
        EXPECT_EQ(result.Text, formatter::Format(tree, Canonical()));
        EXPECT_EQ(result.Text, formatter::Format(input.View(), Canonical()));
    }
    EXPECT_NE(result.Text.find("left, right"), std::string::npos);
    CheckIdentities(source, result);
    for (const auto& token : result.OriginalTokens)
        EXPECT_TRUE(token.Present);

    // Inactive Stage S metadata must not activate a consumer or change the
    // composed constructor/head rule, scope authority, maps or lifetimes.
    const auto composed = test::golden::ReadFile(
        std::filesystem::path(__FILE__).parent_path() /
        "testdata/record-head-commas/constructor-composition.input.cpp");
    const auto point = parser::SourceOffset(composed.find(">,2>") + 2);
    const std::array<parser::SourceRange, 1> selected{{{point, point + 1}}};
    formatter::Style defaults;
    defaults.Layout = formatter::LayoutMode::Preserve;
    defaults.CommaSpaceAfter = formatter::GapPolicy::Space;
    const auto baseline = FormatInput(composed, formatter::PreparedFormat(defaults),
        {.SourceMap = true, .EditScope = selected});
    formatter::FormatResult withMetadata;
    {
        auto style = defaults;
        style.IncludeGroupPrefixes = {"engine/", "engine/detail/"};
        const formatter::PreparedFormat prepared(style);
        style.IncludeGroupPrefixes.clear();
        style.ColumnLimit = 0; // Mutating Style cannot affect prepared scalars.
        Input input(composed);
        auto tree = parser::Parse(input.View(), [](parser::ParseError) {});
        std::string path = "not/a/real/source/location/record.cpp";
        withMetadata = formatter::Format(tree, prepared,
            {.SourceMap = true, .EditScope = selected, .InputFilePath = path});
        EXPECT_EQ(withMetadata.Text, FormatInput(composed, prepared,
            {.SourceMap = true, .EditScope = selected, .InputFilePath = path}).Text);
        const std::array<parser::SourceRange, 1> immutable{{{point - 1, point + 1}}};
        const auto protectedResult = FormatInput(composed, prepared,
            {.ImmutableRanges = immutable, .SourceMap = true, .EditScope = selected,
                .InputFilePath = path});
        path.assign(path.size(), '?');
        EXPECT_EQ(protectedResult.Text, composed);
        CheckIdentities(composed, protectedResult);
        CheckExcludedCopies(composed, protectedResult, selected);
    }
    auto expected = composed;
    expected.insert(point, " ");
    EXPECT_EQ(baseline.Text, expected);
    EXPECT_EQ(withMetadata.Text, baseline.Text);
    EXPECT_TRUE(withMetadata.StructuralOperations.empty());
    CheckIdentities(composed, withMetadata);
    CheckExcludedCopies(composed, withMetadata, selected);
    for (const auto& token : withMetadata.OriginalTokens)
        EXPECT_TRUE(token.Present);
}

TEST(
    PublicPipeline, StructuralPermutationInsertionDeletionAndOriginalExclusions)
{
    auto style = Canonical();
    style.AddMissingControlBraces = true;
    style.RemoveNullStatements = true;
    style.RemoveNullDeclarations = true;
    style.IncludeOrder = formatter::IncludeOrder::Sort;
    const formatter::PreparedFormat prepared(style);
    const std::string source
        = "#include \"z.h\"\n#include \"a.h\"\n;\nvoid f(){ ; if(ok) "
          "invoke(a,b); }\n";
    auto result = FormatInput(source, prepared);
    EXPECT_TRUE(
        result.Text.starts_with("#include \"a.h\"\n#include \"z.h\"\n"));
    EXPECT_NE(result.Text.find("if (ok) {"), std::string::npos);
    CheckIdentities(source, result);
    bool deleted = false, generated = false, directive = false;
    for (const auto& token : result.OriginalTokens)
        deleted |= !token.Present;
    for (const auto& map : result.Map)
        generated |=
            map.Generated && map.Action != formatter::NoStructuralAction;
    for (const auto& range : result.OutputProtection)
        directive |= range.Reasons == formatter::ProtectionReason::Directive;
    EXPECT_TRUE(deleted);
    EXPECT_TRUE(generated);
    EXPECT_TRUE(directive);
    EXPECT_EQ(FormatInput(result.Text, prepared).Text, result.Text);
    const std::array<parser::SourceRange, 1> excluded {
        { { 0, uint32_t(source.find(';')) } }
    };
    auto constrained = FormatInput(
        source, prepared, { .ImmutableRanges = excluded, .SourceMap = true });
    EXPECT_TRUE(
        constrained.Text.starts_with(source.substr(0, excluded[0].End)));
    EXPECT_NE(constrained.Text.find("if (ok) {"), std::string::npos);
    CheckIdentities(source, constrained);
}

TEST(PublicPipeline, ProtectedBytesSurviveBothBranchesAndCRLF)
{
    const std::string source
        = "// hit-fmt off\r\nvoid locked( ){  invoke( x,y ); }\r\n// "
          "hit-fmt on\r\n"
          "void f(){ KEEP(  x, y ); if(ok) invoke(a,b); }\r\n";
    for (bool structural : { false, true })
    {
        auto style = Canonical();
        style.AddMissingControlBraces = structural;
        style.WhitespaceSensitiveMacros = { "KEEP" };
        const formatter::PreparedFormat prepared(style);
        auto result = FormatInput(source, prepared);
        EXPECT_TRUE(
            result.Text.starts_with(source.substr(0, source.find("void f"))));
        EXPECT_NE(result.Text.find("KEEP(  x, y )"), std::string::npos);
        CheckIdentities(source, result);
        for (size_t i = 0; i < result.Text.size(); ++i)
            if (result.Text[i] == '\n')
            {
                ASSERT_GT(i, 0u);
                EXPECT_EQ(result.Text[i - 1], '\r');
            }
        EXPECT_EQ(FormatInput(result.Text, prepared).Text, result.Text);
    }
}

TEST(PublicPipeline, EnumeratorAssignmentExplicitSpacing)
{
    const std::string source = "enum class Mode { First=1, Second =2 };\n";
    for (const auto mode : {formatter::LayoutMode::Preserve,
        formatter::LayoutMode::Canonical})
    {
        auto style = Canonical();
        style.Layout = mode;
        for (const auto policy : {formatter::GapPolicy::Space,
            formatter::GapPolicy::GeneratedSpace})
        {
            style.AssignmentOperatorSpacing = policy;
            const formatter::PreparedFormat prepared(style);
            const auto result = FormatInput(source, prepared);
            if (policy == formatter::GapPolicy::GeneratedSpace &&
                mode == formatter::LayoutMode::Preserve)
            {
                EXPECT_EQ(result.Text, source);
            } else {
                EXPECT_NE(result.Text.find("First = 1"), std::string::npos)
                    << result.Text;
                EXPECT_NE(result.Text.find("Second = 2"), std::string::npos)
                    << result.Text;
            }
            CheckIdentities(source, result);
            EXPECT_EQ(FormatInput(result.Text, prepared).Text, result.Text);
        }
    }
}

TEST(PublicPipeline, EnumeratorAssignmentPoliciesAndProtection)
{
    const std::string source = "enum Mode { First  =  1, Second=2 };\n";
    for (const auto policy : {formatter::GapPolicy::Space,
        formatter::GapPolicy::NoSpace, formatter::GapPolicy::Preserve,
        formatter::GapPolicy::GeneratedSpace,
        formatter::GapPolicy::GeneratedNoSpace})
    {
        formatter::Style style;
        style.AssignmentOperatorSpacing = policy;
        const formatter::PreparedFormat prepared(style);
        const auto result = FormatInput(source, prepared);
        const auto expected = policy == formatter::GapPolicy::Space
            ? "enum Mode { First = 1, Second = 2 };\n"
            : policy == formatter::GapPolicy::NoSpace
                ? "enum Mode { First=1, Second=2 };\n"
                : source;
        EXPECT_EQ(result.Text, expected);
        CheckIdentities(source, result);
    }
    formatter::Style style;
    style.AssignmentOperatorSpacing = formatter::GapPolicy::Space;
    const formatter::PreparedFormat prepared(style);
    const auto second = uint32_t(source.find("Second"));
    const std::array<parser::SourceRange, 1> selected{{{0, second}}};
    const auto scoped = FormatInput(source, prepared,
        {.SourceMap = true, .EditScope = selected});
    EXPECT_EQ(scoped.Text, "enum Mode { First = 1, Second=2 };\n");
    CheckIdentities(source, scoped);
    CheckExcludedCopies(source, scoped, selected);
    const std::array<parser::SourceRange, 1> immutable{{{0, second}}};
    const auto locked = FormatInput(source, prepared,
        {.ImmutableRanges = immutable, .SourceMap = true});
    EXPECT_EQ(locked.Text, "enum Mode { First  =  1, Second = 2 };\n");
    CheckIdentities(source, locked);
    const auto gap = uint32_t(source.find("  ="));
    const std::array<parser::SourceRange, 1> partial{{{gap, gap + 1}}};
    const auto denied = FormatInput(source, prepared,
        {.SourceMap = true, .EditScope = partial});
    EXPECT_EQ(denied.Text, source);
    CheckExcludedCopies(source, denied, partial);
}

TEST(PublicPipeline, EnumeratorAssignmentCommentsAndLineBoundaries)
{
    const std::string source = "enum Mode {\n"
        "    First=  // note\n"
        "        1,\n"
        "    Second=/* value */2,\n"
        "};\n"
        "int count=  // control\n"
        "    1;\n";
    formatter::Style style;
    style.AssignmentOperatorSpacing = formatter::GapPolicy::Space;
    const formatter::PreparedFormat prepared(style);
    const auto result = FormatInput(source, prepared);
    EXPECT_EQ(result.Text, "enum Mode {\n"
        "    First =  // note\n"
        "        1,\n"
        "    Second = /* value */2,\n"
        "};\n"
        "int count =  // control\n"
        "    1;\n");
    for (const auto comment : {"// note", "// control"}) {
        const auto end = uint32_t(source.find(comment));
        const auto begin = end - 2;
        EXPECT_TRUE(std::ranges::any_of(result.Map,
            [=](const auto& map) {
                return map.Exact && map.HasOriginal &&
                    map.Original.Begin <= begin && map.Original.End >= end;
            }));
    }
    CheckIdentities(source, result);
    EXPECT_EQ(FormatInput(result.Text, prepared).Text, result.Text);
    const std::string protectedSource = "// clang-format off\n" + source +
        "// clang-format on\n";
    const auto locked = FormatInput(protectedSource, prepared);
    EXPECT_EQ(locked.Text, protectedSource);
    CheckIdentities(protectedSource, locked);
}

TEST(PublicPipeline, GeneratedAdjacenciesUnderPreserveAndEmptyControlBodies)
{
    formatter::Style style;
    style.AddMissingControlBraces = true;
    style.EmptyControlBody = formatter::EmptyControlBody::EmptyBlock;
    style.ControlBlockSpaceBeforeBrace = formatter::GapPolicy::GeneratedSpace;
    style.EmptyBlockInteriorSpacing = formatter::GapPolicy::GeneratedNoSpace;
    const formatter::PreparedFormat prepared(style);
    const std::string source = "void f(){ if(ok)invoke(); while(wait); }\n";
    auto result = FormatInput(source, prepared);
    EXPECT_NE(result.Text.find("if(ok) {"), std::string::npos);
    EXPECT_NE(result.Text.find("while(wait) {}"), std::string::npos);
    CheckIdentities(source, result);
    EXPECT_EQ(FormatInput(result.Text, prepared).Text, result.Text);
}

TEST(PublicPipeline,
    NamespaceCommentsHaveFinalCoordinatesAndDoNotSwallowFollowingText)
{
    for (auto policy : { formatter::NamespaceComments::AddMissing,
             formatter::NamespaceComments::Fix,
             formatter::NamespaceComments::Remove })
    {
        auto style = Canonical();
        style.NamespaceComments = policy;
        style.NamespaceCommentSpaces = 3;
        const formatter::PreparedFormat prepared(style);
        const std::string source
            = policy == formatter::NamespaceComments::AddMissing
            ? "namespace Name { int value; }\nint "
                "following;\n#define RAW(x) x "
              "## x\n"
            : "namespace Name { int value; } // namespace Wrong\nint "
              "following;\n#define RAW(x) x ## x\n";
        auto result = FormatInput(source, prepared);
        CheckIdentities(source, result);
        EXPECT_NE(result.Text.find("int following;"), std::string::npos);
        if (policy == formatter::NamespaceComments::Remove)
            EXPECT_EQ(result.Text.find("// namespace"), std::string::npos);
        else
            EXPECT_NE(
                result.Text.find("}   // namespace Name"), std::string::npos);
        EXPECT_EQ(FormatInput(result.Text, prepared).Text, result.Text);
        std::string crlf;
        for (const auto c : source)
        {
            if (c == '\n')
                crlf += '\r';
            crlf += c;
        }
        const auto windows = FormatInput(crlf, prepared);
        CheckIdentities(crlf, windows);
        bool protectedDirective = false;
        for (const auto& range : windows.OutputProtection)
        {
            if (range.Reasons == formatter::ProtectionReason::Directive)
            {
                protectedDirective = true;
                EXPECT_EQ(std::string_view(windows.Text)
                              .substr(range.Source.Begin,
                                  range.Source.End - range.Source.Begin),
                    "#define RAW(x) x ## x\r\n");
            }
        }
        EXPECT_TRUE(protectedDirective);
        EXPECT_EQ(FormatInput(windows.Text, prepared).Text, windows.Text);
        EXPECT_NE(windows.Text.find("\r\nint following;"), std::string::npos);
    }
    auto style = Canonical();
    style.NamespaceComments = formatter::NamespaceComments::AddMissing;
    const formatter::PreparedFormat prepared(style);
    auto result = FormatInput("namespace Name {} int following;\n", prepared);
    CheckIdentities("namespace Name {} int following;\n", result);
    // Canonical bodies may place the following declaration safely on a new
    // line.
    const auto comment = result.Text.find("// namespace");
    if (comment != std::string::npos)
        EXPECT_LT(
            result.Text.find('\n', comment), result.Text.find("int following"));
}

TEST(PublicPipeline, ImmutableRangesConstrainNamespaceAndStructuralPlanning)
{
    auto style = Canonical();
    style.NamespaceComments = formatter::NamespaceComments::AddMissing;
    style.AddMissingControlBraces = true;
    const formatter::PreparedFormat prepared(style);
    const std::string source
        = "namespace Name { void f(){ if(ok)invoke(a,b); } }\nint tail;\n";
    const std::array<parser::SourceRange, 1> immutable {
        { { 0, uint32_t(source.find('\n') + 1) } }
    };
    for (const bool sourceMap : { false, true })
    {
        SCOPED_TRACE(sourceMap);
        const auto result = FormatInput(source, prepared,
            { .ImmutableRanges = immutable, .SourceMap = sourceMap });
        EXPECT_TRUE(
            result.Text.starts_with(source.substr(0, immutable[0].End)));
        if (sourceMap)
            CheckIdentities(source, result);
        EXPECT_EQ(result.StructuralDiagnostics.size(), 2u);
        EXPECT_EQ(std::count_if(result.StructuralDiagnostics.begin(),
                      result.StructuralDiagnostics.end(),
                      [](const auto& diagnostic)
                      {
                          return diagnostic.Action ==
                              formatter::StructuralAction::NamespaceComments;
                      }),
            1);
        EXPECT_EQ(
            std::count_if(result.StructuralDiagnostics.begin(),
                result.StructuralDiagnostics.end(),
                [](const auto& diagnostic)
                {
                    return diagnostic.Action ==
                        formatter::StructuralAction::AddMissingControlBraces;
                }),
            1);
        for (const auto& diagnostic : result.StructuralDiagnostics)
            EXPECT_EQ(diagnostic.Reason, formatter::StructuralSkip::Protected);
    }
}

TEST(PublicPipeline, InvalidPreparedRequestsAndActualEmittedWidthDiagnostics)
{
    auto style = Canonical();
    style.ColumnLimit = 0;
    EXPECT_THROW(formatter::PreparedFormat { style }, std::invalid_argument);
    style = Canonical();
    style.CallLikeMacroNames = { "CALL_HINT" };
    EXPECT_THROW(formatter::PreparedFormat { style }, std::invalid_argument);
    style = Canonical();
    style.AmbiguousTemplateDeclarationSpacing = formatter::GapPolicy::Space;
    EXPECT_THROW(formatter::PreparedFormat { style }, std::invalid_argument);
    style = Canonical();
    style.ColumnLimit = 8;
    style.WidthDiagnostics = formatter::WidthDiagnosticPolicy::Report;
    const formatter::PreparedFormat prepared(style);
    const auto result = FormatInput(
        "// hit-fmt off\nint unavoidable_long_name;\n// hit-fmt on\n",
        prepared);
    ASSERT_FALSE(result.Diagnostics.empty());
    EXPECT_EQ(result.Diagnostics[1].Position.Line, 2u);
    EXPECT_EQ(
        result.Diagnostics[1].Kind, formatter::LayoutDiagnosticKind::LineWidth);
    const std::array<parser::SourceRange, 1> bad { { { 1, 1000 } } };
    EXPECT_THROW(FormatInput("int x;", prepared, { .ImmutableRanges = bad }),
        std::invalid_argument);
    auto noMap = FormatInput("int x;\n", prepared, {});
    EXPECT_TRUE(noMap.Map.empty());
    EXPECT_TRUE(noMap.OriginalTokens.empty());
}
TEST(PublicPipeline,
    LayoutGeneratedCommaAnchorComposesThroughStructuralNormalization)
{
    auto style = Canonical();
    style.ColumnLimit = 16;
    style.InitializersTrailingComma =
        formatter::TrailingCommaPolicy::WhenMultiline;
    style.InitializersCloser = formatter::CloserPlacement::OwnLine;
    style.AddMissingControlBraces = true;
    style.IncludeOrder = formatter::IncludeOrder::Sort;
    const formatter::PreparedFormat prepared(style);
    const std::string source
        = "#include \"z.h\"\n#include \"a.h\"\nvoid f(){ if(ok)invoke(); "
          "}\nint values[]={alpha,beta};\n";
    const auto result = FormatInput(source, prepared);
    CheckIdentities(source, result);
    const auto comma = result.Text.find("beta,") + 4;
    ASSERT_NE(comma, std::string::npos);
    bool found = false;
    for (const auto& map : result.Map)
        if (map.Output.Begin == comma && map.Output.End == comma + 1)
        {
            found = true;
            EXPECT_TRUE(map.Generated);
            EXPECT_FALSE(map.Exact);
            EXPECT_EQ(map.OriginalToken, formatter::layout::NoIdentity);
            EXPECT_TRUE(map.HasOriginal);
            EXPECT_EQ(map.Original.Begin, source.find("beta") + 4);
            EXPECT_EQ(map.Original.End, map.Original.Begin);
        }
    EXPECT_TRUE(found);
    Input input(source);
    const auto tree = parser::Parse(input.View(), [](parser::ParseError) {});
    const auto treeResult
        = formatter::Format(tree, prepared, { .SourceMap = true });
    EXPECT_EQ(treeResult.Text, result.Text);
    ASSERT_EQ(treeResult.Map.size(), result.Map.size());
    for (size_t i = 0; i < result.Map.size(); ++i)
    {
        EXPECT_EQ(treeResult.Map[i].Output.Begin, result.Map[i].Output.Begin);
        EXPECT_EQ(
            treeResult.Map[i].Original.Begin, result.Map[i].Original.Begin);
        EXPECT_EQ(treeResult.Map[i].OriginalToken, result.Map[i].OriginalToken);
    }
    EXPECT_EQ(FormatInput(result.Text, prepared).Text, result.Text);
}

TEST(PublicPipeline, BorrowedIndentMappingIsAnAssociationNotAnEdit)
{
    formatter::Style style;
    style.NamespaceBlankLinesAfterOpen = { formatter::BlankLineMode::Exact, 1 };
    const formatter::PreparedFormat prepared(style);
    const std::string source = "namespace Demo {\r\n\tint value;\r\n}\r\n";
    const auto result = FormatInput(source, prepared);
    CheckIdentities(source, result);
    const auto indent = uint32_t(source.find('\t'));
    bool split = false;
    for (size_t i = 0; i + 1 < result.Map.size(); ++i)
    {
        const auto& prefix = result.Map[i];
        const auto& borrowed = result.Map[i + 1];
        if (prefix.HasOriginal && prefix.Original.Begin == indent - 2 &&
            prefix.Original.End == indent + 1 && borrowed.HasOriginal &&
            borrowed.Original.Begin == indent &&
            borrowed.Original.End == indent + 1)
        {
            split = true;
            EXPECT_FALSE(prefix.Exact);
            EXPECT_TRUE(prefix.Generated);
            EXPECT_TRUE(borrowed.Exact);
            EXPECT_FALSE(borrowed.Generated);
            EXPECT_EQ(prefix.Output.End, borrowed.Output.Begin);
            EXPECT_EQ(std::string_view(result.Text)
                          .substr(borrowed.Output.Begin,
                              borrowed.Output.End - borrowed.Output.Begin),
                "\t");
        }
    }
    EXPECT_TRUE(split);
    EXPECT_EQ(FormatInput(result.Text, prepared).Text, result.Text);
}

TEST(PublicPipeline, NamespaceFollowingTextSkipAndClangDisabledProtection)
{
    formatter::Style style;
    style.NamespaceComments = formatter::NamespaceComments::AddMissing;
    const formatter::PreparedFormat prepared(style);
    const std::string source = "namespace Name {} int following;\n";
    const auto result = FormatInput(source, prepared);
    EXPECT_EQ(result.Text, source);
    ASSERT_FALSE(result.StructuralDiagnostics.empty());
    EXPECT_EQ(result.StructuralDiagnostics.back().Reason,
        formatter::StructuralSkip::FollowingText);
    CheckIdentities(source, result);
    style = Canonical();
    style.AddMissingControlBraces = true;
    const std::string disabled
        = "// clang-format off\nvoid f(){ if(ok)invoke(a,b); }\n// "
          "clang-format on\n";
    const auto protectedResult
        = FormatInput(disabled, formatter::PreparedFormat(style));
    EXPECT_EQ(protectedResult.Text, disabled);
    CheckIdentities(disabled, protectedResult);
    style.HonorClangFormatDirectives = false;
    EXPECT_NE(
        FormatInput(disabled, formatter::PreparedFormat(style)).Text, disabled);
}

TEST(PublicPipeline, EmptyAndRecoveryInputRemainLocalWithoutFallback)
{
    const formatter::PreparedFormat prepared(Canonical());
    const auto empty = FormatInput("", prepared);
    EXPECT_TRUE(empty.Text.empty());
    EXPECT_TRUE(empty.OriginalTokens.empty());
    const std::string source
        = "void broken(){ invoke(a,,b); }\nvoid safe(){ invoke(x,y); }\n";
    const auto result = FormatInput(source, prepared);
    EXPECT_NE(result.Text.find("invoke(a,,b)"), std::string::npos);
    EXPECT_NE(result.Text.find("invoke(x, y)"), std::string::npos);
    CheckIdentities(source, result);
    EXPECT_EQ(FormatInput(result.Text, prepared).Text, result.Text);
}

// Scope tests use whole eligible gaps and identity/protection properties. The
// partial-indentation consumer is a separate lane; no frozen-indent golden is
// an expectation of this public projection seam.
TEST(PublicPipeline, EditScopeAbsenceEmptyFullPartialAndLifetime)
{
    const std::string source
        = "void first(){ if(ok)invoke(a,b); }\n"
          "void locked( ){  invoke( x,y ); }\n";
    for (const bool structural : { false, true })
    {
        auto style = Canonical();
        style.AddMissingControlBraces = structural;
        const formatter::PreparedFormat prepared(style);
        const auto unrestricted = FormatInput(source, prepared);
        const std::span<const parser::SourceRange> empty;
        const auto denied = FormatInput(source, prepared,
            { .SourceMap = true, .EditScope = empty });
        EXPECT_EQ(denied.Text, source);
        CheckIdentities(source, denied);
        CheckExcludedCopies(source, denied, empty);
        const std::array<parser::SourceRange, 1> full {
            { { 0, uint32_t(source.size()) } }
        };
        EXPECT_EQ(FormatInput(source, prepared,
                      { .SourceMap = true, .EditScope = full }).Text,
            unrestricted.Text);
        formatter::FormatResult selected;
        const auto tail = uint32_t(source.find("void locked"));
        {
            const std::array<parser::SourceRange, 2> touching {
                { { 0, 5 }, { 5, tail } }
            };
            Input input(source);
            auto tree = parser::Parse(input.View(), [](parser::ParseError) {});
            selected = formatter::Format(tree, prepared,
                { .SourceMap = true, .EditScope = touching });
            EXPECT_EQ(selected.Text,
                formatter::Format(input.View(), prepared,
                    { .EditScope = touching }).Text);
        }
        EXPECT_TRUE(selected.Text.ends_with(source.substr(tail)));
        EXPECT_NE(selected.Text.find("invoke(a, b)"), std::string::npos);
        if (structural)
            EXPECT_NE(selected.Text.find("if (ok) {"), std::string::npos);
        CheckIdentities(source, selected);
        const std::array<parser::SourceRange, 1> selection { { { 0, tail } } };
        CheckExcludedCopies(source, selected, selection);
        bool outside = false;
        for (const auto& range : selected.OutputProtection)
            outside |= (uint16_t(range.Reasons) &
                           uint16_t(formatter::ProtectionReason::
                               OutsideEditScope)) !=
                0;
        EXPECT_TRUE(outside);
    }
}

TEST(PublicPipeline, EditScopeProjectsPermutationGeneratedBytesAndReasonUnions)
{
    auto style = Canonical();
    style.AddMissingControlBraces = true;
    style.RemoveNullDeclarations = true;
    style.IncludeOrder = formatter::IncludeOrder::Sort;
    style.WhitespaceSensitiveMacros = { "KEEP" };
    const formatter::PreparedFormat prepared(style);
    const std::string source
        = "#include \"zzzz.h\"\n#include \"a.h\"\n;\n"
          "void selected(){ if(ok)invoke(a,b); KEEP(  x,y ); }\n"
          "void locked( ){  invoke( x,y ); }\n";
    const auto tail = uint32_t(source.find("void locked"));
    const auto sensitive = uint32_t(source.find("KEEP("));
    const std::array<parser::SourceRange, 1> scope { { { 0, tail } } };
    const std::array<parser::SourceRange, 1> immutable {
        { { sensitive, sensitive + 13 } }
    };
    const auto result = FormatInput(source, prepared,
        { .ImmutableRanges = immutable, .SourceMap = true, .EditScope =
            scope });
    EXPECT_TRUE(result.Text.starts_with(
        "#include \"a.h\"\n#include \"zzzz.h\"\n"));
    EXPECT_TRUE(result.Text.ends_with(source.substr(tail)));
    EXPECT_NE(result.Text.find("KEEP(  x,y )"), std::string::npos);
    CheckIdentities(source, result);
    CheckExcludedCopies(source, result, scope);
    bool moved = false, literal = false, deleted = false, unioned = false;
    for (const auto& token : result.OriginalTokens)
        deleted |= !token.Present;
    for (const auto& map : result.Map)
    {
        if (map.Action == formatter::NoStructuralAction)
            continue;
        const auto kind = result.StructuralOperations[map.Action].Kind;
        moved |= kind == formatter::StructuralAction::IncludeOrder && map.Exact;
        literal |= kind == formatter::StructuralAction::
            AddMissingControlBraces &&
            map.Generated && !map.Exact &&
            map.OriginalToken == formatter::layout::NoIdentity;
    }
    for (const auto& range : result.OutputProtection)
    {
        const auto reasons = uint16_t(range.Reasons);
        unioned |= (reasons & uint16_t(formatter::ProtectionReason::
            Explicit)) &&
            (reasons & uint16_t(formatter::ProtectionReason::SensitiveMacro));
    }
    EXPECT_TRUE(moved);
    EXPECT_TRUE(literal);
    EXPECT_TRUE(deleted);
    EXPECT_TRUE(unioned);
    // A partly selected include group must still skip atomically; neighboring
    // directive/source associations cannot authorize the other include.
    const std::array<parser::SourceRange, 1> partial {
        { { 0, uint32_t(source.find('\n') + 1) } }
    };
    EXPECT_EQ(FormatInput(source, prepared,
                  { .SourceMap = true, .EditScope = partial }).Text,
        source);
}

TEST(PublicPipeline, EditScopeDeletionAndReplacementDoNotOwnTheirEnds)
{
    formatter::Style style;
    style.RemoveNullDeclarations = true;
    style.EmptyControlBody = formatter::EmptyControlBody::EmptyBlock;
    style.EmptyBlockInteriorSpacing = formatter::GapPolicy::GeneratedNoSpace;
    const formatter::PreparedFormat prepared(style);
    const std::array<parser::SourceRange, 1> deleted { { { 0, 1 } } };
    const std::string source = ";  int x;\n";
    const auto result = FormatInput(source, prepared,
        { .SourceMap = true, .EditScope = deleted });
    EXPECT_EQ(result.Text, "  int x;\n");
    CheckIdentities(source, result);
    ASSERT_FALSE(result.OriginalTokens.empty());
    EXPECT_FALSE(result.OriginalTokens[0].Present);
    CheckExcludedCopies(source, result, deleted);
    const std::string control = "void f(){ while(ok); int x; }\n";
    const auto semicolon = uint32_t(control.find(';'));
    const std::array<parser::SourceRange, 1> endpoint {
        { { semicolon, semicolon + 1 } }
    };
    EXPECT_EQ(FormatInput(control, prepared,
                  { .EditScope = endpoint }).Text,
        control);
    const std::array<parser::SourceRange, 1> replacement {
        { { semicolon, semicolon + 2 } }
    };
    const auto replaced = FormatInput(control, prepared,
        { .SourceMap = true, .EditScope = replacement });
    EXPECT_NE(replaced.Text.find("{}"), std::string::npos);
    EXPECT_TRUE(replaced.Text.ends_with("int x; }\n"));
    CheckIdentities(control, replaced);
    CheckExcludedCopies(control, replaced, replacement);
}

TEST(PublicPipeline, EditScopeBraceInsertionRequiresBothOriginalPoints)
{
    formatter::Style style;
    style.AddMissingControlBraces = true;
    style.ControlBlockSpaceBeforeBrace = formatter::GapPolicy::GeneratedSpace;
    const formatter::PreparedFormat prepared(style);
    const std::string source = "void f(){ if(ok)invoke(); }\n";
    const auto begin = uint32_t(source.find("invoke"));
    const auto end = uint32_t(source.find(';') + 1);
    const std::array<parser::SourceRange, 1> unownedEnd { { { begin, end } } };
    EXPECT_EQ(FormatInput(source, prepared,
                  { .EditScope = unownedEnd }).Text,
        source);
    const std::array<parser::SourceRange, 1> ownedEnd { { { begin, end +
        1 } } };
    const auto result = FormatInput(source, prepared,
        { .SourceMap = true, .EditScope = ownedEnd });
    const auto hasInsertedControlPair = [&](const formatter::FormatResult&
        formatted)
    {
        for (size_t action = 0; action < formatted.StructuralOperations.size();
             ++action)
        {
            if (formatted.StructuralOperations[action].Kind !=
                formatter::StructuralAction::AddMissingControlBraces)
                continue;
            size_t opens = 0, closes = 0;
            for (const auto& map : formatted.Map)
            {
                if (map.Action != action || !map.Generated || map.Exact ||
                    !map.HasOriginal || !map.Original.Empty() ||
                    map.OriginalToken != formatter::layout::NoIdentity)
                    continue;
                const auto text = std::string_view(formatted.Text).substr(
                    map.Output.Begin, map.Output.End - map.Output.Begin);
                opens += text == "{" && map.Original.Begin == begin;
                closes += text == "}" && map.Original.Begin == end;
            }
            if (opens == 1 && closes == 1)
                return true;
        }
        return false;
    };
    EXPECT_TRUE(hasInsertedControlPair(result));
    CheckIdentities(source, result);
    CheckExcludedCopies(source, result, ownedEnd);
    EXPECT_TRUE(result.Text.ends_with('\n'));
    // Existing function braces must not satisfy the positive endpoint check.
    style.AddMissingControlBraces = false;
    const auto skipped = FormatInput(source, formatter::PreparedFormat(style),
        { .SourceMap = true, .EditScope = ownedEnd });
    EXPECT_EQ(skipped.Text, source);
    EXPECT_FALSE(hasInsertedControlPair(skipped));
}

TEST(PublicPipeline, EditScopeNamespaceOriginalWindowsAndEOFOwnership)
{
    for (const bool normalize : { false, true })
    {
        for (const auto policy : { formatter::NamespaceComments::AddMissing,
                 formatter::NamespaceComments::Fix,
                 formatter::NamespaceComments::Remove })
        {
            formatter::Style style;
            style.NamespaceComments = policy;
            style.RemoveNullDeclarations = normalize;
            const formatter::PreparedFormat prepared(style);
            const std::string prefix = normalize ? ";\n" : "";
            const std::string bare = prefix + "namespace Name {}";
            const bool existing = policy != formatter::NamespaceComments::
                AddMissing;
            const std::string source = bare +
                (existing ? " // namespace Wrong\n" : "\n");
            const auto closeEnd = uint32_t(bare.size());
            const std::array<parser::SourceRange, 1> selected {
                { { 0, uint32_t(source.size()) } }
            };
            const auto result = FormatInput(source, prepared,
                { .SourceMap = true, .EditScope = selected });
            CheckIdentities(source, result);
            if (policy == formatter::NamespaceComments::Remove)
                EXPECT_EQ(result.Text.find("// namespace"), std::string::npos);
            else
                EXPECT_NE(result.Text.find("// namespace Name"), std::
                    string::npos);
            const std::array<parser::SourceRange, 1> excludedPoint {
                { { 0, closeEnd } }
            };
            const auto denied = FormatInput(source, prepared,
                { .SourceMap = true, .EditScope = excludedPoint });
            EXPECT_EQ(denied.Text, normalize ? source.substr(1) : source);
            CheckIdentities(source, denied);
            // Existing replacement windows cannot be authorized by their
            // close-brace point alone, even after a preceding deletion.
            if (existing)
            {
                const std::array<parser::SourceRange, 1> cutComment {
                    { { 0, uint32_t(source.find("Wrong")) } }
                };
                EXPECT_NE(FormatInput(source, prepared,
                              { .EditScope = cutComment }).Text.find("Wrong"),
                    std::string::npos);
            }
            else
            {
                const std::array<parser::SourceRange, 1> fullEOF {
                    { { 0, uint32_t(bare.size()) } }
                };
                const auto eof = FormatInput(bare, prepared,
                    { .SourceMap = true, .EditScope = fullEOF });
                EXPECT_EQ(eof.Text.find("// namespace"), std::string::npos);
                CheckIdentities(bare, eof);
                const std::string withComment
                    = bare + " // namespace Wrong\n";
                const std::array<parser::SourceRange, 1> wholeComment {
                    { { 0, uint32_t(withComment.size()) } }
                };
                const auto retained = FormatInput(withComment, prepared,
                    { .SourceMap = true, .EditScope = wholeComment });
                EXPECT_EQ(retained.Text,
                    normalize ? withComment.substr(1) : withComment);
                CheckIdentities(withComment, retained);
            }
        }
    }
}

TEST(PublicPipeline, EditScopeCopiedIntersectionsAfterNormalization)
{
    auto style = Canonical();
    style.IncludeOrder = formatter::IncludeOrder::Sort;
    style.RemoveNullDeclarations = true;
    style.TrailingWhitespace = formatter::TrailingWhitespace::Trim;
    const formatter::PreparedFormat prepared(style);
    const std::string source
        = "#include \"z.h\"\r\n#include \"aaa.h\"\r\n;\r\n"
          "void locked( ){\r\n  invoke( x,y );  \r\n\t\n}\r\n"
          "void selected(){ invoke(a,b); }\n";
    const auto firstFunction = uint32_t(source.find("void locked"));
    const auto horizontal = uint32_t(source.find("  \r\n"));
    const auto selectedFunction = uint32_t(source.find("void selected"));
    const std::array<parser::SourceRange, 3> selected {
        { { 0, firstFunction }, { horizontal, horizontal + 2 },
            { selectedFunction, uint32_t(source.size()) } }
    };
    const auto result = FormatInput(source, prepared,
        { .SourceMap = true, .EditScope = selected });
    EXPECT_TRUE(result.Text.starts_with(
        "#include \"aaa.h\"\r\n#include \"z.h\"\r\n"));
    EXPECT_NE(result.Text.find("invoke(a, b)"), std::string::npos);
    CheckIdentities(source, result);
    CheckExcludedCopies(source, result, selected);
    // This does not require the old lowerer to consume the selected trim run;
    // it checks exact excluded suffix/prefix bytes and projected whole gaps.
    EXPECT_NE(result.Text.find("\r\n\t\n}\r\n"), std::string::npos);
}

TEST(PublicPipeline, EditScopeInvalidRangesAndProtectionRemainStronger)
{
    const std::string source
        = "// hit-fmt off\nvoid locked(){ if(ok)invoke(a,b); }\n"
          "// hit-fmt on\nvoid safe(){ if(ok)invoke(x,y); }\n";
    auto style = Canonical();
    style.AddMissingControlBraces = true;
    const formatter::PreparedFormat prepared(style);
    const std::array<parser::SourceRange, 1> full {
        { { 0, uint32_t(source.size()) } }
    };
    const auto result = FormatInput(source, prepared,
        { .SourceMap = true, .EditScope = full });
    EXPECT_TRUE(result.Text.starts_with(source.substr(0, source.find(
        "void safe"))));
    EXPECT_NE(result.Text.find("invoke(x, y)"), std::string::npos);
    CheckIdentities(source, result);
    const auto safe = uint32_t(source.find("void safe"));
    const std::array<parser::SourceRange, 1> onlySafe {
        { { safe, uint32_t(source.size()) } }
    };
    const auto constrained = FormatInput(source, prepared,
        { .SourceMap = true, .EditScope = onlySafe });
    CheckIdentities(source, constrained);
    CheckExcludedCopies(source, constrained, onlySafe);
    bool disabledOutside = false;
    for (const auto& range : constrained.OutputProtection)
    {
        const auto reasons = uint16_t(range.Reasons);
        disabledOutside |= (reasons & uint16_t(formatter::ProtectionReason::
            Disabled)) &&
            (reasons & uint16_t(formatter::ProtectionReason::OutsideEditScope));
    }
    EXPECT_TRUE(disabledOutside);
    for (const std::vector<parser::SourceRange> invalid : {
             std::vector<parser::SourceRange> { { 0, 0 } },
             { { 2, 1 } }, { { 0, uint32_t(source.size() + 1) } },
             { { 4, 6 }, { 1, 3 } }, { { 0, 5 }, { 4, 7 } } })
        EXPECT_THROW(FormatInput(source, prepared, { .EditScope = invalid }),
            std::invalid_argument);
}
TEST(PublicPipeline, RequiresOrdinaryReturnBoundaryPreservesScopeAndOwningMaps)
{
    const formatter::PreparedFormat prepared(Canonical());
    for (const auto declaration : {
             "Envelope<T> inspect(T value){return {};}",
             "library::Envelope<T> inspect(T value){return {};}",
             "Envelope<Envelope<T>> Owner::inspect(T value){return {};}",
             "Envelope<T> Owner::operator()(T value){return {};}" })
    {
        const auto source = "template<class T> requires (requires(T v) { v.touch(); })\n" +
            std::string(declaration) + "\n";
        SCOPED_TRACE(source);
        const auto function = uint32_t(source.find(declaration));
        formatter::FormatResult result;
        {
            Input input(source);
            const auto tree = parser::Parse(input.View(), [](parser::ParseError) {});
            const std::array<parser::SourceRange, 1> selected {
                { { function, uint32_t(source.size()) } }
            };
            result = formatter::Format(tree, prepared,
                { .SourceMap = true, .EditScope = selected });
        }
        CheckIdentities(source, result);
        const std::array<parser::SourceRange, 1> selected {
            { { function, uint32_t(source.size()) } }
        };
        CheckExcludedCopies(source, result, selected);
        EXPECT_EQ(result.Text.substr(0, function), source.substr(0, function));
        const std::array<parser::SourceRange, 1> selectedOutput {
            { { function, uint32_t(result.Text.size()) } }
        };
        EXPECT_EQ(FormatInput(result.Text, prepared,
            { .SourceMap = true, .EditScope = selectedOutput }).Text, result.Text);
        const auto unrestricted = FormatInput(source, prepared);
        CheckIdentities(source, unrestricted);
        EXPECT_EQ(FormatInput(unrestricted.Text, prepared).Text, unrestricted.Text);
    }
}

TEST(PublicPipeline, RequiresRecoveryScopeAndMapsPreserveInvalidBytes)
{
    const formatter::PreparedFormat prepared(Canonical());
    for (const auto declaration : {
             "int inspect(T value){return 1;}",
             "T* inspect(T* value){return value;}",
             "T& access(T& value){return value;}" })
    {
        for (const auto terminal : { "@", "&& @" })
        {
            const auto source = "template<class T> requires requires(T v) { v.touch(); } " +
                std::string(terminal) + "\n" + std::string(declaration) + "\n";
            SCOPED_TRACE(source);
            const auto invalid = uint32_t(source.find('@'));
            const auto function = uint32_t(source.find(declaration));
            formatter::FormatResult result;
            parser::TokenIndex invalidToken = 0;
            {
                Input input(source);
                auto tree = parser::Parse(input.View(), [](parser::ParseError) {});
                const std::array<parser::SourceRange, 1> selected {
                    { { function, uint32_t(source.size()) } }
                };
                result = formatter::Format(tree, prepared,
                    { .SourceMap = true, .EditScope = selected });
                ASSERT_EQ(result.OriginalTokens.size(), tree.TokenCount());
                size_t invalidCount = 0;
                for (parser::TokenIndex token = 0; token < tree.TokenCount(); ++token)
                {
                    const auto map = result.OriginalTokens[token];
                    ASSERT_TRUE(map.Present);
                    EXPECT_LE(map.Output.Begin, map.Output.End);
                    EXPECT_LE(map.Output.End, result.Text.size());
                    EXPECT_EQ(std::string_view(result.Text).substr(map.Output.Begin,
                        map.Output.End - map.Output.Begin), tree.TokenText(token));
                    if (tree.Kind(token) == tokenizer::TokenKind::Invalid)
                    {
                        ++invalidCount;
                        invalidToken = token;
                    }
                }
                EXPECT_EQ(invalidCount, 1u);
            }
            // Result maps/protection remain owning after source/tree/scope storage dies.
            const auto invalidOutput = result.OriginalTokens[invalidToken].Output;
            EXPECT_EQ(std::string_view(result.Text).substr(invalidOutput.Begin,
                invalidOutput.End - invalidOutput.Begin), "@");
            EXPECT_TRUE(std::ranges::any_of(result.OutputProtection,
                [&](const auto& range) {
                    return range.Source.Begin <= invalidOutput.Begin &&
                        range.Source.End >= invalidOutput.End &&
                        (uint16_t(range.Reasons) & uint16_t(formatter::ProtectionReason::Recovery)) &&
                        (uint16_t(range.Reasons) & uint16_t(formatter::ProtectionReason::OutsideEditScope));
                }));
            const std::array<parser::SourceRange, 1> selected {
                { { function, uint32_t(source.size()) } }
            };
            CheckExcludedCopies(source, result, selected);
            EXPECT_EQ(result.Text.substr(0, function), source.substr(0, function));
            size_t invalidCopies = 0;
            for (const auto& map : result.Map)
            {
                EXPECT_LE(map.Output.Begin, map.Output.End);
                EXPECT_LE(map.Output.End, result.Text.size());
                if (map.Exact && map.HasOriginal)
                {
                    EXPECT_EQ(std::string_view(result.Text).substr(map.Output.Begin,
                        map.Output.End - map.Output.Begin), std::string_view(source).substr(
                            map.Original.Begin, map.Original.End - map.Original.Begin));
                    if (map.Original.Begin <= invalid && map.Original.End > invalid)
                        ++invalidCopies;
                }
            }
            EXPECT_EQ(invalidCopies, 1u);
        }
    }
}
} // namespace
} // namespace public_pipeline
