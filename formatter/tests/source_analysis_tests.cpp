#include <formatter/source_analysis.hpp>
#include <parser/parser.hpp>
#include <tests/golden.hpp>

#include <gtest/gtest.h>

#include <array>
#include <filesystem>
#include <format>
#include <memory_resource>
#include <limits>
#include <stdexcept>
#include <string>
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

formatter::SourceAnalysis Analyze(
    const parser::SyntaxTree& tree,
    std::pmr::memory_resource& memory,
    formatter::SourceAnalysisOptions options = {})
{
    std::pmr::monotonic_buffer_resource scratch;
    return formatter::BuildSourceAnalysis(tree, options, memory, scratch);
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

TEST(SourceAnalysis, DiagnosticGolden)
{
    const auto directory = std::filesystem::path(__FILE__).parent_path() /
        "testdata/source-analysis";
    const auto input = test::golden::ReadFile(
        directory / "protection.input.cpp");
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
        if (gap.Preserve || gap.LineBoundary !=
            formatter::GapLineBoundary::Any ||
            gap.Separation == formatter::GapSeparation::Required)
        {
            dump += std::format(
                "gap {} [{},{}) preserve={} separation={} line={} "
                "lf-first={} context={}\n",
                i, gap.Source.Begin, gap.Source.End, gap.Preserve,
                static_cast<unsigned>(gap.Separation), static_cast<unsigned>(
                    gap.LineBoundary), gap.MustStartWithLineFeed,
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

TEST(SourceAnalysis, AlphabeticOperatorsRequireWordSeparation)
{
    Input input("bool value = alpha and not beta;\n");
    auto tree = Parse(input);
    std::pmr::monotonic_buffer_resource memory;
    auto analysis = Analyze(tree, memory);
    for (auto name : {"and", "not", "beta"}) {
        EXPECT_FALSE(analysis.CanReplaceGap(Before(tree, name), "")) << name;
    }
}

TEST(SourceAnalysis, LineTokenTerminationAndEofKeepExactSpelling)
{
    for (auto source : {"// note\nint x;\n", "// note\r\nint x;\r\n",
        "// note", "// note\r", "#define X 1", "%:define X 1"})
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
            changed.replace(gap.Source.Begin, gap.Original.size(), "\n  ");
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
            EXPECT_FALSE(analysis.CanReplaceGap(i,
                analysis.Gaps()[i].Original.empty() ? " " : ""));
        }
        for (auto outside : {region.Tokens.Begin, region.Tokens.End}) {
            EXPECT_TRUE(analysis.CanReplaceGap(outside, "  "));
            auto changed = std::string(tree.Source());
            const auto& gap = analysis.Gaps()[outside];
            changed.replace(gap.Source.Begin, gap.Original.size(), "  ");
            ExpectSameTokens(tree, changed);
        }
    }
    EXPECT_EQ(headers, 3u);
    EXPECT_TRUE(analysis.CanReplaceGap(Before(tree, "tail"), "  "));
    const auto opener = Before(tree, "<");
    const auto token = tree.Token(opener);
    const std::array<parser::SourceRange, 1> ranges{{
        {token.Text.Begin, token.Text.End},
    }};
    auto excluded = Analyze(tree, memory, {.ImmutableRanges = ranges});
    EXPECT_FALSE(excluded.CanReplaceGap(opener, "  "));
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
            EXPECT_FALSE(Has(range.Reasons,
                formatter::ProtectionReason::Disabled));
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
    Input input("void f() { unknown * value; int good = 1; @; }\n"
        "int tail = 2;\n");
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
    Input input("void f() { int alpha = 1; int beta = 2; "
        "alpha = beta + +alpha; "
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
        for (auto replacement : {
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
                    gap.Original.size(),
                    replacement);
                ExpectSameTokens(tree, changed);
            }
        }
        together += analysis.CanReplaceGap(i, "")
            ? std::string_view{} : gap.Original;
        if (i < tree.TokenCount()) {
            together += tree.TokenText(i);
        }
    }
    ExpectSameTokens(tree, together);
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
    const std::array spellings{"alpha",
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
    Input input(". . . < :: > / * > >");
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
    EXPECT_EQ(formatter::CheckedSourceAnalysisSize(util::MaxSourceSize),
        util::MaxSourceSize);
    EXPECT_THROW(static_cast<void>(formatter::CheckedSourceAnalysisSize(
        util::MaxSourceSize + 1)), std::length_error);
    EXPECT_THROW(static_cast<void>(formatter::CheckedSourceAnalysisSize(
        std::numeric_limits<size_t>::max())), std::length_error);
    if constexpr (sizeof(size_t) > sizeof(parser::SourceOffset)) {
        EXPECT_THROW(static_cast<void>(formatter::CheckedSourceAnalysisSize(
            uint64_t{1} << 32)), std::length_error);
    }
}

class CountingResource : public std::pmr::memory_resource
{
public:
    size_t BytesRequested = 0;
    size_t Calls = 0;

private:
    void* do_allocate(size_t bytes, size_t alignment) override
    {
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

TEST(SourceAnalysis, AdversarialMarkersHaveLinearStorage)
{
    // Each marker candidate used to scan the same N-byte prefix. Growing both
    // axes exercises that path without a flaky wall-clock assertion. The
    // classifier now uses cached first/last content offsets (constant work per
    // candidate); these assertions check correctness and linear storage as the
    // workload grows, not a timing surrogate.
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
        EXPECT_LE(file.BytesRequested + scratch.BytesRequested,
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
}

} // namespace
