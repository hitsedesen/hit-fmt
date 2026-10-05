#include <formatter/source_analysis.hpp>
#include <formatter/syntax_spacing.hpp>
#include <parser/parser.hpp>
#include <tests/golden.hpp>
#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <filesystem>
#include <format>
#include <memory_resource>
#include <limits>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>
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

parser::SyntaxTree Parse(const Input& input)
{
    return parser::Parse(input.View(), [] (parser::ParseError) { });
}
void ExpectSameAnalysis(
    const formatter::SourceAnalysis& direct,
    const formatter::SourceAnalysis& deferred)
{
    ASSERT_EQ(direct.Gaps().size(), deferred.Gaps().size());
    for (formatter::GapId i = 0; i < direct.Gaps().size(); ++i) {
        SCOPED_TRACE(i);
        const auto& a = direct.Gaps()[i];
        const auto& b = deferred.Gaps()[i];
        EXPECT_EQ(a.Source.Begin, b.Source.Begin);
        EXPECT_EQ(a.Source.End, b.Source.End);
        EXPECT_EQ(direct.Original(a), deferred.Original(b));
        EXPECT_EQ(a.Separation, b.Separation);
        EXPECT_EQ(a.LineBoundary, b.LineBoundary);
        EXPECT_EQ(a.Reasons, b.Reasons);
        EXPECT_EQ(a.Preserve, b.Preserve);
        EXPECT_EQ(a.MustStartWithLineFeed, b.MustStartWithLineFeed);
        EXPECT_EQ(a.ContextSensitive, b.ContextSensitive);
        EXPECT_EQ(bool(a.HasLineFeed), bool(b.HasLineFeed));
        EXPECT_EQ(bool(a.Horizontal), bool(b.Horizontal));
        EXPECT_EQ(bool(a.IntrinsicPreserve), bool(b.IntrinsicPreserve));
        EXPECT_EQ(
            bool(a.IntrinsicContextSensitive),
            bool(b.IntrinsicContextSensitive));
        for (auto replacement :
            {"", " ", "\t", "\n  ", "\r\n", " \n", "/* no */"})
        {
            EXPECT_EQ(
                direct.CanReplaceGap(i, replacement),
                deferred.CanReplaceGap(i, replacement));
        }
        EXPECT_EQ(
            direct.CanReplaceGap(i, direct.Original(a)),
            deferred.CanReplaceGap(i, deferred.Original(b)));
        EXPECT_EQ(
            direct.IntersectsImmutable(a.Source),
            deferred.IntersectsImmutable(b.Source));
    }
    ASSERT_EQ(direct.Protection().size(), deferred.Protection().size());
    for (size_t i = 0; i < direct.Protection().size(); ++i) {
        const auto& a = direct.Protection()[i];
        const auto& b = deferred.Protection()[i];
        EXPECT_EQ(a.Source.Begin, b.Source.Begin);
        EXPECT_EQ(a.Source.End, b.Source.End);
        EXPECT_EQ(a.Reasons, b.Reasons);
    }
    ASSERT_EQ(direct.Regions().size(), deferred.Regions().size());
    for (size_t i = 0; i < direct.Regions().size(); ++i) {
        const auto& a = direct.Regions()[i];
        const auto& b = deferred.Regions()[i];
        EXPECT_EQ(a.Tokens.Begin, b.Tokens.Begin);
        EXPECT_EQ(a.Tokens.End, b.Tokens.End);
        EXPECT_EQ(a.Source.Begin, b.Source.Begin);
        EXPECT_EQ(a.Source.End, b.Source.End);
        EXPECT_EQ(a.Kind, b.Kind);
        EXPECT_EQ(a.Parent, b.Parent);
        EXPECT_EQ(a.FirstChild, b.FirstChild);
        EXPECT_EQ(a.NextSibling, b.NextSibling);
        EXPECT_EQ(a.Attached.Begin, b.Attached.Begin);
        EXPECT_EQ(a.Attached.End, b.Attached.End);
        EXPECT_EQ(a.Status, b.Status);
    }
    ASSERT_EQ(
        direct.PartialGapPermissions().size(),
        deferred.PartialGapPermissions().size());
    for (size_t i = 0; i < direct.PartialGapPermissions().size(); ++i) {
        const auto& a = direct.PartialGapPermissions()[i];
        const auto& b = deferred.PartialGapPermissions()[i];
        EXPECT_EQ(a.Gap, b.Gap);
        EXPECT_EQ(a.Source.Begin, b.Source.Begin);
        EXPECT_EQ(a.Source.End, b.Source.End);
        EXPECT_EQ(a.Kind, b.Kind);
    }
    const auto size = direct.Gaps().back().Source.End;
    for (parser::SourceOffset i = 0; i <= size; ++i) {
        EXPECT_EQ(
            direct.IntersectsImmutable({i, i}),
            deferred.IntersectsImmutable({i, i})) << i;
        if (i < size) {
            EXPECT_EQ(
                direct.IntersectsImmutable({i, i + 1}),
                deferred.IntersectsImmutable({i, i + 1})) << i;
        }
    }
}
formatter::SourceAnalysis Analyze(
    const parser::SyntaxTree& tree,
    std::pmr::memory_resource& memory,
    formatter::SourceAnalysisOptions options = {})
{
    std::pmr::monotonic_buffer_resource scratch;
    auto direct =
        formatter::BuildSourceAnalysis(tree, options, memory, scratch);
    // Byte facts stay valid across protection and deferred scope changes,
    // including empty, CRLF, bare-CR and non-horizontal whitespace gaps.
    for (const auto& gap : direct.Gaps()) {
        const auto original = direct.Original(gap);
        EXPECT_EQ(
            bool(gap.HasLineFeed),
            original.find('\n') != std::string_view::npos);
        EXPECT_EQ(
            bool(gap.Horizontal),
            original.find_first_not_of(" \t") == std::string_view::npos);
    }
    if (options.EditScope) {
        const auto scope = *options.EditScope;
        options.EditScope.reset();
        auto deferred =
            formatter::BuildSourceAnalysis(tree, options, memory, scratch);
        deferred.RestrictEditScope(scope);
        ExpectSameAnalysis(direct, deferred);
    }
    return direct;
}
bool Has(formatter::ProtectionReason value, formatter::ProtectionReason bit)
{
    return (static_cast<uint16_t>(value) & static_cast<uint16_t>(bit)) != 0;
}
formatter::GapId Before(
    const parser::SyntaxTree& tree,
    std::string_view text,
    size_t occurrence = 0)
{
    for (parser::TokenIndex i = 0; i < tree.TokenCount(); ++i) {
        if (tree.TokenText(i) == text && occurrence-- == 0) {
            return i;
        }
    }
    ADD_FAILURE() << "missing token: " << text;
    return tree.TokenCount();
}
TEST(SourceAnalysis, TouchingDifferentProtectionReasonsKeepTheirByteLocations)
{
    Input input("int value;\n");
    auto tree = Parse(input);
    std::pmr::monotonic_buffer_resource memory;
    using R = formatter::ProtectionReason;
    const std::array different{
        formatter::ProtectedSourceRange{{0, 3}, R::Explicit},
        formatter::ProtectedSourceRange{{3, 6}, R::Disabled}
    };
    const std::array scope{parser::SourceRange{0, uint32_t(input.Size)}};
    auto distinct = Analyze(
        tree,
        memory,
        {.InheritedProtection = different, .EditScope = scope});
    ASSERT_EQ(distinct.Protection().size(), 2);
    EXPECT_EQ(distinct.Protection()[0].Source.End, 3);
    EXPECT_EQ(distinct.Protection()[0].Reasons, R::Explicit);
    EXPECT_EQ(distinct.Protection()[1].Source.Begin, 3);
    EXPECT_EQ(distinct.Protection()[1].Reasons, R::Disabled);
    EXPECT_TRUE(distinct.IntersectsImmutable({3, 3}));
    EXPECT_TRUE(distinct
        .IntersectsImmutable({uint32_t(input.Size), uint32_t(input.Size)}));
    const std::array same{
        formatter::ProtectedSourceRange{{0, 3}, R::Explicit},
        formatter::ProtectedSourceRange{{3, 6}, R::Explicit}
    };
    auto coalesced = Analyze(tree, memory, {.InheritedProtection = same});
    ASSERT_EQ(coalesced.Protection().size(), 1);
    EXPECT_EQ(coalesced.Protection()[0].Source.End, 6);
    const std::array overlap{
        formatter::ProtectedSourceRange{{0, 4}, R::Explicit},
        formatter::ProtectedSourceRange{{3, 6}, R::Disabled}
    };
    auto unioned = Analyze(tree, memory, {.InheritedProtection = overlap});
    ASSERT_EQ(unioned.Protection().size(), 1);
    EXPECT_EQ(unioned.Protection()[0].Source.End, 6);
    EXPECT_EQ(unioned.Protection()[0].Reasons, R::Explicit | R::Disabled);
}
TEST(SourceAnalysis, ScopedIncludeBoundaryDoesNotAbsorbExcludedSeparator)
{
    Input input("#include \"z.h\"\n#include \"a.h\"\n\nint value;\n");
    auto tree = Parse(input);
    std::pmr::monotonic_buffer_resource memory;
    const std::array scope{parser::SourceRange{0, 30}};
    auto scoped = Analyze(tree, memory, {.EditScope = scope});
    ASSERT_EQ(scoped.Protection().size(), 2);
    EXPECT_EQ(scoped.Protection()[0].Source.End, 30);
    EXPECT_EQ(
        scoped.Protection()[0].Reasons,
        formatter::ProtectionReason::Directive);
    EXPECT_EQ(scoped.Protection()[1].Source.Begin, 30);
    EXPECT_EQ(
        scoped.Protection()[1].Reasons,
        formatter::ProtectionReason::OutsideEditScope);
    EXPECT_TRUE(scoped.IntersectsImmutable({30, 30}));
    EXPECT_TRUE(scoped
        .IntersectsImmutable({uint32_t(input.Size), uint32_t(input.Size)}));
    EXPECT_FALSE(scoped.CanReplaceGap(2, "\n"));
}
TEST(SourceAnalysis, DiagnosticGolden)
{
    const auto directory = std::filesystem::path(__FILE__).parent_path() /
        "testdata/source-analysis";
    const auto input =
        test::golden::ReadFile(directory / "protection.input.cpp");
    Input padded(input);
    auto tree = Parse(padded);
    std::pmr::monotonic_buffer_resource memory;
    const std::array names{std::string_view("KEEP")};
    auto analysis = Analyze(tree, memory, {.WhitespaceSensitiveMacros = names});
    std::string dump;
    for (const auto& interval : analysis.Protection()) {
        dump += std::format(
            "immutable [{},{}) reasons={}\n",
            interval.Source.Begin,
            interval.Source.End,
            static_cast<uint16_t>(interval.Reasons));
    }
    for (formatter::GapId i = 0; i < analysis.Gaps().size(); ++i) {
        const auto& gap = analysis.Gaps()[i];
        if (gap.Preserve ||
            gap.LineBoundary != formatter::GapLineBoundary::Any ||
            gap.Separation == formatter::GapSeparation::Required)
        {
            dump += std::format(
                "gap {} [{},{}) preserve={} separation={} line={} "
                "lf-first={} context={}\n",
                i,
                gap.Source.Begin,
                gap.Source.End,
                gap.Preserve,
                static_cast<unsigned>(gap.Separation),
                static_cast<unsigned>(gap.LineBoundary),
                gap.MustStartWithLineFeed,
                gap.ContextSensitive);
        }
    }
    test::golden::ExpectGolden(directory / "protection.analysis.txt", dump);
}
TEST(SourceAnalysis, StandaloneNestedOptionalAndUnclosedMarkers)
{
    Input input("const char* s = \"// hit-fmt off\"; // hit-fmt off\n"
        "/* hit-fmt off */ int not_a_marker;\n"
        "#define LOOK \"// hit-fmt off\"\n"
        "// clang-format off\nint clang_only;\n// clang-format on\n"
        "  // hit-fmt off  \r\nint frozen;\n/* hit-fmt off */\n"
        "int nested;\n// hit-fmt on\nint still_frozen;\n// hit-fmt on\n"
        "int editable;\n// hit-fmt off\nint suffix;\n");
    auto tree = Parse(input);
    std::pmr::monotonic_buffer_resource memory;
    auto normal = Analyze(tree, memory);
    auto clang = Analyze(tree, memory, {.HonorClangFormatDirectives = true});
    auto disabled = [&] (const auto& analysis, std::string_view name) {
        auto offset = tree.Token(Before(tree, name)).Text.Begin;
        for (const auto& range : analysis.Protection()) {
            if (range.Source.Begin <= offset && offset < range.Source.End &&
                Has(range.Reasons, formatter::ProtectionReason::Disabled))
            {
                return true;
            }
        }
        return false;
    };
    EXPECT_FALSE(disabled(normal, "s"));
    EXPECT_FALSE(disabled(normal, "not_a_marker"));
    EXPECT_FALSE(disabled(normal, "clang_only"));
    EXPECT_TRUE(disabled(clang, "clang_only"));
    for (auto name : {"frozen", "nested", "still_frozen", "suffix"}) {
        EXPECT_TRUE(disabled(normal, name)) << name;
    }
    EXPECT_FALSE(disabled(normal, "editable"));
    EXPECT_EQ(normal.Protection().back().Source.End, input.Size);
}
TEST(SourceAnalysis, SensitiveInvocationsAreExplicitAndLocal)
{
    Input input("int prefix = 1;\nKEEP( a, KEEP( b ) );\n"
        "KEEP /* trivia */ ( commented );\n"
        "LOUD( x );\nKEEP( broken, KEEP( [ mismatch ) );\n");
    auto tree = Parse(input);
    std::pmr::monotonic_buffer_resource memory;
    const std::array names{std::string_view("KEEP")};
    auto analysis = Analyze(tree, memory, {.WhitespaceSensitiveMacros = names});
    EXPECT_TRUE(analysis.CanReplaceGap(Before(tree, "prefix"), "  "));
    EXPECT_FALSE(analysis.CanReplaceGap(Before(tree, "a"), ""));
    EXPECT_FALSE(analysis.CanReplaceGap(Before(tree, "b"), "\n"));
    EXPECT_FALSE(analysis.CanReplaceGap(Before(tree, "commented"), ""));
    EXPECT_TRUE(analysis.CanReplaceGap(Before(tree, "x"), "  "));
    EXPECT_FALSE(analysis.CanReplaceGap(Before(tree, "broken"), ""));
    EXPECT_FALSE(analysis.CanReplaceGap(Before(tree, "mismatch"), "  "));
    EXPECT_EQ(analysis.Protection().back().Source.End, input.Size);
    EXPECT_TRUE(Has(
        analysis.Protection().back().Reasons,
        formatter::ProtectionReason::SensitiveMacro));
    EXPECT_TRUE(Has(
        analysis.Protection().back().Reasons,
        formatter::ProtectionReason::Recovery));
}
TEST(SourceAnalysis, CommentsDirectivesSplicesAndCrLf)
{
    Input input("int a = 1; // note\r\nint b = 2;\r\n"
        "int c /* note */ = 3;\r\n#define F(x) x + \\\r\n  1\r\n"
        "int d = 4;\r\nint e = \\\n  5;\nint f = 6;\n");
    auto tree = Parse(input);
    std::pmr::monotonic_buffer_resource memory;
    auto analysis = Analyze(tree, memory);
    const auto afterComment = Before(tree, "int", 1);
    EXPECT_FALSE(analysis.Gaps()[afterComment].Preserve);
    EXPECT_FALSE(analysis.CanReplaceGap(afterComment, " "));
    EXPECT_FALSE(analysis.CanReplaceGap(afterComment, "\r\n  "));
    EXPECT_FALSE(analysis.CanReplaceGap(afterComment, " \n"));
    EXPECT_TRUE(analysis.CanReplaceGap(afterComment, "\n  "));
    EXPECT_TRUE(analysis.CanReplaceGap(Before(tree, "b"), "\t"));
    EXPECT_FALSE(analysis.CanReplaceGap(Before(tree, "/* note */"), "\n"));
    EXPECT_TRUE(analysis.CanReplaceGap(Before(tree, "/* note */"), "  "));
    EXPECT_TRUE(analysis.CanReplaceGap(Before(tree, "d"), "  "));
    EXPECT_FALSE(analysis.CanReplaceGap(Before(tree, "5"), " "));
    EXPECT_TRUE(analysis.CanReplaceGap(Before(tree, "f"), "  "));
    for (const auto& gap : analysis.Gaps()) {
        if (Has(gap.Reasons, formatter::ProtectionReason::Directive)) {
            EXPECT_TRUE(gap.Preserve);
        }
    }
}

TEST(SourceAnalysis, RequiresTerminalRecoveryProtectsConstraintNotFollowingFunction)
{
    const std::string_view declarations[] = {
        "int inspect(T value) { return 1; }",
        "T* inspect(T* value) { return value; }",
        "T& access(T& value) { return value; }",
    };
    struct TerminalCase
    {
        std::string_view Text;
        bool Unparsed;
    };
    const TerminalCase terminals[] = {{"@", true}, {"&& @", false}};
    for (const auto declaration : declarations) {
        for (const auto& terminal : terminals) {
            const auto source =
                "template<class T> requires requires(T v) { v.touch(); } " +
                    std::string(terminal.Text) + "\n" +
                    std::string(declaration) + "\n";
            SCOPED_TRACE(source);
            Input input(source);
            auto tree = Parse(input);
            std::pmr::monotonic_buffer_resource memory;
            auto analysis = Analyze(tree, memory);
            const auto invalid =
                static_cast<parser::SourceOffset>(source.find('@'));
            EXPECT_TRUE(analysis.IntersectsImmutable({invalid, invalid}));
            EXPECT_TRUE(analysis.IntersectsImmutable({invalid, invalid + 1}));
            size_t clauses = 0;
            size_t functions = 0;
            size_t unparsed = 0;
            size_t invalidRegions = 0;
            for (const auto& region : analysis.Regions()) {
                if (region.Source.Begin == invalid &&
                    region.Source.End == invalid + 1)
                {
                    ++invalidRegions;
                    EXPECT_TRUE(
                        region.Status == formatter::RegionStatus::Recovery ||
                        region.Status ==
                            formatter::RegionStatus::RecoveryAndAmbiguous);
                }
                if (region.Kind == parser::NodeKind::RequiresClause) {
                    ++clauses;
                    EXPECT_TRUE(
                        region.Status == formatter::RegionStatus::Recovery ||
                        region.Status ==
                            formatter::RegionStatus::RecoveryAndAmbiguous);
                }
                if (region.Kind == parser::NodeKind::UnparsedSyntax) {
                    ++unparsed;
                    EXPECT_EQ(region.Source.Begin, invalid);
                    EXPECT_EQ(region.Source.End, invalid + 1);
                }
                if (region.Kind == parser::NodeKind::FunctionDefinition) {
                    ++functions;
                    EXPECT_EQ(region.Status, formatter::RegionStatus::Complete);
                    EXPECT_EQ(
                        std::string_view(source).substr(
                            region.Source.Begin,
                            region.Source.End - region.Source.Begin),
                        declaration);
                    EXPECT_FALSE(analysis.IntersectsImmutable(region.Source));
                }
            }
            EXPECT_EQ(clauses, 1u);
            EXPECT_EQ(unparsed, terminal.Unparsed ? 1u : 0u);
            EXPECT_EQ(invalidRegions, 1u);
            EXPECT_EQ(functions, 1u);
        }
    }
}

TEST(SourceAnalysis, RecoverySummariesAndMergedExplicitRanges)
{
    Input input("int good = 1;\nvoid f() { call(a,,b); }\nint tail = 2;\n");
    auto tree = Parse(input);
    std::pmr::monotonic_buffer_resource memory;
    const std::array<parser::SourceRange, 3> ranges{
        {{4, 7}, {6, 10}, {10, 11}},
    };
    auto analysis = Analyze(tree, memory, {.ImmutableRanges = ranges});
    ASSERT_FALSE(analysis.Regions().empty());
    EXPECT_NE(analysis.Regions()[0].Status, formatter::RegionStatus::Complete);
    EXPECT_TRUE(analysis.CanReplaceGap(Before(tree, "tail"), "  "));
    EXPECT_TRUE(analysis.IntersectsImmutable({4, 4}));
    EXPECT_TRUE(analysis.IntersectsImmutable({3, 5}));
    EXPECT_FALSE(analysis.IntersectsImmutable({11, 11}));
    EXPECT_EQ(analysis.Protection()[0].Source.Begin, 4u);
    EXPECT_EQ(analysis.Protection()[0].Source.End, 11u);
    for (formatter::RegionId i = 1; i < analysis.Regions().size(); ++i) {
        EXPECT_LT(analysis.Regions()[i].Parent, i);
    }
    for (size_t i = 1; i < analysis.Protection().size(); ++i) {
        EXPECT_LT(
            analysis.Protection()[i - 1].Source.End,
            analysis.Protection()[i].Source.Begin);
    }
}
void ExpectSameTokens(
    const parser::SyntaxTree& original,
    std::string_view changed)
{
    Input input(changed);
    auto result = parser::SyntaxTree::TokenizeOnly(input.View());
    ASSERT_EQ(original.TokenCount(), result.TokenCount()) << changed;
    for (parser::TokenIndex i = 0; i < original.TokenCount(); ++i) {
        EXPECT_EQ(original.Kind(i), result.Kind(i)) << changed;
        EXPECT_EQ(original.TokenText(i), result.TokenText(i)) << changed;
    }
}
TEST(SourceAnalysis, LineTokenTerminationAndEofKeepExactSpelling)
{
    for (auto source :
        {
            "// note\nint x;\n",
            "// note\r\nint x;\r\n",
            "// note",
            "// note\r",
            "#define X 1",
            "%:define X 1",
        })
    {
        Input input(source);
        auto tree = Parse(input);
        std::pmr::monotonic_buffer_resource memory;
        auto analysis = Analyze(tree, memory);
        const auto right = tree.TokenCount() == 1 ? 1 : Before(tree, "int");
        EXPECT_FALSE(analysis.CanReplaceGap(right, " ")) << source;
        EXPECT_FALSE(analysis.CanReplaceGap(right, " \n")) << source;
        EXPECT_FALSE(analysis.CanReplaceGap(right, "\r\n")) << source;
        if (tree.TokenCount() > 1) {
            EXPECT_TRUE(analysis.CanReplaceGap(right, "\n  "));
            auto changed = std::string(tree.Source());
            const auto& gap = analysis.Gaps()[right];
            changed.replace(
                gap.Source.Begin,
                analysis.Original(gap).size(),
                "\n  ");
            ExpectSameTokens(tree, changed);
        }
    }
}
TEST(SourceAnalysis, HeaderNameInternalGapsAreContextSensitive)
{
    Input input("import<pkg/sub.h>;\nexport import < other / unit . h >;\n"
        "import \"quoted.h\";\nint tail;\n");
    auto tree = Parse(input);
    std::pmr::monotonic_buffer_resource memory;
    auto analysis = Analyze(tree, memory);
    size_t headers = 0;
    for (const auto& region : analysis.Regions()) {
        if (region.Kind != parser::NodeKind::HeaderName) {
            continue;
        }
        ++headers;
        for (auto i = region.Tokens.Begin + 1; i < region.Tokens.End; ++i) {
            EXPECT_TRUE(analysis.Gaps()[i].ContextSensitive);
            EXPECT_FALSE(analysis
                .CanReplaceGap(i, analysis.Original(i).empty() ? " " : ""));
        }
        for (auto outside : {region.Tokens.Begin, region.Tokens.End}) {
            EXPECT_TRUE(analysis.CanReplaceGap(outside, "  "));
            auto changed = std::string(tree.Source());
            const auto& gap = analysis.Gaps()[outside];
            changed
                .replace(gap.Source.Begin, analysis.Original(gap).size(), "  ");
            ExpectSameTokens(tree, changed);
        }
    }
    EXPECT_EQ(headers, 3u);
    EXPECT_TRUE(analysis.CanReplaceGap(Before(tree, "tail"), "  "));
    const auto opener = Before(tree, "<");
    const auto token = tree.Token(opener);
    const std::array<parser::SourceRange, 1> ranges{
        {{token.Text.Begin, token.Text.End}},
    };
    auto excluded = Analyze(tree, memory, {.ImmutableRanges = ranges});
    EXPECT_FALSE(excluded.CanReplaceGap(opener, "  "));
    const std::array<formatter::ProtectedSourceRange, 1> inherited{
        {{ranges[0], formatter::ProtectionReason::HeaderName}},
    };
    auto projected = Analyze(tree, memory, {.InheritedProtection = inherited});
    EXPECT_TRUE(projected.CanReplaceGap(opener, "  "));
    auto mixed = Analyze(
        tree,
        memory,
        {.ImmutableRanges = ranges, .InheritedProtection = inherited});
    EXPECT_FALSE(mixed.CanReplaceGap(opener, "  "));
    bool unionFound = false;
    for (const auto& range : mixed.Protection()) {
        unionFound |= Has(
            range.Reasons,
            formatter::ProtectionReason::
            Explicit) &&
            Has(range.Reasons, formatter::ProtectionReason::HeaderName);
    }
    EXPECT_TRUE(unionFound);
    const std::array<parser::SourceRange, 1> entire{
        {{0, uint32_t(input.Size)}},
    };
    auto scoped = Analyze(tree, memory, {.EditScope = entire});
    EXPECT_TRUE(scoped.CanReplaceGap(opener, "  "));
    EXPECT_FALSE(scoped.CanReplaceGap(Before(tree, "pkg"), " "));
    auto scopedMixed = Analyze(
        tree,
        memory,
        {
            .ImmutableRanges = ranges,
            .InheritedProtection = inherited,
            .EditScope = entire,
        });
    EXPECT_FALSE(scopedMixed.CanReplaceGap(opener, "  "));
    auto invalid = inherited;
    invalid[0].Source.End = uint32_t(tree.Source().size() + 1);
    EXPECT_THROW(
        Analyze(tree, memory, {.InheritedProtection = invalid}),
        std::invalid_argument);
}
TEST(SourceAnalysis, SplicedMarkerLookalikesDoNotDisableFollowingLines)
{
    for (auto horizontal : {"", " ", "\t\r", "\f\v \t\r"}) {
        const auto source = std::string("int joined = \\") + horizontal +
            "\n/* hit-fmt off */\n 1;\nint after;\n";
        Input input(source);
        auto tree = Parse(input);
        std::pmr::monotonic_buffer_resource memory;
        auto analysis = Analyze(tree, memory);
        bool splice = false;
        for (const auto& range : analysis.Protection()) {
            EXPECT_FALSE(
                Has(range.Reasons, formatter::ProtectionReason::Disabled));
            if (Has(range.Reasons, formatter::ProtectionReason::LineSplice)) {
                splice = true;
                EXPECT_EQ(range.Source.Begin, 0u);
                EXPECT_GE(range.Source.End, source.find("\n 1;") + 1);
            }
        }
        EXPECT_TRUE(splice) << horizontal;
        EXPECT_FALSE(analysis.CanReplaceGap(Before(tree, "joined"), "  "));
        EXPECT_TRUE(analysis.CanReplaceGap(Before(tree, "after"), "  "));
        ExpectSameTokens(tree, source); // Includes the tokenizer's skipped gap.
    }
}
TEST(SourceAnalysis, AmbiguousAndInvalidTokensAreLocalNotWholeFileRollback)
{
    Input input(
        "void f() { unknown * value; int good = 1; @; }\n" "int tail = 2;\n");
    auto tree = Parse(input);
    std::pmr::monotonic_buffer_resource memory;
    auto analysis = Analyze(tree, memory);
    EXPECT_EQ(
        analysis.Regions()[0].Status,
        formatter::RegionStatus::RecoveryAndAmbiguous);
    EXPECT_FALSE(analysis.CanReplaceGap(Before(tree, "*"), ""));
    EXPECT_FALSE(analysis.CanReplaceGap(Before(tree, "@"), "\n"));
    EXPECT_TRUE(analysis.CanReplaceGap(Before(tree, "good"), "  "));
    EXPECT_TRUE(analysis.CanReplaceGap(Before(tree, "tail"), "  "));
}
TEST(SourceAnalysis, AcceptedGapEditsKeepExactTokensIndividuallyAndTogether)
{
    Input input(
        "void f() { int alpha = 1; int beta = 2; " "alpha = beta + +alpha; "
        "alpha = beta / *ptr; bool yes = alpha > beta and not done; "
        "return; } // note\n"
        "#define VALUE 1\nimport <pkg/sub.h>;\n// note\r\nint tail;\n"
        "#define EOF_VALUE 2");
    auto tree = Parse(input);
    std::pmr::monotonic_buffer_resource memory;
    auto analysis = Analyze(tree, memory);
    std::string together;
    for (formatter::GapId i = 0; i < analysis.Gaps().size(); ++i) {
        const auto& gap = analysis.Gaps()[i];
        for (auto replacement :
            {
                std::string_view(""),
                std::string_view(" "),
                std::string_view("\r\n"),
                std::string_view("\n"),
                std::string_view("\n  "),
                std::string_view(" \n"),
                std::string_view("\t\r\n"),
            })
        {
            if (analysis.CanReplaceGap(i, replacement)) {
                auto changed = std::string(tree.Source());
                changed.replace(
                    gap.Source.Begin,
                    analysis.Original(gap).size(),
                    replacement);
                ExpectSameTokens(tree, changed);
            }
        }
        together += analysis.CanReplaceGap(i, "")
            ? std::string_view{} : analysis.Original(gap);
        if (i < tree.TokenCount()) {
            together += tree.TokenText(i);
        }
    }
    ExpectSameTokens(tree, together);
    for (auto name : {"and", "not", "done"}) {
        EXPECT_FALSE(analysis.CanReplaceGap(Before(tree, name), "")) << name;
    }
    EXPECT_FALSE(analysis.CanReplaceGap(Before(tree, "beta"), ""));
    EXPECT_FALSE(analysis.CanReplaceGap(Before(tree, "*"), ""));
    EXPECT_FALSE(analysis.CanReplaceGap(Before(tree, "+", 1), ""));
    EXPECT_FALSE(analysis.CanReplaceGap(0, "\\\n"));
    EXPECT_FALSE(analysis.CanReplaceGap(0, "/* injected */"));
}
TEST(SourceAnalysis, ContextSensitivePunctuationIsNotJustTokenization)
{
    Input input("[ [ x ] ] \"x\"_suffix");
    auto tree = Parse(input);
    std::pmr::monotonic_buffer_resource memory;
    auto analysis = Analyze(tree, memory);
    EXPECT_TRUE(analysis.Gaps()[1].ContextSensitive);
    EXPECT_FALSE(analysis.CanReplaceGap(1, ""));
    const auto suffix = Before(tree, "_suffix");
    EXPECT_TRUE(analysis.Gaps()[suffix].ContextSensitive);
    EXPECT_FALSE(analysis.CanReplaceGap(suffix, " "));
}
TEST(SourceAnalysis, LexicalConstraintsCoverPunctuationAndLiteralBoundaries)
{
    for (const auto& [first, second, required] :
        {
            std::tuple{">", "::", false}, {">>", "::", false},
            {"::", "*", false}, {"+", "*", false}, {"&&", "!", false},
            {"+", "+", true}, {"/", "*", true}, {"/", "/", true},
            {">", ">", true}, {"<", "::", true}, {"<:", ":", true},
            {"<", "%", true}, {"%", ">", true}, {":", ">", true},
            {"-", ">", true}, {"->", "*", true}, {"<<", "=", true},
            {">>", "=", true}, {"<=", ">", true}, {".", ".", true}
        })
    {
        Input input(std::string(first) + " " + second);
        auto tree = parser::SyntaxTree::TokenizeOnly(input.View());
        ASSERT_EQ(tree.TokenCount(), 2u);
        EXPECT_EQ(formatter::TokenGapNeedsSeparation(tree, 1), required)
            << first << ' ' << second;
    }
    const std::array spellings{
        "alpha",
        "1",
        "1.0",
        "0x1",
        "u8",
        "e2",
        "_tag",
        "\"x\"",
        "'a'",
        "{",
        "}",
        "[",
        "]",
        "(",
        ")",
        ";",
        ":",
        "...",
        "?",
        "::",
        ".",
        ".*",
        "->",
        "->*",
        "~",
        "!",
        "+",
        "-",
        "*",
        "/",
        "%",
        "^",
        "&",
        "|",
        "=",
        "+=",
        "-=",
        "*=",
        "/=",
        "%=",
        "^=",
        "&=",
        "|=",
        "==",
        "!=",
        "<",
        ">",
        "<=",
        ">=",
        "<=>",
        "&&",
        "||",
        "<<",
        ">>",
        "<<=",
        ">>=",
        "++",
        "--",
        ",",
        "<:",
        ":>",
        "<%",
        "%>",
        "/* note */",
        "and",
        "bitor",
        "or",
        "xor",
        "compl",
        "bitand",
        "and_eq",
        "or_eq",
        "xor_eq",
        "not",
        "not_eq",
    };
    size_t removable = 0;
    for (auto first : spellings) {
        for (auto second : spellings) {
            Input input(std::string(first) + " " + second);
            auto tree = parser::SyntaxTree::TokenizeOnly(input.View());
            if (tree.TokenCount() != 2) {
                continue;
            }
            if (!formatter::TokenGapNeedsSeparation(tree, 1)) {
                ++removable;
                ExpectSameTokens(tree, std::string(first) + second);
            }
        }
    }
    EXPECT_GT(removable, 1000u); // This is not an always-preserve oracle.
    Input input(". . . < :: > / * > > <: : name < :: : name");
    auto tree = parser::SyntaxTree::TokenizeOnly(input.View());
    std::string changed;
    for (parser::TokenIndex i = 0; i < tree.TokenCount(); ++i) {
        if (i && formatter::TokenGapNeedsSeparation(tree, i)) {
            changed += ' ';
        }
        changed += tree.TokenText(i);
    }
    ExpectSameTokens(tree, changed);
}
TEST(SourceAnalysis, SourceSizeIsCheckedBeforeOffsetNarrowing)
{
    EXPECT_EQ(formatter::CheckedSourceAnalysisSize(0), 0u);
    EXPECT_EQ(
        formatter::CheckedSourceAnalysisSize(util::MaxSourceSize),
        util::MaxSourceSize);
    EXPECT_THROW(
        static_cast<void>(formatter::CheckedSourceAnalysisSize(
            util::MaxSourceSize + 1)),
        std::length_error);
    EXPECT_THROW(static_cast<void>(formatter::CheckedSourceAnalysisSize(
        std::numeric_limits<size_t>::max())), std::length_error);
    if constexpr (sizeof(size_t) > sizeof(parser::SourceOffset)) {
        EXPECT_THROW(static_cast<void>(formatter::CheckedSourceAnalysisSize(
            uint64_t{1} << 32)), std::length_error);
    }
}
using PermissionKind = formatter::PartialGapPermissionKind;

const formatter::PartialGapPermission* Permission(
    const formatter::SourceAnalysis& analysis,
    formatter::GapId gap,
    PermissionKind kind)
{
    for (const auto& permission : analysis.PartialGapPermissions()) {
        if (permission.Gap == gap && permission.Kind == kind) {
            return &permission;
        }
    }
    return nullptr;
}
TEST(SourceAnalysis, EditScopeUnrestrictedFullAndEmpty)
{
    Input input("void f() {\ninvoke(a,b);\n}\n");
    auto tree = Parse(input);
    std::pmr::monotonic_buffer_resource memory;
    auto normal = Analyze(tree, memory);
    const std::array<parser::SourceRange, 1> all{{{0, uint32_t(input.Size)}}};
    auto full = Analyze(tree, memory, {.EditScope = all});
    auto empty = Analyze(tree, memory,
        {.EditScope = std::span<const parser::SourceRange>{}});
    EXPECT_TRUE(normal.PartialGapPermissions().empty());
    EXPECT_TRUE(full.PartialGapPermissions().empty());
    EXPECT_TRUE(empty.PartialGapPermissions().empty());
    for (formatter::GapId i = 0; i < normal.Gaps().size(); ++i) {
        for (auto replacement : {"", " ", "\n  ", "\r\n"}) {
            if (i + 1 < normal.Gaps().size()) {
                EXPECT_EQ(
                    normal.CanReplaceGap(i, replacement),
                    full.CanReplaceGap(i, replacement));
            } else if (replacement != full.Original(i)) {
                // Strict EOF is not an owned final-indentation point, even
                // when the whole file's existing bytes are selected.
                EXPECT_FALSE(full.CanReplaceGap(i, replacement));
            }
            if (replacement != empty.Original(i)) {
                EXPECT_FALSE(empty.CanReplaceGap(i, replacement));
            }
        }
        EXPECT_TRUE(empty.CanReplaceGap(i, empty.Original(i)));
        EXPECT_TRUE(Has(
            empty.Gaps()[i].Reasons,
            formatter::ProtectionReason::OutsideEditScope));
    }
    EXPECT_TRUE(empty.IntersectsImmutable({0, 0}));
    EXPECT_TRUE(
        full.IntersectsImmutable({uint32_t(input.Size), uint32_t(input.Size)}));
    EXPECT_FALSE(full.CanReplaceGap(tree.TokenCount(), "\n    "));
    Input zero("");
    auto zeroTree = Parse(zero);
    auto zeroScope = Analyze(zeroTree, memory,
        {.EditScope = std::span<const parser::SourceRange>{}});
    EXPECT_FALSE(zeroScope.CanReplaceGap(0, " "));
    EXPECT_TRUE(zeroScope.IntersectsImmutable({0, 0}));
    Input eof("int a;");
    auto eofTree = Parse(eof);
    const std::array<parser::SourceRange, 1> eofScope{
        {{0, uint32_t(eof.Size)}},
    };
    auto noFinalLf = Analyze(eofTree, memory, {.EditScope = eofScope});
    EXPECT_FALSE(noFinalLf.CanReplaceGap(eofTree.TokenCount(), " "));
    EXPECT_TRUE(noFinalLf.IntersectsImmutable({6, 6}));
}
TEST(SourceAnalysis, EditScopeValidationAndTouchingOwnership)
{
    Input input("int a;\nint b;\n");
    auto tree = Parse(input);
    std::pmr::monotonic_buffer_resource memory;
    for (const auto& ranges : {
        std::vector<parser::SourceRange>{{2, 1}},
        std::vector<parser::SourceRange>{{1, 1}},
        std::vector<parser::SourceRange>{{0, uint32_t(input.Size + 1)}},
        std::vector<parser::SourceRange>{{0, 5}, {4, 7}},
        std::vector<parser::SourceRange>{{7, 9}, {0, 4}}})
    {
        EXPECT_THROW(
            Analyze(tree, memory, {.EditScope = ranges}),
            std::
            invalid_argument);
        auto deferred = Analyze(tree, memory);
        auto original = Analyze(tree, memory);
        EXPECT_THROW(deferred.RestrictEditScope(ranges), std::invalid_argument);
        ExpectSameAnalysis(original, deferred);
        const std::array<parser::SourceRange, 1> valid{
            {{0, uint32_t(input.Size)}},
        };
        deferred.RestrictEditScope(valid);
        auto direct = Analyze(tree, memory, {.EditScope = valid});
        ExpectSameAnalysis(direct, deferred);
    }
    const std::array<parser::SourceRange, 2> touching{{{0, 7}, {7, 14}}};
    auto analysis = Analyze(tree, memory, {.EditScope = touching});
    EXPECT_FALSE(analysis.IntersectsImmutable({6, 9}));
    EXPECT_FALSE(analysis.IntersectsImmutable({7, 7}));
    EXPECT_TRUE(analysis.IntersectsImmutable({14, 14}));
}
TEST(SourceAnalysis, PartialFinalIndentationFencesExactTopology)
{
    for (auto prefix : {"\n", "\r\n", "\n \t\r\n", "\r\n\n"}) {
        const auto source =
            std::string("void f() {") + prefix + " \tinvoke(a,b);\n}\n";
        Input input(source);
        auto tree = Parse(input);
        const auto begin = uint32_t(source.find(" \tinvoke"));
        const std::array<parser::SourceRange, 1> scope{{{begin, begin + 15}}};
        std::pmr::monotonic_buffer_resource memory;
        auto analysis = Analyze(tree, memory, {.EditScope = scope});
        const auto gap = Before(tree, "invoke");
        const auto* permission = Permission(
            analysis,
            gap,
            PermissionKind::
            FinalIndentation);
        ASSERT_NE(permission, nullptr) << prefix;
        EXPECT_EQ(permission->Source.Begin, begin);
        EXPECT_EQ(permission->Source.End, begin + 2);
        EXPECT_TRUE(analysis.Gaps()[gap].Preserve);
        EXPECT_FALSE(analysis.CanReplaceGap(gap, " "));
        EXPECT_FALSE(analysis.CanReplaceGap(gap, "\n    "));
        auto changed = source;
        changed.replace(begin, 2, "    ");
        ExpectSameTokens(tree, changed);
        EXPECT_EQ(changed.substr(0, begin), source.substr(0, begin));
        EXPECT_EQ(changed.substr(begin + 4), source.substr(begin + 2));
        const std::array<parser::SourceRange, 1> cut{{{begin + 1, begin + 15}}};
        auto partial = Analyze(tree, memory, {.EditScope = cut});
        EXPECT_EQ(
            Permission(
                partial,
                gap,
                PermissionKind::
            FinalIndentation),
            nullptr);
    }
}
TEST(SourceAnalysis, ZeroWidthIndentUsesStrictHalfOpenInsertionOwnership)
{
    Input input("void f() {\ninvoke();\n}\n");
    auto tree = Parse(input);
    const auto gap = Before(tree, "invoke");
    const auto begin = tree.Token(gap).Text.Begin;
    std::pmr::monotonic_buffer_resource memory;
    const std::array<parser::SourceRange, 1> selected{{{begin, begin + 10}}};
    auto analysis = Analyze(tree, memory, {.EditScope = selected});
    const auto* permission = Permission(
        analysis,
        gap,
        PermissionKind::
        FinalIndentation);
    ASSERT_NE(permission, nullptr);
    EXPECT_EQ(permission->Source.Begin, begin);
    EXPECT_TRUE(permission->Source.Empty());
    EXPECT_FALSE(analysis.CanReplaceGap(gap, "\n    "));
    const std::array<parser::SourceRange, 1> previous{{{0, begin}}};
    auto excluded = Analyze(tree, memory, {.EditScope = previous});
    EXPECT_EQ(
        Permission(excluded, gap, PermissionKind::FinalIndentation),
        nullptr);
    EXPECT_TRUE(excluded.IntersectsImmutable({begin, begin}));
    // The following line's zero-width indentation must not inherit ownership
    // at end, even though all existing bytes of its LF gap are selected.
    const auto closing = Before(tree, "}");
    EXPECT_EQ(
        Permission(
            analysis,
            closing,
            PermissionKind::
        FinalIndentation),
        nullptr);
    EXPECT_TRUE(analysis.Gaps()[closing].Preserve);
    EXPECT_FALSE(analysis.CanReplaceGap(closing, "\n    "));
    EXPECT_FALSE(analysis.CanReplaceGap(closing, " "));
    const std::array<parser::SourceRange, 1> withFollowingPoint{
        {{begin, begin + 11}},
    };
    auto following = Analyze(tree, memory, {.EditScope = withFollowingPoint});
    EXPECT_FALSE(following.Gaps()[closing].Preserve);
    EXPECT_TRUE(following.CanReplaceGap(closing, "\n    "));
}
TEST(SourceAnalysis, PartialTrailingAndBlankHorizontalTrimKeepsSuffix)
{
    Input input("void f() {\ninvoke(); \t\r\n \t\n  \r\n}\n");
    auto tree = Parse(input);
    const auto gap = Before(tree, "}");
    const auto begin = tree.Token(gap - 1).Text.End;
    std::pmr::monotonic_buffer_resource memory;
    const std::array<parser::SourceRange, 2> scope{
        {{begin, begin + 2}, {begin + 4, begin + 6}},
    };
    auto analysis = Analyze(tree, memory, {.EditScope = scope});
    std::vector<parser::SourceRange> slices;
    for (const auto& permission : analysis.PartialGapPermissions()) {
        if (permission.Gap == gap) {
            EXPECT_EQ(permission.Kind, PermissionKind::TrimHorizontal);
            slices.push_back(permission.Source);
        }
    }
    ASSERT_EQ(slices.size(), 2u);
    EXPECT_EQ(slices[0].Begin, begin);
    EXPECT_EQ(slices[0].End, begin + 2);
    EXPECT_EQ(slices[1].Begin, begin + 4);
    EXPECT_EQ(slices[1].End, begin + 6);
    EXPECT_FALSE(analysis.CanReplaceGap(gap, "\r\n\n\r\n"));
    auto changed = std::string(tree.Source());
    for (auto it = slices.rbegin(); it != slices.rend(); ++it) {
        changed.erase(it->Begin, it->End - it->Begin);
    }
    ExpectSameTokens(tree, changed);
    EXPECT_EQ(changed, "void f() {\ninvoke();\r\n\n  \r\n}\n");
    const std::array<parser::SourceRange, 1> cut{{{begin + 1, begin + 2}}};
    auto partial = Analyze(tree, memory, {.EditScope = cut});
    EXPECT_TRUE(partial.PartialGapPermissions().empty());
}
TEST(SourceAnalysis,
    PartialPermissionsDoNotOverrideExplicitSlicesOrSemanticConstraints)
{
    Input input("void f() {\n  invoke(); \t\n}\n");
    auto tree = Parse(input);
    const auto indentGap = Before(tree, "invoke");
    const auto trimGap = Before(tree, "}");
    const auto indent = tree.Token(indentGap).Text.Begin - 2;
    const auto trim = tree.Token(trimGap - 1).Text.End;
    const std::array<parser::SourceRange, 1> scope{{{indent, trim + 2}}};
    std::pmr::monotonic_buffer_resource memory;
    const std::array<parser::SourceRange, 1> prefix{{{indent - 1, indent}}};
    auto allowed =
        Analyze(tree, memory, {.ImmutableRanges = prefix, .EditScope = scope});
    EXPECT_NE(
        Permission(
            allowed,
            indentGap,
            PermissionKind::
        FinalIndentation),
        nullptr);
    EXPECT_FALSE(allowed.CanReplaceGap(indentGap, "\n    "));
    auto deferred = Analyze(tree, memory, {.ImmutableRanges = prefix});
    ASSERT_TRUE(deferred.Gaps()[indentGap].Preserve);
    deferred.RestrictEditScope(scope);
    ExpectSameAnalysis(allowed, deferred);
    const std::array<formatter::ProtectedSourceRange, 1> headerPrefix{
        {{prefix[0], formatter::ProtectionReason::HeaderName}},
    };
    auto foldedContext = Analyze(
        tree,
        memory,
        {
            .InheritedProtection =
        headerPrefix,
        });
    ASSERT_TRUE(foldedContext.Gaps()[indentGap].ContextSensitive);
    foldedContext.RestrictEditScope(scope);
    EXPECT_NE(
        Permission(foldedContext, indentGap, PermissionKind::FinalIndentation),
        nullptr);
    EXPECT_FALSE(foldedContext.CanReplaceGap(indentGap, "\n    "));
    const std::array<parser::SourceRange, 2> fenced{
        {{indent, indent + 1}, {trim, trim + 1}},
    };
    auto denied =
        Analyze(tree, memory, {.ImmutableRanges = fenced, .EditScope = scope});
    EXPECT_TRUE(denied.PartialGapPermissions().empty());
    for (auto reason :
        {
            formatter::ProtectionReason::Disabled,
            formatter::ProtectionReason::SensitiveMacro,
            formatter::
            ProtectionReason::Recovery,
            formatter::ProtectionReason::Ambiguous,
            formatter::
            ProtectionReason::Directive,
            formatter::ProtectionReason::LineSplice,
            formatter::
            ProtectionReason::HeaderName,
        })
    {
        const std::array<formatter::ProtectedSourceRange, 1> protection{
            {{{indent, trim + 2}, reason}},
        };
        auto blocked = Analyze(
            tree,
            memory,
            {
                .InheritedProtection =
            protection,
                .EditScope = scope,
            });
        EXPECT_TRUE(blocked.PartialGapPermissions().empty());
        EXPECT_FALSE(blocked.CanReplaceGap(indentGap, "\n    "));
        EXPECT_TRUE(Has(blocked.Gaps()[indentGap].Reasons, reason));
        EXPECT_TRUE(Has(
            blocked.Gaps()[indentGap].Reasons,
            formatter::
            ProtectionReason::OutsideEditScope));
        const auto covering = std::find_if(
            blocked.Protection().begin(),
            blocked.Protection().end(),
            [&] (const auto& range) {
                return range.Source.Begin <= indent &&
                    trim + 2 <= range.Source.End;
            });
        ASSERT_NE(covering, blocked.Protection().end());
        EXPECT_EQ(covering->Source.Begin, indent);
        EXPECT_EQ(covering->Source.End, trim + 2);
        EXPECT_EQ(covering->Reasons, reason);
        EXPECT_EQ(blocked.Protection().front().Source.End, indent);
        EXPECT_EQ(
            blocked.Protection().front().Reasons,
            formatter::ProtectionReason::OutsideEditScope);
    }
}
TEST(SourceAnalysis, PartialCommentOwnedCrAndDirectivePrefix)
{
    for (auto prefix : {"// note\r\n", "#define VALUE 1\r\n"}) {
        const auto source = std::string(prefix) + "  int tail;\n";
        Input input(source);
        auto tree = Parse(input);
        const auto begin = uint32_t(source.find("  int"));
        const std::array<parser::SourceRange, 1> scope{
            {{begin, uint32_t(source.size())}},
        };
        std::pmr::monotonic_buffer_resource memory;
        auto analysis = Analyze(tree, memory, {.EditScope = scope});
        const auto gap = Before(tree, "int");
        EXPECT_TRUE(analysis.Gaps()[gap].MustStartWithLineFeed);
        EXPECT_TRUE(analysis.Original(gap).starts_with('\n'));
        const auto* permission = Permission(
            analysis,
            gap,
            PermissionKind::
            FinalIndentation);
        ASSERT_NE(permission, nullptr);
        EXPECT_EQ(permission->Source.Begin, begin);
        auto changed = source;
        changed.replace(begin, 2, "\t");
        ExpectSameTokens(tree, changed);
        EXPECT_FALSE(analysis.CanReplaceGap(gap, "\r\n\t"));
    }
}
TEST(SourceAnalysis, PartialHeaderOpeningPointAndTokenOwnedTrailingWhitespace)
{
    Input input("import\n<pkg/sub.h>;\n// note  \r\nint tail;\n");
    auto tree = Parse(input);
    const auto opener = Before(tree, "<");
    const auto begin = tree.Token(opener).Text.Begin;
    const auto comment = Before(tree, "// note  \r");
    const auto tail = Before(tree, "int");
    std::pmr::monotonic_buffer_resource memory;
    const std::array<parser::SourceRange, 2> scope{
        {
            {begin, tree.Token(opener).Text.End},
            {tree.Token(comment).Text.End - 3, tree.Token(tail).Text.Begin + 1},
        },
    };
    auto analysis = Analyze(tree, memory, {.EditScope = scope});
    EXPECT_NE(
        Permission(
            analysis,
            opener,
            PermissionKind::
        FinalIndentation),
        nullptr);
    EXPECT_EQ(
        Permission(analysis, tail, PermissionKind::TrimHorizontal),
        nullptr);
    const std::array<parser::SourceRange, 1> explicitRange{
        {{begin, begin + 1}},
    };
    auto denied = Analyze(
        tree,
        memory,
        {.ImmutableRanges = explicitRange, .EditScope = scope});
    EXPECT_EQ(
        Permission(denied, opener, PermissionKind::FinalIndentation),
        nullptr);
}
TEST(SourceAnalysis, PartialUnsafeRealSyntaxAndUnsupportedWhitespace)
{
    for (auto source :
        {
            "// hit-fmt off\n  int frozen; \t\n// hit-fmt on\n",
            "void f() { KEEP(\n  a \t\n); }\n",
            "void f() {\n  @; \t\n}\n",
            "void f() {\n  unknown * value; \t\n}\n",
            "int x = \\\n  1; \t\n",
            "import <\n  pkg / unit.h \t\n>;\n",
            "[\n  [ x ] \t\n]\n",
            "void f() {\r  invoke(); \t\r}\n",
            "void f() {\n\f  invoke(); \t\f\n}\n",
        })
    {
        Input input(source);
        auto tree = Parse(input);
        std::vector<parser::SourceRange> scope;
        // Select only complete horizontal runs, never newline topology.
        for (uint32_t i = 0; i < input.Size;) {
            const auto begin = i;
            while (i < input.Size && (source[i] == ' ' || source[i] == '\t')) {
                ++i;
            }
            if (begin < i) {
                scope.push_back({begin, i});
            } else {
                ++i;
            }
        }
        const std::array names{std::string_view("KEEP")};
        std::pmr::monotonic_buffer_resource memory;
        auto analysis = Analyze(
            tree,
            memory,
            {
                .WhitespaceSensitiveMacros =
            names,
                .EditScope = scope,
            });
        // Some independent gaps can still be safe, but every emitted permission
        // must remain outside actual protection and whole-gap semantic fences.
        auto base = Analyze(tree, memory, {.WhitespaceSensitiveMacros = names});
        for (const auto& permission : analysis.PartialGapPermissions()) {
            EXPECT_FALSE(base.IntersectsImmutable(permission.Source)) << source;
            EXPECT_FALSE(base.Gaps()[permission.Gap].ContextSensitive)
                << source;
            EXPECT_FALSE(base.Gaps()[permission.Gap].Preserve &&
                base.Gaps()[permission.Gap].Reasons == formatter::
                    ProtectionReason::None) << source;
        }
        for (formatter::GapId gap = 0; gap < base.Gaps().size(); ++gap) {
            if (base.Gaps()[gap].Preserve ||
                base.Gaps()[gap].ContextSensitive ||
                base.Original(gap).find_first_of("\f\v") != std::
                    string_view::npos)
            {
                EXPECT_EQ(
                    Permission(
                        analysis,
                        gap,
                        PermissionKind::
                    FinalIndentation),
                    nullptr) << source;
                EXPECT_EQ(
                    Permission(
                        analysis,
                        gap,
                        PermissionKind::
                    TrimHorizontal),
                    nullptr) << source;
            }
        }
    }
}
TEST(SourceAnalysis, EditScopeAndScratchStorageMayDieAfterBuild)
{
    Input input("void f() {\n  invoke(); \t\n}\n");
    auto tree = Parse(input);
    std::pmr::monotonic_buffer_resource memory;
    auto analysis = [&] {
        std::pmr::monotonic_buffer_resource scratch;
        std::vector<parser::SourceRange> scope{{11, 24}};
        return formatter::BuildSourceAnalysis(
            tree,
            {.EditScope = scope},
            memory,
            scratch);
    }();
    const auto gap = Before(tree, "invoke");
    EXPECT_NE(
        Permission(analysis, gap, PermissionKind::FinalIndentation),
        nullptr);
    EXPECT_NE(
        Permission(
            analysis,
            Before(tree, "}"),
            PermissionKind::
        TrimHorizontal),
        nullptr);
    EXPECT_FALSE(analysis.CanReplaceGap(gap, "\n    "));
    EXPECT_FALSE(analysis.IntersectsImmutable({11, 13}));
    EXPECT_TRUE(analysis.IntersectsImmutable({24, 24}));
}
TEST(SourceAnalysis, DeferredRestrictionRequiresBuiltUnrestrictedAnalysis)
{
    Input input("void f() {\n  invoke(); \t\n}\n");
    auto tree = Parse(input);
    std::pmr::monotonic_buffer_resource memory;
    const std::array<parser::SourceRange, 1> scope{{{11, 24}}};
    formatter::SourceAnalysis unbuilt(memory);
    EXPECT_THROW(unbuilt.RestrictEditScope(scope), std::logic_error);
    auto original = Analyze(tree, memory);
    auto moved = std::move(original);
    EXPECT_THROW(original.RestrictEditScope(scope), std::logic_error);
    moved.RestrictEditScope(scope);
    auto direct = Analyze(tree, memory, {.EditScope = scope});
    EXPECT_THROW(moved.RestrictEditScope({}), std::logic_error);
    EXPECT_THROW(moved.RestrictEditScope(scope), std::logic_error);
    ExpectSameAnalysis(direct, moved);
    auto restrictedMove = std::move(moved);
    EXPECT_THROW(moved.RestrictEditScope(scope), std::logic_error);
    EXPECT_THROW(restrictedMove.RestrictEditScope(scope), std::logic_error);
    ExpectSameAnalysis(direct, restrictedMove);
    auto empty = Analyze(tree, memory, {.EditScope = std::span<const
        parser::SourceRange>{}});
    EXPECT_THROW(empty.RestrictEditScope(scope), std::logic_error);
    EXPECT_FALSE(empty.CanReplaceGap(Before(tree, "invoke"), "\n    "));
}
TEST(SourceAnalysis, DeferredRestrictionOutlivesTreeScopeAndScratch)
{
    Input input("void f() {\n  invoke(); \t\n}\n");
    std::pmr::monotonic_buffer_resource memory;
    formatter::GapId indent = 0;
    formatter::GapId trim = 0;
    std::optional<formatter::SyntaxSpacing> spacing;
    auto analysis = [&] {
        auto tree = Parse(input);
        indent = Before(tree, "invoke");
        trim = Before(tree, "}");
        auto safety = Analyze(tree, memory);
        std::pmr::monotonic_buffer_resource scratch;
        spacing.emplace(formatter::BuildSyntaxSpacing(
            tree, safety, formatter::GapPolicies{},
            formatter::PointerReferenceDeclaratorLocation::Preserve,
            memory, scratch));
        return safety;
    }(); // Both tree and build scratch have died; source/file memory remain.
    EXPECT_EQ(analysis.Original(indent), "\n  ");
    const auto owner = spacing->Gaps()[indent + 1].Owner;
    ASSERT_NE(owner, formatter::NoRegion);
    ASSERT_LT(owner, analysis.Regions().size());
    EXPECT_EQ(analysis.Regions()[owner].Kind, parser::NodeKind::CallExpression);
    for (formatter::RegionId id = 0; id < analysis.Regions().size(); ++id) {
        const auto& region = analysis.Regions()[id];
        for (auto child = region.FirstChild;
            child != formatter::NoRegion;
            child = analysis.Regions()[child].NextSibling)
        {
            ASSERT_GT(child, id);
            ASSERT_LT(child, analysis.Regions().size());
            EXPECT_EQ(analysis.Regions()[child].Parent, id);
            const auto next = analysis.Regions()[child].NextSibling;
            if (next != formatter::NoRegion) {
                ASSERT_GT(next, child);
            }
        }
        EXPECT_LE(region.Attached.Begin, region.Tokens.Begin);
        EXPECT_GE(region.Attached.End, region.Tokens.End);
    }
    {
        std::vector<parser::SourceRange> scope{{11, 24}};
        analysis.RestrictEditScope(scope);
    }
    EXPECT_NE(
        Permission(analysis, indent, PermissionKind::FinalIndentation),
        nullptr);
    EXPECT_NE(
        Permission(analysis, trim, PermissionKind::TrimHorizontal),
        nullptr);
    EXPECT_FALSE(analysis.CanReplaceGap(indent, "\n    "));
    EXPECT_FALSE(analysis.IntersectsImmutable({11, 13}));
    EXPECT_TRUE(analysis.IntersectsImmutable({24, 24}));
}
class CountingResource
    : public std::pmr::memory_resource
{
public:
    size_t BytesRequested = 0;
    size_t Calls = 0;
    std::optional<size_t> AllocationsUntilFailure;
private:
    void* do_allocate(size_t bytes, size_t alignment) override
    {
        if (AllocationsUntilFailure) {
            if (*AllocationsUntilFailure == 0) {
                throw std::bad_alloc();
            }
            --*AllocationsUntilFailure;
        }
        BytesRequested += bytes;
        ++Calls;
        return std::pmr::new_delete_resource()->allocate(bytes, alignment);
    }

    void do_deallocate(void* pointer, size_t bytes, size_t alignment) override
    {
        std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
    }

    bool do_is_equal(
        const std::pmr::memory_resource& other) const noexcept override
    {
        return this == &other;
    }
};
TEST(SourceAnalysis,
    DeferredRestrictionAllocationFailureLeavesAnalysisUnchanged)
{
    Input input("void f() {\n  invoke(); \t\n}\n");
    auto tree = Parse(input);
    const std::array<parser::SourceRange, 1> scope{{{11, 24}}};
    std::pmr::monotonic_buffer_resource memory;
    auto original = Analyze(tree, memory);
    auto direct = Analyze(tree, memory, {.EditScope = scope});
    size_t failures = 0;
    for (size_t budget = 0; budget < 16; ++budget) {
        CountingResource file;
        auto deferred = Analyze(tree, file);
        file.AllocationsUntilFailure = budget;
        try {
            deferred.RestrictEditScope(scope);
            ExpectSameAnalysis(direct, deferred);
            break;
        } catch (const std::bad_alloc&) {
            ++failures;
            ExpectSameAnalysis(original, deferred);
            file.AllocationsUntilFailure.reset();
            deferred.RestrictEditScope(scope);
            ExpectSameAnalysis(direct, deferred);
        }
    }
    // Exercise scope, both sparse permission growth allocations and protection.
    EXPECT_EQ(failures, 4u);
}
TEST(SourceAnalysis, AdversarialMarkersHaveLinearStorage)
{
    // Cached first/last content offsets keep each marker lookup constant-time.
    // Growing both the shared prefix and marker count checks correctness and
    // linear storage without a flaky wall-clock assertion, not as a timing
    // surrogate.
    for (size_t count : {128u, 2048u, 32768u}) {
        std::string source(count, ' ');
        for (size_t i = 0; i < count; ++i) {
            source += "/* hit-fmt off */ ";
        }
        source.append(count, ' ');
        source += "\nint after;\n";
        Input input(source);
        auto tree = Parse(input);
        CountingResource file;
        CountingResource scratch;
        auto analysis = formatter::BuildSourceAnalysis(tree, {}, file, scratch);
        EXPECT_EQ(tree.TokenCount(), count + 3);
        EXPECT_EQ(analysis.Regions().size(), count + 7);
        EXPECT_TRUE(analysis.Protection().empty());
        EXPECT_TRUE(analysis.CanReplaceGap(Before(tree, "after"), "  "));
        EXPECT_LE(
            file.BytesRequested + scratch.BytesRequested,
            source.size() * 128);
        const auto calls = file.Calls + scratch.Calls;
        for (formatter::GapId gap = 1; gap < count; ++gap) {
            EXPECT_TRUE(analysis.CanReplaceGap(gap, "  "));
        }
        EXPECT_EQ(file.Calls + scratch.Calls, calls);
    }
}
TEST(SourceAnalysis, IterativeClassificationAndScratchLifetime)
{
    std::string text;
    for (size_t i = 0; i < 2000; ++i) {
        text += "int x = 1;\n";
    }
    Input input(text);
    auto tree = Parse(input);
    std::pmr::monotonic_buffer_resource memory;
    auto analysis = Analyze(tree, memory); // scratch is already destroyed
    EXPECT_EQ(
        analysis.Gaps().size(),
        static_cast<size_t>(tree.TokenCount()) + 1);
    EXPECT_TRUE(analysis.CanReplaceGap(Before(tree, "x", 1999), "  "));
    EXPECT_EQ(analysis.Regions()[0].Status, formatter::RegionStatus::Complete);
    // Many disjoint line scopes exercise sparse point permissions without
    // per-hunk rescans or retaining the caller's interval storage.
    std::vector<parser::SourceRange> scope;
    for (uint32_t i = 0; i < 2000; ++i) {
        scope.push_back({i * 11, i * 11 + 10});
    }
    CountingResource scopedFile;
    CountingResource scopedScratch;
    auto scoped = formatter::BuildSourceAnalysis(
        tree,
        {.EditScope = scope},
        scopedFile,
        scopedScratch);
    CountingResource deferredFile;
    CountingResource deferredScratch;
    auto deferred =
        formatter::BuildSourceAnalysis(tree, {}, deferredFile, deferredScratch);
    const auto scratchCalls = deferredScratch.Calls;
    const auto beforeBytes = deferredFile.BytesRequested;
    const auto beforeCalls = deferredFile.Calls;
    deferred.RestrictEditScope(scope);
    ExpectSameAnalysis(scoped, deferred);
    EXPECT_EQ(deferredScratch.Calls, scratchCalls);
    EXPECT_LE(
        deferredFile.BytesRequested - beforeBytes,
        scope.size() *
        (sizeof(parser::SourceRange) +
            4 * sizeof(formatter::PartialGapPermission) +
            2 * sizeof(formatter::ProtectedSourceRange)));
    EXPECT_LE(deferredFile.Calls - beforeCalls, 16u);
    for (size_t i = 1; i < deferred.PartialGapPermissions().size(); ++i) {
        EXPECT_LT(
            deferred.PartialGapPermissions()[i - 1].Source.Begin,
            deferred.PartialGapPermissions()[i].Source.Begin);
    }
    ASSERT_EQ(scoped.PartialGapPermissions().size(), 1999u);
    EXPECT_EQ(
        scoped.PartialGapPermissions().back().Kind,
        PermissionKind::FinalIndentation);
    EXPECT_TRUE(scoped.PartialGapPermissions().back().Source.Empty());
    EXPECT_LE(
        scopedFile.BytesRequested + scopedScratch.BytesRequested,
        text.size() * 256);
    const auto calls = scopedFile.Calls + scopedScratch.Calls;
    for (uint32_t i = 0; i < 2000; ++i) {
        EXPECT_FALSE(scoped.IntersectsImmutable({i * 11, i * 11}));
        EXPECT_TRUE(scoped.IntersectsImmutable({i * 11 + 10, i * 11 + 10}));
    }
    EXPECT_EQ(scopedFile.Calls + scopedScratch.Calls, calls);
}
TEST(SourceAnalysis, NewlineSpellingOwnsFullExistingBytesNotEndInsertions)
{
    Input input("int a;\r\nint b;\r\n");
    auto tree = Parse(input);
    std::pmr::monotonic_buffer_resource memory;
    const std::array selected{
        parser::SourceRange{6, 8}, parser::SourceRange{14, 16}
    };
    auto safety = Analyze(tree, memory, {.EditScope = selected});
    for (formatter::GapId i = 0; i < safety.Gaps().size(); ++i) {
        const auto& gap = safety.Gaps()[i];
        if (safety.Original(gap) == "\r\n") {
            EXPECT_TRUE(safety.CanSpellGapNewlines(i));
            EXPECT_TRUE(gap.Preserve);
            EXPECT_FALSE(safety.CanReplaceGap(i, "\n"));
            EXPECT_TRUE(
                safety.IntersectsImmutable({gap.Source.End, gap.Source.End}));
        } else {
            EXPECT_FALSE(safety.CanSpellGapNewlines(i));
        }
    }
    EXPECT_FALSE(safety.CanSpellGapNewlines(UINT32_MAX));
    const std::array half{
        parser::SourceRange{6, 7}, parser::SourceRange{15, 16}
    };
    auto partial = Analyze(tree, memory, {.EditScope = half});
    for (formatter::GapId i = 0; i < partial.Gaps().size(); ++i)
        EXPECT_FALSE(partial.CanSpellGapNewlines(i));
    const std::array immutable{parser::SourceRange{7, 8}};
    auto protectedGap = Analyze(
        tree,
        memory,
        {.ImmutableRanges = immutable, .EditScope = selected});
    EXPECT_FALSE(protectedGap.CanSpellGapNewlines(3));
    auto empty = Analyze(tree, memory,
        {.EditScope = std::span<const parser::SourceRange>{}});
    for (formatter::GapId i = 0; i < empty.Gaps().size(); ++i)
        EXPECT_FALSE(empty.CanSpellGapNewlines(i));
    Input splice("int x=1+\\\r\n 2;\n");
    auto spliceTree = Parse(splice);
    auto intrinsic = Analyze(spliceTree, memory);
    for (formatter::GapId i = 0; i < intrinsic.Gaps().size(); ++i)
        if (intrinsic.Gaps()[i].IntrinsicPreserve) {
            EXPECT_FALSE(intrinsic.CanSpellGapNewlines(i));
        }
    const std::string recoverySource =
        "template<class T> requires requires(T v) { v.touch(); } @\r\n"
            "int inspect(T value) { return 1; }\r\n";
    Input recovery(recoverySource);
    auto recoveryTree = Parse(recovery);
    auto recovered = Analyze(recoveryTree, memory);
    const auto newline = Before(recoveryTree, "int");
    const auto invalid = parser::SourceOffset(recoverySource.find('@'));
    ASSERT_EQ(recovered.Original(newline), "\r\n");
    EXPECT_EQ(recovered.Gaps()[newline].Source.Begin, invalid + 1);
    EXPECT_EQ(recovered.Gaps()[newline].Source.End, invalid + 3);
    bool recoveryOwnsAtom = false;
    for (const auto& range : recovered.Protection()) {
        recoveryOwnsAtom |=
            Has(range.Reasons, formatter::ProtectionReason::Recovery) &&
                range.Source.Begin <= invalid + 1 &&
                range.Source.End >= invalid + 3;
    }
    EXPECT_TRUE(recoveryOwnsAtom);
    EXPECT_FALSE(recovered.CanSpellGapNewlines(newline));
    ASSERT_EQ(recovered.Original(recovered.Gaps().back()), "\r\n");
    EXPECT_TRUE(recovered.CanSpellGapNewlines(recoveryTree.TokenCount()));

}

} // namespace
