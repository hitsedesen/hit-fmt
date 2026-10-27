#include <formatter/config.hpp>
#include <formatter/formatter.hpp>
#include <formatter/source_analysis.hpp>

#include <gtest/gtest.h>

#include <tests/formatting.hpp>
#include <tests/golden.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <memory_resource>
#include <stdexcept>
#include <string>

namespace {

using test::Input;
using test::FormatInput;

formatter::Style Canonical()
{
    formatter::Style style;
    style.Layout = formatter::LayoutMode::Canonical;
    style.CommaSpaceAfter = formatter::GapPolicy::GeneratedSpace;
    style.CommaSpaceBefore = formatter::GapPolicy::GeneratedNoSpace;
    style.ControlBlockKeywordSpacing = formatter::GapPolicy::Space;
    return style;
}

TEST(PublicPipeline, MultilineCommentIndentationHonorsWholeTokenAuthority)
{
    auto style = Canonical();
    style.IndentWidth = 4;
    style.IndentationKind = formatter::IndentationKind::Spaces;
    const formatter::PreparedFormat prepared(style);
    const std::string source = "void f() {\n/* note\n * body\n */\n}\n";
    const auto begin = parser::SourceOffset(source.find("/*"));
    const std::array partial{parser::SourceRange{begin - 1, begin + 1}};
    const auto scoped = FormatInput(source, prepared, {.EditScope = partial});
    EXPECT_NE(scoped.Text.find("/* note\n * body\n */"), std::string::npos);
    const std::array immutable{parser::SourceRange{begin + 8, begin + 9}};
    const auto protectedResult = FormatInput(source, prepared, {.ImmutableRanges = immutable});
    EXPECT_NE(protectedResult.Text.find("/* note\n * body\n */"), std::string::npos);
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
    const std::string source =
        "void f() {\n    auto value = engine()\n" "            .Open()\n            .Finish();\n}\n";
    const auto begin = parser::SourceOffset(source.find("            .Open"));
    const parser::SourceRange selected[] = {{begin, begin + 12}};
    formatter::FormatRequest request{.EditScope = selected};
    const auto preserved = FormatInput(source, preserveIndent, request);
    EXPECT_EQ(preserved.Text, source);
    Input input(source);
    const auto tree = parser::Parse(input.View(), [] (parser::ParseError) {});
    std::pmr::monotonic_buffer_resource file;
    std::pmr::monotonic_buffer_resource scratch;
    formatter::SourceAnalysisOptions options;
    options.EditScope = selected;
    const auto safety = formatter::BuildSourceAnalysis(tree, options, file, scratch);
    ASSERT_EQ(safety.PartialGapPermissions().size(), 1u);
    const auto permission = safety.PartialGapPermissions().front();
    EXPECT_EQ(permission.Kind, formatter::PartialGapPermissionKind::FinalIndentation);
    EXPECT_EQ(permission.Source.Begin, begin);
    EXPECT_EQ(permission.Source.End, begin + 12);
    const auto& gap = safety.Gaps()[permission.Gap];
    EXPECT_EQ(gap.Source.Begin, begin - 1);
    EXPECT_EQ(gap.Source.End, begin + 12);
    EXPECT_EQ(safety.Original(gap), "\n            ");
    EXPECT_TRUE(gap.Preserve);
    EXPECT_NE(
        static_cast<uint16_t>(gap.Reasons) & static_cast<uint16_t>(formatter::ProtectionReason::OutsideEditScope),
        0u);
    // A permission is not a style demand. Only explicit Spaces selects the
    // narrow final-indent edit, leaving the LF and excluded endpoint exact.
    style.IndentationKind = formatter::IndentationKind::Spaces;
    const formatter::PreparedFormat prepared(style);
    const auto partial = FormatInput(source, prepared, request);
    auto expected = source;
    expected.replace(begin, 12, "        ");
    EXPECT_EQ(partial.Text, expected);
    request.EditScope = std::span<const parser::SourceRange>{};
    const auto denied = FormatInput(source, prepared, request);
    EXPECT_EQ(denied.Text, source);
    request.EditScope = std::nullopt;
    request.ImmutableRanges = selected;
    const auto immutable = FormatInput(source, prepared, request);
    EXPECT_NE(immutable.Text.find("\n            .Open()"), std::string::npos);
    const std::string protectedSource = "// clang-format off\n" + source + "// clang-format on\n";
    request.ImmutableRanges = {};
    EXPECT_EQ(FormatInput(protectedSource, prepared, request).Text, protectedSource);
}

TEST(PublicPipeline, StyleCheckpointKeepsIndependentGapPermissions)
{
    const std::string source =
        "void f() {\r\n    auto value = engine()\r\n" "            .Open()\r\n            .Finish(); \t// note\n}\r\n";
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
        const auto result = FormatInput(source, prepared, {.EditScope = selected});
        auto expected = source;
        expected.replace(begin, size, begin == indent ? "        " : "  ");
        EXPECT_EQ(result.Text, expected);
    }
}

TEST(PublicPipeline, LambdaArgumentsRespectEditScope)
{
    for (const auto source :
        {
            "struct Registry {}; const Registry entry(7, [] { return 2; });\n" "int   following;\n",
            "APPLY(7, ::ops::combine(::ops::make()), " "[](const auto& v) -> std::string { return v; });\n"
                "int   following;\n",
            "APPLY([]() -> decltype(Item{}) { return Item{}; });\n" "int   following;\n",
            "void use() { consume(flag ? first, second : third, " "[] { return 2; }); }\nint   following;\n",
            "void use() { consume(flag ? other ? first, second : third, "
                "fourth : fifth, [] { return 2; }); }\nint   following;\n",
            "APPLY([]<class T, class U>(T value) requires Check<T{}> " "{ return value; });\nint   following;\n",
            "APPLY([] marker(int hidden) { return hidden; }, " "[] { return 2; });\nint   following;\n",
            "APPLY([]);\nint   following;\n",
        })
    {
        SCOPED_TRACE(source);
        for (const auto bodies : {formatter::CategoryLayout::Canonical, formatter::CategoryLayout::Preserve}) {
            auto style = Canonical();
            style.LambdaBodiesLayout = bodies;
            const formatter::PreparedFormat prepared(style);
            ASSERT_NO_THROW(([&] {
                const auto result = FormatInput(source, prepared);
                EXPECT_EQ(FormatInput(result.Text, prepared).Text, result.Text);
                const auto end = std::string_view(source).find("\n");
                const std::array<parser::SourceRange, 1> scope{{{0, static_cast<parser::SourceOffset>(end)}}};
                formatter::FormatRequest request{};
                request.EditScope = scope;
                const auto limited = FormatInput(source, prepared, request);
                EXPECT_TRUE(limited.Text.ends_with("\nint   following;\n"));
            }()));
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
            {"if (!ready) return; [[fallthrough]];", "if (outer) if (!ready) return; [[fallthrough]];"})
        {
            SCOPED_TRACE(candidate);
            SCOPED_TRACE(braces);
            const std::string source = "void sample(int n, bool ready, bool outer) { " "switch(n) { case 1: " +
                std::string(candidate) + " case 2: break; } }\n";
            const auto result = FormatInput(source, prepared);
            EXPECT_NE(result.Text.find("[[fallthrough]];"), std::string::npos);
            EXPECT_EQ(FormatInput(result.Text, prepared).Text, result.Text);
        }
    }
}

TEST(PublicPipeline, ControlSeparatorsRespectIndependentBodyAndHeaderPolicies)
{
    for (auto headers : {formatter::CategoryLayout::Preserve, formatter::CategoryLayout::Canonical}) {
        for (auto bodies : {formatter::CategoryLayout::Preserve, formatter::CategoryLayout::Canonical}) {
            for (auto shape : {formatter::BodyShape::Expanded, formatter::BodyShape::CompactIfFits}) {
                auto style = Canonical();
                style.IndentWidth = 2;
                style.ControlHeadersLayout = headers;
                style.BodiesLayout = bodies;
                style.BodyShape = shape;
                style.ControlBracePlacement = formatter::BracePlacement::NextLine;
                const formatter::PreparedFormat prepared(style);
                const std::string source = "void f() {\n  if (a) {\n    left();\n  }\n" "  else {\n    right();\n  }\n"
                    "  do {\n    tick();\n  }\n  while (a);\n}\n";
                const auto result = FormatInput(source, prepared);
                if (headers == formatter::CategoryLayout::Canonical) {
                    EXPECT_NE(result.Text.find("} else\n  {"), std::string::npos);
                    EXPECT_NE(result.Text.find("} while (a);"), std::string::npos);
                } else {
                    EXPECT_NE(result.Text.find("}\n  else {"), std::string::npos);
                    EXPECT_NE(result.Text.find("}\n  while (a);"), std::string::npos);
                }
                if (bodies == formatter::CategoryLayout::Preserve) {
                    EXPECT_NE(result.Text.find("{\n    left();\n  }"), std::string::npos);
                }
                EXPECT_EQ(FormatInput(result.Text, prepared).Text, result.Text);
                const std::string unbraced = "void f() {\n  if (a)left();else right();\n" "  do tick();while (a);\n}\n";
                const auto arms = FormatInput(unbraced, prepared);
                if (headers == formatter::CategoryLayout::Canonical && bodies == formatter::CategoryLayout::Canonical) {
                    const auto separator = shape == formatter::BodyShape::Expanded ? "left();\n  else" : "left(); else";
                    const auto suffix = shape == formatter::BodyShape::Expanded ? "tick();\n  while" : "tick(); while";
                    EXPECT_NE(arms.Text.find(separator), std::string::npos);
                    EXPECT_NE(arms.Text.find(suffix), std::string::npos);
                } else {
                    EXPECT_NE(arms.Text.find("left();else"), std::string::npos);
                    EXPECT_NE(arms.Text.find("tick();while"), std::string::npos);
                }
                EXPECT_EQ(FormatInput(arms.Text, prepared).Text, arms.Text);
            }
        }
    }
}

TEST(PublicPipeline, ControlSeparatorsRespectProtectionAndScope)
{
    auto style = Canonical();
    style.IndentWidth = 2;
    style.HonorClangFormatDirectives = true;
    const formatter::PreparedFormat prepared(style);
    const std::string source = "void f() {\n  if (a) { left(); } // keep\n  else { right(); }\n"
        "  do { tick(); } /* barrier */\n  while (a);\n" "  if (b) { left(); }\n#if FLAG\n#endif\n  else { right(); }\n"
        "// clang-format off\n  do { tick(); }\nwhile( a );\n" "// clang-format on\n}\n";
    const auto result = FormatInput(source, prepared);
    EXPECT_NE(result.Text.find("// keep\n  else"), std::string::npos);
    EXPECT_NE(result.Text.find("/* barrier */\n  while"), std::string::npos);
    EXPECT_NE(result.Text.find("#if FLAG\n#endif\n  else"), std::string::npos);
    EXPECT_NE(result.Text.find("  do { tick(); }\nwhile( a );\n"), std::string::npos);
    EXPECT_EQ(FormatInput(result.Text, prepared).Text, result.Text);
    const std::array<parser::SourceRange, 0> empty{};
    const auto scoped = FormatInput(source, prepared, {.EditScope = empty});
    EXPECT_EQ(scoped.Text, source);
    const auto gapBegin = source.find("}\n#if");
    const std::array<parser::SourceRange, 1> selected{{{gapBegin, gapBegin + 1}}};
    const auto partial = FormatInput(source, prepared, {.EditScope = selected});
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
        const std::string source = "void f(){if(ok)first_action();else second_action();"
            "do third_action();while(\nvery_long_condition_name);" "while(ok)fourth_action();}\n";
        const auto result = FormatInput(source, prepared);
        if (width == 10000) {
            EXPECT_NE(result.Text.find("first_action(); else"), std::string::npos);
            EXPECT_NE(result.Text.find("third_action(); while"), std::string::npos);
        } else {
            EXPECT_NE(result.Text.find("\n  else"), std::string::npos);
            EXPECT_NE(result.Text.find("\n  while"), std::string::npos);
        }
        EXPECT_EQ(FormatInput(result.Text, prepared).Text, result.Text);
    }
}

TEST(PublicPipeline, AmbiguousAssignmentRhsRespectsEditScope)
{
    auto style = Canonical();
    style.ColumnLimit = 60;
    style.BodyShape = formatter::BodyShape::Expanded;
    style.CallsPacking = formatter::ListPacking::VerticalWhenBroken;
    style.CallsFirstItem = formatter::FirstItemPlacement::NextLine;
    style.RemoveNullStatements = true;
    const formatter::PreparedFormat prepared(style);
    const std::string source = "void f() { External  *  pointer = "
        "Acquire(firstLongArgument, secondLongArgument, thirdLongArgument); " "; int tail = 0; ; }\n";
    const auto result = FormatInput(source, prepared);
    // Retain the null beside ambiguity, but still exercise normalization and
    // formatting of the independently known tail statement.
    EXPECT_NE(result.Text.find("External  *  pointer ="), std::string::npos);
    EXPECT_NE(result.Text.find("Acquire(\n"), std::string::npos);
    EXPECT_EQ(FormatInput(result.Text, prepared).Text, result.Text);
    EXPECT_EQ(FormatInput(source, prepared, {.EditScope = std::span<const parser::SourceRange>{}}).Text, source);
    const std::array immutable{parser::SourceRange{0, uint32_t(source.size())}};
    EXPECT_EQ(FormatInput(source, prepared, {.ImmutableRanges = immutable}).Text, source);
    const auto begin = uint32_t(source.find("Acquire"));
    const auto end = uint32_t(source.find(';'));
    const std::array selected{parser::SourceRange{begin, end}};
    const auto scoped = FormatInput(source, prepared, {.EditScope = selected});
    EXPECT_TRUE(scoped.Text.starts_with(source.substr(0, begin)));
    EXPECT_TRUE(scoped.Text.ends_with(source.substr(end)));
    EXPECT_NE(scoped.Text.find("Acquire(\n"), std::string::npos);
}

TEST(PublicPipeline, UnknownAnnotationsPreserveRecoveredText)
{
    auto style = Canonical();
    const formatter::PreparedFormat prepared(style);
    for (const std::string_view source :
        {
            "META(Key, const Item *, const Value *)\n" "const Item *Factory::build(int n) const { return nullptr; }\n"
                "int tail;\n",
            "BIND(Key, const Item *) TRACK(Group, const Item *) " "MAP(Key, const Item *, const Value *)\n"
                "const Item *Factory::build(int n) const { return nullptr; }\n" "int tail;\n",
            "NOTE(0) NOTE2(1) Vec load(int Type) { Type * value; }\n" "int tail;\n",
            "META(Key, const Item *, const Value *) [[maybe_unused]] "
                "static bool check(const Item *p) { return p; }\nint tail;\n",
            "using Type = int;\nstatic inline Vec NOTE(\"legacy\") " "load(int Type) { Type * value; }\nint tail;\n",
            "RAW(int n) & noexcept { return n; }\nint tail;\n",
            "RAW(int n) noexcept(true) { return n; }\nint tail;\n",
            "RAW(int n) override final { return n; }\nint tail;\n",
        })
    {
        SCOPED_TRACE(source);
        Input input(source);
        auto tree = parser::Parse(input.View());
        const auto result = formatter::Format(tree, prepared, {});
        EXPECT_EQ(FormatInput(result.Text, prepared).Text, result.Text);
        for (const auto* node = tree.Root().FirstChild; node; node = node->NextSibling) {
            if (node->Kind != parser::NodeKind::UnparsedSyntax) {
                continue;
            }
            EXPECT_NE(result.Text.find(tree.SourceText(node->Tokens)), std::string::npos);
        }
        EXPECT_EQ(FormatInput(source, prepared, {.EditScope = std::span<const parser::SourceRange>{}}).Text, source);
        const std::array<parser::SourceRange, 1> immutable = {{{0, source.size()}}};
        EXPECT_EQ(FormatInput(source, prepared, {.ImmutableRanges = immutable}).Text, source);
    }
}

TEST(PublicPipeline, AnnotationRecoverySurvivesStructuralCloneAndScopedTail)
{
    auto style = Canonical();
    style.BodyShape = formatter::BodyShape::Expanded;
    style.RemoveNullStatements = true;
    style.RemoveNullDeclarations = true;
    const formatter::PreparedFormat prepared(style);
    const std::string candidate = "static Vec NOTE(0) load(int Type) { Type * value; ; }";
    const std::string source = "using Type = int;\n" + candidate + "\n;\nint tail(){ ; return 1; ; }\n";
    const auto unrestricted = FormatInput(source, prepared);
    EXPECT_NE(unrestricted.Text.find(candidate), std::string::npos);
    EXPECT_EQ(FormatInput(unrestricted.Text, prepared).Text, unrestricted.Text);
    const std::array<parser::SourceRange, 1> selected = {{{source.find("int tail"), source.size()}}};
    const auto scoped = FormatInput(source, prepared, {.EditScope = selected});
    EXPECT_NE(scoped.Text, source);
    EXPECT_NE(scoped.Text.find(candidate), std::string::npos);
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
            "Vec NOTE(0) load(int n) requires Ready<int> && " "requires(int x) { x; }",
            "Vec NOTE(0) load(int n) requires (Ready<int>)",
            "Vec NOTE(0) load(int n) requires (Ready<int> && requires { 1; })",
        })
    {
        for (const bool functionTry : {false, true}) {
            const auto candidate = std::string(header) + (functionTry ? " try" : "") + " { return n; }" +
                (functionTry
                    ? " /* first */ catch(int Type) { Type * value; }" " /* between */ catch(...) { return 0; }"
                    : "");
            const auto source = candidate + "\nint tail(){return 1;}\n";
            const auto result = FormatInput(source, prepared);
            EXPECT_TRUE(result.Text.starts_with(candidate));
            EXPECT_NE(result.Text, source);
            EXPECT_EQ(FormatInput(result.Text, prepared).Text, result.Text);
            const std::array<parser::SourceRange, 1> handlerScope =
                {{{source.find(functionTry ? "catch" : "return n"), candidate.size()}}};
            const auto scoped = FormatInput(source, prepared, {.EditScope = handlerScope});
            EXPECT_EQ(scoped.Text, source);
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
        {"member{n}", "member(n)", "first{n}, second(n), third{}", "Base<int>{n}, member(n)"})
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
                const auto candidate = "NOTE(0) C(int n)" + std::string(suffix) +
                    std::string(functionTry ? " try : " : " : ") + std::string(initializers) +
                    " { Type * value; ; use(n); }" +
                    (functionTry
                        ? " /* first */ catch(int Type) { Type * value; ; }" " /* second */ catch(...) { ; recover(); }"
                        : "");
                const auto source =
                    "using Type = int;\nstruct C {\n" + candidate + "\nint member;\n};\n;\nint tail(){ ; return 1; }\n";
                SCOPED_TRACE(source);

                const auto result = FormatInput(source, prepared);
                const auto begin = result.Text.find(candidate);
                ASSERT_NE(begin, std::string::npos);
                EXPECT_EQ(FormatInput(result.Text, prepared).Text, result.Text);
                const std::array<parser::SourceRange, 1> scope = {
                    {{source.find(functionTry ? "catch" : "Type * value"), source.find(candidate) + candidate.size()}},
                };
                const auto scoped = FormatInput(source, prepared, {.EditScope = scope});
                EXPECT_EQ(scoped.Text, source);
                EXPECT_EQ(
                    FormatInput(source, prepared, {.EditScope = std::span<const parser::SourceRange>{}}).Text,
                    source);
                const std::array<parser::SourceRange, 1> immutable = {{{0, source.size()}}};
                EXPECT_EQ(FormatInput(source, prepared, {.ImmutableRanges = immutable}).Text, source);
            }
        }
    }
}

TEST(PublicPipeline, RecoveredConstraintOperandsPreserveTextAndScopedTail)
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
        const auto candidate = "Vec NOTE(0) load(int Type)" + std::string(suffix) + " { Type * value; ; }";
        const auto source = "using Type = int;\n" + candidate +
            "\n;\nType * kept;\nint tail(){ ; return 1; }\n" "bool later = a > b;\n";
        SCOPED_TRACE(source);
        const auto result = FormatInput(source, prepared);
        ASSERT_NE(result.Text.find(candidate), std::string::npos);
        EXPECT_EQ(FormatInput(result.Text, prepared).Text, result.Text);
        for (const auto offset : {source.find("requires"), source.find("Type * value"), source.find("int tail")}) {
            const std::array<parser::SourceRange, 1> scope = {
                {
                    {
                        offset,
                        offset == source.find("int tail") ? source.size() : source.find(candidate) + candidate.size(),
                    },
                },
            };
            const auto scoped = FormatInput(source, prepared, {.EditScope = scope});
            EXPECT_NE(scoped.Text.find(candidate), std::string::npos);
            if (offset != source.find("int tail")) {
                EXPECT_EQ(scoped.Text, source);
            } else {
                EXPECT_NE(scoped.Text, source);
            }
        }
        EXPECT_EQ(FormatInput(source, prepared, {.EditScope = std::span<const parser::SourceRange>{}}).Text, source);
        const std::array<parser::SourceRange, 1> immutable = {{{0, source.size()}}};
        EXPECT_EQ(FormatInput(source, prepared, {.ImmutableRanges = immutable}).Text, source);
    }
}

TEST(PublicPipeline, StandaloneCommentCountsRespectScopedEdges)
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
    EXPECT_EQ(FormatInput(result.Text, prepared).Text, result.Text);
    Input input(source);
    auto tree = parser::Parse(input.View(), [] (parser::ParseError) {});
    EXPECT_EQ(formatter::Format(tree, prepared).Text, result.Text);

    const auto comment = parser::SourceOffset(source.find("// note"));
    const std::array selected{parser::SourceRange{0, comment}};
    formatter::FormatRequest request{};
    request.EditScope = selected;
    const auto limited = FormatInput(source, prepared, request);
    EXPECT_EQ(limited.Text, source);
    const std::array immutable{parser::SourceRange{comment - 1, parser::SourceOffset(source.find("int x"))}};
    request.EditScope = {};
    request.ImmutableRanges = immutable;
    const auto protectedResult = FormatInput(source, prepared, request);
    EXPECT_NE(protectedResult.Text.find("{\n// note\nint x;"), std::string::npos);

    style.MaxEmptyLines = 0;
    EXPECT_THROW((void)formatter::PreparedFormat(style), std::invalid_argument);
}

TEST(PublicPipeline, FinalNamespaceWidthDiagnosticsGolden)
{
    const test::golden::Fixture fixture{
        std::filesystem::path(__FILE__).parent_path() / "testdata/public-pipeline",
        "comments",
    };
    const formatter::PreparedFormat prepared(formatter::LoadConfigFile(fixture.Named(".hit-fmt")).FormattingStyle);
    const auto result = FormatInput(fixture.InputNamed(".input.cpp"), prepared);
    std::string diagnostics;
    for (const auto& diagnostic : result.Diagnostics) {
        diagnostics += std::to_string(diagnostic.Position.Line) + ":" + std::to_string(diagnostic.Position.Column) +
            " " + diagnostic.Message + "\n";
    }
    fixture.Expect(diagnostics, ".diagnostics.txt");
}

TEST(PublicPipeline, DeclarationFallbackRequiresRebuiltOwnerAndEditableGap)
{
    const std::string source = "template<class T> struct Box {};\n" "Box<int>  make(){return {};}\n"
        "Box<int>  flat();\n" "Box<int>\nOwner<int>::method(){return {};}\n";
    auto style = formatter::Style{};
    style.DeclarationHeadersLayout = formatter::CategoryLayout::Canonical;
    const formatter::PreparedFormat prepared(style);
    const auto gapBegin = uint32_t(source.find("  make"));
    const auto gapEnd = gapBegin + 2;
    const auto result = FormatInput(source, prepared);
    EXPECT_NE(result.Text.find("Box<int> make()"), std::string::npos);
    EXPECT_NE(result.Text.find("Box<int>  flat()"), std::string::npos);
    EXPECT_NE(result.Text.find("Box<int> Owner<int>::method()"), std::string::npos);
    EXPECT_EQ(FormatInput(result.Text, prepared).Text, result.Text);

    const std::array<parser::SourceRange, 1> selected{{{gapBegin, gapEnd}}};
    const auto scoped = FormatInput(source, prepared, {.EditScope = selected});
    EXPECT_NE(scoped.Text.find("Box<int> make()"), std::string::npos);
    const std::array<parser::SourceRange, 1> partial{{{gapBegin, gapEnd - 1}}};
    const auto unownedEnd = FormatInput(source, prepared, {.EditScope = partial});
    EXPECT_NE(unownedEnd.Text.find("Box<int>  make()"), std::string::npos);
    const auto protectedGap = FormatInput(source, prepared, {.ImmutableRanges = selected});
    EXPECT_NE(protectedGap.Text.find("Box<int>  make()"), std::string::npos);

    const auto qualifiedGap = uint32_t(source.find("\nOwner"));
    const auto qualifiedBegin = uint32_t(source.rfind("Box<int>", qualifiedGap));
    const auto qualifiedEnd = uint32_t(source.size());
    const std::array qualifiedScope{parser::SourceRange{qualifiedBegin, qualifiedEnd}};
    const std::array immutableQualifiedGap{parser::SourceRange{qualifiedGap, qualifiedGap + 1}};
    const auto qualified = FormatInput(source, prepared, {.EditScope = qualifiedScope});
    EXPECT_NE(qualified.Text.find("Box<int> Owner<int>::method()"), std::string::npos) << qualified.Text;
    const auto qualifiedProtected = FormatInput(source, prepared, {.ImmutableRanges = immutableQualifiedGap});
    EXPECT_NE(qualifiedProtected.Text.find("Box<int>\nOwner<int>::method()"), std::string::npos);

    style.DeclarationHeadersLayout = formatter::CategoryLayout::Preserve;
    style.Layout = formatter::LayoutMode::Canonical;
    const auto preserved = FormatInput(source, formatter::PreparedFormat(style));
    EXPECT_NE(preserved.Text.find("Box<int>  make()"), std::string::npos);
    EXPECT_NE(preserved.Text.find("Box<int>\nOwner<int>::method()"), std::string::npos);
}

TEST(PublicPipeline, DeclarationHeaderBreaksRespectWidthAndByteAuthority)
{
    const auto directory = std::filesystem::path(FORMATTER_TESTDATA_DIR) / "../declaration-headers";
    const auto source = test::golden::ReadFile(directory / "input.cpp");
    for (const auto* profile : {"wrap", "narrow", "wide"}) {
        SCOPED_TRACE(profile);
        auto style = formatter::LoadConfigFile(directory / (std::string(profile) + ".hit-fmt")).FormattingStyle;
        style.WidthDiagnostics = formatter::WidthDiagnosticPolicy::Report;
        const formatter::PreparedFormat prepared(style);
        const auto result = FormatInput(source, prepared);
        EXPECT_TRUE(result.Diagnostics.empty());
        EXPECT_EQ(FormatInput(result.Text, prepared).Text, result.Text);
    }

    auto style = formatter::LoadConfigFile(directory / "wrap.hit-fmt").FormattingStyle;
    style.WidthDiagnostics = formatter::WidthDiagnosticPolicy::Report;
    const formatter::PreparedFormat prepared(style);
    const auto firstLine = source.substr(0, source.find('\n') + 1);
    const std::array immutable{parser::SourceRange{0, uint32_t(firstLine.size())}};
    const auto protectedResult = FormatInput(firstLine, prepared, {.ImmutableRanges = immutable});
    EXPECT_EQ(protectedResult.Text, firstLine);
    ASSERT_EQ(protectedResult.Diagnostics.size(), 1u);
    EXPECT_EQ(protectedResult.Diagnostics[0].Kind, formatter::LayoutDiagnosticKind::LineWidth);

    const auto gap = uint32_t(firstLine.find("> ReadFirstResult") + 1);
    const std::array selected{parser::SourceRange{gap, gap + 1}};
    const auto scoped = FormatInput(firstLine, prepared, {.EditScope = selected});
    EXPECT_NE(scoped.Text.find(">\nReadFirstResult"), std::string::npos);
    EXPECT_TRUE(scoped.Diagnostics.empty());

    style.DeclarationHeadersLayout = formatter::CategoryLayout::Preserve;
    const auto preserved = FormatInput(firstLine, formatter::PreparedFormat(style));
    EXPECT_NE(preserved.Text.find("> ReadFirstResult"), std::string::npos);
}

TEST(PublicPipeline, LambdaLineBasesKeepSourceAuthority)
{
    const auto directory = std::filesystem::path(FORMATTER_TESTDATA_DIR) / "../lambda-captures";
    const auto input = test::golden::ReadFile(directory / "profile.input.cpp");
    const auto style =
        formatter::LoadConfigFile(directory / "../../../../hit-fmt/examples/ytsaurus.hit-fmt").FormattingStyle;
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
        const auto result = FormatInput(source, prepared, {});
        EXPECT_TRUE(result.Diagnostics.empty());
        EXPECT_EQ(FormatInput(result.Text, prepared).Text, result.Text);
        EXPECT_NE(result.Text.find("\n            auto guard ="), std::string::npos);
        const auto begin = uint32_t(source.find("class Adapter"));
        const std::array scope{parser::SourceRange{begin, uint32_t(source.size())}};
        const auto scoped = FormatInput(source, prepared, {.EditScope = scope});
        EXPECT_NE(scoped.Text.find("\n            auto guard ="), std::string::npos);
        const auto guardBegin = uint32_t(source.find("                    InvokerGuard", begin));
        const auto guardEnd = uint32_t(source.find("                    callback();", guardBegin));
        const std::array immutable{parser::SourceRange{guardBegin, guardEnd}};
        const auto frozen = FormatInput(source, prepared, {.ImmutableRanges = immutable});
        EXPECT_NE(frozen.Text.find(source.substr(guardBegin, guardEnd - guardBegin)), std::string::npos);
    }
}

TEST(PublicPipeline, ConditionalFragmentsKeepSourceAuthority)
{
    const auto directory = std::filesystem::path(FORMATTER_TESTDATA_DIR) / "../preprocessing-fragments";
    const auto source = test::golden::ReadFile(directory / "canonical.input.cpp");
    const auto style =
        formatter::LoadConfigFile(directory / "../../../../hit-fmt/examples/ytsaurus.hit-fmt").FormattingStyle;
    const formatter::PreparedFormat prepared(style);
    const auto begin = uint32_t(source.find("void Arguments()"));
    const auto end = uint32_t(source.find("void Parameters(", begin));
    const std::array scope{parser::SourceRange{begin, end}};
    const auto scoped = FormatInput(source, prepared, {.EditScope = scope});
    EXPECT_NE(scoped.Text.find("left + delta"), std::string::npos);

    const auto immutableBegin = uint32_t(source.find("    Consume(first,\n"));
    const auto immutableEnd = uint32_t(source.find("    Consume(\n", immutableBegin));
    const std::array immutable{parser::SourceRange{immutableBegin, immutableEnd}};
    const auto protectedResult = FormatInput(source, prepared, {.ImmutableRanges = immutable});
    EXPECT_NE(
        protectedResult.Text.find(source.substr(immutableBegin, immutableEnd - immutableBegin)),
        std::string::npos);
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
    const std::string source = "#include \"z.h\"\n#include \"a.h\"\n;\nvoid f(){ ; if(ok) " "invoke(a,b); }\n";
    auto result = FormatInput(source, prepared);
    EXPECT_TRUE(result.Text.starts_with("#include \"a.h\"\n#include \"z.h\"\n"));
    EXPECT_NE(result.Text.find("if (ok) {"), std::string::npos);
    EXPECT_EQ(FormatInput(result.Text, prepared).Text, result.Text);
    const std::array<parser::SourceRange, 1> excluded{{{0, uint32_t(source.find(';'))}}};
    auto constrained = FormatInput(source, prepared, {.ImmutableRanges = excluded});
    EXPECT_TRUE(constrained.Text.starts_with(source.substr(0, excluded[0].End)));
    EXPECT_NE(constrained.Text.find("if (ok) {"), std::string::npos);
}

TEST(PublicPipeline, ProtectedBytesSurviveBothBranchesAndCRLF)
{
    const std::string source = "// hit-fmt off\r\nvoid locked( ){  invoke( x,y ); }\r\n// " "hit-fmt on\r\n"
        "void f(){ KEEP(  x, y ); if(ok) invoke(a,b); }\r\n";
    for (bool structural : {false, true}) {
        auto style = Canonical();
        style.AddMissingControlBraces = structural;
        style.WhitespaceSensitiveMacros = {"KEEP"};
        const formatter::PreparedFormat prepared(style);
        auto result = FormatInput(source, prepared);
        EXPECT_TRUE(result.Text.starts_with(source.substr(0, source.find("void f"))));
        EXPECT_NE(result.Text.find("KEEP(  x, y )"), std::string::npos);
        for (size_t i = 0; i < result.Text.size(); ++i) {
            if (result.Text[i] == '\n') {
                ASSERT_GT(i, 0u);
                EXPECT_EQ(result.Text[i - 1], '\r');
            }
        }
        EXPECT_EQ(FormatInput(result.Text, prepared).Text, result.Text);
    }
}

TEST(PublicPipeline, EnumeratorAssignmentExplicitSpacing)
{
    const std::string source = "enum class Mode { First=1, Second =2 };\n";
    for (const auto mode : {formatter::LayoutMode::Preserve, formatter::LayoutMode::Canonical}) {
        auto style = Canonical();
        style.Layout = mode;
        for (const auto policy : {formatter::GapPolicy::Space, formatter::GapPolicy::GeneratedSpace}) {
            style.AssignmentOperatorSpacing = policy;
            const formatter::PreparedFormat prepared(style);
            const auto result = FormatInput(source, prepared);
            if (policy == formatter::GapPolicy::GeneratedSpace && mode == formatter::LayoutMode::Preserve) {
                EXPECT_EQ(result.Text, source);
            } else {
                EXPECT_NE(result.Text.find("First = 1"), std::string::npos) << result.Text;
                EXPECT_NE(result.Text.find("Second = 2"), std::string::npos) << result.Text;
            }
            EXPECT_EQ(FormatInput(result.Text, prepared).Text, result.Text);
        }
    }
}

TEST(PublicPipeline, EnumeratorAssignmentRespectsScopeAndProtection)
{
    const auto directory =
        std::filesystem::path(__FILE__).parent_path() / "testdata/consolidated-pipeline/enumerator-assignment";
    const auto source = test::golden::ReadFile(directory / "input.cpp");
    const formatter::PreparedFormat prepared(formatter::LoadConfigFile(directory / "space.hit-fmt").FormattingStyle);
    const auto second = uint32_t(source.find("Second"));
    const std::array<parser::SourceRange, 1> selected{{{0, second}}};
    const auto scoped = FormatInput(source, prepared, {.EditScope = selected});
    EXPECT_EQ(scoped.Text, "enum Mode { First = 1, Second=2 };\n");
    const std::array<parser::SourceRange, 1> immutable{{{0, second}}};
    const auto locked = FormatInput(source, prepared, {.ImmutableRanges = immutable});
    EXPECT_EQ(locked.Text, "enum Mode { First  =  1, Second = 2 };\n");
    const auto gap = uint32_t(source.find("  ="));
    const std::array<parser::SourceRange, 1> partial{{{gap, gap + 1}}};
    const auto denied = FormatInput(source, prepared, {.EditScope = partial});
    EXPECT_EQ(denied.Text, source);
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
        const std::string source = policy == formatter::NamespaceComments::AddMissing
            ? "namespace Name { int value; }\nint " "following;\n#define RAW(x) x " "## x\n"
            : "namespace Name { int value; } // namespace Wrong\nint " "following;\n#define RAW(x) x ## x\n";
        auto result = FormatInput(source, prepared);
        EXPECT_NE(result.Text.find("int following;"), std::string::npos);
        if (policy == formatter::NamespaceComments::Remove) {
            EXPECT_EQ(result.Text.find("// namespace"), std::string::npos);
        } else {
            EXPECT_NE(result.Text.find("}   // namespace Name"), std::string::npos);
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
        EXPECT_EQ(FormatInput(windows.Text, prepared).Text, windows.Text);
        EXPECT_NE(windows.Text.find("\r\nint following;"), std::string::npos);
    }
    auto style = Canonical();
    style.NamespaceComments = formatter::NamespaceComments::AddMissing;
    const formatter::PreparedFormat prepared(style);
    auto result = FormatInput("namespace Name {} int following;\n", prepared);
    // Canonical bodies may place the following declaration safely on a new
    // line.
    const auto comment = result.Text.find("// namespace");
    if (comment != std::string::npos) {
        EXPECT_LT(result.Text.find('\n', comment), result.Text.find("int following"));
    }
}

TEST(PublicPipeline, ImmutableRangesConstrainNamespaceAndStructuralPlanning)
{
    auto style = Canonical();
    style.NamespaceComments = formatter::NamespaceComments::AddMissing;
    style.AddMissingControlBraces = true;
    const formatter::PreparedFormat prepared(style);
    const std::string source = "namespace Name { void f(){ if(ok)invoke(a,b); } }\nint tail;\n";
    const std::array<parser::SourceRange, 1> immutable{{{0, uint32_t(source.find('\n') + 1)}}};
    const auto result = FormatInput(source, prepared, {.ImmutableRanges = immutable});
    EXPECT_TRUE(result.Text.starts_with(source.substr(0, immutable[0].End)));
    EXPECT_EQ(result.StructuralDiagnostics.size(), 2u);
    EXPECT_EQ(
        std::count_if(
            result.StructuralDiagnostics.begin(),
            result.StructuralDiagnostics.end(),
            [] (const auto& diagnostic) {
                return diagnostic.Action == formatter::StructuralAction::NamespaceComments;
            }),
        1);
    EXPECT_EQ(
        std::count_if(
            result.StructuralDiagnostics.begin(),
            result.StructuralDiagnostics.end(),
            [] (const auto& diagnostic) {
                return diagnostic.Action == formatter::StructuralAction::AddMissingControlBraces;
            }),
        1);
    for (const auto& diagnostic : result.StructuralDiagnostics) {
        EXPECT_EQ(diagnostic.Reason, formatter::StructuralSkip::Protected);
    }

    // Comment attachment cannot veto removal of a redundant namespace suffix,
    // but protection of the semicolon atoms still can.
    style.RemoveNullDeclarations = true;
    style.NamespaceComments = formatter::NamespaceComments::Preserve;
    const formatter::PreparedFormat removal(style);
    const std::string suffix = "namespace Name {};; // keep\nint tail;\n";
    const auto begin = uint32_t(suffix.find(";;"));
    for (const bool protectSuffix : {false, true}) {
        const auto comment = uint32_t(suffix.find("//"));
        const std::array protectedRange{
            protectSuffix ? parser::SourceRange{begin, begin + 2} : parser::SourceRange{comment, comment + 7},
        };
        const auto result = FormatInput(suffix, removal, {.ImmutableRanges = protectedRange});
        EXPECT_NE(result.Text.find("// keep\n"), std::string::npos);
    }
}

TEST(PublicPipeline, InvalidPreparedRequestsAndActualEmittedWidthDiagnostics)
{
    auto style = Canonical();
    style.ColumnLimit = 0;
    style.WidthDiagnostics = formatter::WidthDiagnosticPolicy::Report;
    const auto unlimited = FormatInput("int extremely_long_variable_name;\n", formatter::PreparedFormat{style});
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
    const auto result = FormatInput("// hit-fmt off\nint unavoidable_long_name;\n// hit-fmt on\n", prepared);
    ASSERT_FALSE(result.Diagnostics.empty());
    EXPECT_EQ(result.Diagnostics[1].Position.Line, 2u);
    EXPECT_EQ(result.Diagnostics[1].Kind, formatter::LayoutDiagnosticKind::LineWidth);
    const std::array<parser::SourceRange, 1> bad{{{1, 1000}}};
    EXPECT_THROW(FormatInput("int x;", prepared, {.ImmutableRanges = bad}), std::invalid_argument);
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
    EXPECT_EQ(result.StructuralDiagnostics.back().Reason, formatter::StructuralSkip::FollowingText);
    style = Canonical();
    style.AddMissingControlBraces = true;
    const std::string disabled = "// clang-format off\nvoid f(){ if(ok)invoke(a,b); }\n// " "clang-format on\n";
    const auto protectedResult = FormatInput(disabled, formatter::PreparedFormat(style));
    EXPECT_EQ(protectedResult.Text, disabled);
    style.HonorClangFormatDirectives = false;
    EXPECT_NE(FormatInput(disabled, formatter::PreparedFormat(style)).Text, disabled);
}

TEST(PublicPipeline, DecltypeOperandsRespectEditScope)
{
    const auto directory = std::filesystem::path(__FILE__).parent_path() / "testdata/public-pipeline";
    for (const auto* name : {"declaration-operands", "declaration-operands-recovery"}) {
        SCOPED_TRACE(name);
        const test::golden::Fixture fixture{directory, name};
        const auto source = fixture.InputNamed(".input.cpp");
        const formatter::PreparedFormat prepared(Canonical());
        const auto result = FormatInput(source, prepared);
        EXPECT_EQ(FormatInput(result.Text, prepared).Text, result.Text);
        const std::span<const parser::SourceRange> empty;
        const auto denied = FormatInput(source, prepared, {.EditScope = empty});
        EXPECT_EQ(denied.Text, source);
        if (std::string_view(name).ends_with("recovery")) {
            EXPECT_EQ(result.Text, source);
            const std::array<parser::SourceRange, 1> immutable{{{0, static_cast<parser::SourceOffset>(source.size())}}};
            const auto locked = FormatInput(source, prepared, {.ImmutableRanges = immutable});
            EXPECT_EQ(locked.Text, source);
        }
    }
}

TEST(PublicPipeline, EmptyAndRecoveryInputRemainLocalWithoutFallback)
{
    const formatter::PreparedFormat prepared(Canonical());
    const auto empty = FormatInput("", prepared);
    EXPECT_TRUE(empty.Text.empty());
    const std::string source = "void broken(){ invoke(a,,b); }\nvoid safe(){ invoke(x,y); }\n";
    const auto result = FormatInput(source, prepared);
    EXPECT_NE(result.Text.find("invoke(a,,b)"), std::string::npos);
    EXPECT_NE(result.Text.find("invoke(x, y)"), std::string::npos);
    EXPECT_EQ(FormatInput(result.Text, prepared).Text, result.Text);
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
        "void selected(){ if(ok)invoke(a,b); KEEP(  x,y ); }\n" "void locked( ){  invoke( x,y ); }\n";
    const auto tail = uint32_t(source.find("void locked"));
    const auto sensitive = uint32_t(source.find("KEEP("));
    const std::array<parser::SourceRange, 1> scope{{{0, tail}}};
    const std::array<parser::SourceRange, 1> immutable{{{sensitive, sensitive + 13}}};
    const auto result = FormatInput(
        source,
        prepared,
        {
            .ImmutableRanges = immutable,
            .EditScope =
            scope,
        });
    EXPECT_TRUE(result.Text.starts_with("#include \"a.h\"\n#include \"zzzz.h\"\n"));
    EXPECT_TRUE(result.Text.ends_with(source.substr(tail)));
    EXPECT_NE(result.Text.find("KEEP(  x,y )"), std::string::npos);
    // A partly selected include group must still skip atomically; neighboring
    // directive/source associations cannot authorize the other include.
    const std::array<parser::SourceRange, 1> partial{{{0, uint32_t(source.find('\n') + 1)}}};
    EXPECT_EQ(FormatInput(source, prepared, {.EditScope = partial}).Text, source);
}

TEST(PublicPipeline, IncludeRegroupingOwnsBlankLinesAndPreservesMovedIdentities)
{
    const auto directory = std::filesystem::path(__FILE__).parent_path() / "testdata/include-order";
    for (const auto name : {"relative", "groups", "rules"}) {
        const bool groups = std::string_view(name) == "groups";
        const bool rules = std::string_view(name) == "rules";
        const auto source = test::golden::ReadFile(
            directory / (rules ? "rule-groups.input.cpp" : groups ? "groups.input.cpp" : "relative.input.cpp"));
        auto style = formatter::LoadConfigFile(directory /
            (rules
                ? "rules-nearest.hit-fmt"
                : groups ? "groups.hit-fmt" : "directory-relative.hit-fmt")).FormattingStyle;
        formatter::FormatResult owned;
        {
            const formatter::PreparedFormat prepared(style);
            style.IncludeGrouping = formatter::IncludeGrouping::Preserve;
            style.IncludeGroups.clear();
            style.IncludeStandardHeaders = formatter::IncludeStandardHeaders::Preserve;
            std::string path = rules
                ? "yt/yt/core/rpc/runner.cpp"
                : groups ? "library/unit/widget.cpp" : "/project/engine/widget.cpp";
            owned = FormatInput(source, prepared, {.InputFilePath = path});
            Input input(source);
            auto tree = parser::Parse(input.View(), [] (parser::ParseError) {});
            EXPECT_EQ(owned.Text, formatter::Format(tree, prepared, {.InputFilePath = path}).Text);
            EXPECT_NE(owned.Text, source);
            const auto repeated = FormatInput(owned.Text, prepared, {.InputFilePath = path});
            EXPECT_EQ(repeated.Text, owned.Text);
            const auto blank = parser::SourceOffset(source.find("\n\n") + 1);
            // A one-byte hole in blank-line authority blocks the entire move.
            const std::array<parser::SourceRange, 2> scope{
                {{0, blank}, {blank + 1, parser::SourceOffset(source.size())}},
            };
            const auto partial = FormatInput(source, prepared, {.EditScope = scope, .InputFilePath = path});
            EXPECT_EQ(partial.Text, source);
            const std::array<parser::SourceRange, 1> immutable{{{blank, blank + 1}}};
            const auto protectedResult =
                FormatInput(source, prepared, {.ImmutableRanges = immutable, .InputFilePath = path});
            EXPECT_EQ(protectedResult.Text, source);
            path.assign(path.size(), '?');
        }
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
            "#include \"z.h\"\r\n\r\n#include \"a.h\"\r\n" "#include \"child/a.h\"\r\n",
            "#include \"z.h\"\n\r\n#include \"a.h\"\n",
            "#include \"z.h\"\n\n#include \"a.h\"",
            "#include \"z.h\"\n\r#include \"a.h\"\n",
        })
    {
        const auto result = FormatInput(source, prepared);
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
    const auto result = FormatInput(source, prepared, {.EditScope = deleted});
    EXPECT_EQ(result.Text, "  int x;\n");
    const std::string control = "void f(){ while(ok); int x; }\n";
    const auto semicolon = uint32_t(control.find(';'));
    const std::array<parser::SourceRange, 1> endpoint{{{semicolon, semicolon + 1}}};
    const std::array<parser::SourceRange, 1> replacement{{{semicolon, semicolon + 2}}};
    // Replacing the semicolon needs its bytes, not the insertion point at End.
    for (const auto& scope : {endpoint, replacement}) {
        const auto replaced = FormatInput(control, prepared, {.EditScope = scope});
        EXPECT_NE(replaced.Text.find("{}"), std::string::npos);
        EXPECT_TRUE(replaced.Text.ends_with("int x; }\n"));
    }
    const std::array<parser::SourceRange, 1> tail{{{semicolon + 1, uint32_t(control.size())}}};
    const auto protectedTail = FormatInput(control, prepared, {.ImmutableRanges = tail});
    EXPECT_NE(protectedTail.Text.find("{}"), std::string::npos);
    EXPECT_EQ(FormatInput(control, prepared, {.ImmutableRanges = endpoint}).Text, control);
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
    EXPECT_EQ(FormatInput(source, prepared, {.EditScope = unownedEnd}).Text, source);
    const std::array<parser::SourceRange, 1> ownedEnd{{{begin, end + 1}}};
    const auto result = FormatInput(source, prepared, {.EditScope = ownedEnd});
    EXPECT_EQ(result.Text, "void f(){ if(ok) {invoke();} }\n");
    EXPECT_TRUE(result.Text.ends_with('\n'));
    // Existing function braces must not satisfy the positive endpoint check.
    style.AddMissingControlBraces = false;
    const auto skipped = FormatInput(source, formatter::PreparedFormat(style), {.EditScope = ownedEnd});
    EXPECT_EQ(skipped.Text, source);
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
            const std::string source = bare + (existing ? " // namespace Wrong\n" : "\n");
            const auto closeEnd = uint32_t(bare.size());
            const std::array<parser::SourceRange, 1> selected{{{0, uint32_t(source.size())}}};
            const auto result = FormatInput(source, prepared, {.EditScope = selected});
            if (policy == formatter::NamespaceComments::Remove) {
                EXPECT_EQ(result.Text.find("// namespace"), std::string::npos);
            } else {
                EXPECT_NE(
                    result.Text.find("// namespace Name"),
                    std::
                    string::npos);
            }
            const std::array<parser::SourceRange, 1> excludedPoint{{{0, closeEnd}}};
            const auto denied = FormatInput(source, prepared, {.EditScope = excludedPoint});
            EXPECT_EQ(denied.Text, normalize ? source.substr(1) : source);
            // Existing replacement windows cannot be authorized by their
            // close-brace point alone, even after a preceding deletion.
            if (existing) {
                const std::array<parser::SourceRange, 1> cutComment{{{0, uint32_t(source.find("Wrong"))}}};
                EXPECT_NE(
                    FormatInput(source, prepared, {.EditScope = cutComment}).Text.find("Wrong"),
                    std::string::npos);
            } else {
                const std::array<parser::SourceRange, 1> fullEOF{{{0, uint32_t(bare.size())}}};
                const auto eof = FormatInput(bare, prepared, {.EditScope = fullEOF});
                EXPECT_EQ(eof.Text.find("// namespace"), std::string::npos);
                const std::string withComment = bare + " // namespace Wrong\n";
                const std::array<parser::SourceRange, 1> wholeComment{{{0, uint32_t(withComment.size())}}};
                const auto retained = FormatInput(withComment, prepared, {.EditScope = wholeComment});
                EXPECT_EQ(retained.Text, normalize ? withComment.substr(1) : withComment);
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
        "void locked( ){\r\n  invoke( x,y );  \r\n\t\n}\r\n" "void selected(){ invoke(a,b); }\n";
    const auto firstFunction = uint32_t(source.find("void locked"));
    const auto horizontal = uint32_t(source.find("  \r\n"));
    const auto selectedFunction = uint32_t(source.find("void selected"));
    const std::array<parser::SourceRange, 3> selected{
        {{0, firstFunction}, {horizontal, horizontal + 2}, {selectedFunction, uint32_t(source.size())}},
    };
    const auto result = FormatInput(source, prepared, {.EditScope = selected});
    EXPECT_TRUE(result.Text.starts_with("#include \"aaa.h\"\r\n#include \"z.h\"\r\n"));
    EXPECT_NE(result.Text.find("invoke(a, b)"), std::string::npos);
    // The selected trim run may change; excluded suffix/prefix bytes must
    // stay exact.
    EXPECT_NE(result.Text.find("\r\n\t\n}\r\n"), std::string::npos);
}

TEST(PublicPipeline, EditScopeInvalidRangesAndProtectionRemainStronger)
{
    const std::string source =
        "// hit-fmt off\nvoid locked(){ if(ok)invoke(a,b); }\n" "// hit-fmt on\nvoid safe(){ if(ok)invoke(x,y); }\n";
    auto style = Canonical();
    style.AddMissingControlBraces = true;
    const formatter::PreparedFormat prepared(style);
    const std::array<parser::SourceRange, 1> full{{{0, uint32_t(source.size())}}};
    const auto result = FormatInput(source, prepared, {.EditScope = full});
    EXPECT_TRUE(result.Text.starts_with(source.substr(0, source.find("void safe"))));
    EXPECT_NE(result.Text.find("invoke(x, y)"), std::string::npos);
    const auto safe = uint32_t(source.find("void safe"));
    for (const std::vector<parser::SourceRange> invalid :
        {
            std::vector<parser::SourceRange>{{0, 0}},
            {{2, 1}},
            {{0, uint32_t(source.size() + 1)}},
            {{4, 6}, {1, 3}},
            {{0, 5}, {4, 7}},
        })
    {
        EXPECT_THROW(FormatInput(source, prepared, {.EditScope = invalid}), std::invalid_argument);
    }
}

TEST(PublicPipeline, RequiresOrdinaryReturnBoundaryRespectsScope)
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
            "template<class T> requires (requires(T v) { v.touch(); })\n" + std::string(declaration) + "\n";
        SCOPED_TRACE(source);
        const auto function = uint32_t(source.find(declaration));
        const std::array<parser::SourceRange, 1> selected{{{function, uint32_t(source.size())}}};
        const auto result = FormatInput(source, prepared, {.EditScope = selected});
        EXPECT_EQ(result.Text.substr(0, function), source.substr(0, function));
        const std::array<parser::SourceRange, 1> selectedOutput{{{function, uint32_t(result.Text.size())}}};
        EXPECT_EQ(FormatInput(result.Text, prepared, {.EditScope = selectedOutput}).Text, result.Text);
        const auto unrestricted = FormatInput(source, prepared);
        EXPECT_EQ(FormatInput(unrestricted.Text, prepared).Text, unrestricted.Text);
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
            "HEADER_BLOCK(Thing, test, const Item *, input) " "{ if(input){return input;} return 0; }",
            "OPAQUE_BODY(Label, char*(Box<char*>, Box<bool>)) " "{ int value=1+2; return value; };",
            "HEADER_BLOCK(Type) { Type * item; }",
            "HEADER_BLOCK(Tag) { using Hidden=int; int member; }",
        })
    {
        const std::string prefix = "struct Type {};\n";
        const auto source = prefix + candidate + "\nint following(){int x=1;return x;}\n";
        SCOPED_TRACE(source);
        const auto start = parser::SourceOffset(prefix.size());
        const auto end = start + parser::SourceOffset(std::string_view(candidate).size());
        Input input(source);
        const auto tree = parser::Parse(input.View(), [] (parser::ParseError) {});
        const auto result = formatter::Format(tree, prepared, {});
        const auto limited = formatter::Format(tree, prepared, {.EditScope = std::span<const parser::SourceRange>{}});
        EXPECT_EQ(limited.Text, source);
        const std::array selected{parser::SourceRange{end + 1, parser::SourceOffset(source.size())}};
        const auto scoped = formatter::Format(tree, prepared, {.EditScope = selected});
        EXPECT_EQ(scoped.Text.substr(0, end + 1), source.substr(0, end + 1));
        EXPECT_NE(scoped.Text.find("x = 1"), std::string::npos);
        const std::array immutable{parser::SourceRange{end + 1, parser::SourceOffset(source.size())}};
        const auto frozen = formatter::Format(tree, prepared, {.ImmutableRanges = immutable});
        EXPECT_NE(frozen.Text.find("int following(){int x=1;return x;}"), std::string::npos);
        const auto outputStart = result.Text.find(candidate);
        ASSERT_NE(outputStart, std::string::npos);
        EXPECT_NE(result.Text.find("x = 1"), std::string::npos);
        EXPECT_EQ(FormatInput(result.Text, prepared).Text, result.Text);
    }
}

TEST(PublicPipeline, RequiresRecoveryPreservesInvalidBytesOutsideScope)
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
            const auto source = "template<class T> requires requires(T v) { v.touch(); } " + std::string(terminal) +
                "\n" + std::string(declaration) + "\n";
            SCOPED_TRACE(source);
            const auto function = uint32_t(source.find(declaration));
            const std::array<parser::SourceRange, 1> selected{{{function, uint32_t(source.size())}}};
            const auto result = FormatInput(source, prepared, {.EditScope = selected});
            EXPECT_EQ(result.Text.substr(0, function), source.substr(0, function));
        }
    }
}

TEST(PublicPipeline, OpaqueRecordOwnerPreservesHeadAndFormatsSafeBodies)
{
    for (const auto head :
        {"API Parcel", "api [[nodiscard]] Parcel", "API Owner::Parcel", "API Parcel + broken", "API Owner::"})
    {
        const auto source = "struct " + std::string(head) +
            " { Parcel(Item &p) : Base(p) { invoke(a,b); } " "void (*callback)(int); int (*factory())(double); };\n";
        SCOPED_TRACE(source);
        auto style = Canonical();
        style.CommaSpaceAfter = formatter::GapPolicy::Space;
        const formatter::PreparedFormat prepared(style);
        const auto result = FormatInput(source, prepared);
        EXPECT_TRUE(result.Text.starts_with("struct " + std::string(head)));
        EXPECT_NE(result.Text.find("invoke(a, b)"), std::string::npos);
        EXPECT_EQ(FormatInput(result.Text, prepared).Text, result.Text);
        const auto point = parser::SourceOffset(source.find("a,b") + 2);
        const std::array<parser::SourceRange, 1> selected{{{point, point + 1}}};
        const auto scoped = FormatInput(source, prepared, {.EditScope = selected});
        auto expected = source;
        expected.insert(point, " ");
        EXPECT_EQ(scoped.Text, expected);
        const std::array<parser::SourceRange, 1> immutable{{{point - 1, point + 1}}};
        const auto frozen = FormatInput(source, prepared, {.ImmutableRanges = immutable, .EditScope = selected});
        EXPECT_EQ(frozen.Text, source);
    }
}

TEST(PublicPipeline, ConstructorInitializerLambdasRespectProtection)
{
    const std::string_view sources[] = {
        "struct Task { Task() : callback([this] { stop(); }, " "[this] { start(); }) {} };\n",
        "struct Task { Task() noexcept : first([] { return 1; }), " "second([]() noexcept { return 2; }) {} };\n",
        "struct Task { Task() : first(0), callback(wrap(" "[id = 1](int* value) { consume(value, id); })) {} };\n",
        "struct Task { Task() try : first([] { return 1; }), " "second([] { return 2; }) {} catch (...) {} };\n",
    };
    const formatter::PreparedFormat prepared(Canonical());
    for (const auto source : sources) {
        SCOPED_TRACE(source);
        formatter::FormatResult result;
        ASSERT_NO_THROW(result = FormatInput(source, prepared));
        EXPECT_EQ(FormatInput(result.Text, prepared).Text, result.Text);
        const auto begin = uint32_t(source.find('['));
        const auto end = uint32_t(source.find('}', begin) + 1);
        const std::array<parser::SourceRange, 1> immutable{{{begin, end}}};
        const auto protectedResult = FormatInput(source, prepared, {.ImmutableRanges = immutable});
        EXPECT_NE(protectedResult.Text.find(source.substr(begin, end - begin)), std::string::npos);
    }
}

TEST(PublicPipeline, TerminalConstructorTemplateIdsRespectProtectionAndScope)
{
    const test::golden::Fixture fixture{
        std::filesystem::path(__FILE__).parent_path() / "testdata/public-pipeline",
        "terminal-constructor-template-ids",
    };
    const auto source = fixture.InputNamed(".input.cpp");
    const formatter::PreparedFormat prepared(formatter::LoadConfigFile(fixture.Named(".hit-fmt")).FormattingStyle);
    const auto begin = uint32_t(source.find("explicit Parcel"));
    const auto end = uint32_t(source.find('{', begin));
    const std::array<parser::SourceRange, 1> header{{{begin, end}}};
    const auto protectedResult = FormatInput(source, prepared, {.ImmutableRanges = header});
    EXPECT_NE(protectedResult.Text.find(source.substr(begin, end - begin)), std::string::npos);

    const auto point = uint32_t(source.find("value,1") + 6);
    const std::array<parser::SourceRange, 1> selected{{{point, point + 1}}};
    const auto owned = FormatInput(source, prepared, {.EditScope = selected});
    auto expected = source;
    expected.insert(point, " ");
    EXPECT_EQ(owned.Text, expected);
    const std::array<parser::SourceRange, 1> immutable{{{0, uint32_t(source.size())}}};
    const auto frozen = FormatInput(source, prepared, {.ImmutableRanges = immutable});
    EXPECT_EQ(frozen.Text, source);
}

TEST(PublicPipeline, JoinedTemplateClosersRespectEditScope)
{
    const std::string source = "using First = Box<Box<int> >;\n" "using Second = Box<Box<int> >;\n";
    formatter::Style style;
    style.TemplateTypeArgumentInteriorSpacing = formatter::GapPolicy::NoSpace;
    const formatter::PreparedFormat prepared(style);
    const auto complete = FormatInput(source, prepared);
    EXPECT_EQ(complete.Text, "using First = Box<Box<int>>;\n" "using Second = Box<Box<int>>;\n");
    const auto point = uint32_t(source.find("> >") + 1);
    const std::array selected{parser::SourceRange{point, point + 1}};
    const auto scoped = FormatInput(source, prepared, {.EditScope = selected});
    auto expected = source;
    expected.erase(point, 1);
    EXPECT_EQ(scoped.Text, expected);
    const std::array all{parser::SourceRange{0, uint32_t(source.size())}};
    const auto frozen = FormatInput(source, prepared, {.ImmutableRanges = all});
    EXPECT_EQ(frozen.Text, source);
}

TEST(PublicPipeline, TerminalConstructorSelfTypeStatementsRespectEditScope)
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
        const auto source = "template<class T> struct Owner { Owner() {} Owner(T input) {} "
            "void consume(Owner<T> input) {} void inspect(T seed) { " + std::string(body) +
            " invoke(seed,seed); } };\n";
        SCOPED_TRACE(source);
        formatter::FormatResult complete;
        ASSERT_NO_THROW(complete = FormatInput(source, prepared));
        const auto point = uint32_t(source.find("seed,seed") + 5);
        const std::array<parser::SourceRange, 1> selected{{{point, point + 1}}};
        const auto scoped = FormatInput(source, prepared, {.EditScope = selected});
        auto expected = source;
        expected.insert(point, " ");
        EXPECT_EQ(scoped.Text, expected);
        EXPECT_EQ(FormatInput(scoped.Text, prepared, {.EditScope = selected}).Text, scoped.Text);
        const std::array<parser::SourceRange, 1> all{{{0, uint32_t(source.size())}}};
        const auto frozen = FormatInput(source, prepared, {.ImmutableRanges = all});
        EXPECT_EQ(frozen.Text, source);
    }
}

TEST(PublicPipeline,
    TerminalConstructorBoundariesRespectStatementScope)
{
    const std::string source = "template<class E> struct Chest { " "Chest<E>(E* value) noexcept(check(value)) {} "
        "void inspect(E seed) { Chest<E>(slot); " "Chest<E>* pointer = &slot; invoke(seed,seed); } };\n";
    auto style = Canonical();
    style.ColumnLimit = 10000;
    style.CommaSpaceAfter = formatter::GapPolicy::Space;
    const formatter::PreparedFormat prepared(style);
    const auto point = uint32_t(source.find("seed,seed") + 5);
    const std::array<parser::SourceRange, 1> selected{{{point, point + 1}}};
    const auto owned = FormatInput(source, prepared, {.EditScope = selected});
    auto expected = source;
    expected.insert(point, " ");
    EXPECT_EQ(owned.Text, expected);
    EXPECT_EQ(FormatInput(owned.Text, prepared, {.EditScope = selected}).Text, owned.Text);
    const auto complete = FormatInput(source, prepared);
    EXPECT_EQ(FormatInput(complete.Text, prepared).Text, complete.Text);
    const std::array<parser::SourceRange, 1> all{{{0, uint32_t(source.size())}}};
    const auto frozen = FormatInput(source, prepared, {.ImmutableRanges = all});
    EXPECT_EQ(frozen.Text, source);
}

formatter::Style EndingStyle(std::string_view name)
{
    return formatter::LoadConfigFile(std::filesystem::path(__FILE__).parent_path() / "testdata/line-endings" /
        (std::string(name) + ".hit-fmt")).FormattingStyle;
}

TEST(PublicPipeline, LineEndingWholeAtomsScopeAndExistingEOFReplacement)
{
    const std::string source = "int a;\r\nint b;\r\n";
    const formatter::PreparedFormat prepared(EndingStyle("lf"));
    const auto first = parser::SourceOffset(source.find('\r'));
    for (const auto range : {parser::SourceRange{first, first + 1}, parser::SourceRange{first + 1, first + 2}}) {
        const std::array selected{range};
        const auto result = FormatInput(source, prepared, {.EditScope = selected});
        EXPECT_EQ(result.Text, source);
    }
    const std::array selected{
        parser::SourceRange{first, first + 2},
        parser::SourceRange{parser::SourceOffset(source.size() - 2), parser::SourceOffset(source.size())},
    };
    const auto result = FormatInput(source, prepared, {.EditScope = selected});
    EXPECT_EQ(result.Text, "int a;\nint b;\n");
    const std::array<parser::SourceRange, 0> none{};
    EXPECT_EQ(FormatInput(source, prepared, {.EditScope = none}).Text, source);
    const std::array immutable{selected[1]};
    EXPECT_EQ(FormatInput(source, prepared, {.ImmutableRanges = immutable}).Text, "int a;\nint b;\r\n");
    for (const auto name : {"lf", "crlf", "legacy"}) {
        const formatter::PreparedFormat mode(EndingStyle(name));
        for (const auto text : {"", "int a;", "int a;\r"}) {
            EXPECT_EQ(FormatInput(text, mode).Text, text);
        }
        const std::string noFinal = "int a;";
        const std::array whole{parser::SourceRange{0, 6}};
        EXPECT_EQ(FormatInput(noFinal, mode, {.EditScope = whole}).Text, noFinal);
    }
}

TEST(PublicPipeline, LineEndingTokenOwnedCRBareCRProtectionAndIncludeMovement)
{
    const std::string source = "#define VALUE 1\r\n// note\r\n\r\n" "int a;\r \n/* block\r\ntext */\n"
        "const char* s=R\"(one\r\ntwo)\";\n" "// clang-format off\r\nint   b;\r\n// clang-format on\r\n" "int c;\n";
    for (const auto name : {"lf", "crlf"}) {
        const formatter::PreparedFormat prepared(EndingStyle(name));
        const auto result = FormatInput(source, prepared);
        EXPECT_NE(result.Text.find("#define VALUE 1\r\n"), std::string::npos);
        EXPECT_NE(result.Text.find("// note\r\n"), std::string::npos);
        EXPECT_NE(result.Text.find("a;\r "), std::string::npos);
        EXPECT_NE(result.Text.find("int   b;\r\n// clang-format on\r\n"), std::string::npos);
        EXPECT_EQ(result.Text.find("\r\r\n"), std::string::npos);
        EXPECT_EQ(FormatInput(result.Text, prepared).Text, result.Text);
    }
    auto style = EndingStyle("lf");
    style.IncludeOrder = formatter::IncludeOrder::Sort;
    const formatter::PreparedFormat prepared(style);
    // Mixed physical ending islands do not acquire movement rights.
    const std::string mixed = "#include \"z.h\"\r\n#include \"a.h\"\n\nint x;\r\n";
    const auto nonmoving = FormatInput(mixed, prepared);
    EXPECT_TRUE(nonmoving.Text.starts_with(mixed.substr(0, mixed.find("int x"))));
    const std::string includes = "#include \"z.h\"\r\n#include \"a.h\"\r\n\nint x;\r\n";
    const auto moved = FormatInput(includes, prepared);
    EXPECT_NE(moved.Text.find("#include \"a.h\"\r\n#include \"z.h\"\r\n"), std::string::npos);
    EXPECT_TRUE(moved.Text.ends_with("int x;\n"));
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
    const auto after = FormatInput(source, formatter::PreparedFormat(explicitPreserve));
    EXPECT_EQ(after.Text, before.Text);
    EXPECT_NE(after.Text.find("{\r\n"), std::string::npos);
    EXPECT_TRUE(after.Text.ends_with("}\n"));
}

TEST(PublicPipeline, LineEndingCommentGuardDoesNotGrantPartialNewlineRights)
{
    const std::string source = "// lf\n\nint a;\n// crlf\r\n\nint b;\n";
    const formatter::PreparedFormat prepared(EndingStyle("crlf"));
    const auto result = FormatInput(source, prepared);
    EXPECT_EQ(result.Text, "// lf\n\r\nint a;\r\n// crlf\r\n\r\nint b;\r\n");
    EXPECT_EQ(FormatInput(result.Text, prepared).Text, result.Text);
    auto style = EndingStyle("lf");
    style.TrailingWhitespace = formatter::TrailingWhitespace::Trim;
    const std::string partialSource = "int a; \r\n  int b;\r\n";
    const std::array selected{parser::SourceRange{6, 7}};
    const auto partial = FormatInput(partialSource, formatter::PreparedFormat(style), {.EditScope = selected});
    EXPECT_EQ(partial.Text, "int a;\r\n  int b;\r\n");
}

TEST(PublicPipeline, LineEndingFullBytesAtEndKeepHorizontalAndPartialFences)
{
    const formatter::PreparedFormat prepared(EndingStyle("lf"));
    const std::string source = "int a; \r\nint b;\r\n";
    const std::array selected{
        parser::SourceRange{6, 9},
        parser::SourceRange{parser::SourceOffset(source.size() - 2), parser::SourceOffset(source.size())},
    };
    const auto result = FormatInput(source, prepared, {.EditScope = selected});
    EXPECT_EQ(result.Text, "int a; \nint b;\n");
    EXPECT_EQ(FormatInput(result.Text, prepared).Text, result.Text);
    const std::array atomOnly{parser::SourceRange{7, 9}};
    const auto partial = FormatInput(source, prepared, {.EditScope = atomOnly});
    EXPECT_EQ(partial.Text, source); // no right to evade the unselected prefix.
    const std::array immutable{parser::SourceRange{8, 9}};
    EXPECT_EQ(
        FormatInput(source, prepared, {.ImmutableRanges = immutable, .EditScope = selected}).Text,
        "int a; \r\nint b;\n");
}

TEST(PublicPipeline, LineEndingBareCRSurvivesRepeatedSelection)
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
                const auto second = FormatInput(first.Text, prepared);
                EXPECT_EQ(second.Text, first.Text);
                EXPECT_EQ(FormatInput(second.Text, prepared).Text, second.Text);
                style.LineEnding =
                    name == std::string_view("lf") ? formatter::LineEnding::CRLF : formatter::LineEnding::LF;
                const formatter::PreparedFormat opposite(style);
                const auto changed = FormatInput(first.Text, opposite);
                EXPECT_EQ(FormatInput(changed.Text, opposite).Text, changed.Text);
            }
        }
    }
}

TEST(PublicPipeline, LineEndingBareCRRespectsScopeAndProtection)
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
        const auto result = FormatInput(source, prepared, {.EditScope = selected});
        EXPECT_EQ(result.Text, source);
    }
    const std::array immutable{parser::SourceRange{6, 9}};
    const auto protectedResult = FormatInput(source, prepared, {.ImmutableRanges = immutable});
    EXPECT_EQ(protectedResult.Text, source);
    const std::string function = "void f(){\r\r\n    return;\n}\n";
    auto local = EndingStyle("lf");
    local.IndentationKind = formatter::IndentationKind::Spaces;
    local.IndentWidth = 2;
    const formatter::PreparedFormat localPrepared(local);
    const auto result = FormatInput(function, localPrepared);
    EXPECT_NE(result.Text.find("{\r\r\n    return;"), std::string::npos);
    EXPECT_EQ(FormatInput(result.Text, localPrepared).Text, result.Text);
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
        const std::string source = "void f(){\r \n\n    return;}\nint a[]={1\r,\n};\nint b[]={2,};\n";
        const auto result = FormatInput(source, prepared);
        EXPECT_NE(
            result.Text.find(name == std::string_view("lf") ? "{\r \n\n    return;" : "{\r \r\n\r\n    return;"),
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
        EXPECT_EQ(FormatInput(result.Text, prepared).Text, result.Text);
        const auto paired = FormatInput("namespace N {\nint x;\n}\r\n", prepared);
        EXPECT_NE(paired.Text.find("// namespace N"), std::string::npos);
        const auto interior = FormatInput("namespace N {\r \nint x;\n}\n", prepared);
        EXPECT_NE(interior.Text.find("// namespace N"), std::string::npos);
    }
}

TEST(PublicPipeline, LineEndingCompositionControlSeparatorGeometryAndDefaults)
{
    const std::string source = "void flow(bool ready) {\r\n  if (ready) {\r\n    left();\r\n  }\r\n"
        "  else {\r\n    right();\r\n  }\r\n" "  do {\r\n    tick();\r\n  }\r \n"
        "  while (ready);\r\n  do {\r\n    next();\r\n  }\r\n" "  while (ready);\r\n}\r\n";
    for (const auto ending : {formatter::LineEnding::LF, formatter::LineEnding::CRLF}) {
        auto style = Canonical();
        style.LineEnding = ending;
        style.IndentWidth = 2;
        style.BodiesLayout = formatter::CategoryLayout::Preserve;
        const formatter::PreparedFormat prepared(style);
        const auto result = FormatInput(source, prepared);
        EXPECT_NE(result.Text.find("} else {"), std::string::npos);
        EXPECT_NE(result.Text.find("} while (ready);"), std::string::npos);
        EXPECT_NE(
            result.Text
                .find(ending == formatter::LineEnding::LF ? "}\r \n  while (ready);" : "}\r \r\n  while (ready);"),
            std::string::npos);
        EXPECT_EQ(FormatInput(result.Text, prepared).Text, result.Text);
        const auto begin = static_cast<parser::SourceOffset>(source.find("}\r \n") + 1);
        const std::array selected{parser::SourceRange{begin, begin + 3}};
        const auto partial = FormatInput(source, prepared, {.EditScope = selected});
        EXPECT_EQ(partial.Text, source);
    }
    auto omitted = Canonical();
    omitted.BodiesLayout = formatter::CategoryLayout::Preserve;
    auto explicitPreserve = omitted;
    explicitPreserve.LineEnding = formatter::LineEnding::Preserve;
    const auto original = FormatInput(source, formatter::PreparedFormat(omitted));
    const auto explicitResult = FormatInput(source, formatter::PreparedFormat(explicitPreserve));
    EXPECT_EQ(explicitResult.Text, original.Text);
    EXPECT_NE(original.Text.find("} else {"), std::string::npos);
}

TEST(PublicPipeline, LineEndingCompositionRecoveryCloneAndScopedTail)
{
    const std::string candidate = "static Vec NOTE(0) load(int Type) {\r\n Type * value; ;\r\n }";
    const std::string source = "using Type = int;\r\n" + candidate + "\r\n;\r\nint tail(){ ; return 1; ; }\r\n";
    for (const auto ending : {formatter::LineEnding::LF, formatter::LineEnding::CRLF}) {
        auto style = Canonical();
        style.LineEnding = ending;
        style.BodyShape = formatter::BodyShape::Expanded;
        style.RemoveNullStatements = true;
        style.RemoveNullDeclarations = true;
        const formatter::PreparedFormat prepared(style);
        const auto result = FormatInput(source, prepared);
        const auto begin = result.Text.find(candidate);
        ASSERT_NE(begin, std::string::npos);
        EXPECT_TRUE(result.Text.ends_with(ending == formatter::LineEnding::LF ? "}\n" : "}\r\n"));
        EXPECT_EQ(FormatInput(result.Text, prepared).Text, result.Text);
        const std::array selected{
            parser::SourceRange{
                static_cast<parser::SourceOffset>(source.find("int tail")),
                static_cast<parser::SourceOffset>(source.size()),
            },
        };
        const auto scoped = FormatInput(source, prepared, {.EditScope = selected});
        EXPECT_NE(scoped.Text, source);
        EXPECT_NE(scoped.Text.find(candidate), std::string::npos);
        const std::array<parser::SourceRange, 0> none{};
        EXPECT_EQ(FormatInput(source, prepared, {.EditScope = none}).Text, source);
    }
}

TEST(PublicPipeline, TrailingCommentSpacesNamespaceOwnershipAndLegacyCoupling)
{
    using formatter::GapPolicy;
    using formatter::NamespaceComments;
    const std::string source =
        "int amount;\t// namespace is ordinary prose\n" "namespace N {\nint local;\n}\t// arbitrary closer prose\n";
    for (const auto old :
        {
            GapPolicy::Preserve,
            GapPolicy::Space,
            GapPolicy::NoSpace,
            GapPolicy::GeneratedSpace,
            GapPolicy::GeneratedNoSpace,
        })
    {
        for (const auto policy : {NamespaceComments::Preserve, NamespaceComments::AddMissing, NamespaceComments::Fix}) {
            formatter::Style style;
            style.TrailingCommentSpacing = old;
            style.TrailingCommentSpaces = formatter::TrailingCommentSpaceSetting{2};
            style.NamespaceComments = policy;
            style.NamespaceCommentSpaces = 4;
            const formatter::PreparedFormat prepared(style);
            const auto result = FormatInput(source, prepared);
            EXPECT_TRUE(result.Text.starts_with("int amount;  // namespace"));
            EXPECT_TRUE(result.Text.ends_with(
                policy == NamespaceComments::Fix ? "}    // namespace N\n" : "}\t// arbitrary closer prose\n"));
            EXPECT_EQ(FormatInput(result.Text, prepared).Text, result.Text);
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
    const auto missing = FormatInput("namespace N {\nint local;\n}\n", formatter::PreparedFormat(style));
    EXPECT_TRUE(missing.Text.ends_with("}    // namespace N\n"));
}

TEST(PublicPipeline, TrailingCommentSpacesRetainTokens)
{
    const std::string source = "int quotient=8 / \t// divisor\n2;\n" "int product=8 * \t// factor\n2;\n"
        "int task(){ int value=1; value /= \t// divisor\n2; " "return 4 \t// integer\n; }\n"
        "const char* text(){return \"/\" \t// literal\n;}\n" "/**/ \t// consecutive comments\n";
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
    Input before(source), after(result.Text);
    auto original = parser::Parse(before.View(), [] (parser::ParseError) {});
    auto rendered = parser::Parse(after.View(), [] (parser::ParseError) {});
    ASSERT_EQ(original.TokenCount(), rendered.TokenCount());
    for (parser::TokenIndex i = 0; i < original.TokenCount(); ++i) {
        EXPECT_EQ(original.Kind(i), rendered.Kind(i)) << i;
        EXPECT_EQ(original.TokenText(i), rendered.TokenText(i)) << i;
    }
}

TEST(PublicPipeline,
    TrailingCommentSpacesRespectWholeGapScopeAndProtection)
{
    const std::string source = "int amount; \t// memo\r\n";
    const auto begin = parser::SourceOffset(source.find(';') + 1);
    const auto end = parser::SourceOffset(source.find("//"));
    formatter::Style style;
    style.TrailingCommentSpaces = formatter::TrailingCommentSpaceSetting{2};
    const formatter::PreparedFormat prepared(style);
    const std::array whole{parser::SourceRange{begin, end}};
    formatter::FormatRequest request{};
    request.EditScope = whole;
    const auto result = FormatInput(source, prepared, request);
    EXPECT_EQ(result.Text, "int amount;  // memo\r\n");
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
    }
    request.EditScope = std::span<const parser::SourceRange>{};
    EXPECT_EQ(FormatInput(source, prepared, request).Text, source);
    request.EditScope = std::nullopt;
    const std::array immutable{parser::SourceRange{begin + 1, end}};
    request.ImmutableRanges = immutable;
    EXPECT_EQ(FormatInput(source, prepared, request).Text, source);
    request.ImmutableRanges = {};
    const std::string adjacent = "int item;// memo\n";
    const std::array endOnly{parser::SourceRange{0, parser::SourceOffset(adjacent.find("//"))}};
    request.EditScope = endOnly;
    EXPECT_EQ(FormatInput(adjacent, prepared, request).Text, adjacent);
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
    EXPECT_EQ(result.Text.substr(9, 10000).find_first_not_of(' '), std::string::npos);
    EXPECT_FALSE(result.Diagnostics.empty());
    EXPECT_EQ(FormatInput(result.Text, prepared).Text, result.Text);
}

TEST(PublicPipeline,
    TrailingCommentSpacesCompositionRetainsScopedProducerBoundaries)
{
    // These prefixes exercise the integrated separator, annotation and terminal
    // producers. A comment-count demand cannot grant authority over any prefix.
    for (const std::string_view prefix :
        {
            "void work(bool ready){if(ready){tick();}\nelse{wait();}}\n",
            "using Type = int;\n" "static Vec NOTE(0) load(int Type) { Type * value; }\n",
            "template<class T> struct Owner { Owner() {} " "void inspect(T seed) { "
                "Owner<T>(slot); Owner<T>* pointer = &slot; } };\n",
        })
    {
        const auto source = std::string(prefix) + "int tail; \t// scoped memo\n";
        const auto begin = parser::SourceOffset(source.rfind(';') + 1);
        const auto end = parser::SourceOffset(source.find("// scoped"));
        const std::array selected{parser::SourceRange{begin, end}};
        auto style = Canonical();
        style.ColumnLimit = 10000;
        style.TrailingCommentSpaces = formatter::TrailingCommentSpaceSetting{2};
        const formatter::PreparedFormat prepared(style);
        const auto owned = FormatInput(source, prepared, {.EditScope = selected});
        EXPECT_EQ(owned.Text.substr(0, prefix.size()), prefix);
        EXPECT_TRUE(owned.Text.ends_with("int tail;  // scoped memo\n"));
        EXPECT_EQ(FormatInput(owned.Text, prepared, {.EditScope = selected}).Text, owned.Text);
        const std::array partial{parser::SourceRange{begin + 1, end}};
        EXPECT_EQ(FormatInput(source, prepared, {.EditScope = partial}).Text, source);
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
    const std::string header = "static inline DEFINE_CASE(Shared, Helper(alpha   +  beta), " "const Shared& input)";
    const std::string source = "using Shared = int;\n" + header +
        " {Shared* pointer; auto value=left+right; ; " "if(value) Use(value); TOKEN_TEXT(alpha   +  beta);}\n";
    Input input(source);
    auto tree = parser::Parse(input.View(), {}, prepared.ParsingOptions());
    const auto* macro = tree.Root().FirstChild->NextSibling;
    ASSERT_NE(macro, nullptr);
    ASSERT_EQ(macro->Kind, parser::NodeKind::MacroFunctionDefinition);
    const auto* body = macro->FirstChild->NextSibling;
    ASSERT_NE(body, nullptr);
    ASSERT_EQ(body->FirstChild->Kind, parser::NodeKind::DeclarationStatement);
    const auto result = formatter::Format(tree, prepared, {});
    EXPECT_EQ(result.Text, FormatInput(source, prepared).Text);
    EXPECT_NE(result.Text.find(header), std::string::npos);
    EXPECT_NE(result.Text.find("left + right"), std::string::npos);
    EXPECT_NE(result.Text.find("TOKEN_TEXT(alpha   +  beta)"), std::string::npos);
    EXPECT_EQ(FormatInput(result.Text, prepared).Text, result.Text);
}

TEST(PublicPipeline, ClassMacroHintsSurviveNormalizationAndScopedFormatting)
{
    auto style = Canonical();
    style.IndentationKind = formatter::IndentationKind::Spaces;
    style.FunctionBracePlacement = formatter::BracePlacement::NextLine;
    style.ClassMacroNames = {"OPEN_CLASS:CLOSE_CLASS"};
    style.AddMissingControlBraces = true;
    style.RemoveNullStatements = true;
    const formatter::PreparedFormat prepared(style);
    const std::string begin = "OPEN_CLASS(Name, Helper(alpha   +  beta))";
    const std::string end = "CLOSE_CLASS(Helper(gamma   +  delta))";
    const std::string source = begin + "\nvoid Run(int value) {; if(value) Use(value);}\n" + end + "\n";
    Input input(source);
    auto tree = parser::Parse(input.View(), {}, prepared.ParsingOptions());
    ASSERT_EQ(tree.Root().FirstChild->Kind, parser::NodeKind::MacroRecordDeclaration);
    const auto result = formatter::Format(tree, prepared, {});
    EXPECT_EQ(result.Text, FormatInput(source, prepared).Text);
    EXPECT_TRUE(result.Text.starts_with(begin + "\n"));
    EXPECT_TRUE(result.Text.ends_with(end + "\n"));
    EXPECT_EQ(FormatInput(result.Text, prepared).Text, result.Text);

    // Excluding both delimiters must not discard the enclosing class context.
    const std::array selected{
        parser::SourceRange{parser::SourceOffset(begin.size()), parser::SourceOffset(source.find(end))},
    };
    const auto scoped = FormatInput(source, prepared, {.EditScope = selected});
    EXPECT_EQ(scoped.Text, result.Text);
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
    const std::string source = "#if OUTER\n\t # if INNER\n" "#define VALUE first \\\n    second    \\\n    third\n"
        "# endif\n# endif\n/////\nint item;\n";
    const auto begin = parser::SourceOffset(source.find('\t'));
    const auto end = parser::SourceOffset(source.find('\n', begin));
    const std::array selected{parser::SourceRange{begin, end}};
    const auto limited = FormatInput(source, prepared, {.EditScope = selected});
    EXPECT_NE(limited.Text.find("    #if INNER"), std::string::npos);
    const std::array partial{parser::SourceRange{begin + 1, end}};
    const auto denied = FormatInput(source, prepared, {.EditScope = partial});
    EXPECT_EQ(denied.Text, source);
    // Selecting the token alone does not authorize moving its leading prefix.
    const std::array tokenOnly{parser::SourceRange{begin + 2, end}};
    const auto tokenDenied = FormatInput(source, prepared, {.EditScope = tokenOnly});
    EXPECT_EQ(tokenDenied.Text, source);
    const std::array prefix{parser::SourceRange{begin, begin + 2}};
    const auto prefixProtected = FormatInput(source, prepared, {.ImmutableRanges = prefix});
    EXPECT_NE(prefixProtected.Text.find("\t # if INNER"), std::string::npos);
    const std::array immutable{parser::SourceRange{0, parser::SourceOffset(source.size())}};
    EXPECT_EQ(FormatInput(source, prepared, {.ImmutableRanges = immutable}).Text, source);
    const auto disabled = "// hit-fmt off\n" + source + "// hit-fmt on\n";
    EXPECT_EQ(FormatInput(disabled, prepared).Text, disabled);
    const auto result = FormatInput(source, prepared);
    EXPECT_EQ(FormatInput(result.Text, prepared).Text, result.Text);
}

TEST(PublicPipeline, SortedDirectiveWhitespaceProjectsSelectedIncludeAtoms)
{
    formatter::Style style;
    style.IndentPreprocessingDirectives = true;
    style.IncludeOrder = formatter::IncludeOrder::Sort;
    const formatter::PreparedFormat prepared(style);
    const std::string source = "#if OUTER\n\t #include \"zeta.h\"\n" "  #include \"alpha.h\"\n#endif\nint tail;\n";
    const auto begin = parser::SourceOffset(source.find('\t'));
    const auto end = parser::SourceOffset(source.find("#endif"));
    const std::array selected{parser::SourceRange{begin, end}};
    const auto result = FormatInput(source, prepared, {.EditScope = selected});
    EXPECT_LT(result.Text.find("alpha.h"), result.Text.find("zeta.h"));
    EXPECT_NE(result.Text.find("    #include"), std::string::npos);
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
}

TEST(PublicPipeline, FunctionBodyMacroHintsCannotOverrideProtection)
{
    auto style = Canonical();
    style.IndentationKind = formatter::IndentationKind::Spaces;
    style.FunctionBodyMacroNames = {"DEFINE_CASE"};
    style.BinaryOperatorSpacing = formatter::GapPolicy::Space;
    style.HonorClangFormatDirectives = true;
    const formatter::PreparedFormat prepared(style);
    const std::string source = "DEFINE_CASE(Suite, Case) {auto sum=left+right; Use(sum);}\n";
    const auto begin = parser::SourceOffset(source.find('{') + 1);
    const auto end = parser::SourceOffset(source.find('}'));
    const std::array selected{parser::SourceRange{begin, end}};
    const auto limited = FormatInput(source, prepared, {.EditScope = selected});
    EXPECT_NE(limited.Text.find("left + right"), std::string::npos);
    const std::array immutable{parser::SourceRange{0, parser::SourceOffset(source.size())}};
    EXPECT_EQ(FormatInput(source, prepared, {.ImmutableRanges = immutable}).Text, source);
    const auto disabled = "// clang-format off\n" + source + "// clang-format on\n";
    EXPECT_EQ(FormatInput(disabled, prepared).Text, disabled);
    for (const auto malformed : {"DEFINE_CASE(Suite, [bad)) {left+right;}\n", "DEFINE_CASE(Suite, Bad) {left+right;\n"})
    {
        Input input(malformed);
        auto tree = parser::Parse(input.View(), [] (auto) {}, prepared.ParsingOptions());
        ASSERT_NE(tree.Root().FirstChild, nullptr);
        EXPECT_NE(tree.Root().FirstChild->Kind, parser::NodeKind::MacroFunctionDefinition);
        EXPECT_EQ(FormatInput(malformed, prepared).Text, malformed);
    }
}

} // namespace
