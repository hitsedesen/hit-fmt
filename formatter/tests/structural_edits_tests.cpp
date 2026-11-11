#include <formatter/structural_edits.hpp>
#include <formatter/structural_edits_internal.hpp>

#include <gtest/gtest.h>

#include <parser/parser.hpp>

#include <tests/golden.hpp>
#include <tests/input.hpp>

#include <array>
#include <filesystem>
#include <format>
#include <memory_resource>
#include <string>
namespace {

using namespace formatter;

using test::Input;

std::string Dump(const StructuralPlan& plan);

struct File
{
    Input Source;
    parser::SyntaxTree Tree;
    std::pmr::monotonic_buffer_resource Memory;
    std::pmr::monotonic_buffer_resource Scratch;
    SourceAnalysis Safety;

    explicit File(std::string_view source, SourceAnalysisOptions options = {})
        : Source(source)
        , Tree(parser::Parse(Source.View(), [] (parser::ParseError) { }))
        , Safety(BuildSourceAnalysis(Tree, options, Memory, Scratch))
    { }

    StructuralPlan Plan(StructuralOptions options)
    {
        auto plan = PlanStructuralEdits(Tree, Safety, options, Memory, Scratch);
        auto certified = detail::PlanStructuralEditsCertified(Tree, Safety, options, Memory, Scratch);
        EXPECT_EQ(Dump(certified), Dump(plan));
        return plan;
    }

    StructuralMaterialization Apply(const StructuralPlan& plan)
    {
        auto full = MaterializeStructuralEdits(Tree, plan, Memory, Scratch);
        std::pmr::vector<detail::CopiedTokenRun> copied(&Memory);
        auto compact = detail::MaterializeStructuralEditsCompact(Tree, plan, Memory, Scratch, copied);
        EXPECT_EQ(compact.Text, full.Text);
        EXPECT_TRUE(compact.OriginalTokens.empty());
        EXPECT_EQ(compact.Provenance.size(), full.Provenance.size());

        for (size_t i = 0; i < std::min(compact.Provenance.size(), full.Provenance.size()); ++i) {
            const auto& actual = compact.Provenance[i];
            const auto& expected = full.Provenance[i];
            EXPECT_EQ(actual.Output.Begin, expected.Output.Begin);
            EXPECT_EQ(actual.Output.End, expected.Output.End);
            EXPECT_EQ(actual.Original.Begin, expected.Original.Begin);
            EXPECT_EQ(actual.Original.End, expected.Original.End);
            EXPECT_EQ(actual.Operation, expected.Operation);
            EXPECT_EQ(actual.Generated, expected.Generated);
        }

        EXPECT_EQ(compact.GeneratedAdjacencies.size(), full.GeneratedAdjacencies.size());

        for (size_t i = 0; i < std::min(compact.GeneratedAdjacencies.size(), full.GeneratedAdjacencies.size()); ++i) {
            EXPECT_EQ(compact.GeneratedAdjacencies[i].Output.Begin, full.GeneratedAdjacencies[i].Output.Begin);
            EXPECT_EQ(compact.GeneratedAdjacencies[i].Output.End, full.GeneratedAdjacencies[i].Output.End);
            EXPECT_EQ(compact.GeneratedAdjacencies[i].Operation, full.GeneratedAdjacencies[i].Operation);
        }

        detail::MaterializeOriginalTokenOffsets(Tree, compact, copied);
        EXPECT_EQ(compact.OriginalTokens.size(), full.OriginalTokens.size());

        for (size_t i = 0; i < std::min(compact.OriginalTokens.size(), full.OriginalTokens.size()); ++i) {
            EXPECT_EQ(compact.OriginalTokens[i].Present, full.OriginalTokens[i].Present);
            EXPECT_EQ(compact.OriginalTokens[i].Output.Begin, full.OriginalTokens[i].Output.Begin);
            EXPECT_EQ(compact.OriginalTokens[i].Output.End, full.OriginalTokens[i].Output.End);
        }

        return full;
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
                ? std::format("source[{},{}) ", piece.Source.Begin, piece.Source.End)
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

std::vector<TokenOutputOffset> MapOutput(const parser::SyntaxTree& original, std::string_view output)
{
    Input padded(output);
    auto tokens = parser::Parse(padded.View(), [] (parser::ParseError) { });
    EXPECT_EQ(tokens.TokenCount(), original.TokenCount());
    std::vector<TokenOutputOffset> map(original.TokenCount());

    for (parser::TokenIndex i = 0; i < std::min(tokens.TokenCount(), original.TokenCount()); ++i) {
        EXPECT_EQ(tokens.TokenText(i), original.TokenText(i));
        auto r = tokens.Token(i).Text;
        map[i] = {{r.Begin, r.End}, true};
    }

    return map;
}
NamespaceCommentPlan CheckedNamespaceComments(
    const parser::SyntaxTree& tree,
    const SourceAnalysis& safety,
    NamespaceCommentOptions options,
    std::pmr::memory_resource& memory,
    std::pmr::memory_resource& scratch)
{
    auto plan = PlanNamespaceComments(tree, safety, options, memory, scratch);
    auto certified = detail::PlanNamespaceCommentsCertified(tree, safety, options, memory, scratch);
    EXPECT_EQ(certified.Namespaces.size(), plan.Namespaces.size());

    for (size_t i = 0; i < std::min(certified.Namespaces.size(), plan.Namespaces.size()); ++i) {
        const auto& actual = certified.Namespaces[i];
        const auto& expected = plan.Namespaces[i];
        EXPECT_EQ(actual.Owner.Begin, expected.Owner.Begin);
        EXPECT_EQ(actual.Owner.End, expected.Owner.End);
        EXPECT_EQ(actual.Header.Begin, expected.Header.Begin);
        EXPECT_EQ(actual.Header.End, expected.Header.End);
        EXPECT_EQ(actual.Open, expected.Open);
        EXPECT_EQ(actual.Close, expected.Close);
        EXPECT_EQ(actual.Comment, expected.Comment);
        EXPECT_EQ(actual.ExplanatoryComment, expected.ExplanatoryComment);
        EXPECT_EQ(actual.Continuation, expected.Continuation);
    }

    EXPECT_EQ(certified.Skipped.size(), plan.Skipped.size());

    for (size_t i = 0; i < std::min(certified.Skipped.size(), plan.Skipped.size()); ++i) {
        EXPECT_EQ(certified.Skipped[i].Action, plan.Skipped[i].Action);
        EXPECT_EQ(certified.Skipped[i].Tokens.Begin, plan.Skipped[i].Tokens.Begin);
        EXPECT_EQ(certified.Skipped[i].Tokens.End, plan.Skipped[i].Tokens.End);
        EXPECT_EQ(certified.Skipped[i].Reason, plan.Skipped[i].Reason);
    }

    return plan;
}

TEST(StructuralEdits, DiagnosticGolden)
{
    auto directory = std::filesystem::path(__FILE__).parent_path() / "testdata/structural-edits";
    auto source = test::golden::ReadFile(directory / "plans.input.cpp");
    File file(source, {.HonorClangFormatDirectives = true});
    auto plan = file.Plan(All);
    auto normalized = file.Apply(plan);
    auto namespaces = CheckedNamespaceComments(
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
}

TEST(StructuralEdits, NestedControlsKeepDanglingElseAndSingleOriginalPlan)
{
    File file("void f(){if(a) if(b) while(c) x(); else y();}");
    auto plan = file.Plan({.AddMissingControlBraces = true});
    EXPECT_EQ(plan.Operations.size(), 4u);
    auto result = file.Apply(plan);
    EXPECT_EQ(result.Text, "void f(){if(a) {if(b) {while(c) {x();}} else {y();}}}");
    // Reparse only in this property test, never in planning/materialization.
    File again(result.Text);
    EXPECT_TRUE(again.Plan({.AddMissingControlBraces = true}).Edits.empty());
}

TEST(StructuralEdits, ElseIfChainAndDoWhile)
{
    File file("void f(){if(a) x(); else if(b) y(); else do x(); while(c);}");
    auto plan = file.Plan({.AddMissingControlBraces = true});
    auto result = file.Apply(plan);
    EXPECT_EQ(result.Text, "void f(){if(a) {x();} else if(b) {y();} else {do {x();} while(c);}}");
}

TEST(StructuralEdits, NullBodyIsNotRemovedAndEmptyBlockIsIndependent)
{
    for (bool add : {false, true}) {
        File preserved("void f(){while(a); for(;;); if(a); else ; " "do ; while(a);}");
        auto keep = preserved.Plan({.AddMissingControlBraces = add, .RemoveNullStatements = true});
        EXPECT_TRUE(keep.Edits.empty());
        File file(preserved.Tree.Source());
        auto plan = file.Plan({.AddMissingControlBraces = add, .EmptyControlBody = true, .RemoveNullStatements = true});
        auto result = file.Apply(plan);
        EXPECT_EQ(result.Text, "void f(){while(a){} for(;;){} if(a){} else {} do {} while(a);}");
    }
}

TEST(StructuralEdits, LabelsAttributesAndAttachedCommentsAreLocalSkips)
{
    File file(
        "void f(){label: ; switch(a){case 1: ; ;} if(a) [[likely]] x(); " "if(b) y(); // attached\nwhile(c) z();}");
    auto plan = file.Plan(All);
    auto result = file.Apply(plan);
    EXPECT_NE(result.Text.find("label: ;"), std::string::npos);
    EXPECT_NE(result.Text.find("case 1: ;"), std::string::npos);
    EXPECT_NE(result.Text.find("if(a) [[likely]] x();"), std::string::npos);
    EXPECT_NE(result.Text.find("if(b) y(); // attached"), std::string::npos);
    EXPECT_NE(result.Text.find("while(c) {z();}"), std::string::npos);
    EXPECT_GE(plan.Skipped.size(), 3u);

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
        std::string source = "void f(){" + std::string(body) + " while(c) safe(); ;}";
        File controlled(source);
        auto edits = controlled.Plan(All);
        auto changed = controlled.Apply(edits);
        EXPECT_EQ(std::string_view(changed.Text), "void f(){" + std::string(body) + " while(c) {safe();} }");
        EXPECT_TRUE(std::any_of(
            edits.Skipped.begin(),
            edits.Skipped.end(),
            [] (auto diagnostic) {
                return diagnostic.Reason == StructuralSkip::LabelBody;
            }));
    }
}

TEST(StructuralEdits,
    NullRemovalPreservesRequiredExportAndStatementExpressionBodies)
{
    File file("export ; export {; ;} int f(){return ({1; ;});}");
    auto plan = file.Plan({.RemoveNullStatements = true, .RemoveNullDeclarations = true});
    EXPECT_EQ(file.Apply(plan).Text, "export ; export { } int f(){return ({1; ;});}");
    EXPECT_TRUE(std::any_of(
        plan.Skipped.begin(),
        plan.Skipped.end(),
        [] (auto diagnostic) {
            return diagnostic.Reason == StructuralSkip::RequiredBody;
        }));
}

TEST(StructuralEdits, NullRemovalRequiresKnownSyntaxOnBothSides)
{
    const auto directory = std::filesystem::path(__FILE__).parent_path() / "testdata/null-removal";
    File file(test::golden::ReadFile(directory / "input.cpp"));
    const auto plan = file.Plan({.RemoveNullStatements = true, .RemoveNullDeclarations = true});
    EXPECT_FALSE(plan.Edits.empty());
    EXPECT_TRUE(std::any_of(
        plan.Skipped.begin(),
        plan.Skipped.end(),
        [] (const auto& skip) {
            return skip.Reason == StructuralSkip::Incomplete;
        }));
}

TEST(StructuralEdits, ProtectedMacroDisabledRecoveryAndDirectiveBoundaries)
{
    std::array names{std::string_view("KEEP")};
    File file(
        "void f(){if(a) KEEP(x);\n// clang-format off\nwhile(b) x();\n// "
        "clang-format on\nif(c)\n#if FLAG\nx();\n#endif\nwhile(d) z(); " "if( ) ;}",
        {.HonorClangFormatDirectives = true, .WhitespaceSensitiveMacros = names});
    auto plan = file.Plan(All);
    auto result = file.Apply(plan);
    EXPECT_NE(result.Text.find("if(a) KEEP(x);"), std::string::npos);
    EXPECT_NE(result.Text.find("while(b) x();"), std::string::npos);
    EXPECT_NE(result.Text.find("if(c)\n#if FLAG\nx();\n#endif"), std::string::npos);
    EXPECT_NE(result.Text.find("while(d) {z();}"), std::string::npos);

    for (const auto& region : file.Safety.Protection()) {
        if (region.Reasons == ProtectionReason::Directive) {
            continue;
        }

        EXPECT_NE(result.Text.find(file.Tree.SourceText(region.Source)), std::string::npos);
    }
}

TEST(StructuralEdits, ExactIncludeLineAtomsAndConditionalCommentBarriers)
{
    File file("#include <z.h>\r\n#include \"a.h\"\r\n#if FLAG\r\n#include "
        "<d.h>\r\n#include <c.h>\r\n#endif\r\n// belongs to " "b\r\n#include <b.h>\r\n#include <a.h>\r\n");
    auto groups = DiscoverIncludeGroups(file.Tree, file.Safety, file.Memory, file.Scratch);
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
        "#include \"a.h\"\r\n#include <z.h>\r\n#if " "FLAG\r\n#include <c.h>\r\n#include <d.h>\r\n#endif\r\n// "
        "belongs to b\r\n#include <b.h>\r\n#include <a.h>\r\n");

    for (const auto& edit : plan.Edits) {
        EXPECT_EQ(plan.Operations[edit.Operation].Kind, StructuralAction::IncludeOrder);
        EXPECT_EQ(edit.PieceEnd - edit.PieceBegin, 2u);
    }

    // Preserve ignores rules even when raw structural callers provide them
    // alongside matching-header metadata for bare include names.
    const PreparedIncludeGroups inactive(std::vector<IncludeGroupRule>{{{"unrelated/deep/prefix/"}, 1}});
    const auto withMetadata = PlanStructuralEdits(
        file.Tree,
        file.Safety,
        {.SortIncludes = true, .MatchingHeader = IncludeMatchingHeader::First},
        file.Memory,
        file.Scratch,
        {"original.cpp", &inactive});
    const auto unchanged = file.Apply(withMetadata);
    EXPECT_EQ(unchanged.Text, result.Text);
}

TEST(StructuralEdits, IncludeIndentationAndDigraphMarkersStayWithExactAtoms)
{
    File file("  %:include <z.h>\r\n\t# include \"a.h\"\r\n");
    auto plan = file.Plan({.SortIncludes = true});
    ASSERT_EQ(plan.Operations.size(), 1u);
    auto groups = DiscoverIncludeGroups(file.Tree, file.Safety, file.Memory, file.Scratch);
    ASSERT_EQ(groups.size(), 1u);
    EXPECT_EQ(groups.front().Source.Begin, 0u);
    EXPECT_EQ(groups.front().Source.End, file.Source.Size);
    auto result = file.Apply(plan);
    EXPECT_EQ(result.Text, "\t# include \"a.h\"\r\n  %:include <z.h>\r\n");
}

TEST(StructuralEdits, IncludeDiscoveryContainsSortedGroupsAndExactPermutations)
{
    File file("  #include \"a.h\"\n\t#include <z.h>\n\n" " #include <z.h>\n\t#include \"a.h\"\n#include \"a.h\"\n");
    auto groups = DiscoverIncludeGroups(file.Tree, file.Safety, file.Memory, file.Scratch);
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
        EXPECT_TRUE(file.Tree.SourceText(atom).ends_with('\n'));
        cursor = atom.End;
    }

    EXPECT_EQ(cursor, groups[1].Source.End);
    auto result = file.Apply(plan);
    EXPECT_EQ(result.Text.size(), file.Source.Size);
    File sorted(result.Text);
    EXPECT_EQ(DiscoverIncludeGroups(sorted.Tree, sorted.Safety, sorted.Memory, sorted.Scratch).size(), 2u);
    EXPECT_TRUE(sorted.Plan({.SortIncludes = true}).Operations.empty());
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
        auto groups = DiscoverIncludeGroups(file.Tree, file.Safety, file.Memory, file.Scratch);
        bool comment = std::string_view(separator).starts_with("//") || std::string_view(separator).starts_with("/*");
        bool splice = std::string_view(separator).find('\\') != std::string_view::npos;

        if (splice) {
            // The splice itself stays protected. Distinct touching reason masks
            // do not contaminate complete groups wholly on either side of it.
            const auto begin = first.size();
            const auto end = begin + std::string_view(separator).size();
            ASSERT_EQ(groups.size(), 2u);
            EXPECT_EQ(groups[0].Source.End, begin);
            EXPECT_EQ(groups[1].Source.Begin, end);

            for (const auto& group : groups) {
                EXPECT_TRUE(group.Source.End <= begin || end <= group.Source.Begin);
            }

            EXPECT_TRUE(std::any_of(
                file.Safety.Protection().begin(),
                file.Safety.Protection().end(),
                [&] (const auto& range) {
                    return range.Source.Begin < end && begin < range.Source.End &&
                        (uint16_t(range.Reasons) & uint16_t(ProtectionReason::LineSplice));
                }));
        }

        ASSERT_EQ(groups.size(), comment ? 1u : 2u) << separator;
        EXPECT_EQ(groups.front().Source.Begin, 0u);
        EXPECT_EQ(groups.front().Source.End, first.size());

        if (!comment) {
            EXPECT_EQ(groups.back().Source.Begin, first.size() + std::string_view(separator).size());
            EXPECT_EQ(groups.back().Source.End, file.Source.Size);
        }

        auto plan = file.Plan({.SortIncludes = true});
        EXPECT_EQ(plan.Operations.size(), comment ? 0u : 1u) << separator;
        EXPECT_EQ(plan.Skipped.size(), comment ? 1u : 0u) << separator;

        if (comment) {
            EXPECT_EQ(plan.Skipped.front().Reason, StructuralSkip::CommentAttached);
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
        EXPECT_TRUE(DiscoverIncludeGroups(file.Tree, file.Safety, file.Memory, file.Scratch).empty());
        EXPECT_TRUE(file.Plan({.SortIncludes = true}).Edits.empty());
    }

    File mixed("#include <b.h>\r\n#include <a.h>\r\n" "#include <b.h>\n#include <a.h>\n#include <final.h>");
    auto groups = DiscoverIncludeGroups(mixed.Tree, mixed.Safety, mixed.Memory, mixed.Scratch);
    ASSERT_EQ(groups.size(), 2u);
    EXPECT_EQ(groups[0].Source.End, groups[1].Source.Begin);
    EXPECT_EQ(groups[0].Tokens.End, groups[1].Tokens.Begin);
    EXPECT_EQ(groups[1].Tokens.End, mixed.Tree.TokenCount() - 1);
    auto plan = mixed.Plan({.SortIncludes = true});
    EXPECT_EQ(plan.Operations.size(), 2u);
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
        auto groups = DiscoverIncludeGroups(file.Tree, file.Safety, file.Memory, file.Scratch);
        auto plan = file.Plan({.SortIncludes = true});
        bool safe = reason == ProtectionReason::Directive;
        EXPECT_EQ(groups.size(), safe ? 1u : 0u);
        EXPECT_EQ(plan.Operations.size(), safe ? 1u : 0u);
        EXPECT_EQ(plan.Skipped.size(), safe ? 0u : 1u);

        if (!safe) {
            EXPECT_EQ(plan.Skipped.front().Reason, StructuralSkip::Protected);
            EXPECT_EQ(plan.Skipped.front().Tokens.Begin, 0u);
            EXPECT_EQ(plan.Skipped.front().Tokens.End, 2u);
            EXPECT_EQ(file.Safety.Protection().front().Reasons, ProtectionReason::Directive | reason);
        }
    }

    File original(text);
    ASSERT_EQ(DiscoverIncludeGroups(original.Tree, original.Safety, original.Memory, original.Scratch).size(), 1u);

    for (parser::SourceRange scope :
        {
            parser::SourceRange{0, static_cast<parser::SourceOffset>(text.size())},
            parser::SourceRange{0, static_cast<parser::SourceOffset>(text.size() - 1)},
            parser::SourceRange{0, 1},
            parser::SourceRange{1, 2},
        })
    {
        std::array selection{scope};
        auto safety = BuildSourceAnalysis(original.Tree, {.EditScope = selection}, original.Memory, original.Scratch);
        bool whole = scope.End == text.size();
        auto discovered = DiscoverIncludeGroups(original.Tree, safety, original.Memory, original.Scratch);
        auto scopedPlan =
            PlanStructuralEdits(original.Tree, safety, {.SortIncludes = true}, original.Memory, original.Scratch);
        EXPECT_EQ(discovered.size(), whole ? 1u : 0u);
        EXPECT_EQ(scopedPlan.Operations.size(), whole ? 1u : 0u);
        EXPECT_EQ(scopedPlan.Skipped.size(), whole ? 0u : 1u);

        if (!whole) {
            EXPECT_EQ(scopedPlan.Skipped.front().Reason, StructuralSkip::Protected);
        }
    }

    // Exact atom authority excludes the separating blank LF. Touching different
    // reason masks keep their locations; real overlapping protection
    // still wins.
    std::array firstGroupOnly{parser::SourceRange{0, static_cast<parser::SourceOffset>(text.size())}};
    File disjoint(text + "\n" + text, {.EditScope = firstGroupOnly});
    auto selected = DiscoverIncludeGroups(disjoint.Tree, disjoint.Safety, disjoint.Memory, disjoint.Scratch);
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
    File excludedSelection(text + "\n" + text, {.ImmutableRanges = excluded, .EditScope = firstGroupOnly});
    EXPECT_TRUE(DiscoverIncludeGroups(
        excludedSelection.Tree,
        excludedSelection.Safety,
        excludedSelection.Memory,
        excludedSelection.Scratch).empty());
    auto excludedPlan = excludedSelection.Plan({.SortIncludes = true});
    EXPECT_TRUE(excludedPlan.Operations.empty());
    EXPECT_EQ(excludedPlan.Skipped.size(), 2u);
    File empty(text, {.EditScope = std::span<const parser::SourceRange>{}});
    EXPECT_TRUE(DiscoverIncludeGroups(empty.Tree, empty.Safety, empty.Memory, empty.Scratch).empty());
    EXPECT_TRUE(empty.Plan({.SortIncludes = true}).Operations.empty());
    File disabled("// clang-format off\n" + text + "// clang-format on\n", {.HonorClangFormatDirectives = true});
    EXPECT_TRUE(DiscoverIncludeGroups(disabled.Tree, disabled.Safety, disabled.Memory, disabled.Scratch).empty());
    EXPECT_TRUE(disabled.Plan({.SortIncludes = true}).Edits.empty());
    File other(text);
    EXPECT_THROW(
        (void)DiscoverIncludeGroups(original.Tree, other.Safety, original.Memory, original.Scratch),
        std::invalid_argument);
    SourceAnalysis unbuilt(original.Memory);
    EXPECT_THROW(
        (void)DiscoverIncludeGroups(original.Tree, unbuilt, original.Memory, original.Scratch),
        std::invalid_argument);
}

TEST(StructuralEdits, OwnershipValidationRejectsOverlapsAndCoownedInsertions)
{
    File file("void f(){if(a) work();}");
    const auto compactRejects = [] (File& input, const StructuralPlan& rejected) {
        std::pmr::vector<detail::CopiedTokenRun> copied(&input.Memory);
        EXPECT_THROW(
            (void)detail::MaterializeStructuralEditsCompact(input.Tree, rejected, input.Memory, input.Scratch, copied),
            std::invalid_argument);
    };
    auto plan = file.Plan({.AddMissingControlBraces = true});
    ASSERT_EQ(plan.Edits.size(), 2u);
    plan.Edits.push_back(plan.Edits.back());
    EXPECT_THROW((void)file.Apply(plan), std::invalid_argument);
    compactRejects(file, plan);
    auto partial = file.Plan({.AddMissingControlBraces = true});
    partial.Edits.pop_back();
    EXPECT_THROW((void)file.Apply(partial), std::invalid_argument);
    compactRejects(file, partial);
    auto split = file.Plan({.AddMissingControlBraces = true});
    split.Edits.front().Source.Begin += 1;
    split.Edits.front().Source.End += 1;
    EXPECT_THROW((void)file.Apply(split), std::invalid_argument);
    compactRejects(file, split);
    File declarations(";;");
    auto deletion = declarations.Plan({.RemoveNullDeclarations = true});
    ASSERT_EQ(deletion.Edits.size(), 2u);
    deletion.Edits[1].Source.Begin = deletion.Edits[0].Source.Begin;
    EXPECT_THROW((void)declarations.Apply(deletion), std::invalid_argument);
    compactRejects(declarations, deletion);
    File includes("#include <b.h>\n#include <a.h>\n");
    auto repeated = includes.Plan({.SortIncludes = true});
    std::vector<size_t> sources;

    for (size_t i = 0; i < repeated.Pieces.size(); ++i) {
        if (repeated.Pieces[i].FromSource) {
            sources.push_back(i);
        }
    }

    ASSERT_GE(sources.size(), 2u);
    repeated.Pieces[sources[1]].Source = repeated.Pieces[sources[0]].Source;
    EXPECT_THROW((void)includes.Apply(repeated), std::invalid_argument);
    compactRejects(includes, repeated);
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
}

TEST(StructuralEdits, NamespaceThresholdUsesFinalInteriorLinesNotSource)
{
    File file("namespace n {int x;}");
    auto plan = CheckedNamespaceComments(
        file.Tree,
        file.Safety,
        {.Policy = NamespaceCommentPolicy::AddMissing, .ShortNamespaceLines = 1},
        file.Memory,
        file.Scratch);
    ASSERT_EQ(plan.Namespaces.size(), 1u);

    for (const auto& output :
        {std::string("namespace n {\nint x;\n}\n"), std::string("namespace n {\n\nint x;\n}\n")})
    {
        auto map = MapOutput(file.Tree, output);
        auto resolved = ResolveNamespaceComments(file.Tree, plan, output, map, {}, file.Memory, file.Scratch);
        auto materialized = MaterializeNamespaceComments(output, resolved, file.Memory);

        if (output.find("\n\n") == std::string::npos) {
            EXPECT_TRUE(resolved.Edits.empty());
            EXPECT_EQ(resolved.Skipped.back().Reason, StructuralSkip::ShortNamespace);
        } else {
            EXPECT_EQ(materialized, "namespace n {\n\nint x;\n} // namespace n\n");
        }
    }
}

TEST(StructuralEdits, NamespaceZeroDisablesExemptionAndNestedIdentities)
{
    File file("namespace a {namespace b::c {}}\n");
    auto plan = CheckedNamespaceComments(
        file.Tree,
        file.Safety,
        {.Policy = NamespaceCommentPolicy::AddMissing},
        file.Memory,
        file.Scratch);
    EXPECT_EQ(plan.Namespaces.size(), 2u);
    std::string output = "namespace a {\nnamespace b::c {}\n}\n";
    auto map = MapOutput(file.Tree, output);
    auto resolved = ResolveNamespaceComments(file.Tree, plan, output, map, {}, file.Memory, file.Scratch);
    EXPECT_EQ(
        MaterializeNamespaceComments(output, resolved, file.Memory),
        "namespace a {\nnamespace b::c {} // namespace b::c\n} // namespace " "a\n");
    // A namespace keyword behind an opaque prefix does not prove an identity.
    File recovered("NAMESPACE_PREFIX namespace a {}\n");
    auto opaque = CheckedNamespaceComments(
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
    for (auto policy : {NamespaceCommentPolicy::Fix, NamespaceCommentPolicy::Remove}) {
        File file("namespace a {} // namespace old\r\nint x;\r\n");
        auto plan = CheckedNamespaceComments(
            file.Tree,
            file.Safety,
            {.Policy = policy, .ShortNamespaceLines = 100},
            file.Memory,
            file.Scratch);
        auto output = file.Tree.Source();
        auto map = MapOutput(file.Tree, output);
        auto resolved = ResolveNamespaceComments(file.Tree, plan, output, map, {}, file.Memory, file.Scratch);
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
    auto plan = CheckedNamespaceComments(
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
        MaterializeNamespaceComments(anonymous.Tree.Source(), resolved, anonymous.Memory),
        "namespace {} // namespace\n");
    File correct("namespace n {} // namespace n\n");
    auto fix = CheckedNamespaceComments(
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
    auto blocked = CheckedNamespaceComments(
        disabled.Tree,
        disabled.Safety,
        {.Policy = NamespaceCommentPolicy::Remove},
        disabled.Memory,
        disabled.Scratch);
    EXPECT_TRUE(blocked.Namespaces.empty());
    ASSERT_FALSE(blocked.Skipped.empty());
    EXPECT_EQ(blocked.Skipped.front().Reason, StructuralSkip::Protected);
}

TEST(StructuralEdits, NamespaceCommentMappingsAllowOnlyReindentation)
{
    File file("namespace n {\r\n/* note\r\n * body\r\n */\r\n}\r\n");
    const auto plan = CheckedNamespaceComments(
        file.Tree,
        file.Safety,
        {.Policy = NamespaceCommentPolicy::AddMissing},
        file.Memory,
        file.Scratch);
    const auto map = MapOutput(file.Tree, file.Tree.Source());
    struct Change
    {
        std::string_view Before;
        std::string_view After;
        bool Valid;
    };

    for (const auto& change :
        std::array<Change, 7>{
            {
                {"\r\n * body", "\r\n\t* body", true},
                {"/* note", "/*\tnote", false},
                {"* body", "* Body", false},
                {"* body", "*\tbody", false},
                {"\n * body", "\r * body", false},
                {"\r\n * body", "\n\r * body", false},
                {"\r\n * body", "\r \n* body", false},
            },
        })
    {
        SCOPED_TRACE(change.After);
        std::string output(file.Tree.Source());
        ASSERT_EQ(change.Before.size(), change.After.size());
        output.replace(output.find(change.Before), change.Before.size(), change.After);
        const auto resolved = ResolveNamespaceComments(file.Tree, plan, output, map, {}, file.Memory, file.Scratch);

        if (change.Valid) {
            EXPECT_EQ(resolved.Edits.size(), 1u);
            EXPECT_TRUE(resolved.Skipped.empty());
        } else {
            EXPECT_TRUE(resolved.Edits.empty());
            ASSERT_EQ(resolved.Skipped.size(), 1u);
            EXPECT_EQ(resolved.Skipped.front().Reason, StructuralSkip::InvalidMapping);
        }
    }
}

TEST(StructuralEdits, NamespaceCannotSwallowFollowingCodeCommentsOrProtection)
{
    for (const auto& text : {"namespace n {} int x;\n", "namespace n {} /* owner */\n"}) {
        File file(text);
        auto plan = CheckedNamespaceComments(
            file.Tree,
            file.Safety,
            {.Policy = NamespaceCommentPolicy::AddMissing},
            file.Memory,
            file.Scratch);
        auto map = MapOutput(file.Tree, text);
        auto resolved = ResolveNamespaceComments(file.Tree, plan, text, map, {}, file.Memory, file.Scratch);
        EXPECT_TRUE(resolved.Edits.empty());
        ASSERT_FALSE(resolved.Skipped.empty());
        EXPECT_EQ(
            resolved.Skipped.back().Reason,
            std::string_view(text).find("/*") != std::string_view::npos
            ? StructuralSkip::MissingNamespaceComment
            : StructuralSkip::FollowingText);
    }

    File file("namespace n {}\n");
    auto plan = CheckedNamespaceComments(
        file.Tree,
        file.Safety,
        {.Policy = NamespaceCommentPolicy::AddMissing},
        file.Memory,
        file.Scratch);
    auto map = MapOutput(file.Tree, file.Tree.Source());
    auto close = plan.Namespaces.front().Close;
    std::array protection{parser::SourceRange{map[close].Output.End, map[close].Output.End + 1}};
    auto resolved =
        ResolveNamespaceComments(file.Tree, plan, file.Tree.Source(), map, protection, file.Memory, file.Scratch);
    EXPECT_TRUE(resolved.Edits.empty());
    map[close].Present = false;
    auto invalid = ResolveNamespaceComments(file.Tree, plan, file.Tree.Source(), map, {}, file.Memory, file.Scratch);
    EXPECT_EQ(invalid.Skipped.back().Reason, StructuralSkip::InvalidMapping);
    map[close].Present = true;
    map[plan.Namespaces.front().Header.Begin + 1].Present = false;
    auto invalidHeader =
        ResolveNamespaceComments(file.Tree, plan, file.Tree.Source(), map, {}, file.Memory, file.Scratch);
    EXPECT_EQ(invalidHeader.Skipped.back().Reason, StructuralSkip::InvalidMapping);
    File braces("namespace n {\nvoid inside() {}\n}\n" "void after() {}\nnamespace safe {}\n");
    auto identities = CheckedNamespaceComments(
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
        EXPECT_EQ(stale.Skipped.front().Tokens.Begin, identities.Namespaces.front().Owner.Begin);
        EXPECT_EQ(
            MaterializeNamespaceComments(braces.Tree.Source(), stale, braces.Memory),
            "namespace n {\nvoid inside() {}\n}\n" "void after() {}\nnamespace safe {} // namespace safe\n");
    }

    File normalized("namespace n {\n#include <z.h>\n#include <a.h>\n;\n" "void f() {;}\n}\n;\nnamespace safe {}\n");
    auto originalNames = CheckedNamespaceComments(
        normalized.Tree,
        normalized.Safety,
        {.Policy = NamespaceCommentPolicy::AddMissing},
        normalized.Memory,
        normalized.Scratch);
    auto normalization =
        normalized.Plan({.RemoveNullStatements = true, .RemoveNullDeclarations = true, .SortIncludes = true});
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
}

TEST(StructuralEdits, SplitNamespaceRequiresMappedAdjacentUnprotectedContinuation)
{
    File file("namespace outer::inner {} // namespace\n  // outer::inner\nint after;\n");
    const auto plan = CheckedNamespaceComments(
        file.Tree,
        file.Safety,
        {.Policy = NamespaceCommentPolicy::Fix},
        file.Memory,
        file.Scratch);
    ASSERT_EQ(plan.Namespaces.size(), 1u);
    const auto continuation = plan.Namespaces.front().Continuation;
    ASSERT_LT(continuation, file.Tree.TokenCount());
    const auto source = file.Tree.Source();
    const auto mapping = MapOutput(file.Tree, source);

    for (const auto invalid : {continuation, continuation + 1}) {
        auto missing = mapping;
        missing[invalid].Present = false;

        if (invalid != continuation) {
            missing[invalid + 1].Output = mapping[continuation].Output;
        }

        const auto resolved = ResolveNamespaceComments(file.Tree, plan, source, missing, {}, file.Memory, file.Scratch);
        EXPECT_TRUE(resolved.Edits.empty());
        ASSERT_EQ(resolved.Skipped.size(), 1u);
        EXPECT_EQ(resolved.Skipped.front().Reason, StructuralSkip::InvalidMapping);
    }

    std::string changed(source);
    changed[changed.find("// outer::inner") + 3] = 'O';
    const auto stale = ResolveNamespaceComments(file.Tree, plan, changed, mapping, {}, file.Memory, file.Scratch);
    EXPECT_TRUE(stale.Edits.empty());
    EXPECT_EQ(stale.Skipped.front().Reason, StructuralSkip::InvalidMapping);

    const std::array protection{mapping[continuation].Output};
    const auto protectedOutput =
        ResolveNamespaceComments(file.Tree, plan, source, mapping, protection, file.Memory, file.Scratch);
    EXPECT_TRUE(protectedOutput.Edits.empty());
    EXPECT_EQ(protectedOutput.Skipped.front().Reason, StructuralSkip::Protected);

    const auto protectedSafety =
        BuildSourceAnalysis(file.Tree, {.ImmutableRanges = protection}, file.Memory, file.Scratch);
    const auto protectedPlan = CheckedNamespaceComments(
        file.Tree,
        protectedSafety,
        {.Policy = NamespaceCommentPolicy::Fix},
        file.Memory,
        file.Scratch);
    EXPECT_TRUE(protectedPlan.Namespaces.empty());
    EXPECT_EQ(protectedPlan.Skipped.front().Reason, StructuralSkip::Protected);

    changed = source;
    changed.insert(changed.find('\n'), "\n");
    const auto separated = MapOutput(file.Tree, changed);
    const auto separatedPlan =
        ResolveNamespaceComments(file.Tree, plan, changed, separated, {}, file.Memory, file.Scratch);
    EXPECT_TRUE(separatedPlan.Edits.empty());
    EXPECT_EQ(separatedPlan.Skipped.front().Reason, StructuralSkip::InvalidMapping);
}

} // namespace
