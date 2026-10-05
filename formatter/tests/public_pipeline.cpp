#include "public_pipeline.hpp"

#include <formatter/config.hpp>
#include <formatter/formatter.hpp>
#include <formatter/source_analysis.hpp>

#include <gtest/gtest.h>

#include <tests/golden.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <memory_resource>
#include <stdexcept>
#include <string>

namespace public_pipeline {

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

void CheckVisibleResults(
    const formatter::FormatResult& mapped,
    const formatter::FormatResult& plain)
{
    EXPECT_EQ(plain.Text, mapped.Text);
    EXPECT_TRUE(plain.Map.empty());
    EXPECT_TRUE(plain.OriginalTokens.empty());
    EXPECT_TRUE(plain.OutputProtection.empty());
    EXPECT_TRUE(plain.StructuralOperations.empty());
    ASSERT_EQ(
        plain.StructuralDiagnostics.size(),
        mapped.StructuralDiagnostics.size());
    for (size_t i = 0; i < mapped.StructuralDiagnostics.size(); ++i) {
        const auto& a = mapped.StructuralDiagnostics[i];
        const auto& b = plain.StructuralDiagnostics[i];
        EXPECT_EQ(a.Action, b.Action);
        EXPECT_EQ(a.Tokens.Begin, b.Tokens.Begin);
        EXPECT_EQ(a.Tokens.End, b.Tokens.End);
        EXPECT_EQ(a.Reason, b.Reason);
    }
    ASSERT_EQ(plain.Diagnostics.size(), mapped.Diagnostics.size());
    for (size_t i = 0; i < mapped.Diagnostics.size(); ++i) {
        EXPECT_EQ(
            plain.Diagnostics[i].Position.Line,
            mapped.Diagnostics[i].Position.Line);
        EXPECT_EQ(
            plain.Diagnostics[i].Position.Column,
            mapped.Diagnostics[i].Position.Column);
        EXPECT_EQ(plain.Diagnostics[i].Message, mapped.Diagnostics[i].Message);
        EXPECT_EQ(plain.Diagnostics[i].Kind, mapped.Diagnostics[i].Kind);
    }
}

formatter::FormatResult FormatInput(
    std::string_view source,
    const formatter::PreparedFormat& prepared,
    formatter::FormatRequest request = {.SourceMap = true})
{
    Input input(source);
    auto result = formatter::Format(input.View(), prepared, request);
    if (request.SourceMap) {
        // The metadata-free path must retain the same namespace edits, scope
        // and protection decisions, and final diagnostics across these cases.
        request.SourceMap = false;
        const auto plain = formatter::Format(input.View(), prepared, request);
        CheckVisibleResults(result, plain);
    }
    return result;
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
    std::string_view source,
    const formatter::FormatResult& result)
{
    Input input(source);
    auto tree = parser::Parse(input.View(), [] (parser::ParseError) {});
    ASSERT_EQ(result.OriginalTokens.size(), tree.TokenCount());
    for (parser::TokenIndex i = 0; i < tree.TokenCount(); ++i)
    {
        const auto map = result.OriginalTokens[i];
        if (map.Present) {
            EXPECT_EQ(
                std::string_view(result.Text).substr(
                    map.Output.Begin,
                    map.Output.End - map.Output.Begin),
                tree.TokenText(i)) << i;
        }
    }
    for (const auto& map : result.Map) {
        EXPECT_LE(map.Output.Begin, map.Output.End);
        EXPECT_LE(map.Output.End, result.Text.size());
        if (map.Exact && map.HasOriginal) {
            EXPECT_EQ(
                std::string_view(result.Text).substr(
                    map.Output.Begin,
                    map.Output.End - map.Output.Begin),
                source.substr(
                    map.Original.Begin,
                    map.Original.End - map.Original.Begin));
        }
        if (map.Action != formatter::NoStructuralAction) {
            ASSERT_LT(map.Action, result.StructuralOperations.size());
            EXPECT_LE(
                result.StructuralOperations[map.Action].Owner.End,
                tree.TokenCount());
        }
        if (map.OriginalToken != document::NoIdentity) {
            ASSERT_LT(map.OriginalToken, result.OriginalTokens.size());
            EXPECT_TRUE(result.OriginalTokens[map.OriginalToken].Present);
            EXPECT_FALSE(map.Generated);
        }
    }
}

// Maps are evidence of exact preservation, not edit authorization. Every
// excluded original byte must survive once, even through moved/deleted pieces.
void CheckExcludedCopies(
    std::string_view source,
    const formatter::FormatResult& result,
    std::span<const parser::SourceRange> selected)
{
    std::vector<unsigned> copies(source.size(), 0);
    for (const auto& map : result.Map) {
        if (map.Exact && map.HasOriginal) {
            for (auto i = map.Original.Begin; i < map.Original.End; ++i) {
                ASSERT_LT(i, copies.size());
                ++copies[i];
            }
        }
    }
    size_t range = 0;
    for (size_t i = 0; i < source.size(); ++i)
    {
        while (range < selected.size() && selected[range].End <= i) {
            ++range;
        }
        if (range == selected.size() || i < selected[range].Begin) {
            EXPECT_EQ(copies[i], 1u) << "excluded original byte " << i;
        }
    }
}

TEST(PublicPipeline, ChainStructuralBaseHonorsGapScopeAndProtection)
{
    auto style = Canonical();
    style.IndentWidth = 4;
    style.ContinuationOffset = 4;
    style.Layout = formatter::LayoutMode::Preserve;
    style.ExpressionsLayout = formatter::CategoryLayout::Canonical;
    style.ChainsLayout = formatter::CategoryLayout::Canonical;
    style.ChainsBreak = formatter::BreakRequirement::Always;
    style.ChainsPacking = formatter::ListPacking::VerticalWhenBroken;
    const formatter::PreparedFormat preserveIndent(style);
    const std::string source = "void f() {\n    auto value = engine()\n"
        "            .Open()\n            .Finish();\n}\n";
    const auto begin = parser::SourceOffset(source.find("            .Open"));
    const parser::SourceRange selected[] = {{begin, begin + 12}};
    formatter::FormatRequest request{.SourceMap = true, .EditScope = selected};
    const auto preserved = FormatInput(source, preserveIndent, request);
    EXPECT_EQ(preserved.Text, source);
    CheckIdentities(source, preserved);
    CheckExcludedCopies(source, preserved, selected);
    Input input(source);
    const auto tree = parser::Parse(input.View(), [] (parser::ParseError) {});
    std::pmr::monotonic_buffer_resource file;
    std::pmr::monotonic_buffer_resource scratch;
    formatter::SourceAnalysisOptions options;
    options.EditScope = selected;
    const auto safety =
        formatter::BuildSourceAnalysis(tree, options, file, scratch);
    ASSERT_EQ(
        safety
            .PartialGapPermissions()
            .size(),
        1u);
    const auto permission = safety
        .PartialGapPermissions()
        .front();
    EXPECT_EQ(
        permission.Kind,
        formatter::PartialGapPermissionKind::FinalIndentation);
    EXPECT_EQ(permission.Source.Begin, begin);
    EXPECT_EQ(permission.Source.End, begin + 12);
    const auto& gap = safety.Gaps()[permission.Gap];
    EXPECT_EQ(gap.Source.Begin, begin - 1);
    EXPECT_EQ(gap.Source.End, begin + 12);
    EXPECT_EQ(safety.Original(gap), "\n            ");
    EXPECT_TRUE(gap.Preserve);
    EXPECT_NE(
        static_cast<uint16_t>(gap.Reasons) &
        static_cast<uint16_t>(formatter::ProtectionReason::OutsideEditScope),
        0u);
    // A permission is not a style demand. Only explicit Spaces selects the
    // narrow final-indent edit, leaving the LF and excluded endpoint exact.
    style.IndentationKind = formatter::IndentationKind::Spaces;
    const formatter::PreparedFormat prepared(style);
    const auto partial = FormatInput(source, prepared, request);
    auto expected = source;
    expected.replace(begin, 12, "        ");
    EXPECT_EQ(partial.Text, expected);
    CheckIdentities(source, partial);
    CheckExcludedCopies(source, partial, selected);
    request.EditScope = std::span<const parser::SourceRange>{};
    const auto denied = FormatInput(source, prepared, request);
    EXPECT_EQ(denied.Text, source);
    CheckExcludedCopies(source, denied, {});
    request.EditScope = std::nullopt;
    request.ImmutableRanges = selected;
    const auto immutable = FormatInput(source, prepared, request);
    EXPECT_NE(immutable.Text.find("\n            .Open()"), std::string::npos);
    CheckIdentities(source, immutable);
    const std::string protectedSource =
        "// clang-format off\n" + source + "// clang-format on\n";
    request.ImmutableRanges = {};
    EXPECT_EQ(
        FormatInput(protectedSource, prepared, request).Text,
        protectedSource);
}

TEST(PublicPipeline, StyleCheckpointKeepsIndependentGapPermissions)
{
    const std::string source = "void f() {\r\n    auto value = engine()\r\n"
        "            .Open()\r\n            .Finish(); \t// note\n}\r\n";
    auto style = Canonical();
    style.Layout = formatter::LayoutMode::Preserve;
    style.ExpressionsLayout = formatter::CategoryLayout::Canonical;
    style.ChainsLayout = formatter::CategoryLayout::Canonical;
    style.ChainsBreak = formatter::BreakRequirement::Always;
    style.ChainsPacking = formatter::ListPacking::VerticalWhenBroken;
    style.IndentWidth = 4;
    style.ContinuationOffset = 4;
    style.IndentationKind = formatter::IndentationKind::Spaces;
    style.LineEnding = formatter::LineEnding::LF;
    style.TrailingCommentSpaces = formatter::TrailingCommentSpaceSetting{2};
    const formatter::PreparedFormat prepared(style);
    const auto indent = parser::SourceOffset(source.find("            .Open"));
    const auto comment = parser::SourceOffset(source.find(';') + 1);
    for (const auto begin : {indent, comment}) {
        const auto size = begin == indent ? 12u : 2u;
        const std::array selected{parser::SourceRange{begin, begin + size}};
        const auto result = FormatInput(
            source,
            prepared,
            {.SourceMap = true, .EditScope = selected});
        auto expected = source;
        expected.replace(begin, size, begin == indent ? "        " : "  ");
        EXPECT_EQ(result.Text, expected);
        CheckIdentities(source, result);
        CheckExcludedCopies(source, result, selected);
    }
}

TEST(PublicPipeline, LambdaArgumentOwnershipMapsProtectionAndIdempotence)
{
    for (const auto source :
        {
            "struct Registry {}; const Registry entry(7, [] { return 2; });\n"
                "int   following;\n",
            "APPLY(7, ::ops::combine(::ops::make()), "
                "[](const auto& v) -> std::string { return v; });\n"
                "int   following;\n",
            "APPLY([]() -> decltype(Item{}) { return Item{}; });\n"
                "int   following;\n",
            "void use() { consume(flag ? first, second : third, "
                "[] { return 2; }); }\nint   following;\n",
            "void use() { consume(flag ? other ? first, second : third, "
                "fourth : fifth, [] { return 2; }); }\nint   following;\n",
            "APPLY([]<class T, class U>(T value) requires Check<T{}> "
                "{ return value; });\nint   following;\n",
            "APPLY([] marker(int hidden) { return hidden; }, "
                "[] { return 2; });\nint   following;\n",
            "APPLY([]);\nint   following;\n",
        })
    {
        SCOPED_TRACE(source);
        for (const auto bodies :
            {
                formatter::CategoryLayout::Canonical,
                formatter::CategoryLayout::Preserve,
            })
        {
            auto style = Canonical();
            style.LambdaBodiesLayout = bodies;
            const formatter::PreparedFormat prepared(style);
            ASSERT_NO_THROW(([&] {
                const auto result = FormatInput(source, prepared);
                CheckIdentities(source, result);
                EXPECT_EQ(FormatInput(result.Text, prepared).Text, result.Text);
                const auto end = std::string_view(source).find("\n");
                const std::array<parser::SourceRange, 1> scope{
                    {{0, static_cast<parser::SourceOffset>(end)}},
                };
                formatter::FormatRequest request{.SourceMap = true};
                request.EditScope = scope;
                const auto limited = FormatInput(source, prepared, request);
                CheckExcludedCopies(source, limited, scope);
                EXPECT_TRUE(limited.Text.ends_with("\nint   following;\n"));
            }()));
        }
    }
}

TEST(PublicPipeline, UncertainLambdaArgumentsRetainRecoveryProtection)
{
    for (const auto source :
        {
            "APPLY([] marker(int hidden) { return hidden; }, "
                "[] { return 2; });\nint following;\n",
            "APPLY([]);\nint following;\n",
            "APPLY(::ops::, [] { return 2; });\nint following;\n",
            "APPLY(char**, [] { return 2; });\nint following;\n",
            "APPLY(*, [] { return 2; });\nint following;\n",
            "APPLY(value + *, [] { return 2; });\nint following;\n",
            "void use() { consume(flag ? first, second, "
                "[] { return 2; }); }\nint following;\n",
            "APPLY([]<class T, class U>(T value) requires Check<T{} "
                "{ return value; });\nint following;\n",
            "#if CHOICE\nAPPLY([\n#else\n] { return 1; });\n#endif\n"
                "int following;\n",
        })
    {
        SCOPED_TRACE(source);
        Input input(source);
        const auto tree =
            parser::Parse(input.View(), [] (parser::ParseError) {});
        std::pmr::monotonic_buffer_resource memory;
        std::pmr::monotonic_buffer_resource scratch;
        const auto analysis =
            formatter::BuildSourceAnalysis(tree, {}, memory, scratch);
        bool recovered = false;
        for (const auto& region : analysis.Regions()) {
            recovered |= region.Status == formatter::RegionStatus::Recovery ||
                region.Status == formatter::RegionStatus::RecoveryAndAmbiguous;
        }
        EXPECT_TRUE(recovered);
        const formatter::PreparedFormat prepared(Canonical());
        const auto result = FormatInput(source, prepared);
        CheckIdentities(source, result);
        std::vector<unsigned> copies(std::string_view(source).size(), 0);
        for (const auto& map : result.Map) {
            if (map.Exact && map.HasOriginal) {
                for (auto i = map.Original.Begin; i < map.Original.End; ++i) {
                    ++copies[i];
                }
            }
        }
        size_t protectedRecoveryBytes = 0;
        for (const auto& range : analysis.Protection()) {
            const auto mask = static_cast<unsigned>(range.Reasons);
            if ((mask &
                static_cast<unsigned>(formatter::ProtectionReason::Recovery)) !=
                0)
            {
                for (auto i = range.Source.Begin; i < range.Source.End; ++i) {
                    ++protectedRecoveryBytes;
                    EXPECT_EQ(copies[i], 1u) << i;
                }
            }
        }
        if (std::string_view(source) == "APPLY([]);\nint following;\n") {
            std::vector<const parser::Node*> pending{&tree.Root()};
            unsigned missing = 0;
            while (!pending.empty()) {
                const auto* node = pending.back();
                pending.pop_back();
                if (node->Kind == parser::NodeKind::MissingToken) {
                    ++missing;
                    EXPECT_TRUE(node->Tokens.Empty());
                }
                EXPECT_NE(node->Kind, parser::NodeKind::LambdaBody);
                EXPECT_NE(node->Kind, parser::NodeKind::ParameterList);
                EXPECT_NE(node->Kind, parser::NodeKind::ParameterDeclaration);
                for (auto* child = node->FirstChild;
                    child;
                    child = child->NextSibling)
                {
                    pending.push_back(child);
                }
            }
            EXPECT_GT(missing, 0u);
        } else {
            EXPECT_GT(protectedRecoveryBytes, 0u);
        }
    }
}

TEST(PublicPipeline, IfElseLookaheadPreservesFollowingAttributedNullStatement)
{
    for (const bool braces : {false, true}) {
        auto style = Canonical();
        style.RemoveNullStatements = true;
        style.AddMissingControlBraces = braces;
        const formatter::PreparedFormat prepared(style);
        for (const auto candidate :
            {
                "if (!ready) return; [[fallthrough]];",
                "if (outer) if (!ready) return; [[fallthrough]];",
            })
        {
            SCOPED_TRACE(candidate);
            SCOPED_TRACE(braces);
            const std::string source =
                "void sample(int n, bool ready, bool outer) { "
                    "switch(n) { case 1: " + std::string(candidate) +
                    " case 2: break; } }\n";
            const auto result = FormatInput(source, prepared);
            const auto attribute = source.find("[[fallthrough]]");
            const auto semicolon = source.find(';', attribute);
            Input input(source);
            auto tree = parser::Parse(input.View(), [] (parser::ParseError) {});
            bool found = false;
            for (parser::TokenIndex i = 0; i < tree.TokenCount(); ++i) {
                if (tree.Token(i).Text.Begin == semicolon) {
                    found = true;
                    ASSERT_LT(i, result.OriginalTokens.size());
                    EXPECT_TRUE(result.OriginalTokens[i].Present);
                }
            }
            EXPECT_TRUE(found);
            EXPECT_NE(result.Text.find("[[fallthrough]];"), std::string::npos);
            std::vector<unsigned> copies(source.size(), 0);
            for (const auto& map : result.Map) {
                if (map.Exact && map.HasOriginal) {
                    for (auto i = map.Original.Begin; i < map.Original.End; ++i)
                    {
                        ++copies[i];
                    }
                }
            }
            for (auto i = attribute; i <= semicolon; ++i) {
                EXPECT_EQ(copies[i], 1u) << i;
            }
            CheckIdentities(source, result);
            EXPECT_EQ(FormatInput(result.Text, prepared).Text, result.Text);
        }
    }
}

TEST(PublicPipeline, ControlSeparatorsRespectIndependentBodyAndHeaderPolicies)
{
    for (auto headers :
        {
            formatter::CategoryLayout::Preserve,
            formatter::CategoryLayout::Canonical,
        })
    {
        for (auto bodies :
            {
                formatter::CategoryLayout::Preserve,
                formatter::CategoryLayout::Canonical,
            })
        {
            for (auto shape :
                {
                    formatter::BodyShape::Expanded,
                    formatter::BodyShape::CompactIfFits,
                })
            {
                auto style = Canonical();
                style.IndentWidth = 2;
                style.ControlHeadersLayout = headers;
                style.BodiesLayout = bodies;
                style.BodyShape = shape;
                style.ControlBracePlacement =
                    formatter::BracePlacement::NextLine;
                const formatter::PreparedFormat prepared(style);
                const std::string source =
                    "void f() {\n  if (a) {\n    left();\n  }\n"
                        "  else {\n    right();\n  }\n"
                        "  do {\n    tick();\n  }\n  while (a);\n}\n";
                const auto result = FormatInput(source, prepared);
                if (headers == formatter::CategoryLayout::Canonical) {
                    EXPECT_NE(
                        result.Text.find("} else\n  {"),
                        std::string::npos);
                    EXPECT_NE(
                        result.Text.find("} while (a);"),
                        std::string::npos);
                } else {
                    EXPECT_NE(
                        result.Text.find("}\n  else {"),
                        std::string::npos);
                    EXPECT_NE(
                        result.Text.find("}\n  while (a);"),
                        std::string::npos);
                }
                if (bodies == formatter::CategoryLayout::Preserve) {
                    EXPECT_NE(
                        result.Text.find("{\n    left();\n  }"),
                        std::string::npos);
                }
                EXPECT_TRUE(result.StructuralOperations.empty());
                CheckIdentities(source, result);
                EXPECT_EQ(FormatInput(result.Text, prepared).Text, result.Text);
                const std::string unbraced =
                    "void f() {\n  if (a)left();else right();\n"
                        "  do tick();while (a);\n}\n";
                const auto arms = FormatInput(unbraced, prepared);
                if (headers == formatter::CategoryLayout::Canonical &&
                    bodies == formatter::CategoryLayout::Canonical)
                {
                    const auto separator =
                        shape == formatter::BodyShape::Expanded
                        ? "left();\n  else"
                        : "left(); else";
                    const auto suffix = shape == formatter::BodyShape::Expanded
                        ? "tick();\n  while"
                        : "tick(); while";
                    EXPECT_NE(arms.Text.find(separator), std::string::npos);
                    EXPECT_NE(arms.Text.find(suffix), std::string::npos);
                } else {
                    EXPECT_NE(arms.Text.find("left();else"), std::string::npos);
                    EXPECT_NE(
                        arms.Text.find("tick();while"),
                        std::string::npos);
                }
                CheckIdentities(unbraced, arms);
                EXPECT_EQ(FormatInput(arms.Text, prepared).Text, arms.Text);
            }
        }
    }
}

TEST(PublicPipeline, ControlSeparatorMapsProtectionScopesAndOwningResults)
{
    auto style = Canonical();
    style.IndentWidth = 2;
    style.HonorClangFormatDirectives = true;
    const formatter::PreparedFormat prepared(style);
    const std::string source =
        "void f() {\n  if (a) { left(); } // keep\n  else { right(); }\n"
            "  do { tick(); } /* barrier */\n  while (a);\n"
            "  if (b) { left(); }\n#if FLAG\n#endif\n  else { right(); }\n"
            "// clang-format off\n  do { tick(); }\nwhile( a );\n"
            "// clang-format on\n}\n";
    const auto result = FormatInput(source, prepared);
    EXPECT_NE(result.Text.find("// keep\n  else"), std::string::npos);
    EXPECT_NE(result.Text.find("/* barrier */\n  while"), std::string::npos);
    EXPECT_NE(result.Text.find("#if FLAG\n#endif\n  else"), std::string::npos);
    EXPECT_NE(
        result.Text.find("  do { tick(); }\nwhile( a );\n"),
        std::string::npos);
    CheckIdentities(source, result);
    EXPECT_EQ(FormatInput(result.Text, prepared).Text, result.Text);
    const std::array<parser::SourceRange, 0> empty{};
    const auto scoped =
        FormatInput(source, prepared, {.SourceMap = true, .EditScope = empty});
    EXPECT_EQ(scoped.Text, source);
    CheckExcludedCopies(source, scoped, empty);
    const auto gapBegin = source.find("}\n#if");
    const std::array<parser::SourceRange, 1> selected{
        {{gapBegin, gapBegin + 1}},
    };
    const auto partial = FormatInput(
        source,
        prepared,
        {.SourceMap = true, .EditScope = selected});
    CheckExcludedCopies(source, partial, selected);
    EXPECT_EQ(partial.Text, source);
}

TEST(PublicPipeline, UnbracedControlSeparatorCompactWidthAndMultilineSuffix)
{
    for (size_t width : {size_t(10000), size_t(32)}) {
        auto style = Canonical();
        style.IndentWidth = 2;
        style.ColumnLimit = width;
        style.BodyShape = formatter::BodyShape::CompactIfFits;
        const formatter::PreparedFormat prepared(style);
        const std::string source =
            "void f(){if(ok)first_action();else second_action();"
                "do third_action();while(\nvery_long_condition_name);"
                "while(ok)fourth_action();}\n";
        const auto result = FormatInput(source, prepared);
        if (width == 10000) {
            EXPECT_NE(
                result.Text.find("first_action(); else"),
                std::string::npos);
            EXPECT_NE(
                result.Text.find("third_action(); while"),
                std::string::npos);
        } else {
            EXPECT_NE(result.Text.find("\n  else"), std::string::npos);
            EXPECT_NE(result.Text.find("\n  while"), std::string::npos);
        }
        CheckIdentities(source, result);
        EXPECT_EQ(FormatInput(result.Text, prepared).Text, result.Text);
    }
}

TEST(PublicPipeline, UnknownAnnotationBoundariesPreserveRecoveryAndMaps)
{
    auto style = Canonical();
    const formatter::PreparedFormat prepared(style);
    for (const std::string_view source :
        {
            "META(Key, const Item *, const Value *)\n"
                "const Item *Factory::build(int n) const { return nullptr; }\n"
                "int tail;\n",
            "BIND(Key, const Item *) TRACK(Group, const Item *) "
                "MAP(Key, const Item *, const Value *)\n"
                "const Item *Factory::build(int n) const { return nullptr; }\n"
                "int tail;\n",
            "NOTE(0) NOTE2(1) Vec load(int Type) { Type * value; }\n"
                "int tail;\n",
            "META(Key, const Item *, const Value *) [[maybe_unused]] "
                "static bool check(const Item *p) { return p; }\nint tail;\n",
            "using Type = int;\nstatic inline Vec NOTE(\"legacy\") "
                "load(int Type) { Type * value; }\nint tail;\n",
            "RAW(int n) & noexcept { return n; }\nint tail;\n",
            "RAW(int n) noexcept(true) { return n; }\nint tail;\n",
            "RAW(int n) override final { return n; }\nint tail;\n",
        })
    {
        SCOPED_TRACE(source);
        Input input(source);
        auto tree = parser::Parse(input.View());
        const auto result =
            formatter::Format(tree, prepared, {.SourceMap = true});
        CheckIdentities(source, result);
        EXPECT_EQ(FormatInput(result.Text, prepared).Text, result.Text);
        for (const auto* node = tree.Root().FirstChild;
            node;
            node = node->NextSibling)
        {
            if (node->Kind != parser::NodeKind::UnparsedSyntax) {
                continue;
            }
            const parser::SourceRange range = {
                result.OriginalTokens[node->Tokens.Begin].Output.Begin,
                result.OriginalTokens[node->Tokens.End - 1].Output.End,
            };
            bool recovered = false;
            for (const auto& protection : result.OutputProtection) {
                recovered |= (static_cast<uint16_t>(protection.Reasons) &
                    static_cast<
                        uint16_t>(formatter::ProtectionReason::Recovery)) !=
                    0 && protection.Source.Begin <= range.Begin &&
                    protection.Source.End >= range.End;
            }
            EXPECT_TRUE(recovered);
            EXPECT_NE(
                result.Text.find(tree.SourceText(node->Tokens)),
                std::string::npos);
        }
        EXPECT_EQ(
            FormatInput(
                source,
                prepared,
                {
                    .SourceMap = true,
                    .EditScope = std::span<const parser::SourceRange>{},
                }).Text,
            source);
        const std::array<parser::SourceRange, 1> immutable =
            {{{0, source.size()}}};
        EXPECT_EQ(
            FormatInput(
                source,
                prepared,
                {.ImmutableRanges = immutable, .SourceMap = true}).Text,
            source);
    }
}

TEST(PublicPipeline, AnnotationRecoverySurvivesStructuralCloneAndScopedTail)
{
    auto style = Canonical();
    style.BodyShape = formatter::BodyShape::Expanded;
    style.RemoveNullStatements = true;
    style.RemoveNullDeclarations = true;
    const formatter::PreparedFormat prepared(style);
    const std::string candidate =
        "static Vec NOTE(0) load(int Type) { Type * value; ; }";
    const std::string source = "using Type = int;\n" + candidate +
        "\n;\nint tail(){ ; return 1; ; }\n";
    const auto unrestricted = FormatInput(source, prepared);
    EXPECT_FALSE(unrestricted.StructuralOperations.empty());
    EXPECT_NE(unrestricted.Text.find(candidate), std::string::npos);
    CheckIdentities(source, unrestricted);
    EXPECT_EQ(FormatInput(unrestricted.Text, prepared).Text, unrestricted.Text);
    const std::array<parser::SourceRange, 1> selected =
        {{{source.find("int tail"), source.size()}}};
    const auto scoped = FormatInput(
        source,
        prepared,
        {.SourceMap = true, .EditScope = selected});
    EXPECT_NE(scoped.Text, source);
    EXPECT_NE(scoped.Text.find(candidate), std::string::npos);
    CheckExcludedCopies(source, scoped, selected);
    CheckIdentities(source, scoped);
}

TEST(PublicPipeline, RecoveredFunctionTryHandlersHaveNoEditAuthority)
{
    auto style = Canonical();
    style.BodyShape = formatter::BodyShape::Expanded;
    const formatter::PreparedFormat prepared(style);
    for (const std::string_view header :
        {
            "Vec NOTE(0) load(int n) noexcept(true)",
            "RAW(int n) & noexcept",
            "Vec NOTE(0) load(int n) const volatile & noexcept",
            "Vec NOTE(0) load(int n) && noexcept(true) [[maybe_unused]]",
            "Vec NOTE(0) load(int n) requires Ready<int> && "
                "requires(int x) { x; }",
            "Vec NOTE(0) load(int n) requires (Ready<int>)",
            "Vec NOTE(0) load(int n) requires (Ready<int> && requires { 1; })",
        })
    {
        for (const bool functionTry : {false, true}) {
            const auto candidate = std::string(header) +
                (functionTry ? " try" : "") + " { return n; }" +
                (functionTry
                    ? " /* first */ catch(int Type) { Type * value; }"
                        " /* between */ catch(...) { return 0; }"
                    : "");
            const auto source = candidate + "\nint tail(){return 1;}\n";
            const auto result = FormatInput(source, prepared);
            EXPECT_TRUE(result.Text.starts_with(candidate));
            EXPECT_NE(result.Text, source);
            CheckIdentities(source, result);
            EXPECT_EQ(FormatInput(result.Text, prepared).Text, result.Text);
            bool recovered = false;
            for (const auto& protection : result.OutputProtection) {
                recovered |= protection.Source.Begin == 0 &&
                    protection.Source.End >= candidate.size() &&
                    (static_cast<uint16_t>(protection.Reasons) &
                        static_cast<
                            uint16_t>(formatter::ProtectionReason::Recovery)) !=
                        0;
            }
            EXPECT_TRUE(recovered);
            const std::array<parser::SourceRange, 1> handlerScope = {
                {
                    {
                        source.find(functionTry ? "catch" : "return n"),
                        candidate.size(),
                    },
                },
            };
            const auto scoped = FormatInput(
                source,
                prepared,
                {.SourceMap = true, .EditScope = handlerScope});
            EXPECT_EQ(scoped.Text, source);
            CheckExcludedCopies(source, scoped, handlerScope);
            CheckIdentities(source, scoped);
        }
    }
}

TEST(PublicPipeline,
    RecoveredConstructorInitializersAndHandlersHaveNoEditAuthority)
{
    auto style = Canonical();
    style.BodyShape = formatter::BodyShape::Expanded;
    style.RemoveNullDeclarations = true;
    style.RemoveNullStatements = true;
    const formatter::PreparedFormat prepared(style);
    for (const std::string_view initializers :
        {
            "member{n}",
            "member(n)",
            "first{n}, second(n), third{}",
            "Base<int>{n}, member(n)",
        })
    {
        for (const std::string_view suffix :
            {
                "",
                " noexcept",
                " noexcept(true)",
                " noexcept(noexcept(Value{1}))",
                " throw()",
                " [[maybe_unused]]",
                " __attribute__((unused))",
                " noexcept [[maybe_unused]]",
                " [[tag(Value{1})]] noexcept(false)",
                " requires Ready<int>",
                " requires requires(int x) { x; }",
                " requires Ready<int> && requires(int x) { x; }",
                " requires (Ready<int> && requires { 1; })",
                " requires (Ready<int>) || true",
                " requires (Ready<int>) && (Other<int>)",
                " requires (Ready<int>) || probe<Type>()",
                " requires Ready<int> && requires(int Type) { Type; } || true",
                " requires lhs < rhs",
                " requires lhs < probe(rhs)",
                " requires lhs = rhs",
                " requires (lhs = rhs) || true",
            })
        {
            for (const bool functionTry : {false, true}) {
                const auto candidate = "NOTE(0) C(int n)" +
                    std::string(suffix) +
                    std::string(functionTry ? " try : " : " : ") +
                    std::string(initializers) + " { Type * value; ; use(n); }" +
                    (functionTry
                        ? " /* first */ catch(int Type) { Type * value; ; }"
                            " /* second */ catch(...) { ; recover(); }"
                        : "");
                const auto source =
                    "using Type = int;\nstruct C {\n" + candidate +
                        "\nint member;\n};\n;\nint tail(){ ; return 1; }\n";
                SCOPED_TRACE(source);
                // FormatInput's input/tree lifetimes have ended before
                // these owning output, map and protection checks;
                // normalization must clone safely.
                const auto result = FormatInput(source, prepared);
                EXPECT_FALSE(result.StructuralOperations.empty());
                const auto begin = result.Text.find(candidate);
                ASSERT_NE(begin, std::string::npos);
                const auto recoveryReason = static_cast<
                    uint16_t>(formatter::ProtectionReason::Recovery);
                bool recovered = false;
                for (const auto& protection : result.OutputProtection) {
                    recovered |= protection.Source.Begin <= begin &&
                        protection.Source.End >= begin + candidate.size() &&
                        (static_cast<uint16_t>(protection.Reasons) &
                            recoveryReason) != 0;
                }
                EXPECT_TRUE(recovered);
                CheckIdentities(source, result);
                EXPECT_EQ(FormatInput(result.Text, prepared).Text, result.Text);
                const std::array<parser::SourceRange, 1> scope = {
                    {
                        {
                            source.find(functionTry ? "catch" : "Type * value"),
                            source.find(candidate) + candidate.size(),
                        },
                    },
                };
                const auto scoped = FormatInput(
                    source,
                    prepared,
                    {.SourceMap = true, .EditScope = scope});
                EXPECT_EQ(scoped.Text, source);
                CheckExcludedCopies(source, scoped, scope);
                CheckIdentities(source, scoped);
                EXPECT_EQ(
                    FormatInput(
                        source,
                        prepared,
                        {
                            .SourceMap = true,
                            .EditScope = std::span<const parser::SourceRange>{},
                        }).Text,
                    source);
                const std::array<parser::SourceRange, 1> immutable =
                    {{{0, source.size()}}};
                EXPECT_EQ(
                    FormatInput(
                        source,
                        prepared,
                        {.ImmutableRanges = immutable, .SourceMap = true}).Text,
                    source);
            }
        }
    }
}

TEST(PublicPipeline, RecoveredConstraintOperandsKeepOwningMapsAndScopedTail)
{
    auto style = Canonical();
    style.BodyShape = formatter::BodyShape::Expanded;
    style.RemoveNullStatements = true;
    style.RemoveNullDeclarations = true;
    style.ColumnLimit = 10000;
    const formatter::PreparedFormat prepared(style);
    for (const std::string_view suffix :
        {
            " requires (true) || true",
            " requires lhs < rhs",
            " requires lhs < probe(rhs)",
            " requires Ready<int> || probe<Type>()",
            " requires lhs = rhs",
            " requires (lhs = rhs) || true",
        })
    {
        const auto candidate = "Vec NOTE(0) load(int Type)" +
            std::string(suffix) + " { Type * value; ; }";
        const auto source = "using Type = int;\n" + candidate +
            "\n;\nType * kept;\nint tail(){ ; return 1; }\n"
                "bool later = a > b;\n";
        SCOPED_TRACE(source);
        const auto result = FormatInput(source, prepared);
        ASSERT_NE(result.Text.find(candidate), std::string::npos);
        EXPECT_FALSE(result.StructuralOperations.empty());
        CheckIdentities(source, result);
        EXPECT_EQ(FormatInput(result.Text, prepared).Text, result.Text);
        const auto begin = result.Text.find(candidate);
        EXPECT_TRUE(std::any_of(
            result.OutputProtection.begin(),
            result.OutputProtection.end(),
            [&] (const auto& protection) {
                return protection.Source.Begin <= begin &&
                    protection.Source.End >= begin + candidate.size() &&
                    (static_cast<uint16_t>(protection.Reasons) &
                        static_cast<
                            uint16_t>(formatter::ProtectionReason::Recovery)) !=
                        0;
            }));
        for (const auto offset :
            {
                source.find("requires"),
                source.find("Type * value"),
                source.find("int tail"),
            })
        {
            const std::array<parser::SourceRange, 1> scope = {
                {
                    {
                        offset,
                        offset == source.find("int tail")
                            ? source.size()
                            : source.find(candidate) + candidate.size(),
                    },
                },
            };
            const auto scoped = FormatInput(
                source,
                prepared,
                {.SourceMap = true, .EditScope = scope});
            EXPECT_NE(scoped.Text.find(candidate), std::string::npos);
            if (offset != source.find("int tail")) {
                EXPECT_EQ(scoped.Text, source);
            } else {
                EXPECT_NE(scoped.Text, source);
            }
            CheckExcludedCopies(source, scoped, scope);
            CheckIdentities(source, scoped);
        }
        EXPECT_EQ(
            FormatInput(
                source,
                prepared,
                {
                    .SourceMap = true,
                    .EditScope = std::span<const parser::SourceRange>{},
                }).Text,
            source);
        const std::array<parser::SourceRange, 1> immutable =
            {{{0, source.size()}}};
        EXPECT_EQ(
            FormatInput(
                source,
                prepared,
                {.ImmutableRanges = immutable, .SourceMap = true}).Text,
            source);
    }
}

TEST(PublicPipeline, StandaloneCommentCountsSupportOwningMapsAndScopedEdges)
{
    auto style = Canonical();
    style.BodyShape = formatter::BodyShape::Expanded;
    style.EmptyBodyShape = formatter::BodyShape::Expanded;
    style.StandaloneCommentBlankLinesBefore =
        {formatter::BlankLineMode::Exact, 1};
    style.StandaloneCommentBlankLinesAfter =
        {formatter::BlankLineMode::Exact, 1};
    const formatter::PreparedFormat prepared(style);
    const std::string source = "namespace N {\n// note\nint x;\n// end\n}\n";
    const auto result = FormatInput(source, prepared);
    EXPECT_NE(result.Text.find("{\n\n// note\n\n"), std::string::npos);
    EXPECT_NE(result.Text.find("x;\n\n// end\n\n}"), std::string::npos);
    CheckIdentities(source, result);
    EXPECT_EQ(FormatInput(result.Text, prepared).Text, result.Text);
    Input input(source);
    auto tree = parser::Parse(input.View(), [] (parser::ParseError) {});
    EXPECT_EQ(formatter::Format(tree, prepared).Text, result.Text);

    const auto comment = parser::SourceOffset(source.find("// note"));
    const std::array selected{parser::SourceRange{0, comment}};
    formatter::FormatRequest request{.SourceMap = true};
    request.EditScope = selected;
    const auto limited = FormatInput(source, prepared, request);
    EXPECT_EQ(limited.Text, source);
    CheckExcludedCopies(source, limited, selected);
    const std::array immutable{
        parser::SourceRange{
            comment - 1,
            parser::SourceOffset(source.find("int x")),
        },
    };
    request.EditScope = {};
    request.ImmutableRanges = immutable;
    const auto protectedResult = FormatInput(source, prepared, request);
    EXPECT_NE(
        protectedResult.Text.find("{\n// note\nint x;"),
        std::string::npos);
    CheckIdentities(source, protectedResult);

    style.MaxEmptyLines = 0;
    EXPECT_THROW((void)formatter::PreparedFormat(style), std::invalid_argument);
}



TEST(PublicPipeline, FinalNamespaceWidthDiagnosticsGolden)
{
    const test::golden::Fixture fixture{
        std::filesystem::path(__FILE__).parent_path() /
            "testdata/public-pipeline",
        "comments",
    };
    const formatter::PreparedFormat prepared(
        formatter::LoadConfigFile(fixture.Named(".hit-fmt")).FormattingStyle);
    const auto result = FormatInput(fixture.InputNamed(".input.cpp"), prepared);
    std::string diagnostics;
    for (const auto& diagnostic : result.Diagnostics) {
        diagnostics += std::to_string(diagnostic.Position.Line) + ":" +
            std::to_string(diagnostic.Position.Column) + " " +
            diagnostic.Message + "\n";
    }
    fixture.Expect(diagnostics, ".diagnostics.txt");
}

TEST(PublicPipeline, DeclarationFallbackRequiresRebuiltOwnerAndEditableGap)
{
    const std::string source = "template<class T> struct Box {};\n"
        "Box<int>  make(){return {};}\n" "Box<int>  flat();\n"
        "Box<int>\nOwner<int>::method(){return {};}\n";
    auto style = formatter::Style{};
    style.DeclarationHeadersLayout = formatter::CategoryLayout::Canonical;
    const formatter::PreparedFormat prepared(style);
    const auto gapBegin = uint32_t(source.find("  make"));
    const auto gapEnd = gapBegin + 2;
    const auto result = FormatInput(source, prepared);
    EXPECT_NE(result.Text.find("Box<int> make()"), std::string::npos);
    EXPECT_NE(result.Text.find("Box<int>  flat()"), std::string::npos);
    EXPECT_NE(
        result.Text.find("Box<int> Owner<int>::method()"),
        std::string::npos);
    CheckIdentities(source, result);
    EXPECT_EQ(FormatInput(result.Text, prepared).Text, result.Text);

    const std::array<parser::SourceRange, 1> selected{{{gapBegin, gapEnd}}};
    const auto scoped = FormatInput(
        source,
        prepared,
        {.SourceMap = true, .EditScope = selected});
    EXPECT_NE(scoped.Text.find("Box<int> make()"), std::string::npos);
    CheckIdentities(source, scoped);
    CheckExcludedCopies(source, scoped, selected);
    const std::array<parser::SourceRange, 1> partial{{{gapBegin, gapEnd - 1}}};
    const auto unownedEnd = FormatInput(
        source,
        prepared,
        {.SourceMap = true, .EditScope = partial});
    EXPECT_NE(unownedEnd.Text.find("Box<int>  make()"), std::string::npos);
    CheckExcludedCopies(source, unownedEnd, partial);
    const auto protectedGap = FormatInput(
        source,
        prepared,
        {.ImmutableRanges = selected, .SourceMap = true});
    EXPECT_NE(protectedGap.Text.find("Box<int>  make()"), std::string::npos);
    CheckIdentities(source, protectedGap);

    const auto qualifiedGap = uint32_t(source.find("\nOwner"));
    const auto qualifiedBegin =
        uint32_t(source.rfind("Box<int>", qualifiedGap));
    const auto qualifiedEnd = uint32_t(source.size());
    const std::array qualifiedScope{
        parser::SourceRange{qualifiedBegin, qualifiedEnd},
    };
    const std::array immutableQualifiedGap{
        parser::SourceRange{qualifiedGap, qualifiedGap + 1},
    };
    const auto qualified = FormatInput(
        source,
        prepared,
        {.SourceMap = true, .EditScope = qualifiedScope});
    EXPECT_NE(
        qualified.Text.find("Box<int> Owner<int>::method()"),
        std::string::npos) << qualified.Text;
    CheckIdentities(source, qualified);
    CheckExcludedCopies(source, qualified, qualifiedScope);
    const auto qualifiedProtected = FormatInput(
        source,
        prepared,
        {.ImmutableRanges = immutableQualifiedGap, .SourceMap = true});
    EXPECT_NE(
        qualifiedProtected.Text.find("Box<int>\nOwner<int>::method()"),
        std::string::npos);
    CheckIdentities(source, qualifiedProtected);

    style.DeclarationHeadersLayout = formatter::CategoryLayout::Preserve;
    style.Layout = formatter::LayoutMode::Canonical;
    const auto preserved =
        FormatInput(source, formatter::PreparedFormat(style));
    EXPECT_NE(preserved.Text.find("Box<int>  make()"), std::string::npos);
    EXPECT_NE(
        preserved.Text.find("Box<int>\nOwner<int>::method()"),
        std::string::npos);
}

TEST(PublicPipeline, DeclarationHeaderBreaksRespectWidthAndByteAuthority)
{
    const auto directory = std::filesystem::path(FORMATTER_TESTDATA_DIR) /
        "../declaration-headers";
    const auto source = test::golden::ReadFile(directory / "input.cpp");
    for (const auto* profile : {"wrap", "narrow", "wide"}) {
        SCOPED_TRACE(profile);
        auto style = formatter::LoadConfigFile(
            directory / (std::string(profile) + ".hit-fmt")).FormattingStyle;
        style.WidthDiagnostics = formatter::WidthDiagnosticPolicy::Report;
        const formatter::PreparedFormat prepared(style);
        const auto result = FormatInput(source, prepared);
        EXPECT_TRUE(result.Diagnostics.empty());
        CheckIdentities(source, result);
        EXPECT_EQ(FormatInput(result.Text, prepared).Text, result.Text);
    }

    auto style =
        formatter::LoadConfigFile(directory / "wrap.hit-fmt").FormattingStyle;
    style.WidthDiagnostics = formatter::WidthDiagnosticPolicy::Report;
    const formatter::PreparedFormat prepared(style);
    const auto firstLine = source.substr(0, source.find('\n') + 1);
    const std::array immutable{
        parser::SourceRange{0, uint32_t(firstLine.size())},
    };
    const auto protectedResult = FormatInput(
        firstLine,
        prepared,
        {.ImmutableRanges = immutable, .SourceMap = true});
    EXPECT_EQ(protectedResult.Text, firstLine);
    ASSERT_EQ(protectedResult.Diagnostics.size(), 1u);
    EXPECT_EQ(
        protectedResult.Diagnostics[0].Kind,
        formatter::LayoutDiagnosticKind::LineWidth);
    CheckIdentities(firstLine, protectedResult);

    const auto gap = uint32_t(firstLine.find("> ReadFirstResult") + 1);
    const std::array selected{parser::SourceRange{gap, gap + 1}};
    const auto scoped = FormatInput(
        firstLine,
        prepared,
        {.SourceMap = true, .EditScope = selected});
    EXPECT_NE(scoped.Text.find(">\nReadFirstResult"), std::string::npos);
    EXPECT_TRUE(scoped.Diagnostics.empty());
    CheckIdentities(firstLine, scoped);
    CheckExcludedCopies(firstLine, scoped, selected);

    style.DeclarationHeadersLayout = formatter::CategoryLayout::Preserve;
    const auto preserved =
        FormatInput(firstLine, formatter::PreparedFormat(style));
    EXPECT_NE(preserved.Text.find("> ReadFirstResult"), std::string::npos);
}

TEST(PublicPipeline, LambdaLineBasesKeepSourceAuthority)
{
    const auto directory =
        std::filesystem::path(FORMATTER_TESTDATA_DIR) / "../lambda-captures";
    const auto input = test::golden::ReadFile(directory / "profile.input.cpp");
    const auto style = formatter::LoadConfigFile(directory /
        "../../../../hit-fmt/examples/ytsaurus.hit-fmt").FormattingStyle;
    const formatter::PreparedFormat prepared(style);
    for (const bool crlf : {false, true}) {
        std::string source;
        source.reserve(input.size() * (crlf ? 2 : 1));
        for (const char c : input) {
            if (crlf && c == '\n') {
                source += '\r';
            }
            source += c;
        }
        const auto result = FormatInput(source, prepared, {.SourceMap = true});
        CheckIdentities(source, result);
        EXPECT_TRUE(result.Diagnostics.empty());
        EXPECT_EQ(FormatInput(result.Text, prepared).Text, result.Text);
        EXPECT_NE(
            result.Text.find("                auto guard ="),
            std::string::npos);
        const auto begin = uint32_t(source.find("class Adapter"));
        const std::array scope{
            parser::SourceRange{begin, uint32_t(source.size())},
        };
        const auto scoped = FormatInput(
            source,
            prepared,
            {.SourceMap = true, .EditScope = scope});
        CheckExcludedCopies(source, scoped, scope);
        CheckIdentities(source, scoped);
        EXPECT_NE(
            scoped.Text.find("                auto guard ="),
            std::string::npos);
        const auto guardBegin =
            uint32_t(source.find("                    InvokerGuard", begin));
        const auto guardEnd = uint32_t(
            source.find("                    callback();", guardBegin));
        const std::array immutable{parser::SourceRange{guardBegin, guardEnd}};
        const auto frozen = FormatInput(
            source,
            prepared,
            {.ImmutableRanges = immutable, .SourceMap = true});
        CheckIdentities(source, frozen);
        EXPECT_NE(
            frozen.Text.find(source.substr(guardBegin, guardEnd - guardBegin)),
            std::string::npos);
    }
}

TEST(PublicPipeline, ConditionalFragmentsKeepSourceAuthority)
{
    const auto directory = std::filesystem::path(FORMATTER_TESTDATA_DIR) /
        "../preprocessing-fragments";
    const auto source =
        test::golden::ReadFile(directory / "canonical.input.cpp");
    const auto style = formatter::LoadConfigFile(directory /
        "../../../../hit-fmt/examples/ytsaurus.hit-fmt").FormattingStyle;
    const formatter::PreparedFormat prepared(style);
    const auto result = FormatInput(source, prepared, {.SourceMap = true});
    CheckIdentities(source, result);
    EXPECT_EQ(FormatInput(result.Text, prepared).Text, result.Text);

    const auto splitBegin = source.find("    auto a =\n");
    const auto splitEnd = source.find("\n}", splitBegin);
    ASSERT_NE(splitBegin, std::string::npos);
    EXPECT_NE(
        result.Text.find(source.substr(splitBegin, splitEnd - splitBegin)),
        std::string::npos);

    const auto begin = uint32_t(source.find("void Arguments()"));
    const auto end = uint32_t(source.find("void Parameters(", begin));
    const std::array scope{parser::SourceRange{begin, end}};
    const auto scoped =
        FormatInput(source, prepared, {.SourceMap = true, .EditScope = scope});
    CheckIdentities(source, scoped);
    CheckExcludedCopies(source, scoped, scope);
    EXPECT_NE(scoped.Text.find("left + delta"), std::string::npos);

    const auto immutableBegin = uint32_t(source.find("    Consume(first,\n"));
    const auto immutableEnd =
        uint32_t(source.find("    Consume(\n", immutableBegin));
    const std::array immutable{
        parser::SourceRange{immutableBegin, immutableEnd},
    };
    const auto protectedResult = FormatInput(
        source,
        prepared,
        {.ImmutableRanges = immutable, .SourceMap = true});
    EXPECT_NE(
        protectedResult.Text
            .find(source.substr(immutableBegin, immutableEnd - immutableBegin)),
        std::string::npos);
    CheckIdentities(source, protectedResult);
}

TEST(PublicPipeline, RepositoryAndYtsaurusControlHeadersFitWithoutTokenChanges)
{
    const auto directory = std::filesystem::path(FORMATTER_TESTDATA_DIR) /
        "../control-expression-continuation";
    const auto source = test::golden::ReadFile(directory / "input.cpp");
    for (const auto* config :
        {
            "../../../../.hit-fmt",
            "../../../../hit-fmt/examples/ytsaurus.hit-fmt",
        })
    {
        SCOPED_TRACE(config);
        const auto style =
            formatter::LoadConfigFile(directory / config).FormattingStyle;
        const formatter::PreparedFormat prepared(style);
        const auto result = FormatInput(source, prepared);
        EXPECT_TRUE(result.Diagnostics.empty());
        CheckIdentities(source, result);
        EXPECT_EQ(FormatInput(result.Text, prepared).Text, result.Text);
    }
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
        auto tree = parser::Parse(input.View(), [] (parser::ParseError) {});
        std::string path = "missing/directory with spaces/original.cpp";
        const formatter::FormatRequest request{
            .SourceMap = true,
            .InputFilePath = path,
        };
        result = formatter::Format(tree, prepared, request);
        const auto fromSource =
            formatter::Format(input.View(), prepared, request);
        EXPECT_EQ(result.Text, fromSource.Text);
        CheckIdentities(source, fromSource);
        EXPECT_EQ(result.Text, formatter::Format(input.View(), prepared).Text);
        path.assign(path.size(), '?');
        EXPECT_EQ(result.Text, formatter::Format(tree, Canonical()));
        EXPECT_EQ(result.Text, formatter::Format(input.View(), Canonical()));
    }
    EXPECT_NE(result.Text.find("left, right"), std::string::npos);
    CheckIdentities(source, result);
    for (const auto& token : result.OriginalTokens) {
        EXPECT_TRUE(token.Present);
    }

    // Inactive include metadata must not activate regrouping or change the
    // composed constructor/head rule, scope authority, maps or lifetimes.
    const auto composed = test::golden::ReadFile(
        std::filesystem::path(__FILE__).parent_path() /
        "testdata/record-head-commas/constructor-composition.input.cpp");
    const auto point = parser::SourceOffset(composed.find(">,2>") + 2);
    const std::array<parser::SourceRange, 1> selected{{{point, point + 1}}};
    formatter::Style defaults;
    defaults.Layout = formatter::LayoutMode::Preserve;
    defaults.CommaSpaceAfter = formatter::GapPolicy::Space;
    const auto baseline = FormatInput(
        composed,
        formatter::PreparedFormat(defaults),
        {.SourceMap = true, .EditScope = selected});
    formatter::FormatResult withMetadata;
    {
        auto style = defaults;
        style.IncludeGroupPrefixes = {"engine/", "engine/detail/"};
        const formatter::PreparedFormat prepared(style);
        style.IncludeGroupPrefixes.clear();
        style.ColumnLimit = 0; // Mutating Style cannot affect prepared scalars.
        Input input(composed);
        auto tree = parser::Parse(input.View(), [] (parser::ParseError) {});
        std::string path = "not/a/real/source/location/record.cpp";
        withMetadata = formatter::Format(
            tree,
            prepared,
            {.SourceMap = true, .EditScope = selected, .InputFilePath = path});
        EXPECT_EQ(
            withMetadata.Text,
            FormatInput(
                composed,
                prepared,
                {
                    .SourceMap = true,
                    .EditScope = selected,
                    .InputFilePath = path,
                }).Text);
        const std::array<parser::SourceRange, 1> immutable{
            {{point - 1, point + 1}},
        };
        const auto protectedResult = FormatInput(
            composed,
            prepared,
            {
                .ImmutableRanges = immutable,
                .SourceMap = true,
                .EditScope = selected,
                .InputFilePath = path,
            });
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
    for (const auto& token : withMetadata.OriginalTokens) {
        EXPECT_TRUE(token.Present);
    }
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
    const std::string source =
        "#include \"z.h\"\n#include \"a.h\"\n;\nvoid f(){ ; if(ok) "
            "invoke(a,b); }\n";
    auto result = FormatInput(source, prepared);
    EXPECT_TRUE(
        result.Text.starts_with("#include \"a.h\"\n#include \"z.h\"\n"));
    EXPECT_NE(result.Text.find("if (ok) {"), std::string::npos);
    CheckIdentities(source, result);
    bool deleted = false, generated = false, directive = false;
    for (const auto& token : result.OriginalTokens) {
        deleted |= !token.Present;
    }
    for (const auto& map : result.Map) {
        generated |=
            map.Generated && map.Action != formatter::NoStructuralAction;
    }
    for (const auto& range : result.OutputProtection) {
        directive |= range.Reasons == formatter::ProtectionReason::Directive;
    }
    EXPECT_TRUE(deleted);
    EXPECT_TRUE(generated);
    EXPECT_TRUE(directive);
    EXPECT_EQ(FormatInput(result.Text, prepared).Text, result.Text);
    const std::array<parser::SourceRange, 1> excluded{
        {{0, uint32_t(source.find(';'))}},
    };
    auto constrained = FormatInput(
        source,
        prepared,
        {.ImmutableRanges = excluded, .SourceMap = true});
    EXPECT_TRUE(
        constrained.Text.starts_with(source.substr(0, excluded[0].End)));
    EXPECT_NE(constrained.Text.find("if (ok) {"), std::string::npos);
    CheckIdentities(source, constrained);
}

TEST(PublicPipeline, ProtectedBytesSurviveBothBranchesAndCRLF)
{
    const std::string source =
        "// hit-fmt off\r\nvoid locked( ){  invoke( x,y ); }\r\n// "
            "hit-fmt on\r\n"
            "void f(){ KEEP(  x, y ); if(ok) invoke(a,b); }\r\n";
    for (bool structural : {false, true}) {
        auto style = Canonical();
        style.AddMissingControlBraces = structural;
        style.WhitespaceSensitiveMacros = {"KEEP"};
        const formatter::PreparedFormat prepared(style);
        auto result = FormatInput(source, prepared);
        EXPECT_TRUE(
            result.Text.starts_with(source.substr(0, source.find("void f"))));
        EXPECT_NE(result.Text.find("KEEP(  x, y )"), std::string::npos);
        CheckIdentities(source, result);
        for (size_t i = 0; i < result.Text.size(); ++i)
            if (result.Text[i] == '\n') {
                ASSERT_GT(i, 0u);
                EXPECT_EQ(result.Text[i - 1], '\r');
            }
        EXPECT_EQ(FormatInput(result.Text, prepared).Text, result.Text);
    }
}

TEST(PublicPipeline, NestedControlOwnershipPreservesProtectionAndScope)
{
    const test::golden::Fixture fixture{
        std::filesystem::path(__FILE__).parent_path() /
            "testdata/public-pipeline",
        "control-ownership",
    };
    const auto source = fixture.InputNamed(".input.cpp");
    const formatter::PreparedFormat prepared(
        formatter::LoadConfigFile(fixture.Named(".hit-fmt")).FormattingStyle);
    auto complete = FormatInput(source, prepared);
    CheckIdentities(source, complete);
    EXPECT_EQ(FormatInput(complete.Text, prepared).Text, complete.Text);
    EXPECT_TRUE(complete.StructuralOperations.empty());
    const auto firstElse = parser::SourceOffset(source.find("else if"));
    const std::array<parser::SourceRange, 1> locked{
        {{firstElse - 1, firstElse + 4}},
    };
    auto protectedResult = FormatInput(
        source,
        prepared,
        {.ImmutableRanges = locked, .SourceMap = true});
    CheckIdentities(source, protectedResult);
    const std::array<parser::SourceRange, 2> unlocked{
        {{0, locked[0].Begin}, {locked[0].End, uint32_t(source.size())}},
    };
    CheckExcludedCopies(source, protectedResult, unlocked);
    const auto selectedBegin = parser::SourceOffset(source.find("if (second)"));
    const auto selectedEnd = parser::SourceOffset(source.find("#endif"));
    const std::array<parser::SourceRange, 1> selected{
        {{selectedBegin, selectedEnd}},
    };
    auto scoped = FormatInput(
        source,
        prepared,
        {.SourceMap = true, .EditScope = selected});
    CheckIdentities(source, scoped);
    CheckExcludedCopies(source, scoped, selected);
    EXPECT_TRUE(scoped.StructuralOperations.empty());
    const std::string fenced =
        "void run() {\n if (outer) {}\n#if FEATURE\n if (first) {}\n#else\n"
            "// clang-format off\n if (second) {  act( );  } else {}\n"
            "// clang-format on\n#endif\n else {}\n}\n";
    const auto fencedResult = FormatInput(fenced, prepared);
    CheckIdentities(fenced, fencedResult);
    const auto lockedBegin = fenced.find("// clang-format off");
    const auto lockedEnd = fenced.find("#endif");
    EXPECT_NE(
        fencedResult.Text
            .find(fenced.substr(lockedBegin, lockedEnd - lockedBegin)),
        std::string::npos);
    EXPECT_EQ(FormatInput(fencedResult.Text, prepared).Text, fencedResult.Text);
}

TEST(PublicPipeline, EnumeratorAssignmentExplicitSpacing)
{
    const std::string source = "enum class Mode { First=1, Second =2 };\n";
    for (const auto mode :
        {formatter::LayoutMode::Preserve, formatter::LayoutMode::Canonical})
    {
        auto style = Canonical();
        style.Layout = mode;
        for (const auto policy :
            {formatter::GapPolicy::Space, formatter::GapPolicy::GeneratedSpace})
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

TEST(PublicPipeline, EnumeratorAssignmentMapsAndProtection)
{
    const auto directory = std::filesystem::path(__FILE__).parent_path() /
        "testdata/consolidated-pipeline/enumerator-assignment";
    const auto source = test::golden::ReadFile(directory / "input.cpp");
    for (const auto* profile :
        {
            "space",
            "no-space",
            "preserve",
            "generated-space",
            "generated-no-space",
        })
    {
        SCOPED_TRACE(profile);
        const test::golden::Fixture fixture{directory, profile};
        const auto style = formatter::LoadConfigFile(fixture.Named(".hit-fmt"))
            .FormattingStyle;
        const formatter::PreparedFormat prepared(style);
        CheckIdentities(source, FormatInput(source, prepared));
    }
    const formatter::PreparedFormat prepared(
        formatter::LoadConfigFile(directory / "space.hit-fmt").FormattingStyle);
    const auto second = uint32_t(source.find("Second"));
    const std::array<parser::SourceRange, 1> selected{{{0, second}}};
    const auto scoped = FormatInput(
        source,
        prepared,
        {.SourceMap = true, .EditScope = selected});
    EXPECT_EQ(scoped.Text, "enum Mode { First = 1, Second=2 };\n");
    CheckIdentities(source, scoped);
    CheckExcludedCopies(source, scoped, selected);
    const std::array<parser::SourceRange, 1> immutable{{{0, second}}};
    const auto locked = FormatInput(
        source,
        prepared,
        {.ImmutableRanges = immutable, .SourceMap = true});
    EXPECT_EQ(locked.Text, "enum Mode { First  =  1, Second = 2 };\n");
    CheckIdentities(source, locked);
    const auto gap = uint32_t(source.find("  ="));
    const std::array<parser::SourceRange, 1> partial{{{gap, gap + 1}}};
    const auto denied = FormatInput(
        source,
        prepared,
        {.SourceMap = true, .EditScope = partial});
    EXPECT_EQ(denied.Text, source);
    CheckExcludedCopies(source, denied, partial);
}

TEST(PublicPipeline, EnumeratorAssignmentCommentMapsAndProtection)
{
    const test::golden::Fixture fixture{
        std::filesystem::path(__FILE__).parent_path() /
            "testdata/consolidated-pipeline",
        "enumerator-assignment-comments",
    };
    const auto source = fixture.InputNamed(".input.cpp");
    const formatter::PreparedFormat prepared(
        formatter::LoadConfigFile(fixture.Named(".hit-fmt")).FormattingStyle);
    const auto result = FormatInput(source, prepared);
    for (const auto comment : {"// note", "// control"}) {
        const auto end = uint32_t(source.find(comment));
        const auto begin = end - 2;
        EXPECT_TRUE(std::ranges::any_of(
            result.Map,
            [=] (const auto& map) {
                return map.Exact && map.HasOriginal &&
                    map.Original.Begin <= begin && map.Original.End >= end;
            }));
    }
    CheckIdentities(source, result);
    EXPECT_EQ(FormatInput(result.Text, prepared).Text, result.Text);
    const std::string protectedSource =
        "// clang-format off\n" + source + "// clang-format on\n";
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
    for (auto policy :
        {
            formatter::NamespaceComments::AddMissing,
            formatter::NamespaceComments::Fix,
            formatter::NamespaceComments::Remove,
        })
    {
        auto style = Canonical();
        style.NamespaceComments = policy;
        style.NamespaceCommentSpaces = 3;
        const formatter::PreparedFormat prepared(style);
        const std::string source =
            policy == formatter::NamespaceComments::AddMissing
            ? "namespace Name { int value; }\nint "
                "following;\n#define RAW(x) x " "## x\n"
            : "namespace Name { int value; } // namespace Wrong\nint "
                "following;\n#define RAW(x) x ## x\n";
        auto result = FormatInput(source, prepared);
        CheckIdentities(source, result);
        EXPECT_NE(result.Text.find("int following;"), std::string::npos);
        if (policy == formatter::NamespaceComments::Remove) {
            EXPECT_EQ(result.Text.find("// namespace"), std::string::npos);
        } else {
            EXPECT_NE(
                result.Text.find("}   // namespace Name"),
                std::string::npos);
        }
        EXPECT_EQ(FormatInput(result.Text, prepared).Text, result.Text);
        std::string crlf;
        for (const auto c : source) {
            if (c == '\n') {
                crlf += '\r';
            }
            crlf += c;
        }
        const auto windows = FormatInput(crlf, prepared);
        CheckIdentities(crlf, windows);
        bool protectedDirective = false;
        for (const auto& range : windows.OutputProtection) {
            if (range.Reasons == formatter::ProtectionReason::Directive) {
                protectedDirective = true;
                EXPECT_EQ(
                    std::string_view(windows.Text).substr(
                        range.Source.Begin,
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
    if (comment != std::string::npos) {
        EXPECT_LT(
            result.Text.find('\n', comment),
            result.Text.find("int following"));
    }
}

TEST(PublicPipeline, ImmutableRangesConstrainNamespaceAndStructuralPlanning)
{
    auto style = Canonical();
    style.NamespaceComments = formatter::NamespaceComments::AddMissing;
    style.AddMissingControlBraces = true;
    const formatter::PreparedFormat prepared(style);
    const std::string source =
        "namespace Name { void f(){ if(ok)invoke(a,b); } }\nint tail;\n";
    const std::array<parser::SourceRange, 1> immutable{
        {{0, uint32_t(source.find('\n') + 1)}},
    };
    for (const bool sourceMap : {false, true}) {
        SCOPED_TRACE(sourceMap);
        const auto result = FormatInput(
            source,
            prepared,
            {.ImmutableRanges = immutable, .SourceMap = sourceMap});
        EXPECT_TRUE(
            result.Text.starts_with(source.substr(0, immutable[0].End)));
        if (sourceMap) {
            CheckIdentities(source, result);
        }
        EXPECT_EQ(result.StructuralDiagnostics.size(), 2u);
        EXPECT_EQ(
            std::count_if(
                result.StructuralDiagnostics.begin(),
                result.StructuralDiagnostics.end(),
                [] (const auto& diagnostic) {
                    return diagnostic.Action ==
                        formatter::StructuralAction::NamespaceComments;
                }),
            1);
        EXPECT_EQ(
            std::count_if(
                result.StructuralDiagnostics.begin(),
                result.StructuralDiagnostics.end(),
                [] (const auto& diagnostic) {
                    return diagnostic.Action ==
                        formatter::StructuralAction::AddMissingControlBraces;
                }),
            1);
        for (const auto& diagnostic : result.StructuralDiagnostics) {
            EXPECT_EQ(diagnostic.Reason, formatter::StructuralSkip::Protected);
        }
    }
}

TEST(PublicPipeline, InvalidPreparedRequestsAndActualEmittedWidthDiagnostics)
{
    auto style = Canonical();
    style.ColumnLimit = 0;
    style.WidthDiagnostics = formatter::WidthDiagnosticPolicy::Report;
    const auto unlimited = FormatInput(
        "int extremely_long_variable_name;\n",
        formatter::PreparedFormat{style});
    EXPECT_TRUE(unlimited.Diagnostics.empty());
    style = Canonical();
    style.CallLikeMacroNames = {"CALL_HINT"};
    EXPECT_THROW(formatter::PreparedFormat{style}, std::invalid_argument);
    style = Canonical();
    style.AmbiguousTemplateDeclarationSpacing = formatter::GapPolicy::Space;
    EXPECT_THROW(formatter::PreparedFormat{style}, std::invalid_argument);
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
        result.Diagnostics[1].Kind,
        formatter::LayoutDiagnosticKind::LineWidth);
    const std::array<parser::SourceRange, 1> bad{{{1, 1000}}};
    EXPECT_THROW(
        FormatInput("int x;", prepared, {.ImmutableRanges = bad}),
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
    const std::string source =
        "#include \"z.h\"\n#include \"a.h\"\nvoid f(){ if(ok)invoke(); "
            "}\nint values[]={alpha,beta};\n";
    const auto result = FormatInput(source, prepared);
    CheckIdentities(source, result);
    const auto comma = result.Text.find("beta,") + 4;
    ASSERT_NE(comma, std::string::npos);
    bool found = false;
    for (const auto& map : result.Map) {
        if (map.Output.Begin == comma && map.Output.End == comma + 1) {
            found = true;
            EXPECT_TRUE(map.Generated);
            EXPECT_FALSE(map.Exact);
            EXPECT_EQ(map.OriginalToken, document::NoIdentity);
            EXPECT_TRUE(map.HasOriginal);
            EXPECT_EQ(map.Original.Begin, source.find("beta") + 4);
            EXPECT_EQ(map.Original.End, map.Original.Begin);
        }
    }
    EXPECT_TRUE(found);
    Input input(source);
    const auto tree = parser::Parse(input.View(), [] (parser::ParseError) {});
    const auto treeResult =
        formatter::Format(tree, prepared, {.SourceMap = true});
    EXPECT_EQ(treeResult.Text, result.Text);
    ASSERT_EQ(treeResult.Map.size(), result.Map.size());
    for (size_t i = 0; i < result.Map.size(); ++i)
    {
        EXPECT_EQ(treeResult.Map[i].Output.Begin, result.Map[i].Output.Begin);
        EXPECT_EQ(
            treeResult.Map[i].Original.Begin,
            result.Map[i].Original.Begin);
        EXPECT_EQ(treeResult.Map[i].OriginalToken, result.Map[i].OriginalToken);
    }
    EXPECT_EQ(FormatInput(result.Text, prepared).Text, result.Text);
}

TEST(PublicPipeline, BorrowedIndentMappingIsAnAssociationNotAnEdit)
{
    formatter::Style style;
    style.NamespaceBlankLinesAfterOpen = {formatter::BlankLineMode::Exact, 1};
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
            EXPECT_EQ(
                std::string_view(result.Text).substr(
                    borrowed.Output.Begin,
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
    EXPECT_EQ(
        result.StructuralDiagnostics.back().Reason,
        formatter::StructuralSkip::FollowingText);
    CheckIdentities(source, result);
    style = Canonical();
    style.AddMissingControlBraces = true;
    const std::string disabled =
        "// clang-format off\nvoid f(){ if(ok)invoke(a,b); }\n// "
            "clang-format on\n";
    const auto protectedResult =
        FormatInput(disabled, formatter::PreparedFormat(style));
    EXPECT_EQ(protectedResult.Text, disabled);
    CheckIdentities(disabled, protectedResult);
    style.HonorClangFormatDirectives = false;
    EXPECT_NE(
        FormatInput(disabled, formatter::PreparedFormat(style)).Text,
        disabled);
}

TEST(PublicPipeline, DecltypeOperandsPreserveMapsAndEditAuthority)
{
    const auto directory = std::filesystem::path(__FILE__).parent_path() /
        "testdata/public-pipeline";
    for (const auto* name :
        {"declaration-operands", "declaration-operands-recovery"})
    {
        SCOPED_TRACE(name);
        const test::golden::Fixture fixture{directory, name};
        const auto source = fixture.InputNamed(".input.cpp");
        const formatter::PreparedFormat prepared(Canonical());
        const auto result = FormatInput(source, prepared);
        CheckIdentities(source, result);
        for (const auto& token : result.OriginalTokens) {
            EXPECT_TRUE(token.Present);
        }
        EXPECT_EQ(FormatInput(result.Text, prepared).Text, result.Text);
        const std::span<const parser::SourceRange> empty;
        const auto denied = FormatInput(
            source,
            prepared,
            {.SourceMap = true, .EditScope = empty});
        EXPECT_EQ(denied.Text, source);
        CheckIdentities(source, denied);
        CheckExcludedCopies(source, denied, empty);
        if (std::string_view(name).ends_with("recovery")) {
            EXPECT_EQ(result.Text, source);
            const std::array<parser::SourceRange, 1> immutable{
                {{0, static_cast<parser::SourceOffset>(source.size())}},
            };
            const auto locked = FormatInput(
                source,
                prepared,
                {.ImmutableRanges = immutable, .SourceMap = true});
            EXPECT_EQ(locked.Text, source);
            CheckIdentities(source, locked);
            CheckExcludedCopies(source, locked, empty);
        }
    }
}

TEST(PublicPipeline, EmptyAndRecoveryInputRemainLocalWithoutFallback)
{
    const formatter::PreparedFormat prepared(Canonical());
    const auto empty = FormatInput("", prepared);
    EXPECT_TRUE(empty.Text.empty());
    EXPECT_TRUE(empty.OriginalTokens.empty());
    const std::string source =
        "void broken(){ invoke(a,,b); }\nvoid safe(){ invoke(x,y); }\n";
    const auto result = FormatInput(source, prepared);
    EXPECT_NE(result.Text.find("invoke(a,,b)"), std::string::npos);
    EXPECT_NE(result.Text.find("invoke(x, y)"), std::string::npos);
    CheckIdentities(source, result);
    EXPECT_EQ(FormatInput(result.Text, prepared).Text, result.Text);
}

// Whole-gap projection checks identity and protection; selected partial
// indentation is tested separately, not assumed to remain frozen here.
TEST(PublicPipeline, EditScopeAbsenceEmptyFullPartialAndLifetime)
{
    const std::string source = "void first(){ if(ok)invoke(a,b); }\n"
        "void locked( ){  invoke( x,y ); }\n";
    for (const bool structural : {false, true}) {
        auto style = Canonical();
        style.AddMissingControlBraces = structural;
        const formatter::PreparedFormat prepared(style);
        const auto unrestricted = FormatInput(source, prepared);
        const std::span<const parser::SourceRange> empty;
        const auto denied = FormatInput(
            source,
            prepared,
            {.SourceMap = true, .EditScope = empty});
        EXPECT_EQ(denied.Text, source);
        CheckIdentities(source, denied);
        CheckExcludedCopies(source, denied, empty);
        const std::array<parser::SourceRange, 1> full{
            {{0, uint32_t(source.size())}},
        };
        EXPECT_EQ(
            FormatInput(
                source,
                prepared,
                {.SourceMap = true, .EditScope = full}).Text,
            unrestricted.Text);
        formatter::FormatResult selected;
        const auto tail = uint32_t(source.find("void locked"));
        {
            const std::array<parser::SourceRange, 2> touching{
                {{0, 5}, {5, tail}},
            };
            Input input(source);
            auto tree = parser::Parse(input.View(), [] (parser::ParseError) {});
            selected = formatter::Format(
                tree,
                prepared,
                {.SourceMap = true, .EditScope = touching});
            EXPECT_EQ(
                selected.Text,
                formatter::Format(
                    input.View(),
                    prepared,
                    {.EditScope = touching}).Text);
        }
        EXPECT_TRUE(selected.Text.ends_with(source.substr(tail)));
        EXPECT_NE(selected.Text.find("invoke(a, b)"), std::string::npos);
        if (structural) {
            EXPECT_NE(selected.Text.find("if (ok) {"), std::string::npos);
        }
        CheckIdentities(source, selected);
        const std::array<parser::SourceRange, 1> selection{{{0, tail}}};
        CheckExcludedCopies(source, selected, selection);
        bool outside = false;
        for (const auto& range : selected.OutputProtection) {
            outside |= (uint16_t(range.Reasons) & uint16_t(
                formatter::ProtectionReason::
                               OutsideEditScope)) != 0;
        }
        EXPECT_TRUE(outside);
    }
}

TEST(PublicPipeline, EditScopeProjectsPermutationGeneratedBytesAndReasonUnions)
{
    auto style = Canonical();
    style.AddMissingControlBraces = true;
    style.RemoveNullDeclarations = true;
    style.IncludeOrder = formatter::IncludeOrder::Sort;
    style.WhitespaceSensitiveMacros = {"KEEP"};
    const formatter::PreparedFormat prepared(style);
    const std::string source = "#include \"zzzz.h\"\n#include \"a.h\"\n;\n"
        "void selected(){ if(ok)invoke(a,b); KEEP(  x,y ); }\n"
        "void locked( ){  invoke( x,y ); }\n";
    const auto tail = uint32_t(source.find("void locked"));
    const auto sensitive = uint32_t(source.find("KEEP("));
    const std::array<parser::SourceRange, 1> scope{{{0, tail}}};
    const std::array<parser::SourceRange, 1> immutable{
        {{sensitive, sensitive + 13}},
    };
    const auto result = FormatInput(
        source,
        prepared,
        {
            .ImmutableRanges = immutable,
            .SourceMap = true,
            .EditScope =
            scope,
        });
    EXPECT_TRUE(
        result.Text.starts_with("#include \"a.h\"\n#include \"zzzz.h\"\n"));
    EXPECT_TRUE(result.Text.ends_with(source.substr(tail)));
    EXPECT_NE(result.Text.find("KEEP(  x,y )"), std::string::npos);
    CheckIdentities(source, result);
    CheckExcludedCopies(source, result, scope);
    bool moved = false, literal = false, deleted = false, unioned = false;
    for (const auto& token : result.OriginalTokens) {
        deleted |= !token.Present;
    }
    for (const auto& map : result.Map) {
        if (map.Action == formatter::NoStructuralAction) {
            continue;
        }
        const auto kind = result.StructuralOperations[map.Action].Kind;
        moved |= kind == formatter::StructuralAction::IncludeOrder && map.Exact;
        literal |= kind == formatter::StructuralAction::
            AddMissingControlBraces && map.Generated && !map.Exact &&
            map.OriginalToken == document::NoIdentity;
    }
    for (const auto& range : result.OutputProtection) {
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
    const std::array<parser::SourceRange, 1> partial{
        {{0, uint32_t(source.find('\n') + 1)}},
    };
    EXPECT_EQ(
        FormatInput(source, prepared, {.SourceMap = true, .EditScope = partial})
            .Text,
        source);
}

TEST(PublicPipeline, IncludeRegroupingOwnsBlankLinesAndPreservesMovedIdentities)
{
    const auto directory = std::filesystem::path(__FILE__).parent_path() /
        "testdata/include-order";
    for (const bool priorities : {false, true}) {
        const auto source = test::golden::ReadFile(directory /
            (priorities ? "groups.input.cpp" : "relative.input.cpp"));
        auto style = formatter::LoadConfigFile(
            directory /
            (priorities ? "groups.hit-fmt" : "directory-relative.hit-fmt"))
            .FormattingStyle;
        formatter::FormatResult owned;
        {
            const formatter::PreparedFormat prepared(style);
            style.IncludeGrouping = formatter::IncludeGrouping::Preserve;
            style.IncludeGroupPrefixes.clear();
            style.IncludeStandardHeaders =
                formatter::IncludeStandardHeaders::Preserve;
            std::string path = priorities ? "library/unit/widget.cpp"
                : "/project/engine/widget.cpp";
            owned = FormatInput(
                source,
                prepared,
                {.SourceMap = true, .InputFilePath = path});
            Input input(source);
            auto tree = parser::Parse(input.View(), [] (parser::ParseError) {});
            EXPECT_EQ(
                owned.Text,
                formatter::Format(tree, prepared, {.InputFilePath = path})
                    .Text);
            EXPECT_NE(owned.Text, source);
            const auto repeated = FormatInput(
                owned.Text,
                prepared,
                {.SourceMap = true, .InputFilePath = path});
            EXPECT_EQ(repeated.Text, owned.Text);
            EXPECT_TRUE(repeated.StructuralOperations.empty());
            const auto blank = parser::SourceOffset(source.find("\n\n") + 1);
            // A one-byte hole in blank-line authority blocks the entire move.
            const std::array<parser::SourceRange, 2> scope{
                {{0, blank}, {blank + 1, parser::SourceOffset(source.size())}},
            };
            const auto partial = FormatInput(
                source,
                prepared,
                {.SourceMap = true, .EditScope = scope, .InputFilePath = path});
            EXPECT_EQ(partial.Text, source);
            EXPECT_TRUE(partial.StructuralOperations.empty());
            CheckIdentities(source, partial);
            CheckExcludedCopies(source, partial, scope);
            const std::array<parser::SourceRange, 1> immutable{
                {{blank, blank + 1}},
            };
            const auto protectedResult = FormatInput(
                source,
                prepared,
                {
                    .ImmutableRanges = immutable,
                    .SourceMap = true,
                    .InputFilePath = path,
                });
            EXPECT_EQ(protectedResult.Text, source);
            CheckIdentities(source, protectedResult);
            path.assign(path.size(), '?');
        }
        CheckIdentities(source, owned);
        for (const auto& token : owned.OriginalTokens) {
            EXPECT_TRUE(token.Present);
        }
        EXPECT_TRUE(std::any_of(
            owned.Map.begin(),
            owned.Map.end(),
            [&] (const auto& map) {
                return map.Generated &&
                    map.Action != formatter::NoStructuralAction &&
                    owned.StructuralOperations[map.Action].Kind ==
                        formatter::StructuralAction::IncludeOrder;
            }));
    }
}

TEST(PublicPipeline, IncludeRegroupingRetainsLineEndingsAndIncompleteAtoms)
{
    formatter::Style style;
    style.IncludeOrder = formatter::IncludeOrder::Sort;
    style.IncludeGrouping = formatter::IncludeGrouping::Directory;
    style.IncludeMatchingHeader = formatter::IncludeMatchingHeader::First;
    const formatter::PreparedFormat prepared(style);
    for (const auto source :
        {
            "#include \"z.h\"\r\n\r\n#include \"a.h\"\r\n"
                "#include \"child/a.h\"\r\n",
            "#include \"z.h\"\n\r\n#include \"a.h\"\n",
            "#include \"z.h\"\n\n#include \"a.h\"",
            "#include \"z.h\"\n\r#include \"a.h\"\n",
        })
    {
        const auto result = FormatInput(source, prepared);
        CheckIdentities(source, result);
        EXPECT_EQ(FormatInput(result.Text, prepared).Text, result.Text);
        if (std::string_view(source).starts_with("#include \"z.h\"\r\n")) {
            EXPECT_NE(result.Text, source);
            for (std::size_t i = 0; i < result.Text.size(); ++i) {
                if (result.Text[i] == '\n') {
                    ASSERT_GT(i, 0u);
                    EXPECT_EQ(result.Text[i - 1], '\r');
                }
            }
        } else {
            EXPECT_EQ(result.Text, source);
        }
    }
}

TEST(PublicPipeline, EditScopeDeletionAndReplacementDoNotOwnTheirEnds)
{
    formatter::Style style;
    style.RemoveNullDeclarations = true;
    style.EmptyControlBody = formatter::EmptyControlBody::EmptyBlock;
    style.EmptyBlockInteriorSpacing = formatter::GapPolicy::GeneratedNoSpace;
    const formatter::PreparedFormat prepared(style);
    const std::array<parser::SourceRange, 1> deleted{{{0, 1}}};
    const std::string source = ";  int x;\n";
    const auto result = FormatInput(
        source,
        prepared,
        {.SourceMap = true, .EditScope = deleted});
    EXPECT_EQ(result.Text, "  int x;\n");
    CheckIdentities(source, result);
    ASSERT_FALSE(result.OriginalTokens.empty());
    EXPECT_FALSE(result.OriginalTokens[0].Present);
    CheckExcludedCopies(source, result, deleted);
    const std::string control = "void f(){ while(ok); int x; }\n";
    const auto semicolon = uint32_t(control.find(';'));
    const std::array<parser::SourceRange, 1> endpoint{
        {{semicolon, semicolon + 1}},
    };
    EXPECT_EQ(
        FormatInput(control, prepared, {.EditScope = endpoint}).Text,
        control);
    const std::array<parser::SourceRange, 1> replacement{
        {{semicolon, semicolon + 2}},
    };
    const auto replaced = FormatInput(
        control,
        prepared,
        {.SourceMap = true, .EditScope = replacement});
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
    const std::array<parser::SourceRange, 1> unownedEnd{{{begin, end}}};
    EXPECT_EQ(
        FormatInput(source, prepared, {.EditScope = unownedEnd}).Text,
        source);
    const std::array<parser::SourceRange, 1> ownedEnd{{{begin, end + 1}}};
    const auto result = FormatInput(
        source,
        prepared,
        {.SourceMap = true, .EditScope = ownedEnd});
    const auto hasInsertedControlPair = [&] (
        const formatter::FormatResult&
        formatted) {
            for (size_t action = 0; action <
                formatted.StructuralOperations.size();
             ++action)
            {
                if (formatted.StructuralOperations[action].Kind !=
                    formatter::StructuralAction::AddMissingControlBraces)
                {
                    continue;
                }
                size_t opens = 0, closes = 0;
                for (const auto& map : formatted.Map) {
                    if (map.Action != action || !map.Generated || map.Exact ||
                        !map.HasOriginal || !map.Original.Empty() ||
                        map.OriginalToken != document::NoIdentity)
                    {
                        continue;
                    }
                    const auto text = std::string_view(formatted.Text).substr(
                        map.Output.Begin,
                        map.Output.End - map.Output.Begin);
                    opens += text == "{" && map.Original.Begin == begin;
                    closes += text == "}" && map.Original.Begin == end;
                }
                if (opens == 1 && closes == 1) {
                    return true;
                }
            }
            return false;
        };
    EXPECT_TRUE(hasInsertedControlPair(result));
    CheckIdentities(source, result);
    CheckExcludedCopies(source, result, ownedEnd);
    EXPECT_TRUE(result.Text.ends_with('\n'));
    // Existing function braces must not satisfy the positive endpoint check.
    style.AddMissingControlBraces = false;
    const auto skipped = FormatInput(
        source,
        formatter::PreparedFormat(style),
        {.SourceMap = true, .EditScope = ownedEnd});
    EXPECT_EQ(skipped.Text, source);
    EXPECT_FALSE(hasInsertedControlPair(skipped));
}

TEST(PublicPipeline, EditScopeNamespaceOriginalWindowsAndEOFOwnership)
{
    for (const bool normalize : {false, true}) {
        for (const auto policy :
            {
                formatter::NamespaceComments::AddMissing,
                formatter::NamespaceComments::Fix,
                formatter::NamespaceComments::Remove,
            })
        {
            formatter::Style style;
            style.NamespaceComments = policy;
            style.RemoveNullDeclarations = normalize;
            const formatter::PreparedFormat prepared(style);
            const std::string prefix = normalize ? ";\n" : "";
            const std::string bare = prefix + "namespace Name {}";
            const bool existing = policy != formatter::NamespaceComments::
                AddMissing;
            const std::string source =
                bare + (existing ? " // namespace Wrong\n" : "\n");
            const auto closeEnd = uint32_t(bare.size());
            const std::array<parser::SourceRange, 1> selected{
                {{0, uint32_t(source.size())}},
            };
            const auto result = FormatInput(
                source,
                prepared,
                {.SourceMap = true, .EditScope = selected});
            CheckIdentities(source, result);
            if (policy == formatter::NamespaceComments::Remove) {
                EXPECT_EQ(result.Text.find("// namespace"), std::string::npos);
            } else {
                EXPECT_NE(
                    result.Text.find("// namespace Name"),
                    std::
                    string::npos);
            }
            const std::array<parser::SourceRange, 1> excludedPoint{
                {{0, closeEnd}},
            };
            const auto denied = FormatInput(
                source,
                prepared,
                {.SourceMap = true, .EditScope = excludedPoint});
            EXPECT_EQ(denied.Text, normalize ? source.substr(1) : source);
            CheckIdentities(source, denied);
            // Existing replacement windows cannot be authorized by their
            // close-brace point alone, even after a preceding deletion.
            if (existing) {
                const std::array<parser::SourceRange, 1> cutComment{
                    {{0, uint32_t(source.find("Wrong"))}},
                };
                EXPECT_NE(
                    FormatInput(source, prepared, {.EditScope = cutComment})
                        .Text.find("Wrong"),
                    std::string::npos);
            } else {
                const std::array<parser::SourceRange, 1> fullEOF{
                    {{0, uint32_t(bare.size())}},
                };
                const auto eof = FormatInput(
                    bare,
                    prepared,
                    {.SourceMap = true, .EditScope = fullEOF});
                EXPECT_EQ(eof.Text.find("// namespace"), std::string::npos);
                CheckIdentities(bare, eof);
                const std::string withComment = bare + " // namespace Wrong\n";
                const std::array<parser::SourceRange, 1> wholeComment{
                    {{0, uint32_t(withComment.size())}},
                };
                const auto retained = FormatInput(
                    withComment,
                    prepared,
                    {.SourceMap = true, .EditScope = wholeComment});
                EXPECT_EQ(
                    retained.Text,
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
    const std::string source = "#include \"z.h\"\r\n#include \"aaa.h\"\r\n;\r\n"
        "void locked( ){\r\n  invoke( x,y );  \r\n\t\n}\r\n"
        "void selected(){ invoke(a,b); }\n";
    const auto firstFunction = uint32_t(source.find("void locked"));
    const auto horizontal = uint32_t(source.find("  \r\n"));
    const auto selectedFunction = uint32_t(source.find("void selected"));
    const std::array<parser::SourceRange, 3> selected{
        {
            {0, firstFunction},
            {horizontal, horizontal + 2},
            {selectedFunction, uint32_t(source.size())},
        },
    };
    const auto result = FormatInput(
        source,
        prepared,
        {.SourceMap = true, .EditScope = selected});
    EXPECT_TRUE(
        result.Text.starts_with("#include \"aaa.h\"\r\n#include \"z.h\"\r\n"));
    EXPECT_NE(result.Text.find("invoke(a, b)"), std::string::npos);
    CheckIdentities(source, result);
    CheckExcludedCopies(source, result, selected);
    // The selected trim run may change; excluded suffix/prefix bytes must
    // stay exact.
    EXPECT_NE(result.Text.find("\r\n\t\n}\r\n"), std::string::npos);
}

TEST(PublicPipeline, EditScopeInvalidRangesAndProtectionRemainStronger)
{
    const std::string source =
        "// hit-fmt off\nvoid locked(){ if(ok)invoke(a,b); }\n"
            "// hit-fmt on\nvoid safe(){ if(ok)invoke(x,y); }\n";
    auto style = Canonical();
    style.AddMissingControlBraces = true;
    const formatter::PreparedFormat prepared(style);
    const std::array<parser::SourceRange, 1> full{
        {{0, uint32_t(source.size())}},
    };
    const auto result =
        FormatInput(source, prepared, {.SourceMap = true, .EditScope = full});
    EXPECT_TRUE(
        result.Text.starts_with(source.substr(0, source.find("void safe"))));
    EXPECT_NE(result.Text.find("invoke(x, y)"), std::string::npos);
    CheckIdentities(source, result);
    const auto safe = uint32_t(source.find("void safe"));
    const std::array<parser::SourceRange, 1> onlySafe{
        {{safe, uint32_t(source.size())}},
    };
    const auto constrained = FormatInput(
        source,
        prepared,
        {.SourceMap = true, .EditScope = onlySafe});
    CheckIdentities(source, constrained);
    CheckExcludedCopies(source, constrained, onlySafe);
    bool disabledOutside = false;
    for (const auto& range : constrained.OutputProtection) {
        const auto reasons = uint16_t(range.Reasons);
        disabledOutside |= (reasons & uint16_t(formatter::ProtectionReason::
            Disabled)) &&
            (reasons & uint16_t(formatter::ProtectionReason::OutsideEditScope));
    }
    EXPECT_TRUE(disabledOutside);
    for (const std::vector<parser::SourceRange> invalid :
        {
            std::vector<parser::SourceRange>{{0, 0}},
            {{2, 1}},
            {{0, uint32_t(source.size() + 1)}},
            {{4, 6}, {1, 3}},
            {{0, 5}, {4, 7}},
        })
    {
        EXPECT_THROW(
            FormatInput(source, prepared, {.EditScope = invalid}),
            std::invalid_argument);
    }
}
TEST(PublicPipeline, RequiresOrdinaryReturnBoundaryPreservesScopeAndOwningMaps)
{
    const formatter::PreparedFormat prepared(Canonical());
    for (const auto declaration :
        {
            "Envelope<T> inspect(T value){return {};}",
            "library::Envelope<T> inspect(T value){return {};}",
            "Envelope<Envelope<T>> Owner::inspect(T value){return {};}",
            "Envelope<T> Owner::operator()(T value){return {};}",
        })
    {
        const auto source =
            "template<class T> requires (requires(T v) { v.touch(); })\n" +
                std::string(declaration) + "\n";
        SCOPED_TRACE(source);
        const auto function = uint32_t(source.find(declaration));
        formatter::FormatResult result;
        {
            Input input(source);
            const auto tree =
                parser::Parse(input.View(), [] (parser::ParseError) {});
            const std::array<parser::SourceRange, 1> selected{
                {{function, uint32_t(source.size())}},
            };
            result = formatter::Format(
                tree,
                prepared,
                {.SourceMap = true, .EditScope = selected});
        }
        CheckIdentities(source, result);
        const std::array<parser::SourceRange, 1> selected{
            {{function, uint32_t(source.size())}},
        };
        CheckExcludedCopies(source, result, selected);
        EXPECT_EQ(result.Text.substr(0, function), source.substr(0, function));
        const std::array<parser::SourceRange, 1> selectedOutput{
            {{function, uint32_t(result.Text.size())}},
        };
        EXPECT_EQ(
            FormatInput(
                result.Text,
                prepared,
                {.SourceMap = true, .EditScope = selectedOutput}).Text,
            result.Text);
        const auto unrestricted = FormatInput(source, prepared);
        CheckIdentities(source, unrestricted);
        EXPECT_EQ(
            FormatInput(unrestricted.Text, prepared).Text,
            unrestricted.Text);
    }
}

TEST(PublicPipeline, NamelessBodyRecoveryPreservesCandidateAndIndependentEdits)
{
    auto style = Canonical();
    style.AssignmentOperatorSpacing = formatter::GapPolicy::Space;
    style.BodyShape = formatter::BodyShape::Expanded;
    style.FunctionBraceSpacing = formatter::GapPolicy::Space;
    const formatter::PreparedFormat prepared(style);
    for (const auto candidate :
        {
            "HEADER_BLOCK(Thing, test, const Item *, input) "
                "{ if(input){return input;} return 0; }",
            "OPAQUE_BODY(Label, char*(Box<char*>, Box<bool>)) "
                "{ int value=1+2; return value; };",
            "HEADER_BLOCK(Type) { Type * item; }",
            "HEADER_BLOCK(Tag) { using Hidden=int; int member; }",
        })
    {
        const std::string prefix = "struct Type {};\n";
        const auto source =
            prefix + candidate + "\nint following(){int x=1;return x;}\n";
        SCOPED_TRACE(source);
        const auto start = parser::SourceOffset(prefix.size());
        const auto end =
            start + parser::SourceOffset(std::string_view(candidate).size());
        formatter::FormatResult result;
        {
            Input input(source);
            const auto tree =
                parser::Parse(input.View(), [] (parser::ParseError) {});
            result = formatter::Format(tree, prepared, {.SourceMap = true});
            const auto limited = formatter::Format(
                tree,
                prepared,
                {
                    .SourceMap = true,
                    .EditScope = std::span<const parser::SourceRange>{},
                });
            EXPECT_EQ(limited.Text, source);
            CheckExcludedCopies(source, limited, {});
            const std::array selected{
                parser::SourceRange{
                    end + 1,
                    parser::SourceOffset(source.size()),
                },
            };
            const auto scoped = formatter::Format(
                tree,
                prepared,
                {.SourceMap = true, .EditScope = selected});
            EXPECT_EQ(
                scoped.Text.substr(0, end + 1),
                source.substr(0, end + 1));
            EXPECT_NE(scoped.Text.find("x = 1"), std::string::npos);
            CheckExcludedCopies(source, scoped, selected);
            CheckIdentities(source, scoped);
            const std::array immutable{
                parser::SourceRange{
                    end + 1,
                    parser::SourceOffset(source.size()),
                },
            };
            const auto frozen = formatter::Format(
                tree,
                prepared,
                {.ImmutableRanges = immutable, .SourceMap = true});
            EXPECT_NE(
                frozen.Text.find("int following(){int x=1;return x;}"),
                std::string::npos);
            CheckIdentities(source, frozen);
        }
        // Returned text/maps/protection own their memory beyond
        // source/tree/request lifetime.
        const auto outputStart = result.Text.find(candidate);
        ASSERT_NE(outputStart, std::string::npos);
        EXPECT_NE(result.Text.find("x = 1"), std::string::npos);
        EXPECT_TRUE(std::ranges::any_of(
            result.OutputProtection,
            [&] (const auto& range) {
                return range.Source.Begin <= outputStart &&
                    range.Source.End >=
                        outputStart + std::string_view(candidate).size() &&
                    (uint16_t(range.Reasons) &
                        uint16_t(formatter::ProtectionReason::Recovery));
            }));
        const std::array selected{
            parser::SourceRange{0, start},
            parser::SourceRange{end, parser::SourceOffset(source.size())},
        };
        CheckExcludedCopies(source, result, selected);
        CheckIdentities(source, result);
        EXPECT_EQ(FormatInput(result.Text, prepared).Text, result.Text);
    }
}

TEST(PublicPipeline, RequiresRecoveryScopeAndMapsPreserveInvalidBytes)
{
    const formatter::PreparedFormat prepared(Canonical());
    for (const auto declaration :
        {
            "int inspect(T value){return 1;}",
            "T* inspect(T* value){return value;}",
            "T& access(T& value){return value;}",
        })
    {
        for (const auto terminal : {"@", "&& @"}) {
            const auto source =
                "template<class T> requires requires(T v) { v.touch(); } " +
                    std::string(terminal) + "\n" + std::string(declaration) +
                    "\n";
            SCOPED_TRACE(source);
            const auto invalid = uint32_t(source.find('@'));
            const auto function = uint32_t(source.find(declaration));
            formatter::FormatResult result;
            parser::TokenIndex invalidToken = 0;
            {
                Input input(source);
                auto tree =
                    parser::Parse(input.View(), [] (parser::ParseError) {});
                const std::array<parser::SourceRange, 1> selected{
                    {{function, uint32_t(source.size())}},
                };
                result = formatter::Format(
                    tree,
                    prepared,
                    {.SourceMap = true, .EditScope = selected});
                ASSERT_EQ(result.OriginalTokens.size(), tree.TokenCount());
                size_t invalidCount = 0;
                for (parser::TokenIndex token = 0; token <
                    tree.TokenCount(); ++token)
                {
                    const auto map = result.OriginalTokens[token];
                    ASSERT_TRUE(map.Present);
                    EXPECT_LE(map.Output.Begin, map.Output.End);
                    EXPECT_LE(map.Output.End, result.Text.size());
                    EXPECT_EQ(
                        std::string_view(result.Text).substr(
                            map.Output.Begin,
                            map.Output.End - map.Output.Begin),
                        tree.TokenText(token));
                    if (tree.Kind(token) == tokenizer::TokenKind::Invalid) {
                        ++invalidCount;
                        invalidToken = token;
                    }
                }
                EXPECT_EQ(invalidCount, 1u);
            }
            // Result maps/protection remain owning after
            // source/tree/scope storage dies.
            const auto invalidOutput =
                result.OriginalTokens[invalidToken].Output;
            EXPECT_EQ(
                std::string_view(result.Text).substr(
                    invalidOutput.Begin,
                    invalidOutput.End - invalidOutput.Begin),
                "@");
            EXPECT_TRUE(std::ranges::any_of(
                result.OutputProtection,
                [&] (const auto& range) {
                    return range.Source.Begin <= invalidOutput.Begin &&
                        range.Source.End >= invalidOutput.End &&
                        (uint16_t(range.Reasons) &
                            uint16_t(formatter::ProtectionReason::Recovery)) &&
                        (uint16_t(range.Reasons) &
                            uint16_t(
                                formatter::ProtectionReason::OutsideEditScope));
                }));
            const std::array<parser::SourceRange, 1> selected{
                {{function, uint32_t(source.size())}},
            };
            CheckExcludedCopies(source, result, selected);
            EXPECT_EQ(
                result.Text.substr(0, function),
                source.substr(0, function));
            size_t invalidCopies = 0;
            for (const auto& map : result.Map) {
                EXPECT_LE(map.Output.Begin, map.Output.End);
                EXPECT_LE(map.Output.End, result.Text.size());
                if (map.Exact && map.HasOriginal) {
                    EXPECT_EQ(
                        std::string_view(result.Text).substr(
                            map.Output.Begin,
                            map.Output.End - map.Output.Begin),
                        std::string_view(source).substr(
                            map.Original.Begin,
                            map.Original.End - map.Original.Begin));
                    if (map.Original.Begin <= invalid &&
                        map.Original.End > invalid)
                    {
                        ++invalidCopies;
                    }
                }
            }
            EXPECT_EQ(invalidCopies, 1u);
        }
    }
}
TEST(PublicPipeline, OpaqueRecordOwnerPreservesHeadAndFormatsSafeBodies)
{
    for (const auto head :
        {
            "API Parcel",
            "api [[nodiscard]] Parcel",
            "API Owner::Parcel",
            "API Parcel + broken",
            "API Owner::",
        })
    {
        const auto source = "struct " + std::string(head) +
            " { Parcel(Item &p) : Base(p) { invoke(a,b); } "
                "void (*callback)(int); int (*factory())(double); };\n";
        SCOPED_TRACE(source);
        auto style = Canonical();
        style.CommaSpaceAfter = formatter::GapPolicy::Space;
        const formatter::PreparedFormat prepared(style);
        const auto result = FormatInput(source, prepared);
        EXPECT_TRUE(result.Text.starts_with("struct " + std::string(head)));
        EXPECT_NE(result.Text.find("invoke(a, b)"), std::string::npos);
        CheckIdentities(source, result);
        EXPECT_EQ(FormatInput(result.Text, prepared).Text, result.Text);
        const auto point = parser::SourceOffset(source.find("a,b") + 2);
        const std::array<parser::SourceRange, 1> selected{{{point, point + 1}}};
        const auto scoped = FormatInput(
            source,
            prepared,
            {.SourceMap = true, .EditScope = selected});
        auto expected = source;
        expected.insert(point, " ");
        EXPECT_EQ(scoped.Text, expected);
        CheckIdentities(source, scoped);
        CheckExcludedCopies(source, scoped, selected);
        const std::array<parser::SourceRange, 1> immutable{
            {{point - 1, point + 1}},
        };
        const auto frozen = FormatInput(
            source,
            prepared,
            {
                .ImmutableRanges = immutable,
                .SourceMap = true,
                .EditScope = selected,
            });
        EXPECT_EQ(frozen.Text, source);
        CheckIdentities(source, frozen);
    }
}

TEST(PublicPipeline, ConstructorInitializerLambdasPreserveMapsAndProtection)
{
    const std::string_view sources[] = {
        "struct Task { Task() : callback([this] { stop(); }, "
            "[this] { start(); }) {} };\n",
        "struct Task { Task() noexcept : first([] { return 1; }), "
            "second([]() noexcept { return 2; }) {} };\n",
        "struct Task { Task() : first(0), callback(wrap("
            "[id = 1](int* value) { consume(value, id); })) {} };\n",
        "struct Task { Task() try : first([] { return 1; }), "
            "second([] { return 2; }) {} catch (...) {} };\n",
    };
    const formatter::PreparedFormat prepared(Canonical());
    for (const auto source : sources) {
        SCOPED_TRACE(source);
        formatter::FormatResult result;
        ASSERT_NO_THROW(result = FormatInput(source, prepared));
        CheckIdentities(source, result);
        EXPECT_EQ(FormatInput(result.Text, prepared).Text, result.Text);
        const auto begin = uint32_t(source.find('['));
        const auto end = uint32_t(source.find('}', begin) + 1);
        const std::array<parser::SourceRange, 1> immutable{{{begin, end}}};
        const auto protectedResult = FormatInput(
            source,
            prepared,
            {.ImmutableRanges = immutable, .SourceMap = true});
        CheckIdentities(source, protectedResult);
        EXPECT_NE(
            protectedResult.Text.find(source.substr(begin, end - begin)),
            std::string::npos);
        EXPECT_FALSE(protectedResult.OutputProtection.empty());
    }
}
TEST(PublicPipeline, TerminalConstructorTemplateIdsKeepMapsProtectionAndScopes)
{
    const test::golden::Fixture fixture{
        std::filesystem::path(__FILE__).parent_path() /
            "testdata/public-pipeline",
        "terminal-constructor-template-ids",
    };
    const auto source = fixture.InputNamed(".input.cpp");
    const formatter::PreparedFormat prepared(
        formatter::LoadConfigFile(fixture.Named(".hit-fmt")).FormattingStyle);
    formatter::FormatResult complete;
    ASSERT_NO_THROW(complete = FormatInput(source, prepared));
    CheckIdentities(source, complete);
    EXPECT_TRUE(complete.StructuralOperations.empty());
    EXPECT_EQ(FormatInput(complete.Text, prepared).Text, complete.Text);
    const auto begin = uint32_t(source.find("explicit Parcel"));
    const auto end = uint32_t(source.find('{', begin));
    const std::array<parser::SourceRange, 1> header{{{begin, end}}};
    const auto protectedResult = FormatInput(
        source,
        prepared,
        {.ImmutableRanges = header, .SourceMap = true});
    CheckIdentities(source, protectedResult);
    EXPECT_NE(
        protectedResult.Text.find(source.substr(begin, end - begin)),
        std::string::npos);
    EXPECT_FALSE(protectedResult.OutputProtection.empty());

    const auto point = uint32_t(source.find("value,1") + 6);
    const std::array<parser::SourceRange, 1> selected{{{point, point + 1}}};
    auto owned = [&] {
        const std::string temporary = source;
        Input input(temporary);
        const auto tree =
            parser::Parse(input.View(), [] (parser::ParseError) {});
        return formatter::Format(
            tree,
            prepared,
            {.SourceMap = true, .EditScope = selected});
    }();
    auto expected = source;
    expected.insert(point, " ");
    EXPECT_EQ(owned.Text, expected);
    CheckIdentities(source, owned);
    CheckExcludedCopies(source, owned, selected);
    const std::array<parser::SourceRange, 1> immutable{
        {{0, uint32_t(source.size())}},
    };
    const auto frozen = FormatInput(
        source,
        prepared,
        {.ImmutableRanges = immutable, .SourceMap = true});
    EXPECT_EQ(frozen.Text, source);
    CheckIdentities(source, frozen);
}
TEST(PublicPipeline, TerminalConstructorSelfTypeStatementsKeepMapsAndEditScopes)
{
    const std::string_view bodies[] = {
        "Owner<T>(slot); Owner<T>* pointer = &slot;",
        "consume(Owner<T>(seed)); Owner<T>* pointer = nullptr;",
        "Owner<T> copy = Owner<T>(seed); Owner<T>* pointer = &copy;",
        "(void)Owner<T>(seed); Owner<T>* pointer = nullptr;",
    };
    auto style = Canonical();
    // Finite width isolates the selected gap from unrelated line wrapping.
    style.ColumnLimit = 10000;
    style.CommaSpaceAfter = formatter::GapPolicy::Space;
    const formatter::PreparedFormat prepared(style);
    for (auto body : bodies) {
        const auto source =
            "template<class T> struct Owner { Owner() {} Owner(T input) {} "
                "void consume(Owner<T> input) {} void inspect(T seed) { " +
                std::string(body) + " invoke(seed,seed); } };\n";
        SCOPED_TRACE(source);
        formatter::FormatResult complete;
        ASSERT_NO_THROW(complete = FormatInput(source, prepared));
        CheckIdentities(source, complete);
        EXPECT_TRUE(complete.StructuralOperations.empty());
        const auto point = uint32_t(source.find("seed,seed") + 5);
        const std::array<parser::SourceRange, 1> selected{{{point, point + 1}}};
        const auto scoped = FormatInput(
            source,
            prepared,
            {.SourceMap = true, .EditScope = selected});
        auto expected = source;
        expected.insert(point, " ");
        EXPECT_EQ(scoped.Text, expected);
        CheckIdentities(source, scoped);
        CheckExcludedCopies(source, scoped, selected);
        EXPECT_EQ(
            FormatInput(
                scoped.Text,
                prepared,
                {.SourceMap = true, .EditScope = selected}).Text,
            scoped.Text);
        const std::array<parser::SourceRange, 1> all{
            {{0, uint32_t(source.size())}},
        };
        const auto frozen = FormatInput(
            source,
            prepared,
            {.ImmutableRanges = all, .SourceMap = true});
        EXPECT_EQ(frozen.Text, source);
        CheckIdentities(source, frozen);
    }
}
TEST(PublicPipeline,
    TerminalConstructorBoundariesKeepOwningMapsAndStatementScopes)
{
    const std::string source = "template<class E> struct Chest { "
        "Chest<E>(E* value) noexcept(check(value)) {} "
        "void inspect(E seed) { Chest<E>(slot); "
        "Chest<E>* pointer = &slot; invoke(seed,seed); } };\n";
    auto style = Canonical();
    style.ColumnLimit = 10000;
    style.CommaSpaceAfter = formatter::GapPolicy::Space;
    const formatter::PreparedFormat prepared(style);
    const auto point = uint32_t(source.find("seed,seed") + 5);
    const std::array<parser::SourceRange, 1> selected{{{point, point + 1}}};
    auto owned = [&] {
        Input input(source);
        const auto tree =
            parser::Parse(input.View(), [] (parser::ParseError) {});
        return formatter::Format(
            tree,
            prepared,
            {.SourceMap = true, .EditScope = selected});
    }();
    auto expected = source;
    expected.insert(point, " ");
    EXPECT_EQ(owned.Text, expected);
    CheckIdentities(source, owned);
    CheckExcludedCopies(source, owned, selected);
    EXPECT_TRUE(owned.StructuralOperations.empty());
    EXPECT_EQ(
        FormatInput(
            owned.Text,
            prepared,
            {.SourceMap = true, .EditScope = selected}).Text,
        owned.Text);
    const auto complete = FormatInput(source, prepared);
    CheckIdentities(source, complete);
    EXPECT_EQ(FormatInput(complete.Text, prepared).Text, complete.Text);
    const std::array<parser::SourceRange, 1> all{
        {{0, uint32_t(source.size())}},
    };
    const auto frozen = FormatInput(
        source,
        prepared,
        {.ImmutableRanges = all, .SourceMap = true});
    EXPECT_EQ(frozen.Text, source);
    CheckIdentities(source, frozen);
}
formatter::Style EndingStyle(std::string_view name)
{
    return formatter::LoadConfigFile(std::filesystem::path(__FILE__)
        .parent_path() / "testdata/line-endings" /
        (std::string(name) + ".hit-fmt")).FormattingStyle;
}

TEST(PublicPipeline, LineEndingWholeAtomsScopeAndExistingEOFReplacement)
{
    const std::string source = "int a;\r\nint b;\r\n";
    const formatter::PreparedFormat prepared(EndingStyle("lf"));
    const auto first = parser::SourceOffset(source.find('\r'));
    for (const auto range :
        {
            parser::SourceRange{first, first + 1},
            parser::SourceRange{first + 1, first + 2},
        })
    {
        const std::array selected{range};
        const auto result = FormatInput(
            source,
            prepared,
            {.SourceMap = true, .EditScope = selected});
        EXPECT_EQ(result.Text, source);
        CheckExcludedCopies(source, result, selected);
    }
    const std::array selected{
        parser::SourceRange{first, first + 2},
        parser::SourceRange{
            parser::SourceOffset(source.size() - 2),
            parser::SourceOffset(source.size()),
        },
    };
    const auto result = FormatInput(
        source,
        prepared,
        {.SourceMap = true, .EditScope = selected});
    EXPECT_EQ(result.Text, "int a;\nint b;\n");
    CheckIdentities(source, result);
    CheckExcludedCopies(source, result, selected);
    const std::array<parser::SourceRange, 0> none{};
    EXPECT_EQ(
        FormatInput(source, prepared, {.SourceMap = true, .EditScope = none})
            .Text,
        source);
    const std::array immutable{selected[1]};
    EXPECT_EQ(
        FormatInput(
            source,
            prepared,
            {.ImmutableRanges = immutable, .SourceMap = true}).Text,
        "int a;\nint b;\r\n");
    for (const auto name : {"lf", "crlf", "legacy"}) {
        const formatter::PreparedFormat mode(EndingStyle(name));
        for (const auto text : {"", "int a;", "int a;\r"}) {
            EXPECT_EQ(FormatInput(text, mode).Text, text);
        }
        const std::string noFinal = "int a;";
        const std::array whole{parser::SourceRange{0, 6}};
        EXPECT_EQ(
            FormatInput(noFinal, mode, {.SourceMap = true, .EditScope = whole})
                .Text,
            noFinal);
    }
}

TEST(PublicPipeline, LineEndingOwningMapsRetainedPrefixesAndIdempotence)
{
    const std::string source = "void f(){\n \t\n  return;\n}\n";
    formatter::FormatResult result;
    {
        auto style = EndingStyle("crlf");
        style.IndentationKind = formatter::IndentationKind::Spaces;
        style.IndentWidth = 2;
        const formatter::PreparedFormat prepared(style);
        result = FormatInput(source, prepared);
        EXPECT_EQ(result.Text, "void f(){\r\n \t\r\n  return;\r\n}\r\n");
        EXPECT_EQ(FormatInput(result.Text, prepared).Text, result.Text);
        EXPECT_EQ(FormatInput(source, prepared, {}).Text, result.Text);
    }
    CheckIdentities(source, result);
    unsigned changed = 0;
    for (const auto& map : result.Map) {
        if (!map.Exact && map.HasOriginal &&
            map.OriginalToken == document::NoIdentity &&
            source.substr(
                map.Original.Begin,
                map.Original.End - map.Original.Begin) == "\n")
        {
            EXPECT_EQ(
                result.Text.substr(
                    map.Output.Begin,
                    map.Output.End - map.Output.Begin),
                "\r\n");
            EXPECT_FALSE(map.Protected);
            ++changed;
        }
    }
    EXPECT_GE(
        changed,
        2u); // EOF and retained prefix, not just regenerated gaps.
}

TEST(PublicPipeline, LineEndingTokenOwnedCRBareCRProtectionAndIncludeMovement)
{
    const std::string source = "#define VALUE 1\r\n// note\r\n\r\n"
        "int a;\r \n/* block\r\ntext */\n" "const char* s=R\"(one\r\ntwo)\";\n"
        "// clang-format off\r\nint   b;\r\n// clang-format on\r\n" "int c;\n";
    for (const auto name : {"lf", "crlf"}) {
        const formatter::PreparedFormat prepared(EndingStyle(name));
        const auto result = FormatInput(source, prepared);
        CheckIdentities(source, result);
        EXPECT_NE(result.Text.find("#define VALUE 1\r\n"), std::string::npos);
        EXPECT_NE(result.Text.find("// note\r\n"), std::string::npos);
        EXPECT_NE(result.Text.find("a;\r "), std::string::npos);
        EXPECT_NE(
            result.Text.find("int   b;\r\n// clang-format on\r\n"),
            std::string::npos);
        EXPECT_EQ(result.Text.find("\r\r\n"), std::string::npos);
        EXPECT_EQ(FormatInput(result.Text, prepared).Text, result.Text);
    }
    auto style = EndingStyle("lf");
    style.IncludeOrder = formatter::IncludeOrder::Sort;
    const formatter::PreparedFormat prepared(style);
    // Mixed physical ending islands do not acquire movement rights.
    const std::string mixed =
        "#include \"z.h\"\r\n#include \"a.h\"\n\nint x;\r\n";
    const auto nonmoving = FormatInput(mixed, prepared);
    EXPECT_TRUE(
        nonmoving.Text.starts_with(mixed.substr(0, mixed.find("int x"))));
    CheckIdentities(mixed, nonmoving);
    const std::string includes =
        "#include \"z.h\"\r\n#include \"a.h\"\r\n\nint x;\r\n";
    const auto moved = FormatInput(includes, prepared);
    EXPECT_NE(
        moved.Text.find("#include \"a.h\"\r\n#include \"z.h\"\r\n"),
        std::string::npos);
    EXPECT_TRUE(moved.Text.ends_with("int x;\n"));
    CheckIdentities(includes, moved);
    EXPECT_EQ(FormatInput(moved.Text, prepared).Text, moved.Text);
}

TEST(PublicPipeline, LineEndingPreserveRetainsLegacyMixedGeneratedDecisions)
{
    const std::string source = "int a;\r\nvoid f(){return;}\n";
    auto omitted = Canonical();
    omitted.ColumnLimit = 10000;
    auto explicitPreserve = EndingStyle("legacy");
    explicitPreserve.Layout = formatter::LayoutMode::Canonical;
    const auto before = FormatInput(source, formatter::PreparedFormat(omitted));
    const auto after =
        FormatInput(source, formatter::PreparedFormat(explicitPreserve));
    EXPECT_EQ(after.Text, before.Text);
    EXPECT_NE(after.Text.find("{\r\n"), std::string::npos);
    EXPECT_TRUE(after.Text.ends_with("}\n"));
    CheckIdentities(source, after);
}

TEST(PublicPipeline, LineEndingCommentGuardDoesNotGrantPartialNewlineRights)
{
    const std::string source = "// lf\n\nint a;\n// crlf\r\n\nint b;\n";
    const formatter::PreparedFormat prepared(EndingStyle("crlf"));
    const auto result = FormatInput(source, prepared);
    EXPECT_EQ(result.Text, "// lf\n\r\nint a;\r\n// crlf\r\n\r\nint b;\r\n");
    CheckIdentities(source, result);
    EXPECT_EQ(FormatInput(result.Text, prepared).Text, result.Text);
    for (const auto boundary : {source.find("lf") + 2, source.find("crlf") + 4})
    {
        unsigned copies = 0;
        for (const auto& map : result.Map) {
            if (map.Exact && map.HasOriginal &&
                map.Original.Begin <= boundary && boundary < map.Original.End)
            {
                ++copies;
            }
        }
        EXPECT_EQ(copies, 1u);
    }
    auto style = EndingStyle("lf");
    style.TrailingWhitespace = formatter::TrailingWhitespace::Trim;
    const std::string partialSource = "int a; \r\n  int b;\r\n";
    const std::array selected{parser::SourceRange{6, 7}};
    const auto partial = FormatInput(
        partialSource,
        formatter::PreparedFormat(style),
        {.SourceMap = true, .EditScope = selected});
    EXPECT_EQ(partial.Text, "int a;\r\n  int b;\r\n");
    CheckExcludedCopies(partialSource, partial, selected);
    CheckIdentities(partialSource, partial);
}

TEST(PublicPipeline, LineEndingFullBytesAtEndKeepHorizontalAndPartialFences)
{
    const formatter::PreparedFormat prepared(EndingStyle("lf"));
    const std::string source = "int a; \r\nint b;\r\n";
    const std::array selected{
        parser::SourceRange{6, 9},
        parser::SourceRange{
            parser::SourceOffset(source.size() - 2),
            parser::SourceOffset(source.size()),
        },
    };
    const auto result = FormatInput(
        source,
        prepared,
        {.SourceMap = true, .EditScope = selected});
    EXPECT_EQ(result.Text, "int a; \nint b;\n");
    CheckIdentities(source, result);
    CheckExcludedCopies(source, result, selected);
    EXPECT_EQ(FormatInput(result.Text, prepared).Text, result.Text);
    const std::array atomOnly{parser::SourceRange{7, 9}};
    const auto partial = FormatInput(
        source,
        prepared,
        {.SourceMap = true, .EditScope = atomOnly});
    EXPECT_EQ(partial.Text, source); // no right to evade the unselected prefix.
    CheckExcludedCopies(source, partial, atomOnly);
    const std::array immutable{parser::SourceRange{8, 9}};
    EXPECT_EQ(
        FormatInput(
            source,
            prepared,
            {
                .ImmutableRanges = immutable,
                .SourceMap = true,
                .EditScope = selected,
            }).Text,
        "int a; \r\nint b;\n");
}

void CheckBareCRCopies(
    std::string_view source,
    const formatter::FormatResult& result)
{
    CheckIdentities(source, result);
    for (size_t i = 0; i < source.size(); ++i)
    {
        if (source[i] != '\r' ||
            (i + 1 < source.size() && source[i + 1] == '\n'))
        {
            continue;
        }
        unsigned copies = 0;
        for (const auto& map : result.Map) {
            if (!map.Exact || !map.HasOriginal || map.Original.Begin > i ||
                map.Original.End <= i)
            {
                continue;
            }
            ++copies;
            const auto output = map.Output.Begin + i - map.Original.Begin;
            ASSERT_LT(output, result.Text.size());
            EXPECT_EQ(result.Text[output], '\r');
            if (output + 1 < result.Text.size()) {
                EXPECT_NE(result.Text[output + 1], '\n');
            }
        }
        EXPECT_EQ(copies, 1u) << "original bare CR " << i;
    }
}

TEST(PublicPipeline, LineEndingBareCRMembershipMapsAndRepeatedSelection)
{
    for (const auto name : {"lf", "crlf"}) {
        auto style = EndingStyle(name);
        style.TrailingWhitespace = formatter::TrailingWhitespace::Trim;
        const formatter::PreparedFormat prepared(style);
        for (unsigned count : {1u, 2u, 3u, 8u}) {
            for (const std::string tail :
                {
                    std::string(count, '\r') + "\n",
                    std::string(count, '\r') + " \t\r\n \t\n",
                    std::string(count, '\r') + " \t",
                })
            {
                const auto source = "int a;" + tail;
                const auto first = FormatInput(source, prepared);
                CheckBareCRCopies(source, first);
                const auto second = FormatInput(first.Text, prepared);
                EXPECT_EQ(second.Text, first.Text);
                CheckBareCRCopies(first.Text, second);
                EXPECT_EQ(FormatInput(second.Text, prepared).Text, second.Text);
                style.LineEnding = name == std::string_view("lf")
                    ? formatter::LineEnding::CRLF
                    : formatter::LineEnding::LF;
                const formatter::PreparedFormat opposite(style);
                const auto changed = FormatInput(first.Text, opposite);
                CheckBareCRCopies(first.Text, changed);
                EXPECT_EQ(
                    FormatInput(changed.Text, opposite).Text,
                    changed.Text);
            }
        }
    }
}

TEST(PublicPipeline, LineEndingBareCRScopeProtectionAndOwningGeometry)
{
    const std::string source = "int a;\r\r\nint b;\r \n";
    auto style = EndingStyle("lf");
    style.TrailingWhitespace = formatter::TrailingWhitespace::Trim;
    const formatter::PreparedFormat prepared(style);
    for (const auto range :
        {
            parser::SourceRange{6, 9},
            parser::SourceRange{7, 9},
            parser::SourceRange{7, 8},
            parser::SourceRange{8, 9},
            parser::SourceRange{16, 17},
        })
    {
        const std::array selected{range};
        const auto result = FormatInput(
            source,
            prepared,
            {.SourceMap = true, .EditScope = selected});
        EXPECT_EQ(result.Text, source);
        CheckExcludedCopies(source, result, selected);
        CheckBareCRCopies(source, result);
    }
    const std::array immutable{parser::SourceRange{6, 9}};
    const auto protectedResult = FormatInput(
        source,
        prepared,
        {.ImmutableRanges = immutable, .SourceMap = true});
    EXPECT_EQ(protectedResult.Text, source);
    CheckBareCRCopies(source, protectedResult);
    const std::string function = "void f(){\r\r\n    return;\n}\n";
    formatter::FormatResult owned;
    {
        auto local = EndingStyle("lf");
        local.IndentationKind = formatter::IndentationKind::Spaces;
        local.IndentWidth = 2;
        const formatter::PreparedFormat localPrepared(local);
        Input input(function);
        auto tree = parser::Parse(input.View(), [] (parser::ParseError) {});
        owned = formatter::Format(tree, localPrepared, {.SourceMap = true});
        EXPECT_EQ(owned.Text, FormatInput(function, localPrepared).Text);
        EXPECT_NE(owned.Text.find("{\r\r\n    return;"), std::string::npos);
        EXPECT_EQ(FormatInput(owned.Text, localPrepared).Text, owned.Text);
    }
    CheckBareCRCopies(function, owned);
}

TEST(PublicPipeline, LineEndingBareCRNullAdmissionRetainsFirstOriginalIdentity)
{
    for (const bool compound : {false, true}) {
        auto style = EndingStyle("lf");
        style.RemoveNullDeclarations = true;
        style.RemoveNullStatements = true;
        const formatter::PreparedFormat prepared(style);
        const std::string source = compound
            ? "void f(){int a;\r;;;;\nint b;\n}\n"
            : "int a;\r;;;;\nint b;\n";
        const auto result = FormatInput(source, prepared);
        CheckBareCRCopies(source, result);
        EXPECT_NE(result.Text.find("a;\r;\n"), std::string::npos);
        Input input(source);
        auto tree = parser::Parse(input.View(), [] (parser::ParseError) {});
        const auto first = source.find("\r;") + 1;
        for (parser::TokenIndex i = 0; i < tree.TokenCount(); ++i)
            if (tree.Token(i).Text.Begin >= first &&
                tree.Token(i).Text.Begin < first + 4)
            {
                EXPECT_EQ(
                    result.OriginalTokens[i].Present,
                    tree.Token(i).Text.Begin == first);
            }
        EXPECT_EQ(FormatInput(result.Text, prepared).Text, result.Text);
    }
}

TEST(PublicPipeline, LineEndingBareCRPlanningFencesDoNotSuppressSafeNeighbors)
{
    for (const auto name : {"lf", "crlf"}) {
        auto style = EndingStyle(name);
        style.Layout = formatter::LayoutMode::Canonical;
        style.IndentationKind = formatter::IndentationKind::Spaces;
        style.BodyShape = formatter::BodyShape::Expanded;
        style.InitializersTrailingComma = formatter::TrailingCommaPolicy::Never;
        style.MaxEmptyLines = 0;
        const formatter::PreparedFormat prepared(style);
        const std::string source =
            "void f(){\r \n\n    return;}\nint a[]={1\r,\n};\nint b[]={2,};\n";
        const auto result = FormatInput(source, prepared);
        CheckBareCRCopies(source, result);
        EXPECT_NE(
            result.Text.find(name == std::string_view("lf")
                ? "{\r \n\n    return;"
                : "{\r \r\n\r\n    return;"),
            std::string::npos);
        EXPECT_NE(result.Text.find("1\r,"), std::string::npos);
        EXPECT_EQ(result.Text.find("2,"), std::string::npos);
        EXPECT_EQ(FormatInput(result.Text, prepared).Text, result.Text);
    }
    auto legacy = EndingStyle("legacy");
    legacy.TrailingWhitespace = formatter::TrailingWhitespace::Trim;
    auto omitted = legacy;
    omitted.LineEnding = formatter::LineEnding::Preserve;
    const std::string source = "int a;\r \nint b;\r\r\n";
    EXPECT_EQ(
        FormatInput(source, formatter::PreparedFormat(legacy)).Text,
        FormatInput(source, formatter::PreparedFormat(omitted)).Text);
}

TEST(PublicPipeline, LineEndingBareCRNewNamespaceEOFDoesNotCaptureWhitespace)
{
    for (const auto name : {"lf", "crlf"}) {
        auto style = EndingStyle(name);
        style.NamespaceComments = formatter::NamespaceComments::Fix;
        style.ShortNamespaceLines = 0;
        const formatter::PreparedFormat prepared(style);
        const std::string source = "namespace N {\nint x;\n}\r";
        const auto result = FormatInput(source, prepared);
        EXPECT_EQ(result.Text.find("// namespace"), std::string::npos);
        CheckBareCRCopies(source, result);
        EXPECT_EQ(FormatInput(result.Text, prepared).Text, result.Text);
        const auto paired =
            FormatInput("namespace N {\nint x;\n}\r\n", prepared);
        EXPECT_NE(paired.Text.find("// namespace N"), std::string::npos);
        const auto interior =
            FormatInput("namespace N {\r \nint x;\n}\n", prepared);
        EXPECT_NE(interior.Text.find("// namespace N"), std::string::npos);
    }
}


TEST(PublicPipeline, LineEndingCompositionControlSeparatorGeometryAndDefaults)
{
    const std::string source =
        "void flow(bool ready) {\r\n  if (ready) {\r\n    left();\r\n  }\r\n"
            "  else {\r\n    right();\r\n  }\r\n"
            "  do {\r\n    tick();\r\n  }\r \n"
            "  while (ready);\r\n  do {\r\n    next();\r\n  }\r\n"
            "  while (ready);\r\n}\r\n";
    for (const auto ending :
        {formatter::LineEnding::LF, formatter::LineEnding::CRLF})
    {
        auto style = Canonical();
        style.LineEnding = ending;
        style.IndentWidth = 2;
        style.BodiesLayout = formatter::CategoryLayout::Preserve;
        const formatter::PreparedFormat prepared(style);
        const auto result = FormatInput(source, prepared);
        EXPECT_NE(result.Text.find("} else {"), std::string::npos);
        EXPECT_NE(result.Text.find("} while (ready);"), std::string::npos);
        EXPECT_NE(
            result.Text.find(ending == formatter::LineEnding::LF
                ? "}\r \n  while (ready);"
                : "}\r \r\n  while (ready);"),
            std::string::npos);
        CheckBareCRCopies(source, result);
        EXPECT_EQ(FormatInput(result.Text, prepared).Text, result.Text);
        const auto begin = source.find("}\r \n") + 1;
        const std::array selected{parser::SourceRange{begin, begin + 3}};
        const auto partial = FormatInput(
            source,
            prepared,
            {.SourceMap = true, .EditScope = selected});
        EXPECT_EQ(partial.Text, source);
        CheckExcludedCopies(source, partial, selected);
        CheckBareCRCopies(source, partial);
    }
    auto omitted = Canonical();
    omitted.BodiesLayout = formatter::CategoryLayout::Preserve;
    auto explicitPreserve = omitted;
    explicitPreserve.LineEnding = formatter::LineEnding::Preserve;
    const auto original =
        FormatInput(source, formatter::PreparedFormat(omitted));
    const auto explicitResult =
        FormatInput(source, formatter::PreparedFormat(explicitPreserve));
    EXPECT_EQ(explicitResult.Text, original.Text);
    EXPECT_NE(original.Text.find("} else {"), std::string::npos);
    CheckIdentities(source, explicitResult);
}

TEST(PublicPipeline, LineEndingCompositionRecoveryCloneAndScopedTail)
{
    const std::string candidate =
        "static Vec NOTE(0) load(int Type) {\r\n Type * value; ;\r\n }";
    const std::string source = "using Type = int;\r\n" + candidate +
        "\r\n;\r\nint tail(){ ; return 1; ; }\r\n";
    for (const auto ending :
        {formatter::LineEnding::LF, formatter::LineEnding::CRLF})
    {
        auto style = Canonical();
        style.LineEnding = ending;
        style.BodyShape = formatter::BodyShape::Expanded;
        style.RemoveNullStatements = true;
        style.RemoveNullDeclarations = true;
        const formatter::PreparedFormat prepared(style);
        const auto result = FormatInput(source, prepared);
        EXPECT_FALSE(result.StructuralOperations.empty());
        const auto begin = result.Text.find(candidate);
        ASSERT_NE(begin, std::string::npos);
        bool recovered = false;
        for (const auto& protection : result.OutputProtection) {
            recovered |= protection.Source.Begin <= begin &&
                protection.Source.End >= begin + candidate.size() &&
                (static_cast<uint16_t>(protection.Reasons) &
                    static_cast<
                        uint16_t>(formatter::ProtectionReason::Recovery)) != 0;
        }
        EXPECT_TRUE(recovered);
        EXPECT_TRUE(result.Text
            .ends_with(ending == formatter::LineEnding::LF ? "}\n" : "}\r\n"));
        CheckIdentities(source, result);
        EXPECT_EQ(FormatInput(result.Text, prepared).Text, result.Text);
        const std::array selected{
            parser::SourceRange{source.find("int tail"), source.size()},
        };
        const auto scoped = FormatInput(
            source,
            prepared,
            {.SourceMap = true, .EditScope = selected});
        EXPECT_NE(scoped.Text, source);
        EXPECT_NE(scoped.Text.find(candidate), std::string::npos);
        CheckExcludedCopies(source, scoped, selected);
        CheckIdentities(source, scoped);
        const std::array<parser::SourceRange, 0> none{};
        EXPECT_EQ(
            FormatInput(
                source,
                prepared,
                {.SourceMap = true, .EditScope = none}).Text,
            source);
    }
}

} // namespace

} // namespace public_pipeline

namespace public_pipeline {

namespace {

TEST(PublicPipeline, TrailingCommentSpacesNamespaceOwnershipAndLegacyCoupling)
{
    using formatter::GapPolicy;
    using formatter::NamespaceComments;
    const std::string source = "int amount;\t// namespace is ordinary prose\n"
        "namespace N {\nint local;\n}\t// arbitrary closer prose\n";
    for (const auto old :
        {
            GapPolicy::Preserve,
            GapPolicy::Space,
            GapPolicy::NoSpace,
            GapPolicy::GeneratedSpace,
            GapPolicy::GeneratedNoSpace,
        })
    {
        for (const auto policy :
            {
                NamespaceComments::Preserve,
                NamespaceComments::AddMissing,
                NamespaceComments::Fix,
            })
        {
            formatter::Style style;
            style.TrailingCommentSpacing = old;
            style.TrailingCommentSpaces =
                formatter::TrailingCommentSpaceSetting{2};
            style.NamespaceComments = policy;
            style.NamespaceCommentSpaces = 4;
            const formatter::PreparedFormat prepared(style);
            const auto result = FormatInput(source, prepared);
            EXPECT_TRUE(result.Text.starts_with("int amount;  // namespace"));
            EXPECT_TRUE(result.Text.ends_with(policy == NamespaceComments::Fix
                ? "}    // namespace N\n"
                : "}\t// arbitrary closer prose\n"));
            EXPECT_EQ(FormatInput(result.Text, prepared).Text, result.Text);
            if (policy != NamespaceComments::Fix) {
                CheckIdentities(source, result);
            }
        }
    }
    formatter::Style style;
    style.TrailingCommentSpacing = GapPolicy::Space;
    style.NamespaceComments = NamespaceComments::Fix;
    style.NamespaceCommentSpaces = 4;
    const auto legacy = FormatInput(source, formatter::PreparedFormat(style));
    EXPECT_TRUE(legacy.Text.starts_with("int amount; // namespace"));
    EXPECT_TRUE(legacy.Text.ends_with("} // namespace N\n"));
    style.TrailingCommentSpaces = formatter::TrailingCommentSpaceSetting{};
    const auto preserve = FormatInput(source, formatter::PreparedFormat(style));
    EXPECT_TRUE(preserve.Text.starts_with("int amount;\t// namespace"));
    EXPECT_TRUE(preserve.Text.ends_with("}    // namespace N\n"));
    style.NamespaceComments = NamespaceComments::AddMissing;
    const auto missing = FormatInput(
        "namespace N {\nint local;\n}\n",
        formatter::PreparedFormat(style));
    EXPECT_TRUE(missing.Text.ends_with("}    // namespace N\n"));
}

TEST(PublicPipeline, TrailingCommentSpacesLexicalSafetyOwnsTokensAndMaps)
{
    const std::string source = "int quotient=8 / \t// divisor\n2;\n"
        "int product=8 * \t// factor\n2;\n"
        "int task(){ int value=1; value /= \t// divisor\n2; "
        "return 4 \t// integer\n; }\n"
        "const char* text(){return \"/\" \t// literal\n;}\n"
        "/**/ \t// consecutive comments\n";
    formatter::FormatResult result;
    {
        formatter::Style style;
        style.TrailingCommentSpaces = formatter::TrailingCommentSpaceSetting{0};
        const formatter::PreparedFormat prepared(style);
        result = FormatInput(source, prepared);
        EXPECT_NE(result.Text.find("8 / // divisor"), std::string::npos);
        EXPECT_NE(result.Text.find("8 *// factor"), std::string::npos);
        EXPECT_NE(result.Text.find("/=// divisor"), std::string::npos);
        EXPECT_NE(result.Text.find("4// integer"), std::string::npos);
        EXPECT_NE(result.Text.find("\"/\"// literal"), std::string::npos);
        EXPECT_NE(result.Text.find("/**/ \t// consecutive"), std::string::npos);
        EXPECT_EQ(FormatInput(result.Text, prepared).Text, result.Text);
    }
    CheckIdentities(source, result);
    Input before(source), after(result.Text);
    auto original = parser::Parse(before.View(), [] (parser::ParseError) {});
    auto rendered = parser::Parse(after.View(), [] (parser::ParseError) {});
    ASSERT_EQ(original.TokenCount(), rendered.TokenCount());
    for (parser::TokenIndex i = 0; i < original.TokenCount(); ++i) {
        EXPECT_EQ(original.Kind(i), rendered.Kind(i)) << i;
        EXPECT_EQ(original.TokenText(i), rendered.TokenText(i)) << i;
    }
    bool changedGap = false;
    for (const auto& map : result.Map) {
        changedGap |= !map.Exact && map.HasOriginal &&
            map.OriginalToken == document::NoIdentity;
    }
    EXPECT_TRUE(changedGap);
}

TEST(PublicPipeline,
    TrailingCommentSpacesWholeGapScopesProtectionAndOwningResult)
{
    const std::string source = "int amount; \t// memo\r\n";
    const auto begin = parser::SourceOffset(source.find(';') + 1);
    const auto end = parser::SourceOffset(source.find("//"));
    formatter::FormatResult result;
    {
        formatter::Style style;
        style.TrailingCommentSpaces = formatter::TrailingCommentSpaceSetting{2};
        const formatter::PreparedFormat prepared(style);
        const std::array whole{parser::SourceRange{begin, end}};
        formatter::FormatRequest request{.SourceMap = true};
        request.EditScope = whole;
        result = FormatInput(source, prepared, request);
        EXPECT_EQ(result.Text, "int amount;  // memo\r\n");
        CheckExcludedCopies(source, result, whole);
        for (const auto range :
            {
                parser::SourceRange{begin + 1, end},
                parser::SourceRange{0, begin},
                parser::SourceRange{end, parser::SourceOffset(source.size())},
            })
        {
            const std::array selected{range};
            request.EditScope = selected;
            const auto limited = FormatInput(source, prepared, request);
            EXPECT_EQ(limited.Text, source);
            CheckExcludedCopies(source, limited, selected);
        }
        request.EditScope = std::span<const parser::SourceRange>{};
        EXPECT_EQ(FormatInput(source, prepared, request).Text, source);
        request.EditScope = std::nullopt;
        const std::array immutable{parser::SourceRange{begin + 1, end}};
        request.ImmutableRanges = immutable;
        EXPECT_EQ(FormatInput(source, prepared, request).Text, source);
        request.ImmutableRanges = {};
        const std::string adjacent = "int item;// memo\n";
        const std::array endOnly{
            parser::SourceRange{0, parser::SourceOffset(adjacent.find("//"))},
        };
        request.EditScope = endOnly;
        EXPECT_EQ(FormatInput(adjacent, prepared, request).Text, adjacent);
    }
    CheckIdentities(source, result);
}

TEST(PublicPipeline, TrailingCommentSpacesBoundedScalarReportsHonestWidth)
{
    formatter::Style style;
    style.ColumnLimit = 10000;
    style.WidthDiagnostics = formatter::WidthDiagnosticPolicy::Report;
    style.MaxEmptyLines = 0;
    style.TrailingCommentSpaces = formatter::TrailingCommentSpaceSetting{10000};
    const formatter::PreparedFormat prepared(style);
    const auto result = FormatInput("int item;\t// memo\n", prepared);
    const auto comment = result.Text.find("// memo");
    ASSERT_EQ(comment, std::string("int item;").size() + 10000);
    EXPECT_EQ(
        result
            .Text
            .substr(9, 10000)
            .find_first_not_of(' '),
        std::string::npos);
    EXPECT_FALSE(result.Diagnostics.empty());
    CheckIdentities("int item;\t// memo\n", result);
    EXPECT_EQ(FormatInput(result.Text, prepared).Text, result.Text);
}

}

}

namespace public_pipeline {

namespace {

TEST(PublicPipeline,
    TrailingCommentSpacesCompositionRetainsScopedProducerBoundaries)
{
    // These prefixes exercise the integrated separator, annotation and terminal
    // producers. A comment-count demand cannot grant authority over any prefix.
    for (const std::string_view prefix :
        {
            "void work(bool ready){if(ready){tick();}\nelse{wait();}}\n",
            "using Type = int;\n"
                "static Vec NOTE(0) load(int Type) { Type * value; }\n",
            "template<class T> struct Owner { Owner() {} "
                "void inspect(T seed) { "
                "Owner<T>(slot); Owner<T>* pointer = &slot; } };\n",
        })
    {
        const auto source =
            std::string(prefix) + "int tail; \t// scoped memo\n";
        const auto begin = parser::SourceOffset(source.rfind(';') + 1);
        const auto end = parser::SourceOffset(source.find("// scoped"));
        const std::array selected{parser::SourceRange{begin, end}};
        auto style = Canonical();
        style.ColumnLimit = 10000;
        style.TrailingCommentSpaces = formatter::TrailingCommentSpaceSetting{2};
        const formatter::PreparedFormat prepared(style);
        formatter::FormatResult owned;
        {
            Input input(source);
            const auto tree =
                parser::Parse(input.View(), [] (parser::ParseError) {});
            owned = formatter::Format(
                tree,
                prepared,
                {.SourceMap = true, .EditScope = selected});
        }
        EXPECT_EQ(owned.Text.substr(0, prefix.size()), prefix);
        EXPECT_TRUE(owned.Text.ends_with("int tail;  // scoped memo\n"));
        EXPECT_TRUE(owned.StructuralOperations.empty());
        CheckIdentities(source, owned);
        CheckExcludedCopies(source, owned, selected);
        EXPECT_EQ(
            FormatInput(
                owned.Text,
                prepared,
                {.SourceMap = true, .EditScope = selected}).Text,
            owned.Text);
        const std::array partial{parser::SourceRange{begin + 1, end}};
        EXPECT_EQ(
            FormatInput(
                source,
                prepared,
                {.SourceMap = true, .EditScope = partial}).Text,
            source);
    }
}

TEST(PublicPipeline, FunctionBodyMacrosRetainHeadersThroughNormalization)
{
    auto style = Canonical();
    style.IndentationKind = formatter::IndentationKind::Spaces;
    style.FunctionBracePlacement = formatter::BracePlacement::NextLine;
    style.BinaryOperatorSpacing = formatter::GapPolicy::Space;
    style.FunctionBodyMacroNames = {"DEFINE_CASE"};
    style.WhitespaceSensitiveMacros = {"TOKEN_TEXT"};
    style.AddMissingControlBraces = true;
    style.RemoveNullStatements = true;
    const formatter::PreparedFormat prepared(style);
    style.FunctionBodyMacroNames.clear();
    const std::string header =
        "static inline DEFINE_CASE(Shared, Helper(alpha   +  beta), "
            "const Shared& input)";
    const std::string source = "using Shared = int;\n" + header +
        " {Shared* pointer; auto value=left+right; ; "
            "if(value) Use(value); TOKEN_TEXT(alpha   +  beta);}\n";
    Input input(source);
    auto tree = parser::Parse(input.View(), {}, prepared.ParsingOptions());
    const auto* macro = tree.Root().FirstChild->NextSibling;
    ASSERT_NE(macro, nullptr);
    ASSERT_EQ(macro->Kind, parser::NodeKind::MacroFunctionDefinition);
    const auto* body = macro->FirstChild->NextSibling;
    ASSERT_NE(body, nullptr);
    ASSERT_EQ(body->FirstChild->Kind, parser::NodeKind::DeclarationStatement);
    const auto result = formatter::Format(tree, prepared, {.SourceMap = true});
    EXPECT_EQ(result.Text, FormatInput(source, prepared).Text);
    EXPECT_NE(result.Text.find(header), std::string::npos);
    EXPECT_NE(result.Text.find("left + right"), std::string::npos);
    EXPECT_NE(
        result.Text.find("TOKEN_TEXT(alpha   +  beta)"),
        std::string::npos);
    EXPECT_FALSE(result.StructuralOperations.empty());
    EXPECT_EQ(FormatInput(result.Text, prepared).Text, result.Text);
    CheckIdentities(source, result);
}

TEST(PublicPipeline, TextPoliciesRespectOriginalScopeAndProtection)
{
    formatter::Style style;
    style.IndentPreprocessingDirectives = true;
    style.AlignMacroBackslashes = true;
    style.RulerLength = 16;
    style.RulerBlankLinesBefore = {formatter::BlankLineMode::Exact, 1};
    style.RulerBlankLinesAfter = {formatter::BlankLineMode::Exact, 1};
    const formatter::PreparedFormat prepared(style);
    const std::string source = "#if OUTER\n\t # if INNER\n"
        "#define VALUE first \\\n    second    \\\n    third\n"
        "# endif\n# endif\n/////\nint item;\n";
    const auto begin = parser::SourceOffset(source.find('\t'));
    const auto end = parser::SourceOffset(source.find('\n', begin));
    const std::array selected{parser::SourceRange{begin, end}};
    const auto limited = FormatInput(
        source,
        prepared,
        {.SourceMap = true, .EditScope = selected});
    EXPECT_NE(limited.Text.find("    #if INNER"), std::string::npos);
    CheckIdentities(source, limited);
    CheckExcludedCopies(source, limited, selected);
    const std::array partial{parser::SourceRange{begin + 1, end}};
    const auto denied = FormatInput(
        source,
        prepared,
        {.SourceMap = true, .EditScope = partial});
    EXPECT_EQ(denied.Text, source);
    CheckExcludedCopies(source, denied, partial);
    // Selecting the token alone does not authorize moving its leading prefix.
    const std::array tokenOnly{parser::SourceRange{begin + 2, end}};
    const auto tokenDenied = FormatInput(
        source,
        prepared,
        {.SourceMap = true, .EditScope = tokenOnly});
    EXPECT_EQ(tokenDenied.Text, source);
    CheckExcludedCopies(source, tokenDenied, tokenOnly);
    const std::array prefix{parser::SourceRange{begin, begin + 2}};
    const auto prefixProtected = FormatInput(
        source,
        prepared,
        {.ImmutableRanges = prefix, .SourceMap = true});
    EXPECT_NE(prefixProtected.Text.find("\t # if INNER"), std::string::npos);
    CheckIdentities(source, prefixProtected);
    const std::array immutable{
        parser::SourceRange{0, parser::SourceOffset(source.size())},
    };
    EXPECT_EQ(
        FormatInput(source, prepared, {.ImmutableRanges = immutable}).Text,
        source);
    const auto disabled = "// hit-fmt off\n" + source + "// hit-fmt on\n";
    EXPECT_EQ(FormatInput(disabled, prepared).Text, disabled);
    const auto result = FormatInput(source, prepared);
    CheckIdentities(source, result);
    EXPECT_EQ(FormatInput(result.Text, prepared).Text, result.Text);
    EXPECT_FALSE(result.StructuralOperations.empty());
}

TEST(PublicPipeline, SortedDirectiveWhitespaceProjectsSelectedIncludeAtoms)
{
    formatter::Style style;
    style.IndentPreprocessingDirectives = true;
    style.IncludeOrder = formatter::IncludeOrder::Sort;
    const formatter::PreparedFormat prepared(style);
    const std::string source = "#if OUTER\n\t #include \"zeta.h\"\n"
        "  #include \"alpha.h\"\n#endif\nint tail;\n";
    const auto begin = parser::SourceOffset(source.find('\t'));
    const auto end = parser::SourceOffset(source.find("#endif"));
    const std::array selected{parser::SourceRange{begin, end}};
    const auto result = FormatInput(
        source,
        prepared,
        {.SourceMap = true, .EditScope = selected});
    EXPECT_LT(result.Text.find("alpha.h"), result.Text.find("zeta.h"));
    EXPECT_NE(result.Text.find("    #include"), std::string::npos);
    CheckIdentities(source, result);
    CheckExcludedCopies(source, result, selected);
    EXPECT_EQ(FormatInput(result.Text, prepared).Text, result.Text);
}

TEST(PublicPipeline, UnlimitedWidthHasNoHiddenTenThousandColumnLimit)
{
    auto style = Canonical();
    style.ColumnLimit = 0;
    style.WidthDiagnostics = formatter::WidthDiagnosticPolicy::Report;
    const formatter::PreparedFormat prepared(style);
    const auto source = "auto value = \"" + std::string(10050, 'x') + "\";\n";
    const auto result = FormatInput(source, prepared);
    EXPECT_EQ(result.Text, source);
    EXPECT_TRUE(result.Diagnostics.empty());
    CheckIdentities(source, result);
}

TEST(PublicPipeline, FunctionBodyMacroHintsCannotOverrideProtection)
{
    auto style = Canonical();
    style.IndentationKind = formatter::IndentationKind::Spaces;
    style.FunctionBodyMacroNames = {"DEFINE_CASE"};
    style.BinaryOperatorSpacing = formatter::GapPolicy::Space;
    style.HonorClangFormatDirectives = true;
    const formatter::PreparedFormat prepared(style);
    const std::string source =
        "DEFINE_CASE(Suite, Case) {auto sum=left+right; Use(sum);}\n";
    const auto begin = parser::SourceOffset(source.find('{') + 1);
    const auto end = parser::SourceOffset(source.find('}'));
    const std::array selected{parser::SourceRange{begin, end}};
    const auto limited = FormatInput(
        source,
        prepared,
        {.SourceMap = true, .EditScope = selected});
    EXPECT_NE(limited.Text.find("left + right"), std::string::npos);
    CheckExcludedCopies(source, limited, selected);
    const std::array immutable{
        parser::SourceRange{0, parser::SourceOffset(source.size())},
    };
    EXPECT_EQ(
        FormatInput(source, prepared, {.ImmutableRanges = immutable}).Text,
        source);
    const auto disabled =
        "// clang-format off\n" + source + "// clang-format on\n";
    EXPECT_EQ(FormatInput(disabled, prepared).Text, disabled);
    for (const auto malformed :
        {
            "DEFINE_CASE(Suite, [bad)) {left+right;}\n",
            "DEFINE_CASE(Suite, Bad) {left+right;\n",
        })
    {
        Input input(malformed);
        auto tree = parser::Parse(
            input.View(),
            [] (auto) {},
            prepared.ParsingOptions());
        ASSERT_NE(tree.Root().FirstChild, nullptr);
        EXPECT_NE(
            tree.Root().FirstChild->Kind,
            parser::NodeKind::MacroFunctionDefinition);
        EXPECT_EQ(FormatInput(malformed, prepared).Text, malformed);
    }
}

}

void CheckFormattedResult(
    std::string_view source,
    const formatter::Style& style,
    std::string_view expectedText)
{
    const formatter::PreparedFormat prepared(style);
    const auto result = FormatInput(source, prepared);
    EXPECT_EQ(result.Text, expectedText);
    CheckIdentities(source, result);
    EXPECT_EQ(FormatInput(result.Text, prepared).Text, result.Text);
}

}
