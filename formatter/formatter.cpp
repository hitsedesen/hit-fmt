#include "formatter.hpp"

#include <parser/parser.hpp>
#include <tokenizer/token.hpp>

#include <algorithm>
#include <cctype>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace formatter {
namespace {

using NK = parser::NodeKind;
using TK = tokenizer::TokenKind;
using TokenIndex = parser::TokenIndex;
constexpr TokenIndex NoToken = std::numeric_limits<TokenIndex>::max();

bool HasRecoveryChild(const parser::Node& node)
{
    for (auto* child = node.Child(); child; child = child->NextSibling) {
        if (child->Kind == NK::UnparsedSyntax ||
            child->Kind == NK::MissingToken ||
            child->Kind == NK::UnexpectedToken ||
            child->Kind == NK::Error)
        {
            return true;
        }
    }
    return false;
}

bool IsBaseSpacingKeyword(TK kind)
{
    return kind == TK::Keyword_public ||
        kind == TK::Keyword_protected ||
        kind == TK::Keyword_private ||
        kind == TK::Keyword_virtual;
}

bool IsIncompleteStatement(const parser::Node& node)
{
    return (node.Kind == NK::ExpressionStatement ||
            node.Kind == NK::DeclarationStatement ||
            node.Kind == NK::ReturnStatement ||
            node.Kind == NK::ThrowStatement ||
            node.Kind == NK::CoYieldStatement) &&
        HasRecoveryChild(node);
}

// Token spelling and trivia outside configured gaps remain source-owned.
// Classification uses syntax nodes, not neighboring keywords (which also
// occur in expressions and template arguments).
class Formatter
{
public:
    Formatter(const parser::SyntaxTree& tree, const Style& style)
        : Tree_(tree)
        , Style_(style)
        , Placements_(tree.TokenCount(), BracePlacement::Preserve)
        , Spacings_(tree.TokenCount(), SameLineBraceSpacing::Preserve)
        , LambdaGaps_(tree.TokenCount(), false)
        , LambdaParameterInsideGaps_(tree.TokenCount(), false)
        , CaptureGaps_(tree.TokenCount(), LambdaCaptureSpacing::Preserve)
        , FunctionGaps_(tree.TokenCount(), false)
        , CallGaps_(tree.TokenCount(), false)
        , CallInsideGaps_(tree.TokenCount(), false)
        , ParameterInsideGaps_(tree.TokenCount(), false)
        , ControlGaps_(tree.TokenCount(), false)
        , ControlBraces_(tree.TokenCount(), false)
    {
        Output_.reserve(tree.Source().size());
        if (style.IndentAccessModifiers != BodyIndentation::Preserve ||
            style.IndentMembers != BodyIndentation::Preserve ||
            style.InheritanceList == InheritanceListMode::Format ||
            style.ClassBracePlacement != BracePlacement::Preserve ||
            style.StructBracePlacement != BracePlacement::Preserve ||
            style.UnionBracePlacement != BracePlacement::Preserve ||
            style.FunctionBracePlacement != BracePlacement::Preserve ||
            style.LambdaBracePlacement != BracePlacement::Preserve ||
            style.NamespaceBracePlacement != BracePlacement::Preserve ||
            style.ClassBraceSpacing != SameLineBraceSpacing::Preserve ||
            style.LambdaBraceSpacing != SameLineBraceSpacing::Preserve ||
            style.StructBraceSpacing != SameLineBraceSpacing::Preserve ||
            style.EnumBraceSpacing != SameLineBraceSpacing::Preserve ||
            style.UnionBraceSpacing != SameLineBraceSpacing::Preserve ||
            style.FunctionBraceSpacing != SameLineBraceSpacing::Preserve ||
            style.NamespaceBraceSpacing != SameLineBraceSpacing::Preserve ||
            style.LambdaSpaceBeforeParameters !=
                LambdaSpaceBeforeParameters::Preserve ||
            style.SpaceInsideLambdaParameters !=
                SpaceInsideLambdaParameters::Preserve ||
            style.SpaceAroundCaptureList !=
                LambdaCaptureSpacing::Preserve ||
            style.SpaceAroundCaptureAssignment !=
                LambdaCaptureSpacing::Preserve ||
            style.FunctionSpaceBeforeParameters !=
                FunctionSpaceBeforeParameters::Preserve ||
            style.FunctionCallSpaceBeforeParameters !=
                FunctionCallSpaceBeforeParameters::Preserve ||
            style.SpaceInsideCallArguments !=
                SpaceInsideCallArguments::Preserve ||
            style.SpaceInsideFunctionParameters !=
                SpaceInsideFunctionParameters::Preserve ||
            style.ControlBlockKeywordSpacing !=
                ControlBlockKeywordSpacing::Preserve ||
            style.ControlBlockBracePlacement !=
                ControlBlockBracePlacement::Preserve)
        {
            Classify();
        }
    }

    std::string Run()
    {
        const auto count = Tree_.TokenCount();
        if (count == 0) {
            Output_.append(Tree_.Source());
        } else {
            for (TokenIndex index = 0; index < count; ++index) {
                if (MoveBraceBeforeComment(index)) {
                    // The brace now precedes the trailing comment. Keep the
                    // header/body line break if the body was inline with '{'.
                    if (index + 1 == count ||
                        !Tree_.WhitespaceAfter(index).contains('\n'))
                    {
                        Output_.append(Tree_.WhitespaceBefore(index));
                    }
                    continue;
                }
                if (MoveCommentBeforeBrace(index)) {
                    auto gap = Tree_.WhitespaceBefore(index);
                    Output_.append(gap.empty() ? " " : gap);
                    Output_.append(Tree_.TokenText(index + 1));
                    Output_.append(LineEnding(index));
                    auto header = HeaderStarts_[index];
                    Output_.append(BraceIndentation(index,
                        header == NoToken ? index : header));
                    Output_.append(Tree_.TokenText(index));
                    ++index; // The comment has already been emitted.
                    continue;
                }
                if (index + 1 < count &&
                    MoveBraceBeforeComment(index + 1))
                {
                    Output_.append(" {");
                }
                EmitGap(index);
                Output_.append(Tree_.TokenText(index));
            }
            Output_.append(Tree_.WhitespaceAfter(count - 1));
        }
        return std::move(Output_);
    }

private:
    void MarkOpening(
        const parser::Node& node,
        BracePlacement placement,
        SameLineBraceSpacing spacing,
        TokenIndex header = NoToken)
    {
        if (placement == BracePlacement::Preserve &&
            spacing == SameLineBraceSpacing::Preserve)
        {
            return;
        }
        for (auto index = node.Tokens.Begin;
            index < node.Tokens.End && index < Tree_.TokenCount(); ++index)
        {
            if (Tree_.Kind(index) != TK::Operator_OpenBrace ||
                (node.Kind == NK::RecordDeclaration &&
                    Tree_.Role(index) != parser::PunctuationRole::RecordDelimiter))
            {
                continue;
            }
            if (Tree_.MatchingDelimiter(index) != NoToken) {
                Placements_[index] = placement;
                Spacings_[index] = spacing;
                HeaderStarts_[index] = header == NoToken
                    ? node.Tokens.Begin : header;
            }
            return;
        }
    }

    void Classify()
    {
        std::vector<const parser::Node*> nodes{&Tree_.Root()};
        while (!nodes.empty()) {
            auto* node = nodes.back();
            nodes.pop_back();
            // Do not format a valid-looking prefix of a recovered statement.
            if (IsIncompleteStatement(*node)) {
                continue;
            }
            ClassifyNode(*node);
            for (auto* child = node->FirstChild; child;
                child = child->NextSibling)
            {
                nodes.push_back(child);
            }
        }
    }

    void ClassifyNode(const parser::Node& node)
    {
        switch (node.Kind) {
        case NK::RecordDeclaration:
            MarkBodyIndentation(node);
            MarkInheritance(node);
            // The declaration keyword, not a keyword in a base clause or
            // attribute, selects the record's independent setting.
            if (node.Tokens.Begin < Tree_.TokenCount()) {
                switch (Tree_.Kind(node.Tokens.Begin)) {
                case TK::Keyword_class:
                    MarkOpening(node, Style_.ClassBracePlacement,
                        Style_.ClassBraceSpacing);
                    break;
                case TK::Keyword_struct:
                    MarkOpening(node, Style_.StructBracePlacement,
                        Style_.StructBraceSpacing);
                    break;
                case TK::Keyword_union:
                    MarkOpening(node, Style_.UnionBracePlacement,
                        Style_.UnionBraceSpacing);
                    break;
                default:
                    break;
                }
            }
            break;
        case NK::EnumDeclaration:
            MarkBodyIndentation(node);
            MarkOpening(node, BracePlacement::Preserve,
                Style_.EnumBraceSpacing);
            break;
        case NK::NamespaceDeclaration:
            MarkOpening(node, Style_.NamespaceBracePlacement,
                Style_.NamespaceBraceSpacing);
            break;
        case NK::FunctionDefinition:
            for (auto* child = node.FirstChild; child;
                child = child->NextSibling)
            {
                if (child->Kind == NK::CompoundStatement) {
                    MarkOpening(*child, Style_.FunctionBracePlacement,
                        Style_.FunctionBraceSpacing, node.Tokens.Begin);
                } else if (child->Kind == NK::FunctionTryBlock) {
                    for (auto* body = child->FirstChild; body;
                        body = body->NextSibling)
                    {
                        if (body->Kind == NK::CompoundStatement) {
                            MarkOpening(*body, Style_.FunctionBracePlacement,
                                Style_.FunctionBraceSpacing, node.Tokens.Begin);
                            break;
                        }
                    }
                }
            }
            break;
        case NK::IfStatement:
        case NK::ForStatement:
        case NK::WhileStatement:
        case NK::DoWhileStatement:
        case NK::SwitchStatement:
        case NK::CatchClause:
        case NK::TryStatement:
            MarkControlGap(node);
            MarkControlBraces(node);
            break;
        case NK::Declarator:
            MarkFunctionGap(node);
            break;
        case NK::CallExpression:
            MarkCallGap(node);
            break;
        case NK::LambdaCaptureList:
            MarkCaptureGaps(node);
            break;
        case NK::LambdaExpression:
            MarkLambdaParameterGaps(node);
            break;
        case NK::LambdaBody:
            MarkOpening(node, Style_.LambdaBracePlacement,
                Style_.LambdaBraceSpacing);
            // The capture can start before qualifiers and return types.
            break;
        default:
            break;
        }
    }

    void MarkBodyIndentation(const parser::Node& declaration)
    {
        if (HasRecoveryChild(declaration) ||
            (Style_.IndentMembers == BodyIndentation::Preserve &&
                Style_.IndentAccessModifiers == BodyIndentation::Preserve))
        {
            return;
        }
        TokenIndex opening = NoToken;
        const parser::Node* enumerators = nullptr;
        if (declaration.Kind == NK::EnumDeclaration) {
            for (auto* child = declaration.Child(); child;
                child = child->NextSibling)
            {
                if (child->Kind == NK::EnumeratorList) {
                    enumerators = child;
                    opening = child->Tokens.Begin;
                    break;
                }
            }
        } else {
            for (auto i = declaration.Tokens.Begin;
                i < declaration.Tokens.End; ++i)
            {
                if (Tree_.Kind(i) == TK::Operator_OpenBrace &&
                    Tree_.Role(i) == parser::PunctuationRole::RecordDelimiter)
                {
                    opening = i;
                    break;
                }
            }
        }
        if (opening == NoToken || opening >= Tree_.TokenCount() ||
            Tree_.Kind(opening) != TK::Operator_OpenBrace)
        {
            return;
        }
        auto closing = Tree_.MatchingDelimiter(opening);
        if (closing == NoToken || closing >= declaration.Tokens.End) {
            return;
        }
        std::string baseline(Indentation(declaration.Tokens.Begin));
        if (auto parent = BodyIndents_.find(declaration.Tokens.Begin);
            parent != BodyIndents_.end())
        {
            baseline = parent->second;
            BodyIndents_[closing] = baseline;
        }
        auto mark = [&](const parser::Node& member, BodyIndentation policy) {
            auto index = member.Tokens.Begin;
            if (policy != BodyIndentation::Preserve &&
                !member.Tokens.Empty() && index > opening && index < closing &&
                Tree_.Kind(index) != TK::Comment &&
                Tree_.Kind(index) != TK::PreprocessingDirective)
            {
                BodyIndents_[index] = baseline;
                if (policy == BodyIndentation::Indent) {
                    BodyIndents_[index].append(Style_.IndentWidth, ' ');
                }
            }
        };
        if (enumerators) {
            for (auto* child = enumerators->Child(); child;
                child = child->NextSibling)
            {
                if (child->Kind == NK::Enumerator) {
                    mark(*child, Style_.IndentMembers);
                }
            }
        } else {
            for (auto* child = declaration.Child(); child;
                child = child->NextSibling)
            {
                if (child->Kind == NK::AccessSpecifier) {
                    mark(*child, Style_.IndentAccessModifiers);
                } else if (child->Kind != NK::BaseClause &&
                    child->Kind != NK::PreprocessingDirective &&
                    child->Kind != NK::UnparsedSyntax &&
                    child->Kind != NK::MissingToken &&
                    child->Kind != NK::UnexpectedToken &&
                    child->Kind != NK::Error)
                {
                    mark(*child, Style_.IndentMembers);
                }
            }
        }
    }

    struct BaseEntry
    {
        TokenIndex Opening;
        TokenIndex Comma;
        std::string Text;
    };

    std::optional<std::vector<BaseEntry>> CollectBases(
        const parser::Node& first) const
    {
        std::vector<BaseEntry> entries;
        TokenIndex comma = NoToken;
        for (auto* child = &first; child; child = child->NextSibling) {
            if (child->Tokens.Empty()) {
                return std::nullopt;
            }
            if (child->Kind == NK::ListSeparator) {
                if (child->Tokens.Size() != 1 ||
                    Tree_.Kind(child->Tokens.Begin) != TK::Operator_Comma)
                {
                    return std::nullopt;
                }
                comma = child->Tokens.Begin;
                continue;
            }
            const auto opening = child->Tokens.Begin;
            const auto begin = Tree_.Token(opening).Text.Begin;
            const auto end = Tree_.Token(child->Tokens.End - 1).Text.End;
            auto text = Tree_.Source().substr(begin, end - begin);
            if (text.find_first_of("\r\n") != std::string_view::npos) {
                return std::nullopt;
            }
            bool hasName = false;
            for (auto i = opening; i < child->Tokens.End; ++i) {
                auto kind = Tree_.Kind(i);
                if (kind == TK::Comment ||
                    kind == TK::PreprocessingDirective)
                {
                    return std::nullopt;
                }
                hasName |= kind == TK::Identifier ||
                    kind == TK::Keyword_decltype;
            }
            if (!hasName) {
                return std::nullopt;
            }
            std::string normalized;
            size_t cursor = begin;
            for (auto i = opening; i + 1 < child->Tokens.End; ++i) {
                if (!IsBaseSpacingKeyword(Tree_.Kind(i))) {
                    continue;
                }
                const auto tokenEnd = Tree_.Token(i).Text.End;
                normalized.append(Tree_.Source().substr(
                    cursor, tokenEnd - cursor));
                normalized.push_back(' ');
                cursor = Tree_.Token(i + 1).Text.Begin;
            }
            normalized.append(Tree_.Source().substr(cursor, end - cursor));
            entries.push_back({opening, comma, std::move(normalized)});
            if (comma != NoToken) {
                auto beforeComma = Tree_.WhitespaceBefore(comma);
                auto beforeBase = Tree_.WhitespaceBefore(opening);
                if (beforeComma.find_first_not_of(" \t\r\n") !=
                        std::string_view::npos ||
                    beforeBase.find_first_not_of(" \t\r\n") !=
                        std::string_view::npos)
                {
                    return std::nullopt;
                }
            }
        }
        return entries;
    }

    void MarkInheritance(const parser::Node& declaration)
    {
        if (HasRecoveryChild(declaration) ||
            Style_.InheritanceList != InheritanceListMode::Format ||
            Tree_.Kind(declaration.Tokens.Begin) == TK::Keyword_union)
        {
            return;
        }
        const parser::Node* bases = nullptr;
        for (auto* child = declaration.Child(); child;
            child = child->NextSibling)
        {
            if (child->Kind == NK::BaseClause) {
                bases = child;
                break;
            }
        }
        if (!bases || bases->Tokens.Empty() ||
            bases->Tokens.End >= Tree_.TokenCount() ||
            Tree_.Kind(bases->Tokens.End) != TK::Operator_OpenBrace ||
            Tree_.Role(bases->Tokens.End) !=
                parser::PunctuationRole::RecordDelimiter)
        {
            return; // An inheritance list belongs to a definition.
        }
        const auto colon = bases->Tokens.Begin;
        if (colon <= declaration.Tokens.Begin ||
            Tree_.Kind(colon) != TK::Operator_Colon ||
            Tree_.Kind(colon - 1) == TK::Comment ||
            Tree_.Kind(colon - 1) == TK::PreprocessingDirective)
        {
            return;
        }
        const auto* first = bases->Child();
        const auto* last = first;
        bool expectBase = true;
        for (auto* child = first; child; child = child->NextSibling) {
            if (child->Tokens.Empty() ||
                child->Kind != (expectBase ? NK::BaseSpecifier
                                            : NK::ListSeparator))
            {
                return;
            }
            expectBase = !expectBase;
            last = child;
        }
        if (!first || !last || expectBase ||
            first->Tokens.Begin != colon + 1 ||
            last->Tokens.End != bases->Tokens.End ||
            Tree_.Kind(colon + 1) == TK::Comment ||
            Tree_.Kind(colon + 1) == TK::PreprocessingDirective)
        {
            return;
        }
        auto entries = CollectBases(*first);
        if (!entries || entries->empty()) {
            return; // Comments and recovered lists remain source-owned.
        }
        auto beforeColon = Tree_.WhitespaceBefore(colon);
        auto beforeBase = Tree_.WhitespaceBefore(colon + 1);
        if (beforeColon.find_first_not_of(" \t\r\n") !=
                std::string_view::npos ||
            beforeBase.find_first_not_of(" \t\r\n") !=
                std::string_view::npos)
        {
            return;
        }
        for (auto* child = first; child; child = child->NextSibling) {
            if (child->Kind != NK::BaseSpecifier) {
                continue;
            }
            for (auto i = child->Tokens.Begin;
                i + 1 < child->Tokens.End; ++i)
            {
                if (IsBaseSpacingKeyword(Tree_.Kind(i))) {
                    InheritanceGaps_[i + 1] = " ";
                }
            }
        }
        std::string indent(Indentation(declaration.Tokens.Begin));
        if (auto parent = BodyIndents_.find(declaration.Tokens.Begin);
            parent != BodyIndents_.end())
        {
            indent = parent->second;
        }
        indent.append(Style_.InheritanceListIndent == 0
            ? Style_.IndentWidth : Style_.InheritanceListIndent, ' ');
        auto space = [](InheritanceListSpacing policy) -> std::string_view {
            return policy == InheritanceListSpacing::Space ? " " : "";
        };
        const auto colonBefore =
            space(Style_.InheritanceListColonSpaceBefore);
        const auto colonAfter = space(Style_.InheritanceListColonSpaceAfter);
        const auto commaBefore =
            space(Style_.InheritanceListCommaSpaceBefore);
        const auto commaAfter =
            space(Style_.InheritanceListCommaSpaceAfter);
        const auto source = Tree_.Source();
        const auto headerBegin = Tree_.Token(declaration.Tokens.Begin).Text.Begin;
        const auto headerEnd = Tree_.Token(colon - 1).Text.End;
        const auto header = source.substr(headerBegin, headerEnd - headerBegin);
        const auto lineStart = source.rfind('\n', headerBegin);
        auto prefix = headerBegin - (lineStart == std::string_view::npos
            ? 0 : lineStart + 1);
        if (auto parent = BodyIndents_.find(declaration.Tokens.Begin);
            parent != BodyIndents_.end() &&
                Tree_.WhitespaceBefore(declaration.Tokens.Begin).contains('\n'))
        {
            prefix = parent->second.size();
        }
        size_t flatWidth = prefix + header.size() + colonBefore.size() +
            1 + colonAfter.size();
        for (const auto& entry : *entries) {
            if (entry.Comma != NoToken) {
                flatWidth += commaBefore.size() + 1 + commaAfter.size();
            }
            flatWidth += entry.Text.size();
        }
        const bool overflow = header.contains('\n') ||
            flatWidth > Style_.ColumnLimit;
        const bool firstNew = Style_.InheritanceListStart ==
                InheritanceListStart::NewLine ||
            (Style_.InheritanceListStart ==
                InheritanceListStart::NewLineOnOverflow && overflow);
        const auto newline = std::string(LineEnding(colon)) + indent;
        const bool colonWithBase = Style_.InheritanceListColonPlacement ==
            InheritanceListColonPlacement::AttachToBase;
        InheritanceGaps_[colon] = firstNew && colonWithBase
            ? newline : std::string(colonBefore);
        InheritanceGaps_[entries->front().Opening] =
            firstNew && !colonWithBase ? newline : std::string(colonAfter);
        auto column = firstNew
            ? indent.size() + entries->front().Text.size() +
                (colonWithBase ? 1 + colonAfter.size() : 0)
            : prefix + header.size() + colonBefore.size() + 1 +
                colonAfter.size() + entries->front().Text.size();
        const auto split = Style_.InheritanceListSplit;
        const bool onePerLine = split == InheritanceListSplit::OnePerLine ||
            (split == InheritanceListSplit::OnePerLineOnOverflow && overflow);
        for (size_t i = 1; i < entries->size(); ++i) {
            const auto& entry = (*entries)[i];
            const auto width = column + commaBefore.size() + 1 +
                commaAfter.size() + entry.Text.size();
            const bool breakLine = onePerLine || width > Style_.ColumnLimit;
            if (breakLine && Style_.InheritanceListCommaPlacement ==
                    InheritanceListCommaPlacement::AttachToNext)
            {
                InheritanceGaps_[entry.Comma] = newline;
                InheritanceGaps_[entry.Opening] = commaAfter;
                column = indent.size() + 1 + commaAfter.size() +
                    entry.Text.size();
            } else if (breakLine) {
                InheritanceGaps_[entry.Comma] = commaBefore;
                InheritanceGaps_[entry.Opening] = newline;
                column = indent.size() + entry.Text.size();
            } else {
                InheritanceGaps_[entry.Comma] = commaBefore;
                InheritanceGaps_[entry.Opening] = commaAfter;
                column = width;
            }
        }
    }

    void MarkLambdaParameterGaps(const parser::Node& lambda)
    {
        if (Style_.LambdaSpaceBeforeParameters ==
                LambdaSpaceBeforeParameters::Preserve &&
            Style_.SpaceInsideLambdaParameters ==
                SpaceInsideLambdaParameters::Preserve)
        {
            return;
        }
        const auto* capture = lambda.Child();
        const auto* parameters = capture ? capture->NextSibling : nullptr;
        if (parameters && parameters->Kind == NK::TemplateParameterList) {
            parameters = parameters->NextSibling;
        }
        if (!capture || capture->Kind != NK::LambdaCaptureList ||
            capture->Tokens.Empty() || !parameters ||
            parameters->Kind != NK::ParameterList ||
            parameters->Tokens.Empty())
        {
            return;
        }
        const auto bracket = capture->Tokens.End - 1;
        const auto opening = parameters->Tokens.Begin;
        const auto closing = parameters->Tokens.End - 1;
        if (opening <= bracket ||
            Tree_.Kind(bracket) != TK::Operator_CloseBracket ||
            Tree_.Kind(opening) != TK::Operator_OpenParenthesis ||
            Tree_.MatchingDelimiter(capture->Tokens.Begin) != bracket ||
            Tree_.MatchingDelimiter(opening) != closing)
        {
            return;
        }
        if (opening == bracket + 1 &&
            Style_.LambdaSpaceBeforeParameters !=
                LambdaSpaceBeforeParameters::Preserve)
        {
            LambdaGaps_[opening] = true;
        }
        if (Style_.SpaceInsideLambdaParameters ==
                SpaceInsideLambdaParameters::Preserve ||
            opening + 1 == closing)
        {
            return; // Empty parameter pairs remain source-owned.
        }
        const auto* first = parameters->Child();
        const auto* last = first;
        bool expectParameter = true;
        for (auto* child = first; child; child = child->NextSibling) {
            if (child->Tokens.Empty() ||
                child->Kind != (expectParameter ? NK::ParameterDeclaration
                                                 : NK::ListSeparator))
            {
                return;
            }
            expectParameter = !expectParameter;
            last = child;
        }
        if (!first || !last || expectParameter ||
            first->Tokens.Begin != opening + 1 ||
            last->Tokens.End != closing)
        {
            return; // Incomplete lists remain source-owned.
        }
        if (Tree_.Kind(opening + 1) != TK::Comment &&
            Tree_.Kind(opening + 1) != TK::PreprocessingDirective)
        {
            LambdaParameterInsideGaps_[opening + 1] = true;
        }
        if (Tree_.Kind(closing - 1) != TK::Comment &&
            Tree_.Kind(closing - 1) != TK::PreprocessingDirective)
        {
            LambdaParameterInsideGaps_[closing] = true;
        }
    }

    void MarkCaptureAssignment(const parser::Node& capture)
    {
        TokenIndex firstToken = NoToken;
        for (auto i = capture.Tokens.Begin; i < capture.Tokens.End; ++i) {
            auto kind = Tree_.Kind(i);
            if (kind == TK::PreprocessingDirective) {
                return;
            }
            if (kind == TK::Comment) {
                continue;
            }
            if (firstToken == NoToken) {
                firstToken = i;
                continue;
            }
            if (kind == TK::Operator_Assign) {
                // The lone '=' in [=] is a default, not an assignment.
                // Only the first '=' after a capture name belongs to the
                // init-capture; later '=' tokens are part of its initializer.
                if (i + 1 < capture.Tokens.End) {
                    if (Tree_.Kind(i - 1) != TK::Comment) {
                        CaptureGaps_[i] = Style_.SpaceAroundCaptureAssignment;
                    }
                    if (Tree_.Kind(i + 1) != TK::Comment &&
                        Tree_.Kind(i + 1) != TK::PreprocessingDirective)
                    {
                        CaptureGaps_[i + 1] =
                            Style_.SpaceAroundCaptureAssignment;
                    }
                }
                return;
            }
        }
    }

    void MarkCaptureGaps(const parser::Node& captures)
    {
        if (Style_.SpaceAroundCaptureList ==
                LambdaCaptureSpacing::Preserve &&
            Style_.SpaceAroundCaptureAssignment ==
                LambdaCaptureSpacing::Preserve)
        {
            return;
        }
        const auto opening = captures.Tokens.Begin;
        if (captures.Tokens.Size() < 3 ||
            opening >= Tree_.TokenCount() ||
            Tree_.Kind(opening) != TK::Operator_OpenBracket ||
            Tree_.MatchingDelimiter(opening) != captures.Tokens.End - 1)
        {
            return;
        }
        const auto closing = captures.Tokens.End - 1;
        const auto* first = captures.Child();
        const auto* last = first;
        bool expectCapture = true;
        for (auto* child = first; child; child = child->NextSibling) {
            if (child->Tokens.Empty() ||
                child->Kind != (expectCapture ? NK::LambdaCapture
                                               : NK::ListSeparator))
            {
                return; // Do not format a partially parsed capture list.
            }
            expectCapture = !expectCapture;
            last = child;
        }
        if (!first || !last || expectCapture ||
            last->Kind != NK::LambdaCapture ||
            first->Tokens.Begin != opening + 1 ||
            last->Tokens.End != closing)
        {
            return;
        }
        if (Style_.SpaceAroundCaptureList !=
            LambdaCaptureSpacing::Preserve)
        {
            if (Tree_.Kind(opening + 1) != TK::Comment &&
                Tree_.Kind(opening + 1) != TK::PreprocessingDirective)
            {
                CaptureGaps_[opening + 1] = Style_.SpaceAroundCaptureList;
            }
            if (Tree_.Kind(closing - 1) != TK::Comment &&
                Tree_.Kind(closing - 1) != TK::PreprocessingDirective)
            {
                CaptureGaps_[closing] = Style_.SpaceAroundCaptureList;
            }
        }
        if (Style_.SpaceAroundCaptureAssignment !=
            LambdaCaptureSpacing::Preserve)
        {
            for (auto* child = first; child; child = child->NextSibling) {
                if (child->Kind == NK::LambdaCapture) {
                    MarkCaptureAssignment(*child);
                }
            }
        }
    }

    void MarkFunctionGap(const parser::Node& declarator)
    {
        if (Style_.FunctionSpaceBeforeParameters ==
                FunctionSpaceBeforeParameters::Preserve &&
            Style_.SpaceInsideFunctionParameters ==
                SpaceInsideFunctionParameters::Preserve)
        {
            return;
        }
        bool named = false;
        const parser::Node* parameters = nullptr;
        for (auto* child = declarator.Child(); child;
            child = child->NextSibling)
        {
            if (child->Kind == NK::DeclaratorName ||
                child->Kind == NK::DestructorName ||
                child->Kind == NK::OperatorFunctionName)
            {
                named = true;
            } else if (child->Kind == NK::FunctionDeclarator) {
                for (auto* list = child->Child(); list;
                    list = list->NextSibling)
                {
                    if (list->Kind == NK::ParameterList) {
                        parameters = list;
                        break;
                    }
                }
            }
        }
        if (!named || !parameters || parameters->Tokens.Empty()) {
            return;
        }
        auto opening = parameters->Tokens.Begin;
        if (opening == 0 || opening >= Tree_.TokenCount() ||
            Tree_.Kind(opening) != TK::Operator_OpenParenthesis ||
            Tree_.MatchingDelimiter(opening) != parameters->Tokens.End - 1 ||
            // Recovery can misclassify a requires-expression parameter list
            // as a declarator; `requires` is never a function name.
            Tree_.Kind(opening - 1) == TK::Keyword_requires)
        {
            return;
        }
        if (Style_.FunctionSpaceBeforeParameters !=
                FunctionSpaceBeforeParameters::Preserve &&
            Tree_.Kind(opening - 1) != TK::Comment &&
            Tree_.Kind(opening - 1) != TK::PreprocessingDirective)
        {
            FunctionGaps_[opening] = true;
        }
        if (Style_.SpaceInsideFunctionParameters ==
                SpaceInsideFunctionParameters::Preserve ||
            opening + 1 == parameters->Tokens.End - 1)
        {
            return; // No parameters: keep the empty pair source-owned.
        }
        auto closing = parameters->Tokens.End - 1;
        if (Tree_.Kind(opening + 1) != TK::Comment &&
            Tree_.Kind(opening + 1) != TK::PreprocessingDirective)
        {
            ParameterInsideGaps_[opening + 1] = true;
        }
        if (Tree_.Kind(closing - 1) != TK::Comment &&
            Tree_.Kind(closing - 1) != TK::PreprocessingDirective)
        {
            ParameterInsideGaps_[closing] = true;
        }
    }

    bool IsAmbiguousParenthesizedCallee(const parser::Node& callee) const
    {
        if (callee.Kind != NK::ParenthesizedExpression) {
            return false;
        }
        // (Type) (value) and (function) (value) are indistinguishable
        // without name lookup. Only format unmistakable expression callees.
        const auto* inner = callee.Child();
        if (!inner || inner->Tokens.Empty()) {
            return true;
        }
        if (inner->Kind == NK::LambdaExpression ||
            inner->Kind == NK::CallExpression)
        {
            return false;
        }
        return inner->Kind != NK::UnaryExpression ||
            (Tree_.Kind(inner->Tokens.Begin) != TK::Operator_Star &&
                Tree_.Kind(inner->Tokens.Begin) != TK::Operator_Ampersand);
    }

    void MarkCallGap(const parser::Node& call)
    {
        if (Style_.FunctionCallSpaceBeforeParameters ==
                FunctionCallSpaceBeforeParameters::Preserve &&
            Style_.SpaceInsideCallArguments ==
                SpaceInsideCallArguments::Preserve)
        {
            return;
        }
        const auto* callee = call.Child();
        const auto* arguments = callee ? callee->NextSibling : nullptr;
        if (!callee || callee->Tokens.Empty() ||
            callee->Kind == NK::UnparsedSyntax ||
            callee->Kind == NK::Expression ||
            IsAmbiguousParenthesizedCallee(*callee) ||
            !arguments ||
            arguments->Kind != NK::ArgumentList || arguments->Tokens.Empty())
        {
            return;
        }
        auto opening = arguments->Tokens.Begin;
        if (opening == 0 || opening >= Tree_.TokenCount() ||
            callee->Tokens.End > opening ||
            Tree_.Kind(opening) != TK::Operator_OpenParenthesis ||
            Tree_.MatchingDelimiter(opening) != arguments->Tokens.End - 1 ||
            Tree_.Kind(opening - 1) == TK::Keyword_requires)
        {
            return;
        }
        if (Style_.FunctionCallSpaceBeforeParameters !=
                FunctionCallSpaceBeforeParameters::Preserve &&
            Tree_.Kind(opening - 1) != TK::Comment &&
            Tree_.Kind(opening - 1) != TK::PreprocessingDirective)
        {
            CallGaps_[opening] = true;
        }
        if (Style_.SpaceInsideCallArguments ==
            SpaceInsideCallArguments::Preserve)
        {
            return;
        }
        const auto closing = arguments->Tokens.End - 1;
        const auto* first = arguments->Child();
        if (opening + 1 == closing || !first ||
            first->Kind != NK::Argument || first->Tokens.Empty())
        {
            return; // Leave empty and recovered lists source-owned.
        }
        if (first->Tokens.Begin == opening + 1) {
            CallInsideGaps_[opening + 1] = true;
        }
        for (auto* child = first; child; child = child->NextSibling) {
            if (child->Kind == NK::Argument &&
                child->Tokens.End == closing)
            {
                CallInsideGaps_[closing] = true;
                break;
            }
        }
    }

    void MarkControlBraces(const parser::Node& node)
    {
        auto policy = Style_.ControlBlockBracePlacement;
        if (policy == ControlBlockBracePlacement::Preserve) {
            return;
        }
        for (auto* child = node.Child(); child; child = child->NextSibling) {
            if (child->Kind != NK::CompoundStatement ||
                child->Tokens.Empty())
            {
                continue;
            }
            const auto opening = child->Tokens.Begin;
            if (opening >= Tree_.TokenCount() ||
                Tree_.Kind(opening) != TK::Operator_OpenBrace ||
                Tree_.MatchingDelimiter(opening) == NoToken)
            {
                continue;
            }
            auto header = node.Tokens.Begin;
            // The else branch belongs to its own header, not the if condition
            // (which can itself span several lines).
            if (node.Kind == NK::IfStatement) {
                for (auto i = header; i < opening; ++i) {
                    if (Tree_.Kind(i) == TK::Keyword_else) {
                        header = i;
                    }
                }
            }
            // A detached brace must not count as a multiline header.
            // Similarly, a comment after the header is not the header's end.
            auto last = opening;
            while (last > header) {
                --last;
                if (Tree_.Kind(last) != TK::Comment) {
                    break;
                }
            }
            if (last == header && Tree_.Kind(last) == TK::Comment) {
                continue;
            }
            const auto begin = Tree_.Token(header).Text.Begin;
            const auto end = Tree_.Token(last).Text.End;
            const bool multiline = Tree_.Source().substr(begin, end - begin)
                .contains('\n');
            const auto placement =
                policy == ControlBlockBracePlacement::NewLine ||
                    (policy == ControlBlockBracePlacement::AttachUnlessMultiline &&
                        multiline)
                    ? BracePlacement::NewLine : BracePlacement::Attach;
            MarkOpening(*child, placement, SameLineBraceSpacing::Preserve,
                header);
            ControlBraces_[opening] = true;
        }
    }

    void MarkControlGap(const parser::Node& node)
    {
        if (Style_.ControlBlockKeywordSpacing ==
            ControlBlockKeywordSpacing::Preserve)
        {
            return;
        }
        for (auto* child = node.Child(); child; child = child->NextSibling) {
            if (child->Kind != NK::Condition && child->Kind != NK::ForHeader) {
                continue;
            }
            auto opening = child->Tokens.Begin;
            if (opening == 0 || opening >= Tree_.TokenCount() ||
                Tree_.Kind(opening) != TK::Operator_OpenParenthesis ||
                Tree_.MatchingDelimiter(opening) == NoToken)
            {
                continue;
            }
            const auto preceding = Tree_.Kind(opening - 1);
            // Only a keyword directly preceding the parsed header owns this
            // gap. A recovered `if MACRO(...)` must stay source-owned.
            const bool eligible =
                (node.Kind == NK::IfStatement &&
                    (preceding == TK::Keyword_if ||
                        preceding == TK::Keyword_constexpr)) ||
                (node.Kind == NK::ForStatement &&
                    (preceding == TK::Keyword_for ||
                        preceding == TK::Keyword_co_await)) ||
                ((node.Kind == NK::WhileStatement ||
                    node.Kind == NK::DoWhileStatement) &&
                    preceding == TK::Keyword_while) ||
                (node.Kind == NK::SwitchStatement &&
                    preceding == TK::Keyword_switch) ||
                (node.Kind == NK::CatchClause &&
                    preceding == TK::Keyword_catch);
            if (eligible) {
                ControlGaps_[opening] = true;
            }
        }
    }

    std::string_view Indentation(TokenIndex index) const
    {
        auto source = Tree_.Source();
        auto offset = Tree_.Token(index).Text.Begin;
        auto lastNewline = offset == 0 ? std::string_view::npos
            : source.rfind('\n', offset - 1);
        auto start = lastNewline == std::string_view::npos ? 0 : lastNewline + 1;
        auto prefix = source.substr(start, offset - start);
        return prefix.find_first_not_of(" \t") == std::string_view::npos
            ? prefix : std::string_view{};
    }

    std::string_view BraceIndentation(TokenIndex brace, TokenIndex header) const
    {
        if (!ControlBraces_[brace]) {
            return Indentation(header);
        }
        auto source = Tree_.Source();
        auto offset = Tree_.Token(header).Text.Begin;
        auto newline = offset == 0 ? std::string_view::npos
            : source.rfind('\n', offset - 1);
        auto start = newline == std::string_view::npos ? 0 : newline + 1;
        auto prefix = source.substr(start, offset - start);
        return prefix.substr(0, prefix.find_first_not_of(" \t"));
    }

    std::string_view LineEnding(TokenIndex index) const
    {
        auto source = Tree_.Source();
        auto offset = Tree_.Token(index).Text.Begin;
        auto newline = source.rfind('\n', offset);
        if (newline == std::string_view::npos) {
            newline = source.find('\n');
        }
        return newline != std::string_view::npos && newline != 0 &&
                source[newline - 1] == '\r'
            ? "\r\n" : "\n";
    }

    bool MoveBraceBeforeComment(TokenIndex index) const
    {
        if (index < 2 || Placements_[index] != BracePlacement::Attach ||
            Tree_.Kind(index - 1) != TK::Comment ||
            !(Tree_.TokenText(index - 1).starts_with("//") ||
                Tree_.TokenText(index - 1).starts_with("/*")) ||
            Tree_.Kind(index - 2) == TK::Comment ||
            Tree_.Kind(index - 2) == TK::PreprocessingDirective)
        {
            return false;
        }
        auto beforeComment = Tree_.WhitespaceBefore(index - 1);
        auto beforeBrace = Tree_.WhitespaceBefore(index);
        return beforeComment.find('\n') == std::string_view::npos &&
            beforeComment.find_first_not_of(" \t") == std::string_view::npos &&
            beforeBrace.find('\n') != std::string_view::npos &&
            beforeBrace.find_first_not_of(" \t\r\n") ==
                std::string_view::npos &&
            beforeBrace.find('\n') == beforeBrace.rfind('\n');
    }

    bool MoveCommentBeforeBrace(TokenIndex index) const
    {
        if (Placements_[index] != BracePlacement::NewLine ||
            index + 1 >= Tree_.TokenCount() ||
            Tree_.Kind(index + 1) != TK::Comment)
        {
            return false;
        }
        auto comment = Tree_.TokenText(index + 1);
        if (!comment.starts_with("//") && !comment.starts_with("/*")) {
            return false;
        }
        auto beforeBrace = Tree_.WhitespaceBefore(index);
        auto beforeComment = Tree_.WhitespaceBefore(index + 1);
        return beforeBrace.find_first_not_of(" \t") ==
                std::string_view::npos &&
            beforeComment.find_first_not_of(" \t") ==
                std::string_view::npos &&
            Tree_.WhitespaceAfter(index + 1).contains('\n');
    }

    void EmitGap(TokenIndex index)
    {
        auto gap = Tree_.WhitespaceBefore(index);
        if (auto replacement = InheritanceGaps_.find(index);
            replacement != InheritanceGaps_.end())
        {
            Output_ += replacement->second;
            return;
        }
        if (auto indent = BodyIndents_.find(index);
            indent != BodyIndents_.end())
        {
            auto newline = gap.rfind('\n');
            if (newline != std::string_view::npos &&
                gap.find_first_not_of(" \t\r\n") == std::string_view::npos)
            {
                Output_.append(gap.substr(0, newline + 1));
                Output_ += indent->second;
                return;
            }
        }
        if (CaptureGaps_[index] != LambdaCaptureSpacing::Preserve &&
            gap.find_first_not_of(" \t") == std::string_view::npos)
        {
            if (CaptureGaps_[index] == LambdaCaptureSpacing::Space) {
                Output_.push_back(' ');
            }
            return;
        }
        if (LambdaGaps_[index] &&
            gap.find_first_not_of(" \t") == std::string_view::npos)
        {
            if (Style_.LambdaSpaceBeforeParameters ==
                LambdaSpaceBeforeParameters::Space)
            {
                Output_.push_back(' ');
            }
            return;
        }
        if (FunctionGaps_[index] &&
            gap.find_first_not_of(" \t") == std::string_view::npos)
        {
            if (Style_.FunctionSpaceBeforeParameters ==
                FunctionSpaceBeforeParameters::Space)
            {
                Output_.push_back(' ');
            }
            return;
        }
        if (CallGaps_[index] &&
            gap.find_first_not_of(" \t") == std::string_view::npos)
        {
            if (Style_.FunctionCallSpaceBeforeParameters ==
                FunctionCallSpaceBeforeParameters::Space)
            {
                Output_.push_back(' ');
            }
            return;
        }
        if (LambdaParameterInsideGaps_[index] &&
            gap.find_first_not_of(" \t") == std::string_view::npos)
        {
            if (Style_.SpaceInsideLambdaParameters ==
                SpaceInsideLambdaParameters::Space)
            {
                Output_.push_back(' ');
            }
            return;
        }
        if (ParameterInsideGaps_[index] &&
            gap.find_first_not_of(" \t") == std::string_view::npos)
        {
            if (Style_.SpaceInsideFunctionParameters ==
                SpaceInsideFunctionParameters::Space)
            {
                Output_.push_back(' ');
            }
            return;
        }
        if (CallInsideGaps_[index] &&
            gap.find_first_not_of(" \t") == std::string_view::npos)
        {
            if (Style_.SpaceInsideCallArguments ==
                SpaceInsideCallArguments::Space)
            {
                Output_.push_back(' ');
            }
            return;
        }
        if (ControlGaps_[index] &&
            gap.find_first_not_of(" \t") == std::string_view::npos)
        {
            if (Style_.ControlBlockKeywordSpacing ==
                ControlBlockKeywordSpacing::Space)
            {
                Output_.push_back(' ');
            }
            return;
        }
        auto placement = Placements_[index];
        if (index == 0 ||
            // Only a trailing header comment is safe to cross; leave other
            // comments and preprocessing directives where they are.
            (Tree_.Kind(index - 1) == TK::Comment &&
                (placement == BracePlacement::Attach ||
                    Tree_.TokenText(index - 1).starts_with("//"))) ||
            Tree_.Kind(index - 1) == TK::PreprocessingDirective ||
            gap.find_first_not_of(" \t\r\n") != std::string_view::npos)
        {
            Output_.append(gap);
        } else if (placement != BracePlacement::NewLine &&
            gap.find('\n') == std::string_view::npos &&
            Spacings_[index] == SameLineBraceSpacing::SingleSpace &&
            gap.find_first_not_of(" \t") == std::string_view::npos &&
            Tree_.Kind(index - 1) != TK::Comment)
        {
            Output_.push_back(' ');
        } else if (placement == BracePlacement::Preserve) {
            Output_.append(gap);
        } else if (placement == BracePlacement::Attach) {
            // Placement controls line breaks, not existing same-line spacing.
            if (gap.find('\n') == std::string_view::npos) {
                Output_.append(gap);
            } else {
                Output_.push_back(' ');
            }
        } else if (gap.find('\n') != std::string_view::npos) {
            Output_.append(gap);
        } else {
            Output_.append(LineEnding(index));
            auto header = HeaderStarts_[index];
            Output_.append(BraceIndentation(index,
                header == NoToken ? index : header));
        }
    }

    const parser::SyntaxTree& Tree_;
    const Style& Style_;
    std::vector<BracePlacement> Placements_;
    std::unordered_map<TokenIndex, std::string> BodyIndents_;
    std::unordered_map<TokenIndex, std::string> InheritanceGaps_;
    std::vector<SameLineBraceSpacing> Spacings_;
    std::vector<bool> LambdaGaps_;
    std::vector<bool> LambdaParameterInsideGaps_;
    std::vector<LambdaCaptureSpacing> CaptureGaps_;
    std::vector<bool> FunctionGaps_;
    std::vector<bool> CallGaps_;
    std::vector<bool> CallInsideGaps_;
    std::vector<bool> ParameterInsideGaps_;
    std::vector<bool> ControlGaps_;
    std::vector<bool> ControlBraces_;
    // Only initialized for openings with an active placement.
    std::vector<TokenIndex> HeaderStarts_ =
        std::vector<TokenIndex>(Tree_.TokenCount(), NoToken);
    std::string Output_;
};

struct Replacement
{
    size_t Begin;
    size_t End;
    std::string Text;
};

bool IsWhitespace(std::string_view text)
{
    return std::ranges::all_of(text, [] (unsigned char ch) {
        return std::isspace(ch) != 0;
    });
}

// Only rewrite lists whose comma-separated elements and intervening trivia
// can be moved without touching comments, directives, or nested line breaks.
std::optional<Replacement> ReflowParameters(
    const parser::SyntaxTree& tree, const parser::Node& list,
    const Style& style)
{
    using TK = tokenizer::TokenKind;
    auto opening = list.Tokens.Begin;
    auto closing = list.Tokens.End - 1;
    if (list.Tokens.Size() < 3 ||
        tree.Kind(opening) != TK::Operator_OpenParenthesis ||
        tree.Kind(closing) != TK::Operator_CloseParenthesis ||
        tree.MatchingDelimiter(opening) != closing)
    {
        return std::nullopt;
    }

    auto source = tree.Source();
    auto openBegin = tree.Token(opening).Text.Begin;
    auto begin = tree.Token(opening).Text.End;
    auto end = tree.Token(closing).Text.Begin;
    std::vector<std::string_view> parameters;
    size_t cursor = begin;
    bool expectParameter = true;
    for (auto* child = list.Child(); child; child = child->NextSibling) {
        if (child->Tokens.Empty()) {
            return std::nullopt;
        }
        if (expectParameter ? child->Kind != parser::NodeKind::ParameterDeclaration
                            : child->Kind != parser::NodeKind::ListSeparator)
        {
            return std::nullopt;
        }
        auto first = child->Tokens.Begin;
        auto last = child->Tokens.End - 1;
        auto from = tree.Token(first).Text.Begin;
        auto to = tree.Token(last).Text.End;
        if (from < cursor || to > end ||
            !IsWhitespace(source.substr(cursor, from - cursor)))
        {
            return std::nullopt;
        }
        if (expectParameter) {
            auto text = source.substr(from, to - from);
            if (text.find_first_of("\r\n") != std::string_view::npos) {
                return std::nullopt;
            }
            for (auto i = first; i < child->Tokens.End; ++i) {
                if (tree.Kind(i) == TK::Comment ||
                    tree.Kind(i) == TK::PreprocessingDirective)
                {
                    return std::nullopt;
                }
            }
            parameters.push_back(text);
        } else if (first != last || tree.Kind(first) != TK::Operator_Comma) {
            return std::nullopt;
        }
        cursor = to;
        expectParameter = !expectParameter;
    }
    if (expectParameter || parameters.empty() ||
        !IsWhitespace(source.substr(cursor, end - cursor)) ||
        source.substr(begin, end - begin).find('\r') != std::string_view::npos)
    {
        return std::nullopt;
    }

    auto lineStart = source.rfind('\n', openBegin);
    lineStart = lineStart == std::string_view::npos ? 0 : lineStart + 1;
    auto prefix = openBegin + 1 - lineStart;
    auto lineEnd = source.find('\n', end);
    if (lineEnd == std::string_view::npos) {
        lineEnd = source.size();
    }
    auto suffix = source.substr(end, lineEnd - end);
    if (suffix.find('\r') != std::string_view::npos) {
        return std::nullopt;
    }
    size_t flatWidth = prefix + suffix.size();
    for (size_t i = 0; i < parameters.size(); ++i) {
        flatWidth += parameters[i].size() + (i ? 2 : 0);
    }
    const bool innerSpace = style.SpaceInsideFunctionParameters ==
        SpaceInsideFunctionParameters::Space;
    flatWidth += innerSpace ? 2 : 0;
    bool split = source.substr(begin, end - begin).find('\n') !=
        std::string_view::npos;
    if (!split && flatWidth <= style.ColumnLimit) {
        return std::nullopt;
    }

    std::string flat;
    for (auto parameter : parameters) {
        if (!flat.empty()) {
            flat += ", ";
        }
        flat += parameter;
    }
    if (split && style.MergeFunctionParameters &&
        flatWidth <= style.ColumnLimit)
    {
        if (innerSpace) {
            flat.insert(flat.begin(), ' ');
            flat.push_back(' ');
        }
        return Replacement{begin, end, std::move(flat)};
    }
    if (parameters.size() == 1 && flatWidth <= style.ColumnLimit) {
        return std::nullopt;
    }

    bool firstNew = style.FirstFunctionParameterPosition ==
        FirstFunctionParameterPosition::NewLine;
    if (split && !style.EnforceFirstFunctionParameterPosition) {
        firstNew = source.substr(begin,
            tree.Token(list.Child()->Tokens.Begin).Text.Begin - begin)
            .find('\n') != std::string_view::npos;
    }
    size_t baseIndent = 0;
    while (lineStart + baseIndent < openBegin &&
        source[lineStart + baseIndent] == ' ')
    {
        ++baseIndent;
    }
    const size_t indent = style.FunctionParameterIndent ==
        FunctionParameterIndent::Continuation
        ? openBegin + 1 - lineStart : baseIndent + style.IndentWidth;
    const std::string continuation(indent, ' ');
    std::string result;
    if (firstNew) {
        result = "\n" + continuation;
    } else if (innerSpace) {
        result = " ";
    }
    result += parameters.front();
    size_t column = (firstNew ? indent : prefix + (innerSpace ? 1 : 0)) +
        parameters.front().size();
    bool broke = firstNew;
    for (size_t i = 1; i < parameters.size(); ++i) {
        bool last = i + 1 == parameters.size();
        bool overflow = column + 2 + parameters[i].size() +
            (last ? suffix.size() + (innerSpace ? 1 : 0) : 1) >
                style.ColumnLimit;
        bool forceSplit = split && !style.MergeFunctionParameters &&
            flatWidth <= style.ColumnLimit && !broke && last;
        bool breakBefore = style.FunctionParameterSplit ==
            FunctionParameterSplit::OnePerLine || overflow || forceSplit;
        result += ',';
        if (breakBefore) {
            result += '\n';
            result += continuation;
            column = indent + parameters[i].size();
            broke = true;
        } else {
            result += ' ';
            column += 2 + parameters[i].size();
        }
        result += parameters[i];
    }
    if (innerSpace) {
        result.push_back(' ');
    }
    if (result == source.substr(begin, end - begin)) {
        return std::nullopt;
    }
    return Replacement{begin, end, std::move(result)};
}

// Build comments only for unambiguous namespace headers and matched braces.
// Never infer a name from a recovered declaration or a namespace alias.
std::optional<std::pair<TokenIndex, std::string>> NamespaceClosing(
    const parser::SyntaxTree& tree, const parser::Node& node)
{
    TokenIndex opening = NoToken;
    std::string name;
    bool expectName = true;
    for (auto index = node.Tokens.Begin; index < node.Tokens.End; ++index) {
        auto kind = tree.Kind(index);
        if (kind == TK::Keyword_namespace && index == node.Tokens.Begin) {
            continue;
        }
        if (kind == TK::Operator_OpenBrace) {
            opening = index;
            break;
        }
        if (kind == TK::Comment) {
            continue;
        }
        if (kind == TK::Keyword_inline && expectName) {
            continue;
        }
        if (kind == TK::Identifier && expectName) {
            name += tree.TokenText(index);
            expectName = false;
        } else if (kind == TK::Operator_ScopeResolution && !expectName) {
            name += "::";
            expectName = true;
        } else {
            return std::nullopt;
        }
    }
    if (opening == NoToken || (expectName && !name.empty())) {
        return std::nullopt;
    }
    auto closing = tree.MatchingDelimiter(opening);
    if (closing == NoToken || closing >= node.Tokens.End ||
        tree.Kind(closing) != TK::Operator_CloseBrace)
    {
        return std::nullopt;
    }
    return std::pair{closing, std::move(name)};
}

std::string FormatNamespaceComments(
    const parser::SyntaxTree& tree, NamespaceComments policy)
{
    std::vector<Replacement> edits;
    std::vector<const parser::Node*> nodes{&tree.Root()};
    auto source = tree.Source();
    while (!nodes.empty()) {
        auto* node = nodes.back();
        nodes.pop_back();
        if (node->Kind == NK::NamespaceDeclaration) {
            if (auto closing = NamespaceClosing(tree, *node)) {
                auto index = closing->first;
                auto begin = tree.Token(index).Text.End;
                auto next = index + 1;
                auto gap = tree.WhitespaceAfter(index);
                bool sameLine = gap.find_first_of("\r\n") ==
                    std::string_view::npos;
                bool lineComment = sameLine && next < tree.TokenCount() &&
                    tree.Kind(next) == TK::Comment &&
                    tree.TokenText(next).starts_with("//");
                if (lineComment) {
                    auto end = tree.Token(next).Text.End;
                    // Line-comment tokens may include the CR of CRLF.
                    if (end > begin && end < source.size() &&
                        source[end - 1] == '\r' && source[end] == '\n')
                    {
                        --end;
                    }
                    while (end < source.size() &&
                        (source[end] == ' ' || source[end] == '\t'))
                    {
                        ++end;
                    }
                    std::string replacement = policy == NamespaceComments::Remove
                        ? "" : " // namespace" +
                            (closing->second.empty() ? "" : " " + closing->second);
                    if (source.substr(begin, end - begin) != replacement) {
                        edits.push_back({begin, end, std::move(replacement)});
                    }
                } else if (policy == NamespaceComments::Required &&
                    (next == tree.TokenCount() || !sameLine ||
                        tree.Kind(next) != TK::Comment))
                {
                    std::string replacement = " // namespace";
                    if (!closing->second.empty()) {
                        replacement += " " + closing->second;
                    }
                    if (sameLine && next < tree.TokenCount()) {
                        // A line comment must not swallow the next token.
                        auto line = source.rfind('\n', begin);
                        auto start = line == std::string_view::npos ? 0 : line + 1;
                        auto prefix = source.substr(start,
                            tree.Token(index).Text.Begin - start);
                        auto newline = source.rfind('\n', begin);
                        if (newline == std::string_view::npos) {
                            newline = source.find('\n', begin);
                        }
                        replacement += newline != std::string_view::npos &&
                                newline != 0 && source[newline - 1] == '\r'
                            ? "\r\n" : "\n";
                        if (prefix.find_first_not_of(" \t") ==
                            std::string_view::npos)
                        {
                            replacement += prefix;
                        }
                        edits.push_back({begin, begin + gap.size(),
                            std::move(replacement)});
                    } else {
                        auto lineEnd = gap.find_first_of("\r\n");
                        edits.push_back({begin,
                            begin + (lineEnd == std::string_view::npos
                                ? gap.size() : lineEnd),
                            std::move(replacement)});
                    }
                }
            }
        }
        for (auto* child = node->Child(); child; child = child->NextSibling) {
            nodes.push_back(child);
        }
    }
    std::ranges::sort(edits, {}, &Replacement::Begin);
    std::string output;
    output.reserve(source.size());
    size_t cursor = 0;
    for (const auto& edit : edits) {
        if (edit.Begin < cursor) {
            continue;
        }
        output.append(source.substr(cursor, edit.Begin - cursor));
        output += edit.Text;
        cursor = edit.End;
    }
    output.append(source.substr(cursor));
    return output;
}

std::string FormatParameters(const parser::SyntaxTree& tree, const Style& style)
{
    if (style.FunctionParameterSplit == FunctionParameterSplit::Preserve) {
        return std::string(tree.Source());
    }
    std::vector<Replacement> edits;
    std::vector<std::pair<const parser::Node*, const parser::Node*>> nodes{
        {&tree.Root(), nullptr}};
    while (!nodes.empty()) {
        auto [node, parent] = nodes.back();
        nodes.pop_back();
        if (IsIncompleteStatement(*node)) {
            continue;
        }
        // A macro invocation can be recovered as an abstract function
        // declarator. Only a named declarator denotes a function here.
        if (node->Kind == parser::NodeKind::FunctionDeclarator &&
            parent && parent->Kind == parser::NodeKind::Declarator)
        {
            for (auto* child = node->Child(); child;
                child = child->NextSibling)
            {
                if (child->Kind == parser::NodeKind::ParameterList) {
                    if (auto replacement = ReflowParameters(tree, *child, style)) {
                        edits.push_back(std::move(*replacement));
                    }
                }
            }
        }
        for (auto* child = node->Child(); child; child = child->NextSibling) {
            nodes.emplace_back(child, node);
        }
    }
    std::ranges::sort(edits, {}, &Replacement::Begin);
    std::string output;
    output.reserve(tree.Source().size());
    size_t cursor = 0;
    for (const auto& edit : edits) {
        if (edit.Begin < cursor) {
            continue;
        }
        output.append(tree.Source().substr(cursor, edit.Begin - cursor));
        output += edit.Text;
        cursor = edit.End;
    }
    output.append(tree.Source().substr(cursor));
    return output;
}

struct IncludeDirective
{
    std::string_view Path;
    std::string_view Text;
    bool Angled;
};

std::optional<IncludeDirective> ParseInclude(std::string_view text)
{
    if (text.contains('\n') || text.contains('\\')) {
        return std::nullopt; // Continued or otherwise ambiguous directives.
    }
    auto skipSpaces = [&text] {
        auto next = text.find_first_not_of(" \t");
        text.remove_prefix(next == std::string_view::npos ? text.size() : next);
    };
    auto original = text;
    if (!text.starts_with('#')) {
        return std::nullopt;
    }
    text.remove_prefix(1);
    skipSpaces();
    if (!text.starts_with("include")) {
        return std::nullopt;
    }
    text.remove_prefix(7);
    skipSpaces();
    if (text.empty() || (text.front() != '<' && text.front() != '"')) {
        return std::nullopt;
    }
    bool angled = text.front() == '<';
    char closing = angled ? '>' : '"';
    auto end = text.find(closing, 1);
    if (end == std::string_view::npos || end == 1 ||
        text.substr(end + 1).find_first_not_of(" \t\r") !=
            std::string_view::npos)
    {
        return std::nullopt;
    }
    return IncludeDirective{.Path = text.substr(1, end - 1),
        .Text = original, .Angled = angled};
}

std::string FormatIncludes(const parser::SyntaxTree& tree)
{
    std::string output;
    size_t cursor = 0;
    for (TokenIndex begin = 0; begin < tree.TokenCount();) {
        // An adjacent comment may document the next include specifically.
        const bool attachedComment = begin > 0 &&
            tree.Kind(begin - 1) == TK::Comment &&
            (tree.WhitespaceBefore(begin) == "\n" ||
                tree.WhitespaceBefore(begin) == "\r\n");
        if (tree.Kind(begin) != TK::PreprocessingDirective ||
            tree.TokenPosition(begin).Column != 1 || attachedComment)
        {
            ++begin;
            continue;
        }
        auto first = ParseInclude(tree.TokenText(begin));
        if (!first) {
            ++begin;
            continue;
        }
        std::vector<IncludeDirective> directives{*first};
        TokenIndex end = begin + 1;
        while (end < tree.TokenCount() &&
            tree.Kind(end) == TK::PreprocessingDirective &&
            tree.TokenPosition(end).Column == 1 &&
            (tree.WhitespaceBefore(end) == "\n" ||
                tree.WhitespaceBefore(end) == "\r\n"))
        {
            auto next = ParseInclude(tree.TokenText(end));
            if (!next || tree.TokenText(end).ends_with('\r') !=
                    tree.TokenText(begin).ends_with('\r'))
            {
                break;
            }
            directives.push_back(*next);
            ++end;
        }
        if (directives.size() > 1) {
            std::ranges::stable_sort(directives, [] (const auto& left,
                const auto& right) {
                if (left.Angled != right.Angled) {
                    return !left.Angled; // Quoted headers before angled ones.
                }
                return left.Path < right.Path;
            });
            for (TokenIndex index = begin; index < end; ++index) {
                const auto token = tree.Token(index).Text;
                output.append(tree.Source().substr(cursor, token.Begin - cursor));
                output.append(directives[index - begin].Text);
                cursor = token.End;
            }
        }
        begin = end;
    }
    output.append(tree.Source().substr(cursor));
    return output;
}

} // namespace

std::string Format(const parser::SyntaxTree& tree, const Style& style)
{
    const bool bracesPreserved =
        style.IndentAccessModifiers == BodyIndentation::Preserve &&
        style.IndentMembers == BodyIndentation::Preserve &&
        style.InheritanceList == InheritanceListMode::Preserve &&
        style.ClassBracePlacement == BracePlacement::Preserve &&
        style.StructBracePlacement == BracePlacement::Preserve &&
        style.UnionBracePlacement == BracePlacement::Preserve &&
        style.FunctionBracePlacement == BracePlacement::Preserve &&
        style.LambdaBracePlacement == BracePlacement::Preserve &&
        style.NamespaceBracePlacement == BracePlacement::Preserve &&
        style.ClassBraceSpacing == SameLineBraceSpacing::Preserve &&
        style.LambdaBraceSpacing == SameLineBraceSpacing::Preserve &&
        style.StructBraceSpacing == SameLineBraceSpacing::Preserve &&
        style.EnumBraceSpacing == SameLineBraceSpacing::Preserve &&
        style.UnionBraceSpacing == SameLineBraceSpacing::Preserve &&
        style.FunctionBraceSpacing == SameLineBraceSpacing::Preserve &&
        style.NamespaceBraceSpacing == SameLineBraceSpacing::Preserve &&
        style.LambdaSpaceBeforeParameters ==
            LambdaSpaceBeforeParameters::Preserve &&
        style.SpaceInsideLambdaParameters ==
            SpaceInsideLambdaParameters::Preserve &&
        style.SpaceAroundCaptureList ==
            LambdaCaptureSpacing::Preserve &&
        style.SpaceAroundCaptureAssignment ==
            LambdaCaptureSpacing::Preserve &&
        style.FunctionSpaceBeforeParameters ==
            FunctionSpaceBeforeParameters::Preserve &&
        style.FunctionCallSpaceBeforeParameters ==
            FunctionCallSpaceBeforeParameters::Preserve &&
        style.SpaceInsideCallArguments ==
            SpaceInsideCallArguments::Preserve &&
        style.SpaceInsideFunctionParameters ==
            SpaceInsideFunctionParameters::Preserve &&
        style.ControlBlockKeywordSpacing ==
            ControlBlockKeywordSpacing::Preserve &&
        style.ControlBlockBracePlacement ==
            ControlBlockBracePlacement::Preserve;
    std::string formatted;
    if (style.FunctionParameterSplit == FunctionParameterSplit::Preserve) {
        formatted = Formatter(tree, style).Run();
    } else if (bracesPreserved) {
        formatted = FormatParameters(tree, style);
    } else {
        auto braced = Formatter(tree, style).Run();
        if (braced == tree.Source()) {
            formatted = FormatParameters(tree, style);
        } else {
            auto length = braced.size();
            braced.append(util::SourcePaddingSize, '\0');
            auto bracedTree = parser::Parse(
                {.Data = braced.data(), .Size = length},
                [] (parser::ParseError) { });
            formatted = FormatParameters(bracedTree, style);
        }
    }
    if (style.NamespaceComments != NamespaceComments::Preserve) {
        if (formatted == tree.Source()) {
            formatted = FormatNamespaceComments(tree, style.NamespaceComments);
        } else {
            auto length = formatted.size();
            formatted.append(util::SourcePaddingSize, '\0');
            auto finalTree = parser::Parse(
                {.Data = formatted.data(), .Size = length},
                [] (parser::ParseError) { });
            formatted = FormatNamespaceComments(finalTree,
                style.NamespaceComments);
        }
    }
    if (style.IncludeOrder == IncludeOrder::Preserve) {
        return formatted;
    }
    if (formatted == tree.Source()) {
        return FormatIncludes(tree);
    }
    auto length = formatted.size();
    formatted.append(util::SourcePaddingSize, '\0');
    auto finalTree = parser::Parse(
        {.Data = formatted.data(), .Size = length},
        [] (parser::ParseError) { });
    return FormatIncludes(finalTree);
}

std::string Format(const parser::SyntaxTree& tree)
{
    return Format(tree, Style{});
}

std::string Format(util::PaddedSourceView source, const Style& style)
{
    auto tree = parser::Parse(source, [] (parser::ParseError) { });
    return Format(tree, style);
}

std::string Format(util::PaddedSourceView source)
{
    return Format(source, Style{});
}

} // namespace formatter
