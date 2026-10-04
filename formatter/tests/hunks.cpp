#include <formatter/hunks.hpp>
#include <gtest/gtest.h>
#include <algorithm>
#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
namespace {
TEST(HunkParser, GroupsFilesAndResolvesPathsAgainstGivenDirectory)
{
    std::istringstream input(
        "file src/a.cpp\r\nhunk 2:4\r\nhunk 8:8\n"
        "file src/file with spaces.cpp\nhunk 1:1\n"
        "file src/../src/a.cpp\nhunk 9:10\n");
    const auto base = std::filesystem::current_path() / "hunk-test";
    const auto parsed = formatter::ParseChangedHunks(input, base);
    ASSERT_EQ(parsed.Paths.size(), 3);
    EXPECT_EQ(parsed.Paths[0], base / "src/a.cpp");
    EXPECT_EQ(parsed.Paths[1], base / "src/file with spaces.cpp");
    EXPECT_EQ(parsed.Paths[2], parsed.Paths[0]);
    const auto& hunks = parsed.Hunks.at((base / "src/a.cpp").string());
    ASSERT_EQ(hunks.size(), 3);
    EXPECT_EQ(hunks[0], (formatter::Hunk{2, 4}));
    EXPECT_EQ(hunks[1], (formatter::Hunk{8, 8}));
    EXPECT_EQ(hunks[2], (formatter::Hunk{9, 10}));
    EXPECT_EQ(
        parsed.Hunks.at((base / "src/file with spaces.cpp").string())[0],
        (formatter::Hunk{1, 1}));
}
TEST(HunkParser, EmptyInputSelectsNoFiles)
{
    std::istringstream input;
    const auto parsed = formatter::ParseChangedHunks(
        input,
        std::filesystem::current_path());
    EXPECT_TRUE(parsed.Paths.empty());
    EXPECT_TRUE(parsed.Hunks.empty());
}
TEST(HunkParser, RejectsMalformedRecordsWithLineNumbers)
{
    for (auto [text, line] : {
        std::pair<std::string_view, size_t>{"hunk 1:2\n", 1},
        {"file a.cpp\nfile b.cpp\nhunk 1:1\n", 2},
        {"file a.cpp\nhunk 0:1\n", 2},
        {"file a.cpp\nhunk 4:2\n", 2},
        {"file a.cpp\nhunk 2:x\n", 2},
        {"file a.cpp\nhunk 1:2:3\n", 2},
        {"file a.cpp\nline 1:2\n", 2},
    })
    {
        std::istringstream input{std::string(text)};
        try {
            (void)formatter::ParseChangedHunks(
                input,
                std::filesystem::current_path());
            FAIL() << "Accepted: " << text;
        } catch (const std::runtime_error& error) {
            EXPECT_NE(std::string(error.what()).find(
                "changed ranges line " + std::to_string(line)),
                std::string::npos) << text;
        }
    }
}
TEST(HunkParser, RejectsFileWithoutHunksAtEndOfInput)
{
    std::istringstream input("file a.cpp\n");
    EXPECT_THROW(formatter::ParseChangedHunks(
        input,
        std::filesystem::current_path()), std::runtime_error);
}
struct HunkInput
{
    std::string Storage;
    size_t Size;

    explicit HunkInput(std::string_view text)
        : Storage(text)
        , Size(text.size())
    {
        Storage.append(util::SourcePaddingSize, '\0');
    }

    parser::SyntaxTree Tree() const
    {
        return parser::Parse(
            {Storage.data(), Size},
            [] (parser::ParseError) {});
    }
};
TEST(HunkAdapter, RejectsInvalidOriginalPhysicalLines)
{
    for (std::string_view text : {"", "int x;", "int x;\n"}) {
        HunkInput input(text);
        auto tree = input.Tree();
        for (formatter::Hunk hunk : {formatter::Hunk{0, 1}, {2, 1}, {2, 2}}) {
            EXPECT_THROW((void)formatter::FormatChanged(tree,
                formatter::Style{}, std::span(&hunk, 1), false),
                std::invalid_argument) << text;
        }
        if (text.empty()) {
            formatter::Hunk hunk{1, 1};
            EXPECT_THROW((void)formatter::FormatChanged(tree,
                formatter::Style{}, std::span(&hunk, 1), false),
                std::invalid_argument);
        }
    }
}
formatter::ChangedFormatResult Changed(
    std::string_view source,
    std::span<const formatter::Hunk> hunks,
    bool spillover = false,
    formatter::Style style = {},
    formatter::FormatRequest request = {})
{
    HunkInput input(source);
    auto tree = input.Tree();
    return formatter::FormatChanged(
        tree,
        formatter::PreparedFormat(style),
        hunks,
        spillover,
        request);
}
formatter::Style HunkStyle()
{
    formatter::Style style;
    style.Layout = formatter::LayoutMode::Canonical;
    style.IndentationKind = formatter::IndentationKind::Spaces;
    style.SpaceInsideFunctionParameters = formatter::GapPolicy::NoSpace;
    style.CommaSpaceAfter = formatter::GapPolicy::Space;
    return style;
}
TEST(HunkAdapter, CoalescesUnsortedAdjacentAndOverlappingOriginalLines)
{
    const std::string source =
        "int a( int x );\nint b( int x );\nint c( int x );\n";
    const formatter::Hunk split[] = {{3, 3}, {1, 1}, {2, 3}, {1, 2}};
    const formatter::Hunk joined[] = {{1, 3}};
    auto result = Changed(source, split, false, HunkStyle());
    EXPECT_EQ(result.SelectedLines, std::vector<formatter::Hunk>({{1, 3}}));
    EXPECT_EQ(
        result.Result.Text,
        Changed(source, joined, false, HunkStyle()).Result.Text);
}
TEST(HunkAdapter, EmptySelectionAndStrictEOFAuthority)
{
    EXPECT_EQ(Changed("", {}).Result.Text, "");
    EXPECT_TRUE(Changed("", {}).Incomplete.empty());
    for (const std::string source : {"int f( int x );", "int f( int x );\n"}) {
        EXPECT_EQ(Changed(source, {}, true, HunkStyle()).Result.Text, source);
        const formatter::Hunk selected[] = {{1, 1}};
        const auto result = Changed(source, selected, false, HunkStyle());
        EXPECT_EQ(result.Result.Text.ends_with('\n'), source.ends_with('\n'));
        EXPECT_EQ(result.SelectedLines.size(), 1);
    }
}
TEST(HunkAdapter, StrictSelectedIndentTrimAndExcludedFollowingPoint)
{
    const std::string source =
        "void f() {  \r\n      work();  \r\nnext();\r\n}\r\n";
    const formatter::Hunk selected[] = {{2, 2}};
    auto style = HunkStyle();
    style.TrailingWhitespace = formatter::TrailingWhitespace::Trim;
    const auto result = Changed(source, selected, false, style);
    EXPECT_EQ(
        result.Result.Text,
        "void f() {  \r\n    work();\r\nnext();\r\n}\r\n");
    EXPECT_FALSE(result.Incomplete.empty());
    style.IndentationKind = formatter::IndentationKind::Preserve;
    style.TrailingWhitespace = formatter::TrailingWhitespace::Preserve;
    EXPECT_EQ(Changed(source, selected, false, style).Result.Text, source);
}
TEST(HunkAdapter, CallerScopeAndExplicitProtectionOnlyNarrowAuthority)
{
    const std::string source = "void f() {\n      work();\n}\n";
    const formatter::Hunk selected[] = {{2, 2}};
    std::span<const parser::SourceRange> empty;
    EXPECT_EQ(Changed(
        source,
        selected,
        true,
        HunkStyle(),
        {.EditScope = empty}).Result.Text, source);
    for (const parser::SourceRange invalid : {
        parser::SourceRange{0, 0}, {1000, 1001}, {20, 10}})
    {
        EXPECT_THROW((void)Changed(
            source,
            selected,
            false,
            HunkStyle(),
            {.EditScope = std::span(&invalid, 1)}), std::invalid_argument);
    }
    const parser::SourceRange reversed[] = {{20, 22}, {11, 13}};
    EXPECT_THROW((void)Changed(
        source,
        selected,
        false,
        HunkStyle(),
        {.EditScope = reversed}), std::invalid_argument);
    const parser::SourceRange protectedLine[] = {{11, 24}};
    EXPECT_EQ(Changed(
        source,
        selected,
        false,
        HunkStyle(),
        {.ImmutableRanges = protectedLine}).Result.Text, source);
    formatter::ChangedFormatResult withPath;
    {
        std::string path = "missing/directory with spaces/original.cpp";
        withPath = Changed(source, selected, false, HunkStyle(),
            {.SourceMap = true, .InputFilePath = path});
        EXPECT_EQ(withPath.Result.Text,
            Changed(source, selected, false, HunkStyle()).Result.Text);
        EXPECT_EQ(Changed(source, selected, false, HunkStyle(),
            {.ImmutableRanges = protectedLine, .InputFilePath = path}).Result.Text,
            source);
        EXPECT_EQ(Changed(source, selected, true, HunkStyle(),
            {.EditScope = empty, .InputFilePath = path}).Result.Text, source);
        path.assign(path.size(), '?');
    }
    EXPECT_EQ(withPath.SelectedLines, std::vector<formatter::Hunk>({{2, 2}}));
    EXPECT_EQ(withPath.Result.Text, "void f() {\n    work();\n}\n");
    EXPECT_FALSE(withPath.Result.Map.empty());

    // Scope exclusion limits authority, not semantic planning. A sensitive
    // excluded child makes the outer list genuinely unplanned; selected safe
    // child/neighbor owners still establish their own generated context.
    const formatter::Hunk innerLines[] = {{3, 4}};
    auto generated = HunkStyle();
    generated.IndentationKind = formatter::IndentationKind::Preserve;
    generated.CallsLayout = formatter::CategoryLayout::Canonical;
    generated.FunctionCallSpaceBeforeParameters =
        formatter::GapPolicy::GeneratedNoSpace;
    generated.SpaceInsideCallArguments = formatter::GapPolicy::GeneratedNoSpace;
    generated.CommaSpaceBefore = formatter::GapPolicy::GeneratedNoSpace;
    generated.CommaSpaceAfter = formatter::GapPolicy::GeneratedSpace;
    generated.WhitespaceSensitiveMacros = {"KEEP"};
    for (const bool protectedOwner : {false, true}) {
        const std::string prefix = protectedOwner
            ? "void f() {\n  outer  (  KEEP(  before  ) ,\n"
            : "void f() {\n  outer  (  before ,\n";
        const std::string partial = prefix +
            "    child  (  a , b  ) , right  );\n"
            "    neighbor  (  c , d  );\n}\n";
        const auto begin = parser::SourceOffset(partial.find("    child"));
        const auto end = parser::SourceOffset(partial.find("\n}", begin) + 1);
        const parser::SourceRange authority[] = {{begin, end}};
        const auto scoped = Changed(
            partial,
            innerLines,
            false,
            generated,
            {.SourceMap = true, .EditScope = authority});
        const std::string suffix = protectedOwner
            ? "    child(a, b) , right  );\n"
            : "    child(a, b), right);\n";
        EXPECT_EQ(
            scoped.Result.Text,
            prefix + suffix + "    neighbor(c, d);\n}\n");
        // Every original byte outside strict authority remains exact once.
        // End belongs to the excluded closing line, not the selected LF.
        std::vector<unsigned> copied(partial.size(), 0);
        for (const auto& map : scoped.Result.Map) {
            if (map.Exact && map.HasOriginal) {
                for (auto i = map.Original.Begin; i < map.Original.End; ++i) {
                    ++copied[i];
                }
            }
        }
        for (parser::SourceOffset i = 0; i < partial.size(); ++i) {
            if (i < begin || end <= i) {
                EXPECT_EQ(copied[i], 1u) << i;
            }
        }
    }
}
TEST(HunkAdapter, BlockedAtomicBracesDoNotSuppressIndependentFormatting)
{
    const std::string source =
        "void f(bool x) {\n  if (x)\n    work();\n}\nint g( int x );\n";
    const formatter::Hunk selected[] = {{2, 2}, {5, 5}};
    auto style = HunkStyle();
    style.AddMissingControlBraces = true;
    const auto result = Changed(source, selected, false, style);
    EXPECT_TRUE(result.Result.Text.starts_with(
        "void f(bool x) {\n    if (x)\n    work();\n}\n"));
    EXPECT_TRUE(result.Result.Text.ends_with("int g(int x);\n"));
    // The required brace footprint is line 3, not the selected control header.
    EXPECT_TRUE(result.RequiredSkips.empty());
}
TEST(HunkAdapter, NamespaceInteriorSelectionIsNotARequestedCommentFailure)
{
    const std::string source = "namespace n {\nint f( int x );\n}\n";
    const formatter::Hunk selected[] = {{2, 2}};
    auto style = HunkStyle();
    style.NamespaceComments = formatter::NamespaceComments::AddMissing;
    style.ShortNamespaceLines = 0;
    style.IndentNamespaces = formatter::BodyIndentation::Indent;
    const auto result = Changed(source, selected, false, style);
    EXPECT_EQ(result.Result.Text, "namespace n {\n    int f(int x);\n}\n");
    EXPECT_TRUE(result.RequiredSkips.empty());
    for (const auto& coverage : result.Incomplete) {
        EXPECT_NE(
            coverage.Reason,
            formatter::HunkCoverageReason::StructuralEligibility);
    }
}
TEST(HunkAdapter, SafeIncludeSpillSortsAtomicallyAndStrictCoverageIsNotMismatch)
{
    const std::string source =
        "#include \"z.h\"\n#include \"a.h\"\n\nint f( int x );\n";
    const formatter::Hunk selected[] = {{1, 1}, {4, 4}};
    auto style = HunkStyle();
    style.IncludeOrder = formatter::IncludeOrder::Sort;
    const auto strict = Changed(source, selected, false, style);
    EXPECT_TRUE(strict.Result.Text.starts_with(
        "#include \"z.h\"\n#include \"a.h\"\n"));
    EXPECT_TRUE(strict.Result.Text.ends_with("int f(int x);\n"));
    EXPECT_TRUE(strict.RequiredSkips.empty());
    EXPECT_FALSE(strict.Incomplete.empty());
    const auto spill = Changed(source, selected, true, style);
    EXPECT_TRUE(spill.Result.Text.starts_with(
        "#include \"a.h\"\n#include \"z.h\"\n"));
    EXPECT_TRUE(spill.Result.Text.ends_with("int f(int x);\n"));
    EXPECT_EQ(
        spill.PermittedLines,
        (std::vector<formatter::Hunk>{{1, 2}, {4, 4}}));
}
TEST(HunkAdapter, BoundedNestedListsNeverPromoteFunctionOrOverTwoHundredLines)
{
    std::string source =
        "void f() {\n  outer(\n    inner(1, 2),\n    3);\n"
        "  untouched(  );\n}\n";
    const formatter::Hunk selected[] = {{3, 3}};
    const auto nested = Changed(source, selected, true, HunkStyle());
    EXPECT_EQ(nested.PermittedLines, (std::vector<formatter::Hunk>{{2, 4}}));
    EXPECT_TRUE(
        nested.Result.Text.find("  untouched(  );") != std::string::npos);
    source = "void f() {\n  outer(\n    KEEP( 1,  2 ),\n    3);\n"
        "  untouched(  );\n}\n";
    auto protectedStyle = HunkStyle();
    protectedStyle.WhitespaceSensitiveMacros = {"KEEP"};
    const auto protectedList = Changed(source, selected, true, protectedStyle);
    EXPECT_EQ(
        protectedList.PermittedLines,
        (std::vector<formatter::Hunk>{{3, 3}}));
    EXPECT_NE(
        protectedList.Result.Text.find("KEEP( 1,  2 )"),
        std::string::npos);
    EXPECT_NE(
        protectedList.Result.Text.find("  untouched(  );"),
        std::string::npos);
    EXPECT_TRUE(std::any_of(protectedList.Incomplete.begin(),
        protectedList.Incomplete.end(), [] (const auto& record) {
            return record.Reason ==
                formatter::HunkCoverageReason::SemanticProtection;
        }));
    for (const size_t count : {200, 201}) {
        source = "void f() {\n  call(\n";
        for (size_t i = 0; i < count - 2; ++i) {
            source += "    1,\n";
        }
        source += "    2);\n  untouched(  );\n}\n";
        const auto bounded = Changed(source, selected, true, HunkStyle());
        EXPECT_EQ(bounded.PermittedLines, count == 200
            ? (std::vector<formatter::Hunk>{{2, 201}})
            : (std::vector<formatter::Hunk>{{3, 3}}));
        EXPECT_TRUE(
            bounded.Result.Text.find("  untouched(  );") != std::string::npos);
        if (count == 201) {
            EXPECT_FALSE(bounded.Incomplete.empty());
        }
    }
}
TEST(HunkAdapter, AlreadyCompliantPartialLayoutIsIncompleteNotRequiredFailure)
{
    const std::string source = "void f() {\n    call(1,\n        2);\n}\n";
    const formatter::Hunk selected[] = {{3, 3}};
    auto style = HunkStyle();
    style.CallsLayout = formatter::CategoryLayout::Preserve;
    const auto result = Changed(source, selected, false, style);
    EXPECT_TRUE(result.RequiredSkips.empty());
    EXPECT_FALSE(result.Incomplete.empty());
    EXPECT_EQ(result.Result.Text, source);
}
TEST(HunkAdapter, PureScopeBlockedBraceHasDefiniteProofButDisabledBytesDoNot)
{
    const std::string source = "void f(bool x) {\n  if (x) work();\n}\n";
    const formatter::Hunk selected[] = {{2, 2}};
    auto style = HunkStyle();
    style.AddMissingControlBraces = true;
    const auto begin = parser::SourceOffset(source.find("work"));
    const parser::SourceRange narrow[] = {{begin, begin + 2}};
    const auto denied = Changed(source, selected, false, style, {
        .EditScope = narrow,
    });
    ASSERT_EQ(denied.RequiredSkips.size(), 1);
    EXPECT_EQ(
        denied.RequiredSkips[0].Action,
        formatter::StructuralAction::AddMissingControlBraces);
    const std::string disabled = "// hit-fmt off\n" + source +
        "// hit-fmt on\n";
    const formatter::Hunk disabledSelected[] = {{3, 3}};
    const auto protectedResult = Changed(
        disabled,
        disabledSelected,
        false,
        style);
    EXPECT_TRUE(protectedResult.RequiredSkips.empty());
    EXPECT_EQ(protectedResult.Result.Text, disabled);
    EXPECT_FALSE(protectedResult.Incomplete.empty());
}
TEST(HunkAdapter, OwningMapsPreserveEveryOutsideByteAndOriginalIdentity)
{
    const std::string source =
        "int before( int x );\nvoid f(bool x) {\n  if (x) work();\n"
        "  outer  (  child  (  a , b  ) , KEEP(  x, y ) , right  ); "
        "neighbor  (  c , d  );\n"
        "  ;\n}\nint after( int x );\n";
    const formatter::Hunk selected[] = {{2, 6}};
    auto style = HunkStyle();
    style.AddMissingControlBraces = true;
    style.RemoveNullStatements = true;
    style.WhitespaceSensitiveMacros = {"KEEP"};
    style.CallsLayout = formatter::CategoryLayout::Canonical;
    style.FunctionCallSpaceBeforeParameters =
        formatter::GapPolicy::GeneratedNoSpace;
    style.SpaceInsideCallArguments = formatter::GapPolicy::GeneratedNoSpace;
    style.CommaSpaceBefore = formatter::GapPolicy::GeneratedNoSpace;
    style.CommaSpaceAfter = formatter::GapPolicy::GeneratedSpace;
    // Changed destroys the original tree, PreparedFormat and operation arenas
    // before any assertions. Normalization must reuse the configured views
    // across scratch resets without retaining them in the owned result.
    const auto result = Changed(source, selected, false, style, {
        .SourceMap = true,
    });
    EXPECT_NE(result.Result.Text.find("KEEP(  x, y )"), std::string::npos);
    EXPECT_NE(
        result.Result.Text.find(
            "outer(  child(a, b) , KEEP(  x, y ) , right  )"),
        std::string::npos) << result.Result.Text;
    EXPECT_NE(result.Result.Text.find("neighbor(c, d)"), std::string::npos);
    auto explicitStyle = style;
    explicitStyle.SpaceInsideCallArguments = formatter::GapPolicy::Space;
    explicitStyle.CommaSpaceBefore = formatter::GapPolicy::NoSpace;
    explicitStyle.CommaSpaceAfter = formatter::GapPolicy::Space;
    const auto explicitResult = Changed(
        source,
        selected,
        false,
        explicitStyle,
        {.SourceMap = true});
    EXPECT_NE(explicitResult.Result.Text.find(
        "outer( child( a, b ), KEEP(  x, y ), right )"), std::string::npos)
        << explicitResult.Result.Text;
    EXPECT_NE(
        explicitResult.Result.Text.find("neighbor( c, d )"),
        std::string::npos);
    EXPECT_TRUE(std::any_of(result.Result.OutputProtection.begin(),
        result.Result.OutputProtection.end(), [] (const auto& range) {
            return uint16_t(range.Reasons) &
                uint16_t(formatter::ProtectionReason::SensitiveMacro);
        }));
    HunkInput input(source);
    const auto tree = input.Tree();
    ASSERT_EQ(result.Result.OriginalTokens.size(), tree.TokenCount());
    for (parser::TokenIndex i = 0; i < tree.TokenCount(); ++i) {
        const auto token = result.Result.OriginalTokens[i];
        if (token.Present) {
            EXPECT_EQ(std::string_view(result.Result.Text).substr(
                token.Output.Begin,
                token.Output.End - token.Output.Begin), tree.TokenText(i));
        }
    }
    std::vector<unsigned> copied(source.size(), 0);
    for (const auto& map : result.Result.Map) {
        EXPECT_LE(map.Output.End, result.Result.Text.size());
        if (map.Exact && map.HasOriginal) {
            EXPECT_EQ(std::string_view(result.Result.Text).substr(
                map.Output.Begin,
                map.Output.End -
                    map.Output.Begin), std::string_view(source).substr(
                        map.Original.Begin,
                        map.Original.End - map.Original.Begin));
            for (auto i = map.Original.Begin; i < map.Original.End; ++i) {
                ++copied[i];
            }
        }
    }
    for (parser::SourceOffset i = 0; i < source.size(); ++i) {
        if (!(result.EditScope[0].Begin <= i && i < result.EditScope[0].End)) {
            EXPECT_EQ(copied[i], 1u) << i;
        }
    }
    EXPECT_TRUE(result.RequiredSkips.empty());
    EXPECT_TRUE(result.Result.StructuralOperations.size() >= 2);
}
TEST(HunkAdapter, IncludeSpillRetainsTwoHundredLineBoundAndOutsideStatements)
{
    const formatter::Hunk selected[] = {{1, 1}};
    auto style = HunkStyle();
    style.IncludeOrder = formatter::IncludeOrder::Sort;
    for (const size_t count : {200, 201}) {
        std::string source = "#include \"z.h\"\n";
        for (size_t i = 1; i < count; ++i) {
            source += "#include \"a.h\"\n";
        }
        source += "\nint outside( int x );\n";
        const auto result = Changed(source, selected, true, style);
        EXPECT_EQ(
            result.PermittedLines,
            (std::vector<formatter::Hunk>{{1, count == 200 ? 200u : 1u}}));
        EXPECT_TRUE(result.Result.Text.starts_with(count == 200
            ? "#include \"a.h\"\n"
            : "#include \"z.h\"\n"));
        EXPECT_TRUE(result.Result.Text.ends_with("\nint outside( int x );\n"));
        EXPECT_TRUE(result.RequiredSkips.empty());
    }
}
TEST(HunkAdapter, NamespaceClosingFootprintAndEOFRemainAtomic)
{
    const std::string source = "namespace n {\nint value;\n}\n";
    const formatter::Hunk all[] = {{1, 3}};
    const formatter::Hunk closeOnly[] = {{3, 3}};
    auto style = HunkStyle();
    style.NamespaceComments = formatter::NamespaceComments::AddMissing;
    style.ShortNamespaceLines = 0;
    const auto accepted = Changed(
        source,
        all,
        false,
        style,
        {.SourceMap = true});
    EXPECT_NE(accepted.Result.Text.find("// namespace n"), std::string::npos);
    const auto headerDenied = Changed(source, closeOnly, false, style);
    EXPECT_EQ(
        headerDenied.Result.Text.find("// namespace n"),
        std::string::npos);
    EXPECT_TRUE(headerDenied.RequiredSkips.empty());
    EXPECT_TRUE(std::any_of(headerDenied.Incomplete.begin(),
        headerDenied.Incomplete.end(),
        [] (const auto& record) {
            return record.Reason ==
                formatter::HunkCoverageReason::StructuralEligibility;
        }));
    const auto eofDenied = Changed(
        source.substr(0, source.size() - 1),
        all,
        false,
        style);
    EXPECT_EQ(eofDenied.Result.Text.find("// namespace n"), std::string::npos);
    EXPECT_FALSE(eofDenied.Result.Text.ends_with('\n'));
    EXPECT_TRUE(eofDenied.RequiredSkips.empty());
    style.ShortNamespaceLines = 20;
    const auto exempt = Changed(source, all, false, style);
    EXPECT_TRUE(exempt.RequiredSkips.empty());
    EXPECT_TRUE(std::none_of(exempt.Incomplete.begin(), exempt.Incomplete.end(),
        [] (const auto& record) {
            return record.Reason ==
                formatter::HunkCoverageReason::StructuralEligibility;
        }));
}
TEST(HunkAdapter, AmbiguityAndRecoveryDoNotFreezeIndependentSelectedDeclaration)
{
    const std::string source =
        "void f() { unknown * value; @; }\nint independent( int x );\n";
    const formatter::Hunk selected[] = {{1, 2}};
    const auto result = Changed(source, selected, true, HunkStyle());
    EXPECT_NE(
        result.Result.Text.find("unknown * value; @;"),
        std::string::npos);
    EXPECT_TRUE(result.Result.Text.ends_with("int independent(int x);\n"));
    EXPECT_TRUE(result.RequiredSkips.empty());
    EXPECT_TRUE(std::any_of(result.Incomplete.begin(), result.Incomplete.end(),
        [] (const auto& record) {
            return record.Reason ==
                formatter::HunkCoverageReason::SemanticProtection;
        }));
}
TEST(HunkAdapter, OptionalCommaInsertionAndRemovalHaveOriginalAuthority)
{
    const std::string source =
        "int data[] = {\n  1,\n  2\n};\nint outside( int x );\n";
    const formatter::Hunk item[] = {{3, 4}};
    const formatter::Hunk partial[] = {{3, 3}};
    const formatter::Hunk closer[] = {{4, 4}};
    auto style = HunkStyle();
    style.InitializersTrailingComma =
        formatter::TrailingCommaPolicy::WhenMultiline;
    const auto inserted = Changed(source, item, false, style, {
        .SourceMap = true,
    });
    EXPECT_NE(inserted.Result.Text.find("2,"), std::string::npos);
    // The lowerer also requires the closer gap as an atomic layout boundary.
    const auto denied = Changed(source, partial, false, style);
    EXPECT_EQ(denied.Result.Text.find("2,"), std::string::npos);
    EXPECT_FALSE(denied.Incomplete.empty());
    EXPECT_TRUE(denied.RequiredSkips.empty());
    EXPECT_EQ(
        Changed(source, closer, false, style).Result.Text.find("2,"),
        std::string::npos);
    const auto existing = source.substr(
        0,
        source.find("2\n")) + "2,\n};\nint outside( int x );\n";
    style.InitializersTrailingComma = formatter::TrailingCommaPolicy::Never;
    EXPECT_NE(
        Changed(existing, closer, false, style).Result.Text.find("2,"),
        std::string::npos);
    const auto removed = Changed(existing, item, false, style, {
        .SourceMap = true,
    });
    EXPECT_EQ(removed.Result.Text.find("2,"), std::string::npos);
    EXPECT_TRUE(removed.Result.Text.ends_with("int outside( int x );\n"));
    HunkInput input(existing);
    const auto tree = input.Tree();
    const auto deniedRemoval = Changed(existing, closer, false, style, {
        .SourceMap = true,
    });
    bool removedIdentity = false;
    for (parser::TokenIndex i = 0; i < tree.TokenCount(); ++i) {
        if (tree.Kind(i) == tokenizer::TokenKind::Operator_Comma &&
            tree.TokenPosition(i).Line == 3)
        {
            EXPECT_FALSE(removed.Result.OriginalTokens[i].Present);
            EXPECT_TRUE(deniedRemoval.Result.OriginalTokens[i].Present);
            removedIdentity = true;
        }
    }
    EXPECT_TRUE(removedIdentity);
    EXPECT_TRUE(std::any_of(inserted.Result.Map.begin(),
        inserted.Result.Map.end(),
        [&] (const auto& map) {
            return map.Generated && map.OriginalToken ==
                document::NoIdentity &&
                std::string_view(inserted.Result.Text).substr(
                    map.Output.Begin,
                    map.Output.End - map.Output.Begin) == ",";
        }));
}
TEST(HunkAdapter,
    IdempotenceRemapsSelectedOriginalIdentitiesAfterAtomicLineInsertion)
{
    const std::string source =
        "int before( int x );\nvoid f(bool x) {\n  if (x) work();\n"
        "  ;\n}\nint after( int x );\n";
    const formatter::Hunk selected[] = {{2, 5}};
    auto style = HunkStyle();
    style.AddMissingControlBraces = true;
    style.RemoveNullStatements = true;
    const auto first = Changed(source, selected, false, style, {
        .SourceMap = true,
    });
    HunkInput original(source);
    const auto tree = original.Tree();
    auto begin = first.Result.Text.size();
    size_t end = 0;
    for (parser::TokenIndex i = 0; i < tree.TokenCount(); ++i) {
        const auto token = tree.Token(i).Text;
        const auto mapped = first.Result.OriginalTokens[i];
        if (mapped.Present && first.EditScope[0].Begin <= token.Begin &&
            token.End <= first.EditScope[0].End)
        {
            begin = std::min<size_t>(begin, mapped.Output.Begin);
            end = std::max<size_t>(end, mapped.Output.End);
        }
    }
    ASSERT_LT(begin, end);
    const auto line = [&] (size_t offset) {
        return 1 + size_t(std::count(
            first.Result.Text.begin(),
            first.Result.Text.begin() + offset,
            '\n'));
    };
    const formatter::Hunk remapped[] = {{line(begin), line(end - 1)}};
    EXPECT_GT(remapped[0].End, selected[0].End);
    const auto second = Changed(first.Result.Text, remapped, false, style);
    EXPECT_EQ(second.Result.Text, first.Result.Text);
}
} // namespace
