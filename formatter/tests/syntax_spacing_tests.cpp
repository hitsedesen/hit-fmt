#include <formatter/syntax_spacing.hpp>
#include <parser/parser.hpp>
#include <tests/golden.hpp>

#include <gtest/gtest.h>

#include <array>
#include <filesystem>
#include <memory_resource>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

using namespace formatter;
using F = SpacingField;
using C = SpacingOwner;

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

struct Analysis
{
    Input Source;
    parser::SyntaxTree Tree;
    std::pmr::monotonic_buffer_resource Memory;
    SourceAnalysis Safety;
    SyntaxSpacing Spacing;

    explicit Analysis(
        std::string_view text,
        GapPolicies policies = {},
        PointerReferenceDeclaratorLocation binding =
            PointerReferenceDeclaratorLocation::Preserve,
        SourceAnalysisOptions options = {})
        : Source(text)
        , Tree(parser::Parse(Source.View(), [] (parser::ParseError) {}))
        , Safety(Analyze(options))
        , Spacing(Classify(policies, binding))
    { }

    SourceAnalysis Analyze(SourceAnalysisOptions options)
    {
        std::pmr::monotonic_buffer_resource scratch;
        return BuildSourceAnalysis(Tree, options, Memory, scratch);
    }
    SyntaxSpacing Classify(
        const GapPolicies& policies,
        PointerReferenceDeclaratorLocation binding)
    {
        std::pmr::monotonic_buffer_resource scratch;
        return BuildSyntaxSpacing(
            Tree,
            Safety,
            policies,
            binding,
            Memory,
            scratch);
    }
    GapId Before(std::string_view token, size_t occurrence = 0) const
    {
        for (GapId i = 0; i < Tree.TokenCount(); ++i) {
            if (Tree.TokenText(i) == token && occurrence-- == 0) {
                return i;
            }
        }
        ADD_FAILURE() << "missing token " << token;
        return Tree.TokenCount();
    }
};

std::filesystem::path FixturePath(std::string_view name)
{
    return std::filesystem::path("formatter/tests/testdata/syntax-spacing") /
        name;
}

std::string Dump(const Analysis& analysis)
{
    std::ostringstream output;
    for (GapId i = 0; i < analysis.Tree.TokenCount(); ++i) {
        const auto& gap = analysis.Spacing.Gaps()[i];
        if (gap.Field == F::None && gap.Constraint == SpacingConstraint::None &&
            gap.Priority == 0)
        {
            continue;
        }
        output << i << " before=" << analysis.Tree.TokenText(i)
            << " field=" << SpacingFieldName(gap.Field)
            << " owner=" << (gap.Owner
                ? parser::GetNodeKindName(gap.Owner->Kind)
                : "none")
            << " category=" << static_cast<int>(gap.Category)
            << " priority=" << static_cast<int>(gap.Priority)
            << " fallback=" << gap.FallbackSpace
            << " protected=" << analysis.Safety.Gaps()[i].Preserve
            << " constraint=" << static_cast<int>(gap.Constraint) << '\n';
    }
    return output.str();
}

TEST(SyntaxSpacing, DiagnosticGolden)
{
    Analysis analysis(test::golden::ReadFile(FixturePath("roles.input.cpp")));
    test::golden::ExpectGolden(
        FixturePath("roles.spacing.txt"),
        Dump(analysis));
}

TEST(SyntaxSpacing, EveryAdvertisedFieldHasSyntaxOrExplicitRejection)
{
    Analysis analysis(test::golden::ReadFile(FixturePath("roles.input.cpp")));
    std::array<bool, static_cast<size_t>(F::Count)> seen{};
    for (const auto& gap : analysis.Spacing.Gaps()) {
        seen[static_cast<size_t>(gap.Field)] = true;
    }
    for (size_t i = 1; i < static_cast<size_t>(F::PointerBinding); ++i) {
        const auto field = static_cast<F>(i);
        if (SupportsSpacingField(field)) {
            EXPECT_TRUE(seen[i]) << SpacingFieldName(field);
        }
    }
    GapPolicies policies;
    policies.AmbiguousTemplateDeclarationSpacing = GapPolicy::Space;
    EXPECT_THROW(
        (void)analysis.Classify(
            policies,
            PointerReferenceDeclaratorLocation::Preserve),
        std::invalid_argument);
}

TEST(SyntaxSpacing, EveryControlProjectsPoliciesOnlyToItsClassifiedSites)
{
    const auto source = test::golden::ReadFile(FixturePath("roles.input.cpp"));
    for (auto policy : {GapPolicy::Space,
        GapPolicy::NoSpace,
        GapPolicy::GeneratedSpace,
        GapPolicy::GeneratedNoSpace,
        GapPolicy::Preserve,
    })
    {
        GapPolicies policies;
#define SET_POLICY(type, name, initial) policies.name = policy;
        FORMATTER_GAP_OPTIONS(SET_POLICY)
#undef SET_POLICY
        policies.AmbiguousTemplateDeclarationSpacing = GapPolicy::Preserve;
        Analysis analysis(source, policies);
        std::array<bool, static_cast<size_t>(F::Count)> editable{};
        for (GapId i = 0; i < analysis.Tree.TokenCount(); ++i) {
            const auto& gap = analysis.Spacing.Gaps()[i];
            const auto& safety = analysis.Safety.Gaps()[i];
            if (gap.Field == F::None || safety.Preserve ||
                safety.ContextSensitive ||
                safety.LineBoundary ==
                    GapLineBoundary::Required || safety.MustStartWithLineFeed)
            {
                continue;
            }
            editable[static_cast<size_t>(gap.Field)] = true;
            EXPECT_EQ(gap.Policy, policy) << SpacingFieldName(gap.Field);
            for (auto context : {GapContext::Preserved, GapContext::Rebuilt}) {
                auto expected = GapUsesSpace(policy, context);
                if (expected && safety.Separation == GapSeparation::Required &&
                    !safety.Original.empty())
                {
                    expected = true;
                }
                EXPECT_EQ(
                    analysis.Spacing.SpacePreference(
                        i,
                        context,
                        analysis.Safety),
                    expected)
                    << SpacingFieldName(gap.Field);
            }
        }
        for (size_t i = 1; i < static_cast<size_t>(F::PointerBinding); ++i) {
            if (SupportsSpacingField(static_cast<F>(i))) {
                EXPECT_TRUE(editable[i]) << SpacingFieldName(static_cast<F>(i));
            }
        }
    }
}

TEST(SyntaxSpacing, EnumeratorAssignmentsOwnBothGaps)
{
    for (const auto policy : {GapPolicy::Space,
        GapPolicy::NoSpace,
        GapPolicy::Preserve,
        GapPolicy::GeneratedSpace,
        GapPolicy::GeneratedNoSpace,
    })
    {
        GapPolicies policies;
        policies.AssignmentOperatorSpacing = policy;
        Analysis analysis("enum class Mode { First=1, Second = (1 + 2), "
            "Third=choose(3, 4) }; int count=0; void f(int x=1){count+=x;}",
            policies);
        for (size_t occurrence = 0; occurrence != 5; ++occurrence) {
            const auto token = analysis.Before("=", occurrence);
            SCOPED_TRACE(static_cast<int>(analysis.Tree.Role(token)));
            for (const auto gap : {token, token + 1}) {
                EXPECT_EQ(
                    analysis.Spacing.Gaps()[gap].Field,
                    F::AssignmentOperatorSpacing) << occurrence;
                EXPECT_EQ(analysis.Spacing.Gaps()[gap].Policy, policy);
                for (const auto context : {GapContext::Preserved,
                    GapContext::Rebuilt,
                })
                {
                    EXPECT_EQ(analysis.Spacing.SpacePreference(
                        gap,
                        context,
                        analysis.Safety),
                        GapUsesSpace(policy, context)) << occurrence;
                }
            }
            if (occurrence < 3) {
                EXPECT_EQ(
                    analysis.Spacing.Gaps()[token].Category,
                    C::Initializers);
                EXPECT_EQ(
                    analysis.Spacing.Gaps()[token].Owner,
                    analysis.Spacing.Gaps()[token + 1].Owner);
            }
        }
        const auto compound = analysis.Before("+=");
        EXPECT_EQ(
            analysis.Spacing.Gaps()[compound].Field,
            F::AssignmentOperatorSpacing);
        EXPECT_EQ(
            analysis.Spacing.Gaps()[compound + 1].Field,
            F::AssignmentOperatorSpacing);
        EXPECT_EQ(
            analysis.Spacing.Gaps()[analysis.Before("+")].Field,
            F::BinaryOperatorSpacing);
        EXPECT_EQ(
            analysis.Spacing.Gaps()[analysis.Before(",", 2) + 1].Field,
            F::CommaSpaceAfter);
    }
}

TEST(SyntaxSpacing, TrailingCommentPolicyOverridesAssignmentGap)
{
    GapPolicies policies;
    policies.AssignmentOperatorSpacing = GapPolicy::Space;
    Analysis analysis("enum Mode { First=  // note\n 1 }; "
        "int count=  // control\n 1;", policies);
    for (const auto comment : {"// note", "// control"}) {
        const auto gap = analysis.Before(comment);
        EXPECT_EQ(
            analysis.Spacing.Gaps()[gap - 1].Field,
            F::AssignmentOperatorSpacing);
        EXPECT_EQ(
            analysis.Spacing.Gaps()[gap].Field,
            F::TrailingCommentSpacing);
        EXPECT_EQ(analysis.Spacing.Gaps()[gap].Policy, GapPolicy::Preserve);
        EXPECT_EQ(analysis.Spacing.Gaps()[gap].Priority, 3);
        EXPECT_EQ(analysis.Safety.Gaps()[gap].Original, "  ");
        EXPECT_EQ(analysis.Safety.Gaps()[gap].Reasons, ProtectionReason::None);
        EXPECT_FALSE(analysis.Safety.Gaps()[gap].Preserve);
        EXPECT_TRUE(analysis.Safety.CanReplaceGap(gap, " "));
        EXPECT_FALSE(analysis.Spacing.SpacePreference(
            gap,
            GapContext::Rebuilt,
            analysis.Safety));
    }
}

TEST(SyntaxSpacing, SpecificAndEmptyRulesOverrideGeneralRulesEvenWhenPreserved)
{
    GapPolicies policies;
    policies.StatementSemicolonSpacing = GapPolicy::Space;
    policies.DeclarationSemicolonSpacing = GapPolicy::NoSpace;
    policies.SpaceInsideCallArguments = GapPolicy::Space;
    policies.EmptyFunctionCallArgumentsSpacing = GapPolicy::Preserve;
    policies.FunctionCallSpaceBeforeParameters = GapPolicy::Space;
    policies.LambdaCallSpaceBeforeArguments = GapPolicy::NoSpace;
    Analysis analysis("void f(){int item; item++; call(  ); []{}();}",
        policies);
    auto declaration = analysis.Before(";");
    EXPECT_EQ(
        analysis.Spacing.Gaps()[declaration].Field,
        F::DeclarationSemicolonSpacing);
    EXPECT_EQ(
        analysis.Spacing.SpacePreference(
            declaration,
            GapContext::Rebuilt,
            analysis.Safety),
        false);
    auto statement = analysis.Before(";", 1);
    EXPECT_EQ(
        analysis.Spacing.Gaps()[statement].Field,
        F::StatementSemicolonSpacing);
    auto empty = analysis.Before(")", 1);
    EXPECT_EQ(analysis.Safety.Gaps()[empty].Original, "  ");
    EXPECT_EQ(
        analysis.Spacing.Gaps()[empty].Field,
        F::EmptyFunctionCallArgumentsSpacing);
    EXPECT_FALSE(analysis.Spacing.SpacePreference(
        empty,
        GapContext::Rebuilt,
        analysis.Safety));
    auto lambdaCall = analysis.Before("(", 2);
    EXPECT_EQ(
        analysis.Spacing.Gaps()[lambdaCall].Field,
        F::LambdaCallSpaceBeforeArguments);
}

TEST(SyntaxSpacing, DoWhileOwnTerminatorIsNotAHeaderSeparator)
{
    GapPolicies policies;
    policies.StatementSemicolonSpacing = GapPolicy::NoSpace;
    policies.DeclarationSemicolonSpacing = GapPolicy::Space;
    policies.LoopSemicolonSpacing = GapPolicy::Space;
    Analysis analysis("void f(){do {step()  ;} while(flag)  ; "
        "do step()  ; while(flag)  ; for(int i=0; i<2; ++i){} }",
        policies);
    for (size_t occurrence : {1u, 3u}) {
        const auto id = analysis.Before(";", occurrence);
        const auto& gap = analysis.Spacing.Gaps()[id];
        EXPECT_EQ(gap.Field, F::StatementSemicolonSpacing);
        ASSERT_NE(gap.Owner, nullptr);
        EXPECT_EQ(gap.Owner->Kind, parser::NodeKind::DoWhileStatement);
        EXPECT_EQ(gap.Category, C::Bodies);
        EXPECT_EQ(analysis.Spacing.SpacePreference(
            id,
            GapContext::Rebuilt,
            analysis.Safety), false);
        EXPECT_TRUE(analysis.Safety.CanReplaceGap(id, ""));
    }
    for (size_t occurrence : {0u, 2u}) {
        const auto& gap = analysis.Spacing.Gaps()[
            analysis.Before(";", occurrence)];
        EXPECT_EQ(gap.Field, F::StatementSemicolonSpacing);
        ASSERT_NE(gap.Owner, nullptr);
        EXPECT_EQ(gap.Owner->Kind, parser::NodeKind::ExpressionStatement);
    }
    EXPECT_EQ(
        analysis.Spacing.Gaps()[analysis.Before(";", 4)].Field,
        F::None);
    EXPECT_EQ(
        analysis.Spacing.Gaps()[analysis.Before("i", 1)].Field,
        F::LoopSemicolonSpacing);
    Analysis lambda("auto fn=[]{do {} while(flag)  ;};", policies);
    EXPECT_EQ(
        lambda.Spacing.Gaps()[lambda.Before(";")].Category,
        C::LambdaBodies);
}

TEST(SyntaxSpacing, CaptureAssignmentDoesNotOwnRawInitializerInternals)
{
    GapPolicies policies;
    policies.SpaceAroundCaptureAssignment = GapPolicy::Space;
    for (auto capture : {"[x=(a=1)]",
        "[x=a=1]",
        "[&x=(a=1)]",
        "[...x=(a=1)]",
        "[&...x=a=1]",
        "[x /* bind */ = (a=1)]",
    })
    {
        const std::string source = std::string("void f(){int a; auto fn=") +
            capture + "{};}";
        Analysis analysis(source, policies);
        const auto separator = analysis.Before("=", 1);
        const auto inner = analysis.Before("=", 2);
        const auto& own = analysis.Spacing.Gaps()[separator];
        ASSERT_NE(own.Owner, nullptr) << capture;
        EXPECT_EQ(own.Owner->Kind, parser::NodeKind::LambdaCaptureList);
        for (auto id : {separator, separator + 1}) {
            EXPECT_EQ(
                analysis.Spacing.Gaps()[id].Field,
                F::SpaceAroundCaptureAssignment) << capture;
            EXPECT_EQ(analysis.Spacing.SpacePreference(
                id,
                GapContext::Rebuilt,
                analysis.Safety), true) << capture;
        }
        for (auto id : {inner, inner + 1}) {
            const auto& gap = analysis.Spacing.Gaps()[id];
            EXPECT_EQ(gap.Field, F::None) << capture;
            EXPECT_EQ(gap.Category, C::None) << capture;
            EXPECT_NE(gap.Owner, own.Owner) << capture;
            EXPECT_NE(static_cast<uint8_t>(gap.Constraint) &
                static_cast<uint8_t>(SpacingConstraint::UncertainSyntax), 0)
                << capture;
            EXPECT_FALSE(analysis.Spacing.SpacePreference(
                id,
                GapContext::Rebuilt,
                analysis.Safety)) << capture;
        }
    }
    Analysis defaults("void f(){auto fn=[=]{};}", policies);
    for (auto id : {defaults.Before("=", 1), defaults.Before("]")}) {
        EXPECT_NE(
            defaults.Spacing.Gaps()[id].Field,
            F::SpaceAroundCaptureAssignment);
        EXPECT_FALSE(defaults.Spacing.SpacePreference(
            id,
            GapContext::Rebuilt,
            defaults.Safety));
    }
    for (auto capture : {"[x(a=1)]", "[x{a=1}]"}) {
        Analysis direct(std::string("void f(){auto fn=") + capture + "{};}",
            policies);
        EXPECT_EQ(direct.Spacing.Gaps()[direct.Before("=", 1)].Field, F::None);
        EXPECT_FALSE(direct.Spacing.SpacePreference(
            direct.Before("=", 1),
            GapContext::Rebuilt,
            direct.Safety));
    }
    Analysis simple("void f(){auto fn=[&a, values...]{};}", policies,
        PointerReferenceDeclaratorLocation::Left);
    EXPECT_NE(
        simple.Spacing.Gaps()[simple.Before("a")].Field,
        F::PointerBinding);
    EXPECT_NE(
        simple.Spacing.Gaps()[simple.Before("...")].Field,
        F::SpaceAroundCaptureAssignment);
    EXPECT_FALSE(simple.Spacing.SpacePreference(
        simple.Before("a"),
        GapContext::Rebuilt,
        simple.Safety));
}

TEST(SyntaxSpacing, GeneratedPoliciesObserveOwnerNotDescendantEdits)
{
    GapPolicies policies;
    policies.CommaSpaceAfter = GapPolicy::GeneratedSpace;
    policies.BinaryOperatorSpacing = GapPolicy::GeneratedNoSpace;
    Analysis analysis("void f(){invoke(first + second, third);}", policies);
    const auto commaAfter = analysis.Before("third");
    const auto binaryBefore = analysis.Before("+");
    const auto& comma = analysis.Spacing.Gaps()[commaAfter];
    const auto& binary = analysis.Spacing.Gaps()[binaryBefore];
    ASSERT_NE(comma.Owner, nullptr);
    ASSERT_NE(binary.Owner, nullptr);
    EXPECT_EQ(comma.Category, C::Calls);
    EXPECT_EQ(binary.Category, C::Expressions);
    EXPECT_NE(comma.Owner, binary.Owner);
    EXPECT_FALSE(analysis.Spacing.SpacePreference(
        commaAfter,
        GapContext::Preserved,
        analysis.Safety));
    EXPECT_EQ(
        analysis.Spacing.SpacePreference(
            binaryBefore,
            GapContext::Rebuilt,
            analysis.Safety),
        false);
    EXPECT_EQ(
        analysis.Spacing.SpacePreference(
            commaAfter,
            GapContext::Rebuilt,
            analysis.Safety),
        true);
}

TEST(SyntaxSpacing, TrailingDeclarationCommentsBelongToHeaderNotOuterBody)
{
    GapPolicies policies;
    policies.TrailingCommentSpacing = GapPolicy::GeneratedSpace;
    Analysis analysis("int root; // root\nvoid f(){int local; // local\n}",
        policies);
    for (auto text : {"// root", "// local"}) {
        const auto id = analysis.Before(text);
        const auto& gap = analysis.Spacing.Gaps()[id];
        ASSERT_NE(gap.Owner, nullptr);
        EXPECT_EQ(gap.Category, C::DeclarationHeaders);
        EXPECT_TRUE(gap.Owner->Kind == parser::NodeKind::Declaration ||
            gap.Owner->Kind == parser::NodeKind::DeclarationStatement);
        EXPECT_FALSE(analysis.Spacing.SpacePreference(
            id,
            GapContext::Preserved,
            analysis.Safety));
        EXPECT_EQ(analysis.Spacing.SpacePreference(
            id,
            GapContext::Rebuilt,
            analysis.Safety), true);
    }
}

TEST(SyntaxSpacing, ExactTokensAndProtectionOverrideAllPreferences)
{
    GapPolicies policies;
#define ALL_NO_SPACE(type, name, initial) policies.name = GapPolicy::NoSpace;
    FORMATTER_GAP_OPTIONS(ALL_NO_SPACE)
#undef ALL_NO_SPACE
    policies.AmbiguousTemplateDeclarationSpacing = GapPolicy::Preserve;
    Analysis analysis("void f(){value = alpha and not beta; "
        "value = left + +right; // note\n"
        "value = 2;}\nimport <pkg/sub.h>;\n#define ITEM 1\n", policies);
    EXPECT_EQ(
        analysis.Spacing.SpacePreference(
            analysis.Before("and"),
            GapContext::Rebuilt,
            analysis.Safety),
        true);
    EXPECT_EQ(
        analysis.Spacing.SpacePreference(
            analysis.Before("not"),
            GapContext::Rebuilt,
            analysis.Safety),
        true);
    EXPECT_EQ(
        analysis.Spacing.SpacePreference(
            analysis.Before("beta"),
            GapContext::Rebuilt,
            analysis.Safety),
        true);
    for (const auto context : {GapContext::Preserved, GapContext::Rebuilt}) {
        std::string output;
        for (GapId i = 0; i <= analysis.Tree.TokenCount(); ++i) {
            const auto& source = analysis.Safety.Gaps()[i];
            auto replacement = source.Original;
            if (auto space = analysis.Spacing.SpacePreference(
                i, context, analysis.Safety))
            {
                const std::string_view candidate = *space ? " " : "";
                if (analysis.Safety.CanReplaceGap(i, candidate)) {
                    replacement = candidate;
                }
            }
            output.append(replacement);
            if (i < analysis.Tree.TokenCount()) {
                output.append(analysis.Tree.TokenText(i));
            }
        }
        Input changed(output);
        auto tokens = parser::SyntaxTree::TokenizeOnly(changed.View());
        ASSERT_EQ(tokens.TokenCount(), analysis.Tree.TokenCount());
        for (GapId i = 0; i < tokens.TokenCount(); ++i) {
            EXPECT_EQ(tokens.Kind(i), analysis.Tree.Kind(i));
            EXPECT_EQ(tokens.TokenText(i), analysis.Tree.TokenText(i));
        }
    }
    std::array<parser::SourceRange, 1> ranges{{{9, 27}}};
    Analysis protectedSource("void f(){value = alpha + beta;}", policies,
        PointerReferenceDeclaratorLocation::Preserve, {
            .ImmutableRanges = ranges,
        });
    EXPECT_FALSE(protectedSource.Spacing.SpacePreference(
        protectedSource.Before("+"),
        GapContext::Rebuilt,
        protectedSource.Safety));
}

TEST(SyntaxSpacing, CompleteSpecifierToDirectNamedDeclaratorFallback)
{
    Analysis analysis("template<class T> struct Box {}; "
        "Box<int>make(){} Box<Box<int>>nested(){} "
        "Box<int>  flat(); int scalar(){} "
        "namespace types {template<class T> struct Box {}; } "
        "types::Box<int>qualified(){}");
    for (const auto name : {"make", "nested", "flat", "scalar", "qualified"}) {
        const auto id = analysis.Before(name);
        const auto& gap = analysis.Spacing.Gaps()[id];
        ASSERT_NE(gap.Owner, nullptr) << name;
        EXPECT_EQ(gap.Category, C::DeclarationHeaders) << name;
        EXPECT_EQ(gap.Field, F::None) << name;
        EXPECT_TRUE(gap.FallbackSpace) << name;
        // This is a lowering fallback, not an explicit spacing demand.
        EXPECT_EQ(gap.Policy, GapPolicy::Preserve) << name;
        EXPECT_EQ(analysis.Spacing.SpacePreference(
            id, GapContext::Rebuilt, analysis.Safety), std::nullopt) << name;
    }
}

TEST(SyntaxSpacing, DeclarationFallbackDoesNotGuessPunctuationBoundaries)
{
    Analysis analysis("template<class T> struct Box {}; "
        "Box<int>*pointer(){} Box<int>&reference(){} "
        "Box<int>(*nested)(int); Box<int> Holder::*member; "
        "struct C {C(){} ~C(){} operator int(){} }; "
        "void use(){put<int>(); value=a>b; "
        "auto cast=static_cast<int>(value);}");
    for (const auto name : {"pointer", "reference", "nested", "member"}) {
        const auto id = analysis.Before(name);
        EXPECT_FALSE(analysis.Spacing.Gaps()[id].FallbackSpace) << name;
    }
    // Existing lexical separation at punctuation is independent of the
    // declaration fallback; do not assert that those gaps have no fallback.
    for (const auto token : {"*", "&"}) {
        const auto id = analysis.Before(token);
        EXPECT_EQ(analysis.Spacing.Gaps()[id].FallbackSpace,
            analysis.Safety.Gaps()[id].Separation == GapSeparation::Required);
        EXPECT_EQ(analysis.Spacing.SpacePreference(
            id, GapContext::Rebuilt, analysis.Safety), std::nullopt);
    }
    EXPECT_EQ(analysis.Spacing.Gaps()[analysis.Before("b")].Category,
        C::Expressions);
    EXPECT_NE(analysis.Spacing.Gaps()[analysis.Before("put")].Category,
        C::DeclarationHeaders);
    // A constructor can inherit a body sequence fallback, not a return-type
    // boundary. Its owner category must remain independent of this rule.
    EXPECT_NE(analysis.Spacing.Gaps()[analysis.Before("C", 1)].Category,
        C::DeclarationHeaders);

    Analysis uncertain("decltype(value)  incomplete(){}");
    const auto id = uncertain.Before("incomplete");
    EXPECT_FALSE(uncertain.Spacing.Gaps()[id].FallbackSpace);
    EXPECT_EQ(uncertain.Spacing.SpacePreference(
        id, GapContext::Rebuilt, uncertain.Safety), std::nullopt);
}

TEST(SyntaxSpacing, PointerBindingAndNestedMemberOverrides)
{
    GapPolicies policies;
    policies.NestedDeclaratorPointerSpacing = GapPolicy::Space;
    policies.MemberPointerDeclaratorSpacing = GapPolicy::Space;
    policies.NestedDeclaratorInteriorSpacing = GapPolicy::NoSpace;
    for (auto binding : {PointerReferenceDeclaratorLocation::Left,
        PointerReferenceDeclaratorLocation::Middle,
        PointerReferenceDeclaratorLocation::Right,
    })
    {
        Analysis analysis("int *pointer; int &reference = value; "
            "int (*function)(int); int Holder::*member;", policies, binding);
        const auto beforePointer = analysis.Before("*");
        const auto afterPointer = analysis.Before("pointer");
        EXPECT_EQ(
            analysis.Spacing.SpacePreference(
                beforePointer,
                GapContext::Rebuilt,
                analysis.Safety),
            binding != PointerReferenceDeclaratorLocation::Left);
        EXPECT_EQ(
            analysis.Spacing.SpacePreference(
                afterPointer,
                GapContext::Rebuilt,
                analysis.Safety),
            binding != PointerReferenceDeclaratorLocation::Right);
        EXPECT_EQ(
            analysis.Spacing.Gaps()[analysis.Before("function")].Field,
            F::NestedDeclaratorPointerSpacing);
        EXPECT_EQ(
            analysis.Spacing.Gaps()[analysis.Before("member")].Field,
            F::MemberPointerDeclaratorSpacing);
    }
}

TEST(SyntaxSpacing, PreservedNestedPointerRuleOverridesScalarBinding)
{
    Analysis analysis("int (*  function)(int); int Holder::*  member;", {},
        PointerReferenceDeclaratorLocation::Right);
    for (auto name : {"function", "member"}) {
        const auto id = analysis.Before(name);
        EXPECT_EQ(analysis.Safety.Gaps()[id].Original, "  ");
        EXPECT_FALSE(analysis.Spacing.SpacePreference(
            id,
            GapContext::Rebuilt,
            analysis.Safety));
    }
}

TEST(SyntaxSpacing, PointerRunsAndSeparatedDeclaratorsKeepTheirOwnBinding)
{
    for (auto binding : {PointerReferenceDeclaratorLocation::Left,
        PointerReferenceDeclaratorLocation::Middle,
        PointerReferenceDeclaratorLocation::Right,
    })
    {
        Analysis analysis("int **first, **second; "
            "int * const qualified = value;", {}, binding);
        EXPECT_EQ(analysis.Spacing.SpacePreference(
            analysis.Before("*", 1),
            GapContext::Rebuilt,
            analysis.Safety), false);
        EXPECT_EQ(analysis.Spacing.SpacePreference(
            analysis.Before("*", 3),
            GapContext::Rebuilt,
            analysis.Safety), false);
        EXPECT_EQ(analysis.Spacing.SpacePreference(
            analysis.Before("second"),
            GapContext::Rebuilt,
            analysis.Safety), binding ==
                PointerReferenceDeclaratorLocation::Middle);
        EXPECT_EQ(analysis.Spacing.SpacePreference(
            analysis.Before("qualified"),
            GapContext::Rebuilt,
            analysis.Safety), std::nullopt);
    }
}

TEST(SyntaxSpacing, JointPreferencesRetainExactTokenKindsAndSpellings)
{
    const auto source = test::golden::ReadFile(FixturePath("roles.input.cpp"));
    for (auto policy : {GapPolicy::Space, GapPolicy::NoSpace}) {
        GapPolicies policies;
#define SET_JOINT_POLICY(type, name, initial) policies.name = policy;
        FORMATTER_GAP_OPTIONS(SET_JOINT_POLICY)
#undef SET_JOINT_POLICY
        policies.AmbiguousTemplateDeclarationSpacing = GapPolicy::Preserve;
        Analysis analysis(source, policies);
        std::string edited;
        for (GapId i = 0; i <= analysis.Tree.TokenCount(); ++i) {
            const auto& sourceGap = analysis.Safety.Gaps()[i];
            auto replacement = sourceGap.Original;
            if (auto preference = analysis.Spacing.SpacePreference(
                i, GapContext::Rebuilt, analysis.Safety))
            {
                const std::string_view candidate = *preference ? " " : "";
                if (analysis.Safety.CanReplaceGap(i, candidate)) {
                    replacement = candidate;
                }
            }
            edited += replacement;
            if (i < analysis.Tree.TokenCount()) {
                edited += analysis.Tree.TokenText(i);
            }
        }
        Input original(source);
        Input changed(edited);
        auto before = parser::SyntaxTree::TokenizeOnly(original.View());
        auto after = parser::SyntaxTree::TokenizeOnly(changed.View());
        ASSERT_EQ(before.TokenCount(), after.TokenCount());
        for (GapId i = 0; i < before.TokenCount(); ++i) {
            EXPECT_EQ(before.Kind(i), after.Kind(i));
            EXPECT_EQ(before.TokenText(i), after.TokenText(i));
        }
    }
}

TEST(SyntaxSpacing, HeaderCastNewAndAttributeBoundariesHaveDistinctOwners)
{
    GapPolicies headerPolicies;
    headerPolicies.FunctionSuffixSpecifierSpacing = GapPolicy::NoSpace;
    for (auto source : {"void f()try {} catch(...){}",
        "void f() try {} catch(...){}",
        "void f()\ntry {} catch(...){}",
        "struct C {C()try : value(1){}catch(...){} int value;};",
        "struct C {C() try : value(1){}catch(...){} int value;};",
        "struct C {C()\ntry : value(1){}catch(...){} int value;};",
        "void f() /* header */try {}catch(...){}",
    })
    {
        Analysis functionTry(source, headerPolicies);
        const auto id = functionTry.Before("try");
        const auto& gap = functionTry.Spacing.Gaps()[id];
        ASSERT_NE(gap.Owner, nullptr) << source;
        EXPECT_EQ(gap.Owner->Kind, parser::NodeKind::FunctionDefinition)
            << source;
        EXPECT_EQ(gap.Category, C::DeclarationHeaders) << source;
        EXPECT_EQ(gap.Field, F::None) << source;
        EXPECT_EQ(gap.Policy, GapPolicy::Preserve) << source;
        EXPECT_TRUE(gap.FallbackSpace) << source;
        EXPECT_FALSE(functionTry.Spacing.SpacePreference(
            id,
            GapContext::Preserved,
            functionTry.Safety)) << source;
        EXPECT_FALSE(functionTry.Spacing.SpacePreference(
            id,
            GapContext::Rebuilt,
            functionTry.Safety)) << source;
        EXPECT_TRUE(functionTry.Safety.CanReplaceGap(id, " ")) << source;
    }
    Analysis lineComment("void f() // header\ntry {}catch(...){}");
    const auto afterComment = lineComment.Before("try");
    EXPECT_TRUE(lineComment.Spacing.Gaps()[afterComment].FallbackSpace);
    EXPECT_TRUE(lineComment.Safety.Gaps()[afterComment].MustStartWithLineFeed);
    EXPECT_FALSE(lineComment.Safety.CanReplaceGap(afterComment, " "));
    SourceAnalysisOptions protectedOptions;
    protectedOptions.HonorClangFormatDirectives = true;
    Analysis protectedTry("// clang-format off\n"
        "void f()try {}catch(...){}\n// clang-format on\n", headerPolicies,
        PointerReferenceDeclaratorLocation::Preserve, protectedOptions);
    const auto immutable = protectedTry.Before("try");
    EXPECT_TRUE(protectedTry.Safety.Gaps()[immutable].Preserve);
    EXPECT_FALSE(protectedTry.Safety.CanReplaceGap(immutable, " "));
    Analysis standalone("void f(){step(); try {}catch(...){}}");
    const auto standaloneTry = standalone.Before("try");
    EXPECT_EQ(standalone.Spacing.Gaps()[standaloneTry].Category, C::Bodies);
    EXPECT_EQ(
        standalone.Spacing.Gaps()[standaloneTry].Owner->Kind,
        parser::NodeKind::CompoundStatement);
    Analysis unconfirmed("void f()try;", headerPolicies);
    EXPECT_EQ(
        unconfirmed.Spacing.Gaps()[unconfirmed.Before("try")].Field,
        F::None);
    EXPECT_FALSE(unconfirmed.Spacing.Gaps()[
        unconfirmed.Before("try")].FallbackSpace);

    Analysis analysis("void f(){ if(int value = 1; value) {} "
        "for(int i = 0; i < 2; ++i) {} "
        "auto p = new (arena) Cell(value); auto c = (int) value; "
        "auto n = static_cast<int>(value); } [[a]][[b]] int declared;");
    const auto conditionAfter = analysis.Before("value", 1);
    const auto loopAfter = analysis.Before("i", 1);
    EXPECT_EQ(
        analysis.Spacing.Gaps()[conditionAfter].Field,
        F::ConditionInitSemicolonSpacing);
    EXPECT_EQ(
        analysis.Spacing.Gaps()[conditionAfter].Category,
        C::ControlHeaders);
    EXPECT_EQ(
        analysis.Spacing.Gaps()[loopAfter].Field,
        F::LoopSemicolonSpacing);
    EXPECT_EQ(
        analysis.Spacing.Gaps()[analysis.Before("Cell")].Field,
        F::PlacementNewSpaceAfterArguments);
    EXPECT_EQ(
        analysis.Spacing.Gaps()[analysis.Before("(", 4)].Field,
        F::NewInitializerSpaceBeforeParentheses);
    EXPECT_EQ(
        analysis.Spacing.Gaps()[analysis.Before("int", 2)].Field,
        F::CastArgumentSpacing);
    EXPECT_EQ(
        analysis.Spacing.Gaps()[analysis.Before("value", 3)].Field,
        F::CastSpacing);
    for (GapId i = 1; i < analysis.Tree.TokenCount(); ++i) {
        if (analysis.Tree.Role(i) ==
            parser::PunctuationRole::AttributeDelimiter &&
            analysis.Tree.Role(i - 1) ==
                parser::PunctuationRole::AttributeDelimiter &&
            analysis.Tree.Kind(i) == analysis.Tree.Kind(i - 1))
        {
            EXPECT_EQ(analysis.Spacing.Gaps()[i].Field, F::None);
            EXPECT_FALSE(analysis.Spacing.SpacePreference(
                i,
                GapContext::Rebuilt,
                analysis.Safety));
        }
    }
}

class CountingResource
    : public std::pmr::memory_resource
{
public:
    size_t Calls = 0;
    size_t Bytes = 0;
private:
    void* do_allocate(size_t bytes, size_t alignment) override
    {
        ++Calls;
        Bytes += bytes;
        return std::pmr::new_delete_resource()->allocate(bytes, alignment);
    }
    void do_deallocate(void* ptr, size_t bytes, size_t alignment) override
    {
        std::pmr::new_delete_resource()->deallocate(ptr, bytes, alignment);
    }
    bool do_is_equal(
        const std::pmr::memory_resource& other) const noexcept override
    {
        return this == &other;
    }
};

TEST(SyntaxSpacing, NewArrayRulesDoNotLeakIntoSizesOrUnconfirmedTypes)
{
    GapPolicies policies;
    policies.NewArraySpaceBeforeBrackets = GapPolicy::Space;
    policies.SubscriptOperatorSpacing = GapPolicy::Space;
    Analysis analysis("void f(){ auto p = new int[values[0]]; "
        "auto q = new decltype(values[0])[2][3]; }", policies);
    EXPECT_EQ(
        analysis.Spacing.Gaps()[analysis.Before("[", 0)].Field,
        F::NewArraySpaceBeforeBrackets);
    EXPECT_EQ(analysis.Spacing.Gaps()[analysis.Before("[", 1)].Field, F::None);
    // The parser leaves a one-token decltype NewTypeId and calls its operand a
    // NewInitializer, without recording recovery. That type is not confirmed.
    for (auto index : {analysis.Before("[", 2),
        analysis.Before("[", 3),
        analysis.Before("[", 4),
    })
    {
        EXPECT_NE(
            analysis.Spacing.Gaps()[index].Field,
            F::NewArraySpaceBeforeBrackets);
        EXPECT_NE(static_cast<uint8_t>(
            analysis.Spacing.Gaps()[index].Constraint) &
            static_cast<uint8_t>(SpacingConstraint::UncertainSyntax), 0);
        EXPECT_FALSE(analysis.Spacing.SpacePreference(
            index,
            GapContext::Rebuilt,
            analysis.Safety));
    }
}

TEST(SyntaxSpacing, KeywordOperandsAndTemplatesUseTheirOwnLayoutContexts)
{
    Analysis analysis("template<class T> void f(){ auto x = typeid(T); "
        "auto y = noexcept(value); auto z = sizeof...(T); "
        "auto fold = (value + ...); auto lambda = []{return 1;}; }");
    EXPECT_EQ(
        analysis.Spacing.Gaps()[analysis.Before("<")].Category,
        C::DeclarationHeaders);
    for (auto operand : {"T", "value"}) {
        const auto index = operand == std::string_view("T")
            ? analysis.Before(operand, 1)
            : analysis.Before(operand);
        EXPECT_EQ(
            analysis.Spacing.Gaps()[index].Field,
            F::GroupingInteriorSpacing);
        EXPECT_EQ(analysis.Spacing.Gaps()[index].Category, C::Expressions);
    }
    EXPECT_EQ(
        analysis.Spacing.Gaps()[analysis.Before("T", 2)].Category,
        C::Expressions);
    const auto foldClose = analysis.Before(")", 4);
    EXPECT_EQ(
        analysis.Spacing.Gaps()[foldClose].Field,
        F::GroupingInteriorSpacing);
    EXPECT_EQ(
        analysis.Spacing.Gaps()[analysis.Before(";", 4)].Category,
        C::LambdaBodies);
}

TEST(SyntaxSpacing, UncertainParenthesizedCalleeDoesNotBorrowCallPolicies)
{
    GapPolicies policies;
    policies.FunctionCallSpaceBeforeParameters = GapPolicy::Space;
    policies.SpaceInsideCallArguments = GapPolicy::Space;
    policies.CommaSpaceAfter = GapPolicy::Space;
    Analysis analysis("void f(){ (unknown)(first, second); "
        "(*callback)(first); }", policies);
    const auto open = analysis.Before("(", 2);
    const auto first = analysis.Before("first");
    const auto second = analysis.Before("second");
    for (auto index : {open, first, second}) {
        EXPECT_EQ(analysis.Spacing.Gaps()[index].Category, C::None);
        EXPECT_NE(static_cast<uint8_t>(
            analysis.Spacing.Gaps()[index].Constraint) &
            static_cast<uint8_t>(SpacingConstraint::UncertainSyntax), 0);
        EXPECT_FALSE(analysis.Spacing.SpacePreference(
            index,
            GapContext::Rebuilt,
            analysis.Safety));
    }
    EXPECT_EQ(
        analysis.Spacing.Gaps()[analysis.Before("(", 4)].Field,
        F::FunctionCallSpaceBeforeParameters);
}

TEST(SyntaxSpacing, CompleteDirectSequenceBoundariesHaveFlatSpaceFallback)
{
    struct Case
    {
        std::string_view Source;
        std::string_view Right;
        size_t Occurrence;
        parser::NodeKind Owner;
        C Category;
    };
    const Case cases[] = {
        {"void f(){first();\nsecond();}",
            "second",
            0,
            parser::NodeKind::CompoundStatement,
            C::Bodies,
        },
        {"auto action=[]{first();\nsecond();};",
            "second",
            0,
            parser::NodeKind::LambdaBody,
            C::LambdaBodies,
        },
        {"struct S {int first;\nint second;};",
            "int",
            1,
            parser::NodeKind::RecordDeclaration,
            C::Bodies,
        },
        {"int first;\nint second;",
            "int",
            1,
            parser::NodeKind::TranslationUnit,
            C::Bodies,
        },
        {"void f(){{first();\nsecond();}}",
            "second",
            0,
            parser::NodeKind::CompoundStatement,
            C::Bodies,
        },
        {"namespace n {int first;\nint second;}",
            "int",
            1,
            parser::NodeKind::NamespaceDeclaration,
            C::Bodies,
        },
        {"export { int first; int second; }",
            "int",
            1,
            parser::NodeKind::ExportDeclaration,
            C::Bodies,
        },
        {"export /* scope */ { int first; /* note */ int second; }",
            "/* note */",
            0,
            parser::NodeKind::ExportDeclaration,
            C::Bodies,
        },
        {"export /* scope */ { int first; /* note */ int second; }",
            "int",
            1,
            parser::NodeKind::ExportDeclaration,
            C::Bodies,
        },
        {"void f(){first(); /* note */ second();}",
            "/* note */",
            0,
            parser::NodeKind::CompoundStatement,
            C::Bodies,
        },
        {"void f(){first(); /* note */ second();}",
            "second",
            0,
            parser::NodeKind::CompoundStatement,
            C::Bodies,
        },
    };
    for (const auto& item : cases) {
        Analysis analysis(item.Source);
        const auto id = analysis.Before(item.Right, item.Occurrence);
        const auto& gap = analysis.Spacing.Gaps()[id];
        ASSERT_NE(gap.Owner, nullptr) << item.Source;
        EXPECT_EQ(gap.Owner->Kind, item.Owner) << item.Source;
        EXPECT_EQ(gap.Category, item.Category) << item.Source;
        EXPECT_TRUE(gap.FallbackSpace) << item.Source;
        // nullopt is no explicit preference, not permission to borrow bytes.
        // Canonical topology fallback before fitting is the consumer's job.
        EXPECT_FALSE(analysis.Spacing.SpacePreference(
            id,
            GapContext::Rebuilt,
            analysis.Safety));
        EXPECT_TRUE(analysis.Safety.CanReplaceGap(id, " "));
    }
}

TEST(SyntaxSpacing, SequenceAttachmentsRetainSpecificAndSafetyRules)
{
    Analysis attribute("void f(){first(); [[maybe_unused]] int second;}");
    const auto beforeAttribute = attribute.Before("[");
    EXPECT_EQ(
        attribute.Spacing.Gaps()[beforeAttribute].Field,
        F::AttributeSpaceBeforeSpecifier);
    EXPECT_TRUE(attribute.Spacing.Gaps()[beforeAttribute].FallbackSpace);
    EXPECT_EQ(
        attribute.Spacing.Gaps()[beforeAttribute].Category,
        C::DeclarationHeaders);

    Analysis exported("export {int first; [[maybe_unused]] int second;}");
    const auto exportAttribute = exported.Before("[");
    EXPECT_EQ(
        exported.Spacing.Gaps()[exportAttribute].Field,
        F::AttributeSpaceBeforeSpecifier);
    EXPECT_TRUE(exported.Spacing.Gaps()[exportAttribute].FallbackSpace);
    EXPECT_EQ(
        exported.Spacing.Gaps()[exportAttribute].Category,
        C::DeclarationHeaders);
    Analysis unbraced("export /* note */ int first;");
    EXPECT_FALSE(unbraced.Spacing.Gaps()[unbraced.Before("int")].FallbackSpace);
    Analysis exportedComment("export {int first; // note\nint second;}");
    const auto afterExportComment = exportedComment.Before("int", 1);
    EXPECT_TRUE(exportedComment.Spacing.Gaps()[
        afterExportComment].FallbackSpace);
    EXPECT_FALSE(exportedComment.Safety.CanReplaceGap(afterExportComment, " "));
    Analysis exportedDirective("export {int first;\n#define X 1\nint second;}");
    EXPECT_FALSE(exportedDirective.Spacing.Gaps()[
        exportedDirective.Before("int", 1)].FallbackSpace);
    EXPECT_FALSE(exportedDirective.Safety.CanReplaceGap(
        exportedDirective.Before("int", 1),
        " "));
    Analysis parameters("export void f(int first, int second);");
    EXPECT_EQ(
        parameters.Spacing.Gaps()[parameters.Before("int", 1)].Field,
        F::CommaSpaceAfter);
    EXPECT_EQ(
        parameters.Spacing.Gaps()[parameters.Before("int", 1)].Category,
        C::Parameters);

    Analysis comment("void f(){first(); // note\nsecond();}");
    const auto afterComment = comment.Before("second");
    EXPECT_EQ(comment.Spacing.Gaps()[afterComment].Category, C::Bodies);
    EXPECT_TRUE(comment.Spacing.Gaps()[afterComment].FallbackSpace);
    EXPECT_FALSE(comment.Safety.CanReplaceGap(afterComment, " "));

    Analysis directive("void f(){first();\n#define X 1\nsecond();}");
    EXPECT_FALSE(
        directive.Spacing.Gaps()[directive.Before("second")].FallbackSpace);
    EXPECT_FALSE(
        directive.Safety.CanReplaceGap(directive.Before("second"), " "));

    Analysis recovery("void f(){broken( ;\nsecond();}");
    EXPECT_FALSE(
        recovery.Spacing.Gaps()[recovery.Before("second")].FallbackSpace);
    EXPECT_FALSE(recovery.Spacing.SpacePreference(
        recovery.Before("second"),
        GapContext::Rebuilt,
        recovery.Safety));

    Analysis label("void f(){label:\nfirst();} "
        "struct S {public:\nint value;};");
    EXPECT_TRUE(label.Spacing.Gaps()[label.Before("first")].FallbackSpace);
    EXPECT_EQ(
        label.Spacing.Gaps()[label.Before("first")].Field,
        F::LabelColonSpaceAfter);
    EXPECT_TRUE(label.Spacing.Gaps()[label.Before("int")].FallbackSpace);
    EXPECT_EQ(
        label.Spacing.Gaps()[label.Before("int")].Field,
        F::AccessModifierColonSpacing);

    Analysis headers("void f(){for(int i=0; i<2; ++i){} "
        "invoke(first, second);}");
    EXPECT_EQ(
        headers.Spacing.Gaps()[headers.Before("i", 1)].Field,
        F::LoopSemicolonSpacing);
    EXPECT_EQ(
        headers.Spacing.Gaps()[headers.Before("i", 1)].Category,
        C::ControlHeaders);
    EXPECT_EQ(
        headers.Spacing.Gaps()[headers.Before("second")].Field,
        F::CommaSpaceAfter);
    EXPECT_EQ(
        headers.Spacing.Gaps()[headers.Before("second")].Category,
        C::Calls);
}

TEST(SyntaxSpacing, ArenaStorageScalesWithGapsNotOptionsOrNodeWidth)
{
    for (size_t count : {128, 2048, 8192}) {
        std::string source = "void f(){ invoke(";
        for (size_t i = 0; i < count; ++i) {
            if (i) {
                source += ',';
            }
            source += "argument";
        }
        source += ");}";
        Analysis analysis(source);
        CountingResource file;
        CountingResource scratch;
        auto spacing = BuildSyntaxSpacing(
            analysis.Tree, analysis.Safety, GapPolicies{},
            PointerReferenceDeclaratorLocation::Preserve, file, scratch);
        EXPECT_EQ(file.Calls, 1u);
        EXPECT_EQ(
            file.Bytes,
            spacing.Gaps().size() * sizeof(SyntaxGapPreference));
        // Width does not collect every sibling onto the scratch stack.
        EXPECT_LT(scratch.Bytes, 4096u);
        const auto calls = file.Calls + scratch.Calls;
        for (GapId i = 0; i < analysis.Tree.TokenCount(); ++i) {
            (void)spacing.SpacePreference(
                i,
                GapContext::Rebuilt,
                analysis.Safety);
        }
        EXPECT_EQ(file.Calls + scratch.Calls, calls);
    }
}

TEST(SyntaxSpacing, IrrelevantContextsAndRecoveryStayUnnormalized)
{
    GapPolicies policies;
    policies.TemplateTypeArgumentSpacing = GapPolicy::Space;
    policies.FunctionQualifierSpacing = GapPolicy::Space;
    policies.BinaryOperatorSpacing = GapPolicy::NoSpace;
    Analysis analysis("void f(){ auto compare = first < second; "
        "auto result = broken( ; }", policies);
    EXPECT_NE(
        analysis.Spacing.Gaps()[analysis.Before("<")].Field,
        F::TemplateTypeArgumentSpacing);
    EXPECT_FALSE(analysis.Spacing.SpacePreference(
        analysis.Before("broken"),
        GapContext::Rebuilt,
        analysis.Safety));
    for (GapId i = 0; i < analysis.Tree.TokenCount(); ++i) {
        EXPECT_NE(
            analysis.Spacing.Gaps()[i].Field,
            F::FunctionQualifierSpacing);
        if (analysis.Safety.Gaps()[i].Preserve) {
            EXPECT_FALSE(analysis.Spacing.SpacePreference(
                i,
                GapContext::Rebuilt,
                analysis.Safety));
        }
    }
}

} // namespace
