#include "formatter.hpp"

#include <parser/parser.hpp>
#include <tokenizer/token.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
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

std::string GeneratedIndent(size_t columns, const Style& style)
{
    if (style.IndentationKind != IndentationKind::Tabs ||
        style.IndentWidth == 0)
    {
        return std::string(columns, ' ');
    }
    return std::string(columns / style.IndentWidth, '\t') +
        std::string(columns % style.IndentWidth, ' ');
}

std::string GeneratedIndentAfter(std::string_view prefix, size_t columns,
    const Style& style)
{
    if (style.IndentationKind != IndentationKind::Tabs ||
        style.IndentWidth == 0)
    {
        return std::string(columns, ' ');
    }
    size_t column = 0;
    for (char ch : prefix) {
        if (ch == '\t') {
            column += style.IndentWidth - column % style.IndentWidth;
        } else if (ch == ' ') {
            ++column;
        } else {
            return std::string(columns, ' ');
        }
    }
    std::string indent;
    const auto target = column + columns;
    while (column < target) {
        const auto next = column +
            style.IndentWidth - column % style.IndentWidth;
        if (next <= target) {
            indent.push_back('\t');
            column = next;
        } else {
            indent.append(target - column, ' ');
            break;
        }
    }
    return indent;
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
            style.ConstructorInitializerList == InheritanceListMode::Format ||
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
                        Tree_.WhitespaceAfter(index).find('\n') ==
                            std::string_view::npos)
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
            MarkConstructorInitializers(node);
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
                    BodyIndents_[index] += GeneratedIndentAfter(
                        BodyIndents_[index], Style_.IndentWidth, Style_);
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
        const parser::Node& first, bool normalizeKeywords = true) const
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
            if (normalizeKeywords) {
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
        indent += GeneratedIndentAfter(indent,
            Style_.InheritanceListIndent == 0 ? Style_.IndentWidth
                : Style_.InheritanceListIndent, Style_);
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
                Tree_.WhitespaceBefore(declaration.Tokens.Begin).find('\n') !=
                    std::string_view::npos)
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
        const bool overflow = header.find('\n') != std::string_view::npos ||
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

    void MarkConstructorInitializers(const parser::Node& definition)
    {
        if (Style_.ConstructorInitializerList != InheritanceListMode::Format ||
            HasRecoveryChild(definition))
        {
            return;
        }
        const parser::Node* list = nullptr;
        std::vector<const parser::Node*> pending;
        for (auto* child = definition.Child(); child;
            child = child->NextSibling)
        {
            if (child->Kind != NK::CompoundStatement) {
                pending.push_back(child);
            }
        }
        while (!pending.empty()) {
            const auto* node = pending.back();
            pending.pop_back();
            if (node->Kind == NK::ConstructorInitializerList) {
                list = node;
                break;
            }
            if (node->Kind == NK::FunctionDeclarator ||
                node->Kind == NK::InitDeclaratorList ||
                node->Kind == NK::InitDeclarator ||
                node->Kind == NK::Declarator ||
                node->Kind == NK::FunctionTryBlock)
            {
                for (auto* child = node->Child(); child;
                    child = child->NextSibling)
                {
                    pending.push_back(child);
                }
            }
        }
        if (!list || HasRecoveryChild(*list) || list->Tokens.Empty() ||
            list->Tokens.End >= Tree_.TokenCount() ||
            Tree_.Kind(list->Tokens.End) != TK::Operator_OpenBrace)
        {
            return;
        }
        const auto colon = list->Tokens.Begin;
        if (colon <= definition.Tokens.Begin ||
            Tree_.Kind(colon) != TK::Operator_Colon ||
            Tree_.Role(colon) !=
                parser::PunctuationRole::ConstructorInitializerDelimiter ||
            Tree_.Kind(colon - 1) == TK::Comment ||
            Tree_.Kind(colon - 1) == TK::PreprocessingDirective)
        {
            return;
        }
        const auto* first = list->Child();
        const auto* last = first;
        bool expectInitializer = true;
        for (auto* child = first; child; child = child->NextSibling) {
            if (child->Tokens.Empty() || HasRecoveryChild(*child) ||
                child->Kind != (expectInitializer
                    ? NK::ConstructorInitializer : NK::ListSeparator))
            {
                return;
            }
            expectInitializer = !expectInitializer;
            last = child;
        }
        if (!first || !last || expectInitializer ||
            first->Tokens.Begin != colon + 1 ||
            last->Tokens.End != list->Tokens.End)
        {
            return;
        }
        for (auto* child = first; child; child = child->NextSibling) {
            if (child->Kind != NK::ConstructorInitializer) {
                continue;
            }
            auto begin = child->Tokens.Begin;
            auto end = child->Tokens.End - 1;
            if ((Tree_.Kind(begin) != TK::Identifier &&
                    Tree_.Kind(begin) != TK::Keyword_decltype) ||
                (Tree_.Kind(end) != TK::Operator_CloseParenthesis &&
                    Tree_.Kind(end) != TK::Operator_CloseBrace))
            {
                return;
            }
            bool complete = false;
            for (auto i = begin + 1; i < end; ++i) {
                if ((Tree_.Kind(i) == TK::Operator_OpenParenthesis ||
                        Tree_.Kind(i) == TK::Operator_OpenBrace) &&
                    Tree_.MatchingDelimiter(i) == end)
                {
                    complete = true;
                    break;
                }
            }
            if (!complete) {
                return;
            }
        }
        auto entries = CollectBases(*first, false);
        if (!entries || entries->empty()) {
            return;
        }
        auto beforeColon = Tree_.WhitespaceBefore(colon);
        auto beforeInitializer = Tree_.WhitespaceBefore(colon + 1);
        if (beforeColon.find_first_not_of(" \t\r\n") !=
                std::string_view::npos ||
            beforeInitializer.find_first_not_of(" \t\r\n") !=
                std::string_view::npos)
        {
            return;
        }
        std::string indent(Indentation(definition.Tokens.Begin));
        if (auto parent = BodyIndents_.find(definition.Tokens.Begin);
            parent != BodyIndents_.end())
        {
            indent = parent->second;
        }
        indent += GeneratedIndentAfter(indent,
            Style_.ConstructorInitializerListIndent == 0
                ? Style_.IndentWidth
                : Style_.ConstructorInitializerListIndent, Style_);
        auto space = [](InheritanceListSpacing policy) -> std::string_view {
            return policy == InheritanceListSpacing::Space ? " " : "";
        };
        const auto colonBefore =
            space(Style_.ConstructorInitializerListColonSpaceBefore);
        const auto colonAfter =
            space(Style_.ConstructorInitializerListColonSpaceAfter);
        const auto commaBefore =
            space(Style_.ConstructorInitializerListCommaSpaceBefore);
        const auto commaAfter =
            space(Style_.ConstructorInitializerListCommaSpaceAfter);
        const auto source = Tree_.Source();
        const auto headerBegin = Tree_.Token(definition.Tokens.Begin).Text.Begin;
        const auto headerEnd = Tree_.Token(colon - 1).Text.End;
        const auto header = source.substr(headerBegin, headerEnd - headerBegin);
        const auto lineStart = source.rfind('\n', headerBegin);
        auto prefix = headerBegin - (lineStart == std::string_view::npos
            ? 0 : lineStart + 1);
        if (auto parent = BodyIndents_.find(definition.Tokens.Begin);
            parent != BodyIndents_.end() &&
                Tree_.WhitespaceBefore(definition.Tokens.Begin).find('\n') !=
                    std::string_view::npos)
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
        const bool overflow = header.find('\n') != std::string_view::npos ||
            flatWidth > Style_.ColumnLimit;
        const bool firstNew = Style_.ConstructorInitializerListStart ==
                ConstructorInitializerStart::NewLine ||
            (Style_.ConstructorInitializerListStart ==
                ConstructorInitializerStart::NewLineOnOverflow && overflow);
        const auto newline = std::string(LineEnding(colon)) + indent;
        const bool colonWithInitializer =
            Style_.ConstructorInitializerListColonPlacement ==
                ConstructorInitializerColonPlacement::AttachToInitializer;
        InheritanceGaps_[colon] = firstNew && colonWithInitializer
            ? newline : std::string(colonBefore);
        InheritanceGaps_[entries->front().Opening] =
            firstNew && !colonWithInitializer ? newline
                                             : std::string(colonAfter);
        auto column = firstNew
            ? indent.size() + entries->front().Text.size() +
                (colonWithInitializer ? 1 + colonAfter.size() : 0)
            : prefix + header.size() + colonBefore.size() + 1 +
                colonAfter.size() + entries->front().Text.size();
        const auto split = Style_.ConstructorInitializerListSplit;
        const bool onePerLine = split == InheritanceListSplit::OnePerLine ||
            (split == InheritanceListSplit::OnePerLineOnOverflow && overflow);
        for (size_t i = 1; i < entries->size(); ++i) {
            const auto& entry = (*entries)[i];
            const auto width = column + commaBefore.size() + 1 +
                commaAfter.size() + entry.Text.size();
            const bool breakLine = onePerLine || width > Style_.ColumnLimit;
            if (breakLine && Style_.ConstructorInitializerListCommaPlacement ==
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
            const bool multiline =
                Tree_.Source().substr(begin, end - begin).find('\n') !=
                std::string_view::npos;
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
            Tree_.WhitespaceAfter(index + 1).find('\n') !=
            std::string_view::npos;
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

// Minimize a bounded whole-list cost for Pack. The candidate boundaries are
// precisely the parsed argument commas; explicit OnePerLine and Preserve
// policies never pass through this search.
std::optional<std::vector<bool>> ChooseCallArgumentBreaks(
    const std::vector<std::string_view>& arguments, size_t firstColumn,
    size_t continuationColumn, size_t suffixWidth, size_t columnLimit,
    bool requireBreak)
{
    constexpr size_t MaxArguments = 96;
    const auto count = arguments.size();
    if (count < 2 || count > MaxArguments) {
        return std::nullopt;
    }
    constexpr auto Infinity = std::numeric_limits<uint64_t>::max();
    std::vector<uint64_t> cost(count + 1, Infinity);
    std::vector<size_t> next(count, count);
    cost[count] = 0;
    for (size_t start = count; start-- > 0;) {
        auto width = start == 0 ? firstColumn : continuationColumn;
        for (auto end = start; end < count; ++end) {
            width += arguments[end].size() + (end == start ? 0 : 2);
            const bool last = end + 1 == count;
            auto projected = width + (last ? suffixWidth : 1);
            if (projected > columnLimit) {
                break;
            }
            if ((requireBreak && start == 0 && last) ||
                cost[end + 1] == Infinity)
            {
                continue;
            }
            auto slack = columnLimit - projected;
            auto score = cost[end + 1] + uint64_t(slack) * slack /
                (last ? 2 : 1) + (last ? 0 : 250);
            if (score < cost[start]) {
                cost[start] = score;
                next[start] = end + 1;
            }
        }
    }
    if (cost[0] == Infinity) {
        return std::nullopt;
    }
    std::vector<bool> breakAfter(count - 1, false);
    for (size_t start = 0; start < count && next[start] < count;) {
        breakAfter[next[start] - 1] = true;
        start = next[start];
    }
    return breakAfter;
}

// Only rewrite lists whose comma-separated elements and intervening trivia
// can be moved without touching comments, directives, or nested line breaks.
std::optional<Replacement> ReflowDelimitedList(
    const parser::SyntaxTree& tree, const parser::Node& list,
    const Style& style)
{
    using TK = tokenizer::TokenKind;
    const bool call = list.Kind == NK::ArgumentList;
    const bool braced = list.Kind == NK::InitializerList;
    if (!call && !braced && list.Kind != NK::ParameterList) {
        return std::nullopt;
    }
    const bool merge = braced
        ? style.MergeInitializerList &&
            style.InitializerClosingBrace != InitializerClosingBrace::NewLine
        : call
        ? style.ExpressionLayoutCondition == ExpressionLayoutCondition::FitOrOverflow ||
            style.ExpressionLayoutCondition == ExpressionLayoutCondition::Always
        : style.MergeFunctionParameters;
    const bool onePerLine = braced
        ? style.InitializerListSplit == FunctionParameterSplit::OnePerLine : call
        ? style.CallArgumentSplit == CallArgumentSplit::OnePerLine
        : style.FunctionParameterSplit == FunctionParameterSplit::OnePerLine;
    const bool enforceFirst = braced ? false : call
        ? style.EnforceFirstCallArgumentPosition
        : style.EnforceFirstFunctionParameterPosition;
    const bool newLineFirst = braced
        ? style.FirstInitializerElementPosition ==
            FirstInitializerElementPosition::NewLine : call
        ? style.FirstCallArgumentPosition == FirstCallArgumentPosition::NewLine
        : style.FirstFunctionParameterPosition ==
            FirstFunctionParameterPosition::NewLine;
    const bool continuationIndent = braced
        ? style.InitializerListIndent == CallArgumentIndent::Continuation : call
        ? style.CallArgumentIndent == CallArgumentIndent::Continuation
        : style.FunctionParameterIndent == FunctionParameterIndent::Continuation;
    const bool innerSpace = braced
        ? style.InitializerBracesSpacing == TokenSpacing::Space : call
        ? style.SpaceInsideCallArguments == SpaceInsideCallArguments::Space
        : style.SpaceInsideFunctionParameters ==
            SpaceInsideFunctionParameters::Space;
    auto opening = list.Tokens.Begin;
    auto closing = list.Tokens.End - 1;
    if (list.Tokens.Size() < 3 ||
        tree.Kind(opening) != (braced ? TK::Operator_OpenBrace
                                     : TK::Operator_OpenParenthesis) ||
        tree.Kind(closing) != (braced ? TK::Operator_CloseBrace
                                     : TK::Operator_CloseParenthesis) ||
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
        if (expectParameter ? child->Kind != (braced ? NK::InitializerElement :
            call ? NK::Argument : NK::ParameterDeclaration)
                            : child->Kind != NK::ListSeparator)
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
    const bool trailingComma = braced && expectParameter &&
        !parameters.empty();
    if ((expectParameter && !trailingComma) || parameters.empty() ||
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
    flatWidth += (innerSpace ? 2 : 0) + (trailingComma ? 1 : 0);
    bool split = source.substr(begin, end - begin).find('\n') !=
        std::string_view::npos;
    if ((call && parameters.size() == 1) ||
        (!split && flatWidth <= style.ColumnLimit))
    {
        return std::nullopt;
    }
    if (call && split && !(merge && flatWidth <= style.ColumnLimit) &&
        style.ExpressionLayoutCondition != ExpressionLayoutCondition::Always)
    {
        // Overflow and FitOrOverflow retain an existing multiline layout
        // unless one of its physical lines actually exceeds the limit.
        bool overlong = false;
        for (auto from = lineStart; from <= end;) {
            auto until = source.find('\n', from);
            if (until == std::string_view::npos) {
                until = source.size();
            }
            overlong |= until - from > style.ColumnLimit;
            if (until >= end || until == source.size()) {
                break;
            }
            from = until + 1;
        }
        if (!overlong) {
            return std::nullopt;
        }
    }

    std::string flat;
    for (auto parameter : parameters) {
        if (!flat.empty()) {
            flat += ", ";
        }
        flat += parameter;
    }
    if (split && merge && flatWidth <= style.ColumnLimit)
    {
        if (trailingComma) flat += ',';
        if (innerSpace) {
            flat.insert(flat.begin(), ' ');
            flat.push_back(' ');
        }
        return Replacement{begin, end, std::move(flat)};
    }
    if (parameters.size() == 1 && flatWidth <= style.ColumnLimit) {
        return std::nullopt;
    }

    bool firstNew = newLineFirst;
    if (split && !enforceFirst &&
        (!call || style.ExpressionLayoutCondition != ExpressionLayoutCondition::Always)) {
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
    const size_t indent = continuationIndent
        ? openBegin + 1 - lineStart : baseIndent + style.IndentWidth;
    const std::string continuation = GeneratedIndent(indent, style);
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
    std::optional<std::vector<bool>> chosenBreaks;
    if ((call || braced) && !onePerLine &&
        style.ExpressionLayoutAlgorithm ==
            ExpressionLayoutAlgorithm::DynamicProgramming)
    {
        chosenBreaks = ChooseCallArgumentBreaks(parameters,
            firstNew ? indent : prefix + (innerSpace ? 1 : 0), indent,
            suffix.size() + (innerSpace ? 1 : 0) + (trailingComma ? 1 : 0),
            style.ColumnLimit,
            split && !merge && flatWidth <= style.ColumnLimit && !firstNew);
    }
    for (size_t i = 1; i < parameters.size(); ++i) {
        bool last = i + 1 == parameters.size();
        bool overflow = column + 2 + parameters[i].size() +
            (last ? suffix.size() + (innerSpace ? 1 : 0) : 1) >
                style.ColumnLimit;
        bool forceSplit = split && !merge &&
            flatWidth <= style.ColumnLimit && !broke && last;
        bool breakBefore = chosenBreaks ? (*chosenBreaks)[i - 1] :
            onePerLine || overflow || forceSplit;
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
    if (trailingComma) result.push_back(',');
    if (braced) {
        auto closingLine = source.rfind('\n', end);
        bool oldCloseNew = closingLine != std::string_view::npos &&
            source.substr(closingLine + 1, end - closingLine - 1)
                .find_first_not_of(" \t") == std::string_view::npos;
        bool closeNew = style.InitializerClosingBrace ==
            InitializerClosingBrace::NewLine ||
            (style.InitializerClosingBrace == InitializerClosingBrace::Preserve &&
                oldCloseNew);
        if (style.InitializerClosingBrace == InitializerClosingBrace::SameLine)
            closeNew = false;
        if (closeNew) {
            result += "\n";
            result += GeneratedIndent(baseIndent, style);
        } else if (innerSpace) {
            result.push_back(' ');
        }
    } else if (innerSpace) {
        result.push_back(' ');
    }
    if (result == source.substr(begin, end - begin)) {
        return std::nullopt;
    }
    return Replacement{begin, end, std::move(result)};
}

// Even when the parser assigns a comparison or shift role, bare angle
// operators can be template delimiters without name lookup (for example,
// TDependentFalse<Args...>). Never move or space these ambiguous tokens.
bool IsAmbiguousAngleOperator(TK kind)
{
    return kind == TK::Operator_Less || kind == TK::Operator_Greater ||
        kind == TK::Operator_LeftShift || kind == TK::Operator_RightShift;
}

// Expression layout only uses operators identified by the parser. In Always
// mode, normalize safe operator gaps before applying the same width-based
// splitting pass used by Overflow.
std::string FlattenExpressionOperators(const parser::SyntaxTree& tree,
    bool normalizeAll, bool preserveTernary,
    std::vector<size_t>* mergedPositions = nullptr,
    std::vector<Replacement>* mergedEdits = nullptr)
{
    const auto source = tree.Source();
    std::vector<bool> unsafe(tree.TokenCount(), false);
    TokenIndex cursor = 0;
    for (size_t lineStart = 0; lineStart < source.size();) {
        auto lineEnd = source.find('\n', lineStart);
        if (lineEnd == std::string_view::npos) {
            lineEnd = source.size();
        }
        while (cursor < tree.TokenCount() &&
            tree.Token(cursor).Text.End <= lineStart)
        {
            ++cursor;
        }
        auto end = cursor;
        bool protectedLine = (lineEnd > lineStart &&
            source[lineEnd - 1] == '\r') ||
            source.substr(lineStart, lineEnd - lineStart).find('\\') !=
            std::string_view::npos;
        auto first = source.find_first_not_of(" \t", lineStart);
        protectedLine |= first < lineEnd && source[first] == '#';
        while (end < tree.TokenCount() &&
            tree.Token(end).Text.Begin < lineEnd)
        {
            const auto token = tree.Token(end).Text;
            protectedLine |= tree.Kind(end) == TK::Comment ||
                tree.Kind(end) == TK::PreprocessingDirective ||
                token.Begin < lineStart || token.End > lineEnd ||
                source.substr(token.Begin, token.End - token.Begin)
                    .find('\r') != std::string_view::npos;
            ++end;
        }
        if (protectedLine) {
            for (auto i = cursor; i < end; ++i) {
                unsafe[i] = true;
            }
        }
        lineStart = lineEnd == source.size() ? lineEnd : lineEnd + 1;
    }

    std::vector<TokenIndex> operators;
    std::vector<bool> seen(tree.TokenCount(), false);
    std::vector<const parser::Node*> nodes{&tree.Root()};
    while (!nodes.empty()) {
        auto* node = nodes.back();
        nodes.pop_back();
        if (IsIncompleteStatement(*node)) {
            continue;
        }
        if (node->Kind == NK::BinaryExpression ||
            node->Kind == NK::AssignmentExpression ||
            (node->Kind == NK::ConditionalExpression && !preserveTernary))
        {
            const auto* left = node->Child();
            const auto* right = left ? left->NextSibling : nullptr;
            const auto* alternative = right ? right->NextSibling : nullptr;
            if (left && right && !left->Tokens.Empty() &&
                !right->Tokens.Empty() &&
                (node->Kind != NK::ConditionalExpression ||
                    (alternative && !alternative->Tokens.Empty())))
            {
                auto collect = [&] (TokenIndex index) {
                    if (index > 0 && index + 1 < tree.TokenCount() &&
                        !seen[index] && !unsafe[index - 1] &&
                        !unsafe[index] && !unsafe[index + 1] &&
                        tree.Kind(index) != TK::Operator_Comma &&
                        !IsAmbiguousAngleOperator(tree.Kind(index)))
                    {
                        seen[index] = true;
                        operators.push_back(index);
                    }
                };
                for (auto i = left->Tokens.End; i < right->Tokens.Begin; ++i) {
                    const auto role = tree.Role(i);
                    if (node->Kind == NK::ConditionalExpression
                            ? role == parser::PunctuationRole::ConditionalDelimiter
                            : node->Kind == NK::AssignmentExpression
                                ? role == parser::PunctuationRole::AssignmentOperator
                                : role == parser::PunctuationRole::BinaryOperator ||
                                    role == parser::PunctuationRole::ComparisonOperator ||
                                    role == parser::PunctuationRole::ShiftOperator)
                    {
                        collect(i);
                    }
                }
                if (node->Kind == NK::ConditionalExpression) {
                    for (auto i = right->Tokens.End;
                        i < alternative->Tokens.Begin; ++i)
                    {
                        if (tree.Kind(i) == TK::Operator_Colon &&
                            tree.Role(i) ==
                                parser::PunctuationRole::ConditionalDelimiter)
                        {
                            collect(i);
                        }
                    }
                }
            }
        }
        for (auto* child = node->Child(); child; child = child->NextSibling) {
            nodes.push_back(child);
        }
    }
    std::vector<Replacement> edits;
    for (auto index : operators) {
        auto before = tree.WhitespaceBefore(index);
        auto after = tree.WhitespaceAfter(index);
        if (!IsWhitespace(before) || !IsWhitespace(after) ||
            before.find('\r') != std::string_view::npos ||
            after.find('\r') != std::string_view::npos)
        {
            continue;
        }
        auto opening = tree.Token(index).Text.Begin;
        auto closing = tree.Token(index).Text.End;
        if (before != " " &&
            (normalizeAll || before.find('\n') != std::string_view::npos))
        {
            edits.push_back({opening - before.size(), opening, " "});
        }
        if (after != " " &&
            (normalizeAll || after.find('\n') != std::string_view::npos))
        {
            edits.push_back({closing, closing + after.size(), " "});
        }
    }
    std::ranges::sort(edits, {}, &Replacement::Begin);
    std::string output;
    output.reserve(source.size());
    std::vector<Replacement> applied;
    size_t from = 0;
    for (const auto& edit : edits) {
        if (edit.Begin < from) {
            continue;
        }
        if (mergedEdits) {
            applied.push_back(edit);
        }
        output.append(source.substr(from, edit.Begin - from));
        if (mergedPositions &&
            source.substr(edit.Begin, edit.End - edit.Begin).find('\n') !=
                std::string_view::npos)
        {
            mergedPositions->push_back(output.size());
        }
        output += edit.Text;
        from = edit.End;
    }
    output.append(source.substr(from));
    if (mergedEdits) {
        *mergedEdits = std::move(applied);
    }
    return output;
}

// Break only at operators identified by the parser. Other token and trivia
// bytes remain source-owned; comments, directives, CRLF, and multiline tokens
// keep their physical lines unchanged.
int ExpressionPriority(TK kind)
{
    switch (kind) {
    case TK::Operator_Or: return 2;
    case TK::Operator_And: return 3;
    case TK::Operator_BitOr: return 4;
    case TK::Operator_Xor: return 5;
    case TK::Operator_Ampersand: return 6;
    case TK::Operator_Equals:
    case TK::Operator_NotEquals: return 7;
    case TK::Operator_Less:
    case TK::Operator_Greater:
    case TK::Operator_LessEquals:
    case TK::Operator_GreaterEquals:
    case TK::Operator_Spaceship: return 8;
    case TK::Operator_LeftShift:
    case TK::Operator_RightShift: return 9;
    case TK::Operator_Plus:
    case TK::Operator_Minus: return 10;
    default: return 11;
    }
}

struct ExpressionBreak
{
    size_t Begin;
    size_t End;
    int Priority;
    int Nesting;
};

// Find a minimum-cost path through the same safe operator gaps as the greedy
// pass. Each edge is a physical line; short lines and higher-precedence
// breaks cost more. Bound the search on pathological single-line expressions.
std::vector<size_t> ChooseExpressionBreaks(
    std::string_view source, const std::vector<ExpressionBreak>& breaks,
    size_t begin, size_t end, size_t lineStart, size_t lineEnd,
    size_t continuationIndent, size_t columnLimit)
{
    constexpr size_t MaxCandidates = 96;
    const auto count = end - begin;
    if (count == 0 || count > MaxCandidates) {
        return {};
    }
    constexpr auto Infinity = std::numeric_limits<uint64_t>::max();
    int outerNesting = breaks[begin].Nesting;
    for (auto i = begin + 1; i < end; ++i) {
        outerNesting = std::min(outerNesting, breaks[i].Nesting);
    }
    std::vector<uint64_t> cost(count + 1, Infinity);
    std::vector<size_t> next(count + 1, count);
    for (size_t state = count + 1; state-- > 0;) {
        const auto start = state == 0 ? lineStart
            : breaks[begin + state - 1].End;
        const auto indent = state == 0 ? 0 : continuationIndent;
        if (start <= lineEnd && indent + lineEnd - start <= columnLimit) {
            auto slack = columnLimit - (indent + lineEnd - start);
            cost[state] = uint64_t(slack) * slack / 2;
        }
        for (size_t choice = state; choice < count; ++choice) {
            const auto& candidate = breaks[begin + choice];
            if (candidate.Begin <= start) {
                continue;
            }
            const auto width = indent + candidate.Begin - start;
            if (width > columnLimit) {
                break; // Breakpoints are sorted by source position.
            }
            if (source.substr(start, candidate.Begin - start)
                    .find_first_not_of(" \t") == std::string_view::npos ||
                cost[choice + 1] == Infinity)
            {
                continue;
            }
            auto slack = columnLimit - width;
            auto score = cost[choice + 1] + uint64_t(slack) * slack +
                250 + 45 * candidate.Priority +
                8000 * (candidate.Nesting - outerNesting);
            if (score < cost[state]) {
                cost[state] = score;
                next[state] = choice;
            }
        }
    }
    if (cost[0] == Infinity || next[0] == count) {
        return {};
    }
    std::vector<size_t> selected;
    for (size_t state = 0; next[state] < count;) {
        auto choice = next[state];
        selected.push_back(begin + choice);
        state = choice + 1;
    }
    return selected;
}

// Identify both delimiters of one complete parsed conditional expression.
// A colon in a nested conditional, label, or braced initializer is not ours.
std::optional<std::pair<TokenIndex, TokenIndex>> ConditionalDelimiters(
    const parser::SyntaxTree& tree, const parser::Node& node)
{
    if (node.Kind != NK::ConditionalExpression) {
        return std::nullopt;
    }
    const auto* condition = node.Child();
    const auto* thenValue = condition ? condition->NextSibling : nullptr;
    const auto* elseValue = thenValue ? thenValue->NextSibling : nullptr;
    if (!condition || !thenValue || !elseValue ||
        condition->Tokens.Empty() || thenValue->Tokens.Empty() ||
        elseValue->Tokens.Empty())
    {
        return std::nullopt;
    }
    TokenIndex question = NoToken;
    TokenIndex colon = NoToken;
    for (auto i = condition->Tokens.End; i < thenValue->Tokens.Begin; ++i) {
        if (tree.Kind(i) == TK::Operator_Conditional &&
            tree.Role(i) == parser::PunctuationRole::ConditionalDelimiter)
        {
            question = i;
        }
    }
    for (auto i = thenValue->Tokens.End; i < elseValue->Tokens.Begin; ++i) {
        if (tree.Kind(i) == TK::Operator_Colon &&
            tree.Role(i) == parser::PunctuationRole::ConditionalDelimiter)
        {
            colon = i;
        }
    }
    if (question == NoToken || colon == NoToken) {
        return std::nullopt;
    }
    return std::pair{question, colon};
}

bool SafeOperatorGap(const parser::SyntaxTree& tree, TokenIndex index)
{
    if (index == 0 || index + 1 >= tree.TokenCount()) {
        return false;
    }
    auto before = tree.WhitespaceBefore(index);
    auto after = tree.WhitespaceAfter(index);
    return IsWhitespace(before) && IsWhitespace(after) &&
        before.find('\r') == std::string_view::npos &&
        after.find('\r') == std::string_view::npos &&
        tree.Kind(index - 1) != TK::Comment &&
        tree.Kind(index + 1) != TK::Comment &&
        tree.Kind(index - 1) != TK::PreprocessingDirective &&
        tree.Kind(index + 1) != TK::PreprocessingDirective &&
        tree.TokenText(index - 1).find('\n') == std::string_view::npos &&
        tree.TokenText(index + 1).find('\n') == std::string_view::npos;
}

void PlaceOperatorBreak(const parser::SyntaxTree& tree, TokenIndex index,
    bool leading, std::vector<Replacement>& edits)
{
    if (!SafeOperatorGap(tree, index)) {
        return;
    }
    auto before = tree.WhitespaceBefore(index);
    auto after = tree.WhitespaceAfter(index);
    const bool leftBreak = before.find('\n') != std::string_view::npos;
    const bool rightBreak = after.find('\n') != std::string_view::npos;
    if (!leftBreak && !rightBreak) {
        return;
    }
    const auto begin = tree.Token(index).Text.Begin;
    const auto end = tree.Token(index).Text.End;
    if (leftBreak && rightBreak) {
        auto redundant = leading ? after : before;
        auto breakAt = redundant.find('\n');
        if (redundant.find('\n', breakAt + 1) ==
            std::string_view::npos)
        {
            edits.push_back(leading
                ? Replacement{end, end + after.size(), " "}
                : Replacement{begin - before.size(), begin, " "});
        }
        return;
    }
    if (leading == leftBreak) {
        return;
    }
    auto newline = leftBreak ? before : after;
    auto breakAt = newline.find('\n');
    if (newline.find('\n', breakAt + 1) != std::string_view::npos) {
        return;
    }
    newline.remove_prefix(breakAt); // Discard trailing spaces.
    edits.push_back({begin - before.size(), begin,
        leading ? std::string(newline) : " "});
    edits.push_back({end, end + after.size(),
        leading ? " " : std::string(newline)});
}

// Move only the newline beside parsed binary and ternary operators. This
// enforces placement without regrouping already split expressions.
std::string PlaceExistingOperatorBreaks(const parser::SyntaxTree& tree,
    const Style& style)
{
    const auto source = tree.Source();
    std::vector<Replacement> edits;
    std::vector<const parser::Node*> nodes{&tree.Root()};
    while (!nodes.empty()) {
        auto* node = nodes.back();
        nodes.pop_back();
        if (IsIncompleteStatement(*node)) {
            continue;
        }
        if (node->Kind == NK::BinaryExpression) {
            const auto* left = node->Child();
            const auto* right = left ? left->NextSibling : nullptr;
            if (left && right && !left->Tokens.Empty() &&
                !right->Tokens.Empty())
            {
                for (auto i = left->Tokens.End; i < right->Tokens.Begin; ++i) {
                    const auto kind = tree.Kind(i);
                    const auto role = tree.Role(i);
                    if (i == 0 || i + 1 == tree.TokenCount() ||
                        kind == TK::Operator_Comma ||
                        IsAmbiguousAngleOperator(kind) ||
                        (role != parser::PunctuationRole::BinaryOperator &&
                            role != parser::PunctuationRole::ComparisonOperator &&
                            role != parser::PunctuationRole::ShiftOperator))
                    {
                        continue;
                    }
                    PlaceOperatorBreak(tree, i, style.BinaryOperatorPlacement ==
                        BinaryOperatorPlacement::Leading, edits);
                }
            }
        } else if (style.TernaryOperatorPlacement !=
                TernaryOperatorPlacement::Preserve)
        {
            if (auto delimiters = ConditionalDelimiters(tree, *node)) {
                for (auto index : {delimiters->first, delimiters->second}) {
                    PlaceOperatorBreak(tree, index,
                        style.TernaryOperatorPlacement ==
                            TernaryOperatorPlacement::Leading, edits);
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
    size_t from = 0;
    for (const auto& edit : edits) {
        if (edit.Begin < from) {
            continue;
        }
        output.append(source.substr(from, edit.Begin - from));
        output += edit.Text;
        from = edit.End;
    }
    output.append(source.substr(from));
    return output;
}

// ThreeLines is a constraint on the chosen split, not a requirement to
// split every ternary. Once one delimiter is split, split the other too.
std::string CompleteTernaryBreaks(const parser::SyntaxTree& tree,
    const Style& style)
{
    const auto source = tree.Source();
    std::vector<Replacement> edits;
    std::vector<const parser::Node*> nodes{&tree.Root()};
    while (!nodes.empty()) {
        auto* node = nodes.back();
        nodes.pop_back();
        if (IsIncompleteStatement(*node)) {
            continue;
        }
        if (auto delimiters = ConditionalDelimiters(tree, *node)) {
            auto [question, colon] = *delimiters;
            auto tokenBegin = tree.Token(node->Tokens.Begin).Text.Begin;
            auto tokenEnd = tree.Token(node->Tokens.End - 1).Text.End;
            bool unsafe = false;
            for (auto i = node->Tokens.Begin; i < node->Tokens.End; ++i) {
                unsafe |= tree.Kind(i) == TK::Comment ||
                    tree.Kind(i) == TK::PreprocessingDirective;
            }
            if (!unsafe && SafeOperatorGap(tree, question) &&
                SafeOperatorGap(tree, colon) &&
                source.substr(tokenBegin, tokenEnd - tokenBegin).find('\r') ==
                    std::string_view::npos)
            {
                auto hasBreak = [&] (TokenIndex i) {
                    return tree.WhitespaceBefore(i).find('\n') !=
                            std::string_view::npos ||
                        tree.WhitespaceAfter(i).find('\n') !=
                            std::string_view::npos;
                };
                auto questionBreak = hasBreak(question);
                auto colonBreak = hasBreak(colon);
                const auto qPosition = tree.Token(question).Text.Begin;
                const auto cPosition = tree.Token(colon).Text.Begin;
                auto lineStart = source.rfind('\n', qPosition);
                lineStart = lineStart == std::string_view::npos
                    ? 0 : lineStart + 1;
                auto lineEnd = source.find('\n', cPosition);
                if (lineEnd == std::string_view::npos) {
                    lineEnd = source.size();
                }
                auto sameLine = source.substr(qPosition,
                    cPosition - qPosition).find('\n') ==
                        std::string_view::npos;
                if ((questionBreak || colonBreak ||
                    (sameLine && lineEnd - lineStart > style.ColumnLimit)) &&
                    source.substr(lineStart, lineEnd - lineStart)
                        .find('\\') == std::string_view::npos &&
                    (lineEnd == source.size() || lineEnd == 0 ||
                        source[lineEnd - 1] != '\r'))
                {
                    auto baseLine = source.rfind('\n', tokenBegin);
                    baseLine = baseLine == std::string_view::npos
                        ? 0 : baseLine + 1;
                    size_t leading = 0;
                    while (baseLine + leading < source.size() &&
                        source[baseLine + leading] == ' ')
                    {
                        ++leading;
                    }
                    auto continuation = "\n" + GeneratedIndent(
                        leading + style.IndentWidth, style);
                    const bool inheritedTrailing = questionBreak
                        ? tree.WhitespaceAfter(question).find('\n') !=
                              std::string_view::npos
                        : colonBreak &&
                            tree.WhitespaceAfter(colon).find('\n') !=
                              std::string_view::npos;
                    const bool trailing = style.TernaryOperatorPlacement ==
                        TernaryOperatorPlacement::Trailing ||
                        (style.TernaryOperatorPlacement ==
                            TernaryOperatorPlacement::Preserve &&
                            inheritedTrailing);
                    for (auto index : {question, colon}) {
                        if (hasBreak(index)) {
                            continue;
                        }
                        auto position = trailing
                            ? tree.Token(index).Text.End
                            : tree.Token(index).Text.Begin;
                        auto gap = trailing ? tree.WhitespaceAfter(index)
                            : tree.WhitespaceBefore(index);
                        edits.push_back({trailing ? position :
                            position - gap.size(), trailing ?
                            position + gap.size() : position, continuation});
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
    output.reserve(source.size() + edits.size() * style.IndentWidth);
    size_t from = 0;
    for (const auto& edit : edits) {
        if (edit.Begin < from) {
            continue;
        }
        output.append(source.substr(from, edit.Begin - from));
        output += edit.Text;
        from = edit.End;
    }
    output.append(source.substr(from));
    return output;
}

// A nested conditional's ':' belongs at the same depth as its '?', not at
// the indentation inherited from the next condition in the original chain.
std::string AlignLeadingTernaryColons(const parser::SyntaxTree& tree)
{
    const auto source = tree.Source();
    std::vector<Replacement> edits;
    std::vector<const parser::Node*> nodes{&tree.Root()};
    while (!nodes.empty()) {
        auto* node = nodes.back();
        nodes.pop_back();
        if (IsIncompleteStatement(*node)) continue;
        if (auto delimiters = ConditionalDelimiters(tree, *node)) {
            auto [question, colon] = *delimiters;
            auto span = source.substr(tree.Token(node->Tokens.Begin).Text.Begin,
                tree.Token(node->Tokens.End - 1).Text.End -
                    tree.Token(node->Tokens.Begin).Text.Begin);
            bool unsafe = span.find_first_of("\\\r") !=
                std::string_view::npos;
            for (auto i = node->Tokens.Begin; i < node->Tokens.End; ++i) {
                unsafe |= tree.Kind(i) == TK::Comment ||
                    tree.Kind(i) == TK::PreprocessingDirective;
            }
            if (!unsafe && SafeOperatorGap(tree, question) &&
                SafeOperatorGap(tree, colon))
            {
                auto questionAt = tree.Token(question).Text.Begin;
                auto colonAt = tree.Token(colon).Text.Begin;
                auto questionStart = source.rfind('\n', questionAt);
                questionStart = questionStart == std::string_view::npos
                    ? 0 : questionStart + 1;
                auto colonStart = source.rfind('\n', colonAt);
                colonStart = colonStart == std::string_view::npos
                    ? 0 : colonStart + 1;
                auto questionIndent = source.substr(questionStart,
                    questionAt - questionStart);
                auto colonIndent = source.substr(colonStart,
                    colonAt - colonStart);
                auto spacesOnly = [] (std::string_view indent) {
                    return std::ranges::all_of(indent, [] (char c) {
                        return c == ' ';
                    });
                };
                if (questionStart != colonStart &&
                    tree.WhitespaceBefore(question).find('\n') !=
                        std::string_view::npos &&
                    tree.WhitespaceBefore(colon).find('\n') !=
                        std::string_view::npos &&
                    spacesOnly(questionIndent) && spacesOnly(colonIndent) &&
                    questionIndent != colonIndent)
                {
                    edits.push_back({colonStart, colonAt,
                        std::string(questionIndent)});
                }
            }
        }
        for (auto* child = node->Child(); child; child = child->NextSibling) {
            nodes.push_back(child);
        }
    }
    if (edits.empty()) return std::string(source);
    std::ranges::sort(edits, {}, &Replacement::Begin);
    std::string output;
    output.reserve(source.size());
    size_t from = 0;
    for (const auto& edit : edits) {
        if (edit.Begin < from) continue;
        output.append(source.substr(from, edit.Begin - from));
        output += edit.Text;
        from = edit.End;
    }
    output.append(source.substr(from));
    return output;
}

std::string FormatExpressions(const parser::SyntaxTree& tree,
    const Style& style, bool correctExisting = true)
{
    if (style.ExpressionLayoutCondition == ExpressionLayoutCondition::Overflow &&
        correctExisting)
    {
        auto placed = PlaceExistingOperatorBreaks(tree, style);
        if (placed != tree.Source()) {
            auto length = placed.size();
            placed.append(util::SourcePaddingSize, '\0');
            auto parsed = parser::Parse(
                {.Data = placed.data(), .Size = length},
                [] (parser::ParseError) { });
            return FormatExpressions(parsed, style, false);
        }
    }
    if (style.ExpressionLayoutCondition == ExpressionLayoutCondition::Always ||
        style.ExpressionLayoutCondition == ExpressionLayoutCondition::FitOrOverflow)
    {
        const bool always = style.ExpressionLayoutCondition == ExpressionLayoutCondition::Always;
        std::vector<size_t> mergedPositions;
        std::vector<Replacement> mergedEdits;
        auto flattened = FlattenExpressionOperators(tree, always,
            style.TernarySplit == TernarySplit::Preserve,
            always ? nullptr : &mergedPositions,
            always ? nullptr : &mergedEdits);
        Style overflowStyle = style;
        overflowStyle.ExpressionLayoutCondition = ExpressionLayoutCondition::Overflow;
        if (flattened == tree.Source()) {
            return FormatExpressions(tree, overflowStyle);
        }
        if (!always) {
            size_t lineStart = 0;
            auto lineEnd = flattened.find('\n');
            std::string partial;
            size_t from = 0;
            for (size_t i = 0; i < mergedPositions.size(); ++i) {
                auto position = mergedPositions[i];
                while (lineEnd != std::string::npos && position >= lineEnd) {
                    lineStart = lineEnd + 1;
                    lineEnd = flattened.find('\n', lineStart);
                }
                auto end = lineEnd == std::string::npos
                    ? flattened.size() : lineEnd;
                if (end - lineStart <= style.ColumnLimit) {
                    const auto& edit = mergedEdits[i];
                    partial.append(tree.Source().substr(from,
                        edit.Begin - from));
                    partial += edit.Text;
                    from = edit.End;
                }
            }
            partial.append(tree.Source().substr(from));
            flattened = std::move(partial);
            if (flattened == tree.Source()) {
                return FormatExpressions(tree, overflowStyle);
            }
        }
        auto length = flattened.size();
        flattened.append(util::SourcePaddingSize, '\0');
        auto parsed = parser::Parse(
            {.Data = flattened.data(), .Size = length},
            [] (parser::ParseError) { });
        return FormatExpressions(parsed, overflowStyle);
    }
    auto source = tree.Source();
    std::vector<ExpressionBreak> breaks;
    std::vector<int> nesting(tree.TokenCount(), 0);
    int parens = 0;
    for (TokenIndex i = 0; i < tree.TokenCount(); ++i) {
        if (tree.Kind(i) == TK::Operator_CloseParenthesis && parens > 0) {
            --parens;
        }
        nesting[i] = parens;
        if (tree.Kind(i) == TK::Operator_OpenParenthesis) {
            ++parens;
        }
    }
    std::vector<const parser::Node*> nodes{&tree.Root()};
    while (!nodes.empty()) {
        auto* node = nodes.back();
        nodes.pop_back();
        if (IsIncompleteStatement(*node)) {
            continue;
        }
        if (node->Kind == NK::BinaryExpression ||
            node->Kind == NK::AssignmentExpression ||
            node->Kind == NK::ConditionalExpression)
        {
            const auto* left = node->Child();
            const auto* right = left ? left->NextSibling : nullptr;
            const auto* alternative = right ? right->NextSibling : nullptr;
            if (left && right && !left->Tokens.Empty() &&
                !right->Tokens.Empty() &&
                (node->Kind != NK::ConditionalExpression ||
                    (alternative && !alternative->Tokens.Empty())))
            {
                auto first = left->Tokens.End;
                auto last = right->Tokens.Begin;
                for (auto index = first; index < last; ++index) {
                    auto role = tree.Role(index);
                    bool selected = node->Kind == NK::ConditionalExpression
                        ? role == parser::PunctuationRole::ConditionalDelimiter
                        : node->Kind == NK::AssignmentExpression
                            ? role == parser::PunctuationRole::AssignmentOperator
                            : role == parser::PunctuationRole::BinaryOperator ||
                                role == parser::PunctuationRole::ComparisonOperator ||
                                role == parser::PunctuationRole::ShiftOperator;
                    if (!selected || index == 0 ||
                        (node->Kind == NK::ConditionalExpression &&
                            style.TernarySplit != TernarySplit::AsNeeded))
                    {
                        continue;
                    }
                    auto gap = tree.WhitespaceBefore(index);
                    if (!IsWhitespace(gap) ||
                        gap.find_first_of("\r\n") != std::string_view::npos)
                    {
                        continue;
                    }
                    // Avoid comma expressions and operators without a
                    // significant left-hand side on this physical line.
                    if (tree.Kind(index) == TK::Operator_Comma ||
                        IsAmbiguousAngleOperator(tree.Kind(index)))
                    {
                        continue;
                    }
                    const bool trailing = node->Kind == NK::BinaryExpression
                        ? style.BinaryOperatorPlacement ==
                            BinaryOperatorPlacement::Trailing
                        : node->Kind == NK::ConditionalExpression &&
                            style.TernaryOperatorPlacement ==
                                TernaryOperatorPlacement::Trailing;
                    if (trailing) {
                        gap = tree.WhitespaceAfter(index);
                        if (!IsWhitespace(gap) ||
                            gap.find_first_of("\r\n") !=
                                std::string_view::npos)
                        {
                            continue;
                        }
                    }
                    const auto offset = trailing
                        ? tree.Token(index).Text.End
                        : tree.Token(index).Text.Begin;
                    breaks.push_back({trailing ? offset : offset - gap.size(),
                        trailing ? offset + gap.size() : offset,
                        node->Kind == NK::AssignmentExpression ? 0 :
                        node->Kind == NK::ConditionalExpression ? 1 :
                        ExpressionPriority(tree.Kind(index)), nesting[index]});
                }
                if (node->Kind == NK::ConditionalExpression) {
                    for (auto index = right->Tokens.End;
                        index < alternative->Tokens.Begin; ++index)
                    {
                        if (tree.Kind(index) != TK::Operator_Colon ||
                            tree.Role(index) !=
                                parser::PunctuationRole::ConditionalDelimiter)
                        {
                            continue;
                        }
                        if (style.TernarySplit != TernarySplit::AsNeeded) {
                            continue;
                        }
                        const bool trailing = style.TernaryOperatorPlacement ==
                            TernaryOperatorPlacement::Trailing;
                        auto gap = trailing ? tree.WhitespaceAfter(index) :
                            tree.WhitespaceBefore(index);
                        if (IsWhitespace(gap) &&
                            gap.find_first_of("\r\n") ==
                                std::string_view::npos)
                        {
                            auto offset = trailing
                                ? tree.Token(index).Text.End
                                : tree.Token(index).Text.Begin;
                            breaks.push_back({trailing ? offset :
                                offset - gap.size(), trailing ?
                                offset + gap.size() : offset, 1,
                                nesting[index]});
                        }
                    }
                }
            }
        }
        for (auto* child = node->Child(); child; child = child->NextSibling) {
            nodes.push_back(child);
        }
    }
    if (breaks.empty()) {
        return std::string(source);
    }
    std::ranges::sort(breaks, {}, &ExpressionBreak::Begin);
    struct ConditionIndent {
        size_t Begin;
        size_t End;
        size_t Column;
    };
    std::vector<ConditionIndent> conditions;
    if (style.ControlConditionIndent == CallArgumentIndent::Continuation) {
        std::vector<const parser::Node*> headers{&tree.Root()};
        while (!headers.empty()) {
            auto* node = headers.back();
            headers.pop_back();
            if (IsIncompleteStatement(*node) || HasRecoveryChild(*node)) {
                continue;
            }
            if (node->Kind == NK::IfStatement ||
                node->Kind == NK::ForStatement ||
                node->Kind == NK::WhileStatement ||
                node->Kind == NK::DoWhileStatement ||
                node->Kind == NK::SwitchStatement ||
                node->Kind == NK::CatchClause)
            {
                for (auto* child = node->Child(); child;
                    child = child->NextSibling)
                {
                    if (child->Kind != NK::Condition &&
                        child->Kind != NK::ForHeader)
                    {
                        continue;
                    }
                    auto open = child->Tokens.Begin;
                    if (open >= tree.TokenCount() ||
                        tree.Kind(open) != TK::Operator_OpenParenthesis)
                    {
                        continue;
                    }
                    auto close = tree.MatchingDelimiter(open);
                    if (close == NoToken || close >= child->Tokens.End) {
                        continue;
                    }
                    auto offset = tree.Token(open).Text.Begin;
                    auto line = source.rfind('\n', offset);
                    line = line == std::string_view::npos ? 0 : line + 1;
                    if (source.substr(line, offset - line).find('\t') ==
                            std::string_view::npos)
                    {
                        conditions.push_back({tree.Token(open).Text.End,
                            tree.Token(close).Text.Begin, offset - line + 1});
                    }
                }
            }
            for (auto* child = node->Child(); child;
                child = child->NextSibling)
            {
                headers.push_back(child);
            }
        }
    }
    std::ranges::sort(conditions, [] (const auto& a, const auto& b) {
        return a.Begin == b.Begin ? a.End > b.End : a.Begin < b.Begin;
    });
    std::vector<std::optional<size_t>> conditionColumns(breaks.size());
    std::vector<const ConditionIndent*> active;
    size_t nextCondition = 0;
    for (size_t i = 0; i < breaks.size(); ++i) {
        auto offset = breaks[i].Begin;
        while (!active.empty() && active.back()->End <= offset) {
            active.pop_back();
        }
        while (nextCondition < conditions.size() &&
            conditions[nextCondition].Begin <= offset)
        {
            if (conditions[nextCondition].End > offset) {
                active.push_back(&conditions[nextCondition]);
            }
            ++nextCondition;
        }
        if (!active.empty()) {
            conditionColumns[i] = active.back()->Column;
        }
    }
    auto continuationColumn = [&](size_t position, size_t leading) {
        auto it = std::ranges::lower_bound(breaks, position, {},
            &ExpressionBreak::Begin);
        if (it != breaks.end() && it->Begin == position) {
            if (auto column = conditionColumns[it - breaks.begin()]) {
                return *column;
            }
        }
        return leading + style.IndentWidth;
    };
    std::vector<Replacement> edits;
    size_t lineStart = 0;
    size_t cursor = 0;
    TokenIndex tokenCursor = 0;
    while (lineStart < source.size()) {
        auto lineEnd = source.find('\n', lineStart);
        if (lineEnd == std::string_view::npos) {
            lineEnd = source.size();
        }
        while (cursor < breaks.size() && breaks[cursor].Begin < lineStart) {
            ++cursor;
        }
        auto stop = cursor;
        while (stop < breaks.size() && breaks[stop].End <= lineEnd) {
            ++stop;
        }
        size_t leading = 0;
        while (lineStart + leading < lineEnd &&
            source[lineStart + leading] == ' ')
        {
            ++leading;
        }
        bool safe = lineEnd == source.size() ||
            (lineEnd == 0 || source[lineEnd - 1] != '\r');
        safe &= source.substr(lineStart, lineEnd - lineStart).find('\\') ==
            std::string_view::npos;
        while (tokenCursor < tree.TokenCount() &&
            tree.Token(tokenCursor).Text.End <= lineStart)
        {
            ++tokenCursor;
        }
        for (auto index = tokenCursor;
            index < tree.TokenCount() &&
                tree.Token(index).Text.Begin < lineEnd; ++index)
        {
            const auto begin = tree.Token(index).Text.Begin;
            auto kind = tree.Kind(index);
            if (kind == TK::Comment || kind == TK::PreprocessingDirective ||
                source.substr(begin, tree.Token(index).Text.End - begin)
                    .find_first_of("\r\n") != std::string_view::npos)
            {
                safe = false;
                break;
            }
        }
        if (safe && lineStart + leading < lineEnd &&
            source[lineStart + leading] != '#')
        {
            if (style.ExpressionLayoutAlgorithm ==
                    ExpressionLayoutAlgorithm::DynamicProgramming &&
                lineEnd - lineStart > style.ColumnLimit)
            {
                auto selected = ChooseExpressionBreaks(source, breaks,
                    cursor, stop, lineStart, lineEnd,
                    cursor < stop ? continuationColumn(breaks[cursor].Begin,
                        leading) : leading + style.IndentWidth,
                    style.ColumnLimit);
                if (!selected.empty()) {
                    for (auto choice : selected) {
                        const auto& gap = breaks[choice];
                        edits.push_back({gap.Begin, gap.End,
                            "\n" + GeneratedIndent(
                                continuationColumn(gap.Begin, leading),
                                style)});
                    }
                    cursor = stop;
                    lineStart = lineEnd == source.size()
                        ? lineEnd : lineEnd + 1;
                    continue;
                }
            }
            size_t segment = lineStart;
            size_t indent = 0;
            while (indent + lineEnd - segment > style.ColumnLimit) {
                size_t choice = stop;
                for (auto i = cursor; i < stop; ++i) {
                    const auto& candidate = breaks[i];
                    if (candidate.Begin <= segment) {
                        continue;
                    }
                    if (candidate.Begin - segment + indent >
                        style.ColumnLimit)
                    {
                        break; // Candidates are ordered by source offset.
                    }
                    if (source.substr(segment, candidate.Begin - segment)
                            .find_first_not_of(" \t") == std::string_view::npos)
                    {
                        continue;
                    }
                    if (choice == stop ||
                        candidate.Nesting < breaks[choice].Nesting ||
                        (candidate.Nesting == breaks[choice].Nesting &&
                            (candidate.Priority < breaks[choice].Priority ||
                                (candidate.Priority == breaks[choice].Priority &&
                                    candidate.Begin > breaks[choice].Begin))))
                    {
                        choice = i;
                    }
                }
                if (choice == stop) {
                    break;
                }
                const auto& selected = breaks[choice];
                const auto newIndent = continuationColumn(selected.Begin,
                    leading);
                edits.push_back({selected.Begin, selected.End,
                    "\n" + GeneratedIndent(newIndent, style)});
                segment = selected.End;
                indent = newIndent;
                cursor = choice + 1;
            }
        }
        lineStart = lineEnd == source.size() ? lineEnd : lineEnd + 1;
    }
    std::string result;
    result.reserve(source.size() + edits.size() * (style.IndentWidth + 1));
    size_t from = 0;
    for (const auto& edit : edits) {
        result.append(source.substr(from, edit.Begin - from));
        result += edit.Text;
        from = edit.End;
    }
    result.append(source.substr(from));
    return result;
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
    const parser::SyntaxTree& tree, NamespaceComments policy,
    size_t shortNamespaceLines)
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
                auto opening = tree.MatchingDelimiter(index);
                const bool shortNamespace = shortNamespaceLines != 0 &&
                    opening != NoToken &&
                    tree.Token(index).Position.Line -
                        tree.Token(opening).Position.Line <=
                        shortNamespaceLines + 1;
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
                    !shortNamespace &&
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

// Reindent statement starts, not arbitrary physical lines in expressions or
// comments. Preserve recovered blocks and lines containing directives.
std::string FormatBlockIndentation(const parser::SyntaxTree& tree,
    const Style& style)
{
    auto source = tree.Source();
    std::vector<Replacement> edits;
    // Conditional branches may have different syntactic owners. Keep their
    // existing indentation rather than inferring layout across #if arms.
    std::vector<bool> conditional(tree.TokenCount(), false);
    size_t depth = 0;
    for (TokenIndex i = 0; i < tree.TokenCount(); ++i) {
        if (tree.Kind(i) == TK::PreprocessingDirective) {
            auto text = tree.TokenText(i);
            auto hash = text.find('#');
            if (hash != std::string_view::npos) {
                auto word = text.substr(hash + 1);
                word.remove_prefix(std::min(word.find_first_not_of(" \t"),
                    word.size()));
                auto end = word.find_first_of(" \t\r\n");
                word = word.substr(0, end);
                if (word == "if" || word == "ifdef" || word == "ifndef")
                    ++depth;
                else if (word == "endif" && depth) --depth;
            }
        }
        conditional[i] = depth != 0;
    }
    auto mark = [&](TokenIndex index, std::string_view indent) {
        if (index >= tree.TokenCount() || conditional[index] ||
            tree.Kind(index) == TK::Comment ||
            tree.Kind(index) == TK::PreprocessingDirective)
        {
            return;
        }
        auto offset = tree.Token(index).Text.Begin;
        auto line = source.rfind('\n', offset);
        line = line == std::string_view::npos ? 0 : line + 1;
        auto prefix = source.substr(line, offset - line);
        if (prefix.find_first_not_of(" \t") == std::string_view::npos &&
            prefix != indent && prefix.find('\r') == std::string_view::npos)
        {
            edits.push_back({line, offset, std::string(indent)});
        }
    };
    auto originalIndent = [&](TokenIndex index) -> std::string {
        auto offset = tree.Token(index).Text.Begin;
        auto line = source.rfind('\n', offset);
        line = line == std::string_view::npos ? 0 : line + 1;
        auto prefix = source.substr(line, offset - line);
        return prefix.find_first_not_of(" \t") == std::string_view::npos
            ? std::string(prefix) : std::string{};
    };
    struct Task {
        const parser::Node* Node;
        std::optional<std::string> Base;
        NK Parent;
    };
    std::vector<Task> tasks{{&tree.Root(), std::nullopt, tree.Root().Kind}};
    while (!tasks.empty()) {
        auto task = std::move(tasks.back());
        tasks.pop_back();
        const auto& node = *task.Node;
        if (HasRecoveryChild(node) || IsIncompleteStatement(node)) {
            continue;
        }
        if ((node.Kind == NK::CompoundStatement ||
                node.Kind == NK::LambdaBody) &&
            !node.Tokens.Empty() &&
            tree.Kind(node.Tokens.Begin) == TK::Operator_OpenBrace)
        {
            auto opening = node.Tokens.Begin;
            auto closing = tree.MatchingDelimiter(opening);
            if (closing == NoToken || closing >= node.Tokens.End) {
                continue;
            }
            std::string base = task.Base.value_or(originalIndent(opening));
            std::string childIndent = base + GeneratedIndentAfter(base,
                style.IndentWidth, style);
            const bool switchBody = task.Parent == NK::SwitchStatement;
            if (style.IndentBlockBodies) {
                mark(closing, base);
            }
            std::vector<const parser::Node*> children;
            bool afterCase = false;
            for (auto* child = node.Child(); child;
                child = child->NextSibling)
            {
                if (child->Tokens.Empty()) continue;
                auto target = childIndent;
                if (switchBody && child->Kind == NK::CaseLabel) {
                    afterCase = true;
                    if (style.IndentCaseLabels == BodyIndentation::NoIndent)
                        target = base;
                    else if (style.IndentCaseLabels == BodyIndentation::Indent)
                        target = childIndent;
                } else if (switchBody && afterCase &&
                    style.IndentCaseLabels == BodyIndentation::Indent)
                {
                    target += GeneratedIndentAfter(target,
                        style.IndentWidth, style);
                }
                const bool preserveCase = switchBody &&
                    child->Kind == NK::CaseLabel &&
                    style.IndentCaseLabels == BodyIndentation::Preserve;
                if (!preserveCase &&
                    (style.IndentBlockBodies ||
                        (switchBody && child->Kind == NK::CaseLabel)) &&
                    !HasRecoveryChild(*child))
                {
                    mark(child->Tokens.Begin, target);
                }
                children.push_back(child);
            }
            for (auto it = children.rbegin(); it != children.rend(); ++it) {
                tasks.push_back({*it, childIndent, node.Kind});
            }
            continue;
        }
        std::vector<const parser::Node*> children;
        for (auto* child = node.Child(); child;
            child = child->NextSibling)
        {
            children.push_back(child);
        }
        for (auto it = children.rbegin(); it != children.rend(); ++it) {
            tasks.push_back({*it, task.Base, node.Kind});
        }
    }
    std::ranges::sort(edits, {}, &Replacement::Begin);
    std::string output;
    output.reserve(source.size());
    size_t cursor = 0;
    for (const auto& edit : edits) {
        if (edit.Begin < cursor) continue;
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
                    if (auto replacement = ReflowDelimitedList(tree, *child, style)) {
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

// Reflow only complete, whitespace-separated braced initializer elements.
// The caller reparses after outer lists move, then visits nested lists.
std::string FormatInitializers(const parser::SyntaxTree& tree,
    const Style& style)
{
    if (style.InitializerListSplit == FunctionParameterSplit::Preserve) {
        return std::string(tree.Source());
    }
    std::vector<Replacement> edits;
    std::vector<const parser::Node*> nodes{&tree.Root()};
    while (!nodes.empty()) {
        auto* node = nodes.back();
        nodes.pop_back();
        if (IsIncompleteStatement(*node) || HasRecoveryChild(*node)) {
            continue;
        }
        if (node->Kind == NK::InitializerList) {
            if (auto edit = ReflowDelimitedList(tree, *node, style)) {
                edits.push_back(std::move(*edit));
            }
        }
        for (auto* child = node->Child(); child; child = child->NextSibling) {
            nodes.push_back(child);
        }
    }
    std::ranges::sort(edits, {}, &Replacement::Begin);
    std::string output;
    auto source = tree.Source();
    output.reserve(source.size());
    size_t cursor = 0;
    for (const auto& edit : edits) {
        if (edit.Begin < cursor) continue;
        output.append(source.substr(cursor, edit.Begin - cursor));
        output += edit.Text;
        cursor = edit.End;
    }
    output.append(source.substr(cursor));
    return output;
}

// Reflow parsed calls only. The parser cannot resolve names, so leave
// ambiguous parenthesized casts source-owned. Lay out enclosing calls first
// so their comma breaks can resolve overflow before trying nested calls.
std::string FormatCalls(const parser::SyntaxTree& tree, const Style& style,
    size_t targetDepth, bool& deeperCalls)
{
    if (style.CallArgumentSplit == CallArgumentSplit::Preserve ||
        style.ExpressionLayoutCondition == ExpressionLayoutCondition::Preserve)
    {
        return std::string(tree.Source());
    }
    std::vector<Replacement> edits;
    std::vector<std::pair<const parser::Node*, size_t>> nodes{
        {&tree.Root(), 0}};
    while (!nodes.empty()) {
        auto [node, depth] = nodes.back();
        nodes.pop_back();
        if (IsIncompleteStatement(*node)) {
            continue;
        }
        if (node->Kind == NK::CallExpression && depth > targetDepth) {
            deeperCalls = true;
            continue;
        }
        if (node->Kind == NK::CallExpression && depth == targetDepth) {
            const auto* callee = node->Child();
            const auto* arguments = callee ? callee->NextSibling : nullptr;
            if (callee && !callee->Tokens.Empty() && arguments &&
                arguments->Kind == NK::ArgumentList &&
                !arguments->Tokens.Empty() &&
                callee->Kind != NK::UnparsedSyntax &&
                callee->Kind != NK::Expression)
            {
                const auto opening = arguments->Tokens.Begin;
                bool ambiguous = false;
                if (callee->Kind == NK::ParenthesizedExpression) {
                    const auto* inner = callee->Child();
                    ambiguous = !inner || inner->Tokens.Empty() ||
                        (inner->Kind != NK::LambdaExpression &&
                            inner->Kind != NK::CallExpression &&
                            (inner->Kind != NK::UnaryExpression ||
                                (tree.Kind(inner->Tokens.Begin) !=
                                    TK::Operator_Star &&
                                 tree.Kind(inner->Tokens.Begin) !=
                                    TK::Operator_Ampersand)));
                }
                if (!ambiguous && opening > 0 &&
                    callee->Tokens.End <= opening &&
                    tree.Kind(opening - 1) != TK::Keyword_requires)
                {
                    if (auto edit = ReflowDelimitedList(
                        tree, *arguments, style))
                    {
                        edits.push_back(std::move(*edit));
                    }
                }
            }
        }
        auto childDepth = depth + (node->Kind == NK::CallExpression ? 1 : 0);
        if (childDepth <= targetDepth + 1) {
            for (auto* child = node->Child(); child;
                child = child->NextSibling)
            {
                nodes.emplace_back(child, childDepth);
            }
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
    if (text.find('\n') != std::string_view::npos ||
        text.find('\\') != std::string_view::npos) {
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

// Apply local, syntax-owned whitespace rules after structural reflow. Never
// touch a newline, comment, directive, or a recovered statement's prefix.
static std::string FormatTokenSpacing(const parser::SyntaxTree& tree,
    const Style& style)
{
    using PR = parser::PunctuationRole;
    const auto count = tree.TokenCount();
    std::vector<TokenSpacing> gaps(count, TokenSpacing::Preserve);
    std::vector<bool> unsafe(count, false);
    std::vector<bool> loopSemicolons(count, false);
    std::vector<bool> conditionSemicolons(count, false);
    // Type positions and parser-confirmed template-ids are the only safe
    // contexts for changing template argument gaps.
    std::vector<std::pair<const parser::Node*, bool>> nodes{
        {&tree.Root(), false}};
    auto markColon = [&](TokenIndex colon, ColonSpacing spacing) {
        if (spacing == ColonSpacing::Preserve || colon == 0 ||
            colon + 1 >= count || tree.Kind(colon) != TK::Operator_Colon)
        {
            return;
        }
        gaps[colon] = spacing == ColonSpacing::Left ||
                spacing == ColonSpacing::LeftAndRight
            ? TokenSpacing::Space : TokenSpacing::NoSpace;
        gaps[colon + 1] = spacing == ColonSpacing::Right ||
                spacing == ColonSpacing::LeftAndRight
            ? TokenSpacing::Space : TokenSpacing::NoSpace;
    };
    auto markInside = [&](TokenIndex open, TokenIndex close,
        TokenSpacing spacing) {
        if (spacing == TokenSpacing::Preserve || close <= open + 1 ||
            close >= count ||
            tree.WhitespaceBefore(open + 1).find('\n') !=
                std::string_view::npos ||
            tree.WhitespaceBefore(close).find('\n') !=
                std::string_view::npos)
        {
            return;
        }
        gaps[open + 1] = spacing;
        gaps[close] = spacing;
    };
    auto markBodyInterior = [&](TokenIndex open, TokenIndex close,
        TokenSpacing spacing) {
        if (spacing == TokenSpacing::Preserve) {
            return;
        }
        if (close == open + 1) {
            gaps[close] = spacing;
        } else {
            markInside(open, close, spacing);
        }
    };
    auto markEmpty = [&](const parser::Node& list, TK opening,
        TK closing, PR role, TokenSpacing spacing) {
        const auto open = list.Tokens.Begin;
        if (spacing == TokenSpacing::Preserve || open >= count ||
            list.Tokens.End != open + 2 || open + 1 >= count ||
            tree.Kind(open) != opening ||
            tree.Kind(open + 1) != closing ||
            tree.Role(open) != role || tree.Role(open + 1) != role ||
            tree.MatchingDelimiter(open) != open + 1)
        {
            return;
        }
        gaps[open + 1] = spacing;
    };
    auto markKeywordParenthesis = [&](TokenIndex keyword, TokenIndex end,
        TK expected, TokenSpacing spacing) {
        if (spacing == TokenSpacing::Preserve || keyword + 1 >= end ||
            keyword + 1 >= count || tree.Kind(keyword) != expected ||
            tree.Kind(keyword + 1) != TK::Operator_OpenParenthesis)
        {
            return;
        }
        const auto close = tree.MatchingDelimiter(keyword + 1);
        if (close == NoToken || close >= end ||
            tree.Kind(close) != TK::Operator_CloseParenthesis)
        {
            return;
        }
        gaps[keyword + 1] = spacing;
    };
    while (!nodes.empty()) {
        auto [node, typedContext] = nodes.back();
        nodes.pop_back();
        if (IsIncompleteStatement(*node) ||
            (node != &tree.Root() && HasRecoveryChild(*node)) ||
            node->Kind == NK::AmbiguousStatement ||
            node->Kind == NK::UnparsedSyntax ||
            node->Kind == NK::UnexpectedToken ||
            node->Kind == NK::Error)
        {
            for (auto i = node->Tokens.Begin;
                i < node->Tokens.End && i < count; ++i)
            {
                unsafe[i] = true;
            }
            continue;
        }
        if (!HasRecoveryChild(*node)) {
            const auto begin = node->Tokens.Begin;
            const auto end = node->Tokens.End;
            if (begin < count && end <= count) {
                if (node->Kind == NK::ExceptionSpecification) {
                    markKeywordParenthesis(begin, end, TK::Keyword_noexcept,
                        style.NoexceptSpaceBeforeParentheses);
                    if (style.NoexceptSpaceBeforeSpecifier !=
                            TokenSpacing::Preserve &&
                        begin > 0 &&
                        tree.Kind(begin) == TK::Keyword_noexcept &&
                        ((tree.Kind(begin - 1) ==
                                TK::Operator_CloseParenthesis &&
                            tree.Role(begin - 1) ==
                                PR::DeclarationDelimiter) ||
                            tree.Kind(begin - 1) == TK::Keyword_const ||
                            tree.Kind(begin - 1) == TK::Keyword_volatile ||
                            tree.Kind(begin - 1) == TK::Operator_Ampersand ||
                            tree.Kind(begin - 1) == TK::Operator_And))
                    {
                        gaps[begin] = style.NoexceptSpaceBeforeSpecifier;
                    }
                } else if (node->Kind == NK::UnaryExpression) {
                    markKeywordParenthesis(begin, end, TK::Keyword_sizeof,
                        style.SizeofSpaceBeforeParentheses);
                    markKeywordParenthesis(begin, end, TK::Keyword_alignof,
                        style.AlignofSpaceBeforeParentheses);
                    if (node->FirstChild &&
                        node->FirstChild->Kind ==
                            NK::ParenthesizedExpression &&
                        node->FirstChild->Tokens.Begin == begin + 1)
                    {
                        markKeywordParenthesis(begin, end,
                            TK::Keyword_co_await,
                            style.CoAwaitSpaceBeforeParentheses);
                    }
                    if (style.SizeofPackSpaceBeforeParentheses !=
                            TokenSpacing::Preserve &&
                        begin + 4 < end &&
                        tree.Kind(begin) == TK::Keyword_sizeof &&
                        tree.Kind(begin + 1) == TK::Operator_Ellipsis &&
                        tree.Kind(begin + 2) == TK::Operator_OpenParenthesis &&
                        node->FirstChild &&
                        node->FirstChild->Kind == NK::CallExpression &&
                        node->FirstChild->Tokens.Begin == begin + 1 &&
                        node->FirstChild->Tokens.End == end &&
                        tree.MatchingDelimiter(begin + 2) == end - 1 &&
                        tree.Role(begin + 2) == PR::CallDelimiter)
                    {
                        gaps[begin + 2] =
                            style.SizeofPackSpaceBeforeParentheses;
                    }
                } else if (node->Kind == NK::CallExpression &&
                    node->FirstChild && node->FirstChild->NextSibling &&
                    node->FirstChild->Tokens.Begin == begin &&
                    node->FirstChild->Tokens.End == begin + 1 &&
                    node->FirstChild->NextSibling->Kind == NK::ArgumentList &&
                    node->FirstChild->NextSibling->Tokens.Begin == begin + 1)
                {
                    markKeywordParenthesis(begin, end, TK::Keyword_noexcept,
                        style.NoexceptSpaceBeforeParentheses);
                    markKeywordParenthesis(begin, end, TK::Keyword_typeid,
                        style.TypeidSpaceBeforeParentheses);
                } else if (node->Kind == NK::RequiresExpression ||
                    node->Kind == NK::RequiresClause)
                {
                    markKeywordParenthesis(begin, end, TK::Keyword_requires,
                        style.RequiresSpaceBeforeParentheses);
                } else if (node->Kind == NK::StaticAssertDeclaration) {
                    markKeywordParenthesis(begin, end,
                        TK::Keyword_static_assert,
                        style.StaticAssertSpaceBeforeParentheses);
                } else if (node->Kind == NK::CastExpression &&
                    style.NamedCastSpaceBeforeTypeArguments !=
                        TokenSpacing::Preserve &&
                    begin + 3 < end &&
                    (tree.Kind(begin) == TK::Keyword_static_cast ||
                        tree.Kind(begin) == TK::Keyword_const_cast ||
                        tree.Kind(begin) == TK::Keyword_reinterpret_cast ||
                        tree.Kind(begin) == TK::Keyword_dynamic_cast) &&
                    tree.Kind(begin + 1) == TK::Operator_Less &&
                    node->FirstChild && node->FirstChild->Kind == NK::TypeId &&
                    node->FirstChild->Tokens.Begin == begin + 2 &&
                    node->FirstChild->Tokens.End + 1 < end &&
                    tree.Kind(node->FirstChild->Tokens.End) ==
                        TK::Operator_Greater &&
                    tree.Kind(node->FirstChild->Tokens.End + 1) ==
                        TK::Operator_OpenParenthesis &&
                    tree.MatchingDelimiter(
                        node->FirstChild->Tokens.End + 1) < end)
                {
                    gaps[begin + 1] =
                        style.NamedCastSpaceBeforeTypeArguments;
                } else if (style.AlignasSpaceBeforeParentheses !=
                        TokenSpacing::Preserve &&
                    (node->Kind == NK::RecordDeclaration ||
                        node->Kind == NK::EnumDeclaration ||
                        node->Kind == NK::DeclarationSpecifiers))
                {
                    // Only inspect the declaration header, not members.
                    for (auto i = begin; i + 1 < end; ++i) {
                        if (tree.Kind(i) == TK::Operator_OpenBrace ||
                            tree.Kind(i) == TK::Operator_Semicolon)
                        {
                            break;
                        }
                        markKeywordParenthesis(i, end, TK::Keyword_alignas,
                            style.AlignasSpaceBeforeParentheses);
                    }
                }
                if (node->Kind == NK::CastExpression &&
                    style.NamedCastSpaceBeforeOperandParentheses !=
                        TokenSpacing::Preserve &&
                    (tree.Kind(begin) == TK::Keyword_static_cast ||
                        tree.Kind(begin) == TK::Keyword_const_cast ||
                        tree.Kind(begin) == TK::Keyword_reinterpret_cast ||
                        tree.Kind(begin) == TK::Keyword_dynamic_cast) &&
                    node->FirstChild &&
                    node->FirstChild->Kind == NK::TypeId &&
                    node->FirstChild->Tokens.End + 1 < end)
                {
                    const auto open = node->FirstChild->Tokens.End + 1;
                    if (tree.Kind(open - 1) == TK::Operator_Greater &&
                        tree.Kind(open) == TK::Operator_OpenParenthesis &&
                        tree.MatchingDelimiter(open) == end - 1)
                    {
                        gaps[open] =
                            style.NamedCastSpaceBeforeOperandParentheses;
                    }
                }
                if ((node->Kind == NK::ReturnStatement ||
                        node->Kind == NK::ThrowStatement) &&
                    node->FirstChild &&
                    node->FirstChild->Kind == NK::ParenthesizedExpression &&
                    node->FirstChild->Tokens.Begin == begin + 1)
                {
                    const auto open = begin + 1;
                    const auto close = tree.MatchingDelimiter(open);
                    if (tree.Kind(open) == TK::Operator_OpenParenthesis &&
                        close != NoToken &&
                        close + 1 == node->FirstChild->Tokens.End &&
                        tree.Role(open) == PR::GroupingDelimiter)
                    {
                        auto spacing = tree.Kind(begin) == TK::Keyword_return
                            ? style.ReturnSpaceBeforeParentheses
                            : tree.Kind(begin) == TK::Keyword_co_return
                                ? style.CoReturnSpaceBeforeParentheses
                                : tree.Kind(begin) == TK::Keyword_throw
                                    ? style.ThrowSpaceBeforeParentheses
                                    : TokenSpacing::Preserve;
                        gaps[open] = spacing;
                    }
                }
                if (node->Kind == NK::NewTypeId &&
                    (style.NewArraySpaceBeforeBrackets !=
                        TokenSpacing::Preserve ||
                        style.NewArrayInteriorSpacing !=
                            TokenSpacing::Preserve ||
                        style.EmptyNewArrayInteriorSpacing !=
                            TokenSpacing::Preserve))
                {
                    auto* child = node->Child();
                    for (auto i = begin; i < end; ++i) {
                        while (child && child->Tokens.End <= i) {
                            child = child->NextSibling;
                        }
                        if (child && child->Tokens.Begin == i &&
                            (child->Kind == NK::AttributeSpecifier ||
                                child->Kind == NK::TemplateArgumentList ||
                                child->Kind ==
                                    NK::ExtensionAttributeSpecifier))
                        {
                            i = child->Tokens.End - 1;
                            continue;
                        }
                        if (tree.Kind(i) == TK::Operator_OpenParenthesis ||
                            tree.Kind(i) == TK::Operator_OpenBrace)
                        {
                            const auto close = tree.MatchingDelimiter(i);
                            if (close == NoToken || close >= end) {
                                break;
                            }
                            i = close;
                            continue;
                        }
                        if (tree.Kind(i) != TK::Operator_OpenBracket ||
                            i == begin || i + 1 >= end)
                        {
                            continue;
                        }
                        const auto close = tree.MatchingDelimiter(i);
                        if (close == NoToken || close >= end ||
                            tree.Kind(close) != TK::Operator_CloseBracket)
                        {
                            break;
                        }
                        gaps[i] = style.NewArraySpaceBeforeBrackets;
                        if (close == i + 1) {
                            gaps[close] =
                                style.EmptyNewArrayInteriorSpacing;
                        } else {
                            markInside(i, close, style.NewArrayInteriorSpacing);
                        }
                        i = close;
                    }
                }
                if (node->Kind == NK::NewExpression &&
                    (style.NewInitializerSpaceBeforeParentheses !=
                        TokenSpacing::Preserve ||
                        style.NewInitializerSpaceBeforeBraces !=
                            TokenSpacing::Preserve ||
                        style.NewInitializerParenthesisInteriorSpacing !=
                            TokenSpacing::Preserve ||
                        style.NewInitializerBraceInteriorSpacing !=
                            TokenSpacing::Preserve ||
                        style.PlacementNewSpaceAfterArguments !=
                            TokenSpacing::Preserve))
                {
                    for (auto* child = node->Child(); child;
                        child = child->NextSibling)
                    {
                        auto* next = child->NextSibling;
                        if (!next || child->Tokens.End != next->Tokens.Begin ||
                            next->Tokens.Empty() ||
                            next->Tokens.End > count)
                        {
                            continue;
                        }
                        if (child->Kind == NK::NewPlacement &&
                            next->Kind == NK::NewTypeId &&
                            child->Tokens.End > child->Tokens.Begin + 1 &&
                            tree.Kind(child->Tokens.Begin) ==
                                TK::Operator_OpenParenthesis &&
                            tree.Kind(child->Tokens.End - 1) ==
                                TK::Operator_CloseParenthesis &&
                            tree.MatchingDelimiter(child->Tokens.Begin) ==
                                child->Tokens.End - 1)
                        {
                            gaps[next->Tokens.Begin] =
                                style.PlacementNewSpaceAfterArguments;
                        }
                        if (child->Kind != NK::NewTypeId ||
                            next->Kind != NK::NewInitializer)
                        {
                            continue;
                        }
                        const auto open = next->Tokens.Begin;
                        const auto close = next->Tokens.End - 1;
                        if (tree.MatchingDelimiter(open) != close) {
                            continue;
                        }
                        if (tree.Kind(open) == TK::Operator_OpenParenthesis &&
                            tree.Kind(close) ==
                                TK::Operator_CloseParenthesis &&
                            tree.Role(open) == PR::CallDelimiter)
                        {
                            gaps[open] =
                                style.NewInitializerSpaceBeforeParentheses;
                            const auto interior =
                                style.NewInitializerParenthesisInteriorSpacing;
                            if (close == open + 1) {
                                gaps[close] = interior;
                            } else {
                                markInside(open, close, interior);
                            }
                        } else if (tree.Kind(open) ==
                                TK::Operator_OpenBrace &&
                            tree.Kind(close) == TK::Operator_CloseBrace &&
                            tree.Role(open) == PR::InitializerDelimiter)
                        {
                            gaps[open] = style.NewInitializerSpaceBeforeBraces;
                            const auto interior =
                                style.NewInitializerBraceInteriorSpacing;
                            if (close == open + 1) {
                                gaps[close] = interior;
                            } else {
                                markInside(open, close, interior);
                            }
                        }
                    }
                }
                if (node->Kind == NK::DeleteExpression &&
                    (style.DeleteArraySpaceBeforeBrackets !=
                        TokenSpacing::Preserve ||
                        style.DeleteArraySpaceAfterBrackets !=
                            TokenSpacing::Preserve) &&
                    node->FirstChild && node->FirstChild->NextSibling)
                {
                    auto* marker = node->FirstChild;
                    const auto open = marker->Tokens.Begin;
                    if (marker->Kind == NK::DeleteArrayMarker && open > begin &&
                        marker->Tokens.End == open + 2 &&
                        marker->Tokens.End <= count &&
                        tree.Kind(open - 1) == TK::Keyword_delete &&
                        tree.Kind(open) == TK::Operator_OpenBracket &&
                        tree.Kind(open + 1) == TK::Operator_CloseBracket &&
                        tree.MatchingDelimiter(open) == open + 1)
                    {
                        gaps[open] = style.DeleteArraySpaceBeforeBrackets;
                        if (marker->Tokens.End < count &&
                            marker->Tokens.End ==
                                marker->NextSibling->Tokens.Begin)
                        {
                            gaps[marker->Tokens.End] =
                                style.DeleteArraySpaceAfterBrackets;
                        }
                    }
                }
                if (node->Kind == NK::NewPlacement &&
                    (style.PlacementNewSpaceBeforeArguments !=
                        TokenSpacing::Preserve ||
                        style.PlacementNewArgumentInteriorSpacing !=
                            TokenSpacing::Preserve) &&
                    begin > 0 && end > begin + 1 &&
                    tree.Kind(begin - 1) == TK::Keyword_new &&
                    tree.Kind(begin) == TK::Operator_OpenParenthesis &&
                    tree.Kind(end - 1) == TK::Operator_CloseParenthesis &&
                    tree.MatchingDelimiter(begin) == end - 1 &&
                    tree.Role(begin) == PR::CallDelimiter)
                {
                    gaps[begin] = style.PlacementNewSpaceBeforeArguments;
                    markInside(begin, end - 1,
                        style.PlacementNewArgumentInteriorSpacing);
                }
                if (node->Kind == NK::AttributeSpecifier &&
                    style.AttributeSpaceBeforeSpecifier !=
                        TokenSpacing::Preserve &&
                    begin > 0 && end > begin + 3 &&
                    tree.Kind(begin) == TK::Operator_OpenBracket &&
                    tree.Kind(end - 1) == TK::Operator_CloseBracket &&
                    tree.Role(begin) == PR::AttributeDelimiter &&
                    tree.MatchingDelimiter(begin) == end - 1 &&
                    (tree.Kind(begin - 1) == TK::Identifier ||
                        tree.Kind(begin - 1) ==
                            TK::Operator_CloseParenthesis ||
                        tree.Kind(begin - 1) == TK::Operator_CloseBracket ||
                        (tree.Kind(begin - 1) >= TK::Keyword_alignas &&
                            tree.Kind(begin - 1) <= TK::Keyword_while)))
                {
                    gaps[begin] = style.AttributeSpaceBeforeSpecifier;
                }
                if (node->Kind == NK::AttributeSpecifier &&
                    style.AttributeSpaceAfterSpecifier !=
                        TokenSpacing::Preserve &&
                    end < count && end > begin + 3 &&
                    tree.Kind(begin) == TK::Operator_OpenBracket &&
                    tree.Kind(end - 1) == TK::Operator_CloseBracket &&
                    tree.Role(begin) == PR::AttributeDelimiter &&
                    tree.Role(end - 1) == PR::AttributeDelimiter &&
                    tree.MatchingDelimiter(begin) == end - 1 &&
                    (tree.Kind(end) == TK::Identifier ||
                        (tree.Kind(end) >= TK::Keyword_alignas &&
                            tree.Kind(end) <= TK::Keyword_while)))
                {
                    gaps[end] = style.AttributeSpaceAfterSpecifier;
                }
                if (style.DecltypeSpaceBeforeParentheses !=
                        TokenSpacing::Preserve &&
                    (node->Kind == NK::UsingDeclaration ||
                        node->Kind == NK::TrailingReturnType ||
                        node->Kind == NK::DeclarationSpecifiers ||
                        node->Kind == NK::TypeId ||
                        node->Kind == NK::NewTypeId))
                {
                    for (auto i = begin; i + 1 < end; ++i) {
                        markKeywordParenthesis(i, end, TK::Keyword_decltype,
                            style.DecltypeSpaceBeforeParentheses);
                    }
                }
            }
            if (node->Kind == NK::LambdaCaptureList) {
                markEmpty(*node, TK::Operator_OpenBracket,
                    TK::Operator_CloseBracket, PR::LambdaDelimiter,
                    style.EmptyLambdaCaptureSpacing);
            }
            if (node->Kind == NK::LambdaExpression ||
                node->Kind == NK::FunctionDeclarator ||
                node->Kind == NK::CallExpression)
            {
                const auto role = node->Kind == NK::CallExpression
                    ? PR::CallDelimiter : PR::DeclarationDelimiter;
                const auto spacing = node->Kind == NK::LambdaExpression
                    ? style.EmptyLambdaParametersSpacing
                    : node->Kind == NK::FunctionDeclarator
                        ? style.EmptyFunctionParametersSpacing
                        : style.EmptyFunctionCallArgumentsSpacing;
                for (auto* child = node->Child(); child;
                    child = child->NextSibling)
                {
                    if (child->Kind == (node->Kind == NK::CallExpression
                            ? NK::ArgumentList : NK::ParameterList) &&
                        !child->Tokens.Empty())
                    {
                        markEmpty(*child, TK::Operator_OpenParenthesis,
                            TK::Operator_CloseParenthesis, role, spacing);
                    }
                }
            }
            if (node->Kind == NK::TrailingReturnType &&
                style.ReturnArrowSpacing != ReturnArrowSpacing::Preserve &&
                node->Tokens.Begin > 0 && node->Tokens.Begin + 1 < count &&
                node->Tokens.End > node->Tokens.Begin + 1 &&
                tree.Kind(node->Tokens.Begin) ==
                    TK::Operator_IndirectClassMemberAccess)
            {
                const auto arrow = node->Tokens.Begin;
                auto policy = style.ReturnArrowSpacing;
                gaps[arrow] = policy == ReturnArrowSpacing::Left ||
                        policy == ReturnArrowSpacing::LeftAndRight
                    ? TokenSpacing::Space : TokenSpacing::NoSpace;
                gaps[arrow + 1] = policy == ReturnArrowSpacing::Right ||
                        policy == ReturnArrowSpacing::LeftAndRight
                    ? TokenSpacing::Space : TokenSpacing::NoSpace;
            }
            if (node->Kind == NK::FunctionQualifier &&
                node->Tokens.Begin > 0 && node->Tokens.Begin < count)
            {
                const auto at = node->Tokens.Begin;
                const auto kind = tree.Kind(at);
                const auto previous = tree.Kind(at - 1);
                if ((kind == TK::Keyword_const ||
                        kind == TK::Keyword_volatile) &&
                    previous == TK::Operator_CloseParenthesis &&
                    tree.Role(at - 1) == PR::DeclarationDelimiter)
                {
                    gaps[at] = style.FunctionQualifierSpacing;
                } else if ((kind == TK::Operator_Ampersand ||
                               kind == TK::Operator_And) &&
                    ((previous == TK::Operator_CloseParenthesis &&
                         tree.Role(at - 1) == PR::DeclarationDelimiter) ||
                        previous == TK::Keyword_const ||
                        previous == TK::Keyword_volatile))
                {
                    gaps[at] = style.FunctionRefQualifierSpacing;
                }
                if (style.FunctionSuffixSpecifierSpacing !=
                        TokenSpacing::Preserve &&
                    ((kind == TK::Identifier &&
                        (tree.TokenText(at) == "override" ||
                            tree.TokenText(at) == "final")) ||
                        kind == TK::Keyword_mutable ||
                        kind == TK::Keyword_constexpr ||
                        kind == TK::Keyword_consteval ||
                        kind == TK::Keyword_static) &&
                    (previous == TK::Operator_CloseParenthesis ||
                        previous == TK::Operator_CloseBracket ||
                        previous == TK::Keyword_const ||
                        previous == TK::Keyword_volatile ||
                        previous == TK::Keyword_noexcept ||
                        previous == TK::Keyword_mutable ||
                        previous == TK::Operator_Ampersand ||
                        previous == TK::Operator_And))
                {
                    gaps[at] = style.FunctionSuffixSpecifierSpacing;
                }
            }
            if (node->Kind == NK::NestedDeclarator &&
                (style.NestedDeclaratorInteriorSpacing !=
                    TokenSpacing::Preserve ||
                    style.NestedDeclaratorPointerSpacing !=
                        TokenSpacing::Preserve) &&
                node->Tokens.Begin < count && node->Tokens.End <= count &&
                node->Tokens.End > node->Tokens.Begin + 2)
            {
                const auto open = node->Tokens.Begin;
                const auto close = node->Tokens.End - 1;
                if (tree.Kind(open) == TK::Operator_OpenParenthesis &&
                    tree.Kind(close) == TK::Operator_CloseParenthesis &&
                    tree.Role(open) == PR::DeclarationDelimiter &&
                    tree.Role(close) == PR::DeclarationDelimiter &&
                    tree.MatchingDelimiter(open) == close)
                {
                    markInside(open, close,
                        style.NestedDeclaratorInteriorSpacing);
                    if (style.NestedDeclaratorPointerSpacing !=
                        TokenSpacing::Preserve)
                    {
                        for (auto* child = node->Child(); child;
                            child = child->NextSibling)
                        {
                            const auto next = child->Tokens.End;
                            if (child->Kind == NK::PointerOperator &&
                                next > open + 1 && next < close &&
                                tree.Kind(next) == TK::Identifier &&
                                (tree.Kind(next - 1) == TK::Operator_Star ||
                                    tree.Kind(next - 1) ==
                                        TK::Operator_Ampersand ||
                                    tree.Kind(next - 1) == TK::Operator_And))
                            {
                                gaps[next] =
                                    style.NestedDeclaratorPointerSpacing;
                            }
                        }
                    }
                }
            }
            if (node->Kind == NK::MemberPointerOperator &&
                style.MemberPointerDeclaratorSpacing !=
                    TokenSpacing::Preserve &&
                node->Tokens.End > node->Tokens.Begin + 1 &&
                node->Tokens.End < count &&
                tree.Kind(node->Tokens.End - 1) == TK::Operator_Star &&
                tree.Kind(node->Tokens.End - 2) ==
                    TK::Operator_ScopeResolution &&
                tree.Kind(node->Tokens.End) == TK::Identifier)
            {
                gaps[node->Tokens.End] =
                    style.MemberPointerDeclaratorSpacing;
            }
            if (node->Kind == NK::TemplateDeclaration &&
                style.TemplateKeywordSpacing != TokenSpacing::Preserve &&
                node->Tokens.Begin < count &&
                tree.Kind(node->Tokens.Begin) == TK::Keyword_template)
            {
                for (auto* child = node->Child(); child;
                    child = child->NextSibling)
                {
                    if (child->Kind == NK::TemplateParameterList &&
                        child->Tokens.Begin == node->Tokens.Begin + 1 &&
                        child->Tokens.End <= count &&
                        child->Tokens.End > child->Tokens.Begin + 1 &&
                        tree.Kind(child->Tokens.Begin) == TK::Operator_Less &&
                        tree.Kind(child->Tokens.End - 1) ==
                            TK::Operator_Greater)
                    {
                        gaps[child->Tokens.Begin] =
                            style.TemplateKeywordSpacing;
                    }
                }
            }
            if ((node->Kind == NK::TemplateParameterList ||
                    (node->Kind == NK::TemplateArgumentList && typedContext)) &&
                (style.TemplateTypeArgumentInteriorSpacing !=
                    TokenSpacing::Preserve ||
                    style.TemplateTypeArgumentSpacing !=
                        TokenSpacing::Preserve) &&
                node->Tokens.Begin < count && node->Tokens.End <= count &&
                node->Tokens.End >= node->Tokens.Begin + 2 &&
                tree.Kind(node->Tokens.Begin) == TK::Operator_Less &&
                tree.Kind(node->Tokens.End - 1) == TK::Operator_Greater &&
                tree.Role(node->Tokens.Begin) == PR::TemplateDelimiter &&
                tree.Role(node->Tokens.End - 1) == PR::TemplateDelimiter)
            {
                const auto open = node->Tokens.Begin;
                if (node->Kind == NK::TemplateArgumentList && open > 0 &&
                    tree.Kind(open - 1) == TK::Identifier)
                {
                    gaps[open] = style.TemplateTypeArgumentSpacing;
                }
                markInside(open, node->Tokens.End - 1,
                    style.TemplateTypeArgumentInteriorSpacing);
            }
            if (node->Kind == NK::BracedInitializer &&
                (style.InitializerBracesSpacing != TokenSpacing::Preserve ||
                    style.BraceInitializerSpacing != TokenSpacing::Preserve ||
                    style.EmptyBraceInitializerInteriorSpacing !=
                        TokenSpacing::Preserve) &&
                node->Tokens.Begin < count && node->Tokens.End <= count &&
                node->Tokens.End >= node->Tokens.Begin + 2 &&
                tree.Kind(node->Tokens.Begin) == TK::Operator_OpenBrace &&
                tree.Kind(node->Tokens.End - 1) == TK::Operator_CloseBrace &&
                tree.Role(node->Tokens.Begin) == PR::InitializerDelimiter &&
                tree.Role(node->Tokens.End - 1) ==
                    PR::InitializerDelimiter &&
                tree.MatchingDelimiter(node->Tokens.Begin) ==
                    node->Tokens.End - 1 &&
                (!node->FirstChild || !HasRecoveryChild(*node->FirstChild)))
            {
                const auto open = node->Tokens.Begin;
                if (style.BraceInitializerSpacing != TokenSpacing::Preserve &&
                    open > 0 && tree.Kind(open - 1) != TK::Operator_Assign)
                {
                    gaps[open] = style.BraceInitializerSpacing;
                }
                if (node->Tokens.End == open + 2) {
                    gaps[node->Tokens.End - 1] =
                        style.EmptyBraceInitializerInteriorSpacing;
                } else {
                    markInside(open, node->Tokens.End - 1,
                        style.InitializerBracesSpacing);
                }
            }
            if (node->Kind == NK::OperatorFunctionName &&
                style.OperatorOverloadOperatorSpacing !=
                    OperatorOverloadOperatorSpacing::Preserve &&
                node->Tokens.Begin + 1 < node->Tokens.End &&
                node->Tokens.End < count &&
                tree.Kind(node->Tokens.Begin) == TK::Keyword_operator &&
                tree.Kind(node->Tokens.End) == TK::Operator_OpenParenthesis)
            {
                const auto first = node->Tokens.Begin + 1;
                const auto kind = tree.Kind(first);
                if (kind == TK::Operator_OpenParenthesis ||
                    kind == TK::Operator_OpenBracket ||
                    (kind >= TK::Operator_Complement &&
                        kind <= TK::Operator_Comma))
                {
                    auto placement = style.OperatorOverloadOperatorSpacing;
                    gaps[first] = placement ==
                            OperatorOverloadOperatorSpacing::Left ||
                            placement ==
                                OperatorOverloadOperatorSpacing::LeftAndRight
                        ? TokenSpacing::Space : TokenSpacing::NoSpace;
                    gaps[node->Tokens.End] = placement ==
                            OperatorOverloadOperatorSpacing::Right ||
                            placement ==
                                OperatorOverloadOperatorSpacing::LeftAndRight
                        ? TokenSpacing::Space : TokenSpacing::NoSpace;
                }
            }
            if (node->Kind == NK::AttributeSpecifier &&
                (style.AttributeInteriorSpacing != TokenSpacing::Preserve ||
                    style.EmptyAttributeInteriorSpacing !=
                        TokenSpacing::Preserve) &&
                node->Tokens.Begin + 4 <= node->Tokens.End &&
                node->Tokens.End <= count)
            {
                const auto open = node->Tokens.Begin;
                const auto close = node->Tokens.End - 1;
                if (tree.Kind(open) == TK::Operator_OpenBracket &&
                    tree.Kind(open + 1) == TK::Operator_OpenBracket &&
                    tree.Kind(close - 1) == TK::Operator_CloseBracket &&
                    tree.Kind(close) == TK::Operator_CloseBracket &&
                    tree.Role(open) == PR::AttributeDelimiter &&
                    tree.Role(close) == PR::AttributeDelimiter &&
                    tree.MatchingDelimiter(open) == close &&
                    tree.MatchingDelimiter(open + 1) == close - 1)
                {
                    if (node->Tokens.End == open + 4) {
                        gaps[open + 2] =
                            style.EmptyAttributeInteriorSpacing;
                    } else {
                        markInside(open + 1, close - 1,
                            style.AttributeInteriorSpacing);
                    }
                }
            }
            if ((node->Kind == NK::ParenthesizedExpression &&
                    style.GroupingInteriorSpacing !=
                        TokenSpacing::Preserve) ||
                ((node->Kind == NK::Condition ||
                    node->Kind == NK::ForHeader) &&
                    style.ControlBlockConditionSpacing !=
                        TokenSpacing::Preserve))
            {
                const auto open = node->Tokens.Begin;
                const auto close = node->Tokens.End == 0
                    ? NoToken : node->Tokens.End - 1;
                const auto role = node->Kind == NK::ParenthesizedExpression
                    ? PR::GroupingDelimiter : PR::ConditionDelimiter;
                if (open < count && close < count &&
                    tree.Kind(open) == TK::Operator_OpenParenthesis &&
                    tree.Kind(close) == TK::Operator_CloseParenthesis &&
                    tree.Role(open) == role && tree.Role(close) == role &&
                    tree.MatchingDelimiter(open) == close)
                {
                    markInside(open, close,
                        node->Kind == NK::ParenthesizedExpression
                            ? style.GroupingInteriorSpacing
                            : style.ControlBlockConditionSpacing);
                }
            }
            if (node->Kind == NK::AccessSpecifier &&
                style.AccessModifierColonSpacing != TokenSpacing::Preserve &&
                node->Tokens.End <= count && !node->Tokens.Empty() &&
                tree.Role(node->Tokens.End - 1) == PR::LabelDelimiter)
            {
                markColon(node->Tokens.End - 1,
                    style.AccessModifierColonSpacing == TokenSpacing::Space
                        ? ColonSpacing::LeftAndRight : ColonSpacing::NoSpace);
            }
            if (node->Kind == NK::RangeForSeparator &&
                style.RangeBasedForColonSpacing != TokenSpacing::Preserve &&
                node->Tokens.Begin < count &&
                tree.Role(node->Tokens.Begin) == PR::ConditionDelimiter)
            {
                markColon(node->Tokens.Begin,
                    style.RangeBasedForColonSpacing == TokenSpacing::Space
                        ? ColonSpacing::LeftAndRight : ColonSpacing::NoSpace);
            }
            if ((node->Kind == NK::RecordDeclaration ||
                    node->Kind == NK::NamespaceDeclaration) &&
                node->Tokens.Begin < count && node->Tokens.End <= count &&
                (node->Kind == NK::RecordDeclaration
                    ? style.RecordBodyInteriorSpacing
                    : style.NamespaceBodyInteriorSpacing) !=
                    TokenSpacing::Preserve)
            {
                for (auto open = node->Tokens.Begin + 1;
                    open < node->Tokens.End; ++open)
                {
                    if (tree.Kind(open) != TK::Operator_OpenBrace ||
                        (node->Kind == NK::RecordDeclaration &&
                            tree.Role(open) != PR::RecordDelimiter))
                    {
                        continue;
                    }
                    const auto close = tree.MatchingDelimiter(open);
                    if (close == NoToken || close >= node->Tokens.End ||
                        (node->Kind == NK::NamespaceDeclaration &&
                            close != node->Tokens.End - 1))
                    {
                        continue;
                    }
                    markBodyInterior(open, close,
                        node->Kind == NK::RecordDeclaration
                            ? style.RecordBodyInteriorSpacing
                            : style.NamespaceBodyInteriorSpacing);
                    break;
                }
            }
            if (node->Kind == NK::EnumDeclaration &&
                style.EnumBodyInteriorSpacing != TokenSpacing::Preserve)
            {
                for (auto* child = node->Child(); child;
                    child = child->NextSibling)
                {
                    if (child->Kind != NK::EnumeratorList ||
                        child->Tokens.Empty() ||
                        child->Tokens.End > count)
                    {
                        continue;
                    }
                    const auto open = child->Tokens.Begin;
                    const auto close = child->Tokens.End - 1;
                    if (tree.Kind(open) == TK::Operator_OpenBrace &&
                        tree.Kind(close) == TK::Operator_CloseBrace &&
                        tree.MatchingDelimiter(open) == close)
                    {
                        markBodyInterior(open, close,
                            style.EnumBodyInteriorSpacing);
                    }
                }
            }
            if (node->Kind == NK::RequirementBody &&
                style.RequirementBodyInteriorSpacing !=
                    TokenSpacing::Preserve &&
                !node->Tokens.Empty() && node->Tokens.End <= count)
            {
                const auto open = node->Tokens.Begin;
                const auto close = node->Tokens.End - 1;
                if (tree.Kind(open) == TK::Operator_OpenBrace &&
                    tree.Kind(close) == TK::Operator_CloseBrace &&
                    tree.Role(open) == PR::BlockDelimiter &&
                    tree.MatchingDelimiter(open) == close)
                {
                    markBodyInterior(open, close,
                        style.RequirementBodyInteriorSpacing);
                }
            }
            if ((node->Kind == NK::CompoundStatement ||
                    node->Kind == NK::LambdaBody) &&
                (style.BlockInteriorSpacing != TokenSpacing::Preserve ||
                    style.EmptyBlockInteriorSpacing !=
                        TokenSpacing::Preserve) &&
                node->Tokens.Begin < count && node->Tokens.End <= count &&
                node->Tokens.End >= node->Tokens.Begin + 2)
            {
                const auto open = node->Tokens.Begin;
                const auto close = node->Tokens.End - 1;
                const auto role = node->Kind == NK::LambdaBody
                    ? PR::LambdaDelimiter : PR::BlockDelimiter;
                if (tree.Kind(open) == TK::Operator_OpenBrace &&
                    tree.Kind(close) == TK::Operator_CloseBrace &&
                    tree.Role(open) == role && tree.Role(close) == role &&
                    tree.MatchingDelimiter(open) == close)
                {
                    if (close == open + 1) {
                        gaps[close] = style.EmptyBlockInteriorSpacing;
                    } else {
                        markInside(open, close, style.BlockInteriorSpacing);
                    }
                }
            }
            if ((node->Kind == NK::IfStatement ||
                    node->Kind == NK::ForStatement ||
                    node->Kind == NK::WhileStatement ||
                    node->Kind == NK::DoWhileStatement ||
                    node->Kind == NK::SwitchStatement ||
                    node->Kind == NK::TryStatement ||
                    node->Kind == NK::CatchClause) &&
                style.ControlBlockSpaceBeforeBrace !=
                    TokenSpacing::Preserve)
            {
                for (auto* child = node->Child(); child;
                    child = child->NextSibling)
                {
                    auto open = child->Tokens.Begin;
                    if (child->Kind == NK::CompoundStatement &&
                        !child->Tokens.Empty() && open > 0 && open < count &&
                        tree.Kind(open) == TK::Operator_OpenBrace &&
                        tree.Role(open) == PR::BlockDelimiter &&
                        tree.MatchingDelimiter(open) != NoToken)
                    {
                        gaps[open] = style.ControlBlockSpaceBeforeBrace;
                    }
                }
            }
            if (node->Kind == NK::BitField &&
                node->Tokens.Begin < count &&
                tree.Role(node->Tokens.Begin) == PR::BitFieldDelimiter)
            {
                markColon(node->Tokens.Begin, style.BitFieldColonSpacing);
            }
            if ((node->Kind == NK::CaseLabel ||
                    node->Kind == NK::LabelStatement) &&
                !node->Tokens.Empty() && node->Tokens.End <= count &&
                tree.Role(node->Tokens.End - 1) == PR::LabelDelimiter)
            {
                markColon(node->Tokens.End - 1, style.LabelColonSpacing);
            }
            if (node->Kind == NK::EnumDeclaration &&
                style.EnumUnderlyingTypeColonSpacing !=
                    ColonSpacing::Preserve)
            {
                for (auto i = node->Tokens.Begin;
                    i < node->Tokens.End && i < count; ++i)
                {
                    const auto kind = tree.Kind(i);
                    if (kind == TK::Operator_OpenBrace ||
                        kind == TK::Operator_Semicolon)
                    {
                        break;
                    }
                    if (auto close = tree.MatchingDelimiter(i);
                        close != NoToken && close > i &&
                        close < node->Tokens.End)
                    {
                        i = close;
                    } else if (kind == TK::Operator_Colon &&
                        i > node->Tokens.Begin + 1 &&
                        i + 1 < node->Tokens.End)
                    {
                        markColon(i, style.EnumUnderlyingTypeColonSpacing);
                        break;
                    }
                }
            }
            if (node->Kind == NK::MemberExpression &&
                style.MemberAccessOperatorsSpacing ==
                    TightOperatorSpacing::NoSpace &&
                node->FirstChild && node->FirstChild->NextSibling)
            {
                auto begin = node->FirstChild->Tokens.End;
                auto end = node->FirstChild->NextSibling->Tokens.Begin;
                for (auto i = begin; i < end && i + 1 < count; ++i) {
                    if (tree.Kind(i) == TK::Operator_ClassMemberAccess ||
                        tree.Kind(i) ==
                            TK::Operator_IndirectClassMemberAccess ||
                        tree.Kind(i) == TK::Operator_ScopeResolution)
                    {
                        gaps[i] = TokenSpacing::NoSpace;
                        gaps[i + 1] = TokenSpacing::NoSpace;
                        break;
                    }
                }
            }
            if (style.StatementSemicolonSpacing ==
                    TightOperatorSpacing::NoSpace &&
                !node->Tokens.Empty() && node->Tokens.End <= count &&
                tree.Kind(node->Tokens.End - 1) == TK::Operator_Semicolon &&
                (node->Kind == NK::DeclarationStatement ||
                    node->Kind == NK::ExpressionStatement ||
                    node->Kind == NK::ReturnStatement ||
                    node->Kind == NK::CoYieldStatement ||
                    node->Kind == NK::ThrowStatement ||
                    node->Kind == NK::BreakStatement ||
                    node->Kind == NK::ContinueStatement ||
                    node->Kind == NK::GotoStatement ||
                    node->Kind == NK::DoWhileStatement))
            {
                gaps[node->Tokens.End - 1] = TokenSpacing::NoSpace;
            }
            if (style.DeclarationSemicolonSpacing !=
                    TokenSpacing::Preserve &&
                !node->Tokens.Empty() && node->Tokens.End <= count &&
                tree.Kind(node->Tokens.End - 1) == TK::Operator_Semicolon &&
                (node->Kind == NK::DeclarationStatement ||
                    node->Kind == NK::Declaration ||
                    node->Kind == NK::FunctionDeclaration ||
                    node->Kind == NK::RecordDeclaration ||
                    node->Kind == NK::EnumDeclaration ||
                    node->Kind == NK::TemplateDeclaration ||
                    node->Kind == NK::UsingDeclaration ||
                    node->Kind == NK::TypedefDeclaration ||
                    node->Kind == NK::StaticAssertDeclaration ||
                    node->Kind == NK::ConceptDeclaration ||
                    node->Kind == NK::ModuleDeclaration ||
                    node->Kind == NK::ImportDeclaration))
            {
                gaps[node->Tokens.End - 1] =
                    style.DeclarationSemicolonSpacing;
            }
            if (node->Kind == NK::UnaryExpression && node->FirstChild) {
                auto* operand = node->FirstChild;
                if (style.UnaryOperatorSpacing ==
                        TightOperatorSpacing::NoSpace &&
                    node->Tokens.Begin < operand->Tokens.Begin &&
                    operand->Tokens.Begin < count &&
                    tree.Role(node->Tokens.Begin) == PR::UnaryOperator &&
                    tree.Kind(node->Tokens.Begin) != TK::Keyword_sizeof &&
                    tree.Kind(node->Tokens.Begin) != TK::Keyword_alignof &&
                    tree.Kind(node->Tokens.Begin) != TK::Keyword_co_await)
                {
                    gaps[operand->Tokens.Begin] = TokenSpacing::NoSpace;
                }
                if (style.PostfixOperatorSpacing ==
                        TightOperatorSpacing::NoSpace &&
                    operand->Tokens.End < node->Tokens.End &&
                    operand->Tokens.End < count &&
                    (tree.Kind(operand->Tokens.End) == TK::Operator_Increment ||
                        tree.Kind(operand->Tokens.End) ==
                            TK::Operator_Decrement) &&
                    tree.Role(operand->Tokens.End) == PR::UnaryOperator)
                {
                    gaps[operand->Tokens.End] = TokenSpacing::NoSpace;
                }
            }
            if (node->Kind == NK::ConditionalExpression &&
                style.TernaryOperatorSpacing ==
                    TernaryOperatorSpacing::Space &&
                node->FirstChild && node->FirstChild->NextSibling &&
                node->FirstChild->NextSibling->NextSibling)
            {
                const auto* condition = node->FirstChild;
                const auto* positive = condition->NextSibling;
                const auto* negative = positive->NextSibling;
                auto mark = [&](TokenIndex begin, TokenIndex end, TK kind) {
                    for (auto i = begin; i < end && i + 1 < count; ++i) {
                        if (tree.Kind(i) == kind &&
                            tree.Role(i) == PR::ConditionalDelimiter)
                        {
                            gaps[i] = TokenSpacing::Space;
                            gaps[i + 1] = TokenSpacing::Space;
                            break;
                        }
                    }
                };
                mark(condition->Tokens.End, positive->Tokens.Begin,
                    TK::Operator_Conditional);
                mark(positive->Tokens.End, negative->Tokens.Begin,
                    TK::Operator_Colon);
            }
            if (node->Kind == NK::IfStatement ||
                node->Kind == NK::SwitchStatement)
            {
                if (style.ConditionInitSemicolonSpacing !=
                    TokenSpacing::Preserve)
                {
                    for (auto* child = node->Child(); child;
                        child = child->NextSibling)
                    {
                        if (child->Kind != NK::Condition ||
                            child->Tokens.Empty() ||
                            tree.Kind(child->Tokens.Begin) !=
                                TK::Operator_OpenParenthesis)
                        {
                            continue;
                        }
                        auto open = child->Tokens.Begin;
                        auto close = tree.MatchingDelimiter(open);
                        if (close == NoToken ||
                            close + 1 != child->Tokens.End || close <= open + 2)
                        {
                            continue;
                        }
                        TokenIndex separator = NoToken;
                        for (auto i = open + 1; i < close; ++i) {
                            if (tree.Kind(i) == TK::Operator_Semicolon) {
                                if (separator != NoToken) {
                                    separator = NoToken;
                                    break;
                                }
                                separator = i;
                            } else if (auto match = tree.MatchingDelimiter(i);
                                match != NoToken && match > i && match < close)
                            {
                                i = match;
                            }
                        }
                        if (separator != NoToken && separator > open + 1 &&
                            separator + 1 < close)
                        {
                            conditionSemicolons[separator] = true;
                        }
                    }
                }
            }
            if (node->Kind == NK::ForHeader &&
                style.LoopSemicolonSpacing != TokenSpacing::Preserve)
            {
                for (auto* child = node->Child(); child;
                    child = child->NextSibling)
                {
                    if (child->Kind == NK::ListSeparator &&
                        child->Tokens.Begin < count &&
                        tree.Kind(child->Tokens.Begin) == TK::Operator_Semicolon)
                    {
                        loopSemicolons[child->Tokens.Begin] = true;
                    }
                }
            }
            if ((node->Kind == NK::SubscriptExpression &&
                    (style.SubscriptOperatorInteriorSpacing !=
                        TokenSpacing::Preserve ||
                        style.SubscriptOperatorSpacing !=
                            TokenSpacing::Preserve)) ||
                (node->Kind == NK::ArrayDeclarator &&
                    (style.ArrayDeclarationInteriorSpacing !=
                        TokenSpacing::Preserve ||
                        style.ArrayDeclarationSpacing !=
                            TokenSpacing::Preserve ||
                        style.EmptyArrayDeclarationInteriorSpacing !=
                            TokenSpacing::Preserve)) ||
                (node->Kind == NK::StructuredBinding &&
                    style.StructuredBindingBracketSpacing !=
                        TokenSpacing::Preserve))
            {
                for (auto i = node->Tokens.Begin;
                    i < node->Tokens.End && i < count; ++i)
                {
                    auto role = node->Kind == NK::SubscriptExpression
                        ? PR::CallDelimiter : PR::DeclarationDelimiter;
                    if (tree.Kind(i) != TK::Operator_OpenBracket ||
                        tree.Role(i) != role)
                    {
                        continue;
                    }
                    auto close = tree.MatchingDelimiter(i);
                    if (close != NoToken && close < node->Tokens.End) {
                        auto before = node->Kind == NK::SubscriptExpression
                            ? style.SubscriptOperatorSpacing
                            : node->Kind == NK::ArrayDeclarator
                                ? style.ArrayDeclarationSpacing
                                : TokenSpacing::Preserve;
                        if (i > 0 && before != TokenSpacing::Preserve) {
                            gaps[i] = before;
                        }
                        auto inside = node->Kind == NK::SubscriptExpression
                            ? style.SubscriptOperatorInteriorSpacing
                            : node->Kind == NK::ArrayDeclarator
                                ? style.ArrayDeclarationInteriorSpacing
                                : style.StructuredBindingBracketSpacing;
                        if (node->Kind == NK::ArrayDeclarator &&
                            close == i + 1)
                        {
                            gaps[close] =
                                style.EmptyArrayDeclarationInteriorSpacing;
                        } else {
                            markInside(i, close, inside);
                        }
                    }
                    break;
                }
            }
            if (node->Kind == NK::Initializer &&
                style.AssignmentOperatorSpacing != TokenSpacing::Preserve &&
                node->Tokens.Begin < count &&
                tree.Kind(node->Tokens.Begin) == TK::Operator_Assign &&
                node->Tokens.Begin > 0 && node->Tokens.Begin + 1 < count)
            {
                gaps[node->Tokens.Begin] = style.AssignmentOperatorSpacing;
                gaps[node->Tokens.Begin + 1] = style.AssignmentOperatorSpacing;
            }
            if (node->Kind == NK::CastExpression &&
                style.CastArgumentSpacing != TokenSpacing::Preserve)
            {
                for (auto i = node->Tokens.Begin;
                    i < node->Tokens.End && i < count; ++i)
                {
                    if (tree.Kind(i) == TK::Operator_OpenParenthesis &&
                        tree.Role(i) == PR::CastDelimiter)
                    {
                        auto close = tree.MatchingDelimiter(i);
                        if (close != NoToken && close < node->Tokens.End) {
                            markInside(i, close, style.CastArgumentSpacing);
                        }
                        break;
                    }
                }
            }
            if (node->Kind == NK::CastExpression &&
                style.CastSpacing != TokenSpacing::Preserve &&
                tree.Kind(node->Tokens.Begin) == TK::Operator_OpenParenthesis)
            {
                auto close = tree.MatchingDelimiter(node->Tokens.Begin);
                if (close + 1 < node->Tokens.End && close + 1 < count) {
                    gaps[close + 1] = style.CastSpacing;
                }
            }
        }
        // Do not pass template safety through arbitrary arguments or default
        // expressions: they can contain comparisons that resemble templates.
        const bool childTyped =
            node->Kind == NK::TemplateIdExpression ||
            node->Kind == NK::DeclarationSpecifiers ||
            node->Kind == NK::TypeId || node->Kind == NK::NewTypeId ||
            node->Kind == NK::BaseType;
        for (auto* child = node->FirstChild; child;
            child = child->NextSibling)
        {
            nodes.emplace_back(child, childTyped);
        }
    }
    auto pointer = [&](TokenIndex i) {
        return tree.Role(i) == PR::PointerDeclarator ||
            tree.Role(i) == PR::ReferenceDeclarator;
    };
    for (TokenIndex i = 0; i < count; ++i) {
        if (pointer(i) &&
            style.PointerReferenceLocation != PointerReferenceLocation::Preserve)
        {
            // Keep adjacent pointer/reference operators together. Leave
            // member pointers and nested declarators alone rather than
            // guessing their binding.
            auto first = i;
            while (i + 1 < count && pointer(i + 1)) {
                ++i;
            }
            auto last = i;
            if (first == 0 || last + 1 == count ||
                tree.Kind(first - 1) == TK::Operator_OpenParenthesis ||
                tree.Kind(first - 1) == TK::Operator_ScopeResolution ||
                (tree.Kind(last + 1) != TK::Identifier &&
                    tree.Kind(last + 1) != TK::Keyword_const &&
                    tree.Kind(last + 1) != TK::Keyword_volatile))
            {
                continue;
            }
            auto location = style.PointerReferenceLocation;
            // A comma separates declarators, not type and pointer. Leave
            // its following gap to CommaSpacing (or the original source).
            if (tree.Kind(first - 1) != TK::Operator_Comma) {
                gaps[first] = location == PointerReferenceLocation::Left
                    ? TokenSpacing::NoSpace : TokenSpacing::Space;
            }
            for (auto k = first + 1; k <= last; ++k) {
                gaps[k] = TokenSpacing::NoSpace;
            }
            gaps[last + 1] = location == PointerReferenceLocation::Right
                ? TokenSpacing::NoSpace : TokenSpacing::Space;
            continue;
        }
        auto role = tree.Role(i);
        auto kind = tree.Kind(i);
        TokenSpacing operatorStyle = TokenSpacing::Preserve;
        if (role == PR::BinaryOperator ||
            role == PR::ComparisonOperator || role == PR::ShiftOperator)
        {
            operatorStyle = style.BinaryOperatorSpacing;
        } else if (role == PR::AssignmentOperator) {
            operatorStyle = style.AssignmentOperatorSpacing;
        }
        // Angle and shift tokens can also spell template syntax; do not
        // change them without name lookup.
        if (operatorStyle != TokenSpacing::Preserve && i > 0 && i + 1 < count &&
            kind != TK::Operator_Less && kind != TK::Operator_Greater &&
            kind != TK::Operator_LeftShift && kind != TK::Operator_RightShift)
        {
            gaps[i] = operatorStyle;
            gaps[i + 1] = operatorStyle;
        }
        if (kind == TK::Operator_Comma && role == PR::ListSeparator &&
            style.CommaSpacing != CommaSpacing::Preserve && i + 1 < count)
        {
            gaps[i] = TokenSpacing::NoSpace;
            if (tree.Kind(i + 1) != TK::Operator_CloseParenthesis &&
                tree.Kind(i + 1) != TK::Operator_CloseBracket &&
                tree.Kind(i + 1) != TK::Operator_CloseBrace)
            {
                gaps[i + 1] = style.CommaSpacing == CommaSpacing::After
                    ? TokenSpacing::Space : TokenSpacing::NoSpace;
            }
        }
        if (style.MemberAccessOperatorsSpacing ==
                TightOperatorSpacing::NoSpace &&
            kind == TK::Operator_ScopeResolution && i + 1 < count &&
            (tree.Kind(i + 1) == TK::Identifier ||
                tree.Kind(i + 1) == TK::Keyword_template ||
                tree.Kind(i + 1) == TK::Keyword_operator ||
                tree.Kind(i + 1) == TK::Operator_Complement))
        {
            // A leading :: has no qualifier on its left; preserve its
            // separation from preceding keywords and assignments.
            if (i > 0 && tree.Kind(i - 1) == TK::Identifier) {
                gaps[i] = TokenSpacing::NoSpace;
            }
            gaps[i + 1] = TokenSpacing::NoSpace;
        }
        if (style.MemberAccessOperatorsSpacing ==
                TightOperatorSpacing::NoSpace &&
            (kind == TK::Operator_PointerToMemberDereference ||
                kind == TK::Operator_IndirectPointerToMemberDereference) &&
            i > 0 && i + 1 < count)
        {
            gaps[i] = TokenSpacing::NoSpace;
            gaps[i + 1] = TokenSpacing::NoSpace;
        }
        if (kind == TK::Operator_Semicolon &&
            (loopSemicolons[i] || conditionSemicolons[i]))
        {
            gaps[i] = TokenSpacing::NoSpace;
            if (i + 1 < count &&
                tree.Kind(i + 1) != TK::Operator_Semicolon &&
                tree.Kind(i + 1) != TK::Operator_CloseParenthesis)
            {
                gaps[i + 1] = loopSemicolons[i]
                    ? style.LoopSemicolonSpacing
                    : style.ConditionInitSemicolonSpacing;
            }
        }
    }
    std::string output;
    output.reserve(tree.Source().size());
    for (TokenIndex i = 0; i < count; ++i) {
        auto gap = tree.WhitespaceBefore(i);
        auto setting = gaps[i];
        const auto memberPointer = [](TK kind) {
            return kind == TK::Operator_PointerToMemberDereference ||
                kind == TK::Operator_IndirectPointerToMemberDereference;
        };
        // The parser may leave member-pointer expressions recovered, but
        // the two complete operator tokens are lexically unambiguous.
        const bool safeMemberPointerGap = i > 0 &&
            style.MemberAccessOperatorsSpacing ==
                TightOperatorSpacing::NoSpace &&
            ((memberPointer(tree.Kind(i)) &&
                (tree.Kind(i - 1) == TK::Identifier ||
                    tree.Kind(i - 1) == TK::Operator_CloseParenthesis ||
                    tree.Kind(i - 1) == TK::Operator_CloseBracket)) ||
                (memberPointer(tree.Kind(i - 1)) &&
                    tree.Kind(i) == TK::Identifier));
        if (i == 0 ||
            ((unsafe[i] || unsafe[i - 1]) && !safeMemberPointerGap) ||
            tree.Kind(i - 1) == TK::Comment || tree.Kind(i) == TK::Comment ||
            tree.Kind(i - 1) == TK::PreprocessingDirective ||
            tree.Kind(i) == TK::PreprocessingDirective ||
            gap.find_first_not_of(" \t") != std::string_view::npos)
        {
            setting = TokenSpacing::Preserve;
        }
        // Removing space between two operator spellings can change tokens
        // (e.g. + + becomes ++). Restrict such gaps to word/delimiter edges.
        if (setting == TokenSpacing::NoSpace) {
            auto left = tree.TokenText(i - 1);
            auto right = tree.TokenText(i);
            auto edge = [](char ch) {
                return std::isalnum(static_cast<unsigned char>(ch)) ||
                    ch == '_' || ch == ')' || ch == ']' || ch == '(' ||
                    ch == '[' || ch == ',' || ch == ';';
            };
            if (!edge(left.back()) && !edge(right.front()) &&
                tree.Kind(i) != TK::Operator_Comma &&
                tree.Kind(i) != TK::Operator_Semicolon &&
                !(pointer(i - 1) && pointer(i)) &&
                !(tree.Kind(i - 1) == TK::Operator_OpenBrace &&
                    tree.Kind(i) == TK::Operator_OpenBrace) &&
                !(tree.Kind(i - 1) == TK::Operator_CloseBrace &&
                    tree.Kind(i) == TK::Operator_CloseBrace) &&
                !(tree.Kind(i - 1) == TK::Operator_ScopeResolution &&
                    tree.Kind(i) == TK::Operator_Complement) &&
                tree.Kind(i - 1) != TK::Operator_OpenBrace &&
                tree.Kind(i) != TK::Operator_CloseBrace) {
                setting = TokenSpacing::Preserve;
            }
        }
        if (setting == TokenSpacing::Preserve) {
            output.append(gap);
        } else if (setting == TokenSpacing::Space) {
            output.push_back(' ');
        }
        output.append(tree.TokenText(i));
    }
    if (count) {
        output.append(tree.WhitespaceAfter(count - 1));
    } else {
        output.append(tree.Source());
    }
    return output;
}

// Only horizontal trivia before standalone directives and trailing line
// comments is eligible. Directive bodies, continuations, and comment text
// stay byte-for-byte identical.
static std::string FormatLineOwnedSpacing(const parser::SyntaxTree& tree,
    const Style& style)
{
    enum class Directive { Other, If, Branch, EndIf };
    auto directiveKind = [](std::string_view text) {
        if (text.starts_with("%:")) {
            text.remove_prefix(2);
        } else if (text.starts_with('#')) {
            text.remove_prefix(1);
        } else {
            return Directive::Other;
        }
        auto first = text.find_first_not_of(" \t\v\f");
        if (first == std::string_view::npos) {
            return Directive::Other;
        }
        text.remove_prefix(first);
        const auto word = text.substr(0,
            text.find_first_not_of(
                "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_"));
        if (word == "if" || word == "ifdef" || word == "ifndef") {
            return Directive::If;
        }
        if (word == "elif" || word == "elifdef" ||
            word == "elifndef" || word == "else")
        {
            return Directive::Branch;
        }
        return word == "endif" ? Directive::EndIf : Directive::Other;
    };
    size_t depth = 0;
    bool balanced = true;
    if (style.PreprocessorIndentation ==
        PreprocessorIndentation::PreprocessorIndented)
    {
        for (TokenIndex i = 0; i < tree.TokenCount(); ++i) {
            if (tree.Kind(i) != TK::PreprocessingDirective) {
                continue;
            }
            switch (directiveKind(tree.TokenText(i))) {
            case Directive::If:
                ++depth;
                break;
            case Directive::Branch:
                balanced &= depth != 0;
                break;
            case Directive::EndIf:
                if (depth == 0) {
                    balanced = false;
                } else {
                    --depth;
                }
                break;
            case Directive::Other:
                break;
            }
        }
        balanced &= depth == 0;
    }
    depth = 0;
    std::string output;
    output.reserve(tree.Source().size());
    for (TokenIndex i = 0; i < tree.TokenCount(); ++i) {
        const auto gap = tree.WhitespaceBefore(i);
        bool replaced = false;
        if (style.TrailingCommentSpacing != TokenSpacing::Preserve &&
            i > 0 && tree.Kind(i) == TK::Comment &&
            tree.TokenText(i).starts_with("//") &&
            tree.Kind(i - 1) != TK::Comment &&
            tree.Kind(i - 1) != TK::PreprocessingDirective &&
            (style.TrailingCommentSpacing != TokenSpacing::NoSpace ||
                tree.TokenText(i - 1).back() != '/') &&
            gap.find_first_not_of(" \t") == std::string_view::npos)
        {
            if (style.TrailingCommentSpacing == TokenSpacing::Space) {
                output.push_back(' ');
            }
            replaced = true;
        }
        if (!replaced &&
            style.PreprocessorIndentation != PreprocessorIndentation::Preserve &&
            tree.Kind(i) == TK::PreprocessingDirective &&
            tree.TokenText(i).find('\n') == std::string_view::npos &&
            (balanced || style.PreprocessorIndentation ==
                PreprocessorIndentation::LineStart))
        {
            const auto newline = gap.rfind('\n');
            const auto indentStart = newline == std::string_view::npos
                ? 0 : newline + 1;
            const auto prefix = gap.substr(indentStart);
            if ((newline != std::string_view::npos || i == 0) &&
                prefix.find_first_not_of(" \t") ==
                    std::string_view::npos)
            {
                auto level = depth;
                const auto kind = directiveKind(tree.TokenText(i));
                if (kind == Directive::Branch || kind == Directive::EndIf) {
                    level = level == 0 ? 0 : level - 1;
                }
                if (level > 256 && style.PreprocessorIndentation ==
                        PreprocessorIndentation::PreprocessorIndented)
                {
                    output.append(gap);
                } else {
                    output.append(gap.substr(0, indentStart));
                }
                if (style.PreprocessorIndentation ==
                    PreprocessorIndentation::PreprocessorIndented &&
                    level <= 256)
                {
                    output += GeneratedIndent(level * style.IndentWidth,
                        style);
                }
                replaced = true;
            }
        }
        if (!replaced) {
            output.append(gap);
        }
        output.append(tree.TokenText(i));
        if (tree.Kind(i) == TK::PreprocessingDirective && balanced &&
            style.PreprocessorIndentation ==
                PreprocessorIndentation::PreprocessorIndented)
        {
            const auto kind = directiveKind(tree.TokenText(i));
            if (kind == Directive::If) {
                ++depth;
            } else if (kind == Directive::EndIf && depth != 0) {
                --depth;
            }
        }
    }
    if (tree.TokenCount()) {
        output.append(tree.WhitespaceAfter(tree.TokenCount() - 1));
    } else {
        output.append(tree.Source());
    }
    return output;
}

// Trim line-end trivia without editing raw strings, literal continuations,
// or preprocessing tokens. A trailing backslash may be a line splice.
static std::string TrimTrailingWhitespace(const parser::SyntaxTree& tree)
{
    const auto source = tree.Source();
    std::string result;
    result.reserve(source.size());
    size_t token = 0;
    for (size_t begin = 0; begin < source.size();) {
        const auto newline = source.find('\n', begin);
        const bool hasNewline = newline != std::string_view::npos;
        const auto end = hasNewline ? newline : source.size();
        const auto contentEnd = end > begin && source[end - 1] == '\r'
            ? end - 1 : end;
        auto trim = contentEnd;
        while (trim > begin && (source[trim - 1] == ' ' ||
                source[trim - 1] == '\t'))
        {
            --trim;
        }
        while (token < tree.TokenCount() &&
            tree.Token(token).Text.End <= trim)
        {
            ++token;
        }
        const bool insideToken = token < tree.TokenCount() &&
            tree.Token(token).Text.Begin < contentEnd &&
            tree.Token(token).Text.End > trim &&
            tree.Kind(token) != TK::Comment;
        if (insideToken ||
            (trim > begin && source[trim - 1] == '\\'))
        {
            trim = contentEnd;
        }
        result.append(source.substr(begin, trim - begin));
        result.append(source.substr(contentEnd,
            (hasNewline ? end + 1 : end) - contentEnd));
        begin = hasNewline ? end + 1 : end;
    }
    return result;
}

std::vector<LayoutDiagnostic> CheckExpressionLayout(
    const parser::SyntaxTree& tree, const Style& style)
{
    std::vector<LayoutDiagnostic> diagnostics;
    if (style.ExpressionLayoutCondition == ExpressionLayoutCondition::Preserve) {
        return diagnostics;
    }
    const auto source = tree.Source();
    uint32_t lineNumber = 1;
    for (size_t start = 0; start < source.size(); ++lineNumber) {
        auto end = source.find('\n', start);
        if (end == std::string_view::npos) {
            end = source.size();
        }
        auto width = end - start;
        if (width && source[end - 1] == '\r') {
            --width;
        }
        if (width > style.ColumnLimit) {
            diagnostics.push_back({
                {.Line = lineNumber,
                 .Column = static_cast<uint32_t>(style.ColumnLimit + 1)},
                "line exceeds ColumnLimit (" + std::to_string(width) +
                    " > " + std::to_string(style.ColumnLimit) + ")",
                LayoutDiagnosticKind::LineWidth});
        }
        start = end == source.size() ? end : end + 1;
    }
    std::vector<const parser::Node*> nodes{&tree.Root()};
    auto checkPlacement = [&] (TokenIndex index, bool leading,
        std::string_view name) {
        if (!SafeOperatorGap(tree, index)) {
            return;
        }
        bool left = tree.WhitespaceBefore(index).find('\n') !=
            std::string_view::npos;
        bool right = tree.WhitespaceAfter(index).find('\n') !=
            std::string_view::npos;
        if ((left || right) && (leading ? !left || right : !right || left)) {
            const auto& token = tree.Token(index);
            diagnostics.push_back({token.Position, std::string(name) +
                (leading ? " must start the following line" :
                    " must end the preceding line"),
                LayoutDiagnosticKind::OperatorPlacement});
        }
    };
    while (!nodes.empty()) {
        auto* node = nodes.back();
        nodes.pop_back();
        if (IsIncompleteStatement(*node)) {
            continue;
        }
        if (node->Kind == NK::BinaryExpression) {
            const auto* left = node->Child();
            const auto* right = left ? left->NextSibling : nullptr;
            if (left && right && !left->Tokens.Empty() &&
                !right->Tokens.Empty())
            {
                for (auto i = left->Tokens.End; i < right->Tokens.Begin; ++i) {
                    const auto role = tree.Role(i);
                    if (!IsAmbiguousAngleOperator(tree.Kind(i)) &&
                        (role == parser::PunctuationRole::BinaryOperator ||
                            role == parser::PunctuationRole::ComparisonOperator ||
                            role == parser::PunctuationRole::ShiftOperator))
                    {
                        checkPlacement(i, style.BinaryOperatorPlacement ==
                            BinaryOperatorPlacement::Leading,
                            "binary operator");
                    }
                }
            }
        } else if (auto delimiters = ConditionalDelimiters(tree, *node)) {
            auto [question, colon] = *delimiters;
            if (style.TernaryOperatorPlacement !=
                TernaryOperatorPlacement::Preserve)
            {
                bool leading = style.TernaryOperatorPlacement ==
                    TernaryOperatorPlacement::Leading;
                checkPlacement(question, leading, "ternary ?");
                checkPlacement(colon, leading, "ternary :");
            }
            if (style.TernarySplit == TernarySplit::ThreeLines &&
                SafeOperatorGap(tree, question) &&
                SafeOperatorGap(tree, colon))
            {
                auto hasBreak = [&] (TokenIndex i) {
                    return tree.WhitespaceBefore(i).find('\n') !=
                            std::string_view::npos ||
                        tree.WhitespaceAfter(i).find('\n') !=
                            std::string_view::npos;
                };
                if (hasBreak(question) != hasBreak(colon)) {
                    auto missing = hasBreak(question) ? colon : question;
                    const auto& token = tree.Token(missing);
                    diagnostics.push_back({token.Position,
                        std::string("ternary '") +
                            (missing == question ? '?' : ':') +
                            "' needs a line break to match the other delimiter",
                        LayoutDiagnosticKind::TernarySplit});
                }
            }
        }
        for (auto* child = node->Child(); child; child = child->NextSibling) {
            nodes.push_back(child);
        }
    }
    std::ranges::sort(diagnostics, [] (const auto& a, const auto& b) {
        return std::pair{a.Position.Line, a.Position.Column} <
            std::pair{b.Position.Line, b.Position.Column};
    });
    return diagnostics;
}

std::string Format(const parser::SyntaxTree& tree, const Style& style)
{
    const bool bracesPreserved =
        style.IndentAccessModifiers == BodyIndentation::Preserve &&
        style.IndentMembers == BodyIndentation::Preserve &&
        style.InheritanceList == InheritanceListMode::Preserve &&
        style.ConstructorInitializerList == InheritanceListMode::Preserve &&
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
    if (style.InitializerListSplit != FunctionParameterSplit::Preserve) {
        // Outer edits can contain nested lists. Reparse until they stabilize;
        // if nesting exceeds the bound, leave initializer layout untouched.
        auto beforeInitializers = formatted;
        bool stable = false;
        for (size_t pass = 0; pass < 16; ++pass) {
            auto length = formatted.size();
            formatted.append(util::SourcePaddingSize, '\0');
            auto parsed = parser::Parse(
                {.Data = formatted.data(), .Size = length},
                [] (parser::ParseError) { });
            auto result = FormatInitializers(parsed, style);
            stable = result == std::string_view(formatted.data(), length);
            formatted = std::move(result);
            if (stable) break;
        }
        if (!stable) formatted = std::move(beforeInitializers);
    }
    bool headerReflowed = false;
    if (style.CallArgumentSplit != CallArgumentSplit::Preserve &&
        style.ExpressionLayoutCondition != ExpressionLayoutCondition::Preserve)
    {
        // Choose enclosing calls first, then nested calls only when present.
        // Bound reparsing on extremely deep expression chains.
        for (size_t depth = 0; depth < 3; ++depth) {
            bool deeperCalls = false;
            if (formatted == tree.Source()) {
                auto result = FormatCalls(tree, style, depth, deeperCalls);
                headerReflowed |= result != formatted;
                formatted = std::move(result);
            } else {
                auto length = formatted.size();
                formatted.append(util::SourcePaddingSize, '\0');
                auto parsed = parser::Parse(
                    {.Data = formatted.data(), .Size = length},
                    [] (parser::ParseError) { });
                auto result = FormatCalls(parsed, style, depth, deeperCalls);
                headerReflowed |= result !=
                    std::string_view(formatted.data(), length);
                formatted = std::move(result);
            }
            if (!deeperCalls) {
                break;
            }
        }
    }
    if (style.ExpressionLayoutCondition != ExpressionLayoutCondition::Preserve) {
        if (formatted == tree.Source()) {
            auto result = FormatExpressions(tree, style);
            headerReflowed |= result != formatted;
            formatted = std::move(result);
        } else {
            auto length = formatted.size();
            formatted.append(util::SourcePaddingSize, '\0');
            auto parsed = parser::Parse(
                {.Data = formatted.data(), .Size = length},
                [] (parser::ParseError) { });
            auto result = FormatExpressions(parsed, style);
            headerReflowed |= result !=
                std::string_view(formatted.data(), length);
            formatted = std::move(result);
        }
        if (style.TernarySplit == TernarySplit::ThreeLines) {
            auto length = formatted.size();
            formatted.append(util::SourcePaddingSize, '\0');
            auto parsed = parser::Parse(
                {.Data = formatted.data(), .Size = length},
                [] (parser::ParseError) { });
            auto grouped = CompleteTernaryBreaks(parsed, style);
            if (grouped != std::string_view(formatted.data(), length)) {
                headerReflowed = true;
                auto groupLength = grouped.size();
                grouped.append(util::SourcePaddingSize, '\0');
                auto groupTree = parser::Parse(
                    {.Data = grouped.data(), .Size = groupLength},
                    [] (parser::ParseError) { });
                Style overflowStyle = style;
                overflowStyle.ExpressionLayoutCondition =
                    ExpressionLayoutCondition::Overflow;
                formatted = FormatExpressions(groupTree, overflowStyle);
            } else {
                formatted = std::move(grouped);
            }
        }
        if (style.TernarySplit == TernarySplit::ThreeLines &&
            style.TernaryOperatorPlacement == TernaryOperatorPlacement::Leading)
        {
            auto length = formatted.size();
            formatted.append(util::SourcePaddingSize, '\0');
            auto parsed = parser::Parse(
                {.Data = formatted.data(), .Size = length},
                [] (parser::ParseError) { });
            formatted = AlignLeadingTernaryColons(parsed);
        }
    }
    // AttachUnlessMultiline depends on the resulting header, not the
    // original one. Revisit only control braces after argument and expression
    // reflow; rerunning unrelated formatting passes is unnecessary.
    if (headerReflowed && style.ControlBlockBracePlacement ==
            ControlBlockBracePlacement::AttachUnlessMultiline)
    {
        auto length = formatted.size();
        formatted.append(util::SourcePaddingSize, '\0');
        auto parsed = parser::Parse(
            {.Data = formatted.data(), .Size = length},
            [] (parser::ParseError) { });
        Style controlStyle;
        controlStyle.IndentWidth = style.IndentWidth;
        controlStyle.IndentationKind = style.IndentationKind;
        controlStyle.ControlBlockBracePlacement =
            style.ControlBlockBracePlacement;
        formatted = Formatter(parsed, controlStyle).Run();
    }
    if (style.NamespaceComments != NamespaceComments::Preserve) {
        if (formatted == tree.Source()) {
            formatted = FormatNamespaceComments(tree, style.NamespaceComments,
                style.ShortNamespaceLines);
        } else {
            auto length = formatted.size();
            formatted.append(util::SourcePaddingSize, '\0');
            auto finalTree = parser::Parse(
                {.Data = formatted.data(), .Size = length},
                [] (parser::ParseError) { });
            formatted = FormatNamespaceComments(finalTree,
                style.NamespaceComments, style.ShortNamespaceLines);
        }
    }
    if (style.PointerReferenceLocation != PointerReferenceLocation::Preserve ||
        style.BinaryOperatorSpacing != TokenSpacing::Preserve ||
        style.AssignmentOperatorSpacing != TokenSpacing::Preserve ||
        style.CastSpacing != TokenSpacing::Preserve ||
        style.CommaSpacing != CommaSpacing::Preserve ||
        style.LoopSemicolonSpacing != TokenSpacing::Preserve ||
        style.ConditionInitSemicolonSpacing != TokenSpacing::Preserve ||
        style.SubscriptOperatorInteriorSpacing != TokenSpacing::Preserve ||
        style.SubscriptOperatorSpacing != TokenSpacing::Preserve ||
        style.ArrayDeclarationInteriorSpacing != TokenSpacing::Preserve ||
        style.ArrayDeclarationSpacing != TokenSpacing::Preserve ||
        style.BraceInitializerSpacing != TokenSpacing::Preserve ||
        style.ReturnArrowSpacing != ReturnArrowSpacing::Preserve ||
        style.EmptyFunctionCallArgumentsSpacing != TokenSpacing::Preserve ||
        style.EmptyFunctionParametersSpacing != TokenSpacing::Preserve ||
        style.EmptyLambdaParametersSpacing != TokenSpacing::Preserve ||
        style.EmptyLambdaCaptureSpacing != TokenSpacing::Preserve ||
        style.StructuredBindingBracketSpacing != TokenSpacing::Preserve ||
        style.UnaryOperatorSpacing != TightOperatorSpacing::Preserve ||
        style.PostfixOperatorSpacing != TightOperatorSpacing::Preserve ||
        style.TernaryOperatorSpacing != TernaryOperatorSpacing::Preserve ||
        style.TemplateKeywordSpacing != TokenSpacing::Preserve ||
        style.TemplateTypeArgumentInteriorSpacing != TokenSpacing::Preserve ||
        style.TemplateTypeArgumentSpacing != TokenSpacing::Preserve ||
        style.FunctionQualifierSpacing != TokenSpacing::Preserve ||
        style.FunctionRefQualifierSpacing != TokenSpacing::Preserve ||
        style.NestedDeclaratorInteriorSpacing != TokenSpacing::Preserve ||
        style.NestedDeclaratorPointerSpacing != TokenSpacing::Preserve ||
        style.MemberPointerDeclaratorSpacing != TokenSpacing::Preserve ||
        style.NoexceptSpaceBeforeParentheses != TokenSpacing::Preserve ||
        style.AlignasSpaceBeforeParentheses != TokenSpacing::Preserve ||
        style.AlignofSpaceBeforeParentheses != TokenSpacing::Preserve ||
        style.SizeofSpaceBeforeParentheses != TokenSpacing::Preserve ||
        style.DecltypeSpaceBeforeParentheses != TokenSpacing::Preserve ||
        style.TypeidSpaceBeforeParentheses != TokenSpacing::Preserve ||
        style.RequiresSpaceBeforeParentheses != TokenSpacing::Preserve ||
        style.StaticAssertSpaceBeforeParentheses != TokenSpacing::Preserve ||
        style.AttributeSpaceAfterSpecifier != TokenSpacing::Preserve ||
        style.NamedCastSpaceBeforeTypeArguments != TokenSpacing::Preserve ||
        style.PlacementNewSpaceBeforeArguments != TokenSpacing::Preserve ||
        style.AttributeSpaceBeforeSpecifier != TokenSpacing::Preserve ||
        style.NewInitializerSpaceBeforeParentheses !=
            TokenSpacing::Preserve ||
        style.DeleteArraySpaceBeforeBrackets != TokenSpacing::Preserve ||
        style.SizeofPackSpaceBeforeParentheses != TokenSpacing::Preserve ||
        style.NewInitializerSpaceBeforeBraces != TokenSpacing::Preserve ||
        style.NewInitializerParenthesisInteriorSpacing !=
            TokenSpacing::Preserve ||
        style.DeleteArraySpaceAfterBrackets != TokenSpacing::Preserve ||
        style.PlacementNewSpaceAfterArguments != TokenSpacing::Preserve ||
        style.NewArraySpaceBeforeBrackets != TokenSpacing::Preserve ||
        style.NewArrayInteriorSpacing != TokenSpacing::Preserve ||
        style.EmptyAttributeInteriorSpacing != TokenSpacing::Preserve ||
        style.CoAwaitSpaceBeforeParentheses != TokenSpacing::Preserve ||
        style.NewInitializerBraceInteriorSpacing != TokenSpacing::Preserve ||
        style.PlacementNewArgumentInteriorSpacing != TokenSpacing::Preserve ||
        style.EmptyArrayDeclarationInteriorSpacing !=
            TokenSpacing::Preserve ||
        style.EmptyNewArrayInteriorSpacing != TokenSpacing::Preserve ||
        style.NoexceptSpaceBeforeSpecifier != TokenSpacing::Preserve ||
        style.NamedCastSpaceBeforeOperandParentheses !=
            TokenSpacing::Preserve ||
        style.ReturnSpaceBeforeParentheses != TokenSpacing::Preserve ||
        style.ThrowSpaceBeforeParentheses != TokenSpacing::Preserve ||
        style.CoReturnSpaceBeforeParentheses != TokenSpacing::Preserve ||
        style.FunctionSuffixSpecifierSpacing != TokenSpacing::Preserve ||
        style.RecordBodyInteriorSpacing != TokenSpacing::Preserve ||
        style.NamespaceBodyInteriorSpacing != TokenSpacing::Preserve ||
        style.EnumBodyInteriorSpacing != TokenSpacing::Preserve ||
        style.RequirementBodyInteriorSpacing != TokenSpacing::Preserve ||
        style.EmptyBraceInitializerInteriorSpacing != TokenSpacing::Preserve ||
        style.OperatorOverloadOperatorSpacing !=
            OperatorOverloadOperatorSpacing::Preserve ||
        style.AttributeInteriorSpacing != TokenSpacing::Preserve ||
        style.StatementSemicolonSpacing != TightOperatorSpacing::Preserve ||
        style.MemberAccessOperatorsSpacing != TightOperatorSpacing::Preserve ||
        style.InitializerBracesSpacing != TokenSpacing::Preserve ||
        style.CastArgumentSpacing != TokenSpacing::Preserve ||
        style.GroupingInteriorSpacing != TokenSpacing::Preserve ||
        style.ControlBlockConditionSpacing != TokenSpacing::Preserve ||
        style.AccessModifierColonSpacing != TokenSpacing::Preserve ||
        style.RangeBasedForColonSpacing != TokenSpacing::Preserve ||
        style.BlockInteriorSpacing != TokenSpacing::Preserve ||
        style.EmptyBlockInteriorSpacing != TokenSpacing::Preserve ||
        style.ControlBlockSpaceBeforeBrace != TokenSpacing::Preserve ||
        style.DeclarationSemicolonSpacing != TokenSpacing::Preserve ||
        style.BitFieldColonSpacing != ColonSpacing::Preserve ||
        style.EnumUnderlyingTypeColonSpacing != ColonSpacing::Preserve ||
        style.LabelColonSpacing != ColonSpacing::Preserve)
    {
        if (formatted == tree.Source()) {
            formatted = FormatTokenSpacing(tree, style);
        } else {
            auto length = formatted.size();
            formatted.append(util::SourcePaddingSize, '\0');
            auto parsed = parser::Parse(
                {.Data = formatted.data(), .Size = length},
                [] (parser::ParseError) { });
            formatted = FormatTokenSpacing(parsed, style);
        }
    }
    if (style.IncludeOrder != IncludeOrder::Preserve) {
        if (formatted == tree.Source()) {
            formatted = FormatIncludes(tree);
        } else {
            auto length = formatted.size();
            formatted.append(util::SourcePaddingSize, '\0');
            auto parsed = parser::Parse(
                {.Data = formatted.data(), .Size = length},
                [] (parser::ParseError) { });
            formatted = FormatIncludes(parsed);
        }
    }
    if (style.TrailingCommentSpacing != TokenSpacing::Preserve ||
        style.PreprocessorIndentation != PreprocessorIndentation::Preserve)
    {
        if (formatted == tree.Source()) {
            formatted = FormatLineOwnedSpacing(tree, style);
        } else {
            auto length = formatted.size();
            formatted.append(util::SourcePaddingSize, '\0');
            auto parsed = parser::Parse(
                {.Data = formatted.data(), .Size = length},
                [] (parser::ParseError) { });
            formatted = FormatLineOwnedSpacing(parsed, style);
        }
    }
    if (style.IndentBlockBodies ||
        style.IndentCaseLabels != BodyIndentation::Preserve)
    {
        auto length = formatted.size();
        formatted.append(util::SourcePaddingSize, '\0');
        auto parsed = parser::Parse(
            {.Data = formatted.data(), .Size = length},
            [] (parser::ParseError) { });
        formatted = FormatBlockIndentation(parsed, style);
    }
    if (style.TrailingWhitespace == TrailingWhitespace::Preserve) {
        return formatted;
    }
    if (formatted == tree.Source()) {
        return TrimTrailingWhitespace(tree);
    }
    auto length = formatted.size();
    formatted.append(util::SourcePaddingSize, '\0');
    auto finalTree = parser::Parse(
        {.Data = formatted.data(), .Size = length},
        [] (parser::ParseError) { });
    return TrimTrailingWhitespace(finalTree);
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
