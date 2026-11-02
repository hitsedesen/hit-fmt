#include "syntax_spacing.hpp"

#include "flat_span_internal.hpp"

#include <unicode/utf8.hpp>

#include <algorithm>
#include <array>
#include <stdexcept>

namespace formatter {

namespace {

using NK = parser::NodeKind;

using TK = tokenizer::TokenKind;

using PR = parser::PunctuationRole;

using TI = parser::TokenIndex;

using F = SpacingField;

using C = SpacingOwner;

constexpr TI NoToken = UINT32_MAX;

bool IsWord(TK kind)
{
    return kind == TK::Identifier || kind >= TK::Keyword_alignas;
}

bool IsSpecifierContainer(NK kind)
{
    return kind == NK::DeclarationSpecifiers || kind == NK::TypeId || kind == NK::NewTypeId ||
        kind == NK::TrailingReturnType;
}

bool IsArgumentNameComment(std::string_view text)
{
    if (!text.starts_with("/*") || !text.ends_with("*/")) {
        return false;
    }

    text = text.substr(2, text.size() - 4);
    const auto first = text.find_first_not_of(" \t");

    if (first == std::string_view::npos) {
        return false;
    }

    text.remove_prefix(first);
    text = text.substr(0, text.find_last_not_of(" \t") + 1);

    if (text.ends_with('=')) {
        text.remove_suffix(1);
        text = text.substr(0, text.find_last_not_of(" \t") + 1);
    }

    bool start = true;

    while (!text.empty()) {
        const auto cp = unicode::utf8::GetFirstCodePoint(text);

        if (!cp || (cp->Value != '_' && !(start ? unicode::IsXIDStart(cp->Value) : unicode::IsXIDContinue(cp->Value))))
        {
            return false;
        }

        text.remove_prefix(cp->Size);
        start = false;
    }

    return !start;
}

bool IsControl(NK kind)
{
    switch (kind) {
        case NK::IfStatement:
        case NK::SwitchStatement:
        case NK::WhileStatement:
        case NK::DoWhileStatement:
        case NK::ForStatement:
        case NK::TryStatement:
        case NK::CatchClause:
            return true;
        default:
            return false;
    }
}

bool IsDeclaration(NK kind)
{
    switch (kind) {
        case NK::Declaration:
        case NK::FunctionDeclaration:
        case NK::RecordDeclaration:
        case NK::EnumDeclaration:
        case NK::UsingDeclaration:
        case NK::TypedefDeclaration:
        case NK::StaticAssertDeclaration:
        case NK::ConceptDeclaration:
        case NK::ModuleDeclaration:
        case NK::ImportDeclaration:
        case NK::AsmDeclaration:
        case NK::EmptyDeclaration:
        case NK::DeclarationStatement:
        case NK::TemplateDeclaration:
            return true;
        default:
            return false;
    }
}

struct Context
{
    RegionId Owner = NoRegion;
    C Category = C::None;
    RegionId ListOwner = NoRegion;
    C ListCategory = C::None;
    TI NewTypeSkip = 0;
    TI TypeOperandEnd = 0;
    TI ControlHeaderEnd = 0;
    bool NestedDeclarator = false;
    bool SeparatedDeclarator = false;
    bool KeywordOperands = false;
    bool UncertainOperands = false;
    bool UncertainSyntax = false;
};

// Only the current root-to-node path is retained. Next child and direct-token
// cursor avoid collecting a wide node's children onto the stack.
template <bool CollectFlat>
struct FlatFrame
{ };

template <>
struct FlatFrame<true>
{
    TI Candidate = NoToken;
    bool RecoveryBody = false;
};

template <bool CollectFlat>
struct Frame
{
    RegionId Node;
    RegionId Parent;
    RegionId Child;
    Context State;
    TI Cursor;
    bool Entered = false;
    RegionId PreviousChild = NoRegion;
    RegionId PreviousSibling = NoRegion;
    bool Complete = true;
    bool PreviousChildComplete = false;
    bool PreviousSiblingComplete = false;
    [[no_unique_address]] FlatFrame<CollectFlat> Flat;
};

class Classifier
{
public:
    Classifier(
        const parser::SyntaxTree& tree,
        const GapPolicies& policies,
        PointerReferenceDeclaratorLocation binding,
        std::span<SyntaxGapPreference> gaps,
        std::span<const SourceRegion> regions,
        detail::FlatSpanCollector* flat)
        : Tree_(tree)
        , Policies_{
            GapPolicy::Preserve,
#define SYNTAX_SPACING_POLICY(type, name, initial) policies.name,
            FORMATTER_GAP_OPTIONS(SYNTAX_SPACING_POLICY)
#undef SYNTAX_SPACING_POLICY
            GapPolicy::Preserve,
        }
        , Binding_(binding)
        , Gaps_(gaps)
        , Regions_(regions)
        , Flat_(flat)
    { }

    RegionId Id(const SourceRegion& node) const
    {
        return static_cast<RegionId>(&node - Regions_.data());
    }

    const SourceRegion* Child(const SourceRegion& node) const
    {
        return node.FirstChild == NoRegion ? nullptr : &Regions_[node.FirstChild];
    }

    const SourceRegion* Next(const SourceRegion& node) const
    {
        return node.NextSibling == NoRegion ? nullptr : &Regions_[node.NextSibling];
    }

    bool HasScopeQualifier(const SourceRegion& node, TI scope) const
    {
        auto previous = scope;

        while (previous > node.Tokens.Begin && Tree_.Kind(previous - 1) == TK::Comment) {
            --previous;
        }

        if (previous == node.Tokens.Begin) {
            return false;
        }

        --previous;
        const auto kind = Tree_.Kind(previous);

        // Qualified declarators can retain template closers without a
        // dedicated list role. Expression operands have their own node ranges,
        // so a comparison/shift before a leading :: is outside this node.
        if (kind == TK::Identifier || kind == TK::Operator_Greater || kind == TK::Operator_RightShift) {
            return true;
        }

        if (kind != TK::Operator_CloseParenthesis) {
            return false;
        }

        auto opening = Tree_.MatchingDelimiter(previous);

        if (opening == NoToken || opening <= node.Tokens.Begin) {
            return false;
        }

        while (opening > node.Tokens.Begin && Tree_.Kind(opening - 1) == TK::Comment) {
            --opening;
        }

        return opening > node.Tokens.Begin && Tree_.Kind(opening - 1) == TK::Keyword_decltype;
    }

    void Mark(TI gap, F field, const SourceRegion& owner, C category, uint8_t priority = 2, bool fallback = false)
    {
        if (gap == 0 || gap >= Tree_.TokenCount() || priority < Gaps_[gap].Priority) {
            return;
        }

        Gaps_[gap] = SyntaxGapPreference{
            Id(owner),
            field,
            category,
            Policies_[size_t(field)],
            fallback,
            priority,
            SpacingConstraint::None,
            Gaps_[gap].NamespaceClosingComment,
        };
    }

    void PreserveUncertain(TI gap, const SourceRegion& owner)
    {
        if (gap > 0 && gap < Tree_.TokenCount()) {
            Gaps_[gap] = SyntaxGapPreference{
                Id(owner),
                F::None,
                C::None,
                GapPolicy::Preserve,
                false,
                255,
                SpacingConstraint::UncertainSyntax,
                Gaps_[gap].NamespaceClosingComment,
            };
        }
    }

    void Pair(
        TI token,
        F before,
        F after,
        const SourceRegion& owner,
        C category,
        uint8_t priority = 2,
        bool fallback = true)
    {
        Mark(token, before, owner, category, priority, fallback);
        Mark(token + 1, after, owner, category, priority, fallback);
    }

    bool Delimited(TI open, TI end) const
    {
        return open < Tree_.TokenCount() && end > open + 1 && end <= Tree_.TokenCount() &&
            Tree_.MatchingDelimiter(open) == end - 1;
    }

    void PackEllipsis(const SourceRegion& node, Context state, TI token)
    {
        const bool list = node.Kind != NK::UnaryExpression && state.ListOwner != NoRegion;
        const auto& owner = list ? Regions_[state.ListOwner] : state.Owner != NoRegion ? Regions_[state.Owner] : node;
        const auto category = list ? state.ListCategory : state.Category;

        if ((token != node.Tokens.Begin || node.Kind != NK::LambdaCapture) && Gaps_[token].Field != F::PointerBinding) {
            Mark(token, F::PackExpansionSpaceBeforeEllipsis, owner, category, 2);
        }

        if (token + 1 < node.Tokens.End) {
            Mark(token + 1, F::PackExpansionSpaceAfterEllipsis, owner, category, 2, true);
        }
    }

    void Interior(
        TI open,
        TI end,
        F nonempty,
        F empty,
        const SourceRegion& owner,
        C category,
        uint8_t priority = 2,
        bool fallback = false)
    {
        if (!Delimited(open, end)) {
            return;
        }

        if (end == open + 2) {
            Mark(open + 1, empty, owner, category, priority + 1, fallback);
        } else {
            Mark(open + 1, nonempty, owner, category, priority, fallback);
            Mark(end - 1, nonempty, owner, category, priority, fallback);
        }
    }

    void SyntaxBoundaries(const SourceRegion& node, const SourceRegion* parent, Context state)
    {
        const auto begin = node.Tokens.Begin;
        const auto end = node.Tokens.End;
        const auto category = state.Category == C::LambdaBodies ? C::LambdaBodies : C::Bodies;
        const auto afterKeyword = [&] (F field, C ownerCategory = C::DeclarationHeaders) {
            if (begin + 1 < end && Tree_.Kind(begin + 1) != TK::Operator_Semicolon) {
                Mark(begin + 1, field, node, ownerCategory, 1, true);
            }
        };

        if (parent && IsControl(parent->Kind) &&
            (IsControl(node.Kind) || IsDeclaration(node.Kind) || node.Kind == NK::CompoundStatement ||
                node.Kind == NK::ExpressionStatement || node.Kind == NK::ReturnStatement ||
                node.Kind == NK::ThrowStatement || node.Kind == NK::CoYieldStatement ||
                node.Kind == NK::BreakStatement || node.Kind == NK::ContinueStatement ||
                node.Kind == NK::GotoStatement || node.Kind == NK::AttributedStatement))
        {
            Mark(begin, F::ControlBodySpacing, *parent, C::ControlHeaders, 2, true);
        }

        switch (node.Kind) {
            case NK::ReturnStatement:
                afterKeyword(
                    Tree_.Kind(begin) == TK::Keyword_co_return ? F::CoReturnValueSpacing : F::ReturnValueSpacing,
                    category);
                break;
            case NK::ThrowStatement:
                afterKeyword(F::ThrowOperandSpacing, category);
                break;
            case NK::CoYieldStatement:
                afterKeyword(F::CoYieldValueSpacing, category);
                break;
            case NK::GotoStatement:
                afterKeyword(F::GotoLabelSpacing, category);
                break;
            case NK::CaseLabel:
                if (Tree_.Kind(begin) == TK::Keyword_case) {
                    afterKeyword(F::CaseValueSpacing, category);
                }

                break;
            case NK::NewTypeId:
                if (parent && parent->Kind == NK::NewExpression) {
                    Mark(begin, F::NewTypeSpacing, *parent, C::Expressions, 1, true);
                }

                break;
            case NK::BaseType:
                if (parent && parent->Kind == NK::BaseSpecifier && begin > parent->Tokens.Begin &&
                    Tree_.Kind(begin - 1) != TK::Comment)
                {
                    Mark(begin, F::BaseSpecifierSpacing, *parent, C::BaseLists, 2, true);
                }

                break;
            case NK::DeleteExpression:
                afterKeyword(F::DeleteOperandSpacing, C::Expressions);
                break;
            case NK::NestedDeclarator:
                // A macro-shaped prefix inside the same declarator is opaque.
                if (parent && (begin == parent->Tokens.Begin || parent->Kind == NK::TypeId)) {
                    Mark(begin, F::NestedDeclaratorSpaceBeforeParentheses, node, C::DeclarationHeaders, 1);
                }

                break;
            case NK::MemberPointerOperator:
                Mark(begin, F::MemberPointerSpaceBeforeQualifier, node, C::DeclarationHeaders, 1, true);
                break;
            case NK::StructuredBinding:
                Mark(begin, F::StructuredBindingSpaceBeforeBrackets, node, C::DeclarationHeaders, 2, true);
                break;
            case NK::ModuleDeclaration:
            case NK::ImportDeclaration: {
                const auto keyword = begin + (Tree_.Kind(begin) == TK::Keyword_export);

                if (keyword != begin) {
                    Mark(keyword, F::ExportKeywordSpacing, node, C::DeclarationHeaders, 1, true);
                }

                if (keyword + 1 < end && Tree_.Kind(keyword + 1) != TK::Operator_Semicolon) {
                    Mark(
                        keyword + 1,
                        node.Kind == NK::ModuleDeclaration ? F::ModuleKeywordSpacing : F::ImportKeywordSpacing,
                        node,
                        C::DeclarationHeaders,
                        1,
                        true);
                }

                break;
            }
            case NK::ExportDeclaration:
                afterKeyword(F::ExportKeywordSpacing);

                if (Tree_.Kind(end - 1) == TK::Operator_CloseBrace) {
                    const auto open = Tree_.MatchingDelimiter(end - 1);

                    if (open != NoToken) {
                        Interior(
                            open,
                            end,
                            F::ExportBodyInteriorSpacing,
                            F::ExportBodyInteriorSpacing,
                            node,
                            C::Bodies,
                            2,
                            true);
                    }
                }

                break;
            case NK::LinkageSpecification:
                afterKeyword(F::LinkageLanguageSpacing);

                if (Tree_.Kind(end - 1) == TK::Operator_CloseBrace) {
                    const auto open = Tree_.MatchingDelimiter(end - 1);

                    if (open != NoToken) {
                        Mark(open, F::LinkageBraceSpacing, node, C::DeclarationHeaders, 2, true);
                        Interior(
                            open,
                            end,
                            F::LinkageBodyInteriorSpacing,
                            F::LinkageBodyInteriorSpacing,
                            node,
                            C::Bodies,
                            2,
                            true);
                    }
                }

                break;
            case NK::ConceptDeclaration:
                afterKeyword(F::ConceptNameSpacing);
                break;
            case NK::NamespaceDeclaration:
                if (Tree_.Kind(end - 1) == TK::Operator_Semicolon) {
                    Mark(end - 1, F::DeclarationSemicolonSpacing, node, C::DeclarationHeaders, 3);
                }

                break;
            case NK::UsingDeclaration:
                afterKeyword(F::UsingKeywordSpacing);

                if (begin + 2 < end &&
                    (Tree_.Kind(begin + 1) == TK::Keyword_namespace || Tree_.Kind(begin + 1) == TK::Keyword_typename))
                {
                    Mark(begin + 2, F::UsingKeywordSpacing, node, C::DeclarationHeaders, 1, true);
                }

                break;
            case NK::RequiresClause:
                Mark(begin, F::RequiresClauseSpaceBefore, node, C::DeclarationHeaders, 3, true);
                afterKeyword(F::RequiresClauseSpaceAfter, C::Expressions);
                break;
            case NK::NestedRequirement:
                afterKeyword(F::RequiresClauseSpaceAfter, C::Expressions);
                break;
            case NK::TypeRequirement:
                afterKeyword(F::DependentTypeNameSpacing);
                break;
            case NK::RequirementBody:
                Mark(begin, F::RequiresExpressionSpaceBeforeBrace, parent ? *parent : node, C::Expressions, 2, true);
                break;
            case NK::IfStatement:
                if (begin && Tree_.Kind(begin - 1) == TK::Keyword_else) {
                    Mark(begin, F::ControlClauseSpacing, parent ? *parent : node, C::ControlHeaders, 3, true);
                }

                break;
            case NK::CatchClause:
                Mark(begin, F::ControlClauseSpacing, parent ? *parent : node, C::ControlHeaders, 3, true);
                break;
            default:
                break;
        }
    }

    void Enter(const SourceRegion& node, const SourceRegion* parent, Context& state, RegionStatus status)
    {
        const auto kind = node.Kind;
        const auto begin = node.Tokens.Begin;
        const auto end = node.Tokens.End;

        if (begin >= Tree_.TokenCount() || end > Tree_.TokenCount() || begin == end) {
            return;
        }

        if (kind == NK::NewExpression) {
            for (const auto* child = Child(node); child; child = Next(*child)) {
                if (child->Kind == NK::NewTypeId && child->Tokens.Size() == 1 &&
                    Tree_.Kind(child->Tokens.Begin) == TK::Keyword_decltype)
                {
                    // A decltype type production needs its operand, not a
                    // new-initializer. The parser can mark this Complete;
                    // it is still not a confirmed type.
                    state.UncertainSyntax = true;
                }
            }
        }

        if (state.UncertainSyntax) {
            state.Owner = Id(node);
            state.Category = C::None;
            return;
        }

        // These productions inherit context without defining spacing edges.
        // Direct() still classifies their tokens, including qualified names.
        if (kind == NK::IdentifierExpression || kind == NK::LiteralExpression || kind == NK::Expression ||
            kind == NK::DeclaratorName)
        {
            return;
        }

        const auto statementCategory = state.Category == C::LambdaBodies ? C::LambdaBodies : C::Bodies;

        if (IsDeclaration(kind) || kind == NK::FunctionDefinition || kind == NK::LambdaExpression ||
            kind == NK::NamespaceDeclaration || kind == NK::DeclarationSpecifiers || kind == NK::TypeId)
        {
            state.Owner = Id(node);
            state.Category = C::DeclarationHeaders;
        }

        if (IsControl(kind)) {
            state.Owner = Id(node);
            state.Category = C::ControlHeaders;
            state.ControlHeaderEnd = 0;
        }

        if (kind == NK::BinaryExpression || kind == NK::AssignmentExpression || kind == NK::ConditionalExpression ||
            kind == NK::UnaryExpression || kind == NK::CastExpression || kind == NK::TypeConstructionExpression ||
            kind == NK::FoldExpression || kind == NK::NewExpression || kind == NK::DeleteExpression ||
            kind == NK::ParenthesizedExpression || kind == NK::RequirementExpression || kind == NK::StatementExpression)
        {
            state.Owner = Id(node);
            state.Category = C::Expressions;
        }

        if (kind == NK::MemberExpression) {
            state.Owner = Id(node);
            state.Category = C::Chains;
        }

        if (kind == NK::NewTypeId) {
            state.NewTypeSkip = 0;
        }

        if (IsSpecifierContainer(kind) || kind == NK::AsmDeclaration) {
            state.TypeOperandEnd = 0;
        }

        if (kind == NK::NestedDeclarator) {
            state.NestedDeclarator = true;
        }

        if (kind == NK::ParameterDeclaration) {
            state.NestedDeclarator = false;
        }

        if (kind == NK::InitDeclarator && parent && parent->Kind == NK::InitDeclaratorList) {
            state.SeparatedDeclarator = begin != parent->Tokens.Begin;
        }

        auto list = [&] (C category, const SourceRegion* owner = nullptr) {
            state.Owner = owner ? Id(*owner) : Id(node);
            state.Category = category;
            state.ListOwner = state.Owner;
            state.ListCategory = category;
            state.UncertainOperands = false;
        };
        SyntaxBoundaries(node, parent, state);
        switch (kind) {
            case NK::CallExpression: {
                state.KeywordOperands =
                    Tree_.Kind(begin) == TK::Keyword_typeid || Tree_.Kind(begin) == TK::Keyword_noexcept ||
                        (parent && parent->Kind == NK::UnaryExpression &&
                            Tree_.Kind(parent->Tokens.Begin) == TK::Keyword_sizeof &&
                            Tree_.Kind(begin) == TK::Operator_Ellipsis);
                state.UncertainOperands = false;
                const auto* callee = Child(node);

                if (callee && callee->Kind == NK::ParenthesizedExpression) {
                    const auto* inner = Child(*callee);
                    state.UncertainOperands = !inner ||
                        (inner->Kind != NK::LambdaExpression && inner->Kind != NK::CallExpression &&
                            !(inner->Kind == NK::UnaryExpression &&
                                (Tree_.Kind(inner->Tokens.Begin) == TK::Operator_Star ||
                                    Tree_.Kind(inner->Tokens.Begin) == TK::Operator_Ampersand)));
                } else if (callee && callee->Kind == NK::Expression && !state.KeywordOperands) {
                    state.UncertainOperands = true;
                }

                if (state.KeywordOperands) {
                    state.Owner = Id(node);
                    state.Category = C::Expressions;
                }

                if (Child(node) && Child(node)->Tokens.End == begin + 1) {
                    if (Tree_.Kind(begin) == TK::Keyword_typeid) {
                        KeywordParenthesis(node, F::TypeidSpaceBeforeParentheses, C::Expressions);
                    } else if (Tree_.Kind(begin) == TK::Keyword_noexcept) {
                        KeywordParenthesis(node, F::NoexceptSpaceBeforeParentheses, C::Expressions);
                    }
                }

                break;
            }
            case NK::ArgumentList: {
                if (state.UncertainOperands && parent) {
                    state.Owner = Id(*parent);
                    state.Category = C::None;
                    state.ListOwner = Id(*parent);
                    state.ListCategory = C::None;
                    PreserveUncertain(begin, *parent);

                    if (Delimited(begin, end)) {
                        PreserveUncertain(begin + 1, *parent);
                        PreserveUncertain(end - 1, *parent);
                    }

                    break;
                }

                if (state.KeywordOperands && parent) {
                    list(C::Expressions, parent);
                    Interior(
                        begin,
                        end,
                        F::GroupingInteriorSpacing,
                        F::GroupingInteriorSpacing,
                        *parent,
                        C::Expressions,
                        3);
                    break;
                }

                list(C::Calls, parent && parent->Kind == NK::CallExpression ? parent : &node);
                bool lambda = parent && parent->Kind == NK::CallExpression && Child(*parent) &&
                    (Child(*parent)->Kind == NK::LambdaExpression ||
                        (Child(*parent)->Kind == NK::ParenthesizedExpression && Child(*Child(*parent)) &&
                            Child(*Child(*parent))->Kind == NK::LambdaExpression));
                Mark(
                    begin,
                    lambda ? F::LambdaCallSpaceBeforeArguments : F::FunctionCallSpaceBeforeParameters,
                    Regions_[state.Owner],
                    C::Calls);
                Interior(
                    begin,
                    end,
                    F::SpaceInsideCallArguments,
                    F::EmptyFunctionCallArgumentsSpacing,
                    Regions_[state.Owner],
                    C::Calls);
                break;
            }
            case NK::Argument:
                if (parent && parent->Kind == NK::ArgumentList && !state.KeywordOperands && !state.UncertainOperands) {
                    ArgumentComments(node, *parent, C::Calls);
                }

                break;
            case NK::InitializerElement:
                if (parent && parent->Kind == NK::InitializerList) {
                    ArgumentComments(node, *parent, C::Initializers);
                }

                break;
            case NK::ParameterList: {
                const bool lambda = parent && parent->Kind == NK::LambdaExpression;
                list(C::Parameters, parent ? parent : &node);
                Mark(
                    begin,
                    lambda ? F::LambdaSpaceBeforeParameters : F::FunctionSpaceBeforeParameters,
                    Regions_[state.Owner],
                    C::Parameters);
                Interior(
                    begin,
                    end,
                    lambda ? F::SpaceInsideLambdaParameters : F::SpaceInsideFunctionParameters,
                    lambda ? F::EmptyLambdaParametersSpacing : F::EmptyFunctionParametersSpacing,
                    Regions_[state.Owner],
                    C::Parameters);
                break;
            }
            case NK::LambdaCapture:
                Capture(node, parent);
                break;
            case NK::LambdaCaptureList:
                list(C::Captures);
                Interior(begin, end, F::SpaceAroundCaptureList, F::EmptyLambdaCaptureSpacing, node, C::Captures);
                break;
            case NK::BracedInitializer: {
                list(C::Initializers);
                // An intervening label comment owns its following gap, not
                // the type/assignment boundary before an initializer.
                const bool afterComment = begin > 0 && Tree_.Kind(begin - 1) == TK::Comment;

                if (!afterComment && parent && parent->Kind == NK::InitializerElement && parent->Tokens.Begin < begin) {
                    Mark(begin, F::AssignmentOperatorSpacing, *parent, C::Initializers, 3);
                }

                if (!afterComment &&
                    (!parent || (parent->Kind != NK::Initializer && parent->Kind != NK::InitializerElement) ||
                        parent->Tokens.Begin == begin) &&
                    (!parent ||
                        (parent->Kind != NK::ReturnStatement && parent->Kind != NK::ThrowStatement &&
                            parent->Kind != NK::AssignmentExpression)))
                {
                    Mark(begin, F::BraceInitializerSpacing, node, C::Initializers);
                }

                Interior(
                    begin,
                    end,
                    F::InitializerBracesSpacing,
                    F::EmptyBraceInitializerInteriorSpacing,
                    node,
                    C::Initializers);
                break;
            }
            case NK::InitializerList:
            case NK::EnumeratorList:
                list(C::Initializers);

                if (kind == NK::EnumeratorList) {
                    Interior(
                        begin,
                        end,
                        F::EnumBodyInteriorSpacing,
                        F::EnumBodyInteriorSpacing,
                        node,
                        C::Bodies,
                        3,
                        true);
                    Mark(begin, F::EnumBraceSpacing, parent ? *parent : node, C::DeclarationHeaders, 3, true);
                }

                break;
            case NK::BaseClause:
                list(C::BaseLists);
                Pair(begin, F::BaseListsColonSpaceBefore, F::BaseListsColonSpaceAfter, node, C::BaseLists, 3);
                break;
            case NK::ConstructorInitializerList:
                list(C::ConstructorLists);
                Pair(
                    begin,
                    F::ConstructorListsColonSpaceBefore,
                    F::ConstructorListsColonSpaceAfter,
                    node,
                    C::ConstructorLists,
                    3);
                break;
            case NK::Condition:
            case NK::ForHeader:
                list(C::ControlHeaders, parent ? parent : &node);
                state.ControlHeaderEnd = end;

                if (ControlKeywordBoundary(begin, parent)) {
                    Mark(begin, F::ControlBlockKeywordSpacing, Regions_[state.Owner], C::ControlHeaders, 3, true);
                } else {
                    // A skipped macro-shaped identifier is not the control
                    // keyword. Its invocation boundary has no control
                    // authority.
                    PreserveUncertain(begin, node);
                }

                Interior(
                    begin,
                    end,
                    F::ControlBlockConditionSpacing,
                    F::ControlBlockConditionSpacing,
                    Regions_[state.Owner],
                    C::ControlHeaders);
                break;
            case NK::ParenthesizedExpression:
            case NK::FoldExpression:
            case NK::StatementExpression:
                if (kind == NK::ParenthesizedExpression &&
                    (status == RegionStatus::Recovery || status == RegionStatus::RecoveryAndAmbiguous) &&
                    SizeofPackOperand(node, parent) && Delimited(begin, end))
                {
                    // Recovery can end exactly at the closer's insertion
                    // point. Source gap permission alone does not prove an
                    // operand interior; retain both edges, not the prefix.
                    PreserveUncertain(begin + 1, node);
                    PreserveUncertain(end - 1, node);
                    break;
                }

                Interior(begin, end, F::GroupingInteriorSpacing, F::GroupingInteriorSpacing, node, C::Expressions);
                break;
            case NK::NestedDeclarator:
                Interior(
                    begin,
                    end,
                    F::NestedDeclaratorInteriorSpacing,
                    F::NestedDeclaratorInteriorSpacing,
                    node,
                    C::DeclarationHeaders,
                    3);
                break;
            case NK::ArrayDeclarator:
                Mark(begin, F::ArrayDeclarationSpacing, node, C::DeclarationHeaders);
                Interior(
                    begin,
                    end,
                    F::ArrayDeclarationInteriorSpacing,
                    F::EmptyArrayDeclarationInteriorSpacing,
                    node,
                    C::DeclarationHeaders);
                break;
            case NK::StructuredBinding:
                Interior(
                    begin,
                    end,
                    F::StructuredBindingBracketSpacing,
                    F::StructuredBindingBracketSpacing,
                    node,
                    C::DeclarationHeaders);
                break;
            case NK::SubscriptExpression:
                // First child is the base; the remaining direct span owns [.
                if (Child(node)) {
                    const auto open = Child(node)->Tokens.End;

                    if (open < end && Tree_.Kind(open) == TK::Operator_OpenBracket) {
                        Mark(open, F::SubscriptOperatorSpacing, node, C::Expressions);
                        Interior(
                            open,
                            end,
                            F::SubscriptOperatorInteriorSpacing,
                            F::SubscriptOperatorInteriorSpacing,
                            node,
                            C::Expressions);
                    }
                }

                break;
            case NK::TemplateParameterList:
            case NK::TemplateArgumentList:
                list(kind == NK::TemplateParameterList ? C::TemplateParameters : C::TemplateArguments);

                if (Tree_.Role(begin) == PR::TemplateDelimiter && Tree_.Role(end - 1) == PR::TemplateDelimiter) {
                    Mark(
                        begin,
                        kind == NK::TemplateParameterList ? F::TemplateKeywordSpacing : F::TemplateTypeArgumentSpacing,
                        node,
                        state.Category);
                    // Template delimiters can be contextually split >>; require
                    // parser roles but do not require tokenizer pairing here.
                    Mark(begin + 1, F::TemplateTypeArgumentInteriorSpacing, node, state.Category);
                    Mark(end - 1, F::TemplateTypeArgumentInteriorSpacing, node, state.Category);
                }

                if (kind == NK::TemplateParameterList && parent &&
                    (parent->Kind == NK::TemplateDeclaration || parent->Kind == NK::TemplateParameter))
                {
                    Mark(end, F::TemplateHeaderSpacing, *parent, C::DeclarationHeaders, 1, true);
                }

                break;
            case NK::AttributeSpecifier:
            case NK::ExtensionAttributeSpecifier: {
                auto opening = begin + 1;

                while (opening < end && Tree_.Kind(opening) == TK::Comment) {
                    ++opening;
                }

                const bool standard = kind == NK::AttributeSpecifier;
                const bool complete = standard
                    ? end >= begin + 4 && Delimited(begin, end) && Tree_.Kind(begin + 1) == TK::Operator_OpenBracket &&
                        Tree_.MatchingDelimiter(begin + 1) == end - 2
                    : opening < end && Tree_.Kind(opening) == TK::Operator_OpenParenthesis && Delimited(opening, end);

                if (complete) {
                    // Extension boundaries defer to declaration punctuation
                    // and suffix rules, which have priority 3.
                    const uint8_t priority = standard ? 3 : 2;

                    // An attribute at a confirmed parenthesis edge does not
                    // own that gap; retain the enclosing interior policy.
                    if (begin == 0 || Tree_.Kind(begin - 1) != TK::Operator_OpenParenthesis ||
                        Gaps_[begin].Priority == 0)
                    {
                        Mark(
                            begin,
                            F::AttributeSpaceBeforeSpecifier,
                            state.Owner != NoRegion ? Regions_[state.Owner] : node,
                            state.Category,
                            priority,
                            true);
                    }

                    if (end == Tree_.TokenCount() || Tree_.Kind(end) != TK::Operator_CloseParenthesis ||
                        Gaps_[end].Priority == 0)
                    {
                        Mark(
                            end,
                            F::AttributeSpaceAfterSpecifier,
                            state.Owner != NoRegion ? Regions_[state.Owner] : node,
                            state.Category,
                            priority,
                            true);
                    }

                    // Extension payloads remain opaque; their parentheses
                    // are not C++ attribute brackets or ordinary call syntax.
                    if (standard) {
                        Interior(
                            begin + 1,
                            end - 1,
                            F::AttributeInteriorSpacing,
                            F::EmptyAttributeInteriorSpacing,
                            node,
                            state.Category,
                            3);
                    }
                }

                break;
            }
            case NK::NewPlacement:
                list(C::Calls);
                Mark(begin, F::PlacementNewSpaceBeforeArguments, node, C::Expressions, 4, true);
                Mark(end, F::PlacementNewSpaceAfterArguments, node, C::Expressions, 4, true);
                Interior(
                    begin,
                    end,
                    F::PlacementNewArgumentInteriorSpacing,
                    F::PlacementNewArgumentInteriorSpacing,
                    node,
                    C::Calls,
                    4);
                break;
            case NK::NewInitializer: {
                list(C::Initializers);
                const bool brace = Tree_.Kind(begin) == TK::Operator_OpenBrace;
                Mark(
                    begin,
                    brace ? F::NewInitializerSpaceBeforeBraces : F::NewInitializerSpaceBeforeParentheses,
                    node,
                    C::Initializers,
                    4);
                Interior(
                    begin,
                    end,
                    brace ? F::NewInitializerBraceInteriorSpacing : F::NewInitializerParenthesisInteriorSpacing,
                    brace ? F::NewInitializerBraceInteriorSpacing : F::NewInitializerParenthesisInteriorSpacing,
                    node,
                    C::Initializers,
                    4);
                break;
            }
            case NK::DeleteArrayMarker:
                Interior(
                    begin,
                    end,
                    F::DeleteArrayInteriorSpacing,
                    F::DeleteArrayInteriorSpacing,
                    node,
                    C::Expressions);
                Mark(begin, F::DeleteArraySpaceBeforeBrackets, node, C::Expressions, 4);

                if (parent && parent->Kind == NK::DeleteExpression && node.NextSibling != NoRegion &&
                    !Regions_[node.NextSibling].Tokens.Empty() && Regions_[node.NextSibling].Tokens.Begin >= end &&
                    Regions_[node.NextSibling].Tokens.End <= parent->Tokens.End &&
                    Regions_[node.NextSibling].Kind != NK::MissingToken &&
                    Regions_[node.NextSibling].Kind != NK::UnparsedSyntax &&
                    Regions_[node.NextSibling].Kind != NK::UnexpectedToken &&
                    Regions_[node.NextSibling].Kind != NK::Error)
                {
                    Mark(end, F::DeleteArraySpaceAfterBrackets, node, C::Expressions, 4, true);
                }

                break;
            case NK::FunctionTryBlock:
                if (parent && parent->Kind == NK::FunctionDefinition && Tree_.Kind(begin) == TK::Keyword_try) {
                    // The introducer is a header transition, not a suffix.
                    Mark(begin, F::FunctionTryBlockSpacing, *parent, C::DeclarationHeaders, 2, true);
                }

                break;
            case NK::ClassVirtSpecifier:
                // A parsed class-virt-specifier is a separate header component,
                // even after a template-id or comment.
                Mark(begin, F::RecordFinalSpacing, parent ? *parent : node, C::DeclarationHeaders, 1, true);
                break;
            case NK::FunctionQualifier: {
                const auto token = Tree_.Kind(begin);
                auto field = F::FunctionSuffixSpecifierSpacing;

                if (token == TK::Keyword_const || token == TK::Keyword_volatile) {
                    field = F::FunctionQualifierSpacing;
                } else if (token == TK::Operator_Ampersand || token == TK::Operator_And) {
                    auto previous = begin;

                    while (previous > 0 && Tree_.Kind(previous - 1) == TK::Comment) {
                        --previous;
                    }

                    const bool cv = previous > 0 &&
                        (Tree_.Kind(previous - 1) == TK::Keyword_const ||
                            Tree_.Kind(previous - 1) == TK::Keyword_volatile);
                    field = cv ? F::FunctionCvRefQualifierSpacing : F::FunctionRefQualifierSpacing;

                    // An opaque macro suffix is still separate from the
                    // ref-qualifier, even without an attribute production.
                    if (parent && end < parent->Tokens.End && Tree_.Kind(end) == TK::Identifier) {
                        Mark(end, F::FunctionSuffixSpecifierSpacing, *parent, C::DeclarationHeaders, 1, true);
                    }
                }

                Mark(begin, field, parent ? *parent : node, C::DeclarationHeaders, 3, true);
                break;
            }
            case NK::ExceptionSpecification:
                Mark(begin, F::NoexceptSpaceBeforeSpecifier, parent ? *parent : node, C::DeclarationHeaders, 3, true);
                KeywordParenthesis(node, F::NoexceptSpaceBeforeParentheses, C::DeclarationHeaders);
                Interior(
                    begin + 1,
                    end,
                    F::NoexceptSpecifierInteriorSpacing,
                    F::NoexceptSpecifierInteriorSpacing,
                    node,
                    C::DeclarationHeaders);
                KeywordParenthesis(node, F::NoexceptSpaceBeforeParentheses, C::DeclarationHeaders);
                break;
            case NK::TrailingReturnType:
                Pair(
                    begin,
                    F::ReturnArrowSpaceBefore,
                    F::ReturnArrowSpaceAfter,
                    parent ? *parent : node,
                    C::DeclarationHeaders,
                    3);
                break;
            case NK::AccessSpecifier:
                Pair(end - 1, F::AccessModifierColonSpacing, F::AccessModifierColonSpacing, node, C::Bodies, 3, false);
                break;
            case NK::RangeForSeparator:
                Pair(
                    begin,
                    F::RangeBasedForColonSpacing,
                    F::RangeBasedForColonSpacing,
                    state.ListOwner != NoRegion ? Regions_[state.ListOwner] : node,
                    C::ControlHeaders,
                    3);
                break;
            case NK::BitField:
                Pair(begin, F::BitFieldColonSpaceBefore, F::BitFieldColonSpaceAfter, node, C::DeclarationHeaders, 3);
                break;
            case NK::CaseLabel:
            case NK::LabelStatement:
                if (Tree_.Kind(end - 1) == TK::Operator_Colon) {
                    Pair(end - 1, F::LabelColonSpaceBefore, F::LabelColonSpaceAfter, node, C::Bodies, 3, false);
                }

                break;
            case NK::OperatorFunctionName:
                if (begin + 1 < end) {
                    Mark(
                        begin + 1,
                        F::OperatorOverloadOperatorSpaceBefore,
                        parent ? *parent : node,
                        C::DeclarationHeaders,
                        4);
                    Mark(end, F::OperatorOverloadOperatorSpaceAfter, parent ? *parent : node, C::DeclarationHeaders, 4);

                    if (Tree_.Kind(begin + 1) == TK::Operator_OpenParenthesis ||
                        Tree_.Kind(begin + 1) == TK::Operator_OpenBracket)
                    {
                        Interior(
                            begin + 1,
                            end,
                            F::OperatorNameInteriorSpacing,
                            F::OperatorNameInteriorSpacing,
                            node,
                            C::DeclarationHeaders,
                            3);
                    }
                }

                break;
            case NK::PointerOperator:
            case NK::MemberPointerOperator:
                Pointer(node, state);
                break;
            case NK::LambdaBody:
            case NK::CompoundStatement:
            case NK::RequirementBody:
                state.Owner = Id(node);
                state.Category = kind == NK::LambdaBody ? C::LambdaBodies : C::Bodies;
                state.ListOwner = NoRegion;
                state.ListCategory = C::None;
                Interior(
                    begin,
                    end,
                    kind == NK::RequirementBody ? F::RequirementBodyInteriorSpacing : F::BlockInteriorSpacing,
                    kind == NK::LambdaBody
                    ? F::EmptyLambdaBodySpacing
                    : kind == NK::RequirementBody ? F::RequirementBodyInteriorSpacing : F::EmptyBlockInteriorSpacing,
                    node,
                    state.Category,
                    2,
                    true);

                if (kind == NK::LambdaBody) {
                    Mark(begin, F::LambdaBraceSpacing, parent ? *parent : node, C::DeclarationHeaders, 3, true);
                } else if (parent && (parent->Kind == NK::FunctionDefinition || parent->Kind == NK::FunctionTryBlock)) {
                    Mark(begin, F::FunctionBraceSpacing, *parent, C::DeclarationHeaders, 3, true);
                } else if (parent && IsControl(parent->Kind)) {
                    Mark(begin, F::ControlBlockSpaceBeforeBrace, *parent, C::ControlHeaders, 3, true);
                } else if (parent && parent->Kind == NK::NamespaceDeclaration) {
                    Interior(
                        begin,
                        end,
                        F::NamespaceBodyInteriorSpacing,
                        F::NamespaceBodyInteriorSpacing,
                        node,
                        C::Bodies,
                        3,
                        true);
                    Mark(begin, F::NamespaceBraceSpacing, *parent, C::DeclarationHeaders, 3, true);
                }

                break;
            case NK::CastExpression:
                Cast(node);
                break;
            case NK::ReturnStatement:
            case NK::ThrowStatement:
                if (Child(node) &&
                    (Child(node)->Kind == NK::ParenthesizedExpression || Child(node)->Kind == NK::FoldExpression) &&
                    Child(node)->Tokens.Begin == begin + 1)
                {
                    Mark(
                        begin + 1,
                        kind == NK::ThrowStatement
                        ? F::ThrowSpaceBeforeParentheses
                        : Tree_.Kind(begin) == TK::Keyword_co_return
                            ? F::CoReturnSpaceBeforeParentheses
                            : F::ReturnSpaceBeforeParentheses,
                        node,
                        C::Expressions,
                        3,
                        true);
                }

                break;
            case NK::RequirementExpression:
                Interior(
                    begin,
                    end,
                    F::CompoundRequirementInteriorSpacing,
                    F::CompoundRequirementInteriorSpacing,
                    node,
                    C::Expressions,
                    2,
                    true);
                break;
            case NK::TypeConstraint:
                Pair(begin, F::TypeConstraintSpaceBefore, F::TypeConstraintSpaceAfter, node, C::Expressions, 3, true);
                break;
            case NK::RequiresExpression:
            case NK::RequiresClause:
                KeywordParenthesis(node, F::RequiresSpaceBeforeParentheses, C::Expressions);
                break;
            case NK::StaticAssertDeclaration:
                KeywordParenthesis(node, F::StaticAssertSpaceBeforeParentheses, C::DeclarationHeaders);
                break;
            default:
                break;
        }

        if (Tree_.Kind(end - 1) == TK::Operator_Semicolon) {
            if (IsDeclaration(kind)) {
                Mark(end - 1, F::DeclarationSemicolonSpacing, node, C::DeclarationHeaders, 3);
            } else if (kind == NK::ExpressionStatement || kind == NK::DeclarationStatement ||
                kind == NK::ReturnStatement || kind == NK::ThrowStatement || kind == NK::BreakStatement ||
                kind == NK::ContinueStatement || kind == NK::GotoStatement || kind == NK::CoYieldStatement ||
                kind == NK::DoWhileStatement || kind == NK::SimpleRequirement || kind == NK::TypeRequirement ||
                kind == NK::CompoundRequirement || kind == NK::NestedRequirement)
            {
                Mark(end - 1, F::StatementSemicolonSpacing, node, statementCategory);
            }
        }

    }

    template <bool CollectFlat>
    void Direct(const SourceRegion& node, Context& state, TI begin, TI end, TI flatCandidate = NoToken)
    {
        if constexpr (CollectFlat) {
            if (flatCandidate != NoToken) {
                DirectImpl<true>(node, state, begin, end, flatCandidate);
                return;
            }
        }
        DirectImpl<false>(node, state, begin, end, NoToken);
    }

    template <bool CollectFlat>
    void DirectImpl(const SourceRegion& node, Context& state, TI begin, TI end, TI flatCandidate)
    {
        // Enum macro fields are a preprocessor sequence, not C++ expressions.
        // Its layout planner owns only the validated tuple boundaries.
        if (node.Kind == NK::MacroEnumDeclaration || node.Kind == NK::MacroEnumHeader ||
            node.Kind == NK::MacroEnumEntry)
        {
            return;
        }

        // Capture initializer ranges are raw, not parsed expression children.
        // Capture() handles their confirmed prefix and preserves internals.
        if (node.Kind == NK::LambdaCapture && !state.UncertainSyntax) {
            return;
        }

        const auto& owner = state.Owner != NoRegion ? Regions_[state.Owner] : node;

        for (TI i = begin; i < end; ++i) {
            const auto kind = Tree_.Kind(i);

            if constexpr (CollectFlat) {
                Flat_->Token(flatCandidate, i, kind);
            }

            if (state.UncertainSyntax) {
                PreserveUncertain(i, owner);
                PreserveUncertain(i + 1, owner);
                continue;
            }

            if (node.Kind == NK::NewTypeId && i < state.NewTypeSkip) {
                i = std::min(end, state.NewTypeSkip) - 1;
                continue;
            }

            const auto role = Tree_.Role(i);

            if ((node.Kind == NK::IdentifierExpression || node.Kind == NK::LiteralExpression ||
                node.Kind == NK::Expression) && kind >= TK::Identifier && kind <= TK::Literal_String &&
                role == PR::None)
            {
                if (i != 0 && Gaps_[i].Priority == 0) {
                    Gaps_[i].Owner = Id(owner);
                    Gaps_[i].Category = state.Category;
                }

                continue;
            }

            if (node.Kind == NK::AsmDeclaration && state.TypeOperandEnd == 0) {
                if (kind == TK::Operator_OpenParenthesis) {
                    const auto close = Tree_.MatchingDelimiter(i);

                    if (close != NoToken && close < node.Tokens.End) {
                        Mark(i, F::AsmSpaceBeforeParentheses, node, C::DeclarationHeaders, 2);
                        Interior(
                            i,
                            close + 1,
                            F::AsmInteriorSpacing,
                            F::AsmInteriorSpacing,
                            node,
                            C::DeclarationHeaders);
                        state.TypeOperandEnd = close + 1;
                    }
                } else if (i > node.Tokens.Begin && IsWord(kind) && IsWord(Tree_.Kind(i - 1))) {
                    Mark(i, F::AsmKeywordSpacing, node, C::DeclarationHeaders, 1, true);
                }
            }

            if (IsControl(node.Kind) && i > node.Tokens.Begin &&
                ((kind == TK::Keyword_constexpr || kind == TK::Keyword_consteval || kind == TK::Keyword_co_await) &&
                    (Tree_.Kind(i - 1) == TK::Keyword_if || Tree_.Kind(i - 1) == TK::Keyword_for)))
            {
                Mark(i, F::ControlKeywordModifierSpacing, node, C::ControlHeaders, 1, true);
            }

            if (node.Kind == NK::IfStatement && i == node.Tokens.Begin + 1 && kind == TK::Operator_Not &&
                i + 1 < node.Tokens.End && Tree_.Kind(i + 1) == TK::Keyword_consteval)
            {
                Mark(i, F::ControlKeywordModifierSpacing, node, C::ControlHeaders, 1, true);
                Mark(i + 1, F::UnaryOperatorSpacing, node, C::ControlHeaders, 1);
            }

            if (kind == TK::Operator_Semicolon && owner.Kind == NK::RequirementBody) {
                Mark(i, F::StatementSemicolonSpacing, owner, C::Bodies, 2);
            }

            if (node.Kind == NK::BaseSpecifier && i > node.Tokens.Begin && IsWord(Tree_.Kind(i - 1)) && IsWord(kind)) {
                Mark(i, F::BaseSpecifierSpacing, node, C::BaseLists, 1, true);
            }

            if (node.Kind == NK::Declarator && i > node.Tokens.Begin &&
                (kind == TK::Keyword_const || kind == TK::Keyword_volatile) &&
                (Tree_.Kind(i - 1) == TK::Keyword_const || Tree_.Kind(i - 1) == TK::Keyword_volatile))
            {
                Mark(i, F::PointerQualifierSpacing, owner, C::DeclarationHeaders, 1, true);
            }

            if (node.Kind == NK::RecordDeclaration && i > node.Tokens.Begin && kind == TK::Identifier &&
                (Tree_.Kind(i - 1) == TK::Keyword_class || Tree_.Kind(i - 1) == TK::Keyword_struct ||
                    Tree_.Kind(i - 1) == TK::Keyword_union))
            {
                Mark(i, F::RecordNameSpacing, node, C::DeclarationHeaders, 1, true);
            }

            if (node.Kind == NK::EnumDeclaration && i > node.Tokens.Begin) {
                if (Tree_.Kind(i - 1) == TK::Keyword_enum && (kind == TK::Keyword_class || kind == TK::Keyword_struct))
                {
                    Mark(i, F::EnumKeywordSpacing, node, C::DeclarationHeaders, 1, true);
                } else if (kind == TK::Identifier &&
                    (Tree_.Kind(i - 1) == TK::Keyword_enum || Tree_.Kind(i - 1) == TK::Keyword_class ||
                        Tree_.Kind(i - 1) == TK::Keyword_struct))
                {
                    Mark(i, F::EnumNameSpacing, node, C::DeclarationHeaders, 1, true);
                }
            }

            if (node.Kind == NK::NamespaceDeclaration && i > node.Tokens.Begin &&
                Tree_.Kind(i - 1) == TK::Keyword_namespace && kind == TK::Identifier)
            {
                Mark(i, F::NamespaceNameSpacing, node, C::DeclarationHeaders, 1, true);
            }

            if (node.Kind == NK::ModulePartition && kind == TK::Operator_Colon) {
                Pair(
                    i,
                    F::ModulePartitionColonSpaceBefore,
                    F::ModulePartitionColonSpaceAfter,
                    node,
                    C::DeclarationHeaders,
                    1);
            }

            if (node.Kind == NK::TemplateParameter && i > node.Tokens.Begin && kind == TK::Identifier) {
                auto before = i;

                while (before > node.Tokens.Begin && Tree_.Kind(before - 1) == TK::Comment) {
                    --before;
                }

                auto after = i + 1;

                while (after < node.Tokens.End && Tree_.Kind(after) == TK::Comment) {
                    ++after;
                }

                const auto previous = before > node.Tokens.Begin ? Tree_.Kind(before - 1) : TK::EndOfFile;
                const bool constrained = (Tree_.Kind(node.Tokens.Begin) == TK::Identifier ||
                    Tree_.Kind(node.Tokens.Begin) == TK::Operator_ScopeResolution) &&
                    (previous == TK::Identifier ||
                        (before > node.Tokens.Begin && Tree_.Role(before - 1) == PR::TemplateDelimiter)) &&
                    (after == node.Tokens.End || Tree_.Kind(after) == TK::Operator_Assign);

                if (previous == TK::Keyword_class || previous == TK::Keyword_typename || constrained) {
                    Mark(i, F::TemplateParameterNameSpacing, owner, C::TemplateParameters, 1, true);
                }
            }

            if ((node.Kind == NK::IfStatement && kind == TK::Keyword_else) ||
                (node.Kind == NK::DoWhileStatement && kind == TK::Keyword_while))
            {
                Mark(i, F::ControlClauseSpacing, node, C::ControlHeaders, 3, true);
            }

            if (IsSpecifierContainer(node.Kind)) {
                if (kind == TK::Operator_OpenParenthesis || kind == TK::Operator_OpenBracket) {
                    const auto close = Tree_.MatchingDelimiter(i);

                    if (close != NoToken && close < node.Tokens.End) {
                        // Type operands can contain raw expression tokens.
                        state.TypeOperandEnd = close + 1;
                    }
                }

                if (i > node.Tokens.Begin && i >= state.TypeOperandEnd && IsWord(Tree_.Kind(i - 1)) && IsWord(kind)) {
                    Mark(i, F::DeclarationSpecifierSpacing, owner, state.Category, 0, true);
                }
            }

            if (node.Kind == NK::DeclaratorName && i == node.Tokens.Begin && node.Parent != NoRegion &&
                Regions_[node.Parent].Kind == NK::Declarator && i > Regions_[node.Parent].Tokens.Begin &&
                IsWord(Tree_.Kind(i - 1)) && IsWord(kind))
            {
                Mark(i, F::DeclarationNameSpacing, owner, state.Category, 0, true);
            }

            if (i != 0 && Gaps_[i].Priority == 0 && node.Kind != NK::UnparsedSyntax &&
                node.Kind != NK::AmbiguousStatement && node.Kind != NK::UnexpectedToken && node.Kind != NK::Error &&
                role != PR::AmbiguousOperator)
            {
                Gaps_[i].Owner = Id(owner);
                Gaps_[i].Category = state.Category;
            }

            if (kind == TK::Operator_Comma && role != PR::ListSeparator && role != PR::BinaryOperator) {
                // Raw declarator syntax can contain opaque macro arguments.
                // Without a list or expression role, canonical fallback must
                // not erase the original separation around their commas.
                PreserveUncertain(i, owner);
                PreserveUncertain(i + 1, owner);
            } else if (kind == TK::Operator_Comma) {
                if (state.UncertainOperands && role == PR::ListSeparator && state.ListOwner != NoRegion) {
                    PreserveUncertain(i, Regions_[state.ListOwner]);
                    PreserveUncertain(i + 1, Regions_[state.ListOwner]);
                    continue;
                }

                const bool separator = role == PR::ListSeparator && state.ListOwner != NoRegion;
                const auto& listOwner = separator ? Regions_[state.ListOwner] : owner;
                const auto category = separator ? state.ListCategory : state.Category;
                Mark(i, F::CommaSpaceBefore, listOwner, category, 3);

                // A trailing comma's right gap is a closer edge.
                if (i + 1 < Tree_.TokenCount() && Tree_.Kind(i + 1) != TK::Operator_CloseBrace &&
                    Tree_.Kind(i + 1) != TK::Operator_CloseParenthesis &&
                    Tree_.Kind(i + 1) != TK::Operator_CloseBracket && Tree_.Role(i + 1) != PR::TemplateDelimiter)
                {
                    Mark(i + 1, F::CommaSpaceAfter, listOwner, category, 3, true);
                }
            } else if (kind == TK::Operator_Semicolon && state.ListCategory == C::ControlHeaders &&
                i < state.ControlHeaderEnd)
            {
                // Header separators override a declaration init-statement's ;.
                Mark(i, F::ControlHeaderSemicolonSpaceBefore, Regions_[state.ListOwner], C::ControlHeaders, 4);

                if (i + 1 < Tree_.TokenCount() && Tree_.Kind(i + 1) != TK::Operator_Semicolon &&
                    Tree_.Kind(i + 1) != TK::Operator_CloseParenthesis)
                {
                    Mark(
                        i + 1,
                        Regions_[state.ListOwner].Kind == NK::ForStatement
                        ? F::LoopSemicolonSpacing
                        : F::ConditionInitSemicolonSpacing,
                        Regions_[state.ListOwner],
                        C::ControlHeaders,
                        4,
                        true);
                }
            } else if (role == PR::BinaryOperator || role == PR::ComparisonOperator || role == PR::ShiftOperator ||
                node.Kind == NK::FoldOperator)
            {
                const bool memberPointer = kind == TK::Operator_PointerToMemberDereference ||
                    kind == TK::Operator_IndirectPointerToMemberDereference;
                const auto field = memberPointer ? F::MemberAccessOperatorsSpacing : F::BinaryOperatorSpacing;
                Pair(i, field, field, owner, C::Expressions, 1, !memberPointer);
            } else if (role == PR::AssignmentOperator ||
                ((node.Kind == NK::Initializer || node.Kind == NK::InitializerElement || node.Kind == NK::Enumerator) &&
                    kind == TK::Operator_Assign) ||
                ((node.Kind == NK::ConceptDeclaration || node.Kind == NK::NamespaceDeclaration) &&
                    kind == TK::Operator_Assign))
            {
                Pair(i, F::AssignmentOperatorSpacing, F::AssignmentOperatorSpacing, owner, state.Category, 1);
            } else if (role == PR::ConditionalDelimiter) {
                Pair(i, F::TernaryOperatorSpacing, F::TernaryOperatorSpacing, owner, C::Expressions, 1, true);
            } else if (role == PR::UnaryOperator) {
                const bool postfix = node.Kind == NK::UnaryExpression && Child(node) && i >= Child(node)->Tokens.End;
                Mark(
                    postfix ? i : i + 1,
                    postfix ? F::PostfixOperatorSpacing : F::UnaryOperatorSpacing,
                    owner,
                    C::Expressions,
                    1);
            } else if (kind == TK::Operator_ScopeResolution && role != PR::AmbiguousOperator &&
                node.Kind != NK::OperatorFunctionName)
            {
                // Leading global qualification has no left qualifier edge,
                // even after specifiers inside the same type production.
                if (HasScopeQualifier(node, i)) {
                    Mark(i, F::ScopeResolutionSpacing, owner, state.Category, 1);
                } else if (i > node.Tokens.Begin) {
                    Mark(i, F::LeadingScopeResolutionSpacing, owner, state.Category, 1, true);
                }

                Mark(i + 1, F::ScopeResolutionSpacing, owner, state.Category, 1);
            } else if (node.Kind == NK::MemberExpression &&
                (kind == TK::Operator_ClassMemberAccess || kind == TK::Operator_IndirectClassMemberAccess ||
                    kind == TK::Operator_PointerToMemberDereference ||
                    kind == TK::Operator_IndirectPointerToMemberDereference))
            {
                Pair(i, F::MemberAccessOperatorsSpacing, F::MemberAccessOperatorsSpacing, node, C::Chains, 1, false);
            }

            if (node.Kind == NK::NewTypeId && (kind == TK::Operator_OpenParenthesis || kind == TK::Operator_OpenBrace))
            {
                const auto close = Tree_.MatchingDelimiter(i);

                if (close != NoToken && close < node.Tokens.End) {
                    state.NewTypeSkip = close + 1;
                    i = std::min(end, state.NewTypeSkip) - 1;
                    continue;
                }
            }

            if (node.Kind == NK::NewTypeId && kind == TK::Operator_OpenBracket && role != PR::AttributeDelimiter) {
                const auto close = Tree_.MatchingDelimiter(i);

                if (close != NoToken && close < node.Tokens.End) {
                    Mark(i, F::NewArraySpaceBeforeBrackets, owner, C::Expressions, 3);
                    Interior(
                        i,
                        close + 1,
                        F::NewArrayInteriorSpacing,
                        F::EmptyNewArrayInteriorSpacing,
                        owner,
                        C::Expressions,
                        3);
                    // A size can contain subscripts. Only the outer
                    // bracket is a new array declarator. Retain its boundary
                    // across direct spans interrupted by structured children.
                    state.NewTypeSkip = close + 1;
                    i = std::min(end, state.NewTypeSkip) - 1;
                    continue;
                }
            }

            if (node.Kind == NK::RecordDeclaration && kind == TK::Operator_OpenBrace && role == PR::RecordDelimiter) {
                const auto close = Tree_.MatchingDelimiter(i);
                Interior(
                    i,
                    close == NoToken ? i : close + 1,
                    F::RecordBodyInteriorSpacing,
                    F::RecordBodyInteriorSpacing,
                    node,
                    C::Bodies,
                    3,
                    true);
                F brace = F::None;
                switch (Tree_.Kind(node.Tokens.Begin)) {
                    case TK::Keyword_class:
                        brace = F::ClassBraceSpacing;
                        break;
                    case TK::Keyword_struct:
                        brace = F::StructBraceSpacing;
                        break;
                    case TK::Keyword_union:
                        brace = F::UnionBraceSpacing;
                        break;
                    default:
                        break;
                }

                if (brace != F::None) {
                    Mark(i, brace, node, C::DeclarationHeaders, 3, true);
                }
            }

            if (node.Kind == NK::NamespaceDeclaration && kind == TK::Operator_OpenBrace) {
                const auto close = Tree_.MatchingDelimiter(i);

                if (close != NoToken && close < node.Tokens.End && close + 1 < Tree_.TokenCount() &&
                    Tree_.Kind(close + 1) == TK::Comment && Tree_.TokenText(close + 1).starts_with("//") &&
                    Tree_.WhitespaceBefore(close + 1).find_first_not_of(" \t") == std::string_view::npos)
                {
                    Gaps_[close + 1].NamespaceClosingComment = true;
                }

                Interior(
                    i,
                    close == NoToken ? i : close + 1,
                    F::NamespaceBodyInteriorSpacing,
                    F::NamespaceBodyInteriorSpacing,
                    node,
                    C::Bodies,
                    3,
                    true);
                Mark(i, F::NamespaceBraceSpacing, node, C::DeclarationHeaders, 3, true);
            }

            if (node.Kind == NK::EnumDeclaration && kind == TK::Operator_Colon) {
                Pair(
                    i,
                    F::EnumUnderlyingTypeColonSpaceBefore,
                    F::EnumUnderlyingTypeColonSpaceAfter,
                    node,
                    C::DeclarationHeaders,
                    3);
            }

            if (kind >= TK::Keyword_alignas) {
                KeywordAt(node, state, i);
            }

            if (kind == TK::Operator_Ellipsis &&
                (node.Kind == NK::TemplateParameter || node.Kind == NK::Declarator ||
                    (node.Kind == NK::InitDeclaratorList && node.Parent != NoRegion &&
                        Regions_[node.Parent].Kind == NK::ParameterDeclaration) ||
                    (node.Kind == NK::TypeId && i + 1 == node.Tokens.End) ||
                    (node.Kind == NK::UnaryExpression &&
                        (Tree_.Kind(node.Tokens.Begin) == TK::Keyword_sizeof ||
                            (i + 1 == node.Tokens.End && Child(node) && Child(node)->Tokens.End == i)))))
            {
                PackEllipsis(node, state, i);
            }

            if (kind == TK::Comment && i > 0 && Tree_.TokenText(i).starts_with("//") &&
                Tree_.Kind(i - 1) != TK::Comment && Tree_.Kind(i - 1) != TK::PreprocessingDirective &&
                Tree_.WhitespaceBefore(i).find('\n') == std::string_view::npos)
            {
                Mark(i, F::TrailingCommentSpacing, owner, state.Category, 3, true);
            }
        }
    }

    void SequenceBoundary(const SourceRegion& container, const SourceRegion& previous, const SourceRegion& current)
    {
        const auto isItem = [] (NK kind) {
            if (IsDeclaration(kind) || IsControl(kind)) {
                return true;
            }

            switch (kind) {
                case NK::FunctionDefinition:
                case NK::NamespaceDeclaration:
                case NK::ExportDeclaration:
                case NK::LinkageSpecification:
                case NK::CompoundStatement:
                case NK::ExpressionStatement:
                case NK::ReturnStatement:
                case NK::BreakStatement:
                case NK::ContinueStatement:
                case NK::GotoStatement:
                case NK::ThrowStatement:
                case NK::CoYieldStatement:
                case NK::CaseLabel:
                case NK::LabelStatement:
                case NK::AccessSpecifier:
                case NK::Comment:
                case NK::AttributeSpecifier:
                case NK::ExtensionAttributeSpecifier:
                case NK::Requirement:
                case NK::SimpleRequirement:
                case NK::CompoundRequirement:
                case NK::TypeRequirement:
                case NK::NestedRequirement:
                    return true;
                default:
                    return false;
            }
        };
        switch (container.Kind) {
            case NK::TranslationUnit:
            case NK::PreprocessingBranch:
            case NK::CompoundStatement:
            case NK::LambdaBody:
            case NK::RequirementBody:
            case NK::RecordDeclaration:
            case NK::NamespaceDeclaration:
            case NK::LinkageSpecification:
                break;
            case NK::ExportDeclaration: {
                // Export also wraps a single unbraced declaration. Only direct
                // sibling items inside its matched tail braces form a sequence.
                const auto close = container.Tokens.End - 1;

                if (Tree_.Kind(close) != TK::Operator_CloseBrace) {
                    return;
                }

                const auto open = Tree_.MatchingDelimiter(close);

                if (open == NoToken || previous.Tokens.Begin <= open || current.Tokens.End > close) {
                    return;
                }

                break;
            }
            default:
                return;
        }

        if (!previous.Tokens.Empty() && !current.Tokens.Empty() && previous.Tokens.End == current.Tokens.Begin &&
            isItem(previous.Kind) && isItem(current.Kind))
        {
            // A complete direct sibling boundary is a sequence edge, not a
            // semicolon heuristic. Specific attribute/comment/colon rules win.
            Mark(
                current.Tokens.Begin,
                F::StatementSequenceSpacing,
                container,
                container.Kind == NK::LambdaBody ? C::LambdaBodies : C::Bodies,
                1,
                true);
            // A specific policy still owns normalization, but a preserved
            // label/access/attribute edge needs the safe sequence fallback.
            auto& gap = Gaps_[current.Tokens.Begin];

            if ((static_cast<uint8_t>(gap.Constraint) & static_cast<uint8_t>(SpacingConstraint::UncertainSyntax)) == 0)
            {
                gap.FallbackSpace = true;
            }
        }
    }

    void DeclarationBoundary(const SourceRegion& declaration, const SourceRegion& specifiers, const SourceRegion& list)
    {
        if ((!IsDeclaration(declaration.Kind) && declaration.Kind != NK::FunctionDefinition &&
            declaration.Kind != NK::ParameterDeclaration && declaration.Kind != NK::ForInitStatement &&
            declaration.Kind != NK::ForCondition && declaration.Kind != NK::RangeForDeclaration) ||
            specifiers.Kind != NK::DeclarationSpecifiers || list.Kind != NK::InitDeclaratorList ||
            specifiers.Tokens.Empty() || specifiers.Tokens.End != list.Tokens.Begin)
        {
            return;
        }

        const auto* init = Child(list);
        const auto* declarator = init ? Child(*init) : nullptr;
        const auto* name = declarator ? Child(*declarator) : nullptr;

        // Proven qualifier template lists precede the terminal name without
        // changing the return-type / complete-qualified-name boundary.
        while (name && name->Kind == NK::TemplateArgumentList) {
            name = Next(*name);
        }

        if (!init || init->Kind != NK::InitDeclarator || !declarator || declarator->Kind != NK::Declarator || !name ||
            (name->Kind != NK::DeclaratorName && name->Kind != NK::DestructorName &&
                name->Kind != NK::OperatorFunctionName) || name->Tokens.Empty())
        {
            // Pointer/nested/member-pointer/attribute entries are
            // distinct boundaries. Their specific rules, including Preserve,
            // must not acquire a direct-name fallback.
            return;
        }

        if (Tree_.Kind(list.Tokens.Begin) == TK::Operator_ScopeResolution) {
            // A qualified conversion operator can follow specifiers containing
            // its class name. This is not a return-type/name boundary.
            return;
        }

        // A qualified declarator starts before its terminal name child.
        // Separate the complete specifiers from the whole qualified name,
        // including a destructor's '~', not merely its final identifier.
        const auto first = Tree_.Kind(list.Tokens.Begin);
        // A pack marker or recovered opener is not a type/name policy site.
        const bool nameStart =
            first == TK::Identifier || (name->Kind == NK::DestructorName && first == TK::Operator_Complement) ||
                (name->Kind == NK::OperatorFunctionName && first == TK::Keyword_operator);
        Mark(
            list.Tokens.Begin,
            nameStart ? F::DeclarationNameSpacing : F::None,
            declaration,
            declaration.Kind == NK::ParameterDeclaration
            ? C::Parameters
            : declaration.Kind == NK::ForInitStatement || declaration.Kind == NK::ForCondition ||
                declaration.Kind == NK::RangeForDeclaration
                ? C::ControlHeaders
                : C::DeclarationHeaders,
            1,
            true);
    }

    void Finish(const SourceRegion& node)
    {
        if (node.Kind != NK::SubscriptExpression || !Child(node)) {
            return;
        }

        const auto open = Child(node)->Tokens.End;

        if (open < Tree_.TokenCount() &&
            (static_cast<uint8_t>(Gaps_[open].Constraint) & static_cast<uint8_t>(SpacingConstraint::UncertainSyntax)) !=
                0)
        {
            // Propagate an uncertain base through subscript edges in O(1),
            // rather than rescanning the base subtree at each chain suffix.
            PreserveUncertain(open, node);
            PreserveUncertain(open + 1, node);
            PreserveUncertain(node.Tokens.End - 1, node);
            PreserveUncertain(node.Tokens.End, node);
        }
    }

private:
    void ArgumentComments(const SourceRegion& node, const SourceRegion& list, C category)
    {
        if (node.Status != RegionStatus::Complete) {
            return;
        }

        const auto separator = [&] (TI token) {
            return Tree_.Kind(token) == TK::Operator_Comma && Tree_.Role(token) == PR::ListSeparator;
        };
        const auto markRun = [&] (TI first, TI last) {
            for (auto gap = first + 1; gap < last; ++gap) {
                if (Tree_.TokenText(gap - 1).starts_with("/*") && Tree_.TokenText(gap).starts_with("/*")) {
                    Mark(gap, F::ArgumentCommentSpacing, list, category, 4);
                }
            }
        };
        // Own the argument-facing gap and gaps between consecutive block
        // comments in its attached runs, but not separator-facing gaps.
        const auto begin = node.Tokens.Begin;
        auto prefix = begin;

        while (prefix > list.Tokens.Begin + 1 && Tree_.Kind(prefix - 1) == TK::Comment) {
            --prefix;
        }

        if (prefix < begin && (prefix == list.Tokens.Begin + 1 || separator(prefix - 1))) {
            markRun(prefix, begin);

            if (Tree_.TokenText(begin - 1).starts_with("/*")) {
                const auto field = IsArgumentNameComment(Tree_.TokenText(begin - 1))
                    ? F::ArgumentNameCommentSpacing
                    : F::ArgumentCommentSpacing;
                Mark(begin, field, list, category, 4);
            }
        }

        const auto end = node.Tokens.End;
        const auto closer = list.Tokens.End - 1;
        auto suffix = end;

        while (suffix < closer && Tree_.Kind(suffix) == TK::Comment) {
            ++suffix;
        }

        if (suffix > end && (suffix == closer || separator(suffix))) {
            markRun(end, suffix);

            if (Tree_.TokenText(end).starts_with("/*")) {
                Mark(end, F::ArgumentCommentSpacing, list, category, 4);
            }
        }
    }

    void Capture(const SourceRegion& node, const SourceRegion* parent)
    {
        const auto end = node.Tokens.End;
        auto cursor = node.Tokens.Begin;
        const auto next = [&] (TI token) {
            while (token < end && Tree_.Kind(token) == TK::Comment) {
                ++token;
            }

            return token;
        };
        cursor = next(cursor);

        if (cursor < end && Tree_.Kind(cursor) == TK::Operator_Ampersand) {
            cursor = next(cursor + 1);
        }

        if (cursor < end && Tree_.Kind(cursor) == TK::Operator_Ellipsis) {
            PackEllipsis(node, {.Owner = Id(node), .Category = C::Captures}, cursor);
            cursor = next(cursor + 1);
        }

        TI unknown = node.Tokens.Begin + 1;

        if (cursor < end && Tree_.Kind(cursor) == TK::Identifier) {
            cursor = next(cursor + 1);
            unknown = cursor;

            if (cursor < end && Tree_.Kind(cursor) == TK::Operator_Assign && next(cursor + 1) < end && parent &&
                parent->Kind == NK::LambdaCaptureList)
            {
                Pair(cursor, F::SpaceAroundCaptureAssignment, F::SpaceAroundCaptureAssignment, *parent, C::Captures, 1);
                // The separator's right edge is owned; everything after the
                // first initializer token is unparsed, including later '='.
                unknown = cursor + 2;
            } else if (cursor < end && Tree_.Kind(cursor) == TK::Operator_Ellipsis && next(cursor + 1) == end) {
                PackEllipsis(node, {.Owner = Id(node), .Category = C::Captures}, cursor);
                // A simple pack capture has a suffix, not an initializer.
                unknown = end;
            }
        }

        // This disjoint raw range is visited once, never searched for '=' or
        // parsed speculatively. Unknown internals cannot borrow outer policies.
        for (TI i = unknown; i < end; ++i) {
            PreserveUncertain(i, node);
        }
    }

    bool SizeofPackOperand(const SourceRegion& node, const SourceRegion* parent) const
    {
        if (!parent || parent->Kind != NK::UnaryExpression || Tree_.Kind(parent->Tokens.Begin) != TK::Keyword_sizeof) {
            return false;
        }

        auto cursor = parent->Tokens.Begin + 1;

        while (cursor < node.Tokens.Begin && Tree_.Kind(cursor) == TK::Comment) {
            ++cursor;
        }

        if (cursor >= node.Tokens.Begin || Tree_.Kind(cursor) != TK::Operator_Ellipsis) {
            return false;
        }

        ++cursor;

        while (cursor < node.Tokens.Begin && Tree_.Kind(cursor) == TK::Comment) {
            ++cursor;
        }

        return cursor == node.Tokens.Begin;
    }

    bool ControlKeywordBoundary(TI open, const SourceRegion* parent) const
    {
        if (!parent || !IsControl(parent->Kind)) {
            return false;
        }

        TI previous = open;

        while (previous > parent->Tokens.Begin && Tree_.Kind(previous - 1) == TK::Comment) {
            --previous;
        }

        if (previous == parent->Tokens.Begin) {
            return false;
        }

        const auto kind = Tree_.Kind(previous - 1);
        switch (parent->Kind) {
            case NK::IfStatement:
                return kind == TK::Keyword_if || kind == TK::Keyword_constexpr;
            case NK::ForStatement:
                return kind == TK::Keyword_for || kind == TK::Keyword_co_await;
            case NK::WhileStatement:
            case NK::DoWhileStatement:
                return kind == TK::Keyword_while;
            case NK::SwitchStatement:
                return kind == TK::Keyword_switch;
            case NK::CatchClause:
                return kind == TK::Keyword_catch;
            default:
                return false;
        }
    }

    void KeywordParenthesis(const SourceRegion& node, F field, C category)
    {
        const auto open = node.Tokens.Begin + 1;

        if (open < node.Tokens.End && Tree_.Kind(open) == TK::Operator_OpenParenthesis &&
            Tree_.MatchingDelimiter(open) < node.Tokens.End)
        {
            Mark(open, field, node, category, 4, true);
        }
    }

    void KeywordAt(const SourceRegion& node, Context state, TI i)
    {
        if (i + 1 >= node.Tokens.End) {
            return;
        }

        F field = F::None;
        const auto kind = Tree_.Kind(i);

        if (kind == TK::Keyword_typename && IsSpecifierContainer(node.Kind)) {
            Mark(i + 1, F::DependentTypeNameSpacing, node, state.Category, 1, true);
        }

        if (kind == TK::Keyword_template && Tree_.Kind(i + 1) == TK::Identifier &&
            (node.Kind == NK::MemberExpression || node.Kind == NK::IdentifierExpression ||
                node.Kind == NK::DeclaratorName || IsSpecifierContainer(node.Kind) || node.Kind == NK::BaseType))
        {
            Mark(i + 1, F::DependentTemplateNameSpacing, node, state.Category, 1, true);
        }

        // Keyword tokens are considered only in their parsed production; an
        // Identifier spellings resembling keywords are irrelevant.
        if (node.Kind == NK::UnaryExpression) {
            switch (kind) {
                case TK::Keyword_sizeof:
                    field = F::SizeofSpaceBeforeParentheses;
                    break;
                case TK::Keyword_alignof:
                    field = F::AlignofSpaceBeforeParentheses;
                    break;
                case TK::Keyword_co_await:
                    Mark(i + 1, F::CoAwaitOperandSpacing, node, C::Expressions, 2, true);
                    field = F::CoAwaitSpaceBeforeParentheses;
                    break;
                default:
                    break;
            }

            if (kind == TK::Keyword_sizeof && i + 2 < node.Tokens.End && Tree_.Kind(i + 1) == TK::Operator_Ellipsis &&
                Tree_.Kind(i + 2) == TK::Operator_OpenParenthesis)
            {
                Mark(i + 2, F::SizeofPackSpaceBeforeParentheses, node, C::Expressions, 4, true);
            }
        } else if (node.Kind == NK::CallExpression) {
            if (kind == TK::Keyword_typeid) {
                field = F::TypeidSpaceBeforeParentheses;
            } else if (kind == TK::Keyword_noexcept) {
                field = F::NoexceptSpaceBeforeParentheses;
            }
        } else if (node.Kind == NK::DeclarationSpecifiers || node.Kind == NK::TypeId || node.Kind == NK::NewTypeId ||
            node.Kind == NK::UsingDeclaration || node.Kind == NK::TrailingReturnType ||
            node.Kind == NK::RecordDeclaration || node.Kind == NK::EnumDeclaration)
        {
            if (kind == TK::Keyword_decltype) {
                field = F::DecltypeSpaceBeforeParentheses;
            } else if (kind == TK::Keyword_alignas) {
                field = F::AlignasSpaceBeforeParentheses;
            }
        }

        if (field == F::None) {
            return;
        }

        auto opening = i + 1;

        while (opening < node.Tokens.End && Tree_.Kind(opening) == TK::Comment) {
            ++opening;
        }

        if (opening < node.Tokens.End && Tree_.Kind(opening) == TK::Operator_OpenParenthesis &&
            Tree_.MatchingDelimiter(opening) < node.Tokens.End)
        {
            Mark(opening, field, state.Owner != NoRegion ? Regions_[state.Owner] : node, state.Category, 4, true);

            if (kind == TK::Keyword_alignas) {
                auto after = Tree_.MatchingDelimiter(opening) + 1;

                // An alignment specifier ends before the next header
                // component. Specific attribute/brace policies still win.
                while (after < node.Tokens.End) {
                    Mark(after, F::AlignasSpaceAfterSpecifier, node, state.Category, 1, true);

                    if (Tree_.Kind(after) != TK::Comment) {
                        break;
                    }

                    ++after;
                }
            }
        }
    }

    void Pointer(const SourceRegion& node, Context state)
    {
        const auto begin = node.Tokens.Begin;
        const auto end = node.Tokens.End;
        const bool pack = end < Tree_.TokenCount() && Tree_.Kind(end) == TK::Operator_Ellipsis;

        if (Binding_ != PointerReferenceDeclaratorLocation::Preserve) {
            Mark(begin, F::PointerBinding, node, C::DeclarationHeaders, 1);
            auto& before = Gaps_[begin];

            if (before.Field == F::PointerBinding) {
                const bool previousPointer = begin > 0 &&
                    (Tree_.Role(begin - 1) == PR::PointerDeclarator ||
                        Tree_.Role(begin - 1) == PR::ReferenceDeclarator);
                before.Policy = previousPointer || Binding_ == PointerReferenceDeclaratorLocation::Left
                    ? GapPolicy::NoSpace
                    : GapPolicy::Space;

                // Each comma-separated declarator has its own pointer, not a
                // shared type-bound pointer. Do not attach it to the comma.
                if (begin > 0 && Tree_.Kind(begin - 1) == TK::Operator_Comma) {
                    before.Policy = GapPolicy::Space;
                }
            }

            Mark(end, F::PointerBinding, node, C::DeclarationHeaders, 1);

            if (end < Tree_.TokenCount() && Gaps_[end].Field == F::PointerBinding) {
                const bool attachName = Binding_ == PointerReferenceDeclaratorLocation::Right ||
                    (state.SeparatedDeclarator && Binding_ == PointerReferenceDeclaratorLocation::Left);
                Gaps_[end].Policy = attachName || pack ? GapPolicy::NoSpace : GapPolicy::Space;
            }
        }

        // A pack ellipsis belongs to the declarator operator, not its name.
        // Name-spacing overrides must not detach it from a pointer/reference.
        if (pack) {
            return;
        }

        if (node.Kind == NK::MemberPointerOperator) {
            Mark(end, F::MemberPointerDeclaratorSpacing, node, C::DeclarationHeaders, 4);
        } else if (state.NestedDeclarator) {
            Mark(end, F::NestedDeclaratorPointerSpacing, node, C::DeclarationHeaders, 4);
        }
    }

    void Cast(const SourceRegion& node)
    {
        const auto begin = node.Tokens.Begin;
        const auto end = node.Tokens.End;

        if (Tree_.Kind(begin) == TK::Operator_OpenParenthesis) {
            const auto close = Tree_.MatchingDelimiter(begin);

            if (close < end) {
                Interior(begin, close + 1, F::CastArgumentSpacing, F::CastArgumentSpacing, node, C::Expressions, 3);
                Mark(close + 1, F::CastSpacing, node, C::Expressions, 3);
            }
        } else if (Child(node) && Child(node)->Kind == NK::TemplateArgumentList && begin + 1 < end &&
            Tree_.Kind(begin + 1) == TK::Operator_Less)
        {
            Mark(begin + 1, F::NamedCastSpaceBeforeTypeArguments, node, C::Expressions, 4);
            const auto typeEnd = Child(node)->Tokens.End - 1;

            if (typeEnd + 1 < end && Tree_.Kind(typeEnd + 1) == TK::Operator_OpenParenthesis) {
                Mark(typeEnd + 1, F::NamedCastSpaceBeforeOperandParentheses, node, C::Expressions, 4);
                Interior(typeEnd + 1, end, F::CastArgumentSpacing, F::CastArgumentSpacing, node, C::Expressions, 3);
            }
        }
    }

    const parser::SyntaxTree& Tree_;
    std::array<GapPolicy, size_t(F::Count)> Policies_;
    PointerReferenceDeclaratorLocation Binding_;
    std::span<SyntaxGapPreference> Gaps_;
    std::span<const SourceRegion> Regions_;
    detail::FlatSpanCollector* Flat_;
};

} // namespace

std::string_view SpacingFieldName(SpacingField field)
{
    constexpr std::string_view Names[] = {
        "None",
#define SYNTAX_SPACING_NAME(type, name, initial) #name,
        FORMATTER_GAP_OPTIONS(SYNTAX_SPACING_NAME)
#undef SYNTAX_SPACING_NAME
        "PointerBinding",
    };
    const auto index = static_cast<size_t>(field);
    return index < std::size(Names) ? Names[index] : std::string_view{};
}

bool SupportsSpacingField(SpacingField field)
{
    return field > F::None && field < F::Count && field != F::AmbiguousTemplateDeclarationSpacing;
}

GapContext SpacingOwnerContext(SpacingOwner owner, const CompiledPolicies& policies)
{
    LayoutMode mode = LayoutMode::Preserve;
    switch (owner) {
        case C::Calls:
            mode = policies.Calls.Layout;
            break;
        case C::Parameters:
            mode = policies.Parameters.Layout;
            break;
        case C::TemplateParameters:
            mode = policies.TemplateParameters.Layout;
            break;
        case C::TemplateArguments:
            mode = policies.TemplateArguments.Layout;
            break;
        case C::Initializers:
            mode = policies.Initializers.Layout;
            break;
        case C::Captures:
            mode = policies.Captures.Layout;
            break;
        case C::ControlHeaders:
            mode = policies.ControlHeaders.Layout;
            break;
        case C::BaseLists:
            mode = policies.BaseLists.Items.Layout;
            break;
        case C::ConstructorLists:
            mode = policies.ConstructorLists.Items.Layout;
            break;
        case C::Expressions:
            mode = policies.Expressions.Layout;
            break;
        case C::Chains:
            mode = policies.Chains.Layout;
            break;
        case C::DeclarationHeaders:
            mode = policies.DeclarationHeaders;
            break;
        case C::Bodies:
            mode = policies.Bodies.Layout;
            break;
        case C::LambdaBodies:
            mode = policies.LambdaBodies.Layout;
            break;
        case C::None:
            break;
    }

    return mode == LayoutMode::Canonical ? GapContext::Rebuilt : GapContext::Preserved;
}

SyntaxSpacing::SyntaxSpacing(std::pmr::memory_resource& fileMemory)
    : Gaps_(&fileMemory)
    , FlatSpans_(&fileMemory)
{ }

std::optional<bool> SyntaxSpacing::SpacePreference(
    GapId gap,
    GapContext ownerContext,
    const SourceAnalysis& safety,
    GapAdjacency adjacency) const
{
    if (gap >= Gaps_.size() || gap >= safety.Gaps().size()) {
        return std::nullopt;
    }

    const auto& source = safety.Gaps()[gap];

    if ((static_cast<uint8_t>(Gaps_[gap].Constraint) & static_cast<uint8_t>(SpacingConstraint::UncertainSyntax)) != 0) {
        return std::nullopt;
    }

    if (source.Preserve || source.ContextSensitive || source.LineBoundary == GapLineBoundary::Required ||
        source.MustStartWithLineFeed)
    {
        return std::nullopt;
    }

    if (TrailingCommentSpaces_ && Gaps_[gap].Field == F::TrailingCommentSpacing) {
        return std::nullopt;
    }

    // Inserting a separator makes an attached argument comment face the
    // separator. Closer edges and trailing line comments keep their own rules.
    const auto policy = adjacency == GapAdjacency::AfterComma &&
        (Gaps_[gap].Field == F::ArgumentCommentSpacing || Gaps_[gap].Field == F::ArgumentNameCommentSpacing)
        ? CommaSpaceAfter_
        : Gaps_[gap].Policy;
    auto preference = GapUsesSpace(policy, ownerContext);

    if (preference && source.Separation == GapSeparation::Required && !safety.Original(source).empty()) {
        return true;
    }

    return preference;
}

std::optional<std::uint16_t> SyntaxSpacing::TrailingCommentCount(GapId gap, const SourceAnalysis& safety) const
{
    if (!TrailingCommentSpaces_ || !TrailingCommentSpaces_->Count || gap >= Gaps_.size() || gap >= safety.Gaps().size())
    {
        return std::nullopt;
    }

    const auto& preference = Gaps_[gap];
    const auto& source = safety.Gaps()[gap];

    if (preference.Field != F::TrailingCommentSpacing || preference.NamespaceClosingComment ||
        (static_cast<uint8_t>(preference.Constraint) & static_cast<uint8_t>(SpacingConstraint::UncertainSyntax)) != 0 ||
        source.Preserve || source.ContextSensitive || source.LineBoundary == GapLineBoundary::Required ||
        source.MustStartWithLineFeed || !source.Horizontal)
    {
        return std::nullopt;
    }

    auto count = *TrailingCommentSpaces_->Count;
    if (source.Separation == GapSeparation::Required && !safety.Original(source).empty()) {
        count = std::max<std::size_t>(count, 1);
    }
    return static_cast<std::uint16_t>(count);
}

template <bool CollectFlat>
SyntaxSpacing detail::FlatSpanAccess::BuildSpacing(
    const parser::SyntaxTree& tree,
    const SourceAnalysis& safety,
    const GapPolicies& spacing,
    PointerReferenceDeclaratorLocation pointerBinding,
    std::pmr::memory_resource& fileMemory,
    std::pmr::memory_resource& scratchMemory,
    std::optional<TrailingCommentSpaceSetting> trailingCommentSpaces,
    FlatSpanCollector* flat)
{
    if (trailingCommentSpaces && trailingCommentSpaces->Count && *trailingCommentSpaces->Count > 10000) {
        throw std::invalid_argument("TrailingCommentSpaces must be between 0 and 10000");
    }

    if (safety.Gaps().size() != static_cast<size_t>(tree.TokenCount()) + 1 ||
        safety.Source().data() != tree.Source().data() || safety.Source().size() != tree.Source().size())
    {
        throw std::invalid_argument("syntax spacing requires matching source analysis");
    }

    if (spacing.AmbiguousTemplateDeclarationSpacing != GapPolicy::Preserve) {
        throw std::invalid_argument(
            "AmbiguousTemplateDeclarationSpacing is unsupported: " "ambiguous syntax is preserved");
    }

    SyntaxSpacing result(fileMemory);
    result.CommaSpaceAfter_ = spacing.CommaSpaceAfter;
    result.TrailingCommentSpaces_ = trailingCommentSpaces;
    result.Gaps_.resize(safety.Gaps().size());
    const auto regions = safety.Regions();

    if (regions.empty()) {
        throw std::invalid_argument("syntax spacing requires matching tree regions");
    }

    Classifier classifier(tree, spacing, pointerBinding, result.Gaps_, regions, flat);
    std::pmr::vector<Frame<CollectFlat>> stack(&scratchMemory);
    stack.push_back({0, NoRegion, regions[0].FirstChild, {}, regions[0].Tokens.Begin});

    while (!stack.empty()) {
        auto& frame = stack.back();
        const auto& node = regions[frame.Node];
        const auto* parentNode = frame.Parent == NoRegion ? nullptr : &regions[frame.Parent];

        if (!frame.Entered) {
            if constexpr (CollectFlat) {
                if (node.Kind == NK::CallExpression) {
                    flat->Call(node);
                }

                if (frame.Flat.Candidate != NoToken || FlatSpanCollector::CandidateKind(node.Kind)) {
                    frame.Flat.Candidate = flat->Enter(frame.Node, node, frame.Flat.Candidate);
                }

                frame.Flat.RecoveryBody = FlatSpanCollector::RecoveryBody(node);
            }

            classifier.Enter(node, parentNode, frame.State, node.Status);
            frame.Complete =
                node.Status == RegionStatus::Complete && !frame.State.UncertainSyntax && !frame.State.UncertainOperands;
            frame.Entered = true;
        }

        if (frame.Child != NoRegion) {
            const auto childId = frame.Child;
            const auto& child = regions[childId];

            if constexpr (CollectFlat) {
                if (frame.Flat.RecoveryBody) {
                    flat->BodyChild(frame.Node, child);
                }
            }

            frame.Child = child.NextSibling;
            const auto end = std::min(child.Tokens.Begin, node.Tokens.End);

            if (frame.Cursor < end) {
                if constexpr (CollectFlat)
                    classifier.Direct<true>(node, frame.State, frame.Cursor, end, frame.Flat.Candidate);
                else
                    classifier.Direct<false>(node, frame.State, frame.Cursor, end);
            }

            frame.Cursor = std::max(frame.Cursor, child.Tokens.End);
            auto state = frame.State;

            if (child.Kind == NK::Comment && frame.PreviousChild != NoRegion &&
                IsDeclaration(regions[frame.PreviousChild].Kind))
            {
                state.Owner = frame.PreviousChild;
                state.Category = C::DeclarationHeaders;
            }

            const auto previous = frame.PreviousChild;
            const bool previousComplete = frame.PreviousChildComplete;
            frame.PreviousChild = childId;
            const auto parent = frame.Node;

            if (child.FirstChild == NoRegion) {
                auto candidate = NoToken;

                if constexpr (CollectFlat) {
                    if (child.Kind == NK::CallExpression) {
                        flat->Call(child);
                    }

                    if (frame.Flat.Candidate != NoToken || FlatSpanCollector::CandidateKind(child.Kind)) {
                        candidate = flat->Enter(childId, child, frame.Flat.Candidate);
                    }
                }

                classifier.Enter(child, &node, state, child.Status);
                const bool complete =
                    child.Status == RegionStatus::Complete && !state.UncertainSyntax && !state.UncertainOperands;

                if (!child.Tokens.Empty()) {
                    classifier.Direct<CollectFlat>(child, state, child.Tokens.Begin, child.Tokens.End, candidate);
                }

                classifier.Finish(child);

                if (complete && previous != NoRegion && previousComplete) {
                    classifier.SequenceBoundary(node, regions[previous], child);
                    classifier.DeclarationBoundary(node, regions[previous], child);
                }

                frame.Complete &= complete;
                frame.PreviousChildComplete = complete;
                continue;
            }

            auto candidate = NoToken;

            if constexpr (CollectFlat) {
                candidate = frame.Flat.Candidate;
            }

            stack.push_back({childId, parent, child.FirstChild, state, child.Tokens.Begin});

            if constexpr (CollectFlat) {
                stack.back().Flat.Candidate = candidate;
            }

            stack.back().PreviousSibling = previous;
            stack.back().PreviousSiblingComplete = previousComplete;
        } else {
            if (frame.Cursor < node.Tokens.End) {
                if constexpr (CollectFlat)
                    classifier.Direct<true>(node, frame.State, frame.Cursor, node.Tokens.End, frame.Flat.Candidate);
                else
                    classifier.Direct<false>(node, frame.State, frame.Cursor, node.Tokens.End);
            }

            classifier.Finish(node);

            if (frame.Parent != NoRegion && frame.Complete && frame.PreviousSibling != NoRegion &&
                frame.PreviousSiblingComplete)
            {
                classifier.SequenceBoundary(*parentNode, regions[frame.PreviousSibling], node);
                classifier.DeclarationBoundary(*parentNode, regions[frame.PreviousSibling], node);
            }

            if (stack.size() > 1) {
                auto& parent = stack[stack.size() - 2];
                parent.Complete &= frame.Complete;
                parent.PreviousChildComplete = frame.Complete;
            }

            stack.pop_back();
        }
    }

    auto flatGap = NoToken;

    if constexpr (CollectFlat) {
        flatGap = flat->BeforeGaps();
    }

    for (size_t i = 0; i < result.Gaps_.size(); ++i) {
        auto& gap = result.Gaps_[i];
        const auto& source = safety.Gaps()[i];
        gap.FallbackSpace |= source.Separation == GapSeparation::Required;

        if (i > 0 && i < tree.TokenCount() && gap.Field == F::None && gap.Owner != NoRegion &&
            gap.Category != C::None && gap.Priority != 255)
        {
            const auto blockComment = [&] (TI token) {
                return tree.Kind(token) == TK::Comment && tree.TokenText(token).starts_with("/*");
            };

            if (blockComment(TI(i - 1)) || blockComment(TI(i))) {
                gap.Field = F::BlockCommentSpacing;
                gap.Policy = spacing.BlockCommentSpacing;
                gap.FallbackSpace = true;
            } else if (tree.Kind(TI(i - 1)) == TK::Literal_String && tree.Kind(TI(i)) == TK::Literal_String) {
                gap.Field = F::StringLiteralFragmentSpacing;
                gap.Policy = spacing.StringLiteralFragmentSpacing;
                gap.FallbackSpace = true;
            }
        }

        if (source.Preserve || source.ContextSensitive || source.LineBoundary == GapLineBoundary::Required ||
            source.MustStartWithLineFeed)
        {
            gap.Policy = GapPolicy::Preserve;
            gap.Constraint =
                static_cast<SpacingConstraint>(static_cast<uint8_t>(gap.Constraint) |
                    static_cast<uint8_t>(SpacingConstraint::SourceSafety));
        }

        if constexpr (CollectFlat) {
            if (i == flatGap) {
                flatGap = flat->Gap(TI(i), source, gap);
            }
        }
    }

    if constexpr (CollectFlat) {
        flat->Finish(result, result.FlatSpans_);
    }

    return result;
}

SyntaxSpacing BuildSyntaxSpacing(
    const parser::SyntaxTree& tree,
    const SourceAnalysis& safety,
    const GapPolicies& spacing,
    PointerReferenceDeclaratorLocation pointerBinding,
    std::pmr::memory_resource& fileMemory,
    std::pmr::memory_resource& scratchMemory,
    std::optional<TrailingCommentSpaceSetting> trailingCommentSpaces)
{
    return detail::FlatSpanAccess::BuildSpacing<false>(
        tree,
        safety,
        spacing,
        pointerBinding,
        fileMemory,
        scratchMemory,
        trailingCommentSpaces,
        nullptr);
}

SyntaxSpacing detail::BuildFlatSyntaxSpacing(
    const parser::SyntaxTree& tree,
    const SourceAnalysis& safety,
    const CompiledPolicies& policies,
    const LibraryAnnotations& annotations,
    uint8_t tabWidth,
    std::pmr::memory_resource& fileMemory,
    std::pmr::memory_resource& scratchMemory)
{
    FlatSpanCollector flat(tree, safety, policies, annotations, tabWidth, scratchMemory);
    if (flat.Enabled())
        return FlatSpanAccess::BuildSpacing<true>(
            tree,
            safety,
            policies.Spacing,
            policies.PointerReferenceDeclaratorLocation,
            fileMemory,
            scratchMemory,
            policies.TrailingCommentSpaces,
            &flat);
    return FlatSpanAccess::BuildSpacing<false>(
        tree,
        safety,
        policies.Spacing,
        policies.PointerReferenceDeclaratorLocation,
        fileMemory,
        scratchMemory,
        policies.TrailingCommentSpaces,
        nullptr);
}

} // namespace formatter
