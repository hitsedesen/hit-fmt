#include <formatter/structural_edits.hpp>

#include <gtest/gtest.h>

#include <parser/parser.hpp>

#include <tests/golden.hpp>

#include <array>
#include <filesystem>
#include <format>
#include <memory_resource>
#include <string>
namespace {

using namespace formatter;

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

struct File
{
    Input Source;
    parser::SyntaxTree Tree;
    std::pmr::monotonic_buffer_resource Memory;
    std::pmr::monotonic_buffer_resource Scratch;
    SourceAnalysis Safety;

    explicit File(std::string_view source, SourceAnalysisOptions options = {})
        : Source(source)
        , Tree(parser::Parse(Source.View(), [] (parser::ParseError) {}))
        , Safety(BuildSourceAnalysis(Tree, options, Memory, Scratch))
    { }

    StructuralPlan Plan(StructuralOptions options)
    {
        return PlanStructuralEdits(Tree, Safety, options, Memory, Scratch);
    }

    StructuralMaterialization Apply(const StructuralPlan& plan)
    {
        return MaterializeStructuralEdits(Tree, plan, Memory, Scratch);
    }
};

constexpr StructuralOptions All{true, true, true, true, true};
std::string Dump(const StructuralPlan& plan)
{
    std::string result;
    for (size_t i = 0; i < plan.Operations.size(); ++i) {
        auto op = plan.Operations[i];
        result += std::format(
            "action {} kind={} tokens=[{},{})\n",
            i,
            static_cast<unsigned>(op.Kind),
            op.Owner.Begin,
            op.Owner.End);
    }
    for (auto edit : plan.Edits) {
        result += std::format(
            "edit action={} source=[{},{}) edge={} depth={} pieces=",
            edit.Operation,
            edit.Source.Begin,
            edit.Source.End,
            static_cast<unsigned>(edit.Boundary),
            edit.Depth);
        for (auto i = edit.PieceBegin; i < edit.PieceEnd; ++i) {
            const auto& piece = plan.Pieces[i];
            result += piece.FromSource
                ? std::format(
                    "source[{},{}) ",
                    piece.Source.Begin,
                    piece.Source.End)
                : std::format("'{}' ", piece.Literal);
        }
        if (result.ends_with(' ')) {
            result.pop_back();
        }
        result += "\n";
    }
    for (auto skip : plan.Skipped) {
        result += std::format(
            "skip kind={} tokens=[{},{}) reason={}\n",
            static_cast<unsigned>(skip.Action),
            skip.Tokens.Begin,
            skip.Tokens.End,
            static_cast<unsigned>(skip.Reason));
    }
    return result;
}
void CheckMapping(File& file, const StructuralMaterialization& result)
{
    for (parser::TokenIndex i = 0; i < file.Tree.TokenCount(); ++i) {
        auto map = result.OriginalTokens[i];
        if (map.Present) {
            EXPECT_EQ(
                std::string_view(result.Text).substr(
                    map.Output.Begin,
                    map.Output.End - map.Output.Begin),
                file.Tree.TokenText(i));
        } else {
            EXPECT_EQ(
                file.Tree.Kind(i),
                tokenizer::TokenKind::Operator_Semicolon);
        }
    }
    size_t cursor = 0;
    for (const auto& span : result.Provenance) {
        EXPECT_EQ(span.Output.Begin, cursor);
        cursor = span.Output.End;
        if (!span.Generated) {
            EXPECT_EQ(
                std::string_view(result.Text).substr(
                    span.Output.Begin,
                    span.Output.End - span.Output.Begin),
                file.Tree.SourceText(span.Original));
        }
    }
    EXPECT_EQ(cursor, result.Text.size());
    for (const auto& gap : result.GeneratedAdjacencies) {
        EXPECT_LE(gap.Output.Begin, gap.Output.End);
        EXPECT_LE(gap.Output.End, result.Text.size());
    }
}
std::vector<TokenOutputOffset> MapOutput(
    const parser::SyntaxTree& original,
    std::string_view output)
{
    Input padded(output);
    auto tokens = parser::SyntaxTree::TokenizeOnly(padded.View());
    EXPECT_EQ(tokens.TokenCount(), original.TokenCount());
    std::vector<TokenOutputOffset> map(original.TokenCount());
    for (parser::TokenIndex i = 0;
         i < std::min(tokens.TokenCount(), original.TokenCount());
        ++i)
    {
        EXPECT_EQ(tokens.TokenText(i), original.TokenText(i));
        auto r = tokens.Token(i).Text;
        map[i] = {{r.Begin, r.End}, true};
    }
    return map;
}
TEST(StructuralEdits, DiagnosticGolden)
{
    auto directory = std::filesystem::path(__FILE__).parent_path() /
        "testdata/structural-edits";
    auto source = test::golden::ReadFile(directory / "plans.input.cpp");
    File file(source, {.HonorClangFormatDirectives = true});
    auto plan = file.Plan(All);
    auto normalized = file.Apply(plan);
    auto namespaces = PlanNamespaceComments(
        file.Tree,
        file.Safety,
        {.Policy = NamespaceCommentPolicy::AddMissing},
        file.Memory,
        file.Scratch);
    auto comments = ResolveNamespaceComments(
        file.Tree,
        namespaces,
        normalized.Text,
        normalized.OriginalTokens,
        {},
        file.Memory,
        file.Scratch);
    test::golden::ExpectGolden(
        directory / "plans.analysis.txt",
        Dump(plan) + "namespace-output-plan\n" + Dump(comments));
    CheckMapping(file, normalized);
}
TEST(StructuralEdits, NestedControlsKeepDanglingElseAndSingleOriginalPlan)
{
    File file("void f(){if(a) if(b) while(c) x(); else y();}");
    auto plan = file.Plan({.AddMissingControlBraces = true});
    EXPECT_EQ(plan.Operations.size(), 4u);
    file.Scratch.release(); // No plan references may point into scratch.
    auto result = file.Apply(plan);
    EXPECT_EQ(
        result.Text,
        "void f(){if(a) {if(b) {while(c) {x();}} else {y();}}}");
    CheckMapping(file, result);
    // Reparse only in this property test, never in planning/materialization.
    File again(result.Text);
    EXPECT_TRUE(again
        .Plan({.AddMissingControlBraces = true})
        .Edits
        .empty());
}
TEST(StructuralEdits, ElseIfChainAndDoWhile)
{
    File file("void f(){if(a) x(); else if(b) y(); else do x(); while(c);}");
    auto plan = file.Plan({.AddMissingControlBraces = true});
    auto result = file.Apply(plan);
    EXPECT_EQ(
        result.Text,
        "void f(){if(a) {x();} else if(b) {y();} else {do {x();} while(c);}}");
    CheckMapping(file, result);
}
TEST(StructuralEdits, NullBodyIsNotRemovedAndEmptyBlockIsIndependent)
{
    for (bool add : {false, true}) {
        File preserved(
            "void f(){while(a); for(;;); if(a); else ; " "do ; while(a);}");
        auto keep = preserved.Plan(
            {.AddMissingControlBraces = add, .RemoveNullStatements = true});
        EXPECT_TRUE(keep.Edits.empty());
        File file(preserved.Tree.Source());
        auto plan = file.Plan({
            .AddMissingControlBraces = add,
            .EmptyControlBody = true,
            .RemoveNullStatements = true,
        });
        auto result = file.Apply(plan);
        EXPECT_EQ(
            result.Text,
            "void f(){while(a){} for(;;){} if(a){} else {} do {} while(a);}");
        CheckMapping(file, result);
    }
}
TEST(StructuralEdits, LabelsAttributesAndAttachedCommentsAreLocalSkips)
{
    File file("void f(){label: ; switch(a){case 1: ; ;} if(a) [[likely]] x(); "
        "if(b) y(); // attached\nwhile(c) z();}");
    auto plan = file.Plan(All);
    auto result = file.Apply(plan);
    EXPECT_NE(result.Text.find("label: ;"), std::string::npos);
    EXPECT_NE(result.Text.find("case 1: ;"), std::string::npos);
    EXPECT_NE(result.Text.find("if(a) [[likely]] x();"), std::string::npos);
    EXPECT_NE(result.Text.find("if(b) y(); // attached"), std::string::npos);
    EXPECT_NE(result.Text.find("while(c) {z();}"), std::string::npos);
    EXPECT_GE(plan.Skipped.size(), 3u);
    CheckMapping(file, result);
    // LabelStatement ends at the colon. Its required statement can therefore
    // be a sibling of an enclosing control, not a sibling of the label.
    for (const auto& body :
        {
            "if(a) controlled: ; work();",
            "if(a) if(b) nested: work();",
            "while(a) if(b) nestedNull: ; work();",
            "if(a) first: second: ; work();",
        })
    {
        std::string source = "void f(){" + std::string(body) +
            " while(c) safe(); ;}";
        File controlled(source);
        auto edits = controlled.Plan(All);
        auto changed = controlled.Apply(edits);
        EXPECT_EQ(
            std::string_view(changed.Text),
            "void f(){" + std::string(body) + " while(c) {safe();} }");
        EXPECT_TRUE(std::any_of(
            edits.Skipped.begin(),
            edits.Skipped.end(),
            [] (auto diagnostic) {
                return diagnostic.Reason == StructuralSkip::LabelBody;
            }));
        CheckMapping(controlled, changed);
    }
}
TEST(StructuralEdits,
    NullRemovalPreservesRequiredExportAndStatementExpressionBodies)
{
    File file("export ; export {; ;} int f(){return ({1; ;});}");
    auto plan = file
        .Plan({.RemoveNullStatements = true, .RemoveNullDeclarations = true});
    EXPECT_EQ(
        file.Apply(plan).Text,
        "export ; export { } int f(){return ({1; ;});}");
    EXPECT_TRUE(std::any_of(
        plan.Skipped.begin(),
        plan.Skipped.end(),
        [] (auto diagnostic) {
            return diagnostic.Reason == StructuralSkip::RequiredBody;
        }));
}
TEST(StructuralEdits, ProtectedMacroDisabledRecoveryAndDirectiveBoundaries)
{
    std::array names{std::string_view("KEEP")};
    File file(
        "void f(){if(a) KEEP(x);\n// clang-format off\nwhile(b) x();\n// "
        "clang-format on\nif(c)\n#if FLAG\nx();\n#endif\nwhile(d) z(); "
        "if( ) ;}",
        {
            .HonorClangFormatDirectives = true,
            .WhitespaceSensitiveMacros = names,
        });
    auto plan = file.Plan(All);
    auto result = file.Apply(plan);
    EXPECT_NE(result.Text.find("if(a) KEEP(x);"), std::string::npos);
    EXPECT_NE(result.Text.find("while(b) x();"), std::string::npos);
    EXPECT_NE(
        result.Text.find("if(c)\n#if FLAG\nx();\n#endif"),
        std::string::npos);
    EXPECT_NE(result.Text.find("while(d) {z();}"), std::string::npos);
    for (const auto& region : file.Safety.Protection()) {
        if (region.Reasons == ProtectionReason::Directive) {
            continue;
        }
        EXPECT_NE(
            result.Text.find(file.Tree.SourceText(region.Source)),
            std::string::npos);
    }
    CheckMapping(file, result);
}
TEST(StructuralEdits, ExactIncludeLineAtomsAndConditionalCommentBarriers)
{
    File file("#include <z.h>\r\n#include \"a.h\"\r\n#if FLAG\r\n#include "
        "<d.h>\r\n#include <c.h>\r\n#endif\r\n// belongs to "
        "b\r\n#include <b.h>\r\n#include <a.h>\r\n");
    auto groups = DiscoverIncludeGroups(
        file.Tree,
        file.Safety,
        file.Memory,
        file.Scratch);
    ASSERT_EQ(groups.size(), 2u);
    auto plan = file.Plan({.SortIncludes = true});
    EXPECT_EQ(plan.Operations.size(), 2u);
    ASSERT_EQ(plan.Skipped.size(), 1u);
    EXPECT_EQ(plan.Skipped.front().Reason, StructuralSkip::CommentAttached);
    for (size_t i = 0; i < groups.size(); ++i) {
        EXPECT_EQ(groups[i].Tokens.Begin, plan.Operations[i].Owner.Begin);
        EXPECT_EQ(groups[i].Tokens.End, plan.Operations[i].Owner.End);
        EXPECT_EQ(groups[i].Source.Begin, plan.Edits[i].Source.Begin);
        EXPECT_EQ(groups[i].Source.End, plan.Edits[i].Source.End);
    }
    auto result = file.Apply(plan);
    EXPECT_EQ(
        result.Text,
        "#include \"a.h\"\r\n#include <z.h>\r\n#if "
        "FLAG\r\n#include <c.h>\r\n#include <d.h>\r\n#endif\r\n// "
        "belongs to b\r\n#include <b.h>\r\n#include <a.h>\r\n");
    for (const auto& edit : plan.Edits) {
        EXPECT_EQ(
            plan.Operations[edit.Operation].Kind,
            StructuralAction::IncludeOrder);
        EXPECT_EQ(edit.PieceEnd - edit.PieceBegin, 2u);
    }
    CheckMapping(file, result);
}
TEST(StructuralEdits, IncludeIndentationAndDigraphMarkersStayWithExactAtoms)
{
    File file("  %:include <z.h>\r\n\t# include \"a.h\"\r\n");
    auto plan = file.Plan({.SortIncludes = true});
    ASSERT_EQ(plan.Operations.size(), 1u);
    auto groups = DiscoverIncludeGroups(
        file.Tree,
        file.Safety,
        file.Memory,
        file.Scratch);
    ASSERT_EQ(groups.size(), 1u);
    EXPECT_EQ(groups.front().Source.Begin, 0u);
    EXPECT_EQ(groups.front().Source.End, file.Source.Size);
    file.Scratch.release();
    auto result = file.Apply(plan);
    EXPECT_EQ(result.Text, "\t# include \"a.h\"\r\n  %:include <z.h>\r\n");
    CheckMapping(file, result);
}
TEST(StructuralEdits, IncludeDiscoveryContainsSortedGroupsAndExactPermutations)
{
    File file("  #include \"a.h\"\n\t#include <z.h>\n\n"
        " #include <z.h>\n\t#include \"a.h\"\n#include \"a.h\"\n");
    auto groups = DiscoverIncludeGroups(
        file.Tree,
        file.Safety,
        file.Memory,
        file.Scratch);
    ASSERT_EQ(groups.size(), 2u);
    EXPECT_EQ(groups[0].Tokens.Begin, 0u);
    EXPECT_EQ(groups[0].Tokens.End, 2u);
    EXPECT_EQ(groups[1].Tokens.Begin, 2u);
    EXPECT_EQ(groups[1].Tokens.End, 5u);
    EXPECT_EQ(groups[0].Source.Begin, 0u);
    EXPECT_EQ(groups[0].Source.End + 1, groups[1].Source.Begin);
    EXPECT_EQ(groups[1].Source.End, file.Source.Size);
    auto plan = file.Plan({.SortIncludes = true});
    ASSERT_EQ(plan.Operations.size(), 1u); // Sorted discovery is not an edit.
    EXPECT_TRUE(plan.Skipped.empty());
    EXPECT_EQ(plan.Operations.front().Owner.Begin, groups[1].Tokens.Begin);
    EXPECT_EQ(plan.Edits.front().Source.Begin, groups[1].Source.Begin);
    EXPECT_EQ(plan.Edits.front().Source.End, groups[1].Source.End);
    std::vector<parser::SourceRange> atoms;
    for (const auto& piece : plan.Pieces) {
        EXPECT_TRUE(piece.FromSource);
        EXPECT_TRUE(piece.Literal.empty());
        atoms.push_back(piece.Source);
    }
    std::sort(
        atoms.begin(),
        atoms.end(),
        [] (auto a, auto b) {
            return a.Begin < b.Begin;
        });
    auto cursor = groups[1].Source.Begin;
    for (auto atom : atoms) {
        EXPECT_EQ(atom.Begin, cursor);
        EXPECT_TRUE(file
            .Tree
            .SourceText(atom)
            .ends_with('\n'));
        cursor = atom.End;
    }
    EXPECT_EQ(cursor, groups[1].Source.End);
    auto result = file.Apply(plan);
    EXPECT_EQ(result.Text.size(), file.Source.Size);
    CheckMapping(file, result);
    File sorted(result.Text);
    EXPECT_EQ(
        DiscoverIncludeGroups(
            sorted.Tree,
            sorted.Safety,
            sorted.Memory,
            sorted.Scratch).size(),
        2u);
    EXPECT_TRUE(sorted
        .Plan({.SortIncludes = true})
        .Operations
        .empty());
}
TEST(StructuralEdits, IncludeDiscoveryRespectsCompleteGroupBoundaries)
{
    // Every separator is a fence, not a spelling-based eligibility shortcut.
    for (auto separator :
        {
            "\n",
            "// attached\n",
            "/* attached */\n",
            "#define VALUE 1\n",
            "#include HEADER\n",
            "#include <>\n",
            "#include <bad.h> // tail\n",
            "#include \\\n<continued.h>\n",
            "opaque();\n",
        })
    {
        std::string first = "#include <a.h>\n#include <b.h>\n";
        std::string second = "  %:include <d.h>\n\t#include <c.h>\n";
        File file(first + separator + second);
        auto groups = DiscoverIncludeGroups(
            file.Tree,
            file.Safety,
            file.Memory,
            file.Scratch);
        bool comment = std::string_view(separator).starts_with("//") ||
            std::string_view(separator).starts_with("/*");
        bool splice =
            std::string_view(separator).find('\\') != std::string_view::npos;
        if (splice) {
            // The splice itself stays protected. Distinct touching reason masks
            // do not contaminate complete groups wholly on either side of it.
            const auto begin = first.size();
            const auto end = begin + std::string_view(separator).size();
            ASSERT_EQ(groups.size(), 2u);
            EXPECT_EQ(groups[0].Source.End, begin);
            EXPECT_EQ(groups[1].Source.Begin, end);
            for (const auto& group : groups) {
                EXPECT_TRUE(
                    group.Source.End <= begin || end <= group.Source.Begin);
            }
            EXPECT_TRUE(std::any_of(
                file
                    .Safety
                    .Protection()
                    .begin(),
                file
                    .Safety
                    .Protection()
                    .end(),
                [&] (const auto& range) {
                    return range.Source.Begin < end &&
                        begin < range.Source.End &&
                        (uint16_t(range.Reasons) &
                            uint16_t(ProtectionReason::LineSplice));
                }));
        }
        ASSERT_EQ(groups.size(), comment ? 1u : 2u) << separator;
        EXPECT_EQ(groups.front().Source.Begin, 0u);
        EXPECT_EQ(groups.front().Source.End, first.size());
        if (!comment) {
            EXPECT_EQ(
                groups.back().Source.Begin,
                first.size() + std::string_view(separator).size());
            EXPECT_EQ(groups.back().Source.End, file.Source.Size);
        }
        auto plan = file.Plan({.SortIncludes = true});
        EXPECT_EQ(plan.Operations.size(), comment ? 0u : 1u) << separator;
        EXPECT_EQ(plan.Skipped.size(), comment ? 1u : 0u) << separator;
        if (comment) {
            EXPECT_EQ(
                plan.Skipped.front().Reason,
                StructuralSkip::CommentAttached);
        }
    }
    for (auto text :
        {
            "",
            "#include <a.h>\n",
            "#include <b.h>\n#include <a.h>",
            "#include <b.h>\r\n#include <a.h>\n",
            "#include \\\n<z.h>\n#include <a.h>\n",
            "#include <z.h> // comment\n#include <a.h>\n",
        })
    {
        File file(text);
        EXPECT_TRUE(DiscoverIncludeGroups(
            file.Tree,
            file.Safety,
            file.Memory,
            file.Scratch).empty());
        EXPECT_TRUE(file
            .Plan({.SortIncludes = true})
            .Edits
            .empty());
    }
    File mixed("#include <b.h>\r\n#include <a.h>\r\n"
        "#include <b.h>\n#include <a.h>\n#include <final.h>");
    auto groups = DiscoverIncludeGroups(
        mixed.Tree,
        mixed.Safety,
        mixed.Memory,
        mixed.Scratch);
    ASSERT_EQ(groups.size(), 2u);
    EXPECT_EQ(groups[0].Source.End, groups[1].Source.Begin);
    EXPECT_EQ(groups[0].Tokens.End, groups[1].Tokens.Begin);
    EXPECT_EQ(groups[1].Tokens.End, mixed.Tree.TokenCount() - 1);
    auto plan = mixed.Plan({.SortIncludes = true});
    EXPECT_EQ(plan.Operations.size(), 2u);
    CheckMapping(mixed, mixed.Apply(plan));
}
TEST(StructuralEdits, IncludeDiscoveryNeverWeakensProtectionOrScope)
{
    std::string text = "#include <z.h>\n#include <a.h>\n";
    for (auto reason :
        {
            ProtectionReason::Directive,
            ProtectionReason::Explicit,
            ProtectionReason::Disabled,
            ProtectionReason::SensitiveMacro,
            ProtectionReason::Recovery,
            ProtectionReason::Ambiguous,
            ProtectionReason::Recovery | ProtectionReason::Ambiguous,
            ProtectionReason::LineSplice,
            ProtectionReason::HeaderName,
            ProtectionReason::OutsideEditScope,
        })
    {
        std::array inherited{ProtectedSourceRange{{1, 2}, reason}};
        File file(text, {.InheritedProtection = inherited});
        auto groups = DiscoverIncludeGroups(
            file.Tree,
            file.Safety,
            file.Memory,
            file.Scratch);
        auto plan = file.Plan({.SortIncludes = true});
        bool safe = reason == ProtectionReason::Directive;
        EXPECT_EQ(groups.size(), safe ? 1u : 0u);
        EXPECT_EQ(plan.Operations.size(), safe ? 1u : 0u);
        EXPECT_EQ(plan.Skipped.size(), safe ? 0u : 1u);
        if (!safe) {
            EXPECT_EQ(plan.Skipped.front().Reason, StructuralSkip::Protected);
            EXPECT_EQ(plan.Skipped.front().Tokens.Begin, 0u);
            EXPECT_EQ(plan.Skipped.front().Tokens.End, 2u);
            EXPECT_EQ(
                file
                    .Safety
                    .Protection()
                    .front()
                    .Reasons,
                ProtectionReason::Directive | reason);
        }
    }
    File original(text);
    ASSERT_EQ(
        DiscoverIncludeGroups(
            original.Tree,
            original.Safety,
            original.Memory,
            original.Scratch).size(),
        1u);
    for (parser::SourceRange scope :
        {
            parser::SourceRange{
                0,
                static_cast<parser::SourceOffset>(text.size()),
            },
            parser::SourceRange{
                0,
                static_cast<parser::SourceOffset>(text.size() - 1),
            },
            parser::SourceRange{0, 1},
            parser::SourceRange{1, 2},
        })
    {
        std::array selection{scope};
        auto safety = BuildSourceAnalysis(
            original.Tree,
            {.EditScope = selection},
            original.Memory,
            original.Scratch);
        bool whole = scope.End == text.size();
        auto discovered = DiscoverIncludeGroups(
            original.Tree,
            safety,
            original.Memory,
            original.Scratch);
        auto scopedPlan = PlanStructuralEdits(
            original.Tree,
            safety,
            {.SortIncludes = true},
            original.Memory,
            original.Scratch);
        EXPECT_EQ(discovered.size(), whole ? 1u : 0u);
        EXPECT_EQ(scopedPlan.Operations.size(), whole ? 1u : 0u);
        EXPECT_EQ(scopedPlan.Skipped.size(), whole ? 0u : 1u);
        if (!whole) {
            EXPECT_EQ(
                scopedPlan.Skipped.front().Reason,
                StructuralSkip::Protected);
        }
    }
    // Exact atom authority excludes the separating blank LF. Touching different
    // reason masks keep their locations; real overlapping protection
    // still wins.
    std::array firstGroupOnly{parser::SourceRange{
        0, static_cast<parser::SourceOffset>(text.size())}};
    File disjoint(text + "\n" + text, {.EditScope = firstGroupOnly});
    auto selected = DiscoverIncludeGroups(
        disjoint.Tree,
        disjoint.Safety,
        disjoint.Memory,
        disjoint.Scratch);
    ASSERT_EQ(selected.size(), 1u);
    EXPECT_EQ(selected.front().Source.Begin, 0u);
    EXPECT_EQ(selected.front().Source.End, text.size());
    auto disjointPlan = disjoint.Plan({.SortIncludes = true});
    ASSERT_EQ(disjointPlan.Operations.size(), 1u);
    ASSERT_EQ(disjointPlan.Skipped.size(), 1u);
    EXPECT_EQ(disjointPlan.Skipped.front().Reason, StructuralSkip::Protected);
    EXPECT_EQ(disjointPlan.Skipped.front().Tokens.Begin, 2u);
    // Semantic exclusion can independently remove the selected group too.
    std::array excluded{parser::SourceRange{0, 1}};
    File excludedSelection(
        text + "\n" + text,
        {.ImmutableRanges = excluded, .EditScope = firstGroupOnly});
    EXPECT_TRUE(DiscoverIncludeGroups(
        excludedSelection.Tree,
        excludedSelection.Safety,
        excludedSelection.Memory,
        excludedSelection.Scratch).empty());
    auto excludedPlan = excludedSelection.Plan({.SortIncludes = true});
    EXPECT_TRUE(excludedPlan.Operations.empty());
    EXPECT_EQ(excludedPlan.Skipped.size(), 2u);
    File empty(text, {.EditScope = std::span<const parser::SourceRange>{}});
    EXPECT_TRUE(DiscoverIncludeGroups(
        empty.Tree,
        empty.Safety,
        empty.Memory,
        empty.Scratch).empty());
    EXPECT_TRUE(empty
        .Plan({.SortIncludes = true})
        .Operations
        .empty());
    File disabled(
        "// clang-format off\n" + text + "// clang-format on\n",
        {.HonorClangFormatDirectives = true});
    EXPECT_TRUE(DiscoverIncludeGroups(
        disabled.Tree,
        disabled.Safety,
        disabled.Memory,
        disabled.Scratch).empty());
    EXPECT_TRUE(disabled
        .Plan({.SortIncludes = true})
        .Edits
        .empty());
    File other(text);
    EXPECT_THROW(
        (void)DiscoverIncludeGroups(
            original.Tree,
            other.Safety,
            original.Memory,
            original.Scratch),
        std::invalid_argument);
    SourceAnalysis unbuilt(original.Memory);
    EXPECT_THROW(
        (void)DiscoverIncludeGroups(
            original.Tree,
            unbuilt,
            original.Memory,
            original.Scratch),
        std::invalid_argument);
}
TEST(StructuralEdits, IncludeDiscoveryOwnsDescriptorsForManyDisjointGroups)
{
    constexpr size_t Count = 256;
    std::pmr::monotonic_buffer_resource memory;
    auto groups = [&] {
        std::string source;
        for (size_t i = 0; i < Count; ++i) {
            source += i % 2
                ? "#include <z.h>\n#include <a.h>\n\n"
                : "#include <a.h>\n#include <z.h>\n\n";
        }
        File file(source);
        std::pmr::monotonic_buffer_resource scratch;
        auto result =
            DiscoverIncludeGroups(file.Tree, file.Safety, memory, scratch);
        scratch.release();
        auto plan = file.Plan({.SortIncludes = true});
        EXPECT_EQ(plan.Operations.size(), Count / 2);
        EXPECT_TRUE(plan.Skipped.empty());
        for (size_t i = 0; i < plan.Operations.size(); ++i) {
            EXPECT_EQ(
                plan.Operations[i].Owner.Begin,
                result[2 * i + 1].Tokens.Begin);
            EXPECT_EQ(
                plan.Edits[i].Source.Begin,
                result[2 * i + 1].Source.Begin);
            EXPECT_EQ(plan.Edits[i].Source.End, result[2 * i + 1].Source.End);
        }
        return result; // Source, tree, analysis and scratch all die here.
    }();
    ASSERT_EQ(groups.size(), Count);
    for (size_t i = 0; i < Count; ++i) {
        EXPECT_EQ(groups[i].Tokens.Begin, 2 * i);
        EXPECT_EQ(groups[i].Tokens.End, 2 * i + 2);
        EXPECT_EQ(groups[i].Source.Begin, 31 * i);
        EXPECT_EQ(groups[i].Source.End, 31 * i + 30);
    }
}
TEST(StructuralEdits, OwnershipValidationRejectsOverlapsAndCoownedInsertions)
{
    File file("void f(){if(a) work();}");
    auto plan = file.Plan({.AddMissingControlBraces = true});
    ASSERT_EQ(plan.Edits.size(), 2u);
    plan.Edits.push_back(plan.Edits.back());
    EXPECT_THROW((void)file.Apply(plan), std::invalid_argument);
    auto partial = file.Plan({.AddMissingControlBraces = true});
    partial.Edits.pop_back();
    EXPECT_THROW((void)file.Apply(partial), std::invalid_argument);
    auto split = file.Plan({.AddMissingControlBraces = true});
    split.Edits.front().Source.Begin += 1;
    split.Edits.front().Source.End += 1;
    EXPECT_THROW((void)file.Apply(split), std::invalid_argument);
    File declarations(";;");
    auto deletion = declarations.Plan({.RemoveNullDeclarations = true});
    ASSERT_EQ(deletion.Edits.size(), 2u);
    deletion.Edits[1].Source.Begin = deletion.Edits[0].Source.Begin;
    EXPECT_THROW((void)declarations.Apply(deletion), std::invalid_argument);
}
TEST(StructuralEdits, DeepNestingUsesOnePlanAndDeterministicCloseOrdering)
{
    std::string source = "void f(){";
    for (size_t i = 0; i < 128; ++i) {
        source += "if(a) ";
    }
    source += "x();}";
    File file(source);
    auto plan = file.Plan({.AddMissingControlBraces = true});
    EXPECT_EQ(plan.Operations.size(), 128u);
    auto result = file.Apply(plan);
    EXPECT_EQ(std::count(result.Text.begin(), result.Text.end(), '{'), 129);
    CheckMapping(file, result);
}
TEST(StructuralEdits, NamespaceThresholdUsesFinalInteriorLinesNotSource)
{
    File file("namespace n {int x;}");
    auto plan = PlanNamespaceComments(
        file.Tree,
        file.Safety,
        {
            .Policy = NamespaceCommentPolicy::AddMissing,
            .ShortNamespaceLines = 1,
        },
        file.Memory,
        file.Scratch);
    ASSERT_EQ(plan.Namespaces.size(), 1u);
    for (const auto& output :
        {
            std::string("namespace n {\nint x;\n}\n"),
            std::string("namespace n {\n\nint x;\n}\n"),
        })
    {
        auto map = MapOutput(file.Tree, output);
        auto resolved = ResolveNamespaceComments(
            file.Tree,
            plan,
            output,
            map,
            {},
            file.Memory,
            file.Scratch);
        auto materialized =
            MaterializeNamespaceComments(output, resolved, file.Memory);
        if (output.find("\n\n") == std::string::npos) {
            EXPECT_TRUE(resolved.Edits.empty());
            EXPECT_EQ(
                resolved.Skipped.back().Reason,
                StructuralSkip::ShortNamespace);
        } else {
            EXPECT_EQ(
                materialized,
                "namespace n {\n\nint x;\n} // namespace n\n");
        }
    }
}
TEST(StructuralEdits, NamespaceZeroDisablesExemptionAndNestedIdentities)
{
    File file("namespace a {namespace b::c {}}\n");
    auto plan = PlanNamespaceComments(
        file.Tree,
        file.Safety,
        {.Policy = NamespaceCommentPolicy::AddMissing},
        file.Memory,
        file.Scratch);
    EXPECT_EQ(plan.Namespaces.size(), 2u);
    std::string output = "namespace a {\nnamespace b::c {}\n}\n";
    auto map = MapOutput(file.Tree, output);
    auto resolved = ResolveNamespaceComments(
        file.Tree,
        plan,
        output,
        map,
        {},
        file.Memory,
        file.Scratch);
    EXPECT_EQ(
        MaterializeNamespaceComments(output, resolved, file.Memory),
        "namespace a {\nnamespace b::c {} // namespace b::c\n} // namespace "
        "a\n");
    // The parser classifies inline namespace as a generic declaration:
    // a typed keyword only authorizes a diagnostic, never a fabricated
    // identity.
    File recovered("inline namespace a {}\n");
    auto opaque = PlanNamespaceComments(
        recovered.Tree,
        recovered.Safety,
        {.Policy = NamespaceCommentPolicy::AddMissing},
        recovered.Memory,
        recovered.Scratch);
    EXPECT_TRUE(opaque.Namespaces.empty());
    ASSERT_FALSE(opaque.Skipped.empty());
    EXPECT_EQ(opaque.Skipped.front().Reason, StructuralSkip::Incomplete);
}
TEST(StructuralEdits, NamespaceFixRemoveKeepCrLfAndIgnoreThreshold)
{
    for (auto policy :
        {NamespaceCommentPolicy::Fix, NamespaceCommentPolicy::Remove})
    {
        File file("namespace a {} // old\r\nint x;\r\n");
        auto plan = PlanNamespaceComments(
            file.Tree,
            file.Safety,
            {.Policy = policy, .ShortNamespaceLines = 100},
            file.Memory,
            file.Scratch);
        auto output = file.Tree.Source();
        auto map = MapOutput(file.Tree, output);
        auto resolved = ResolveNamespaceComments(
            file.Tree,
            plan,
            output,
            map,
            {},
            file.Memory,
            file.Scratch);
        ASSERT_EQ(resolved.Edits.size(), 1u);
        EXPECT_EQ(
            MaterializeNamespaceComments(output, resolved, file.Memory),
            policy == NamespaceCommentPolicy::Fix
            ? "namespace a {} // namespace a\r\nint x;\r\n"
            : "namespace a {}\r\nint x;\r\n");
    }
}
TEST(StructuralEdits, AnonymousNamespaceNoOpFixAndOriginalProtection)
{
    File anonymous("namespace {}\n");
    auto plan = PlanNamespaceComments(
        anonymous.Tree,
        anonymous.Safety,
        {.Policy = NamespaceCommentPolicy::AddMissing},
        anonymous.Memory,
        anonymous.Scratch);
    auto mapping = MapOutput(anonymous.Tree, anonymous.Tree.Source());
    auto resolved = ResolveNamespaceComments(
        anonymous.Tree,
        plan,
        anonymous.Tree.Source(),
        mapping,
        {},
        anonymous.Memory,
        anonymous.Scratch);
    EXPECT_EQ(
        MaterializeNamespaceComments(
            anonymous.Tree.Source(),
            resolved,
            anonymous.Memory),
        "namespace {} // namespace\n");
    File correct("namespace n {} // namespace n\n");
    auto fix = PlanNamespaceComments(
        correct.Tree,
        correct.Safety,
        {.Policy = NamespaceCommentPolicy::Fix},
        correct.Memory,
        correct.Scratch);
    auto correctMap = MapOutput(correct.Tree, correct.Tree.Source());
    EXPECT_TRUE(ResolveNamespaceComments(
        correct.Tree,
        fix,
        correct.Tree.Source(),
        correctMap,
        {},
        correct.Memory,
        correct.Scratch).Edits.empty());
    File disabled(
        "// clang-format off\nnamespace n {} // " "keep\n// clang-format on\n",
        {.HonorClangFormatDirectives = true});
    auto blocked = PlanNamespaceComments(
        disabled.Tree,
        disabled.Safety,
        {.Policy = NamespaceCommentPolicy::Remove},
        disabled.Memory,
        disabled.Scratch);
    EXPECT_TRUE(blocked.Namespaces.empty());
    ASSERT_FALSE(blocked.Skipped.empty());
    EXPECT_EQ(blocked.Skipped.front().Reason, StructuralSkip::Protected);
}
TEST(StructuralEdits, NamespaceCannotSwallowFollowingCodeCommentsOrProtection)
{
    for (const auto& text :
        {"namespace n {} int x;\n", "namespace n {} /* owner */\n"})
    {
        File file(text);
        auto plan = PlanNamespaceComments(
            file.Tree,
            file.Safety,
            {.Policy = NamespaceCommentPolicy::AddMissing},
            file.Memory,
            file.Scratch);
        auto map = MapOutput(file.Tree, text);
        auto resolved = ResolveNamespaceComments(
            file.Tree,
            plan,
            text,
            map,
            {},
            file.Memory,
            file.Scratch);
        EXPECT_TRUE(resolved.Edits.empty());
        ASSERT_FALSE(resolved.Skipped.empty());
        EXPECT_EQ(
            resolved.Skipped.back().Reason,
            StructuralSkip::FollowingText);
    }
    File file("namespace n {}\n");
    auto plan = PlanNamespaceComments(
        file.Tree,
        file.Safety,
        {.Policy = NamespaceCommentPolicy::AddMissing},
        file.Memory,
        file.Scratch);
    auto map = MapOutput(file.Tree, file.Tree.Source());
    auto close = plan.Namespaces.front().Close;
    std::array protection{
        parser::SourceRange{map[close].Output.End, map[close].Output.End + 1}};
    auto resolved = ResolveNamespaceComments(
        file.Tree,
        plan,
        file.Tree.Source(),
        map,
        protection,
        file.Memory,
        file.Scratch);
    EXPECT_TRUE(resolved.Edits.empty());
    map[close].Present = false;
    auto invalid = ResolveNamespaceComments(
        file.Tree,
        plan,
        file.Tree.Source(),
        map,
        {},
        file.Memory,
        file.Scratch);
    EXPECT_EQ(invalid.Skipped.back().Reason, StructuralSkip::InvalidMapping);
    map[close].Present = true;
    map[plan.Namespaces.front().Header.Begin + 1].Present = false;
    auto invalidHeader = ResolveNamespaceComments(
        file.Tree,
        plan,
        file.Tree.Source(),
        map,
        {},
        file.Memory,
        file.Scratch);
    EXPECT_EQ(
        invalidHeader.Skipped.back().Reason,
        StructuralSkip::InvalidMapping);
    File braces("namespace n {\nvoid inside() {}\n}\n"
        "void after() {}\nnamespace safe {}\n");
    auto identities = PlanNamespaceComments(
        braces.Tree,
        braces.Safety,
        {.Policy = NamespaceCommentPolicy::AddMissing},
        braces.Memory,
        braces.Scratch);
    ASSERT_EQ(identities.Namespaces.size(), 2u);
    std::vector<parser::TokenIndex> closers;
    for (parser::TokenIndex i = 0; i < braces.Tree.TokenCount(); ++i) {
        if (braces.Tree.Kind(i) == tokenizer::TokenKind::Operator_CloseBrace) {
            closers.push_back(i);
        }
    }
    ASSERT_EQ(closers.size(), 4u);
    auto namespaceClose = identities.Namespaces.front().Close;
    ASSERT_EQ(namespaceClose, closers[1]);
    for (auto functionClose : {closers[0], closers[2]}) {
        auto swapped = MapOutput(braces.Tree, braces.Tree.Source());
        // All spellings still match: identity must be checked using the
        // original interior and outside-neighbor order, not brace spelling.
        std::swap(swapped[namespaceClose], swapped[functionClose]);
        auto stale = ResolveNamespaceComments(
            braces.Tree,
            identities,
            braces.Tree.Source(),
            swapped,
            {},
            braces.Memory,
            braces.Scratch);
        ASSERT_EQ(stale.Edits.size(), 1u);
        ASSERT_EQ(stale.Skipped.size(), 1u);
        EXPECT_EQ(stale.Skipped.front().Reason, StructuralSkip::InvalidMapping);
        EXPECT_EQ(
            stale.Skipped.front().Tokens.Begin,
            identities.Namespaces.front().Owner.Begin);
        EXPECT_EQ(
            MaterializeNamespaceComments(
                braces.Tree.Source(),
                stale,
                braces.Memory),
            "namespace n {\nvoid inside() {}\n}\n"
            "void after() {}\nnamespace safe {} // namespace safe\n");
    }
    File normalized("namespace n {\n#include <z.h>\n#include <a.h>\n;\n"
        "void f() {;}\n}\n;\nnamespace safe {}\n");
    auto originalNames = PlanNamespaceComments(
        normalized.Tree,
        normalized.Safety,
        {.Policy = NamespaceCommentPolicy::AddMissing},
        normalized.Memory,
        normalized.Scratch);
    auto normalization = normalized.Plan({
        .RemoveNullStatements = true,
        .RemoveNullDeclarations = true,
        .SortIncludes = true,
    });
    auto changed = normalized.Apply(normalization);
    auto comments = ResolveNamespaceComments(
        normalized.Tree,
        originalNames,
        changed.Text,
        changed.OriginalTokens,
        {},
        normalized.Memory,
        normalized.Scratch);
    EXPECT_EQ(comments.Edits.size(), 2u);
    EXPECT_TRUE(comments.Skipped.empty());
    bool deleted = false, reordered = false;
    parser::SourceOffset previousDirective = 0;
    for (parser::TokenIndex i = 0; i < normalized.Tree.TokenCount(); ++i) {
        auto offset = changed.OriginalTokens[i];
        deleted |= !offset.Present;
        if (normalized.Tree.Kind(i) ==
            tokenizer::TokenKind::PreprocessingDirective)
        {
            reordered |= offset.Output.Begin < previousDirective;
            previousDirective = offset.Output.Begin;
        }
    }
    EXPECT_TRUE(deleted);
    EXPECT_TRUE(reordered);
    CheckMapping(normalized, changed);
}

} // namespace
