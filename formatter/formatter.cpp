#include "formatter.hpp"

#include <parser/parser.hpp>
#include <tokenizer/token.hpp>
#include <tokenizer/tokenizer.hpp>
#include <util/assert.hpp>
#include <util/phase_profiler.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <limits>
#include <memory_resource>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
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

std::string GeneratedIndentAfter(
    std::string_view prefix,
    size_t columns,
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

// Source gap plus an offset into rendered output. Offsets survive moving the
// output string; string_views into its buffer would not (for small strings).
struct RenderedGap
{
    util::SourceOffset Begin;
    util::SourceOffset End;
    size_t OutputBegin;
    size_t OutputSize;
};

// Token spelling and trivia outside configured gaps remain source-owned.
// Classification uses syntax nodes, not neighboring keywords (which also
// occur in expressions and template arguments).
class Formatter
{
public:
    Formatter(
        const parser::SyntaxTree& tree,
        const Style& style,
        const std::vector<bool>* changedControlHeaders = nullptr)
        : Tree_(tree)
        , Style_(style)
        , ChangedControlHeaders_(changedControlHeaders)
        , Scratch_(ScratchBuffer_, sizeof(ScratchBuffer_))
        , Placements_(tree.TokenCount(), BracePlacement::Preserve, &Scratch_)
        , Spacings_(
            tree.TokenCount(),
            SameLineBraceSpacing::Preserve,
            &Scratch_)
        , LambdaGaps_(tree.TokenCount(), false, &Scratch_)
        , LambdaParameterInsideGaps_(tree.TokenCount(), false, &Scratch_)
        , CaptureGaps_(
            tree.TokenCount(),
            LambdaCaptureSpacing::Preserve,
            &Scratch_)
        , FunctionGaps_(tree.TokenCount(), false, &Scratch_)
        , CallGaps_(tree.TokenCount(), false, &Scratch_)
        , LambdaCallGaps_(tree.TokenCount(), false, &Scratch_)
        , CallInsideGaps_(tree.TokenCount(), false, &Scratch_)
        , ParameterInsideGaps_(tree.TokenCount(), false, &Scratch_)
        , ControlGaps_(tree.TokenCount(), false, &Scratch_)
        , ControlBraces_(tree.TokenCount(), false, &Scratch_)
        , HeaderStarts_(tree.TokenCount(), NoToken, &Scratch_)
    {
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
            style.LambdaCallSpaceBeforeArguments !=
                LambdaCallSpaceBeforeArguments::Inherit ||
            style.SpaceInsideCallArguments !=
                SpaceInsideCallArguments::Preserve ||
            style.SpaceInsideFunctionParameters !=
                SpaceInsideFunctionParameters::Preserve ||
            style.ControlBlockKeywordSpacing !=
                ControlBlockKeywordSpacing::Preserve ||
            style.EnforceControlBlockBracePlacement || changedControlHeaders)
        {
            HIT_FMT_PROFILE_PHASE("formatter.brace_classify");
            Classify();
        }
    }

    // Copy unchanged token spans in bulk; render only changed gaps.
    // Token reordering still needs the full renderer below.
    std::string Run(std::vector<RenderedGap>* edits = nullptr)
    {
        HIT_FMT_PROFILE_PHASE("formatter.brace_render");
        const auto source = Tree_.Source();
        std::string result;
        size_t cursor = 0;
        bool changed = false;
        for (TokenIndex index = 0; index < Tree_.TokenCount(); ++index) {
            if (MoveBraceBeforeComment(index) ||
                MoveCommentBeforeBrace(index))
            {
                Output_.clear();
                if (edits) {
                    edits->clear();
                }
                return RunReordered(edits);
            }
            const auto begin = index == 0 ? size_t{0} :
                Tree_.Token(index - 1).Text.End;
            const auto end = Tree_.Token(index).Text.Begin;
            Output_.clear();
            EmitGap(index);
            if (Output_ == Tree_.WhitespaceBefore(index)) {
                continue;
            }
            if (!changed) {
                result.reserve(source.size());
            }
            result.append(source.substr(cursor, begin - cursor));
            const auto outputBegin = result.size();
            result.append(Output_);
            if (edits) {
                edits->push_back({static_cast<util::SourceOffset>(begin),
                    static_cast<util::SourceOffset>(end), outputBegin,
                    Output_.size()});
            }
            cursor = end;
            changed = true;
        }
        if (!changed) {
            return std::string(source);
        }
        result.append(source.substr(cursor));
        return result;
    }

    std::string RunReordered(std::vector<RenderedGap>* edits)
    {
        HIT_FMT_PROFILE_PHASE("formatter.brace_render_reordered");
        Output_.reserve(Tree_.Source().size());
        const auto count = Tree_.TokenCount();
        if (count == 0) {
            Output_.append(Tree_.Source());
        } else {
            for (TokenIndex index = 0; index < count; ++index) {
                if (MoveBraceBeforeComment(index)) {
                    // Reordered tokens cannot borrow the original syntax.
                    if (edits) {
                        edits->clear();
                    }
                    edits = nullptr;
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
                    if (edits) {
                        edits->clear();
                    }
                    edits = nullptr;
                    auto gap = Tree_.WhitespaceBefore(index);
                    Output_.append(gap.empty() ? " " : gap);
                    Output_.append(Tree_.TokenText(index + 1));
                    Output_.append(LineEnding(index));
                    auto header = HeaderStarts_[index];
                    Output_.append(BraceIndentation(
                        index,
                        header == NoToken ? index : header));
                    Output_.append(Tree_.TokenText(index));
                    ++index; // The comment has already been emitted.
                    continue;
                }
                if (index + 1 < count &&
                    MoveBraceBeforeComment(index + 1))
                {
                    if (edits) {
                        edits->clear();
                    }
                    edits = nullptr;
                    Output_.append(" {");
                }
                const auto outputBegin = Output_.size();
                EmitGap(index);
                if (edits) {
                    const auto gap = Tree_.WhitespaceBefore(index);
                    const auto emitted = std::string_view(Output_).substr(
                        outputBegin,
                        Output_.size() - outputBegin);
                    if (emitted != gap) {
                        const auto begin = index == 0 ? size_t{0} :
                            Tree_.Token(index - 1).Text.End;
                        const auto editBegin =
                            static_cast<util::SourceOffset>(begin);
                        edits->push_back({editBegin,
                            Tree_.Token(index).Text.Begin, outputBegin,
                            emitted.size()});
                    }
                }
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
            index < node.Tokens.End && index < Tree_.TokenCount();
            ++index)
        {
            if (Tree_.Kind(index) != TK::Operator_OpenBrace ||
                (node.Kind == NK::RecordDeclaration &&
                    Tree_.Role(index) !=
                        parser::PunctuationRole::RecordDelimiter))
            {
                continue;
            }
            if (Tree_.MatchingDelimiter(index) != NoToken &&
                (!ChangedControlHeaders_ ||
                    (index < ChangedControlHeaders_->size() &&
                        (*ChangedControlHeaders_)[index])))
            {
                Placements_[index] = placement;
                Spacings_[index] = spacing;
                HeaderStarts_[index] = header == NoToken
                    ? node.Tokens.Begin
                    : header;
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
            if (IsIncompleteStatement(*node) ||
                (node->Kind ==
                    NK::RecordDeclaration && HasRecoveryChild(*node)))
            {
                continue;
            }
            ClassifyNode(*node);
            for (auto* child = node->FirstChild;
                child;
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
                            MarkOpening(
                                node,
                                Style_.ClassBracePlacement,
                                Style_.ClassBraceSpacing);
                            break;
                        case TK::Keyword_struct:
                            MarkOpening(
                                node,
                                Style_.StructBracePlacement,
                                Style_.StructBraceSpacing);
                            break;
                        case TK::Keyword_union:
                            MarkOpening(
                                node,
                                Style_.UnionBracePlacement,
                                Style_.UnionBraceSpacing);
                            break;
                        default:
                            break;
                    }
                }
                break;
            case NK::EnumDeclaration:
                MarkBodyIndentation(node);
                MarkOpening(
                    node,
                    BracePlacement::Preserve,
                    Style_.EnumBraceSpacing);
                break;
            case NK::NamespaceDeclaration:
                MarkOpening(
                    node,
                    Style_.NamespaceBracePlacement,
                    Style_.NamespaceBraceSpacing);
                break;
            case NK::FunctionDefinition:
                MarkConstructorInitializers(node);
                for (auto* child = node.FirstChild;
                    child;
                child = child->NextSibling)
            {
                    if (child->Kind == NK::CompoundStatement) {
                        MarkOpening(
                            *child,
                            Style_.FunctionBracePlacement,
                            Style_.FunctionBraceSpacing,
                            node.Tokens.Begin);
                    } else if (child->Kind == NK::FunctionTryBlock) {
                        for (auto* body = child->FirstChild;
                            body;
                        body = body->NextSibling)
                    {
                            if (body->Kind == NK::CompoundStatement) {
                                MarkOpening(
                                    *body,
                                    Style_.FunctionBracePlacement,
                                    Style_.FunctionBraceSpacing,
                                    node.Tokens.Begin);
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
                MarkOpening(
                    node,
                    Style_.LambdaBracePlacement,
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
            for (auto* child = declaration.Child();
                child;
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
                i < declaration.Tokens.End;
                ++i)
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
        auto mark = [&] (const parser::Node& member, BodyIndentation policy) {
            auto index = member.Tokens.Begin;
            if (policy != BodyIndentation::Preserve &&
                !member.Tokens.Empty() && index > opening && index < closing &&
                Tree_.Kind(index) != TK::Comment &&
                Tree_.Kind(index) != TK::PreprocessingDirective &&
                // A header interrupted by directives can be recovered with
                // its initializer colon as the apparent member start.
                Tree_.Kind(index) != TK::Operator_Colon)
            {
                BodyIndents_[index] = baseline;
                if (policy == BodyIndentation::Indent) {
                    auto width = member.Kind == NK::AccessSpecifier
                        ? Style_.AccessModifierIndentWidth
                        : Style_.MemberIndentWidth;
                    BodyIndents_[index] += GeneratedIndentAfter(
                        BodyIndents_[index],
                        width ? width : Style_.IndentWidth,
                        Style_);
                }
            }
        };
        if (enumerators) {
            for (auto* child = enumerators->Child();
                child;
                child = child->NextSibling)
            {
                if (child->Kind == NK::Enumerator) {
                    mark(*child, Style_.IndentMembers);
                }
            }
        } else {
            for (auto* child = declaration.Child();
                child;
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
        const parser::Node& first,
        bool normalizeKeywords = true) const
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
                if (kind == TK::Comment || kind == TK::PreprocessingDirective) {
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
                        cursor,
                        tokenEnd - cursor));
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
        for (auto* child = declaration.Child();
            child;
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
                child->Kind != (expectBase
                    ? NK::BaseSpecifier
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
                i + 1 < child->Tokens.End;
                ++i)
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
            Style_.InheritanceListIndent == 0
                ? Style_.IndentWidth
                : Style_.InheritanceListIndent, Style_);
        auto space = [] (InheritanceListSpacing policy) -> std::string_view {
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
        const auto headerBegin =
            Tree_.Token(declaration.Tokens.Begin).Text.Begin;
        const auto headerEnd = Tree_.Token(colon - 1).Text.End;
        const auto header = source.substr(headerBegin, headerEnd - headerBegin);
        const auto lineStart = source.rfind('\n', headerBegin);
        auto prefix = headerBegin - (lineStart == std::string_view::npos
            ? 0
            : lineStart + 1);
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
            ? newline
            : std::string(colonBefore);
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
        for (auto* child = definition.Child();
            child;
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
                for (auto* child = node->Child();
                    child;
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
                    ? NK::ConstructorInitializer
                    : NK::ListSeparator))
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
        auto space = [] (InheritanceListSpacing policy) -> std::string_view {
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
        const auto headerBegin =
            Tree_.Token(definition.Tokens.Begin).Text.Begin;
        const auto headerEnd = Tree_.Token(colon - 1).Text.End;
        const auto header = source.substr(headerBegin, headerEnd - headerBegin);
        const auto lineStart = source.rfind('\n', headerBegin);
        auto prefix = headerBegin - (lineStart == std::string_view::npos
            ? 0
            : lineStart + 1);
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
            ? newline
            : std::string(colonBefore);
        InheritanceGaps_[entries->front().Opening] =
            firstNew && !colonWithInitializer
                ? newline
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
                child->Kind != (expectParameter
                    ? NK::ParameterDeclaration
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
        // A trailing block comment can be a commented parameter name.
        // Only its horizontal gap before ')' belongs to this option;
        // line comments and directives must keep their line ending.
        if (Tree_.Kind(closing - 1) != TK::PreprocessingDirective &&
            (Tree_.Kind(closing - 1) != TK::Comment ||
                Tree_.TokenText(closing - 1).starts_with("/*")))
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
                child->Kind != (expectCapture
                    ? NK::LambdaCapture
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
        if (Style_.SpaceAroundCaptureList != LambdaCaptureSpacing::Preserve) {
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
        for (auto* child = declarator.Child();
            child;
            child = child->NextSibling)
        {
            if (child->Kind == NK::DeclaratorName ||
                child->Kind == NK::DestructorName ||
                child->Kind == NK::OperatorFunctionName)
            {
                named = true;
            } else if (child->Kind == NK::FunctionDeclarator) {
                for (auto* list = child->Child();
                    list;
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
        const auto* callee = call.Child();
        const auto* arguments = callee ? callee->NextSibling : nullptr;
        if (!callee || callee->Tokens.Empty() ||
            callee->Kind == NK::UnparsedSyntax ||
            callee->Kind == NK::Expression ||
            IsAmbiguousParenthesizedCallee(*callee) ||
            !arguments || arguments->Kind != NK::ArgumentList ||
            arguments->Tokens.Empty() || HasRecoveryChild(*arguments))
        {
            return;
        }
        const bool lambdaOverride = callee->Kind == NK::LambdaExpression &&
            Style_.LambdaCallSpaceBeforeArguments !=
                LambdaCallSpaceBeforeArguments::Inherit;
        const bool spacePreserved = lambdaOverride
            ? Style_.LambdaCallSpaceBeforeArguments ==
                LambdaCallSpaceBeforeArguments::Preserve
            : Style_.FunctionCallSpaceBeforeParameters ==
                FunctionCallSpaceBeforeParameters::Preserve;
        if (spacePreserved && Style_.SpaceInsideCallArguments ==
                SpaceInsideCallArguments::Preserve)
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
        if (!spacePreserved &&
            Tree_.Kind(opening - 1) != TK::Comment &&
            Tree_.Kind(opening - 1) != TK::PreprocessingDirective)
        {
            if (lambdaOverride) {
                LambdaCallGaps_[opening] = true;
            } else {
                CallGaps_[opening] = true;
            }
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
            if (child->Kind == NK::Argument && child->Tokens.End == closing) {
                CallInsideGaps_[closing] = true;
                break;
            }
        }
    }

    void MarkControlBraces(const parser::Node& node)
    {
        auto policy = Style_.ControlBlockBracePlacement;
        if (!Style_.EnforceControlBlockBracePlacement &&
            !ChangedControlHeaders_)
        {
            return;
        }
        for (auto* child = node.Child(); child; child = child->NextSibling) {
            if (child->Kind != NK::CompoundStatement || child->Tokens.Empty()) {
                continue;
            }
            const auto opening = child->Tokens.Begin;
            if (ChangedControlHeaders_ &&
                (opening >= ChangedControlHeaders_->size() ||
                    !(*ChangedControlHeaders_)[opening]))
            {
                continue;
            }
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
                    (policy ==
                        ControlBlockBracePlacement::AttachUnlessMultiline &&
                        multiline)
                    ? BracePlacement::NewLine
                    : BracePlacement::Attach;
            MarkOpening(
                *child,
                placement,
                SameLineBraceSpacing::Preserve,
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
        auto lastNewline = offset == 0
            ? std::string_view::npos
            : source.rfind('\n', offset - 1);
        auto start = lastNewline == std::string_view::npos
            ? 0
            : lastNewline + 1;
        auto prefix = source.substr(start, offset - start);
        return prefix.find_first_not_of(" \t") == std::string_view::npos
            ? prefix
            : std::string_view{};
    }

    std::string_view BraceIndentation(TokenIndex brace, TokenIndex header) const
    {
        if (!ControlBraces_[brace]) {
            return Indentation(header);
        }
        auto source = Tree_.Source();
        auto offset = Tree_.Token(header).Text.Begin;
        auto newline = offset == 0
            ? std::string_view::npos
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
            ? "\r\n"
            : "\n";
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
        if (LambdaCallGaps_[index] &&
            gap.find_first_not_of(" \t") == std::string_view::npos)
        {
            if (Style_.LambdaCallSpaceBeforeArguments ==
                LambdaCallSpaceBeforeArguments::Space)
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
            Output_.append(BraceIndentation(
                index,
                header == NoToken ? index : header));
        }
    }

    const parser::SyntaxTree& Tree_;
    const Style& Style_;
    const std::vector<bool>* ChangedControlHeaders_;
    char ScratchBuffer_[8192];
    std::pmr::monotonic_buffer_resource Scratch_;
    std::pmr::vector<BracePlacement> Placements_;
    std::unordered_map<TokenIndex, std::string> BodyIndents_;
    std::unordered_map<TokenIndex, std::string> InheritanceGaps_;
    std::pmr::vector<SameLineBraceSpacing> Spacings_;
    std::pmr::vector<bool> LambdaGaps_;
    std::pmr::vector<bool> LambdaParameterInsideGaps_;
    std::pmr::vector<LambdaCaptureSpacing> CaptureGaps_;
    std::pmr::vector<bool> FunctionGaps_;
    std::pmr::vector<bool> CallGaps_;
    std::pmr::vector<bool> LambdaCallGaps_;
    std::pmr::vector<bool> CallInsideGaps_;
    std::pmr::vector<bool> ParameterInsideGaps_;
    std::pmr::vector<bool> ControlGaps_;
    std::pmr::vector<bool> ControlBraces_;
    // Only initialized for openings with an active placement.
    std::pmr::vector<TokenIndex> HeaderStarts_;
    std::string Output_;
};

struct Replacement
{
    Replacement(size_t begin, size_t end, std::string text)
        : Begin(static_cast<util::SourceOffset>(begin))
        , End(static_cast<util::SourceOffset>(end))
        , Text(std::move(text))
    {
        ASSERT(begin <= util::MaxSourceSize && end <= util::MaxSourceSize)
    }

    util::SourceOffset Begin;
    util::SourceOffset End;
    std::string Text;
};

struct LayoutEdits
{
    std::string Output;
    std::vector<Replacement> Edits;
    std::vector<parser::WhitespaceEdit> Gaps;
    std::vector<RenderedGap> RenderedGaps;
    bool SortedIncludes = false;
};

std::optional<parser::SyntaxTree> TryRebindLayout(
    const parser::SyntaxTree& tree,
    LayoutEdits& layout);

// Record only gaps changed by the brace renderer. Moving a comment or brace
// clears the plan so the caller falls back to verifying/parsing the output.
std::optional<LayoutEdits> RenderFormatter(
    const parser::SyntaxTree& tree,
    const Style& style,
    const std::vector<bool>* changedControlHeaders = nullptr)
{
    std::vector<RenderedGap> gaps;
    auto output = Formatter(tree, style, changedControlHeaders).Run(&gaps);
    return LayoutEdits{std::move(output), {}, {}, std::move(gaps)};
}

LayoutEdits RenderLayoutEdits(
    std::string_view source,
    std::vector<Replacement> edits,
    size_t reserve = 0)
{
    std::string output;
    output.reserve(std::max(source.size(), reserve));
    size_t cursor = 0;
    size_t accepted = 0;
    for (auto& edit : edits) {
        if (edit.Begin < cursor) {
            continue;
        }
        output.append(source.substr(cursor, edit.Begin - cursor));
        output += edit.Text;
        cursor = edit.End;
        if (&edit != &edits[accepted]) {
            edits[accepted] = std::move(edit);
        }
        ++accepted;
    }
    edits.erase(edits.begin() + accepted, edits.end());
    output.append(source.substr(cursor));
    return {std::move(output), std::move(edits), {}};
}

bool IsWhitespace(std::string_view text)
{
    return std::ranges::all_of(text, [] (unsigned char ch) {
        return std::isspace(ch) != 0;
    });
}

// Only rewrite lists whose comma-separated elements and intervening trivia
// can be moved without touching comments, directives, or nested line breaks.
std::optional<Replacement> ReflowDelimitedList(
    const parser::SyntaxTree& tree,
    const parser::Node& list,
    const Style& style,
    std::optional<size_t> callLineString = std::nullopt)
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
            ? style.ExpressionLayoutCondition ==
            ExpressionLayoutCondition::FitOrOverflow ||
            style.ExpressionLayoutCondition == ExpressionLayoutCondition::Always
            : style.MergeFunctionParameters;
    const bool onePerLine = braced
        ? style.InitializerListSplit == FunctionParameterSplit::OnePerLine
        : call
            ? style.CallArgumentSplit == CallArgumentSplit::OnePerLine
            : style.FunctionParameterSplit ==
                FunctionParameterSplit::OnePerLine;
    const bool enforceFirst = braced ? false : call
        ? style.EnforceFirstCallArgumentPosition
        : style.EnforceFirstFunctionParameterPosition;
    const bool newLineFirst = braced
        ? style.FirstInitializerElementPosition ==
            FirstInitializerElementPosition::NewLine
        : call
            ? style.FirstCallArgumentPosition ==
                FirstCallArgumentPosition::NewLine
            : style.FirstFunctionParameterPosition ==
            FirstFunctionParameterPosition::NewLine;
    const bool continuationIndent = braced
        ? style.InitializerListIndent == CallArgumentIndent::Continuation
        : call
            ? style.CallArgumentIndent == CallArgumentIndent::Continuation
            : style.FunctionParameterIndent ==
            FunctionParameterIndent::Continuation;
    const bool innerSpace = braced
        ? style.InitializerBracesSpacing == TokenSpacing::Space
        : call
            ? style.SpaceInsideCallArguments == SpaceInsideCallArguments::Space
            : style.SpaceInsideFunctionParameters ==
            SpaceInsideFunctionParameters::Space;
    const bool commaSpace = style.CommaSpacing != CommaSpacing::NoSpace;
    auto opening = list.Tokens.Begin;
    auto closing = list.Tokens.End - 1;
    if (list.Tokens.Size() < 3 ||
        tree.Kind(opening) != (braced
            ? TK::Operator_OpenBrace
            : TK::Operator_OpenParenthesis) ||
        tree.Kind(closing) != (braced
            ? TK::Operator_CloseBrace
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
        if (expectParameter
            ? child->Kind != (braced
                ? NK::InitializerElement
                : call ? NK::Argument : NK::ParameterDeclaration)
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
                if ((tree.Kind(i) == TK::Comment &&
                        tree.TokenText(i).starts_with("//")) ||
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
    const bool trailingComma = braced && expectParameter && !parameters.empty();
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
        flatWidth += parameters[i].size() + (i ? 1 + commaSpace : 0);
    }
    flatWidth += (innerSpace ? 2 : 0) + (trailingComma ? 1 : 0);
    // A newline used only for the closing delimiter does not split the
    // elements and must not make a fitting list reflow on the next pass.
    bool split = source.substr(begin, cursor - begin).find('\n') !=
        std::string_view::npos;
    // Do not split a short one-argument member call merely because a
    // following chain segment makes the physical line too long. The
    // chain's layout, not this call's argument, owns that overflow.
    if (call && parameters.size() == 1 && !split &&
        closing + 1 < tree.TokenCount() &&
        (tree.Kind(closing + 1) == TK::Operator_ClassMemberAccess ||
            tree.Kind(closing + 1) ==
                TK::Operator_IndirectClassMemberAccess) &&
        source.substr(
            openBegin,
            tree.Token(closing).Text.End - openBegin).find('\n') ==
                std::string_view::npos &&
        tree.Token(closing).Text.End - lineStart <= style.ColumnLimit)
    {
        return std::nullopt;
    }
    bool stringMoved = false;
    if (callLineString && *callLineString < parameters.size()) {
        auto* argument = list.Child();
        for (size_t i = 0; i < *callLineString; ++i) {
            argument = argument->NextSibling->NextSibling;
        }
        stringMoved = source.substr(
            begin,
            tree.Token(argument->Tokens.Begin).Text.Begin - begin)
            .find('\n') != std::string_view::npos;
    }
    if (!split && flatWidth <= style.ColumnLimit) {
        return std::nullopt;
    }
    if (call && split && !onePerLine && !stringMoved &&
        !(merge && flatWidth <= style.ColumnLimit) &&
        style.ExpressionLayoutCondition != ExpressionLayoutCondition::Always)
    {
        // Packed Overflow and FitOrOverflow retain an existing multiline
        // layout unless one of its physical lines exceeds the limit.
        // OnePerLine must normalize safe already-split calls as well.
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
            flat += commaSpace ? ", " : ",";
        }
        flat += parameter;
    }
    if (split && merge && flatWidth <= style.ColumnLimit) {
        if (trailingComma) {
            flat += ',';
        }
        if (innerSpace) {
            flat.insert(flat.begin(), ' ');
            flat.push_back(' ');
        }
        return Replacement{begin, end, std::move(flat)};
    }
    if (parameters.size() == 1 && flatWidth <= style.ColumnLimit &&
        !stringMoved)
    {
        return std::nullopt;
    }

    bool firstNew = callLineString ? false : newLineFirst;
    if (!callLineString && split && !enforceFirst &&
        (!call || style.ExpressionLayoutCondition !=
            ExpressionLayoutCondition::Always))
    {
        firstNew = source.substr(
            begin,
            tree.Token(list.Child()->Tokens.Begin).Text.Begin - begin)
            .find('\n') != std::string_view::npos;
    }
    if (!braced && !call &&
        style.FirstFunctionParameterPosition ==
            FirstFunctionParameterPosition::NameLineIfFits)
    {
        const size_t firstWidth = prefix + (innerSpace ? 1 : 0) +
            parameters.front().size() +
            (parameters.size() == 1
                ? suffix.size() + (innerSpace ? 1 : 0)
                : 1);
        if (firstWidth > style.ColumnLimit) {
            firstNew = true;
        }
    }
    size_t baseIndent = 0;
    while (lineStart + baseIndent < openBegin &&
        source[lineStart + baseIndent] == ' ')
    {
        ++baseIndent;
    }
    const size_t indent = !braced && !call && firstNew &&
        style.FunctionParameterNewLineIndent != 0
        ? baseIndent + style.FunctionParameterNewLineIndent
        : continuationIndent
            ? openBegin + 1 - lineStart
            : baseIndent + style.IndentWidth;
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
    for (size_t i = 1; i < parameters.size(); ++i) {
        bool last = i + 1 == parameters.size();
        bool overflow = column + 1 + commaSpace + parameters[i].size() +
            (last ? suffix.size() + (innerSpace ? 1 : 0) : 1) >
                style.ColumnLimit;
        bool forceSplit = split && !merge &&
            flatWidth <= style.ColumnLimit && !broke && last;
        bool breakBefore = onePerLine || overflow || forceSplit;
        if (callLineString && i <= *callLineString) {
            breakBefore = false;
        }
        result += ',';
        if (breakBefore) {
            result += '\n';
            result += continuation;
            column = indent + parameters[i].size();
            broke = true;
        } else {
            if (commaSpace) {
                result += ' ';
            }
            column += 1 + commaSpace + parameters[i].size();
        }
        result += parameters[i];
    }
    if (trailingComma) {
        result.push_back(',');
    }
    if (braced) {
        bool closeNew = style.InitializerClosingBrace ==
            InitializerClosingBrace::NewLine;
        if (closeNew) {
            result += "\n";
            result += GeneratedIndent(baseIndent, style);
        } else if (innerSpace) {
            result.push_back(' ');
        }
    } else if (call
        ? style.FunctionCallArgumentClosingParenthesis ==
            ClosingDelimiterPlacement::NewLine
        : style.FunctionParameterClosingParenthesis ==
            ClosingDelimiterPlacement::NewLine)
    {
        result += "\n" + GeneratedIndent(baseIndent, style);
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
std::string FlattenExpressionOperators(
    const parser::SyntaxTree& tree,
    bool normalizeAll,
    bool preserveTernary,
    std::vector<size_t>* mergedPositions = nullptr,
    std::vector<Replacement>* mergedEdits = nullptr)
{
    HIT_FMT_PROFILE_PHASE("formatter.expression_flatten");
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
                    bool selected = false;
                    if (node->Kind == NK::ConditionalExpression) {
                        selected = role ==
                            parser::PunctuationRole::ConditionalDelimiter;
                    } else if (node->Kind == NK::AssignmentExpression) {
                        selected = role ==
                            parser::PunctuationRole::AssignmentOperator;
                    } else {
                        selected = role ==
                                parser::PunctuationRole::BinaryOperator ||
                            role ==
                                parser::PunctuationRole::ComparisonOperator ||
                            role == parser::PunctuationRole::ShiftOperator;
                    }
                    if (selected) {
                        collect(i);
                    }
                }
                if (node->Kind == NK::ConditionalExpression) {
                    for (auto i = right->Tokens.End;
                        i < alternative->Tokens.Begin;
                        ++i)
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
        case TK::Operator_Or:
            return 2;
        case TK::Operator_And:
            return 3;
        case TK::Operator_BitOr:
            return 4;
        case TK::Operator_Xor:
            return 5;
        case TK::Operator_Ampersand:
            return 6;
        case TK::Operator_Equals:
        case TK::Operator_NotEquals:
            return 7;
        case TK::Operator_Less:
        case TK::Operator_Greater:
        case TK::Operator_LessEquals:
        case TK::Operator_GreaterEquals:
        case TK::Operator_Spaceship:
            return 8;
        case TK::Operator_LeftShift:
        case TK::Operator_RightShift:
            return 9;
        case TK::Operator_Plus:
        case TK::Operator_Minus:
            return 10;
        default:
            return 11;
    }
}

struct ExpressionBreak
{
    ExpressionBreak(size_t begin, size_t end, int priority, int nesting)
        : Begin(static_cast<util::SourceOffset>(begin))
        , End(static_cast<util::SourceOffset>(end))
        , Priority(priority)
        , Nesting(nesting)
    {
        ASSERT(begin <= util::MaxSourceSize && end <= util::MaxSourceSize)
    }

    util::SourceOffset Begin;
    util::SourceOffset End;
    int Priority;
    int Nesting;
};

// Identify both delimiters of one complete parsed conditional expression.
// A colon in a nested conditional, label, or braced initializer is not ours.
std::optional<std::pair<TokenIndex, TokenIndex>> ConditionalDelimiters(
    const parser::SyntaxTree& tree,
    const parser::Node& node)
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

void PlaceOperatorBreak(
    const parser::SyntaxTree& tree,
    TokenIndex index,
    bool leading,
    std::vector<Replacement>& edits)
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
std::optional<LayoutEdits> PlaceExistingOperatorBreaks(
    const parser::SyntaxTree& tree,
    const Style& style)
{
    HIT_FMT_PROFILE_PHASE("formatter.existing_operator_breaks");
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
                            role !=
                                parser::PunctuationRole::ComparisonOperator &&
                            role != parser::PunctuationRole::ShiftOperator))
                    {
                        continue;
                    }
                    PlaceOperatorBreak(tree, i, style.BinaryOperatorPlacement ==
                        BinaryOperatorPlacement::Leading, edits);
                }
            }
        } else if (node->Kind == NK::AssignmentExpression) {
            const auto* left = node->Child();
            const auto* right = left ? left->NextSibling : nullptr;
            if (left && right && !left->Tokens.Empty() &&
                !right->Tokens.Empty())
            {
                for (auto i = left->Tokens.End; i < right->Tokens.Begin; ++i) {
                    if (tree.Role(i) ==
                            parser::PunctuationRole::AssignmentOperator)
                    {
                        PlaceOperatorBreak(tree, i,
                            style.AssignmentOperatorPlacement ==
                                BinaryOperatorPlacement::Leading, edits);
                    }
                }
            }
        } else if (style.EnforceTernaryOperatorPlacement) {
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
    if (edits.empty()) {
        return std::nullopt;
    }
    std::ranges::sort(edits, {}, &Replacement::Begin);
    return RenderLayoutEdits(source, std::move(edits));
}

// ThreeLines leaves fitting ternaries flat. If a delimiter is already
// broken or the enclosing line overflows, break both delimiters.
std::optional<LayoutEdits> CompleteTernaryBreaks(
    const parser::SyntaxTree& tree,
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
                    ? 0
                    : lineStart + 1;
                auto lineEnd = source.find('\n', cPosition);
                if (lineEnd == std::string_view::npos) {
                    lineEnd = source.size();
                }
                auto sameLine = source.substr(
                    qPosition,
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
                        ? 0
                        : baseLine + 1;
                    size_t leading = 0;
                    while (baseLine + leading < source.size() &&
                        source[baseLine + leading] == ' ')
                    {
                        ++leading;
                    }
                    auto continuation = "\n" + GeneratedIndent(
                        leading + style.IndentWidth,
                        style);
                    const bool trailing = style.TernaryOperatorPlacement ==
                        TernaryOperatorPlacement::Trailing;
                    for (auto index : {question, colon}) {
                        if (hasBreak(index)) {
                            continue;
                        }
                        auto position = trailing
                            ? tree.Token(index).Text.End
                            : tree.Token(index).Text.Begin;
                        auto gap = trailing
                            ? tree.WhitespaceAfter(index)
                            : tree.WhitespaceBefore(index);
                        edits.push_back({trailing
                            ? position
                            : position - gap.size(), trailing
                                ? position + gap.size()
                                : position, continuation});
                    }
                }
            }
        }
        for (auto* child = node->Child(); child; child = child->NextSibling) {
            nodes.push_back(child);
        }
    }
    if (edits.empty()) {
        return std::nullopt;
    }
    std::ranges::sort(edits, {}, &Replacement::Begin);
    const auto reserve = source.size() + edits.size() * style.IndentWidth;
    return RenderLayoutEdits(source, std::move(edits), reserve);
}

// Align leading ?/: one level past the condition's line; a nested
// conditional's ':' stays at the same depth as its own '?'.
std::optional<LayoutEdits> AlignLeadingTernaryOperators(
    const parser::SyntaxTree& tree,
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
                    ? 0
                    : questionStart + 1;
                auto colonStart = source.rfind('\n', colonAt);
                colonStart = colonStart == std::string_view::npos
                    ? 0
                    : colonStart + 1;
                auto questionIndent = source.substr(
                    questionStart,
                    questionAt - questionStart);
                auto colonIndent = source.substr(
                    colonStart,
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
                    spacesOnly(questionIndent) && spacesOnly(colonIndent))
                {
                    auto conditionAt =
                        tree.Token(node->Tokens.Begin).Text.Begin;
                    auto conditionStart = source.rfind('\n', conditionAt);
                    conditionStart = conditionStart == std::string_view::npos
                        ? 0
                        : conditionStart + 1;
                    auto base = source.substr(
                        conditionStart,
                        conditionAt - conditionStart);
                    auto leading = base.find_first_not_of(' ');
                    if (leading == std::string_view::npos) {
                        leading = base.size();
                    }
                    auto required = leading + style.IndentWidth;
                    if (base.find('\t') == std::string_view::npos &&
                        questionIndent.size() < required)
                    {
                        auto newIndent = GeneratedIndent(required, style);
                        edits.push_back({questionStart, questionAt, newIndent});
                        if (colonIndent != newIndent) {
                            edits.push_back({colonStart, colonAt,
                                std::move(newIndent)});
                        }
                    } else if (questionIndent != colonIndent) {
                        edits.push_back({colonStart, colonAt,
                            std::string(questionIndent)});
                    }
                }
            }
        }
        for (auto* child = node->Child(); child; child = child->NextSibling) {
            nodes.push_back(child);
        }
    }
    if (edits.empty()) {
        return std::nullopt;
    }
    std::ranges::sort(edits, {}, &Replacement::Begin);
    return RenderLayoutEdits(source, std::move(edits));
}

// Split complete for-clauses and if/switch init-statements before trying
// expression operators. Do not infer semicolon roles from nested syntax.
std::optional<LayoutEdits> FormatControlHeaderClauses(
    const parser::SyntaxTree& tree,
    const Style& style)
{
    if (style.ControlHeaderClauseSplit == ControlHeaderClauseSplit::Preserve) {
        return std::nullopt;
    }
    const auto source = tree.Source();
    std::vector<Replacement> edits;
    std::vector<const parser::Node*> nodes{&tree.Root()};
    while (!nodes.empty()) {
        auto* node = nodes.back();
        nodes.pop_back();
        for (auto* child = node->Child(); child; child = child->NextSibling) {
            nodes.push_back(child);
        }
        if (IsIncompleteStatement(*node) || HasRecoveryChild(*node) ||
            (node->Kind != NK::ForStatement &&
                node->Kind != NK::IfStatement &&
                node->Kind != NK::SwitchStatement))
        {
            continue;
        }
        for (auto* header = node->Child();
            header;
            header = header->NextSibling)
        {
            if (header->Kind != NK::ForHeader && header->Kind !=
                NK::Condition)
            {
                continue;
            }
            if ((header->Kind == NK::ForHeader &&
                    node->Kind != NK::ForStatement) ||
                (header->Kind == NK::Condition &&
                    node->Kind == NK::ForStatement) ||
                IsIncompleteStatement(*header) ||
                HasRecoveryChild(*header) || header->Tokens.Empty())
            {
                continue;
            }
            auto open = header->Tokens.Begin;
            auto close = header->Tokens.End - 1;
            if (tree.Kind(open) != TK::Operator_OpenParenthesis ||
                tree.Kind(close) != TK::Operator_CloseParenthesis ||
                tree.MatchingDelimiter(open) != close)
            {
                continue;
            }
            std::vector<TokenIndex> semicolons;
            bool unsafe = false;
            for (auto i = open + 1; i < close; ++i) {
                auto kind = tree.Kind(i);
                if (kind == TK::Comment || kind == TK::PreprocessingDirective ||
                    tree.TokenText(i).find_first_of("\r\n\\") !=
                        std::string_view::npos)
                {
                    unsafe = true;
                    break;
                }
            }
            if (unsafe) {
                continue;
            }
            for (auto i = open + 1; i < close; ++i) {
                auto kind = tree.Kind(i);
                if (kind == TK::Operator_Semicolon) {
                    semicolons.push_back(i);
                } else if (tree.MatchingDelimiter(i) != NoToken &&
                    (kind == TK::Operator_OpenParenthesis ||
                        kind == TK::Operator_OpenBracket ||
                        kind == TK::Operator_OpenBrace))
                {
                    i = tree.MatchingDelimiter(i);
                }
            }
            const size_t expected = header->Kind == NK::ForHeader ? 2 : 1;
            if (unsafe || semicolons.size() != expected) {
                continue;
            }
            auto lineStart = source.rfind('\n', tree.Token(open).Text.Begin);
            lineStart = lineStart == std::string_view::npos ? 0 : lineStart + 1;
            auto prefix = source.substr(
                lineStart,
                tree.Token(open).Text.Begin - lineStart);
            if (prefix.find_first_of("\r\t\\") !=
                std::string_view::npos)
            {
                continue;
            }
            auto lineEnd = source.find('\n', tree.Token(close).Text.End);
            if (lineEnd != std::string_view::npos && lineEnd != 0 &&
                source[lineEnd - 1] == '\r')
            {
                continue;
            }
            auto interior = source.substr(
                tree.Token(open).Text.End,
                tree.Token(close).Text.Begin - tree.Token(open).Text.End);
            if (interior.find('\r') != std::string_view::npos ||
                (interior.find('\n') == std::string_view::npos &&
                    tree.Token(close).Text.End - lineStart <=
                        style.ColumnLimit))
            {
                continue;
            }
            size_t indent = prefix.find_first_not_of(' ');
            if (indent == std::string_view::npos) {
                indent = prefix.size();
            }
            auto continuation = style.ControlConditionIndent ==
                CallArgumentIndent::Continuation;
            if (header->Kind == NK::ForHeader &&
                style.ForHeaderClauseIndent != ForHeaderClauseIndent::Inherit)
            {
                continuation = style.ForHeaderClauseIndent ==
                    ForHeaderClauseIndent::Continuation;
            }
            indent = continuation
                ? tree.Token(open).Text.End - lineStart
                : indent + style.IndentWidth;
            std::vector<Replacement> clauseEdits;
            for (auto semi : semicolons) {
                auto next = semi + 1;
                if (next >= close ||
                    tree.Kind(next) == TK::Operator_Semicolon ||
                    tree.Kind(next) == TK::Comment ||
                    tree.Kind(next) == TK::PreprocessingDirective)
                {
                    unsafe = true;
                    break;
                }
                auto begin = tree.Token(semi).Text.End;
                auto end = tree.Token(next).Text.Begin;
                auto gap = source.substr(begin, end - begin);
                if (!IsWhitespace(gap) || gap.find('\r') !=
                        std::string_view::npos)
                {
                    unsafe = true;
                    break;
                }
                if (gap.find('\n') == std::string_view::npos) {
                    clauseEdits.push_back({begin, end,
                        "\n" + GeneratedIndent(indent, style)});
                }
            }
            if (!unsafe) {
                edits.insert(
                    edits.end(),
                    clauseEdits.begin(),
                    clauseEdits.end());
            }
        }
    }
    if (edits.empty()) {
        return std::nullopt;
    }
    std::ranges::sort(edits, {}, &Replacement::Begin);
    return RenderLayoutEdits(source, std::move(edits));
}

std::optional<LayoutEdits> FormatExpressions(
    const parser::SyntaxTree& tree,
    const Style& style,
    bool correctExisting = true)
{
    HIT_FMT_PROFILE_PHASE("formatter.expression_layout");
    if (style.ExpressionLayoutCondition ==
        ExpressionLayoutCondition::Overflow &&
        correctExisting)
    {
        if (auto placed = PlaceExistingOperatorBreaks(tree, style)) {
            auto rebound = TryRebindLayout(tree, *placed);
            const auto length = placed->Output.size();
            placed->Output.append(util::SourcePaddingSize, '\0');
            if (!rebound) {
                HIT_FMT_PROFILE_EVENT("formatter.unsafe_expression_skipped");
                return FormatExpressions(tree, style, false);
            }
            auto result = FormatExpressions(*rebound, style, false);
            if (result) {
                return LayoutEdits{std::move(result->Output), {}};
            }
            placed->Output.resize(length);
            return LayoutEdits{std::move(placed->Output), {}};
        }
    }
    if (style.ExpressionLayoutCondition == ExpressionLayoutCondition::Always ||
        style.ExpressionLayoutCondition ==
            ExpressionLayoutCondition::FitOrOverflow)
    {
        const bool always = style.ExpressionLayoutCondition ==
            ExpressionLayoutCondition::Always;
        std::vector<size_t> mergedPositions;
        std::vector<Replacement> mergedEdits;
        auto flattened = FlattenExpressionOperators(
            tree,
            always,
            style.TernarySplit == TernarySplit::Preserve,
            always ? nullptr : &mergedPositions,
            always ? nullptr : &mergedEdits);
        Style overflowStyle = style;
        overflowStyle.ExpressionLayoutCondition =
            ExpressionLayoutCondition::Overflow;
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
                    ? flattened.size()
                    : lineEnd;
                if (end - lineStart <= style.ColumnLimit) {
                    const auto& edit = mergedEdits[i];
                    partial.append(tree.Source().substr(
                        from,
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
        util::PaddedSourceView flattenedSource{
            .Data = flattened.data(), .Size = length};
        auto rebound = tree.RebindWhitespaceGaps(flattenedSource);
        if (!rebound) {
            rebound = tree.RebindMatchingTokens(flattenedSource);
        }
        if (!rebound) {
            HIT_FMT_PROFILE_EVENT("formatter.unsafe_expression_skipped");
            return FormatExpressions(tree, overflowStyle);
        }
        auto result = FormatExpressions(*rebound, overflowStyle);
        if (result) {
            return LayoutEdits{std::move(result->Output), {}};
        }
        flattened.resize(length);
        return LayoutEdits{std::move(flattened), {}};
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
                    bool selected = false;
                    if (node->Kind == NK::ConditionalExpression) {
                        selected = role ==
                            parser::PunctuationRole::ConditionalDelimiter;
                    } else if (node->Kind == NK::AssignmentExpression) {
                        selected = role ==
                            parser::PunctuationRole::AssignmentOperator;
                    } else {
                        selected = role ==
                                parser::PunctuationRole::BinaryOperator ||
                            role ==
                                parser::PunctuationRole::ComparisonOperator ||
                            role == parser::PunctuationRole::ShiftOperator;
                    }
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
                        : node->Kind == NK::AssignmentExpression
                            ? style.AssignmentOperatorPlacement ==
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
                        node->Kind == NK::AssignmentExpression
                            ? 0
                            : node->Kind == NK::ConditionalExpression
                                ? 1
                                : ExpressionPriority(tree.Kind(
                                    index)), nesting[index]});
                }
                if (node->Kind == NK::ConditionalExpression) {
                    for (auto index = right->Tokens.End;
                        index < alternative->Tokens.Begin;
                        ++index)
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
                        auto gap = trailing
                            ? tree.WhitespaceAfter(index)
                            : tree.WhitespaceBefore(index);
                        if (IsWhitespace(gap) &&
                            gap.find_first_of("\r\n") ==
                                std::string_view::npos)
                        {
                            auto offset = trailing
                                ? tree.Token(index).Text.End
                                : tree.Token(index).Text.Begin;
                            breaks.push_back({trailing
                                ? offset
                                : offset - gap.size(), trailing
                                    ? offset + gap.size()
                                    : offset, 1,
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
        return std::nullopt;
    }
    std::ranges::sort(breaks, {}, &ExpressionBreak::Begin);
    struct ConditionIndent
    {
        ConditionIndent(size_t begin, size_t end, size_t column)
            : Begin(static_cast<util::SourceOffset>(begin))
            , End(static_cast<util::SourceOffset>(end))
            , Column(static_cast<util::SourceOffset>(column))
        {
            ASSERT(begin <= util::MaxSourceSize &&
                end <= util::MaxSourceSize && column <= util::MaxSourceSize)
        }

        util::SourceOffset Begin;
        util::SourceOffset End;
        util::SourceOffset Column;
    };
    std::vector<ConditionIndent> conditions;
    if (style.ControlConditionIndent == CallArgumentIndent::Continuation ||
        style.CallArgumentIndent == CallArgumentIndent::Continuation)
    {
        std::vector<const parser::Node*> headers{&tree.Root()};
        while (!headers.empty()) {
            auto* node = headers.back();
            headers.pop_back();
            if (IsIncompleteStatement(*node) || HasRecoveryChild(*node)) {
                continue;
            }
            if (style.ControlConditionIndent ==
                    CallArgumentIndent::Continuation &&
                (node->Kind == NK::IfStatement ||
                    node->Kind == NK::ForStatement ||
                    node->Kind == NK::WhileStatement ||
                    node->Kind == NK::DoWhileStatement ||
                    node->Kind == NK::SwitchStatement ||
                    node->Kind == NK::CatchClause))
            {
                for (auto* child = node->Child();
                    child;
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
            if (style.CallArgumentIndent ==
                    CallArgumentIndent::Continuation &&
                node->Kind == NK::CallExpression)
            {
                const auto* callee = node->Child();
                const auto* arguments = callee ? callee->NextSibling : nullptr;
                if (callee && arguments && !callee->Tokens.Empty() &&
                    arguments->Kind == NK::ArgumentList &&
                    !arguments->Tokens.Empty() &&
                    callee->Kind != NK::UnparsedSyntax &&
                    callee->Kind != NK::Expression &&
                    callee->Kind != NK::ParenthesizedExpression)
                {
                    auto open = arguments->Tokens.Begin;
                    auto close = tree.MatchingDelimiter(open);
                    if (open > 0 && close != NoToken &&
                        close < arguments->Tokens.End &&
                        tree.Kind(open) == TK::Operator_OpenParenthesis &&
                        tree.Kind(open - 1) != TK::Keyword_requires)
                    {
                        auto offset = tree.Token(open).Text.Begin;
                        auto line = source.rfind('\n', offset);
                        line = line == std::string_view::npos ? 0 : line + 1;
                        if (source.substr(line, offset - line).find('\t') ==
                                std::string_view::npos)
                        {
                            conditions.push_back({tree.Token(open).Text.End,
                                tree.Token(close).Text.Begin,
                                offset - line + 1});
                        }
                    }
                }
            }
            for (auto* child = node->Child();
                child;
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
    auto continuationColumn = [&] (size_t position, size_t leading) {
        auto it = std::ranges::lower_bound(
            breaks,
            position,
            {},
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
            index < tree.TokenCount() && tree.Token(index).Text.Begin < lineEnd;
            ++index)
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
                                (candidate.Priority ==
                                    breaks[choice].Priority &&
                                    candidate.Begin > breaks[choice].Begin))))
                    {
                        choice = i;
                    }
                }
                if (choice == stop) {
                    break;
                }
                const auto& selected = breaks[choice];
                const auto newIndent = continuationColumn(
                    selected.Begin,
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
    if (edits.empty()) {
        return std::nullopt;
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
    return LayoutEdits{std::move(result), std::move(edits)};
}

// Build comments only for unambiguous namespace headers and matched braces.
// Never infer a name from a recovered declaration or a namespace alias.
std::optional<std::pair<TokenIndex, std::string>> NamespaceClosing(
    const parser::SyntaxTree& tree,
    const parser::Node& node)
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

std::vector<Replacement> CollectNamespaceCommentEdits(
    const parser::SyntaxTree& tree,
    NamespaceComments policy,
    size_t shortNamespaceLines,
    size_t spaces,
    TokenSpacing trailingSpacing)
{
    // A later trailing-comment pass would normalize the generated gap.
    // Apply that same rule here so namespace comments can be emitted last.
    if (trailingSpacing == TokenSpacing::Space) {
        spaces = 1;
    } else if (trailingSpacing == TokenSpacing::NoSpace) {
        spaces = 0;
    }
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
                if (lineComment && policy != NamespaceComments::AddMissing) {
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
                    std::string replacement;
                    if (policy != NamespaceComments::Remove) {
                        replacement = std::string(spaces, ' ') +
                            "// namespace";
                        if (!closing->second.empty()) {
                            replacement += " " + closing->second;
                        }
                    }
                    if (source.substr(begin, end - begin) != replacement) {
                        edits.push_back({begin, end, std::move(replacement)});
                    }
                } else if ((policy == NamespaceComments::AddMissing ||
                        policy == NamespaceComments::Fix) && !lineComment &&
                    !shortNamespace &&
                    (next == tree.TokenCount() || !sameLine ||
                        tree.Kind(next) != TK::Comment))
                {
                    std::string replacement(spaces, ' ');
                    replacement += "// namespace";
                    if (!closing->second.empty()) {
                        replacement += " " + closing->second;
                    }
                    if (sameLine && next < tree.TokenCount()) {
                        // A line comment must not swallow the next token.
                        auto line = source.rfind('\n', begin);
                        auto start = line == std::string_view::npos
                            ? 0
                            : line + 1;
                        auto prefix = source.substr(
                            start,
                            tree.Token(index).Text.Begin - start);
                        auto newline = source.rfind('\n', begin);
                        if (newline == std::string_view::npos) {
                            newline = source.find('\n', begin);
                        }
                        const bool crlf = newline != std::string_view::npos &&
                            newline != 0 && source[newline - 1] == '\r';
                        replacement += crlf ? "\r\n" : "\n";
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
                                ? gap.size()
                                : lineEnd),
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
    return edits;
}

std::optional<LayoutEdits> AttachElseToBrace(
    const parser::SyntaxTree& tree)
{
    auto source = tree.Source();
    std::vector<Replacement> edits;
    std::vector<const parser::Node*> nodes{&tree.Root()};
    while (!nodes.empty()) {
        auto* node = nodes.back();
        nodes.pop_back();
        if (node->Kind == NK::IfStatement && !HasRecoveryChild(*node)) {
            for (auto* child = node->Child();
                child;
                child = child->NextSibling)
            {
                if (child->Kind != NK::CompoundStatement ||
                    child->Tokens.Empty() ||
                    child->Tokens.End >= tree.TokenCount() ||
                    tree.Kind(child->Tokens.End - 1) !=
                        TK::Operator_CloseBrace ||
                    tree.Kind(child->Tokens.End) != TK::Keyword_else)
                {
                    continue;
                }
                auto begin = tree.Token(child->Tokens.End - 1).Text.End;
                auto end = tree.Token(child->Tokens.End).Text.Begin;
                auto gap = source.substr(begin, end - begin);
                if (IsWhitespace(gap) &&
                    gap.find('\r') == std::string_view::npos && gap != " ")
                {
                    edits.push_back({begin, end, " "});
                }
            }
        }
        for (auto* child = node->Child(); child; child = child->NextSibling) {
            nodes.push_back(child);
        }
    }
    if (edits.empty()) {
        return std::nullopt;
    }
    std::ranges::sort(edits, {}, &Replacement::Begin);
    return RenderLayoutEdits(source, std::move(edits));
}

std::optional<LayoutEdits> CollapseEmptyFunctionBodies(
    const parser::SyntaxTree& tree)
{
    auto source = tree.Source();
    std::vector<Replacement> edits;
    std::vector<const parser::Node*> nodes{&tree.Root()};
    while (!nodes.empty()) {
        auto* node = nodes.back();
        nodes.pop_back();
        if (node->Kind == NK::FunctionDefinition && !HasRecoveryChild(*node)) {
            for (auto* child = node->Child();
                child;
                child = child->NextSibling)
            {
                if (child->Kind != NK::CompoundStatement ||
                    child->Tokens.End != child->Tokens.Begin + 2 ||
                    tree.Kind(child->Tokens.Begin) != TK::Operator_OpenBrace ||
                    tree.Kind(child->Tokens.End - 1) != TK::Operator_CloseBrace)
                {
                    continue;
                }
                auto begin = tree.Token(child->Tokens.Begin).Text.End;
                auto end = tree.Token(child->Tokens.End - 1).Text.Begin;
                auto gap = source.substr(begin, end - begin);
                if (IsWhitespace(gap) && gap.find('\r') ==
                        std::string_view::npos && gap != " ")
                {
                    edits.push_back({begin, end, " "});
                }
            }
        }
        for (auto* child = node->Child(); child; child = child->NextSibling) {
            nodes.push_back(child);
        }
    }
    if (edits.empty()) {
        return std::nullopt;
    }
    std::ranges::sort(edits, {}, &Replacement::Begin);
    return RenderLayoutEdits(source, std::move(edits));
}

// Remove only complete, standalone null statements in compound bodies.
// Control bodies (if (x);, while (x);, etc.) and declaration terminators
// have different syntactic owners and must remain untouched.
struct NullStatementRemoval
{
    std::string Output;
    std::vector<TokenIndex> Removed;
};

std::optional<NullStatementRemoval> RemoveNullStatements(
    const parser::SyntaxTree& tree)
{
    const auto source = tree.Source();
    std::vector<std::pair<Replacement, TokenIndex>> edits;
    std::vector<std::pair<const parser::Node*, NK>> nodes{
        {&tree.Root(), tree.Root().Kind}};
    while (!nodes.empty()) {
        auto [node, parentKind] = nodes.back();
        nodes.pop_back();
        for (auto* child = node->Child(); child; child = child->NextSibling) {
            nodes.emplace_back(child, node->Kind);
        }
        if (node->Kind != NK::CompoundStatement ||
            parentKind == NK::Argument || HasRecoveryChild(*node) ||
            IsIncompleteStatement(*node))
        {
            continue;
        }
        bool directives = false;
        for (auto* child = node->Child(); child; child = child->NextSibling) {
            directives |= child->Kind == NK::PreprocessingDirective ||
                child->Kind == NK::PreprocessingConditional;
        }
        if (directives) {
            continue;
        }
        const parser::Node* previous = nullptr;
        for (auto* child = node->Child(); child; child = child->NextSibling) {
            if (child->Kind == NK::ExpressionStatement &&
                child->Tokens.End == child->Tokens.Begin + 1 &&
                tree.Kind(child->Tokens.Begin) == TK::Operator_Semicolon &&
                (!previous || (previous->Kind != NK::CaseLabel &&
                    previous->Kind != NK::LabelStatement)))
            {
                const auto begin = tree.Token(child->Tokens.Begin).Text.Begin;
                const auto end = tree.Token(child->Tokens.Begin).Text.End;
                auto lineStart = source.rfind('\n', begin);
                lineStart = lineStart == std::string_view::npos
                    ? 0
                    : lineStart + 1;
                auto lineEnd = source.find('\n', end);
                if (lineEnd == std::string_view::npos) {
                    lineEnd = source.size();
                }
                auto suffix = source.substr(end, lineEnd - end);
                if (suffix.find_first_not_of(" \t") ==
                        std::string_view::npos &&
                    source.substr(lineStart, lineEnd - lineStart).find('\r') ==
                        std::string_view::npos)
                {
                    auto prefix = source.substr(lineStart, begin - lineStart);
                    if (prefix.find_first_not_of(" \t") ==
                            std::string_view::npos)
                    {
                        edits.push_back({{lineStart,
                            std::min(source.size(), lineEnd + 1), ""},
                            child->Tokens.Begin});
                    } else {
                        edits.push_back({{begin, end, ""},
                            child->Tokens.Begin});
                    }
                }
            }
            if (child->Kind != NK::Comment) {
                previous = child;
            }
        }
    }
    if (edits.empty()) {
        return std::nullopt;
    }
    std::ranges::sort(edits, {}, [] (const auto& edit) {
        return edit.first.Begin;
    });
    NullStatementRemoval result;
    result.Output.reserve(source.size());
    size_t cursor = 0;
    for (const auto& [edit, index] : edits) {
        if (edit.Begin < cursor) {
            continue;
        }
        result.Output.append(source.substr(cursor, edit.Begin - cursor));
        result.Output += edit.Text;
        result.Removed.push_back(index);
        cursor = edit.End;
    }
    result.Output.append(source.substr(cursor));
    return result;
}

// Keep nonempty parsed blocks multiline without reshaping lambda bodies or
// moving comments, directives, and ambiguous statements.
std::optional<LayoutEdits> FormatNonEmptyBlockBodies(
    const parser::SyntaxTree& tree,
    const Style& style)
{
    if (style.NonEmptyBlockBodyLayout != NonEmptyBlockBodyLayout::Multiline) {
        return std::nullopt;
    }
    const auto source = tree.Source();
    std::vector<Replacement> edits;
    std::vector<std::pair<const parser::Node*, NK>> nodes{
        {&tree.Root(), tree.Root().Kind}};
    while (!nodes.empty()) {
        auto [node, parentKind] = nodes.back();
        nodes.pop_back();
        for (auto* child = node->Child(); child; child = child->NextSibling) {
            nodes.emplace_back(child, node->Kind);
        }
        if (node->Kind != NK::CompoundStatement ||
            parentKind == NK::Argument || !node->Child() ||
            HasRecoveryChild(*node) || IsIncompleteStatement(*node) ||
            node->Tokens.End < node->Tokens.Begin + 3)
        {
            continue;
        }
        auto open = node->Tokens.Begin;
        auto close = node->Tokens.End - 1;
        if (tree.Kind(open) != TK::Operator_OpenBrace ||
            tree.Kind(close) != TK::Operator_CloseBrace ||
            tree.MatchingDelimiter(open) != close)
        {
            continue;
        }
        auto line = source.rfind('\n', tree.Token(open).Text.Begin);
        line = line == std::string_view::npos ? 0 : line + 1;
        auto prefix = source.substr(line, tree.Token(open).Text.Begin - line);
        auto leading = prefix.find_first_not_of(" \t");
        auto base = std::string(prefix.substr(0, leading));
        auto interior = base + GeneratedIndentAfter(base,
            style.BlockBodyIndentWidth
                ? style.BlockBodyIndentWidth
                : style.IndentWidth, style);
        std::vector<Replacement> bodyEdits;
        auto previous = tree.Token(open).Text.End;
        bool safe = true;
        for (auto* child = node->Child(); child; child = child->NextSibling) {
            if (child->Tokens.Empty() || HasRecoveryChild(*child) ||
                IsIncompleteStatement(*child) ||
                child->Kind == NK::Comment ||
                child->Kind == NK::PreprocessingDirective)
            {
                safe = false;
                break;
            }
            auto begin = tree.Token(child->Tokens.Begin).Text.Begin;
            if (begin < previous || child->Tokens.End > close) {
                safe = false;
                break;
            }
            auto gap = source.substr(previous, begin - previous);
            if (!IsWhitespace(gap) ||
                gap.find('\r') != std::string_view::npos)
            {
                safe = false;
                break;
            }
            if (gap.find('\n') == std::string_view::npos) {
                bodyEdits.push_back({previous, begin, "\n" + interior});
            }
            for (auto i = child->Tokens.Begin; i < child->Tokens.End; ++i) {
                if (tree.Kind(i) == TK::Comment ||
                    tree.Kind(i) == TK::PreprocessingDirective)
                {
                    safe = false;
                    break;
                }
            }
            if (!safe) {
                break;
            }
            previous = tree.Token(child->Tokens.End - 1).Text.End;
        }
        if (!safe || previous > tree.Token(close).Text.Begin) {
            continue;
        }
        auto gap = source.substr(
            previous,
            tree.Token(close).Text.Begin - previous);
        if (!IsWhitespace(gap) || gap.find('\r') != std::string_view::npos) {
            continue;
        }
        if (!bodyEdits.empty() &&
            style.BlockClosingBrace == ClosingDelimiterPlacement::NewLine &&
            gap.find('\n') == std::string_view::npos)
        {
            bodyEdits.push_back({previous, tree.Token(close).Text.Begin,
                "\n" + base});
        }
        edits.insert(edits.end(), bodyEdits.begin(), bodyEdits.end());
    }
    if (edits.empty()) {
        return std::nullopt;
    }
    std::ranges::sort(edits, {}, &Replacement::Begin);
    return RenderLayoutEdits(source, std::move(edits));
}

// Add braces only around complete, trivia-free controlled statements. Use
// innermost-first passes so nested unbraced controls keep their else binding.
struct ControlBracesResult
{
    std::string Output;
    std::vector<parser::InsertedControlBlock> Blocks;
};

std::optional<ControlBracesResult> AddControlBraces(
    const parser::SyntaxTree& tree,
    const Style& style)
{
    auto source = tree.Source();
    std::vector<Replacement> edits;
    std::vector<parser::InsertedControlBlock> blocks;
    std::vector<const parser::Node*> nodes{&tree.Root()};
    while (!nodes.empty()) {
        auto* node = nodes.back();
        nodes.pop_back();
        if (HasRecoveryChild(*node) || IsIncompleteStatement(*node)) {
            continue;
        }
        const bool control = node->Kind == NK::IfStatement ||
            node->Kind == NK::ForStatement ||
            node->Kind == NK::WhileStatement ||
            node->Kind == NK::SwitchStatement ||
            node->Kind == NK::DoWhileStatement;
        bool unbracedSwitchCases = false;
        if (node->Kind == NK::SwitchStatement) {
            for (auto* child = node->Child();
                child;
                child = child->NextSibling)
            {
                unbracedSwitchCases |= child->Kind == NK::CaseLabel;
            }
        }
        bool hasAttributes = false;
        if (control) {
            for (auto* child = node->Child();
                child;
                child = child->NextSibling)
            {
                hasAttributes |= child->Kind == NK::AttributeSpecifier ||
                    child->Kind == NK::ExtensionAttributeSpecifier;
            }
        }
        if (control && !unbracedSwitchCases && !hasAttributes) {
            for (auto* child = node->Child();
                child;
                child = child->NextSibling)
            {
                if (child->Kind == NK::Condition ||
                    child->Kind == NK::ForHeader ||
                    child->Kind == NK::CompoundStatement ||
                    child->Kind == NK::CaseLabel ||
                    child->Kind == NK::Comment ||
                    child->Kind == NK::PreprocessingDirective ||
                    child->Tokens.Empty())
                {
                    continue;
                }
                auto start = child->Tokens.Begin;
                const bool nullBody = child->Kind == NK::ExpressionStatement &&
                    child->Tokens.End == start + 1 &&
                    start < tree.TokenCount() &&
                    tree.Kind(start) == TK::Operator_Semicolon;
                if ((nullBody && style.EmptyControlBody ==
                        EmptyControlBody::Preserve) ||
                    (!nullBody && !style.AddMissingControlBraces))
                {
                    continue;
                }
                if (start == 0 || start >= tree.TokenCount() ||
                    (child->Kind == NK::IfStatement &&
                        tree.Kind(start - 1) == TK::Keyword_else))
                {
                    continue;
                }
                auto header = start - 1;
                if (tree.Kind(header) == TK::Comment ||
                    tree.Kind(header) == TK::PreprocessingDirective)
                {
                    continue;
                }
                if (child->Kind == NK::IfStatement ||
                    child->Kind == NK::ForStatement ||
                    child->Kind == NK::WhileStatement ||
                    child->Kind == NK::DoWhileStatement)
                {
                    bool missing = false;
                    for (auto* grandchild = child->Child();
                        grandchild;
                        grandchild = grandchild->NextSibling)
                    {
                        if (grandchild->Kind != NK::Condition &&
                            grandchild->Kind != NK::ForHeader &&
                            grandchild->Kind != NK::CompoundStatement &&
                            grandchild->Kind != NK::Comment &&
                            grandchild->Kind != NK::PreprocessingDirective)
                        {
                            missing = true;
                        }
                    }
                    if (missing && tree.WhitespaceBefore(start).find('\n') !=
                            std::string_view::npos)
                    {
                        continue;
                    }
                }
                auto before = tree.Token(header).Text.End;
                auto begin = tree.Token(start).Text.Begin;
                auto end = tree.Token(child->Tokens.End - 1).Text.End;
                if (!IsWhitespace(source.substr(before, begin - before))) {
                    continue;
                }
                bool safe = true;
                for (auto i = start; i < child->Tokens.End; ++i) {
                    if (tree.Kind(i) == TK::Comment ||
                        tree.Kind(i) == TK::PreprocessingDirective)
                    {
                        safe = false;
                        break;
                    }
                }
                auto lineEnd = source.find('\n', end);
                if (lineEnd == std::string_view::npos) {
                    lineEnd = source.size();
                }
                auto nodeBegin = tree.Token(node->Tokens.Begin).Text.Begin;
                auto nodeEnd = tree.Token(node->Tokens.End - 1).Text.End;
                if (!safe || source.substr(end, lineEnd - end).find("//") !=
                        std::string_view::npos ||
                    source.substr(nodeBegin, nodeEnd - nodeBegin).find('\r') !=
                        std::string_view::npos ||
                    (lineEnd > 0 && source[lineEnd - 1] == '\r'))
                {
                    continue;
                }
                auto headerLine = source.rfind(
                    '\n',
                    tree.Token(node->Tokens.Begin).Text.Begin);
                headerLine = headerLine == std::string_view::npos
                    ? 0
                    : headerLine + 1;
                auto headerPrefix = source.substr(
                    headerLine,
                    tree.Token(node->Tokens.Begin).Text.Begin - headerLine);
                auto firstText = headerPrefix.find_first_not_of(" \t");
                auto base = std::string(headerPrefix.substr(0, firstText));
                auto bodyIndent = base + GeneratedIndentAfter(
                    base,
                    style.IndentWidth,
                    style);
                auto gap = source.substr(before, begin - before);
                auto headerBegin = tree.Token(node->Tokens.Begin).Text.Begin;
                bool multilineHeader = source.substr(
                    headerBegin,
                    before - headerBegin).find('\n') !=
                        std::string_view::npos;
                if (nullBody) {
                    bool separate = style.ControlBlockBracePlacement ==
                            ControlBlockBracePlacement::NewLine ||
                        (style.ControlBlockBracePlacement ==
                            ControlBlockBracePlacement::AttachUnlessMultiline &&
                            multilineHeader) ||
                        gap.find('\n') != std::string_view::npos;
                    auto interior = style.EmptyBlockInteriorSpacing ==
                        TokenSpacing::Space ? " " : "";
                    std::string beforeBrace;
                    if (separate) {
                        beforeBrace = "\n" + base;
                    } else if (style.ControlBlockSpaceBeforeBrace !=
                        TokenSpacing::NoSpace)
                    {
                        beforeBrace = " ";
                    }
                    edits.push_back({before, end,
                        beforeBrace + "{" + interior + "}"});
                    blocks.push_back({child, true});
                    continue;
                }
                bool separateOpening = style.ControlBlockBracePlacement ==
                    ControlBlockBracePlacement::NewLine ||
                    (multilineHeader && style.ControlBlockBracePlacement ==
                        ControlBlockBracePlacement::AttachUnlessMultiline);
                std::string opening;
                if (separateOpening) {
                    opening = "\n" + base + "{";
                } else if (style.ControlBlockSpaceBeforeBrace ==
                    TokenSpacing::NoSpace)
                {
                    opening = "{";
                } else {
                    opening = " {";
                }
                opening += gap.find('\n') == std::string_view::npos
                    ? "\n" + bodyIndent
                    : std::string(gap);
                edits.push_back({before, begin, std::move(opening)});
                auto closing = style.BlockClosingBrace ==
                        ClosingDelimiterPlacement::SameLine
                    ? " }"
                    : "\n" + base + "}";
                auto closingEnd = end;
                if (child->Tokens.End < tree.TokenCount()) {
                    auto next = child->Tokens.End;
                    auto nextBegin = tree.Token(next).Text.Begin;
                    if (nextBegin <= lineEnd &&
                        tree.Kind(next) != TK::Keyword_else &&
                        !(node->Kind == NK::DoWhileStatement &&
                            tree.Kind(next) == TK::Keyword_while) &&
                        IsWhitespace(source.substr(end, nextBegin - end)))
                    {
                        closingEnd = nextBegin;
                        closing += "\n" + base;
                    }
                }
                edits.push_back({end, closingEnd, std::move(closing)});
                blocks.push_back({child, false});
            }
        }
        for (auto* child = node->Child(); child; child = child->NextSibling) {
            nodes.push_back(child);
        }
    }
    if (edits.empty()) {
        return std::nullopt;
    }
    std::ranges::sort(edits, {}, &Replacement::Begin);
    ControlBracesResult result;
    result.Blocks = std::move(blocks);
    size_t cursor = 0;
    for (const auto& edit : edits) {
        if (edit.Begin < cursor) {
            continue;
        }
        result.Output.append(source.substr(cursor, edit.Begin - cursor));
        result.Output += edit.Text;
        cursor = edit.End;
    }
    result.Output.append(source.substr(cursor));
    return result;
}

// Reindent statement starts, not arbitrary physical lines in expressions or
// comments. Preserve recovered blocks and lines containing directives.
std::optional<LayoutEdits> FormatBlockIndentation(
    const parser::SyntaxTree& tree,
    const Style& style)
{
    auto source = tree.Source();
    char scratchBuffer[8192];
    std::pmr::monotonic_buffer_resource scratch(
        scratchBuffer, sizeof(scratchBuffer));
    std::pmr::vector<Replacement> edits{&scratch};
    // Conditional branches may have different syntactic owners. Keep their
    // existing indentation rather than inferring layout across #if arms.
    std::pmr::vector<bool> conditional(tree.TokenCount(), false, &scratch);
    size_t depth = 0;
    for (TokenIndex i = 0; i < tree.TokenCount(); ++i) {
        if (tree.Kind(i) == TK::PreprocessingDirective) {
            auto text = tree.TokenText(i);
            auto hash = text.find('#');
            if (hash != std::string_view::npos) {
                auto word = text.substr(hash + 1);
                word.remove_prefix(std::min(
                    word.find_first_not_of(" \t"),
                    word.size()));
                auto end = word.find_first_of(" \t\r\n");
                word = word.substr(0, end);
                if (word == "if" || word == "ifdef" || word == "ifndef") {
                    ++depth;
                } else if (word == "endif" && depth) {
                    --depth;
                }
            }
        }
        conditional[i] = depth != 0;
    }
    auto mark = [&] (TokenIndex index, std::string_view indent,
        bool allowComment = false) {
            if (index >= tree.TokenCount() || conditional[index] ||
            (tree.Kind(index) == TK::Comment && !allowComment) ||
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
    auto isStatementAttribute = [&] (TokenIndex index) {
        return index < tree.TokenCount() &&
            tree.Kind(index) == TK::Operator_OpenBracket &&
            tree.Role(index) == parser::PunctuationRole::AttributeDelimiter;
    };
    auto lineIndent = [&] (TokenIndex index) -> std::string {
        auto offset = tree.Token(index).Text.Begin;
        auto line = source.rfind('\n', offset);
        line = line == std::string_view::npos ? 0 : line + 1;
        auto prefix = source.substr(line, offset - line);
        auto nonSpace = prefix.find_first_not_of(" \t");
        return std::string(prefix.substr(0, nonSpace));
    };
    auto originalIndent = [&] (TokenIndex index) -> std::string {
        auto offset = tree.Token(index).Text.Begin;
        auto line = source.rfind('\n', offset);
        line = line == std::string_view::npos ? 0 : line + 1;
        auto prefix = source.substr(line, offset - line);
        return prefix.find_first_not_of(" \t") == std::string_view::npos
            ? std::string(prefix)
            : std::string{};
    };
    // Only standalone, argument-free member calls or unambiguous arguments
    // of standalone wrappers can delimit visual scopes.
    enum class ScopeCallForm { Dot, Arrow, Wrapped };
    struct ScopeCall
    {
        std::string_view Receiver;
        ScopeCallForm Form;
        std::string_view Method;
        TokenIndex Begin = NoToken;
        TokenIndex End = NoToken;
        TokenIndex MethodIndex = NoToken;
    };
    auto configuredMethod = [&] (std::string_view method) {
        for (const auto& pair : style.IndentedCallScopePairs) {
            auto separator = pair.find(':');
            if (method == std::string_view(pair).substr(0, separator) ||
                method == std::string_view(pair).substr(separator + 1))
            {
                return true;
            }
        }
        return false;
    };
    auto scopeCall = [&] (const parser::Node* statement)
        -> std::optional<ScopeCall> {
        if (!statement || statement->Kind != NK::ExpressionStatement ||
            HasRecoveryChild(*statement))
        {
            return std::nullopt;
        }
        auto begin = statement->Tokens.Begin;
        auto end = statement->Tokens.End;
        if (tree.Kind(begin) != TK::Identifier) {
            return std::nullopt;
        }
        if (end == begin + 6 &&
            (tree.Kind(begin + 1) == TK::Operator_ClassMemberAccess ||
                tree.Kind(begin + 1) ==
                    TK::Operator_IndirectClassMemberAccess) &&
            tree.Kind(begin + 2) == TK::Identifier &&
            tree.Kind(begin + 3) == TK::Operator_OpenParenthesis &&
            tree.Kind(begin + 4) == TK::Operator_CloseParenthesis &&
            tree.Kind(begin + 5) == TK::Operator_Semicolon)
        {
            return ScopeCall{tree.TokenText(begin),
                tree.Kind(begin + 1) == TK::Operator_ClassMemberAccess
                    ? ScopeCallForm::Dot : ScopeCallForm::Arrow,
                tree.TokenText(begin + 2)};
        }
        if (end < begin + 7 ||
            tree.Kind(begin + 1) != TK::Operator_OpenParenthesis ||
            tree.Kind(end - 2) != TK::Operator_CloseParenthesis ||
            tree.Kind(end - 1) != TK::Operator_Semicolon ||
            tree.MatchingDelimiter(begin + 1) != end - 2)
        {
            return std::nullopt;
        }
        auto closing = end - 2;
        std::optional<TokenIndex> methodIndex;
        for (auto argBegin = begin + 2; argBegin < closing;) {
            auto argEnd = argBegin;
            while (argEnd < closing && tree.Kind(
                argEnd) != TK::Operator_Comma)
            {
                auto matched = tree.MatchingDelimiter(argEnd);
                argEnd = matched != NoToken && matched > argEnd &&
                        matched < closing
                    ? matched + 1
                    : argEnd + 1;
            }
            if (argEnd == argBegin + 3 &&
                tree.Kind(argBegin) == TK::Identifier &&
                tree.Kind(argBegin + 1) == TK::Operator_OpenParenthesis &&
                tree.Kind(argBegin + 2) == TK::Operator_CloseParenthesis &&
                tree.MatchingDelimiter(argBegin + 1) == argBegin + 2 &&
                configuredMethod(tree.TokenText(argBegin)))
            {
                if (methodIndex) {
                    return std::nullopt;
                }
                methodIndex = argBegin;
            }
            argBegin = argEnd + 1;
        }
        if (!methodIndex) {
            return std::nullopt;
        }
        return ScopeCall{{}, ScopeCallForm::Wrapped,
            tree.TokenText(*methodIndex), begin, end, *methodIndex};
    };
    auto scopeInteriorStatement = [&] (const parser::Node* statement) {
        if (!statement || statement->Tokens.Empty() ||
            HasRecoveryChild(*statement))
        {
            return false;
        }
        if (statement->Kind == NK::ExpressionStatement ||
            statement->Kind == NK::DeclarationStatement)
        {
            return true;
        }
        // A direct-initialized variable with an unresolved type can be
        // ambiguous to the parser, but is still a complete statement.
        return statement->Kind == NK::AmbiguousStatement &&
            tree.Kind(statement->Tokens.End - 1) == TK::Operator_Semicolon;
    };
    auto sameScopeRoot = [&] (const ScopeCall& lhs,
        const ScopeCall& rhs) {
            if (lhs.Form != rhs.Form) {
                return false;
            }
            if (lhs.Form == ScopeCallForm::Wrapped) {
                return tree.TokenText(lhs.Begin) == tree.TokenText(rhs.Begin);
            }
            return lhs.Receiver == rhs.Receiver;
        };
    auto sameScopeShape = [&] (const ScopeCall& lhs,
        const ScopeCall& rhs) {
            if (lhs.Form != rhs.Form) {
                return false;
            }
            if (lhs.Form != ScopeCallForm::Wrapped) {
                return lhs.Receiver == rhs.Receiver;
            }
        // Every wrapper token except the configured method must match.
            auto length = lhs.End - lhs.Begin;
            if (length != rhs.End - rhs.Begin ||
            lhs.MethodIndex - lhs.Begin != rhs.MethodIndex - rhs.Begin)
        {
                return false;
            }
            for (TokenIndex i = 0; i < length; ++i) {
                if (i != lhs.MethodIndex - lhs.Begin &&
                tree.TokenText(lhs.Begin + i) != tree.TokenText(rhs.Begin + i))
            {
                    return false;
                }
            }
            return true;
        };
    struct Task
    {
        const parser::Node* Node;
        std::optional<std::string> Base;
        NK Parent;
    };
    std::pmr::vector<Task> tasks{&scratch};
    tasks.push_back({&tree.Root(), std::nullopt, tree.Root().Kind});
    while (!tasks.empty()) {
        auto task = std::move(tasks.back());
        tasks.pop_back();
        const auto& node = *task.Node;
        if (HasRecoveryChild(node) || IsIncompleteStatement(node) ||
            (node.Kind == NK::CompoundStatement && task.Parent == NK::Argument))
        {
            // An argument containing a statement block is typically a macro
            // body; its existing multi-line indentation belongs to the
            // macro invocation rather than the surrounding compound block.
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
            const auto blockWidth = style.BlockBodyIndentWidth
                ? style.BlockBodyIndentWidth
                : style.IndentWidth;
            std::string childIndent = base + GeneratedIndentAfter(base,
                blockWidth, style);
            const bool switchBody = task.Parent == NK::SwitchStatement;
            if (style.IndentBlockBodies) {
                mark(closing, base);
            }
            std::pmr::vector<Task> children{&scratch};
            std::pmr::vector<const parser::Node*> leadingComments{&scratch};
            bool afterCase = false;
            bool nextIsCaseBlock = false;
            bool afterUnbracedSwitch = false;
            std::string caseIndent;
            std::vector<const parser::Node*> visualScopeStatements;
            if (node.Kind == NK::CompoundStatement &&
                !style.IndentedCallScopePairs.empty())
            {
                auto closingName = [&] (std::string_view method)
                    -> std::optional<std::string_view> {
                    for (const auto& pair : style.IndentedCallScopePairs) {
                        auto separator = pair.find(':');
                        if (method == std::string_view(pair).substr(
                            0,
                            separator))
                        {
                            return std::string_view(pair).substr(separator + 1);
                        }
                    }
                    return std::nullopt;
                };
                for (auto* open = node.Child();
                    open;
                    open = open->NextSibling)
                {
                    auto call = scopeCall(open);
                    if (!call || originalIndent(open->Tokens.Begin) !=
                            childIndent)
                    {
                        continue;
                    }
                    auto close = closingName(call->Method);
                    if (!close) {
                        continue;
                    }
                    std::vector<std::string_view> closers{*close};
                    std::vector<std::string> bases{childIndent};
                    std::vector<std::string> interiors{
                        childIndent + GeneratedIndentAfter(childIndent,
                            blockWidth, style)};
                    std::vector<ScopeCall> scopes{*call};
                    std::vector<const parser::Node*> interior;
                    for (auto* next = open->NextSibling;
                        next;
                        next = next->NextSibling)
                    {
                        if (!scopeInteriorStatement(next)) {
                            break;
                        }
                        auto nested = scopeCall(next);
                        if (nested &&
                            sameScopeShape(*nested, scopes.back()) &&
                            nested->Method == closers.back() &&
                            originalIndent(next->Tokens.Begin) == bases.back())
                        {
                            if (closers.size() == 1) {
                                if (!interior.empty()) {
                                    visualScopeStatements.insert(
                                        visualScopeStatements.end(),
                                        interior.begin(),
                                        interior.end());
                                }
                                break;
                            }
                            closers.pop_back();
                            bases.pop_back();
                            interiors.pop_back();
                            scopes.pop_back();
                            interior.push_back(next);
                            continue;
                        }
                        if (nested && sameScopeRoot(*nested, scopes.back())) {
                            if (auto nestedClose =
                                closingName(nested->Method))
                            {
                                auto indent = interiors.back();
                                if (originalIndent(
                                    next->Tokens.Begin) != indent)
                                {
                                    break;
                                }
                                auto* following = next->NextSibling;
                                auto deeper = indent + GeneratedIndentAfter(
                                    indent,
                                    blockWidth,
                                    style);
                                // Flat nested pairs retain their parent's
                                // interior indent; visually nested pairs use
                                // one more level. Both must still close.
                                bool visual = scopeInteriorStatement(
                                    following) &&
                                    originalIndent(following->Tokens.Begin) ==
                                        deeper;
                                interior.push_back(next);
                                closers.push_back(*nestedClose);
                                bases.push_back(indent);
                                interiors.push_back(
                                    visual
                                        ? std::move(deeper)
                                        : std::move(indent));
                                scopes.push_back(*nested);
                                continue;
                            }
                        }
                        if (originalIndent(next->Tokens.Begin) !=
                            interiors.back())
                        {
                            break;
                        }
                        interior.push_back(next);
                    }
                }
            }
            for (auto* child = node.Child();
                child;
                child = child->NextSibling)
            {
                if (child->Tokens.Empty()) {
                    leadingComments.clear();
                    continue;
                }
                if (child->Kind == NK::Comment) {
                    leadingComments.push_back(child);
                } else if (child->Kind == NK::PreprocessingDirective) {
                    leadingComments.clear();
                }
                // The parser leaves the statement after an unbraced switch's
                // case label as a sibling of the switch. Its original indent
                // is more reliable than the compound block's normal indent.
                const bool switchTail = afterUnbracedSwitch &&
                    child->Kind != NK::Comment &&
                    child->Kind != NK::PreprocessingDirective;
                if (switchTail) {
                    afterUnbracedSwitch = false;
                }
                bool unbracedSwitch = false;
                if (child->Kind == NK::SwitchStatement) {
                    for (auto* body = child->Child();
                        body;
                        body = body->NextSibling)
                    {
                        if (body->Kind == NK::CaseLabel) {
                            unbracedSwitch = true;
                            break;
                        }
                    }
                }
                if (unbracedSwitch) {
                    afterUnbracedSwitch = true;
                }
                auto target = switchTail
                    ? originalIndent(child->Tokens.Begin)
                    : childIndent;
                const bool caseLabel = switchBody &&
                    child->Kind == NK::CaseLabel;
                const bool caseBlock = switchBody && nextIsCaseBlock &&
                    child->Kind == NK::CompoundStatement;
                if (caseLabel) {
                    afterCase = true;
                    nextIsCaseBlock = true;
                    if (style.IndentCaseLabels == BodyIndentation::NoIndent) {
                        target = base;
                    } else if (style.IndentCaseLabels ==
                        BodyIndentation::Indent)
                    {
                        const auto width = style.CaseLabelIndentWidth
                            ? style.CaseLabelIndentWidth
                            : style.IndentWidth;
                        target = base +
                            GeneratedIndentAfter(base, width, style);
                    }
                    caseIndent = style.IndentCaseLabels ==
                        BodyIndentation::Preserve
                        ? originalIndent(child->Tokens.Begin)
                        : target;
                } else if (caseBlock) {
                    // A brace immediately after a case label belongs to the
                    // label, not to an extra level within the switch body.
                    target = caseIndent;
                } else if (switchBody && afterCase &&
                    style.IndentCaseLabels == BodyIndentation::Indent)
                {
                    const auto width = style.CaseLabelIndentWidth
                        ? style.CaseLabelIndentWidth
                        : style.IndentWidth;
                    target += GeneratedIndentAfter(target, width, style);
                }
                if (!caseLabel && child->Kind != NK::Comment &&
                    child->Kind != NK::PreprocessingDirective)
                {
                    nextIsCaseBlock = false;
                }
                const bool preserveCase = caseLabel &&
                    style.IndentCaseLabels == BodyIndentation::Preserve;
                const bool attribute =
                    isStatementAttribute(child->Tokens.Begin);
                if (!switchTail && !preserveCase && !HasRecoveryChild(*child) &&
                    (attribute
                        ? style.StatementAttributeIndent !=
                        StatementAttributeIndent::Preserve
                        : style.IndentBlockBodies || caseLabel))
                {
                    auto indent = target;
                    if (attribute && style.StatementAttributeIndent ==
                            StatementAttributeIndent::Indent)
                    {
                        const auto width = style.StatementAttributeIndentWidth
                            ? style.StatementAttributeIndentWidth
                            : style.IndentWidth;
                        indent += GeneratedIndentAfter(indent, width, style);
                    }
                    // A standalone scope brace can be intentionally aligned
                    // with its enclosing block. Its contents still follow
                    // the logical nesting level rather than that brace.
                    const bool standaloneBrace =
                        child->Kind == NK::CompoundStatement && !caseBlock;
                    const bool visualScope =
                        std::ranges::find(visualScopeStatements, child) !=
                        visualScopeStatements.end();
                    if (!visualScope && (!standaloneBrace ||
                        style.StandaloneBlockBraceIndentation ==
                            StandaloneBlockBraceIndentation::Indent))
                    {
                        mark(child->Tokens.Begin, indent);
                        if (style.IndentBlockBodies &&
                            style.IndentLeadingStatementComments &&
                            !leadingComments.empty() &&
                            child->Kind != NK::Comment &&
                            child->Kind != NK::PreprocessingDirective &&
                            !conditional[child->Tokens.Begin] &&
                            !originalIndent(child->Tokens.Begin).empty() &&
                            originalIndent(child->Tokens.Begin) != indent)
                        {
                            // Move only a contiguous run of standalone line
                            // comments sharing the statement's old indent.
                            auto next =
                                tree.Token(child->Tokens.Begin).Text.Begin;
                            auto oldIndent = originalIndent(
                                child->Tokens.Begin);
                            bool attached = true;
                            for (auto it = leadingComments.rbegin();
                                it != leadingComments.rend();
                                ++it)
                            {
                                const auto& comment = **it;
                                auto first = comment.Tokens.Begin;
                                auto begin = tree.Token(first).Text.Begin;
                                auto end = tree.Token(comment.Tokens.End - 1)
                                    .Text.End;
                                if (end > next) {
                                    attached = false;
                                    break;
                                }
                                auto gap = source.substr(end, next - end);
                                if (comment.Tokens.End != first + 1 ||
                                    !tree.TokenText(first).starts_with("//") ||
                                    originalIndent(first) != oldIndent ||
                                    std::count(
                                        gap.begin(),
                                        gap.end(),
                                        '\n') != 1 ||
                                    gap.find_first_not_of(" \t\n") !=
                                        std::string_view::npos)
                                {
                                    attached = false;
                                    break;
                                }
                                next = begin;
                            }
                            if (attached) {
                                for (const auto* comment : leadingComments) {
                                    mark(comment->Tokens.Begin, indent, true);
                                }
                            }
                        }
                    }
                }
                if (child->Kind != NK::Comment) {
                    leadingComments.clear();
                }
                children.push_back({child, target, node.Kind});
            }
            for (auto it = children.rbegin(); it != children.rend(); ++it) {
                tasks.push_back(std::move(*it));
            }
            continue;
        }
        const bool control = node.Kind == NK::IfStatement ||
            node.Kind == NK::ForStatement ||
            node.Kind == NK::WhileStatement ||
            node.Kind == NK::DoWhileStatement ||
            node.Kind == NK::SwitchStatement;
        std::pmr::vector<Task> children{&scratch};
        for (auto* child = node.Child();
            child;
            child = child->NextSibling)
        {
            auto childBase = task.Base;
            // An inline member function's opening brace is not at the start
            // of its line. Anchor its body to the declaration instead.
            if (node.Kind == NK::FunctionDefinition &&
                child->Kind == NK::CompoundStatement)
            {
                childBase = originalIndent(node.Tokens.Begin);
            }
            if (node.Kind == NK::LambdaExpression &&
                child->Kind == NK::LambdaBody && !node.Tokens.Empty())
            {
                // A trailing return type may place the opening brace on an
                // indented continuation line. In that case, anchor the body
                // and closing brace to the lambda's first line instead.
                childBase = lineIndent(child->Tokens.Begin);
                for (auto* part = node.Child();
                    part;
                    part = part->NextSibling)
                {
                    if (part->Kind == NK::TrailingReturnType) {
                        childBase = lineIndent(node.Tokens.Begin);
                        break;
                    }
                }
            }
            const bool body = control && childBase &&
                child->Kind != NK::Condition &&
                child->Kind != NK::ForHeader &&
                child->Kind != NK::CompoundStatement &&
                child->Kind != NK::PreprocessingDirective &&
                child->Kind != NK::Comment && !child->Tokens.Empty();
            if (body && child->Kind != NK::CaseLabel &&
                !HasRecoveryChild(*child))
            {
                const bool attribute =
                    isStatementAttribute(child->Tokens.Begin);
                if (attribute) {
                    if (style.StatementAttributeIndent !=
                            StatementAttributeIndent::Preserve)
                    {
                        auto indent = *childBase;
                        if (style.StatementAttributeIndent ==
                                StatementAttributeIndent::Indent)
                        {
                            const auto width =
                                style.StatementAttributeIndentWidth
                                    ? style.StatementAttributeIndentWidth
                                    : style.IndentWidth;
                            indent += GeneratedIndentAfter(
                                indent,
                                width,
                                style);
                        }
                        mark(child->Tokens.Begin, indent);
                    }
                } else if (style.IndentBlockBodies) {
                    // `else if` continues the header rather than nesting a
                    // second unbraced body under it.
                    const bool elseIf = node.Kind == NK::IfStatement &&
                        child->Kind == NK::IfStatement &&
                        child->Tokens.Begin > node.Tokens.Begin &&
                        tree.Kind(child->Tokens.Begin - 1) == TK::Keyword_else;
                    if (!elseIf) {
                        const auto width = style.BlockBodyIndentWidth
                            ? style.BlockBodyIndentWidth
                            : style.IndentWidth;
                        *childBase += GeneratedIndentAfter(
                            *childBase,
                            width,
                            style);
                        mark(child->Tokens.Begin, *childBase);
                    }
                }
            }
            children.push_back({child, std::move(childBase), node.Kind});
        }
        for (auto it = children.rbegin(); it != children.rend(); ++it) {
            tasks.push_back(std::move(*it));
        }
    }
    if (edits.empty()) {
        return std::nullopt;
    }
    std::ranges::sort(edits, {}, &Replacement::Begin);
    std::string output;
    output.reserve(source.size());
    std::vector<Replacement> accepted;
    size_t cursor = 0;
    for (const auto& edit : edits) {
        if (edit.Begin < cursor) {
            continue;
        }
        output.append(source.substr(cursor, edit.Begin - cursor));
        output += edit.Text;
        cursor = edit.End;
        accepted.push_back(edit);
    }
    output.append(source.substr(cursor));
    return LayoutEdits{std::move(output), std::move(accepted)};
}

std::optional<LayoutEdits> FormatParameters(
    const parser::SyntaxTree& tree,
    const Style& style)
{
    if (style.FunctionParameterSplit == FunctionParameterSplit::Preserve) {
        return std::nullopt;
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
        // A recovered abstract declarator may be a macro call. Reflow it
        // only when its exact invocation name was explicitly allowed.
        bool selectedMacro = node->Kind == NK::FunctionDeclarator &&
            parent && parent->Kind == NK::AbstractDeclarator &&
            node->Tokens.Begin > 0 &&
            tree.Kind(node->Tokens.Begin - 1) == TK::Identifier &&
            std::ranges::find(
                style.CallLikeMacroNames,
                tree.TokenText(node->Tokens.Begin - 1)) !=
                style.CallLikeMacroNames.end();
        if (node->Kind == NK::FunctionDeclarator && parent &&
            (parent->Kind == NK::Declarator || selectedMacro))
        {
            for (auto* child = node->Child();
                child;
                child = child->NextSibling)
            {
                if (child->Kind == parser::NodeKind::ParameterList) {
                    if (auto replacement =
                        ReflowDelimitedList(tree, *child, style))
                    {
                        edits.push_back(std::move(*replacement));
                    }
                }
            }
        }
        for (auto* child = node->Child(); child; child = child->NextSibling) {
            nodes.emplace_back(child, node);
        }
    }
    if (edits.empty()) {
        return std::nullopt;
    }
    std::ranges::sort(edits, {}, &Replacement::Begin);
    return RenderLayoutEdits(tree.Source(), std::move(edits));
}

// static_assert is a declaration, not a call expression. Split its two
// single-line operands at the top-level comma when the declaration overflows.
std::string FormatStaticAsserts(
    const parser::SyntaxTree& tree,
    const Style& style)
{
    if (style.CallArgumentSplit == CallArgumentSplit::Preserve) {
        return std::string(tree.Source());
    }
    const auto source = tree.Source();
    std::vector<Replacement> edits;
    std::vector<const parser::Node*> nodes{&tree.Root()};
    while (!nodes.empty()) {
        const auto* node = nodes.back();
        nodes.pop_back();
        for (auto* child = node->Child(); child; child = child->NextSibling) {
            nodes.push_back(child);
        }
        if (node->Kind != NK::StaticAssertDeclaration ||
            node->Tokens.End < node->Tokens.Begin + 5 ||
            tree.Kind(node->Tokens.Begin) != TK::Keyword_static_assert)
        {
            continue;
        }
        const auto open = node->Tokens.Begin + 1;
        if (tree.Kind(open) != TK::Operator_OpenParenthesis) {
            continue;
        }
        const auto close = tree.MatchingDelimiter(open);
        if (close == NoToken || close >= node->Tokens.End ||
            tree.Kind(close) != TK::Operator_CloseParenthesis)
        {
            continue;
        }
        const auto begin = tree.Token(open).Text.End;
        const auto end = tree.Token(close).Text.Begin;
        auto content = source.substr(begin, end - begin);
        if (content.find_first_of("\r\n") != std::string_view::npos) {
            continue;
        }
        int angles = 0;
        TokenIndex comma = NoToken;
        bool safe = true;
        for (auto i = open + 1; i < close; ++i) {
            const auto kind = tree.Kind(i);
            if (kind == TK::Comment || kind == TK::PreprocessingDirective) {
                safe = false;
                break;
            }
            if (kind == TK::Operator_OpenParenthesis ||
                kind == TK::Operator_OpenBracket ||
                kind == TK::Operator_OpenBrace)
            {
                auto match = tree.MatchingDelimiter(i);
                if (match == NoToken || match >= close) {
                    safe = false;
                    break;
                }
                i = match;
            } else if (kind == TK::Operator_Less) {
                ++angles;
            } else if (kind == TK::Operator_Greater && angles > 0) {
                --angles;
            } else if (kind == TK::Operator_RightShift && angles > 0) {
                angles = std::max(0, angles - 2);
            } else if (kind == TK::Operator_Comma && angles == 0) {
                if (comma != NoToken) {
                    safe = false;
                    break;
                }
                comma = i;
            }
        }
        if (!safe || angles != 0 || comma == NoToken) {
            continue;
        }
        auto first = source.substr(begin, tree.Token(comma).Text.Begin - begin);
        auto second = source.substr(
            tree.Token(comma).Text.End,
            end - tree.Token(comma).Text.End);
        auto trim = [] (std::string_view text) {
            const auto start = text.find_first_not_of(" \t");
            if (start == std::string_view::npos) {
                return std::string_view{};
            }
            return text.substr(start, text.find_last_not_of(" \t") - start + 1);
        };
        first = trim(first);
        second = trim(second);
        if (first.empty() || second.empty()) {
            continue;
        }
        auto lineStart = source.rfind('\n', tree.Token(open).Text.Begin);
        lineStart = lineStart == std::string_view::npos ? 0 : lineStart + 1;
        auto lineEnd = source.find('\n', end);
        if (lineEnd == std::string_view::npos) {
            lineEnd = source.size();
        }
        if (lineEnd - lineStart <= style.ColumnLimit) {
            continue;
        }
        size_t indent = lineStart;
        while (indent < tree.Token(open).Text.Begin && source[indent] == ' ') {
            ++indent;
        }
        auto continuation = "\n" + GeneratedIndent(
            indent - lineStart + style.IndentWidth,
            style);
        edits.push_back({begin, end,
            continuation + std::string(first) + "," + continuation +
                std::string(second)});
    }
    std::ranges::sort(edits, {}, &Replacement::Begin);
    std::string result;
    size_t cursor = 0;
    for (const auto& edit : edits) {
        result.append(source.substr(cursor, edit.Begin - cursor));
        result += edit.Text;
        cursor = edit.End;
    }
    result.append(source.substr(cursor));
    return result;
}

// Reflow only complete, whitespace-separated braced initializer elements.
// The caller refreshes syntax after outer lists move, then visits nested lists.
std::optional<std::string> FormatInitializers(
    const parser::SyntaxTree& tree,
    const Style& style)
{
    if (style.InitializerListSplit == FunctionParameterSplit::Preserve) {
        return std::nullopt;
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
    if (edits.empty()) {
        return std::nullopt;
    }
    std::ranges::sort(edits, {}, &Replacement::Begin);
    std::string output;
    auto source = tree.Source();
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

const parser::Node* SingleStatementLambdaChild(
    const parser::Node& body,
    const parser::SyntaxTree& tree)
{
    if (body.Kind != NK::LambdaBody || !body.Child() ||
        body.Child()->NextSibling || body.Child()->Tokens.Empty() ||
        body.Child()->Kind == NK::Comment ||
        body.Child()->Kind == NK::PreprocessingDirective ||
        HasRecoveryChild(body) || IsIncompleteStatement(*body.Child()))
    {
        return nullptr;
    }
    const auto* statement = body.Child();
    for (auto i = statement->Tokens.Begin; i < statement->Tokens.End; ++i) {
        if (tree.Kind(i) == TK::Comment ||
            tree.Kind(i) == TK::PreprocessingDirective)
        {
            return nullptr;
        }
    }
    auto begin = tree.Token(statement->Tokens.Begin).Text.Begin;
    auto end = tree.Token(statement->Tokens.End - 1).Text.End;
    auto text = tree.Source().substr(begin, end - begin);
    return text.find_first_of("\r\n") == std::string_view::npos
        ? statement
        : nullptr;
}

constexpr size_t MaxCallReflowDepth = 8;

// When a safe call split resolves an overlong assignment RHS, reclaim the
// preceding assignment line if its new first line fits. Do not merge an
// existing break merely because an unrelated part of the file reflowed.
std::string JoinAssignmentBeforeCallReflow(
    const parser::SyntaxTree& tree,
    const Style& style)
{
    const auto source = tree.Source();
    std::vector<Replacement> edits;
    std::vector<const parser::Node*> nodes{&tree.Root()};
    while (!nodes.empty()) {
        const auto* node = nodes.back();
        nodes.pop_back();
        for (auto* child = node->Child(); child; child = child->NextSibling) {
            nodes.push_back(child);
        }
        if (node->Kind != NK::AssignmentExpression ||
            HasRecoveryChild(*node) || IsIncompleteStatement(*node))
        {
            continue;
        }
        const auto* lhs = node->Child();
        const auto* rhs = lhs ? lhs->NextSibling : nullptr;
        if (!lhs || !rhs || lhs->Tokens.Empty() || rhs->Tokens.Empty() ||
            rhs->Kind != NK::CallExpression ||
            HasRecoveryChild(*rhs) || IsIncompleteStatement(*rhs))
        {
            continue;
        }
        TokenIndex assignment = NoToken;
        for (auto i = lhs->Tokens.End; i < rhs->Tokens.Begin; ++i) {
            if (tree.Role(i) == parser::PunctuationRole::AssignmentOperator &&
                tree.TokenText(i) == "=")
            {
                assignment = i;
            }
        }
        if (assignment == NoToken) {
            continue;
        }
        auto begin = tree.Token(assignment).Text.End;
        auto end = tree.Token(rhs->Tokens.Begin).Text.Begin;
        if (end < begin) {
            continue;
        }
        auto gap = source.substr(begin, end - begin);
        if (!IsWhitespace(gap) || gap.find('\r') !=
                std::string_view::npos ||
            gap.find('\n') == std::string_view::npos ||
            gap.find('\n', gap.find('\n') + 1) !=
                std::string_view::npos)
        {
            continue;
        }
        auto rhsLineEnd = source.find('\n', end);
        if (rhsLineEnd == std::string_view::npos) {
            rhsLineEnd = source.size();
        }
        auto rhsLineStart = source.rfind('\n', end);
        rhsLineStart = rhsLineStart == std::string_view::npos
            ? 0
            : rhsLineStart + 1;
        auto lhsLineStart = source.rfind('\n', begin);
        lhsLineStart = lhsLineStart == std::string_view::npos
            ? 0
            : lhsLineStart + 1;
        if (rhsLineEnd - rhsLineStart <= style.ColumnLimit ||
            source.substr(rhsLineStart, end - rhsLineStart)
                .find_first_not_of(" \t") != std::string_view::npos ||
            source.substr(end, rhsLineEnd - end).find_first_of("\r\\") !=
                std::string_view::npos ||
            source.substr(end, rhsLineEnd - end).find("//") !=
                std::string_view::npos ||
            source.substr(end, rhsLineEnd - end).find("/*") !=
                std::string_view::npos ||
            (rhsLineEnd < source.size() && rhsLineEnd > 0 &&
                source[rhsLineEnd - 1] == '\r'))
        {
            continue;
        }
        bool unsafe = false;
        for (auto i = rhs->Tokens.Begin; i < rhs->Tokens.End; ++i) {
            unsafe |= tree.Kind(i) == TK::Comment ||
                tree.Kind(i) == TK::PreprocessingDirective;
        }
        if (unsafe) {
            continue;
        }
        std::vector<const parser::Node*> calls{rhs};
        while (!calls.empty()) {
            auto* call = calls.back();
            calls.pop_back();
            if (call->Kind == NK::CallExpression &&
                !HasRecoveryChild(*call) &&
                !IsIncompleteStatement(*call))
            {
                const auto* callee = call->Child();
                const auto* arguments = callee ? callee->NextSibling : nullptr;
                if (callee && arguments &&
                    callee->Kind != NK::Expression &&
                    callee->Kind != NK::ParenthesizedExpression &&
                    arguments->Kind == NK::ArgumentList)
                {
                    auto reflow = ReflowDelimitedList(tree, *arguments, style);
                    if (reflow && reflow->Begin >= end &&
                        reflow->Begin < rhsLineEnd)
                    {
                        auto breakAt = reflow->Text.find('\n');
                        if (breakAt != std::string::npos &&
                            begin - lhsLineStart + 1 +
                                reflow->Begin - end + breakAt <=
                                    style.ColumnLimit)
                        {
                            edits.push_back({begin, end, " "});
                            break;
                        }
                    }
                }
            }
            for (auto* child = call->Child();
                child;
                child = child->NextSibling)
            {
                calls.push_back(child);
            }
        }
    }
    std::ranges::sort(edits, {}, &Replacement::Begin);
    std::string output;
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

// Split only complete, flat capture lists on overlong lambda headers.
// Keeping [ on the call line avoids relocating the lambda body.
std::optional<LayoutEdits> FormatLambdaCaptures(
    const parser::SyntaxTree& tree,
    const Style& style)
{
    if (style.LambdaCaptureSplit == LambdaCaptureSplit::Preserve) {
        return std::nullopt;
    }
    const auto source = tree.Source();
    std::vector<Replacement> edits;
    std::vector<const parser::Node*> nodes{&tree.Root()};
    while (!nodes.empty()) {
        auto* node = nodes.back();
        nodes.pop_back();
        for (auto* child = node->Child(); child; child = child->NextSibling) {
            nodes.push_back(child);
        }
        if (node->Kind != NK::LambdaCaptureList ||
            HasRecoveryChild(*node) || IsIncompleteStatement(*node) ||
            node->Tokens.End < node->Tokens.Begin + 5)
        {
            continue;
        }
        auto open = node->Tokens.Begin;
        auto close = node->Tokens.End - 1;
        if (tree.Kind(open) != TK::Operator_OpenBracket ||
            tree.Kind(close) != TK::Operator_CloseBracket ||
            tree.MatchingDelimiter(open) != close)
        {
            continue;
        }
        auto begin = tree.Token(open).Text.End;
        auto end = tree.Token(close).Text.Begin;
        auto line = source.rfind('\n', begin);
        line = line == std::string_view::npos ? 0 : line + 1;
        auto lineEnd = source.find('\n', begin);
        if (lineEnd == std::string_view::npos) {
            lineEnd = source.size();
        }
        auto prefix = source.substr(line, begin - line);
        if (lineEnd - line <= style.ColumnLimit ||
            tree.Token(close).Text.End - line <= style.ColumnLimit ||
            source.substr(begin, end - begin).find_first_of("\r\n\\") !=
                std::string_view::npos ||
            prefix.find_first_of("\r\t\\") != std::string_view::npos ||
            (lineEnd < source.size() && lineEnd > 0 &&
                source[lineEnd - 1] == '\r'))
        {
            continue;
        }
        auto first = prefix.find_first_not_of(' ');
        auto base = std::string(prefix.substr(0, first));
        auto indent = base + GeneratedIndentAfter(
            base,
            style.IndentWidth,
            style);
        std::string replacement;
        auto previous = begin;
        bool safe = true;
        bool expectCapture = true;
        size_t count = 0;
        for (auto* child = node->Child();
            child;
            child = child->NextSibling)
        {
            if (child->Tokens.Empty() || HasRecoveryChild(*child) ||
                IsIncompleteStatement(*child) ||
                child->Kind != (expectCapture
                    ? NK::LambdaCapture
                    : NK::ListSeparator))
            {
                safe = false;
                break;
            }
            auto from = tree.Token(child->Tokens.Begin).Text.Begin;
            auto to = tree.Token(child->Tokens.End - 1).Text.End;
            if (from < previous || to > end ||
                !IsWhitespace(source.substr(previous, from - previous)))
            {
                safe = false;
                break;
            }
            if (expectCapture) {
                auto capture = source.substr(from, to - from);
                if (capture.find_first_of("\r\n\\") !=
                        std::string_view::npos)
                {
                    safe = false;
                    break;
                }
                for (auto i = child->Tokens.Begin;
                    i < child->Tokens.End;
                    ++i)
                {
                    if (tree.Kind(i) == TK::Comment ||
                        tree.Kind(i) == TK::PreprocessingDirective)
                    {
                        safe = false;
                        break;
                    }
                }
                if (!safe) {
                    break;
                }
                replacement += "\n" + indent + std::string(capture);
                ++count;
            } else if (child->Tokens.End != child->Tokens.Begin + 1 ||
                tree.Kind(child->Tokens.Begin) != TK::Operator_Comma)
            {
                safe = false;
                break;
            } else {
                replacement += ',';
            }
            previous = to;
            expectCapture = !expectCapture;
        }
        if (!safe || expectCapture || count < 2 ||
            !IsWhitespace(source.substr(previous, end - previous)))
        {
            continue;
        }
        replacement += "\n" + base;
        edits.push_back({begin, end, std::move(replacement)});
    }
    if (edits.empty()) {
        return std::nullopt;
    }
    std::ranges::sort(edits, {}, &Replacement::Begin);
    return RenderLayoutEdits(source, std::move(edits));
}

// Relocate a complete multiline lambda only if the new header fits and
// every interior physical line can follow it without touching token text.
std::vector<Replacement> MoveMultilineLambdaArgument(
    const parser::SyntaxTree& tree,
    const parser::Node& argument,
    size_t gapBegin,
    size_t argumentBegin,
    std::string_view indent,
    size_t baseIndent,
    const Style& style)
{
    const auto source = tree.Source();
    if (style.IndentationKind == IndentationKind::Tabs ||
        indent.find('\t') != std::string_view::npos ||
        indent.size() < baseIndent ||
        HasRecoveryChild(argument) || IsIncompleteStatement(argument))
    {
        return {};
    }
    auto headerEnd = source.find('\n', argumentBegin);
    auto argumentEnd = tree.Token(argument.Tokens.End - 1).Text.End;
    if (headerEnd == std::string_view::npos ||
        indent.size() + headerEnd - argumentBegin > style.ColumnLimit ||
        source.substr(argumentBegin, argumentEnd - argumentBegin)
            .find_first_of("\r\\") != std::string_view::npos)
    {
        return {};
    }
    for (auto i = argument.Tokens.Begin; i < argument.Tokens.End; ++i) {
        if (tree.Kind(i) == TK::PreprocessingDirective ||
            tree.TokenText(i).find_first_of("\r\n") !=
                std::string_view::npos)
        {
            return {};
        }
    }
    const auto delta = indent.size() - baseIndent;
    std::vector<Replacement> edits;
    edits.push_back({gapBegin, argumentBegin, "\n" + std::string(indent)});
    for (auto line = headerEnd + 1; line < argumentEnd;) {
        auto end = source.find('\n', line);
        if (end == std::string_view::npos || end > argumentEnd) {
            end = argumentEnd;
        }
        auto first = line;
        while (first < end && source[first] == ' ') {
            ++first;
        }
        if (first < end) {
            if (first - line < baseIndent ||
                source.substr(line, first - line).find('\t') !=
                    std::string_view::npos ||
                source[first] == '\t')
            {
                return {};
            }
            edits.push_back({line, first,
                std::string(first - line + delta, ' ')});
        }
        line = end == argumentEnd ? end : end + 1;
    }
    return edits;
}

// Compare safe one-break layouts across the parsed expression rather
// than committing to the first nested call. Call candidates are built by
// the configured call reflow; named casts contribute an operand boundary.
std::optional<LayoutEdits> BalanceDelimitedExpressions(
    const parser::SyntaxTree& tree,
    const Style& style)
{
    HIT_FMT_PROFILE_PHASE("formatter.expression_balance");
    if (style.ExpressionLayoutCondition ==
        ExpressionLayoutCondition::Preserve)
    {
        return std::nullopt;
    }
    const auto source = tree.Source();
    struct Candidate
    {
        Candidate(
            Replacement edit,
            size_t line,
            size_t longest,
            size_t imbalance,
            size_t breaks,
            bool outer,
            size_t logicalScope,
            bool needsCallCompetitor)
            : Edit(std::move(edit))
            , Line(static_cast<util::SourceOffset>(line))
            , Longest(static_cast<util::SourceOffset>(longest))
            , Imbalance(static_cast<util::SourceOffset>(imbalance))
            , Breaks(static_cast<util::SourceOffset>(breaks))
            , Outer(outer)
            , LogicalScope(static_cast<util::SourceOffset>(logicalScope))
            , NeedsCallCompetitor(needsCallCompetitor)
        {
            ASSERT(line <= util::MaxSourceSize &&
                longest <= util::MaxSourceSize &&
                imbalance <= util::MaxSourceSize &&
                breaks <= util::MaxSourceSize &&
                logicalScope <= util::MaxSourceSize)
        }

        Replacement Edit;
        util::SourceOffset Line;
        util::SourceOffset Longest;
        util::SourceOffset Imbalance;
        util::SourceOffset Breaks;
        bool Outer;
        util::SourceOffset LogicalScope;
        bool NeedsCallCompetitor;
    };
    std::vector<Candidate> candidates;
    std::vector<std::pair<size_t, size_t>> ownedCallArguments;
    std::vector<const parser::Node*> nodes{&tree.Root()};
    while (!nodes.empty()) {
        const auto* node = nodes.back();
        nodes.pop_back();
        for (auto* child = node->Child(); child; child = child->NextSibling) {
            nodes.push_back(child);
        }
        if (node->Tokens.Empty() || HasRecoveryChild(*node)) {
            continue;
        }
        if (node->Kind == NK::CallExpression &&
            style.CallArgumentSplit == CallArgumentSplit::OnePerLine)
        {
            const auto* callee = node->Child();
            const auto* args = callee ? callee->NextSibling : nullptr;
            const auto* first = args ? args->Child() : nullptr;
            if (callee && !callee->Tokens.Empty() &&
                callee->Kind != NK::Expression &&
                callee->Kind != NK::ParenthesizedExpression &&
                args && args->Kind == NK::ArgumentList &&
                !args->Tokens.Empty() &&
                first && first->Kind == NK::Argument &&
                first->NextSibling &&
                first->NextSibling->Kind == NK::ListSeparator &&
                first->NextSibling->NextSibling &&
                first->NextSibling->NextSibling->Kind == NK::Argument)
            {
                auto begin = tree.Token(args->Tokens.Begin).Text.End;
                auto end = tree.Token(args->Tokens.End - 1).Text.Begin;
                if (begin <= end && source.substr(begin, end - begin)
                        .find('\n') == std::string_view::npos)
                {
                    // If the call cannot be safely reflowed later, keep
                    // its flat argument list source-owned instead of
                    // hiding the overflow inside a nested expression.
                    ownedCallArguments.emplace_back(
                        tree.Token(args->Tokens.Begin).Text.Begin,
                        tree.Token(args->Tokens.End - 1).Text.End);
                }
            }
        }
        std::optional<Replacement> edit;
        bool multiArgumentCall = false;
        bool needsCallCompetitor = false;
        bool cast = node->Kind == NK::CastExpression;
        if (cast) {
            const auto* type = node->Child();
            const auto* operand = type ? type->NextSibling : nullptr;
            if (!type || !operand || operand->Tokens.Empty() ||
                operand->NextSibling || HasRecoveryChild(*type) ||
                HasRecoveryChild(*operand))
            {
                continue;
            }
            auto opening = type->Tokens.End;
            while (opening < operand->Tokens.Begin &&
                tree.Kind(opening) != TK::Operator_OpenParenthesis)
            {
                ++opening;
            }
            if (opening >= operand->Tokens.Begin ||
                tree.MatchingDelimiter(opening) != node->Tokens.End - 1)
            {
                continue;
            }
            auto begin = tree.Token(opening).Text.End;
            auto end = tree.Token(operand->Tokens.Begin).Text.Begin;
            auto openBegin = tree.Token(node->Tokens.Begin).Text.Begin;
            auto closeEnd = tree.Token(node->Tokens.End - 1).Text.End;
            auto line = source.rfind('\n', openBegin);
            line = line == std::string_view::npos ? 0 : line + 1;
            auto prefix = source.substr(line, begin - line);
            if (end < begin ||
                !IsWhitespace(source.substr(begin, end - begin)) ||
                prefix.find_first_of("\r\t\\") != std::string_view::npos ||
                source.substr(openBegin, closeEnd - openBegin)
                    .find_first_of("\r\n\\") != std::string_view::npos ||
                closeEnd - line <= style.ColumnLimit)
            {
                continue;
            }
            bool unsafe = false;
            for (auto i = node->Tokens.Begin; i < node->Tokens.End; ++i) {
                unsafe |= tree.Kind(i) == TK::Comment ||
                    tree.Kind(i) == TK::PreprocessingDirective ||
                    tree.Kind(i) == TK::Operator_Conditional;
                if (style.CallArgumentSplitColumnLimit != 0 &&
                    tree.Kind(i) == TK::Identifier &&
                    std::ranges::find(
                        style.CallArgumentSplitColumnLimitNames,
                        tree.TokenText(i)) !=
                        style.CallArgumentSplitColumnLimitNames.end())
                {
                    unsafe = true;
                }
            }
            if (unsafe) {
                continue;
            }
            auto leading = prefix.find_first_not_of(' ');
            edit = Replacement{begin, end,
                "\n" + GeneratedIndent(leading + style.IndentWidth, style)};
        } else if (node->Kind == NK::BinaryExpression &&
            style.ControlConditionIndent == CallArgumentIndent::IndentWidth &&
            style.CallArgumentIndent == CallArgumentIndent::IndentWidth)
        {
            const auto* left = node->Child();
            const auto* right = left ? left->NextSibling : nullptr;
            if (!left || !right || left->Tokens.Empty() ||
                right->Tokens.Empty() || right->NextSibling ||
                HasRecoveryChild(*left) || HasRecoveryChild(*right))
            {
                continue;
            }
            auto op = left->Tokens.End;
            while (op < right->Tokens.Begin) {
                auto role = tree.Role(op);
                if (role == parser::PunctuationRole::BinaryOperator ||
                    role == parser::PunctuationRole::ComparisonOperator ||
                    role == parser::PunctuationRole::ShiftOperator)
                {
                    break;
                }
                ++op;
            }
            if (op >= right->Tokens.Begin ||
                tree.Kind(op) == TK::Operator_Comma ||
                IsAmbiguousAngleOperator(tree.Kind(op)))
            {
                continue;
            }
            needsCallCompetitor = ExpressionPriority(tree.Kind(op)) > 8;
            auto leftEnd = tree.Token(left->Tokens.End - 1).Text.End;
            auto opBegin = tree.Token(op).Text.Begin;
            auto opEnd = tree.Token(op).Text.End;
            auto rightBegin = tree.Token(right->Tokens.Begin).Text.Begin;
            if (leftEnd > opBegin || opEnd > rightBegin ||
                !IsWhitespace(source.substr(leftEnd, opBegin - leftEnd)) ||
                !IsWhitespace(source.substr(opEnd, rightBegin - opEnd)))
            {
                continue;
            }
            auto line = source.rfind('\n', opBegin);
            line = line == std::string_view::npos ? 0 : line + 1;
            auto prefix = source.substr(line, opBegin - line);
            auto expressionEnd = tree.Token(node->Tokens.End - 1).Text.End;
            auto lineEnd = source.find('\n', expressionEnd);
            if (lineEnd == std::string_view::npos) {
                lineEnd = source.size();
            }
            if (prefix.find_first_of("\r\t\\") !=
                    std::string_view::npos || source.substr(
                        tree.Token(node->Tokens.Begin).Text.Begin,
                        expressionEnd -
                            tree.Token(node->Tokens.Begin).Text.Begin)
                    .find_first_of("\r\n\\") != std::string_view::npos ||
                (ExpressionPriority(tree.Kind(op)) >= 7
                    ? lineEnd - line
                    : expressionEnd - line) <=
                    style.ColumnLimit)
            {
                continue;
            }
            bool unsafe = false;
            for (auto i = node->Tokens.Begin; i < node->Tokens.End; ++i) {
                unsafe |= tree.Kind(i) == TK::Comment ||
                    tree.Kind(i) == TK::PreprocessingDirective;
            }
            if (unsafe) {
                continue;
            }
            auto leading = prefix.find_first_not_of(' ');
            auto indent = "\n" +
                GeneratedIndent(leading + style.IndentWidth, style);
            edit = style.BinaryOperatorPlacement ==
                    BinaryOperatorPlacement::Trailing
                ? Replacement{opEnd, rightBegin, std::move(indent)}
                : Replacement{leftEnd, opBegin, std::move(indent)};
        } else if (node->Kind == NK::CallExpression &&
            style.CallArgumentSplit != CallArgumentSplit::Preserve)
        {
            const auto* callee = node->Child();
            const auto* args = callee ? callee->NextSibling : nullptr;
            const auto* sole = args ? args->Child() : nullptr;
            if (!callee || callee->Tokens.Empty() ||
                callee->Kind == NK::Expression ||
                callee->Kind == NK::ParenthesizedExpression ||
                !args || args->Kind != NK::ArgumentList ||
                !sole || sole->Kind != NK::Argument ||
                HasRecoveryChild(*sole))
            {
                continue;
            }
            multiArgumentCall = sole->NextSibling != nullptr;
            if (multiArgumentCall &&
                (sole->NextSibling->Kind != NK::ListSeparator ||
                    !sole->NextSibling->NextSibling ||
                    sole->NextSibling->NextSibling->Kind != NK::Argument))
            {
                continue;
            }
            if (callee->Tokens.End == callee->Tokens.Begin + 1 &&
                tree.Kind(callee->Tokens.Begin) == TK::Identifier &&
                (std::ranges::find(
                    style.CallLineFirstStringNames,
                    tree.TokenText(callee->Tokens.Begin)) !=
                     style.CallLineFirstStringNames.end() ||
                 std::ranges::find(
                     style.WhitespaceSensitiveMacros,
                     tree.TokenText(callee->Tokens.Begin)) !=
                     style.WhitespaceSensitiveMacros.end()))
            {
                continue;
            }
            auto callStyle = style;
            if (style.CallArgumentSplitColumnLimit != 0 &&
                callee->Tokens.End == callee->Tokens.Begin + 1 &&
                tree.Kind(callee->Tokens.Begin) == TK::Identifier &&
                std::ranges::find(
                    style.CallArgumentSplitColumnLimitNames,
                    tree.TokenText(callee->Tokens.Begin)) !=
                    style.CallArgumentSplitColumnLimitNames.end())
            {
                callStyle.ColumnLimit = std::min(
                    style.ColumnLimit,
                    style.CallArgumentSplitColumnLimit);
            }
            edit = ReflowDelimitedList(tree, *args, callStyle);
        }
        if (!edit) {
            continue;
        }
        auto split = edit->Text.find('\n');
        if (source.substr(edit->Begin, edit->End - edit->Begin)
                .find_first_of("\r\n\\") != std::string_view::npos ||
            split == std::string::npos || (!multiArgumentCall &&
                edit->Text.find('\n', split + 1) != std::string::npos))
        {
            continue;
        }
        auto line = source.rfind('\n', edit->Begin);
        line = line == std::string_view::npos ? 0 : line + 1;
        auto lineEnd = source.find('\n', edit->End);
        if (lineEnd == std::string_view::npos) {
            lineEnd = source.size();
        }
        if (source.substr(line, lineEnd - line).find('\r') !=
                std::string_view::npos || lineEnd - line <= style.ColumnLimit)
        {
            continue;
        }
        if (style.TernarySplit == TernarySplit::ThreeLines) {
            bool ternary = false;
            for (auto i = node->Tokens.Begin; i < node->Tokens.End; ++i) {
                ternary |= tree.Kind(i) == TK::Operator_Conditional;
            }
            if (ternary) {
                continue;
            }
        }
        auto first = edit->Begin - line + split;
        auto longest = first;
        auto shortest = first;
        auto breaks = size_t{1};
        auto previous = split + 1;
        for (auto next = edit->Text.find('\n', previous);
            next != std::string::npos;
            next = edit->Text.find('\n', previous))
        {
            auto width = next - previous;
            longest = std::max(longest, width);
            shortest = std::min(shortest, width);
            previous = next + 1;
            ++breaks;
        }
        auto last = edit->Text.size() - previous + lineEnd - edit->End;
        longest = std::max(longest, last);
        shortest = std::min(shortest, last);
        if (longest > style.ColumnLimit) {
            continue;
        }
        candidates.push_back({std::move(*edit), line, longest,
            longest - shortest, breaks,
            cast || node->Kind == NK::BinaryExpression,
            node->Kind == NK::BinaryExpression ? node->Tokens.Size() : 0,
            needsCallCompetitor});
    }
    // Once a multi-argument call is eligible for OnePerLine, a break
    // inside one argument must not make its enclosing call appear to fit.
    std::erase_if(candidates, [&] (const Candidate& candidate) {
        return candidate.Outer && std::ranges::any_of(ownedCallArguments,
            [&] (const auto& range) {
                return candidate.Edit.Begin > range.first &&
                    candidate.Edit.End < range.second;
            });
    });
    // Keep the established expression pass in charge when no call layout
    // competes with a higher-precedence binary break on the same line.
    std::vector<size_t> callLines;
    for (const auto& candidate : candidates) {
        if (!candidate.Outer) {
            callLines.push_back(candidate.Line);
        }
    }
    std::erase_if(candidates, [&] (const Candidate& candidate) {
        return candidate.NeedsCallCompetitor &&
            std::ranges::find(callLines, candidate.Line) == callLines.end();
    });
    std::ranges::sort(candidates, {}, &Candidate::Line);
    std::vector<Replacement> edits;
    for (size_t i = 0; i < candidates.size();) {
        auto end = i + 1;
        bool hasOuter = candidates[i].Outer;
        while (end < candidates.size() &&
            candidates[end].Line == candidates[i].Line)
        {
            hasOuter |= candidates[end].Outer;
            ++end;
        }
        // Leave call-only lines to normal call reflow. Where a parsed
        // expression offers an alternative, score all its safe layouts
        // (including multi-argument calls) by their resulting line widths.
        if (hasOuter) {
            auto best = i;
            for (auto k = i + 1; k < end; ++k) {
                const auto& candidate = candidates[k];
                const auto& selected = candidates[best];
                if (candidate.LogicalScope && selected.LogicalScope &&
                    candidate.LogicalScope != selected.LogicalScope)
                {
                    if (candidate.LogicalScope > selected.LogicalScope) {
                        best = k;
                    }
                } else if (candidate.NeedsCallCompetitor &&
                    !selected.Outer &&
                    candidate.Breaks <= selected.Breaks)
                {
                    // A fitting expression break owns its enclosing
                    // layout before a nested call with no fewer breaks.
                    best = k;
                } else if (selected.NeedsCallCompetitor &&
                    !candidate.Outer &&
                    selected.Breaks <= candidate.Breaks)
                {
                    continue;
                } else if (std::tuple(candidate.Longest, candidate.Breaks,
                        candidate.Imbalance, -static_cast<std::ptrdiff_t>(
                            candidate.LogicalScope)) <
                    std::tuple(selected.Longest, selected.Breaks,
                        selected.Imbalance, -static_cast<std::ptrdiff_t>(
                            selected.LogicalScope)))
                {
                    best = k;
                }
            }
            edits.push_back(std::move(candidates[best].Edit));
        }
        i = end;
    }
    if (edits.empty()) {
        return std::nullopt;
    }
    std::string result;
    size_t cursor = 0;
    size_t accepted = 0;
    for (auto& edit : edits) {
        if (edit.Begin < cursor) {
            continue;
        }
        result.append(source.substr(cursor, edit.Begin - cursor));
        result += edit.Text;
        cursor = edit.End;
        if (&edit != &edits[accepted]) {
            edits[accepted] = std::move(edit);
        }
        ++accepted;
    }
    edits.erase(edits.begin() + accepted, edits.end());
    result.append(source.substr(cursor));
    return LayoutEdits{std::move(result), std::move(edits)};
}

// Split an overlong outer call even when an inner call already spans lines.
// Only move plain parsed arguments; preserve their internal breaks while
// shifting the inner call's existing continuation to its new base column.
std::optional<Replacement> ReflowMultilineCallArguments(
    const parser::SyntaxTree& tree,
    const parser::Node& list,
    const Style& style)
{
    if (style.CallArgumentSplit != CallArgumentSplit::OnePerLine ||
        style.FirstCallArgumentPosition != FirstCallArgumentPosition::NewLine ||
        style.CallArgumentIndent != CallArgumentIndent::IndentWidth ||
        style.FunctionCallArgumentClosingParenthesis !=
            ClosingDelimiterPlacement::SameLine ||
        style.IndentationKind == IndentationKind::Tabs ||
        list.Kind != NK::ArgumentList || list.Tokens.Size() < 5)
    {
        return std::nullopt;
    }
    const auto open = list.Tokens.Begin;
    const auto close = list.Tokens.End - 1;
    if (tree.Kind(open) != TK::Operator_OpenParenthesis ||
        tree.Kind(close) != TK::Operator_CloseParenthesis ||
        tree.MatchingDelimiter(open) != close)
    {
        return std::nullopt;
    }
    auto source = tree.Source();
    auto line = source.rfind('\n', tree.Token(open).Text.Begin);
    line = line == std::string_view::npos ? 0 : line + 1;
    auto lineEnd = source.find('\n', tree.Token(open).Text.Begin);
    if (lineEnd == std::string_view::npos) {
        lineEnd = source.size();
    }
    if (lineEnd - line <= style.ColumnLimit) {
        return std::nullopt;
    }
    auto base = line;
    while (base < tree.Token(open).Text.Begin && source[base] == ' ') {
        ++base;
    }
    if (source.substr(base, tree.Token(open).Text.Begin - base)
            .find('\t') != std::string_view::npos)
    {
        return std::nullopt;
    }
    const auto indentWidth = style.CallArgumentIndent ==
            CallArgumentIndent::Continuation
        ? tree.Token(open).Text.End - line
        : base - line + style.IndentWidth;
    const auto indent = GeneratedIndent(indentWidth, style);
    auto cursor = tree.Token(open).Text.End;
    auto closingBegin = tree.Token(close).Text.Begin;
    std::vector<std::string> parameters;
    bool expectArgument = true;
    bool hasMultiline = false;
    for (auto* child = list.Child(); child; child = child->NextSibling) {
        if (child->Tokens.Empty() || HasRecoveryChild(*child) ||
            (expectArgument
                ? child->Kind != NK::Argument
                : child->Kind != NK::ListSeparator))
        {
            return std::nullopt;
        }
        auto from = tree.Token(child->Tokens.Begin).Text.Begin;
        auto to = tree.Token(child->Tokens.End - 1).Text.End;
        if (from < cursor || to > closingBegin ||
            !IsWhitespace(source.substr(cursor, from - cursor)))
        {
            return std::nullopt;
        }
        if (expectArgument) {
            auto text = source.substr(from, to - from);
            if (text.find_first_of("\r\\") != std::string_view::npos) {
                return std::nullopt;
            }
            for (auto i = child->Tokens.Begin; i < child->Tokens.End; ++i) {
                auto kind = tree.Kind(i);
                if (kind == TK::Comment ||
                    kind == TK::PreprocessingDirective ||
                    kind == TK::Operator_OpenBrace ||
                    tree.TokenText(i).find_first_of("\r\n") !=
                        std::string_view::npos)
                {
                    return std::nullopt;
                }
            }
            std::string parameter;
            auto split = text.find('\n');
            if (split != std::string_view::npos) {
                if (!child->Child() ||
                    child->Child()->Kind != NK::CallExpression)
                {
                    return std::nullopt;
                }
                hasMultiline = true;
                size_t smallestIndent = std::numeric_limits<size_t>::max();
                for (auto start = split + 1; start < text.size();) {
                    auto end = text.find('\n', start);
                    if (end == std::string_view::npos) {
                        end = text.size();
                    }
                    auto first = start;
                    while (first < end && text[first] == ' ') {
                        ++first;
                    }
                    if (first == end || text[first] == '\t') {
                        return std::nullopt;
                    }
                    smallestIndent = std::min(smallestIndent, first - start);
                    start = end == text.size() ? end : end + 1;
                }
                if (smallestIndent < base - line) {
                    return std::nullopt;
                }
                const auto shift = static_cast<std::ptrdiff_t>(
                    indentWidth + style.IndentWidth) -
                    static_cast<std::ptrdiff_t>(smallestIndent);
                parameter.append(text.substr(0, split));
                for (auto start = split + 1; start < text.size();) {
                    auto end = text.find('\n', start);
                    if (end == std::string_view::npos) {
                        end = text.size();
                    }
                    auto first = start;
                    while (first < end && text[first] == ' ') {
                        ++first;
                    }
                    auto width = static_cast<std::ptrdiff_t>(first - start) +
                        shift;
                    if (width < 0) {
                        return std::nullopt;
                    }
                    parameter += '\n';
                    parameter.append(static_cast<size_t>(width), ' ');
                    parameter.append(text.substr(first, end - first));
                    start = end == text.size() ? end : end + 1;
                }
            } else {
                parameter = text;
            }
            parameters.push_back(std::move(parameter));
        } else if (child->Tokens.Size() != 1 ||
            tree.Kind(child->Tokens.Begin) != TK::Operator_Comma)
        {
            return std::nullopt;
        }
        cursor = to;
        expectArgument = !expectArgument;
    }
    if (expectArgument || parameters.size() < 2 || !hasMultiline ||
        !IsWhitespace(source.substr(cursor, closingBegin - cursor)) ||
        indentWidth + parameters.front().size() + 1 > style.ColumnLimit)
    {
        return std::nullopt;
    }
    std::string replacement;
    for (const auto& parameter : parameters) {
        if (!replacement.empty()) {
            replacement += ',';
        }
        replacement += "\n" + indent + parameter;
    }
    return Replacement{tree.Token(open).Text.End, closingBegin,
        std::move(replacement)};
}

// Reflow parsed calls only. The parser cannot resolve names, so leave
// ambiguous parenthesized casts source-owned. Lay out enclosing calls first
// so their comma breaks can resolve overflow before trying nested calls.
using CallFrontier = std::pmr::vector<
    std::pair<const parser::Node*, size_t>>;

// Layout edits may replace entire constructs while preserving every token.
// Recover changed gaps so positions can be updated without lexing the whole
// output. Refuse anything that is not an ordered token copy.
bool CollectWhitespaceEdits(
    const parser::SyntaxTree& tree,
    const std::vector<Replacement>& replacements,
    std::pmr::vector<parser::WhitespaceEdit>& gaps)
{
    const auto source = tree.Source();
    TokenIndex token = 0;
    for (const auto& replacement : replacements) {
        auto cursor = static_cast<size_t>(replacement.Begin);
        size_t rendered = 0;
        while (token < tree.TokenCount() &&
            tree.Token(token).Text.End <= cursor)
        {
            ++token;
        }
        if (token < tree.TokenCount() &&
            tree.Token(token).Text.Begin < cursor)
        {
            return false; // The edit begins inside a token.
        }
        while (token < tree.TokenCount() &&
            tree.Token(token).Text.Begin < replacement.End)
        {
            const auto span = tree.Token(token).Text;
            if (span.End > replacement.End) {
                return false;
            }
            const auto text = tree.TokenText(token);
            const auto next = replacement.Text.find(text, rendered);
            if (next == std::string::npos ||
                !IsWhitespace(std::string_view(replacement.Text).substr(
                    rendered,
                    next - rendered)))
            {
                return false;
            }
            auto oldGap = source.substr(cursor, span.Begin - cursor);
            auto newGap = std::string_view(replacement.Text).substr(
                rendered,
                next - rendered);
            if (oldGap != newGap) {
                gaps.push_back({static_cast<util::SourceOffset>(cursor),
                    span.Begin, newGap});
            }
            cursor = span.End;
            rendered = next + text.size();
            ++token;
        }
        const auto oldTail = source.substr(cursor, replacement.End - cursor);
        const auto newTail = std::string_view(replacement.Text).substr(
            rendered);
        if (!IsWhitespace(newTail)) {
            return false;
        }
        if (oldTail != newTail) {
            gaps.push_back({static_cast<util::SourceOffset>(cursor),
                replacement.End, newTail});
        }
    }
    return true;
}

std::optional<parser::SyntaxTree> TryRebindLayout(
    const parser::SyntaxTree& tree,
    LayoutEdits& layout)
{
    auto lexicalFallback = [&] {
        const auto length = layout.Output.size();
        layout.Output.append(util::SourcePaddingSize, '\0');
        util::PaddedSourceView source{
            .Data = layout.Output.data(), .Size = length};
        auto rebound = tree.RebindMatchingTokens(source);
        if (!rebound) {
            rebound = tree.RebindReorderedComments(source);
        }
        if (!rebound && layout.SortedIncludes) {
            rebound = tree.RebindSortedIncludes(source);
        }
        layout.Output.resize(length);
        return rebound;
    };
    if (layout.Edits.empty() && layout.Gaps.empty() &&
        layout.RenderedGaps.empty())
    {
        const auto length = layout.Output.size();
        layout.Output.append(util::SourcePaddingSize, '\0');
        auto rebound = tree.RebindWhitespaceGaps(
            {.Data = layout.Output.data(), .Size = length});
        layout.Output.resize(length);
        return rebound ? std::move(rebound) : lexicalFallback();
    }
    // RenderedGap offsets are resolved after reserving space for padding;
    // otherwise appending padding could invalidate views into Output.
    layout.Output.reserve(layout.Output.size() + util::SourcePaddingSize);
    std::byte buffer[1024];
    std::pmr::monotonic_buffer_resource scratch(buffer, sizeof(buffer));
    std::pmr::vector<parser::WhitespaceEdit> edits{&scratch};
    bool safe = !layout.Gaps.empty() || !layout.RenderedGaps.empty();
    if (!layout.Gaps.empty()) {
        edits.assign(layout.Gaps.begin(), layout.Gaps.end());
    } else if (!layout.RenderedGaps.empty()) {
        for (const auto& gap : layout.RenderedGaps) {
            edits.push_back({gap.Begin, gap.End,
                std::string_view(layout.Output).substr(
                    gap.OutputBegin,
                    gap.OutputSize)});
        }
    } else {
        safe = CollectWhitespaceEdits(tree, layout.Edits, edits);
    }
    if (!safe) {
        HIT_FMT_PROFILE_EVENT("edits.unrepresentable");
        return lexicalFallback();
    }
    const auto length = layout.Output.size();
    layout.Output.append(util::SourcePaddingSize, '\0');
    auto rebound = tree.RebindWhitespaceEdits(
        {.Data = layout.Output.data(), .Size = length},
        edits);
    layout.Output.resize(length);
    return rebound ? std::move(rebound) : lexicalFallback();
}

std::optional<LayoutEdits> FormatCalls(
    const parser::SyntaxTree& tree,
    const Style& style,
    size_t targetDepth,
    bool& deeperCalls,
    const std::vector<bool>* newlySplitTokens = nullptr,
    const CallFrontier* frontier = nullptr,
    CallFrontier* nextFrontier = nullptr)
{
    if (style.CallArgumentSplit == CallArgumentSplit::Preserve &&
        !style.EnforceMultilineSingleCallArgumentPosition)
    {
        return std::nullopt;
    }
    std::optional<Style> selectedCallStyle;
    std::vector<Replacement> edits;
    std::byte stackBuffer[1024];
    std::pmr::monotonic_buffer_resource scratch(
        stackBuffer, sizeof(stackBuffer));
    CallFrontier nodes{&scratch};
    if (frontier) {
        nodes.assign(frontier->rbegin(), frontier->rend());
    } else {
        nodes.emplace_back(&tree.Root(), 0);
    }
    while (!nodes.empty()) {
        auto [node, depth] = nodes.back();
        nodes.pop_back();
        if (IsIncompleteStatement(*node)) {
            continue;
        }
        if (node->Kind == NK::CallExpression && depth > targetDepth) {
            deeperCalls = true;
            if (nextFrontier) {
                nextFrontier->emplace_back(node, depth);
            }
            continue;
        }
        if (node->Kind == NK::CallExpression && depth == targetDepth) {
            const auto* callee = node->Child();
            const auto* arguments = callee ? callee->NextSibling : nullptr;
            if (callee && !callee->Tokens.Empty() && arguments &&
                arguments->Kind == NK::ArgumentList &&
                !arguments->Tokens.Empty() &&
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
                    std::optional<size_t> callLineString;
                    if (callee->Tokens.End == callee->Tokens.Begin + 1 &&
                        tree.Kind(callee->Tokens.Begin) == TK::Identifier)
                    {
                        auto name = tree.TokenText(callee->Tokens.Begin);
                        bool selected = std::ranges::find(
                            style.CallLineFirstStringNames,
                            name) != style.CallLineFirstStringNames.end();
                        if (selected) {
                            size_t position = 0;
                            for (auto* argument = arguments->Child();
                                argument;
                                argument = argument->NextSibling)
                            {
                                if (argument->Kind != NK::Argument) {
                                    continue;
                                }
                                if (argument->Tokens.End ==
                                        argument->Tokens.Begin + 1 &&
                                    tree.Kind(argument->Tokens.Begin) ==
                                        TK::Literal_String)
                                {
                                    callLineString = position;
                                    break;
                                }
                                ++position;
                            }
                        }
                    }
                    const bool selectedLimit =
                        style.CallArgumentSplitColumnLimit != 0 &&
                        callee->Tokens.End == callee->Tokens.Begin + 1 &&
                        tree.Kind(callee->Tokens.Begin) == TK::Identifier &&
                        std::ranges::find(
                            style.CallArgumentSplitColumnLimitNames,
                            tree.TokenText(callee->Tokens.Begin)) !=
                            style.CallArgumentSplitColumnLimitNames.end();
                    if (selectedLimit && !newlySplitTokens &&
                        style.CallArgumentSplit !=
                            CallArgumentSplit::Preserve &&
                        !selectedCallStyle)
                    {
                        selectedCallStyle.emplace(style);
                        selectedCallStyle->ColumnLimit = std::min(
                            style.ColumnLimit,
                            style.CallArgumentSplitColumnLimit);
                    }
                    auto outer = newlySplitTokens ||
                        style.CallArgumentSplit == CallArgumentSplit::Preserve
                        ? std::optional<Replacement>{}
                        : ReflowDelimitedList(tree, *arguments,
                            selectedLimit ? *selectedCallStyle : style,
                            callLineString);
                    if (!outer && !newlySplitTokens && !callLineString) {
                        outer = ReflowMultilineCallArguments(
                            tree,
                            *arguments,
                            selectedLimit ? *selectedCallStyle : style);
                    }
                    // Compare two safe one-break layouts for nested sole
                    // arguments. Prefer the inner call when the outer split
                    // would strand it, but use the outer split when it
                    // balances a preceding assignment with the RHS.
                    std::optional<Replacement> inner;
                    const auto* sole = arguments->Child();
                    const auto* nested = sole &&
                        sole->Kind == NK::Argument && !sole->NextSibling
                        ? sole->Child()
                        : nullptr;
                    if (!newlySplitTokens &&
                        style.CallArgumentSplit !=
                            CallArgumentSplit::Preserve &&
                        !selectedLimit && depth + 1 < MaxCallReflowDepth &&
                            nested &&
                        nested->Kind == NK::CallExpression)
                    {
                        const auto* nestedCallee = nested->Child();
                        const auto* nestedArguments = nestedCallee
                            ? nestedCallee->NextSibling
                            : nullptr;
                        if (nestedCallee &&
                            nestedCallee->Kind != NK::Expression &&
                            nestedCallee->Kind != NK::ParenthesizedExpression &&
                            nestedArguments &&
                            nestedArguments->Kind == NK::ArgumentList)
                        {
                            inner = ReflowDelimitedList(
                                tree,
                                *nestedArguments,
                                style);
                        }
                    }
                    bool preferNested = inner.has_value();
                    if (inner && outer) {
                        auto balance = [&] (const Replacement& edit)
                            -> std::optional<size_t> {
                            auto split = edit.Text.find('\n');
                            if (split == std::string::npos ||
                                edit.Text.find('\n', split + 1) !=
                                    std::string::npos)
                            {
                                return std::nullopt;
                            }
                            const auto source = tree.Source();
                            auto line = source.rfind('\n', edit.Begin);
                            line = line == std::string_view::npos
                                ? 0
                                : line + 1;
                            auto lineEnd = source.find('\n', edit.End);
                            if (lineEnd == std::string_view::npos) {
                                lineEnd = source.size();
                            }
                            auto first = edit.Begin - line + split;
                            auto last = edit.Text.size() - split - 1 +
                                lineEnd - edit.End;
                            if (std::max(first, last) > style.ColumnLimit) {
                                return std::nullopt;
                            }
                            return std::max(first, last) -
                                std::min(first, last);
                        };
                        auto outerBalance = balance(*outer);
                        auto innerBalance = balance(*inner);
                        if (outerBalance &&
                            (!innerBalance || *outerBalance < *innerBalance))
                        {
                            preferNested = false;
                        }
                    }
                    if (auto edit = preferNested ? std::nullopt :
                        std::move(outer))
                    {
                        edits.push_back(std::move(*edit));
                    } else if (const auto* argument = arguments->Child();
                        argument && !argument->NextSibling &&
                        argument->Kind == NK::Argument &&
                        (!newlySplitTokens ||
                            std::any_of(newlySplitTokens->begin() +
                                    argument->Tokens.Begin + 1,
                                newlySplitTokens->begin() +
                                    argument->Tokens.End,
                                [] (bool split) { return split; })) &&
                        !argument->Tokens.Empty() &&
                        argument->Tokens.End < arguments->Tokens.End &&
                        !HasRecoveryChild(*argument) &&
                        !IsIncompleteStatement(*argument) &&
                        tree.MatchingDelimiter(opening) ==
                            arguments->Tokens.End - 1)
                    {
                        // A multiline sole argument cannot use comma reflow.
                        // Move a safe argument opening, or shift all lambda
                        // body lines when moving a direct lambda.
                        const auto source = tree.Source();
                        auto start = tree.Token(opening).Text.End;
                        auto end =
                            tree.Token(argument->Tokens.Begin).Text.Begin;
                        auto argumentEnd = tree.Token(
                            argument->Tokens.End - 1).Text.End;
                        if (end >= start && argumentEnd >= end) {
                            auto gap = source.substr(start, end - start);
                            auto text = source.substr(end, argumentEnd - end);
                            if (IsWhitespace(gap) &&
                                gap.find('\r') == std::string_view::npos &&
                                text.find('\n') != std::string_view::npos &&
                                text.find('\r') == std::string_view::npos)
                            {
                                auto line = source.rfind('\n', start);
                                line = line == std::string_view::npos
                                    ? 0
                                    : line + 1;
                                auto lineEnd = source.find('\n', end);
                                auto lambda = argument->Child() &&
                                    argument->Child()->Kind ==
                                        NK::LambdaExpression;
                                bool opaque = false;
                                for (auto i = argument->Tokens.Begin;
                                    i < argument->Tokens.End;
                                    ++i)
                                {
                                    opaque |= tree.Kind(i) ==
                                            TK::Operator_OpenBrace ||
                                        tree.Kind(i) == TK::Comment ||
                                        tree.Kind(i) ==
                                            TK::PreprocessingDirective;
                                }
                                const bool nestedCall = argument->Child() &&
                                    argument->Child()->Kind ==
                                        NK::CallExpression;
                                const bool overlong = lineEnd !=
                                    std::string_view::npos &&
                                    lineEnd - line > style.ColumnLimit;
                                const auto* expression = argument->Child();
                                const auto* left = expression &&
                                        expression->Kind == NK::BinaryExpression
                                    ? expression->Child()
                                    : nullptr;
                                const auto* right = left
                                    ? left->NextSibling
                                    : nullptr;
                                // A fitting binary break already gives a
                                // multiline sole argument a stable layout.
                                const auto binaryBreak = right &&
                                    !right->Tokens.Empty() && newlySplitTokens
                                    ? (style.BinaryOperatorPlacement ==
                                            BinaryOperatorPlacement::Trailing
                                        ? right->Tokens.Begin
                                        : left->Tokens.End)
                                    : NoToken;
                                const bool newlySplitBinary = binaryBreak !=
                                        NoToken &&
                                    binaryBreak < newlySplitTokens->size() &&
                                    (*newlySplitTokens)[binaryBreak];
                                const bool newLine =
                                    gap.find('\n') != std::string_view::npos;
                                using Position =
                                    MultilineSingleCallArgumentPosition;
                                const auto position =
                                    style.MultilineSingleCallArgumentPosition;
                                const auto& s = style;
                                const auto enforce =
                                s.EnforceMultilineSingleCallArgumentPosition;
                                const bool moveToCallLine = enforce &&
                                    position == Position::CallLine &&
                                    gap.find('\n') == gap.rfind('\n') &&
                                    !opaque && !nestedCall;
                                const bool moveToNewLine = position ==
                                        Position::NewLine &&
                                    (enforce ||
                                        (newlySplitTokens &&
                                            !newlySplitBinary) ||
                                        overlong) && !opaque && !nestedCall;
                                if (newLine) {
                                    if (moveToCallLine) {
                                        auto spacing =
                                            style.SpaceInsideCallArguments ==
                                                SpaceInsideCallArguments::Space
                                                ? " "
                                                : "";
                                        edits.push_back({start, end, spacing});
                                    }
                                } else if (moveToNewLine ||
                                    (lambda && overlong))
                                {
                                    auto openBegin =
                                        tree.Token(opening).Text.Begin;
                                    auto prefix = source.substr(
                                        line,
                                        openBegin - line);
                                    auto leading =
                                        prefix.find_first_not_of(" \t");
                                    auto base = prefix.substr(0, leading);
                                    auto indent = style.CallArgumentIndent ==
                                            CallArgumentIndent::Continuation &&
                                            prefix.find('\t') ==
                                                std::string_view::npos
                                        ? GeneratedIndent(start - line, style)
                                        : std::string(base) +
                                            GeneratedIndentAfter(
                                                base,
                                                style.IndentWidth,
                                                style);
                                    if (lambda) {
                                        auto lambdaEdits =
                                            MoveMultilineLambdaArgument(
                                                tree,
                                                *argument,
                                                start,
                                                end,
                                                indent,
                                                base.size(),
                                                style);
                                        edits.insert(edits.end(),
                                            std::make_move_iterator(
                                                lambdaEdits.begin()),
                                            std::make_move_iterator(
                                                lambdaEdits.end()));
                                    } else {
                                        edits.push_back({start, end,
                                            "\n" + indent});
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        auto childDepth = depth + (node->Kind == NK::CallExpression ? 1 : 0);
        if (childDepth <= targetDepth + 1) {
            for (auto* child = node->Child();
                child;
                child = child->NextSibling)
            {
                nodes.emplace_back(child, childDepth);
            }
        }
    }
    if (edits.empty()) {
        return std::nullopt;
    }
    std::ranges::sort(edits, {}, &Replacement::Begin);
    std::string output;
    output.reserve(tree.Source().size());
    size_t cursor = 0;
    size_t accepted = 0;
    for (auto& edit : edits) {
        if (edit.Begin < cursor) {
            continue;
        }
        output.append(tree.Source().substr(cursor, edit.Begin - cursor));
        output += edit.Text;
        cursor = edit.End;
        if (&edit != &edits[accepted]) {
            edits[accepted] = std::move(edit);
        }
        ++accepted;
    }
    edits.erase(edits.begin() + accepted, edits.end());
    output.append(tree.Source().substr(cursor));
    return LayoutEdits{std::move(output), std::move(edits)};
}

std::optional<LayoutEdits> FormatSingleStatementLambdas(
    const parser::SyntaxTree& tree,
    const Style& style)
{
    if (style.SingleStatementLambdaBodyLayout !=
            SingleStatementLambdaBodyLayout::OneLineIfFits)
    {
        return std::nullopt;
    }
    const auto source = tree.Source();
    std::vector<Replacement> edits;
    std::vector<const parser::Node*> nodes{&tree.Root()};
    while (!nodes.empty()) {
        const auto* body = nodes.back();
        nodes.pop_back();
        for (auto* child = body->Child(); child; child = child->NextSibling) {
            nodes.push_back(child);
        }
        const auto* statement = SingleStatementLambdaChild(*body, tree);
        if (!statement || body->Tokens.End < body->Tokens.Begin + 3) {
            continue;
        }
        auto open = body->Tokens.Begin;
        auto close = body->Tokens.End - 1;
        if (tree.Kind(open) != TK::Operator_OpenBrace ||
            tree.Kind(close) != TK::Operator_CloseBrace ||
            tree.MatchingDelimiter(open) != close)
        {
            continue;
        }
        const auto begin = tree.Token(open).Text.End;
        const auto end = tree.Token(close).Text.Begin;
        auto start = tree.Token(statement->Tokens.Begin).Text.Begin;
        auto finish = tree.Token(statement->Tokens.End - 1).Text.End;
        auto before = source.substr(begin, start - begin);
        auto after = source.substr(finish, end - finish);
        if (!IsWhitespace(before) || !IsWhitespace(after) ||
            before.find('\r') != std::string_view::npos ||
            after.find('\r') != std::string_view::npos ||
            (before.find('\n') == std::string_view::npos &&
                after.find('\n') == std::string_view::npos))
        {
            continue;
        }
        auto line = source.rfind('\n', tree.Token(open).Text.Begin);
        line = line == std::string_view::npos ? 0 : line + 1;
        auto lineEnd = source.find('\n', tree.Token(close).Text.End);
        if (lineEnd == std::string_view::npos) {
            lineEnd = source.size();
        }
        auto statementText = source.substr(start, finish - start);
        auto suffix = source.substr(
            tree.Token(close).Text.End,
            lineEnd - tree.Token(close).Text.End);
        size_t width = begin - line + statementText.size() + 3 +
            suffix.size(); // { statement; } includes two spaces and }.
        if (width > style.ColumnLimit) {
            continue;
        }
        edits.push_back({begin, end,
            " " + std::string(statementText) + " "});
    }
    if (edits.empty()) {
        return std::nullopt;
    }
    std::ranges::sort(edits, {}, &Replacement::Begin);
    LayoutEdits layout;
    size_t cursor = 0;
    for (const auto& edit : edits) {
        if (edit.Begin < cursor) {
            continue;
        }
        layout.Output.append(source.substr(cursor, edit.Begin - cursor));
        layout.Output += edit.Text;
        cursor = edit.End;
    }
    layout.Output.append(source.substr(cursor));
    return layout;
}

// Split at parsed member-call boundaries; do not move arbitrary member
// accesses, multiline arguments, comments, or incomplete expressions.
std::vector<Replacement> ReflowChainedCall(
    const parser::SyntaxTree& tree,
    const parser::Node& outer,
    const Style& style)
{
    struct Segment
    {
        size_t GapBegin;
        size_t Begin;
        size_t End;
    };
    std::vector<Segment> segments;
    const parser::Node* base = &outer;
    auto source = tree.Source();
    while (base->Kind == NK::CallExpression) {
        auto* member = base->Child();
        if (!member || member->Kind != NK::MemberExpression ||
            !member->Child() || !member->Child()->NextSibling ||
            HasRecoveryChild(*base))
        {
            break;
        }
        auto* object = member->Child();
        auto* name = object->NextSibling;
        TokenIndex access = NoToken;
        for (auto i = object->Tokens.End; i < name->Tokens.Begin; ++i) {
            if (tree.Kind(i) == TK::Operator_ClassMemberAccess ||
                tree.Kind(i) == TK::Operator_IndirectClassMemberAccess)
            {
                access = i;
                break;
            }
        }
        if (access ==
            NoToken || object->Tokens.Empty() || base->Tokens.Empty())
        {
            break;
        }
        auto gapBegin = tree.Token(object->Tokens.End - 1).Text.End;
        auto begin = tree.Token(access).Text.Begin;
        auto end = tree.Token(base->Tokens.End - 1).Text.End;
        if (!IsWhitespace(source.substr(gapBegin, begin - gapBegin)) ||
            source.substr(gapBegin, begin - gapBegin).find('\r') !=
                std::string_view::npos ||
            source.substr(begin, end - begin).find_first_of("\r\n") !=
                std::string_view::npos)
        {
            break;
        }
        bool unsafe = false;
        for (auto i = access; i < base->Tokens.End; ++i) {
            unsafe |= tree.Kind(i) == TK::Comment ||
                tree.Kind(i) == TK::PreprocessingDirective;
        }
        if (unsafe) {
            break;
        }
        segments.push_back({gapBegin, begin, end});
        base = object;
    }
    if (segments.empty() ||
        (segments.size() == 1 && base->Kind != NK::CallExpression) ||
        base->Tokens.Empty() || HasRecoveryChild(*base))
    {
        return {};
    }
    std::ranges::reverse(segments);
    if (base->Kind != NK::CallExpression || !base->Child() ||
        base->Child()->Tokens.Empty())
    {
        return {};
    }
    auto* callee = base->Child();
    auto calleeBegin = tree.Token(callee->Tokens.Begin).Text.Begin;
    auto calleeEnd = tree.Token(callee->Tokens.End - 1).Text.End;
    // A simple configured name also selects namespace-qualified calls.
    // Do not mistake an arbitrary object method for a known root.
    bool objectMethod = false;
    for (auto i = callee->Tokens.Begin; i < callee->Tokens.End; ++i) {
        objectMethod |= tree.Kind(i) == TK::Operator_ClassMemberAccess ||
            tree.Kind(i) == TK::Operator_IndirectClassMemberAccess;
    }
    if (objectMethod || tree.Kind(callee->Tokens.End - 1) != TK::Identifier) {
        return {};
    }
    auto calleeName = source.substr(calleeBegin, calleeEnd - calleeBegin);
    auto terminalName = tree.TokenText(callee->Tokens.End - 1);
    if (std::ranges::find(style.ChainedCallRootNames, terminalName) ==
            style.ChainedCallRootNames.end() &&
        std::ranges::find(style.ChainedCallRootNames, calleeName) ==
            style.ChainedCallRootNames.end())
    {
        return {};
    }
    auto baseBegin = tree.Token(base->Tokens.Begin).Text.Begin;
    auto baseEnd = tree.Token(base->Tokens.End - 1).Text.End;
    if (source.substr(baseBegin, baseEnd - baseBegin).find_first_of("\r\n") !=
        std::string_view::npos)
    {
        return {};
    }
    auto lineStart = source.rfind('\n', baseBegin);
    lineStart = lineStart == std::string_view::npos ? 0 : lineStart + 1;
    auto prefix = source.substr(lineStart, baseBegin - lineStart);
    auto firstText = prefix.find_first_not_of(" \t");
    auto leading = prefix.substr(0, firstText);
    size_t indent = style.ChainedCallIndent == CallArgumentIndent::Continuation
        ? baseBegin - lineStart : leading.size() + style.IndentWidth;
    auto continuation = GeneratedIndent(indent, style);
    // The split condition belongs to this chain, not its enclosing line.
    // A long condition or ternary must not force a short nested call to split.
    size_t chainColumn = baseEnd - baseBegin;
    bool overlong = chainColumn > style.ColumnLimit;
    for (const auto& segment : segments) {
        auto gap = source.substr(
            segment.GapBegin,
            segment.Begin - segment.GapBegin);
        auto width = segment.End - segment.Begin;
        chainColumn = gap.find('\n') == std::string_view::npos
            ? chainColumn + width
            : width;
        overlong |= chainColumn > style.ColumnLimit;
    }
    if (style.ChainedCallSplitCondition ==
            ChainedCallSplitCondition::Overflow && !overlong)
    {
        return {};
    }
    std::vector<Replacement> edits;
    // The first member call belongs with a non-call receiver (object.Get()).
    // A call receiver (Bind().Next()) already supplies the first call.
    size_t first = base->Kind == NK::CallExpression ? 0 : 1;
    size_t column = first == 0 ? baseEnd - baseBegin
        : segments.front().End - baseBegin;
    for (size_t i = first; i < segments.size(); ++i) {
        const auto& segment = segments[i];
        auto oldGap = source.substr(
            segment.GapBegin,
            segment.Begin - segment.GapBegin);
        auto width = segment.End - segment.Begin;
        bool existingBreak = oldGap.find('\n') != std::string_view::npos;
        if (existingBreak && style.ChainedCallSplitCondition ==
                ChainedCallSplitCondition::Overflow)
        {
            column = width;
            continue;
        }
        bool breakBefore = style.ChainedCallLayout ==
            ChainedCallLayout::OnePerLine ||
            (style.ChainedCallSplitCondition ==
                ChainedCallSplitCondition::Always && i == first) ||
            column + width > style.ColumnLimit;
        std::string gap = breakBefore ? "\n" + continuation : "";
        if (gap != oldGap) {
            edits.push_back({segment.GapBegin, segment.Begin, std::move(gap)});
        }
        column = (breakBefore ? 0 : column) + width;
    }
    return edits;
}

std::optional<std::string> FormatChainedCalls(
    const parser::SyntaxTree& tree,
    const Style& style)
{
    if (style.ChainedCallSplitCondition ==
        ChainedCallSplitCondition::Preserve)
    {
        return std::nullopt;
    }
    std::vector<Replacement> edits;
    std::vector<const parser::Node*> nodes{&tree.Root()};
    while (!nodes.empty()) {
        auto* node = nodes.back();
        nodes.pop_back();
        if (IsIncompleteStatement(*node) || HasRecoveryChild(*node)) {
            continue;
        }
        auto chain = node->Kind == NK::CallExpression
            ? ReflowChainedCall(tree, *node, style)
            : std::vector<Replacement>{};
        if (!chain.empty()) {
            edits.insert(
                edits.end(),
                std::make_move_iterator(chain.begin()),
                std::make_move_iterator(chain.end()));
            // The callee contains the same chain boundaries. Visit only
            // independent arguments on subsequent layout passes.
            continue;
        }
        for (auto* child = node->Child(); child; child = child->NextSibling) {
            nodes.push_back(child);
        }
    }
    if (edits.empty()) {
        return std::nullopt;
    }
    std::ranges::sort(edits, {}, &Replacement::Begin);
    std::string output;
    auto source = tree.Source();
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

struct IncludeDirective
{
    std::string_view Path;
    std::string_view Text;
    bool Angled;
};

std::optional<IncludeDirective> ParseInclude(std::string_view text)
{
    if (text.find('\n') != std::string_view::npos ||
        text.find('\\') != std::string_view::npos)
    {
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

std::optional<LayoutEdits> FormatIncludes(const parser::SyntaxTree& tree)
{
    LayoutEdits layout;
    auto& output = layout.Output;
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
                        // Quoted headers before angled ones.
                        return !left.Angled;
                    }
                    return left.Path < right.Path;
                });
            for (TokenIndex index = begin; index < end; ++index) {
                const auto token = tree.Token(index).Text;
                output.append(
                    tree.Source().substr(cursor, token.Begin - cursor));
                output.append(directives[index - begin].Text);
                cursor = token.End;
            }
        }
        begin = end;
    }
    output.append(tree.Source().substr(cursor));
    if (output == tree.Source()) {
        return std::nullopt;
    }
    layout.SortedIncludes = true;
    return layout;
}

} // namespace

// Apply local, syntax-owned whitespace rules after structural reflow. Never
// touch a newline, comment, directive, or a recovered statement's prefix.
static std::optional<LayoutEdits> FormatTokenSpacing(
    const parser::SyntaxTree& tree,
    const Style& style)
{
    using PR = parser::PunctuationRole;
    const auto count = tree.TokenCount();
    std::byte scratchBuffer[8192];
    std::pmr::monotonic_buffer_resource scratch(
        scratchBuffer, sizeof(scratchBuffer));
    std::pmr::vector<TokenSpacing> gaps(count, TokenSpacing::Preserve,
        &scratch);
    std::pmr::vector<bool> unsafe(count, false, &scratch);
    std::pmr::vector<bool> separatedDeclarators(count, false, &scratch);
    std::pmr::vector<bool> newArrayPointers(count, false, &scratch);
    std::pmr::vector<bool> loopSemicolons(count, false, &scratch);
    std::pmr::vector<bool> conditionSemicolons(count, false, &scratch);
    // Type positions and parser-confirmed template-ids are the only safe
    // contexts for changing template argument gaps.
    std::pmr::vector<std::pair<const parser::Node*, bool>> nodes{&scratch};
    nodes.emplace_back(&tree.Root(), false);
    auto markColon = [&] (TokenIndex colon, ColonSpacing spacing) {
        if (spacing == ColonSpacing::Preserve || colon == 0 ||
            colon + 1 >= count || tree.Kind(colon) != TK::Operator_Colon)
        {
            return;
        }
        gaps[colon] = spacing == ColonSpacing::Left ||
                spacing == ColonSpacing::LeftAndRight
            ? TokenSpacing::Space
            : TokenSpacing::NoSpace;
        gaps[colon + 1] = spacing == ColonSpacing::Right ||
                spacing == ColonSpacing::LeftAndRight
            ? TokenSpacing::Space
            : TokenSpacing::NoSpace;
    };
    auto markInside = [&] (TokenIndex open, TokenIndex close,
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
    auto markBodyInterior = [&] (TokenIndex open, TokenIndex close,
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
    auto markEmpty = [&] (const parser::Node& list, TK opening,
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
    auto markKeywordParenthesis = [&] (TokenIndex keyword, TokenIndex end,
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
                i < node->Tokens.End && i < count;
                ++i)
            {
                unsafe[i] = true;
            }
            continue;
        }
        if (!HasRecoveryChild(*node)) {
            if (node->Kind == NK::NewTypeId) {
                for (auto* part = node->Child();
                    part;
                    part = part->NextSibling)
                {
                    if (part->Kind == NK::PointerOperator &&
                        part->Tokens.Begin < count)
                    {
                        newArrayPointers[part->Tokens.Begin] = true;
                    }
                }
            }
            if (node->Kind == NK::InitDeclaratorList) {
                bool afterComma = false;
                for (auto* item = node->Child();
                    item;
                    item = item->NextSibling)
                {
                    if (item->Kind == NK::ListSeparator) {
                        afterComma = true;
                    } else if (item->Kind == NK::InitDeclarator && afterComma) {
                        for (auto* declarator = item->Child();
                            declarator;
                            declarator = declarator->NextSibling)
                        {
                            if (declarator->Kind != NK::Declarator) {
                                continue;
                            }
                            for (auto* part = declarator->Child();
                                part;
                                part = part->NextSibling)
                            {
                                if (part->Kind == NK::PointerOperator &&
                                    part->Tokens.Begin < count)
                                {
                                    separatedDeclarators[part->Tokens.Begin] =
                                        true;
                                }
                            }
                        }
                        afterComma = false;
                    }
                }
            }
            const auto begin = node->Tokens.Begin;
            const auto end = node->Tokens.End;
            if (begin < count && end <= count) {
                if (node->Kind == NK::ExceptionSpecification) {
                    markKeywordParenthesis(
                        begin,
                        end,
                        TK::Keyword_noexcept,
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
                    markKeywordParenthesis(
                        begin,
                        end,
                        TK::Keyword_sizeof,
                        style.SizeofSpaceBeforeParentheses);
                    markKeywordParenthesis(
                        begin,
                        end,
                        TK::Keyword_alignof,
                        style.AlignofSpaceBeforeParentheses);
                    if (node->FirstChild &&
                        node->FirstChild->Kind ==
                            NK::ParenthesizedExpression &&
                        node->FirstChild->Tokens.Begin == begin + 1)
                    {
                        markKeywordParenthesis(
                            begin,
                            end,
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
                    markKeywordParenthesis(
                        begin,
                        end,
                        TK::Keyword_noexcept,
                        style.NoexceptSpaceBeforeParentheses);
                    markKeywordParenthesis(
                        begin,
                        end,
                        TK::Keyword_typeid,
                        style.TypeidSpaceBeforeParentheses);
                } else if (node->Kind == NK::RequiresExpression ||
                    node->Kind == NK::RequiresClause)
                {
                    markKeywordParenthesis(
                        begin,
                        end,
                        TK::Keyword_requires,
                        style.RequiresSpaceBeforeParentheses);
                } else if (node->Kind == NK::StaticAssertDeclaration) {
                    markKeywordParenthesis(
                        begin,
                        end,
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
                    gaps[begin + 1] = style.NamedCastSpaceBeforeTypeArguments;
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
                        markKeywordParenthesis(
                            i,
                            end,
                            TK::Keyword_alignas,
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
                            gaps[close] = style.EmptyNewArrayInteriorSpacing;
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
                    for (auto* child = node->Child();
                        child;
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
                    markInside(
                        begin,
                        end - 1,
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
                        markKeywordParenthesis(
                            i,
                            end,
                            TK::Keyword_decltype,
                            style.DecltypeSpaceBeforeParentheses);
                    }
                }
            }
            if (node->Kind == NK::LambdaCaptureList) {
                markEmpty(
                    *node,
                    TK::Operator_OpenBracket,
                    TK::Operator_CloseBracket,
                    PR::LambdaDelimiter,
                    style.EmptyLambdaCaptureSpacing);
            }
            if (node->Kind == NK::LambdaExpression ||
                node->Kind == NK::FunctionDeclarator ||
                node->Kind == NK::CallExpression)
            {
                const auto role = node->Kind == NK::CallExpression
                    ? PR::CallDelimiter
                    : PR::DeclarationDelimiter;
                const auto spacing = node->Kind == NK::LambdaExpression
                    ? style.EmptyLambdaParametersSpacing
                    : node->Kind == NK::FunctionDeclarator
                        ? style.EmptyFunctionParametersSpacing
                        : style.EmptyFunctionCallArgumentsSpacing;
                for (auto* child = node->Child();
                    child;
                    child = child->NextSibling)
                {
                    if (child->Kind == (node->Kind == NK::CallExpression
                            ? NK::ArgumentList
                            : NK::ParameterList) &&
                        !child->Tokens.Empty())
                    {
                        markEmpty(
                            *child,
                            TK::Operator_OpenParenthesis,
                            TK::Operator_CloseParenthesis,
                            role,
                            spacing);
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
                    ? TokenSpacing::Space
                    : TokenSpacing::NoSpace;
                gaps[arrow + 1] = policy == ReturnArrowSpacing::Right ||
                        policy == ReturnArrowSpacing::LeftAndRight
                    ? TokenSpacing::Space
                    : TokenSpacing::NoSpace;
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
                    markInside(
                        open,
                        close,
                        style.NestedDeclaratorInteriorSpacing);
                    if (style.NestedDeclaratorPointerSpacing !=
                        TokenSpacing::Preserve)
                    {
                        for (auto* child = node->Child();
                            child;
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
                gaps[node->Tokens.End] = style.MemberPointerDeclaratorSpacing;
            }
            if (node->Kind == NK::TemplateDeclaration &&
                style.TemplateKeywordSpacing != TokenSpacing::Preserve &&
                node->Tokens.Begin < count &&
                tree.Kind(node->Tokens.Begin) == TK::Keyword_template)
            {
                for (auto* child = node->Child();
                    child;
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
                markInside(
                    open,
                    node->Tokens.End - 1,
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
                    open > 0 && tree.Kind(open - 1) != TK::Operator_Assign &&
                    tree.Kind(open - 1) != TK::Keyword_return &&
                    tree.Kind(open - 1) != TK::Keyword_co_return)
                {
                    gaps[open] = style.BraceInitializerSpacing;
                }
                if (node->Tokens.End == open + 2) {
                    gaps[node->Tokens.End - 1] =
                        style.EmptyBraceInitializerInteriorSpacing;
                } else {
                    markInside(
                        open,
                        node->Tokens.End - 1,
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
                        ? TokenSpacing::Space
                        : TokenSpacing::NoSpace;
                    gaps[node->Tokens.End] = placement ==
                            OperatorOverloadOperatorSpacing::Right ||
                            placement ==
                                OperatorOverloadOperatorSpacing::LeftAndRight
                        ? TokenSpacing::Space
                        : TokenSpacing::NoSpace;
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
                        gaps[open + 2] = style.EmptyAttributeInteriorSpacing;
                    } else {
                        markInside(
                            open + 1,
                            close - 1,
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
                    ? NoToken
                    : node->Tokens.End - 1;
                const auto role = node->Kind == NK::ParenthesizedExpression
                    ? PR::GroupingDelimiter
                    : PR::ConditionDelimiter;
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
                        ? ColonSpacing::LeftAndRight
                        : ColonSpacing::NoSpace);
            }
            if (node->Kind == NK::RangeForSeparator &&
                style.RangeBasedForColonSpacing != TokenSpacing::Preserve &&
                node->Tokens.Begin < count &&
                tree.Role(node->Tokens.Begin) == PR::ConditionDelimiter)
            {
                markColon(node->Tokens.Begin,
                    style.RangeBasedForColonSpacing == TokenSpacing::Space
                        ? ColonSpacing::LeftAndRight
                        : ColonSpacing::NoSpace);
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
                    open < node->Tokens.End;
                    ++open)
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
                for (auto* child = node->Child();
                    child;
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
                        markBodyInterior(
                            open,
                            close,
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
                    markBodyInterior(
                        open,
                        close,
                        style.RequirementBodyInteriorSpacing);
                }
            }
            if ((node->Kind == NK::CompoundStatement ||
                    node->Kind == NK::LambdaBody) &&
                (style.BlockInteriorSpacing != TokenSpacing::Preserve ||
                    style.EmptyBlockInteriorSpacing !=
                        TokenSpacing::Preserve ||
                    style.EmptyLambdaBodySpacing !=
                        EmptyLambdaBodySpacing::Inherit) &&
                node->Tokens.Begin < count && node->Tokens.End <= count &&
                node->Tokens.End >= node->Tokens.Begin + 2)
            {
                const auto open = node->Tokens.Begin;
                const auto close = node->Tokens.End - 1;
                const auto role = node->Kind == NK::LambdaBody
                    ? PR::LambdaDelimiter
                    : PR::BlockDelimiter;
                if (tree.Kind(open) == TK::Operator_OpenBrace &&
                    tree.Kind(close) == TK::Operator_CloseBrace &&
                    tree.Role(open) == role && tree.Role(close) == role &&
                    tree.MatchingDelimiter(open) == close)
                {
                    if (close == open + 1) {
                        auto spacing = style.EmptyBlockInteriorSpacing;
                        if (node->Kind == NK::LambdaBody) {
                            switch (style.EmptyLambdaBodySpacing) {
                                case EmptyLambdaBodySpacing::Inherit:
                                    break;
                                case EmptyLambdaBodySpacing::Preserve:
                                    spacing = TokenSpacing::Preserve;
                                    break;
                                case EmptyLambdaBodySpacing::Space:
                                    spacing = TokenSpacing::Space;
                                    break;
                                case EmptyLambdaBodySpacing::NoSpace:
                                    spacing = TokenSpacing::NoSpace;
                                    break;
                            }
                        }
                        gaps[close] = spacing;
                    } else if (node->Kind != NK::LambdaBody ||
                        style.SingleStatementLambdaBodyLayout !=
                            SingleStatementLambdaBodyLayout::Preserve ||
                        !SingleStatementLambdaChild(*node, tree))
                    {
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
                for (auto* child = node->Child();
                    child;
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
                    i < node->Tokens.End && i < count;
                    ++i)
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
                (style.MemberAccessOperatorsSpacing ==
                        TightOperatorSpacing::NoSpace ||
                    style.ScopeResolutionSpacing ==
                        TightOperatorSpacing::NoSpace) &&
                node->FirstChild && node->FirstChild->NextSibling)
            {
                auto begin = node->FirstChild->Tokens.End;
                auto end = node->FirstChild->NextSibling->Tokens.Begin;
                for (auto i = begin; i < end && i + 1 < count; ++i) {
                    if (((tree.Kind(i) == TK::Operator_ClassMemberAccess ||
                            tree.Kind(i) ==
                                TK::Operator_IndirectClassMemberAccess) &&
                            style.MemberAccessOperatorsSpacing ==
                                TightOperatorSpacing::NoSpace) ||
                        (tree.Kind(i) == TK::Operator_ScopeResolution &&
                            style.ScopeResolutionSpacing ==
                                TightOperatorSpacing::NoSpace))
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
                gaps[node->Tokens.End - 1] = style.DeclarationSemicolonSpacing;
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
                auto mark = [&] (TokenIndex begin, TokenIndex end, TK kind) {
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
                mark(
                    condition->Tokens.End,
                    positive->Tokens.Begin,
                    TK::Operator_Conditional);
                mark(
                    positive->Tokens.End,
                    negative->Tokens.Begin,
                    TK::Operator_Colon);
            }
            if (node->Kind == NK::IfStatement ||
                node->Kind == NK::SwitchStatement)
            {
                if (style.ConditionInitSemicolonSpacing !=
                    TokenSpacing::Preserve)
                {
                    for (auto* child = node->Child();
                        child;
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
                for (auto* child = node->Child();
                    child;
                    child = child->NextSibling)
                {
                    if (child->Kind == NK::ListSeparator &&
                        child->Tokens.Begin < count &&
                        tree.Kind(child->Tokens.Begin) ==
                            TK::Operator_Semicolon)
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
                auto begin = node->Kind == NK::SubscriptExpression &&
                        node->FirstChild
                    ? node->FirstChild->Tokens.End
                    : node->Tokens.Begin;
                for (auto i = begin;
                    i < node->Tokens.End && i < count;
                    ++i)
                {
                    auto role = node->Kind == NK::SubscriptExpression
                        ? PR::CallDelimiter
                        : PR::DeclarationDelimiter;
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
                        if (node->Kind ==
                            NK::ArrayDeclarator && close == i + 1)
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
                    i < node->Tokens.End && i < count;
                    ++i)
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
                tree.Kind(node->Tokens.Begin) == TK::Operator_OpenParenthesis &&
                // `sizeof(T)` can be parsed as a cast in dependent code.
                // Its closing parenthesis is not a cast followed by an
                // operand, so never tighten the following operator gap.
                (node->Tokens.Begin == 0 ||
                    (tree.Kind(node->Tokens.Begin - 1) != TK::Keyword_sizeof &&
                        tree.Kind(node->Tokens.Begin - 1) !=
                            TK::Keyword_alignof &&
                        tree.Kind(node->Tokens.Begin - 1) !=
                            TK::Keyword_typeid &&
                        tree.Kind(node->Tokens.Begin - 1) !=
                            TK::Keyword_noexcept &&
                        tree.Kind(node->Tokens.Begin - 1) !=
                            TK::Keyword_decltype)))
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
        for (auto* child = node->FirstChild;
            child;
            child = child->NextSibling)
        {
            nodes.emplace_back(child, childTyped);
        }
    }
    auto pointer = [&] (TokenIndex i) {
        return tree.Role(i) == PR::PointerDeclarator ||
            tree.Role(i) == PR::ReferenceDeclarator;
    };
    for (TokenIndex i = 0; i < count; ++i) {
        if (pointer(i) &&
            style.PointerReferenceDeclaratorLocation !=
                PointerReferenceDeclaratorLocation::Preserve)
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
                    tree.Kind(last + 1) != TK::Keyword_volatile &&
                    !(newArrayPointers[first] &&
                        tree.Kind(last + 1) == TK::Operator_OpenBracket)))
            {
                continue;
            }
            auto location = style.PointerReferenceDeclaratorLocation;
            if (tree.Kind(first - 1) == TK::Operator_Comma) {
                // After a comma, a confirmed subsequent declarator has no
                // repeated type to attach to. An unconfirmed pointer (e.g.
                // an argument of an unknown macro) stays source-owned.
                if (separatedDeclarators[first]) {
                    gaps[last + 1] = location ==
                            PointerReferenceDeclaratorLocation::Middle
                        ? TokenSpacing::Space
                        : TokenSpacing::NoSpace;
                }
                continue;
            }
            gaps[first] = location == PointerReferenceDeclaratorLocation::Left
                ? TokenSpacing::NoSpace
                : TokenSpacing::Space;
            for (auto k = first + 1; k <= last; ++k) {
                gaps[k] = TokenSpacing::NoSpace;
            }
            // The array rule owns the bracket gap (including Preserve).
            // Pointer placement changes only the type-to-pointer gap here.
            if (!(newArrayPointers[first] &&
                    tree.Kind(last + 1) == TK::Operator_OpenBracket))
            {
                gaps[last + 1] = location ==
                        PointerReferenceDeclaratorLocation::Right
                    ? TokenSpacing::NoSpace
                    : TokenSpacing::Space;
            }
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
            // Only CommaSpacing owns the gap before a comma.
            if (tree.Kind(i + 1) != TK::Operator_Comma) {
                gaps[i + 1] = operatorStyle;
            }
        }
        // A fold's ellipsis is followed by its grouping delimiter, not
        // another operand. Its closing gap belongs to grouping spacing.
        if (kind == TK::Operator_Ellipsis && role == PR::BinaryOperator &&
            i + 1 < count &&
            tree.Kind(i + 1) == TK::Operator_CloseParenthesis &&
            tree.Role(i + 1) == PR::GroupingDelimiter)
        {
            gaps[i + 1] = style.GroupingInteriorSpacing;
        }
        if (kind == TK::Operator_Comma &&
            (role == PR::ListSeparator || role == PR::BinaryOperator) &&
            style.EnforceCommaSpacing && i + 1 < count)
        {
            gaps[i] = TokenSpacing::NoSpace;
            if (tree.Kind(i + 1) != TK::Operator_CloseParenthesis &&
                tree.Kind(i + 1) != TK::Operator_CloseBracket &&
                tree.Kind(i + 1) != TK::Operator_CloseBrace)
            {
                gaps[i + 1] = style.CommaSpacing == CommaSpacing::After ||
                        style.CommaSpacing == CommaSpacing::Space
                    ? TokenSpacing::Space
                    : TokenSpacing::NoSpace;
            }
        }
        if (style.ScopeResolutionSpacing ==
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
    std::vector<parser::WhitespaceEdit> gapEdits;
    size_t cursor = 0;
    bool changed = false;
    for (TokenIndex i = 0; i < count; ++i) {
        auto setting = gaps[i];
        if (setting == TokenSpacing::Preserve) {
            continue;
        }
        auto gap = tree.WhitespaceBefore(i);
        // A binary expression ending just before a comma may have marked
        // this gap earlier. The comma policy takes precedence, even when it
        // asks to preserve source whitespace.
        if ((tree.Kind(i) == TK::Operator_Comma ||
                (i > 0 && tree.Kind(i - 1) == TK::Operator_Comma)) &&
            !style.EnforceCommaSpacing)
        {
            setting = TokenSpacing::Preserve;
        }
        const auto memberPointer = [] (TK kind) {
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
            auto edge = [] (char ch) {
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
                tree.Kind(i) != TK::Operator_CloseBrace)
            {
                setting = TokenSpacing::Preserve;
            }
        }
        const bool replace =
            (setting == TokenSpacing::Space && gap != " ") ||
            (setting == TokenSpacing::NoSpace && !gap.empty());
        if (!replace) {
            continue;
        }
        const auto begin = i == 0 ? size_t{0} : tree.Token(i - 1).Text.End;
        const auto end = tree.Token(i).Text.Begin;
        if (!changed) {
            output.reserve(tree.Source().size());
        }
        output.append(tree.Source().substr(cursor, begin - cursor));
        if (setting == TokenSpacing::Space) {
            output.push_back(' ');
        }
        gapEdits.push_back({static_cast<util::SourceOffset>(begin),
            static_cast<util::SourceOffset>(end),
            setting == TokenSpacing::Space ? " " : ""});
        cursor = end;
        changed = true;
    }
    if (!changed) {
        return std::nullopt;
    }
    output.append(tree.Source().substr(cursor));
    return LayoutEdits{std::move(output), {}, std::move(gapEdits)};
}

// Only horizontal trivia before standalone directives and trailing line
// comments is eligible. Directive bodies, continuations, and comment text
// stay byte-for-byte identical.
static std::string FormatLineOwnedSpacing(
    const parser::SyntaxTree& tree,
    const Style& style)
{
    enum class Directive { Other, If, Branch, EndIf };
    auto directiveKind = [] (std::string_view text) {
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
            style.PreprocessorIndentation !=
                PreprocessorIndentation::Preserve &&
            tree.Kind(i) == TK::PreprocessingDirective &&
            tree.TokenText(i).find('\n') == std::string_view::npos &&
            (balanced || style.PreprocessorIndentation ==
                PreprocessorIndentation::LineStart))
        {
            const auto newline = gap.rfind('\n');
            const auto indentStart = newline == std::string_view::npos
                ? 0
                : newline + 1;
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
                    output += GeneratedIndent(level * style.IndentWidth, style);
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

// Place closing delimiters independently of list and block reflow. Edit
// whitespace following a real final token (or remove a safe trailing comma);
// comments and directives own their line, and empty constructs have no final
// element to attach the closer to.
static std::optional<LayoutEdits> FormatClosingDelimiters(
    const parser::SyntaxTree& tree,
    const Style& style)
{
    const auto source = tree.Source();
    std::vector<Replacement> edits;
    std::vector<const parser::Node*> nodes{&tree.Root()};
    while (!nodes.empty()) {
        const auto* node = nodes.back();
        nodes.pop_back();
        for (auto* child = node->Child(); child; child = child->NextSibling) {
            nodes.push_back(child);
        }
        ClosingDelimiterPlacement placement =
            ClosingDelimiterPlacement::SameLine;
        bool enforce = false;
        const parser::Node* target = node;
        std::string_view closing;
        switch (node->Kind) {
            case NK::ParameterList:
                enforce = style.EnforceFunctionParameterClosingParenthesis;
                placement = style.FunctionParameterClosingParenthesis;
                closing = ")";
                break;
            case NK::ArgumentList:
                enforce = style.EnforceFunctionCallArgumentClosingParenthesis;
                placement = style.FunctionCallArgumentClosingParenthesis;
                closing = ")";
                break;
            case NK::Initializer:
                // Unknown typed temporaries can contain a braced initializer
                // without a parsed element list. Only its whitespace-only
                // closing gap is safe to adjust; never reshape the contents.
                for (auto* child = node->Child();
                    child;
                    child = child->NextSibling)
                {
                    if (child->Kind == NK::UnparsedSyntax &&
                        !child->Tokens.Empty() &&
                        tree.TokenText(child->Tokens.Begin) == "{")
                    {
                        target = child;
                        break;
                    }
                }
                if (target == node) break;
                [[fallthrough]];
            case NK::InitializerList:
                enforce = style.EnforceInitializerClosingBrace;
                placement = style.InitializerClosingBrace ==
                    InitializerClosingBrace::NewLine
                    ? ClosingDelimiterPlacement::NewLine
                    : ClosingDelimiterPlacement::SameLine;
                closing = "}";
                break;
            case NK::CompoundStatement:
            case NK::LambdaBody:
                enforce = style.EnforceBlockClosingBrace;
                placement = style.BlockClosingBrace;
                closing = "}";
                break;
            default:
                continue;
        }
        if (!enforce || target->Tokens.End - target->Tokens.Begin < 3) {
            continue;
        }
        const auto open = target->Tokens.Begin;
        const auto close = target->Tokens.End - 1;
        const auto last = close - 1;
        if (tree.TokenText(close) != closing ||
            tree.Kind(last) == TK::Comment ||
            tree.Kind(last) == TK::PreprocessingDirective)
        {
            continue;
        }
        const auto begin = tree.Token(last).Text.End;
        const auto end = tree.Token(close).Text.Begin;
        if (end < begin) {
            continue;
        }
        const auto gap = source.substr(begin, end - begin);
        if (!IsWhitespace(gap) || gap.find('\r') != std::string_view::npos) {
            continue;
        }
        const bool separate = gap.find('\n') != std::string_view::npos;
        auto closingNewline = [&] {
            const auto openBegin = tree.Token(open).Text.Begin;
            const auto lineStart = source.rfind('\n', openBegin);
            size_t indent = lineStart == std::string_view::npos
                ? 0 : lineStart + 1;
            while (indent < openBegin &&
                (source[indent] == ' ' || source[indent] == '\t'))
            {
                ++indent;
            }
            const auto base = lineStart == std::string_view::npos
                ? 0
                : lineStart + 1;
            return "\n" + GeneratedIndent(indent - base, style);
        };
        const bool initializer = node->Kind == NK::InitializerList ||
            node->Kind == NK::Initializer;
        if (initializer && separate &&
            placement == ClosingDelimiterPlacement::SameLine &&
            close + 2 < tree.TokenCount() &&
            tree.Kind(close + 1) == TK::Operator_Comma &&
            tree.Kind(close + 2) == TK::Operator_OpenBrace &&
            tree.MatchingDelimiter(close + 2) != NoToken &&
            tree.WhitespaceBefore(close + 1).find('\n') ==
                std::string_view::npos &&
            tree.WhitespaceBefore(close + 2).find('\n') ==
                std::string_view::npos)
        {
            // The closing brace anchors a sibling initializer boundary;
            // moving it would also pull the next '{' onto the prior line.
            continue;
        }
        if (initializer && node->Kind == NK::InitializerList &&
            placement == ClosingDelimiterPlacement::SameLine)
        {
            const parser::Node* finalElement = nullptr;
            for (auto* child = node->Child();
                child;
                child = child->NextSibling)
            {
                if (child->Kind == NK::InitializerElement) {
                    finalElement = child;
                }
            }
            if (finalElement && !finalElement->Tokens.Empty()) {
                auto from = tree.Token(finalElement->Tokens.Begin).Text.Begin;
                auto to = tree.Token(finalElement->Tokens.End - 1).Text.End;
                if (source.substr(from, to - from).find('\n') !=
                    std::string_view::npos)
                {
                    continue;
                }
            }
        }
        if (initializer && placement == ClosingDelimiterPlacement::SameLine &&
            tree.Kind(last) == TK::Operator_Comma)
        {
            if (style.InitializerTrailingComma ==
                InitializerTrailingComma::Keep)
            {
                if (!separate) {
                    if (node->Kind == NK::InitializerList && source.substr(
                        tree.Token(open).Text.Begin,
                        end - tree.Token(open).Text.Begin).find('\n') ==
                            std::string_view::npos)
                    {
                        auto splitStyle = style;
                        splitStyle.InitializerListSplit =
                            FunctionParameterSplit::OnePerLine;
                        splitStyle.FirstInitializerElementPosition =
                            FirstInitializerElementPosition::NewLine;
                        splitStyle.ColumnLimit = 0;
                        if (auto split = ReflowDelimitedList(
                            tree, *node, splitStyle))
                        {
                            split->Text += closingNewline();
                            edits.push_back(std::move(*split));
                            continue;
                        }
                    }
                    edits.push_back({begin, end, closingNewline()});
                }
            } else if (last > open + 1 &&
                tree.Kind(last - 1) != TK::Comment &&
                tree.Kind(last - 1) != TK::PreprocessingDirective)
            {
                const auto previousEnd = tree.Token(last - 1).Text.End;
                const auto commaBegin = tree.Token(last).Text.Begin;
                if (previousEnd <= commaBegin && IsWhitespace(source.substr(
                    previousEnd,
                    commaBegin - previousEnd)) &&
                    source.substr(previousEnd, commaBegin - previousEnd)
                        .find('\r') == std::string_view::npos)
                {
                    edits.push_back({previousEnd, end,
                        style.InitializerBracesSpacing == TokenSpacing::Space
                            ? " "
                            : ""});
                }
            }
            continue;
        }
        if (initializer && node->Kind == NK::Initializer &&
            placement == ClosingDelimiterPlacement::SameLine &&
            tree.Kind(last) == TK::Operator_CloseParenthesis)
        {
            auto match = tree.MatchingDelimiter(last);
            if (match != NoToken && match > open &&
                source.substr(tree.Token(match).Text.Begin,
                    tree.Token(last).Text.End -
                        tree.Token(match).Text.Begin).find('\n') !=
                    std::string_view::npos)
            {
                continue;
            }
        }
        if (placement == ClosingDelimiterPlacement::SameLine) {
            if (!separate) {
                continue;
            }
            bool space = node->Kind == NK::CompoundStatement ||
                node->Kind == NK::LambdaBody;
            if (node->Kind == NK::InitializerList ||
                node->Kind == NK::Initializer)
            {
                space = style.InitializerBracesSpacing == TokenSpacing::Space;
            }
            if (node->Kind == NK::ArgumentList) {
                space = style.SpaceInsideCallArguments ==
                    SpaceInsideCallArguments::Space;
            }
            if (node->Kind == NK::ParameterList) {
                space = style.SpaceInsideFunctionParameters ==
                    SpaceInsideFunctionParameters::Space;
            }
            edits.push_back({begin, end, space ? " " : ""});
        } else if (!separate) {
            edits.push_back({begin, end, closingNewline()});
        }
    }
    if (edits.empty()) {
        return std::nullopt;
    }
    std::ranges::sort(edits, {}, &Replacement::Begin);
    std::string result;
    result.reserve(source.size());
    size_t cursor = 0;
    size_t accepted = 0;
    for (auto& edit : edits) {
        if (edit.Begin < cursor) {
            continue;
        }
        result.append(source.substr(cursor, edit.Begin - cursor));
        result += edit.Text;
        cursor = edit.End;
        if (&edit != &edits[accepted]) {
            edits[accepted] = std::move(edit);
        }
        ++accepted;
    }
    edits.erase(edits.begin() + accepted, edits.end());
    result.append(source.substr(cursor));
    return LayoutEdits{std::move(result), std::move(edits)};
}

// Unknown template types can leave otherwise ordinary declarations
// ambiguous. Normalize only an unmistakable `Type<...> name =` prefix;
// never guess at the remainder of the statement.
static std::string FormatAmbiguousTemplateDeclarationSpacing(
    const parser::SyntaxTree& tree,
    const Style& style)
{
    if (style.AmbiguousTemplateDeclarationSpacing ==
            AmbiguousTemplateDeclarationSpacing::Preserve)
    {
        return std::string(tree.Source());
    }
    const auto source = tree.Source();
    std::vector<Replacement> edits;
    std::vector<const parser::Node*> nodes{&tree.Root()};
    while (!nodes.empty()) {
        const auto* node = nodes.back();
        nodes.pop_back();
        for (auto* child = node->Child(); child; child = child->NextSibling) {
            nodes.push_back(child);
        }
        auto first = node->Tokens.Begin;
        if (node->Kind != NK::AmbiguousStatement ||
            node->Tokens.End < first + 6 || !node->Child() ||
            node->Child()->Kind != NK::UnknownTypeName ||
            tree.Kind(first) != TK::Identifier ||
            tree.Kind(first + 1) != TK::Operator_Less ||
            tree.Kind(node->Tokens.End - 1) != TK::Operator_Semicolon)
        {
            continue;
        }
        int angles = 0;
        TokenIndex close = NoToken;
        bool unsafe = false;
        for (auto i = first + 1; i < node->Tokens.End; ++i) {
            auto kind = tree.Kind(i);
            if (kind == TK::Comment || kind == TK::PreprocessingDirective) {
                unsafe = true;
                break;
            }
            if (kind == TK::Operator_Less) {
                ++angles;
            } else if (kind == TK::Operator_Greater) {
                --angles;
            } else if (kind == TK::Operator_RightShift) {
                angles -= 2;
            }
            if (angles == 0) {
                close = i;
                break;
            }
            if (angles < 0) {
                break;
            }
        }
        if (unsafe || close == NoToken || close + 2 >= node->Tokens.End ||
            tree.Kind(close + 1) != TK::Identifier ||
            tree.Kind(close + 2) != TK::Operator_Assign)
        {
            continue;
        }
        auto setSpace = [&] (TokenIndex before, TokenIndex after) {
            auto begin = tree.Token(before).Text.End;
            auto end = tree.Token(after).Text.Begin;
            if (end < begin || !IsWhitespace(
                source.substr(begin, end - begin)) ||
                source.substr(begin, end - begin).find_first_of("\r\n") !=
                    std::string_view::npos ||
                (end - begin == 1 && source[begin] == ' '))
            {
                return;
            }
            edits.push_back({begin, end, " "});
        };
        setSpace(close, close + 1);
        if (style.AssignmentOperatorSpacing == TokenSpacing::Space) {
            setSpace(close + 1, close + 2);
        }
    }
    std::ranges::sort(edits, {}, &Replacement::Begin);
    std::string output;
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

// Trim line-end trivia without editing raw strings, literal continuations,
// or preprocessing tokens. A trailing backslash may be a line splice.
static std::optional<LayoutEdits> TrimTrailingWhitespace(
    const parser::SyntaxTree& tree)
{
    const auto source = tree.Source();
    std::vector<Replacement> edits;
    size_t token = 0;
    for (size_t begin = 0; begin < source.size();) {
        const auto newline = source.find('\n', begin);
        const bool hasNewline = newline != std::string_view::npos;
        const auto end = hasNewline ? newline : source.size();
        const auto contentEnd = end > begin && source[end - 1] == '\r'
            ? end - 1
            : end;
        auto trim = contentEnd;
        while (trim > begin && (source[trim - 1] == ' ' ||
                source[trim - 1] == '\t'))
        {
            --trim;
        }
        while (token < tree.TokenCount() && tree.Token(token).Text.End <=
            trim)
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
        if (trim != contentEnd) {
            edits.push_back({trim, contentEnd, ""});
        }
        begin = hasNewline ? end + 1 : end;
    }
    if (edits.empty()) {
        return std::nullopt;
    }
    return RenderLayoutEdits(source, std::move(edits));
}

namespace {

struct DisabledRegion
{
    size_t Begin;
    size_t End;
};

// Markers must occupy a whole comment line; text inside strings, directives,
// or trailing comments must not disable formatting. The protected range
// includes both marker lines so even their original indentation is retained.
template <class Visit>
std::vector<DisabledRegion> FindDisabledRegionsImpl(
    std::string_view source,
    Visit&& visit)
{
    std::vector<DisabledRegion> regions;
    size_t begin = 0;
    size_t depth = 0;
    visit([&] (const tokenizer::Token& comment) {
        auto text = source.substr(
            comment.Text.Begin,
            comment.Text.Size());
        if (text.starts_with("//")) {
            while (!text.empty() && (text.back() == ' ' ||
                    text.back() == '\t' || text.back() == '\r'))
            {
                text.remove_suffix(1);
            }
        }
        const bool off = text == "// hit-fmt off" ||
            text == "// clang-format off" ||
            text == "/* hit-fmt off */" ||
            text == "/* clang-format off */";
        const bool on = text == "// hit-fmt on" ||
            text == "// clang-format on" ||
            text == "/* hit-fmt on */" ||
            text == "/* clang-format on */";
        if (!off && !on) {
            return;
        }
        const auto token = comment.Text;
        const auto previousNewline = source.rfind('\n', token.Begin);
        const auto lineStart = previousNewline == std::string_view::npos
            ? 0
            : previousNewline + 1;
        const auto newline = source.find('\n', token.End);
        const auto lineEnd = newline == std::string_view::npos
            ? source.size()
            : newline + 1;
        auto whitespace = [] (std::string_view text) {
            return text.find_first_not_of(" \t\r") == std::string_view::npos;
        };
        if (!whitespace(source.substr(lineStart, token.Begin - lineStart)) ||
            !whitespace(source.substr(token.End,
                (newline == std::string_view::npos ? source.size() : newline) -
                    token.End)))
        {
            return;
        }
        if (off) {
            if (depth++ == 0) {
                begin = lineStart;
            }
        } else if (depth != 0 && --depth == 0) {
            regions.push_back({begin, lineEnd});
        }
    });
    if (depth != 0) {
        regions.push_back({begin, source.size()});
    }
    return regions;
}

std::vector<DisabledRegion> FindDisabledRegions(
    const parser::SyntaxTree& tree)
{
    return FindDisabledRegionsImpl(tree.Source(), [&] (auto&& found) {
        for (TokenIndex i = 0; i < tree.TokenCount(); ++i) {
            if (tree.Kind(i) == TK::Comment) {
                found(tree.Token(i));
            }
        }
    });
}

std::vector<DisabledRegion> FindDisabledRegions(
    util::PaddedSourceView source)
{
    tokenizer::Tokenizer lexer(source);
    return FindDisabledRegionsImpl(source.View(), [&] (auto&& found) {
        for (auto token : lexer) {
            if (token.Kind == TK::Comment) {
                found(token);
            }
        }
    });
}

bool IsDisabledOffset(
    const std::vector<DisabledRegion>& regions,
    size_t offset)
{
    auto after = std::upper_bound(regions.begin(), regions.end(), offset,
        [] (size_t position, const DisabledRegion& region) {
            return position < region.Begin;
        });
    return after != regions.begin() && offset < (after - 1)->End;
}

struct SensitiveInvocation
{
    size_t Begin;
    size_t End;
    std::string_view Name;
};

std::optional<std::vector<SensitiveInvocation>> FindSensitiveInvocations(
    const parser::SyntaxTree& tree,
    const Style& style)
{
    std::vector<SensitiveInvocation> invocations;
    for (TokenIndex i = 0; i + 1 < tree.TokenCount(); ++i) {
        if (tree.Kind(i) != TK::Identifier ||
            tree.Kind(i + 1) != TK::Operator_OpenParenthesis ||
            std::ranges::find(
                style.WhitespaceSensitiveMacros,
                tree.TokenText(i)) == style.WhitespaceSensitiveMacros.end())
        {
            continue;
        }
        const auto close = tree.MatchingDelimiter(i + 1);
        if (close == NoToken || close <= i + 1 || close >= tree.TokenCount()) {
            return std::nullopt;
        }
        invocations.push_back({tree.Token(i).Text.Begin,
            tree.Token(close).Text.End, tree.TokenText(i)});
        i = close;
    }
    return invocations;
}

std::optional<std::vector<SensitiveInvocation>> FindSensitiveInvocations(
    util::PaddedSourceView source,
    const Style& style)
{
    bool error = false;
    tokenizer::Tokenizer lexer(source,
        [&] (tokenizer::TokenizationError) { error = true; });
    std::vector<tokenizer::Token> tokens;
    for (auto token : lexer) {
        tokens.push_back(token);
    }
    if (error) {
        return std::nullopt;
    }
    std::vector<TokenIndex> matching(tokens.size(), NoToken);
    std::vector<TokenIndex> stack;
    for (TokenIndex i = 0; i < tokens.size(); ++i) {
        const auto kind = tokens[i].Kind;
        if (kind == TK::Operator_OpenParenthesis ||
            kind == TK::Operator_OpenBrace ||
            kind == TK::Operator_OpenBracket)
        {
            stack.push_back(i);
        } else if (kind == TK::Operator_CloseParenthesis ||
            kind == TK::Operator_CloseBrace ||
            kind == TK::Operator_CloseBracket)
        {
            if (stack.empty()) {
                continue;
            }
            const auto open = tokens[stack.back()].Kind;
            if ((open == TK::Operator_OpenParenthesis &&
                    kind == TK::Operator_CloseParenthesis) ||
                (open == TK::Operator_OpenBrace &&
                    kind == TK::Operator_CloseBrace) ||
                (open == TK::Operator_OpenBracket &&
                    kind == TK::Operator_CloseBracket))
            {
                matching[stack.back()] = i;
                stack.pop_back();
            }
        }
    }
    std::vector<SensitiveInvocation> invocations;
    for (TokenIndex i = 0; i + 1 < tokens.size(); ++i) {
        if (tokens[i].Kind != TK::Identifier ||
            tokens[i + 1].Kind != TK::Operator_OpenParenthesis)
        {
            continue;
        }
        auto name = source.View().substr(
            tokens[i].Text.Begin,
            tokens[i].Text.Size());
        if (std::ranges::find(
            style.WhitespaceSensitiveMacros,
            name) == style.WhitespaceSensitiveMacros.end())
        {
            continue;
        }
        const auto close = matching[i + 1];
        if (close == NoToken) {
            return std::nullopt;
        }
        invocations.push_back({tokens[i].Text.Begin,
            tokens[close].Text.End, name});
        i = close;
    }
    return invocations;
}

std::string RestoreSensitiveInvocations(
    const parser::SyntaxTree& original,
    const Style& style,
    std::string formatted)
{
    HIT_FMT_PROFILE_PHASE("formatter.restore_sensitive");
    if (style.WhitespaceSensitiveMacros.empty()) {
        return formatted;
    }
    const auto before = FindSensitiveInvocations(original, style);
    if (!before) {
        return std::string(original.Source());
    }
    if (before->empty()) {
        return formatted;
    }
    const auto length = formatted.size();
    formatted.append(util::SourcePaddingSize, '\0');
    util::PaddedSourceView resultSource{
        .Data = formatted.data(), .Size = length};
    auto resultTree = original.RebindWhitespaceGaps(resultSource);
    const auto after = resultTree
        ? FindSensitiveInvocations(*resultTree, style)
        : FindSensitiveInvocations(resultSource, style);
    if (!after || after->size() != before->size()) {
        return std::string(original.Source());
    }
    std::string result;
    result.reserve(length);
    size_t cursor = 0;
    for (size_t i = 0; i < before->size(); ++i) {
        const auto& oldInvocation = (*before)[i];
        const auto& newInvocation = (*after)[i];
        if (oldInvocation.Name != newInvocation.Name ||
            newInvocation.Begin < cursor || newInvocation.End > length)
        {
            return std::string(original.Source());
        }
        result.append(formatted.data() + cursor, newInvocation.Begin - cursor);
        result.append(original.Source().substr(
            oldInvocation.Begin,
            oldInvocation.End - oldInvocation.Begin));
        cursor = newInvocation.End;
    }
    result.append(formatted.data() + cursor, length - cursor);
    return result;
}

std::string RestoreDisabledRegions(
    const parser::SyntaxTree& original,
    std::string formatted)
{
    HIT_FMT_PROFILE_PHASE("formatter.restore_disabled");
    const auto regions = FindDisabledRegions(original);
    if (regions.empty()) {
        return formatted;
    }
    const auto length = formatted.size();
    formatted.append(util::SourcePaddingSize, '\0');
    util::PaddedSourceView resultSource{
        .Data = formatted.data(), .Size = length};
    auto resultTree = original.RebindWhitespaceGaps(resultSource);
    const auto resultRegions = resultTree
        ? FindDisabledRegions(*resultTree)
        : FindDisabledRegions(resultSource);
    if (regions.size() != resultRegions.size()) {
        return std::string(original.Source());
    }
    std::string result;
    result.reserve(length);
    size_t cursor = 0;
    for (size_t i = 0; i < regions.size(); ++i) {
        if (resultRegions[i].Begin < cursor || resultRegions[i].End > length) {
            return std::string(original.Source());
        }
        result.append(
            formatted.data() + cursor,
            resultRegions[i].Begin - cursor);
        result.append(original.Source().substr(
            regions[i].Begin,
            regions[i].End - regions[i].Begin));
        cursor = resultRegions[i].End;
    }
    result.append(formatted.data() + cursor, length - cursor);
    return result;
}

} // namespace

// Only promise a width fix when the formatted source preserves the token
// stream and every resulting line occupied by the original line fits.
static std::vector<bool> FixableLineWidths(
    const parser::SyntaxTree& tree,
    const Style& style)
{
    auto source = tree.Source();
    auto lineCount = std::count(source.begin(), source.end(), '\n') + 2;
    std::vector<bool> fixable(lineCount, false);
    auto formatted = Format(tree, style);
    if (formatted == source) {
        return fixable;
    }
    auto length = formatted.size();
    formatted.append(util::SourcePaddingSize, '\0');
    auto result = parser::SyntaxTree::TokenizeOnly(
        {.Data = formatted.data(), .Size = length});
    bool sameTokens = result.TokenCount() == tree.TokenCount();
    if (sameTokens) {
        for (TokenIndex i = 0; i < tree.TokenCount(); ++i) {
            if (tree.Kind(i) != result.Kind(i) ||
                tree.TokenText(i) != result.TokenText(i))
            {
                sameTokens = false;
                break;
            }
        }
    }
    std::vector<bool> lineFits{false};
    for (size_t start = 0; start < length;) {
        auto end = formatted.find('\n', start);
        if (end == std::string::npos || end > length) {
            end = length;
        }
        auto width = end - start;
        if (width && formatted[end - 1] == '\r') {
            --width;
        }
        lineFits.push_back(width <= style.ColumnLimit);
        start = end == length ? length : end + 1;
    }
    std::vector<TokenIndex> first(fixable.size(), NoToken);
    std::vector<TokenIndex> last(fixable.size(), NoToken);
    std::vector<bool> multiLineToken(fixable.size(), false);
    for (TokenIndex i = 0; i < tree.TokenCount(); ++i) {
        auto line = tree.TokenPosition(i).Line;
        if (line >= fixable.size()) {
            continue;
        }
        auto text = tree.TokenText(i);
        auto newlines = std::count(text.begin(), text.end(), '\n');
        if (newlines) {
            for (size_t j = line;
                j <= line + newlines && j < multiLineToken.size();
                ++j)
            {
                multiLineToken[j] = true;
            }
            continue;
        }
        if (first[line] == NoToken) {
            first[line] = i;
        }
        last[line] = i;
    }
    auto hashToken = [] (const parser::SyntaxTree& syntax,
        TokenIndex index) {
            auto text = std::hash<std::string_view>{}(
            syntax.TokenText(index));
            return text ^ (static_cast<size_t>(syntax.Kind(index)) << 1);
        };
    auto hashTriple = [&] (const parser::SyntaxTree& syntax,
        TokenIndex index) {
            auto hash = hashToken(syntax, index);
            for (TokenIndex i = 1; i < 3; ++i) {
            hash ^= hashToken(syntax, index + i) +
                size_t{0x9e3779b9} + (hash << 6) + (hash >> 2);
            }
            return hash;
        };
    std::unordered_map<size_t, std::vector<TokenIndex>> positions;
    if (!sameTokens) {
        for (TokenIndex i = 0; i + 2 < result.TokenCount(); ++i) {
            positions[hashTriple(result, i)].push_back(i);
        }
    }
    for (size_t line = 1; line < fixable.size(); ++line) {
        if (first[line] == NoToken || multiLineToken[line]) {
            continue;
        }
        auto beginIndex = first[line];
        auto endIndex = last[line];
        if (!sameTokens) {
            // An unrelated token edit must not hide a fixable line. Match
            // only a unique, unchanged run of tokens from that line.
            if (endIndex - beginIndex < 2) {
                continue;
            }
            auto it = positions.find(hashTriple(tree, beginIndex));
            if (it == positions.end() || it->second.size() > 64) {
                continue;
            }
            TokenIndex matched = NoToken;
            for (auto index : it->second) {
                if (endIndex - beginIndex >= result.TokenCount() - index) {
                    continue;
                }
                bool equal = true;
                for (TokenIndex offset = 0;
                    offset <= endIndex - beginIndex;
                    ++offset)
                {
                    equal &= tree.Kind(beginIndex + offset) ==
                            result.Kind(index + offset) &&
                        tree.TokenText(beginIndex + offset) ==
                            result.TokenText(index + offset);
                    if (!equal) {
                        break;
                    }
                }
                if (equal) {
                    if (matched != NoToken) {
                        matched = NoToken;
                        break;
                    }
                    matched = index;
                }
            }
            if (matched == NoToken) {
                continue;
            }
            endIndex = matched + endIndex - beginIndex;
            beginIndex = matched;
        }
        auto begin = result.TokenPosition(beginIndex).Line;
        auto end = result.TokenPosition(endIndex).Line;
        if (begin == 0 || begin > end || end >= lineFits.size()) {
            continue;
        }
        fixable[line] = std::all_of(
            lineFits.begin() + begin,
            lineFits.begin() + end + 1,
            [] (bool fits) { return fits; });
    }
    return fixable;
}

std::vector<LayoutDiagnostic> CheckExpressionLayout(
    const parser::SyntaxTree& tree,
    const Style& style)
{
    std::vector<LayoutDiagnostic> diagnostics;
    const auto source = tree.Source();
    const auto disabled = FindDisabledRegions(tree);
    uint32_t lineNumber = 1;
    if (style.LineLengthCheck != LineLengthCheck::Preserve) {
        for (size_t start = 0; start < source.size(); ++lineNumber) {
            auto end = source.find('\n', start);
            if (end == std::string_view::npos) {
                end = source.size();
            }
            auto width = end - start;
            if (width && source[end - 1] == '\r') {
                --width;
            }
            if (width > style.ColumnLimit &&
                !IsDisabledOffset(disabled, start))
            {
                diagnostics.push_back({
                    {.Line = lineNumber,
                     .Column = static_cast<uint32_t>(style.ColumnLimit + 1)},
                    "line exceeds ColumnLimit (" + std::to_string(width) +
                        " > " + std::to_string(style.ColumnLimit) + ")",
                    LayoutDiagnosticKind::LineWidth});
            }
            start = end == source.size() ? end : end + 1;
        }
        if (style.LineLengthCheck == LineLengthCheck::Soft &&
            !diagnostics.empty())
        {
            auto fixable = FixableLineWidths(tree, style);
            std::erase_if(diagnostics, [&] (const LayoutDiagnostic& issue) {
                return issue.Kind == LayoutDiagnosticKind::LineWidth &&
                    (issue.Position.Line >= fixable.size() ||
                        !fixable[issue.Position.Line]);
            });
        }
    }
    if (style.ExpressionLayoutCondition ==
        ExpressionLayoutCondition::Preserve)
    {
        return diagnostics;
    }
    std::vector<const parser::Node*> nodes{&tree.Root()};
    auto checkPlacement = [&] (TokenIndex index, bool leading,
        std::string_view name) {
            if (!SafeOperatorGap(tree, index) ||
            IsDisabledOffset(disabled, tree.Token(index).Text.Begin))
        {
                return;
            }
            bool left = tree.WhitespaceBefore(index).find('\n') !=
            std::string_view::npos;
            bool right = tree.WhitespaceAfter(index).find('\n') !=
            std::string_view::npos;
            if ((left || right) && (leading
                ? !left || right
                : !right || left))
            {
                const auto& token = tree.Token(index);
                diagnostics.push_back({token.Position, std::string(name) +
                (leading
                    ? " must start the following line"
                    : " must end the preceding line"),
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
                            role ==
                                parser::PunctuationRole::ComparisonOperator ||
                            role == parser::PunctuationRole::ShiftOperator))
                    {
                        checkPlacement(i, style.BinaryOperatorPlacement ==
                            BinaryOperatorPlacement::Leading,
                            "binary operator");
                    }
                }
            }
        } else if (node->Kind == NK::AssignmentExpression) {
            const auto* left = node->Child();
            const auto* right = left ? left->NextSibling : nullptr;
            if (left && right && !left->Tokens.Empty() &&
                !right->Tokens.Empty())
            {
                for (auto i = left->Tokens.End; i < right->Tokens.Begin; ++i) {
                    if (tree.Role(i) ==
                            parser::PunctuationRole::AssignmentOperator)
                    {
                        checkPlacement(i, style.AssignmentOperatorPlacement ==
                            BinaryOperatorPlacement::Leading,
                            "assignment operator");
                    }
                }
            }
        } else if (auto delimiters = ConditionalDelimiters(tree, *node)) {
            auto [question, colon] = *delimiters;
            if (style.EnforceTernaryOperatorPlacement) {
                bool leading = style.TernaryOperatorPlacement ==
                    TernaryOperatorPlacement::Leading;
                checkPlacement(question, leading, "ternary ?");
                checkPlacement(colon, leading, "ternary :");
            }
            if (style.TernarySplit == TernarySplit::ThreeLines &&
                SafeOperatorGap(tree, question) &&
                SafeOperatorGap(tree, colon) &&
                !IsDisabledOffset(disabled, tree.Token(question).Text.Begin) &&
                !IsDisabledOffset(disabled, tree.Token(colon).Text.Begin))
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

// Normalize standalone slash rulers, not arbitrary comments or slash text
// inside a raw string. Short // and /// comments are never separators.
std::vector<Replacement> CollectSeparatorEdits(
    const parser::SyntaxTree& tree,
    size_t length)
{
    auto source = tree.Source();
    std::vector<Replacement> edits;
    for (TokenIndex i = 0; i < tree.TokenCount(); ++i) {
        if (tree.Kind(i) != TK::Comment) {
            continue;
        }
        auto text = tree.TokenText(i);
        if (text.size() < 8 ||
            text.find_first_not_of('/') != std::string_view::npos)
        {
            continue;
        }
        auto begin = tree.Token(i).Text.Begin;
        auto end = tree.Token(i).Text.End;
        auto lineStart = source.rfind('\n', begin);
        lineStart = lineStart == std::string_view::npos ? 0 : lineStart + 1;
        auto lineEnd = source.find('\n', end);
        if (lineEnd == std::string_view::npos) {
            lineEnd = source.size();
        }
        auto before = source.substr(lineStart, begin - lineStart);
        auto after = source.substr(end, lineEnd - end);
        if (before.find_first_not_of(" \t") == std::string_view::npos &&
            after.find_first_not_of(" \t\r") == std::string_view::npos &&
            (begin != lineStart || text.size() != length))
        {
            edits.push_back({lineStart, end, std::string(length, '/')});
        }
    }
    return edits;
}

std::string Format(const parser::SyntaxTree& tree, const Style& requested)
{
    HIT_FMT_PROFILE_PHASE("formatter.total");
    Style style = requested;
    if (style.EnforceTernarySplit &&
        style.ExpressionLayoutCondition ==
            ExpressionLayoutCondition::Preserve)
    {
        style.ExpressionLayoutCondition =
            ExpressionLayoutCondition::Overflow;
    }
    // Keep source-owned layouts until their enforcement switch is enabled.
    // The requested preferences remain available for newly reflowed headers.
    if (!style.EnforceFunctionParameterSplit) {
        style.FunctionParameterSplit = FunctionParameterSplit::Preserve;
    }
    if (!style.EnforceCallArgumentSplit) {
        style.CallArgumentSplit = CallArgumentSplit::Preserve;
    }
    if (!style.EnforceInitializerListSplit) {
        style.InitializerListSplit = FunctionParameterSplit::Preserve;
    }
    if (!style.EnforceTernarySplit) {
        style.TernarySplit = TernarySplit::Preserve;
    }
    if (!style.EnforceClassBracePlacement) {
        style.ClassBracePlacement = BracePlacement::Preserve;
    }
    if (!style.EnforceStructBracePlacement) {
        style.StructBracePlacement = BracePlacement::Preserve;
    }
    if (!style.EnforceUnionBracePlacement) {
        style.UnionBracePlacement = BracePlacement::Preserve;
    }
    if (!style.EnforceFunctionBracePlacement) {
        style.FunctionBracePlacement = BracePlacement::Preserve;
    }
    if (!style.EnforceLambdaBracePlacement) {
        style.LambdaBracePlacement = BracePlacement::Preserve;
    }
    if (!style.EnforceNamespaceBracePlacement) {
        style.NamespaceBracePlacement = BracePlacement::Preserve;
    }
    // The preferences are used in generated layouts. Unchanged gaps are
    // source-owned unless their independent enforcement switch is enabled.
    Style existing = style;
    if (!style.EnforceClassBraceSpacing) {
        existing.ClassBraceSpacing = SameLineBraceSpacing::Preserve;
    }
    if (!style.EnforceStructBraceSpacing) {
        existing.StructBraceSpacing = SameLineBraceSpacing::Preserve;
    }
    if (!style.EnforceUnionBraceSpacing) {
        existing.UnionBraceSpacing = SameLineBraceSpacing::Preserve;
    }
    if (!style.EnforceFunctionBraceSpacing) {
        existing.FunctionBraceSpacing = SameLineBraceSpacing::Preserve;
    }
    if (!style.EnforceLambdaBraceSpacing) {
        existing.LambdaBraceSpacing = SameLineBraceSpacing::Preserve;
    }
    if (!style.EnforceNamespaceBraceSpacing) {
        existing.NamespaceBraceSpacing = SameLineBraceSpacing::Preserve;
    }
    if (!style.EnforceSpaceInsideFunctionParameters) {
        existing.SpaceInsideFunctionParameters =
            SpaceInsideFunctionParameters::Preserve;
    }
    if (!style.EnforceSpaceInsideCallArguments) {
        existing.SpaceInsideCallArguments = SpaceInsideCallArguments::Preserve;
    }
    if (!style.EnforceInitializerBracesSpacing) {
        existing.InitializerBracesSpacing = TokenSpacing::Preserve;
    }
    if (!style.EnforceEmptyBlockInteriorSpacing) {
        existing.EmptyBlockInteriorSpacing = TokenSpacing::Preserve;
    }
    if (!style.EnforceControlBlockSpaceBeforeBrace) {
        existing.ControlBlockSpaceBeforeBrace = TokenSpacing::Preserve;
    }
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
        existing.ClassBraceSpacing == SameLineBraceSpacing::Preserve &&
        existing.LambdaBraceSpacing == SameLineBraceSpacing::Preserve &&
        existing.StructBraceSpacing == SameLineBraceSpacing::Preserve &&
        existing.EnumBraceSpacing == SameLineBraceSpacing::Preserve &&
        existing.UnionBraceSpacing == SameLineBraceSpacing::Preserve &&
        existing.FunctionBraceSpacing == SameLineBraceSpacing::Preserve &&
        existing.NamespaceBraceSpacing == SameLineBraceSpacing::Preserve &&
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
        style.LambdaCallSpaceBeforeArguments ==
            LambdaCallSpaceBeforeArguments::Inherit &&
        style.SpaceInsideCallArguments ==
            SpaceInsideCallArguments::Preserve &&
        style.SpaceInsideFunctionParameters ==
            SpaceInsideFunctionParameters::Preserve &&
        style.ControlBlockKeywordSpacing ==
            ControlBlockKeywordSpacing::Preserve &&
        !style.EnforceControlBlockBracePlacement;
    std::string formatted;
    // Most passes leave the text unchanged. Keep the parsed tree until a pass
    // actually edits it rather than tokenizing the same source repeatedly.
    // Keep a scratch syntax owner and its source alive while later
    // whitespace-only edits borrow its nodes. Replace it on a structural edit.
    struct SyntaxOwner
    {
        std::string Source;
        parser::SyntaxTree Tree;
    };
    std::unique_ptr<SyntaxOwner> syntaxOwner;
    std::optional<parser::SyntaxTree> cachedTree;
    size_t revision = 0;
    bool unsafeStructuralEdit = false;
    std::string beforeHeaderReflow;
    std::optional<parser::SyntaxTree> beforeHeaderTree;
    bool captureHeaderSource = false;
    bool capturedHeaderSource = false;
#ifdef HIT_FMT_PROFILE_PHASES
    uint32_t lastEditLine = 0;
#endif
    auto currentTree = [&] () -> const parser::SyntaxTree& {
        HIT_FMT_PROFILE_PHASE("formatter.tree_refresh");
        if (formatted == tree.Source()) {
            return tree;
        }
        // Some specialized passes below still update formatted directly.
        // Discard a cached tree if one of them changed its input.
        if (cachedTree && cachedTree->Source() != formatted) {
            cachedTree.reset();
            syntaxOwner.reset();
        }
        if (!cachedTree) {
            syntaxOwner.reset();
            const auto length = formatted.size();
            formatted.append(util::SourcePaddingSize, '\0');
            util::PaddedSourceView current{
                .Data = formatted.data(), .Size = length};
            if (auto rebound = tree.RebindWhitespaceGaps(current)) {
                cachedTree.emplace(std::move(*rebound));
            } else if (auto rebound = tree.RebindMatchingTokens(current)) {
                cachedTree.emplace(std::move(*rebound));
                HIT_FMT_PROFILE_EVENT("parser.rebind_lexical_fallback");
            } else if (auto rebound = tree.RebindReorderedComments(current)) {
                cachedTree.emplace(std::move(*rebound));
                HIT_FMT_PROFILE_EVENT("parser.rebind_reordered_fallback");
            } else {
                HIT_FMT_PROFILE_EVENT(
                    "formatter.unsafe_structure_preserved");
#ifdef HIT_FMT_PROFILE_PHASES
                if (util::profile::Enabled()) {
                    util::profile::GetResults().Count(
                        "formatter.unsafe_after_change",
                        lastEditLine);
                }
#endif
                // Do not interpret changed syntax through stale nodes.
                // Leave this file untouched when no verified overlay exists.
                unsafeStructuralEdit = true;
                formatted = std::string(tree.Source());
                return tree;
            }
            formatted.resize(length);
        }
        return *cachedTree;
    };
    auto captureHeader = [&] (const parser::SyntaxTree& parsed) {
        if (!captureHeaderSource || capturedHeaderSource) {
            return;
        }
        beforeHeaderReflow = formatted;
        const auto length = beforeHeaderReflow.size();
        beforeHeaderReflow.append(util::SourcePaddingSize, '\0');
        beforeHeaderTree.emplace(parsed.CloneSyntax(
            {.Data = beforeHeaderReflow.data(), .Size = length}));
        beforeHeaderReflow.resize(length);
        capturedHeaderSource = true;
    };
    auto applyPass = [&] (auto&& pass HIT_FMT_PROFILE_PASS_ARG) {
        HIT_FMT_PROFILE_PASS();
        const auto& parsed = currentTree();
        auto result = pass(parsed);
        if (result == formatted) {
            return false;
        }
        HIT_FMT_PROFILE_EDIT(lastEditLine);
        captureHeader(parsed);
        cachedTree.reset();
        syntaxOwner.reset();
        formatted = std::move(result);
        ++revision;
        return true;
    };
    auto applyOptionalPass = [&] (auto&& pass HIT_FMT_PROFILE_PASS_ARG) {
        HIT_FMT_PROFILE_PASS();
        const auto& parsed = currentTree();
        auto result = pass(parsed);
        if (!result || *result == formatted) {
            return false;
        }
        HIT_FMT_PROFILE_EDIT(lastEditLine);
        captureHeader(parsed);
        cachedTree.reset();
        syntaxOwner.reset();
        formatted = std::move(*result);
        ++revision;
        return true;
    };
    auto rebindLayout = [&] (const parser::SyntaxTree& parsed,
        LayoutEdits& layout) -> std::optional<parser::SyntaxTree> {
        if (&parsed != &tree && !parsed.BorrowsSyntax()) {
            HIT_FMT_PROFILE_EVENT("edits.owned_syntax");
            return std::nullopt;
        }
        return TryRebindLayout(parsed, layout);
    };
    auto applyLayoutPass = [&] (auto&& pass HIT_FMT_PROFILE_PASS_ARG) {
        HIT_FMT_PROFILE_PASS();
        const auto& parsed = currentTree();
        auto layout = pass(parsed);
        if (!layout || layout->Output == formatted) {
            return false;
        }
        HIT_FMT_PROFILE_EDIT(lastEditLine);
        captureHeader(parsed);
        std::optional<parser::SyntaxTree> rebound;
        if (&parsed != &tree && !parsed.BorrowsSyntax()) {
            auto retained = std::make_unique<SyntaxOwner>(SyntaxOwner{
                std::move(formatted), std::move(*cachedTree)});
            cachedTree.reset();
            if (retained->Tree.Source().data() == retained->Source.data()) {
                syntaxOwner = std::move(retained);
                rebound = TryRebindLayout(syntaxOwner->Tree, *layout);
                if (rebound) {
                    HIT_FMT_PROFILE_EVENT("edits.owner_adopted");
                }
            } else {
                // Small-string moves can change the source address.
                formatted = std::move(retained->Source);
            }
        } else {
            rebound = rebindLayout(parsed, *layout);
        }
        cachedTree.reset();
        const auto* data = layout->Output.data();
        formatted = std::move(layout->Output);
        if (rebound && formatted.data() == data) {
            cachedTree.emplace(std::move(*rebound));
            HIT_FMT_PROFILE_EVENT("edits.cached_rebound");
        } else {
            syntaxOwner.reset();
            if (rebound) {
                HIT_FMT_PROFILE_EVENT("edits.moved_output");
            }
        }
        ++revision;
        return true;
    };
    bool initialTokensReordered = false;
    {
        HIT_FMT_PROFILE_PHASE("formatter.initial_layout");
        auto applyParameters = [&] (const parser::SyntaxTree& parsed) {
            auto layout = FormatParameters(parsed, style);
            if (!layout) {
                formatted = std::string(parsed.Source());
                return;
            }
            auto rebound = rebindLayout(parsed, *layout);
            const auto* data = layout->Output.data();
            formatted = std::move(layout->Output);
            if (rebound && formatted.data() == data) {
                cachedTree.emplace(std::move(*rebound));
            }
        };
        if (bracesPreserved &&
            style.FunctionParameterSplit != FunctionParameterSplit::Preserve)
        {
            applyParameters(tree);
        } else {
            auto braceLayout = *RenderFormatter(tree, existing);
            if (braceLayout.Output == tree.Source() &&
                style.FunctionParameterSplit !=
                    FunctionParameterSplit::Preserve)
            {
                applyParameters(tree);
            } else {
                initialTokensReordered = braceLayout.RenderedGaps.empty() &&
                    braceLayout.Output != tree.Source();
                auto braceRebound = rebindLayout(tree, braceLayout);
                if (style.FunctionParameterSplit ==
                    FunctionParameterSplit::Preserve)
                {
                    const auto* data = braceLayout.Output.data();
                    formatted = std::move(braceLayout.Output);
                    if (braceRebound && formatted.data() == data) {
                        cachedTree.emplace(std::move(*braceRebound));
                    }
                } else {
                    const auto length = braceLayout.Output.size();
                    braceLayout.Output.append(util::SourcePaddingSize, '\0');
                    util::PaddedSourceView bracedSource{
                        .Data = braceLayout.Output.data(), .Size = length};
                    auto bracedTree = std::move(braceRebound);
                    if (!bracedTree) {
                        bracedTree = tree.RebindWhitespaceGaps(bracedSource);
                    }
                    if (!bracedTree) {
                        HIT_FMT_PROFILE_EVENT(
                            "formatter.unsafe_initial_braces_preserved");
                        return std::string(tree.Source());
                    }
                    auto parameters = FormatParameters(*bracedTree, style);
                    if (parameters) {
                        auto rebound = rebindLayout(*bracedTree, *parameters);
                        const auto* data = parameters->Output.data();
                        formatted = std::move(parameters->Output);
                        if (rebound && formatted.data() == data) {
                            cachedTree.emplace(std::move(*rebound));
                        }
                    } else {
                        const auto* data = braceLayout.Output.data();
                        braceLayout.Output.resize(length);
                        formatted = std::move(braceLayout.Output);
                        if (formatted.data() == data) {
                            cachedTree.emplace(std::move(*bracedTree));
                        }
                    }
                }
            }
        }
    }
    // Early structural passes only rearrange existing tokens. A missing
    // construct cannot appear until AddControlBraces runs below.
    bool hasStaticAssert = false;
    bool hasBrace = false;
    bool hasControl = false;
    bool hasMemberAccess = false;
    bool hasCaptureBracket = false;
    bool hasNamespace = false;
    bool hasComment = false;
    bool hasConditional = false;
    for (TokenIndex i = 0; i < tree.TokenCount(); ++i) {
        const auto kind = tree.Kind(i);
        hasStaticAssert |= kind == TK::Keyword_static_assert;
        hasBrace |= kind == TK::Operator_OpenBrace;
        hasControl |= kind == TK::Keyword_if || kind == TK::Keyword_for ||
            kind == TK::Keyword_while || kind == TK::Keyword_do ||
            kind == TK::Keyword_switch;
        hasMemberAccess |= kind == TK::Operator_ClassMemberAccess ||
            kind == TK::Operator_IndirectClassMemberAccess;
        hasCaptureBracket |= kind == TK::Operator_OpenBracket;
        hasNamespace |= kind == TK::Keyword_namespace;
        hasComment |= kind == TK::Comment;
        hasConditional |= kind == TK::Operator_Conditional;
    }
    if (style.CallArgumentSplit != CallArgumentSplit::Preserve &&
        hasStaticAssert)
    {
        applyPass([&] (const auto& parsed) {
            return FormatStaticAsserts(parsed, style);
        });
    }
    if (hasBrace &&
        style.InitializerListSplit != FunctionParameterSplit::Preserve)
    {
        // Outer edits can contain nested lists. Repeat until they stabilize;
        // if nesting exceeds the bound, leave initializer layout untouched.
        std::string beforeInitializers;
        bool stable = false;
        bool changed = false;
        for (size_t pass = 0; pass < 16; ++pass) {
            stable = !applyOptionalPass([&] (const auto& parsed) {
                auto result = FormatInitializers(parsed, style);
                if (result && !changed && *result != formatted) {
                    beforeInitializers = std::move(formatted);
                    changed = true;
                }
                return result;
            });
            if (stable) {
                break;
            }
        }
        if (!stable) {
            cachedTree.reset();
            syntaxOwner.reset();
            formatted = std::move(beforeInitializers);
        }
    }
    if (style.RemoveNullStatements &&
        (RemoveNullStatements(tree) ||
            (initialTokensReordered && RemoveNullStatements(currentTree()))))
    {
        // The removed statement is represented in scratch syntax; no
        // second syntax parse is needed after its semicolon disappears.
        const auto& parsed = currentTree();
        auto result = RemoveNullStatements(parsed);
        std::optional<parser::SyntaxTree> updated;
        if (result) {
            const auto length = result->Output.size();
            result->Output.append(util::SourcePaddingSize, '\0');
            updated = parsed.RebindRemovedNullStatements(
                {.Data = result->Output.data(), .Size = length},
                result->Removed);
            result->Output.resize(length);
        }
        cachedTree.reset();
        syntaxOwner.reset();
        if (result) {
            const auto* data = result->Output.data();
            formatted = std::move(result->Output);
            if (updated && formatted.data() == data) {
                cachedTree.emplace(std::move(*updated));
            }
        }
    }
    if (hasControl && (style.AddMissingControlBraces ||
        style.EmptyControlBody == EmptyControlBody::EmptyBlock))
    {
        for (size_t pass = 0; pass < 16; ++pass) {
            HIT_FMT_PROFILE_PHASE("formatter.control_braces");
            const auto& parsed = currentTree();
            auto result = AddControlBraces(parsed, style);
            if (!result || result->Output == formatted) {
                break;
            }
#ifdef HIT_FMT_PROFILE_PHASES
            const auto phaseLocation = std::source_location::current();
#endif
            HIT_FMT_PROFILE_EDIT(lastEditLine);
            const auto length = result->Output.size();
            result->Output.append(util::SourcePaddingSize, '\0');
            auto updated = parsed.RebindControlBraces(
                {.Data = result->Output.data(), .Size = length},
                result->Blocks);
            result->Output.resize(length);
            cachedTree.reset();
            syntaxOwner.reset();
            const auto* data = result->Output.data();
            formatted = std::move(result->Output);
            if (updated && formatted.data() == data) {
                cachedTree.emplace(std::move(*updated));
            }
            ++revision;
        }
    }
    if (hasControl && (style.AddMissingControlBraces ||
        style.EmptyControlBody == EmptyControlBody::EmptyBlock))
    {
        applyLayoutPass([&] (const auto& parsed) {
            return AttachElseToBrace(parsed);
        });
    }
    if (style.CollapseEmptyFunctionBodies) {
        applyLayoutPass([&] (const auto& parsed) {
            return CollapseEmptyFunctionBodies(parsed);
        });
    }
    if (style.NonEmptyBlockBodyLayout == NonEmptyBlockBodyLayout::Multiline) {
        applyLayoutPass([&] (const auto& parsed) {
            return FormatNonEmptyBlockBodies(parsed, style);
        });
    }
    if (hasCaptureBracket &&
        style.LambdaCaptureSplit != LambdaCaptureSplit::Preserve)
    {
        applyLayoutPass([&] (const auto& parsed) {
            return FormatLambdaCaptures(parsed, style);
        });
    }
    // Normalize statement indentation before creating call continuations.
    // Otherwise a tabbed or under-indented call retains its old base column.
    bool indentedBeforeReflow = false;
    size_t indentedRevision = 0;
    if (style.IndentBlockBodies &&
        style.CallArgumentSplit != CallArgumentSplit::Preserve)
    {
        // A changed pass can expose further edits on the next traversal.
        // Only a no-op pass is safe to reuse at the end of the pipeline.
        indentedBeforeReflow = !applyLayoutPass([&] (const auto& parsed) {
            return FormatBlockIndentation(parsed, style);
        });
        indentedRevision = revision;
    }
    const bool trackNewCallBreaks =
        style.CallArgumentSplit != CallArgumentSplit::Preserve &&
        style.ExpressionLayoutCondition !=
            ExpressionLayoutCondition::Preserve &&
        style.MultilineSingleCallArgumentPosition ==
            MultilineSingleCallArgumentPosition::NewLine &&
        !style.EnforceMultilineSingleCallArgumentPosition;
    const bool mayReflowHeader = trackNewCallBreaks ||
        style.CallArgumentSplit != CallArgumentSplit::Preserve ||
        style.ExpressionLayoutCondition !=
            ExpressionLayoutCondition::Preserve ||
        style.ControlHeaderClauseSplit != ControlHeaderClauseSplit::Preserve ||
        style.SingleStatementLambdaBodyLayout ==
            SingleStatementLambdaBodyLayout::OneLineIfFits;
    captureHeaderSource = mayReflowHeader &&
        (!style.EnforceControlBlockBracePlacement || trackNewCallBreaks);
    bool headerReflowed = false;
    if (hasConditional &&
        style.ExpressionLayoutCondition !=
            ExpressionLayoutCondition::Preserve &&
        style.TernarySplit == TernarySplit::ThreeLines)
    {
        // Give an overlong ternary inside a call argument its own breaks
        // before a call split hides the overflow on the enclosing line.
        headerReflowed |= applyLayoutPass([&] (const auto& parsed) {
            return CompleteTernaryBreaks(parsed, style);
        });
    }
    if (style.ExpressionLayoutCondition !=
        ExpressionLayoutCondition::Preserve)
    {
        headerReflowed |= applyLayoutPass([&] (const auto& parsed) {
            return BalanceDelimitedExpressions(parsed, style);
        });
    }
    if (style.CallArgumentSplit != CallArgumentSplit::Preserve ||
        style.EnforceMultilineSingleCallArgumentPosition)
    {
        if (style.CallArgumentSplit != CallArgumentSplit::Preserve) {
            applyPass([&] (const auto& parsed) {
                return JoinAssignmentBeforeCallReflow(parsed, style);
            });
        }
        // Carry unvisited nested calls forward while the source is unchanged.
        // Once a depth edits the source, resume from the updated tree.
        std::byte frontierBuffer[2048];
        std::pmr::monotonic_buffer_resource frontierScratch(
            frontierBuffer, sizeof(frontierBuffer));
        CallFrontier frontier{&frontierScratch};
        CallFrontier nextFrontier{&frontierScratch};
        bool useFrontier = false;
        for (size_t depth = 0; depth < MaxCallReflowDepth; ++depth) {
            bool deeperCalls = false;
            nextFrontier.clear();
            const bool changed = applyLayoutPass([&] (const auto& parsed) {
                return FormatCalls(
                    parsed,
                    style,
                    depth,
                    deeperCalls,
                    nullptr,
                    useFrontier ? &frontier : nullptr,
                    &nextFrontier);
            });
            headerReflowed |= changed;
            if (!deeperCalls) {
                break;
            }
            useFrontier = !changed;
            if (useFrontier) {
                frontier.swap(nextFrontier);
            }
        }
    }
    if (style.ControlHeaderClauseSplit != ControlHeaderClauseSplit::Preserve) {
        headerReflowed |= applyLayoutPass([&] (const auto& parsed) {
            return FormatControlHeaderClauses(parsed, style);
        });
    }
    if (style.ExpressionLayoutCondition !=
        ExpressionLayoutCondition::Preserve)
    {
        headerReflowed |= applyLayoutPass([&] (const auto& parsed) {
            return FormatExpressions(parsed, style);
        });
        if (hasConditional && style.TernarySplit == TernarySplit::ThreeLines) {
            if (applyLayoutPass([&] (const auto& parsed) {
                return CompleteTernaryBreaks(parsed, style);
            }))
            {
                headerReflowed = true;
                Style overflowStyle = style;
                overflowStyle.ExpressionLayoutCondition =
                    ExpressionLayoutCondition::Overflow;
                applyLayoutPass([&] (const auto& parsed) {
                    return FormatExpressions(parsed, overflowStyle);
                });
            }
        }
        if (hasConditional && style.TernarySplit == TernarySplit::ThreeLines &&
            style.TernaryOperatorPlacement == TernaryOperatorPlacement::Leading)
        {
            applyLayoutPass([&] (const auto& parsed) {
                return AlignLeadingTernaryOperators(parsed, style);
            });
        }
    }
    if (trackNewCallBreaks && capturedHeaderSource &&
        beforeHeaderReflow != formatted)
    {
        bool repositioned = false;
        const parser::SyntaxTree* before = &tree;
        const auto& parsed = currentTree();
        if (beforeHeaderReflow != tree.Source()) {
            ASSERT(beforeHeaderTree);
            before = &*beforeHeaderTree;
        }
        if (before->TokenCount() == parsed.TokenCount()) {
            std::vector<bool> newBreaks(parsed.TokenCount(), false);
            bool sameTokens = true;
            for (TokenIndex i = 0; i < parsed.TokenCount(); ++i) {
                sameTokens &= before->Kind(i) == parsed.Kind(i) &&
                    before->TokenText(i) == parsed.TokenText(i);
                newBreaks[i] = before->WhitespaceBefore(i).find('\n') ==
                        std::string_view::npos &&
                    parsed.WhitespaceBefore(i).find('\n') !=
                        std::string_view::npos;
            }
            if (sameTokens && std::ranges::any_of(
                newBreaks,
                [] (bool split) { return split; }))
            {
                std::byte frontierBuffer[2048];
                std::pmr::monotonic_buffer_resource frontierScratch(
                    frontierBuffer, sizeof(frontierBuffer));
                CallFrontier frontier{&frontierScratch};
                CallFrontier nextFrontier{&frontierScratch};
                bool useFrontier = false;
                for (size_t depth = 0; depth < MaxCallReflowDepth; ++depth) {
                    bool deeperCalls = false;
                    nextFrontier.clear();
                    const bool changedCall = applyLayoutPass(
                        [&] (const auto& calls) {
                            return FormatCalls(
                                calls,
                                style,
                                depth,
                                deeperCalls,
                                &newBreaks,
                                useFrontier ? &frontier : nullptr,
                                &nextFrontier);
                        });
                    repositioned |= changedCall;
                    headerReflowed |= changedCall;
                    if (!deeperCalls) {
                        break;
                    }
                    useFrontier = !changedCall;
                    if (useFrontier) {
                        frontier.swap(nextFrontier);
                    }
                }
            }
        }
        if (repositioned && hasConditional &&
            style.TernarySplit == TernarySplit::ThreeLines &&
            style.TernaryOperatorPlacement == TernaryOperatorPlacement::Leading)
        {
            headerReflowed |= applyLayoutPass([&] (const auto& alignedTree) {
                return AlignLeadingTernaryOperators(alignedTree, style);
            });
        }
    }
    if (style.SingleStatementLambdaBodyLayout ==
            SingleStatementLambdaBodyLayout::OneLineIfFits)
    {
        headerReflowed |= applyLayoutPass([&] (const auto& parsed) {
            return FormatSingleStatementLambdas(parsed, style);
        });
    }
    captureHeaderSource = false;
    // Revisit control braces after a header was reflowed. Without
    // enforcement, limit this pass to headers whose line breaks changed;
    // other braces remain source-owned.
    if (headerReflowed) {
        applyLayoutPass([&] (const auto& parsed) {
            std::vector<bool> changed;
            if (!style.EnforceControlBlockBracePlacement) {
                const parser::SyntaxTree* before = &tree;
                if (beforeHeaderReflow != tree.Source()) {
                    before = beforeHeaderTree
                        ? &*beforeHeaderTree
                        : nullptr;
                }
                changed.resize(parsed.TokenCount(), false);
                if (before && before->TokenCount() == parsed.TokenCount()) {
                    bool sameTokens = true;
                    for (TokenIndex i = 0; i < parsed.TokenCount(); ++i) {
                        sameTokens &= before->Kind(i) == parsed.Kind(i) &&
                            before->TokenText(i) == parsed.TokenText(i);
                    }
                    if (sameTokens) {
                        std::vector<const parser::Node*> nodes{&parsed.Root()};
                        while (!nodes.empty()) {
                            auto* node = nodes.back();
                            nodes.pop_back();
                            for (auto* child = node->Child();
                                child;
                                child = child->NextSibling)
                            {
                                nodes.push_back(child);
                                if (child->Kind != NK::CompoundStatement ||
                                    child->Tokens.Empty())
                                {
                                    continue;
                                }
                                const auto open = child->Tokens.Begin;
                                if (open >= changed.size() ||
                                    node->Tokens.Begin >= open)
                                {
                                    continue;
                                }
                                auto first = node->Tokens.Begin;
                                for (auto i = first + 1; i <= open; ++i) {
                                    if (before->WhitespaceBefore(i) !=
                                            parsed.WhitespaceBefore(i) &&
                                        (before->WhitespaceBefore(i).find(
                                            '\n') !=
                                                std::string_view::npos ||
                                            parsed.WhitespaceBefore(i).find(
                                                '\n') !=
                                                std::string_view::npos))
                                    {
                                        changed[open] = true;
                                    }
                                }
                            }
                        }
                    }
                }
            }
            Style controlStyle;
            controlStyle.ClassBracePlacement = BracePlacement::Preserve;
            controlStyle.StructBracePlacement = BracePlacement::Preserve;
            controlStyle.UnionBracePlacement = BracePlacement::Preserve;
            controlStyle.FunctionBracePlacement = BracePlacement::Preserve;
            controlStyle.LambdaBracePlacement = BracePlacement::Preserve;
            controlStyle.NamespaceBracePlacement = BracePlacement::Preserve;
            controlStyle.ClassBraceSpacing = SameLineBraceSpacing::Preserve;
            controlStyle.StructBraceSpacing = SameLineBraceSpacing::Preserve;
            controlStyle.UnionBraceSpacing = SameLineBraceSpacing::Preserve;
            controlStyle.FunctionBraceSpacing = SameLineBraceSpacing::Preserve;
            controlStyle.LambdaBraceSpacing = SameLineBraceSpacing::Preserve;
            controlStyle.NamespaceBraceSpacing = SameLineBraceSpacing::Preserve;
            controlStyle.SpaceInsideCallArguments =
                SpaceInsideCallArguments::Preserve;
            controlStyle.SpaceInsideFunctionParameters =
                SpaceInsideFunctionParameters::Preserve;
            controlStyle.IndentWidth = style.IndentWidth;
            controlStyle.IndentationKind = style.IndentationKind;
            controlStyle.ControlBlockBracePlacement =
                style.ControlBlockBracePlacement;
            controlStyle.EnforceControlBlockBracePlacement = true;
            return RenderFormatter(parsed, controlStyle,
                style.EnforceControlBlockBracePlacement
                    ? nullptr
                    : &changed);
        });
    }
    // Apply brace preferences only where a preceding pass has reflowed the
    // declaration header. Unchanged braces need explicit enforcement.
    if (formatted != tree.Source() &&
        (!requested.EnforceClassBracePlacement ||
            !requested.EnforceStructBracePlacement ||
            !requested.EnforceUnionBracePlacement ||
            !requested.EnforceFunctionBracePlacement ||
            !requested.EnforceLambdaBracePlacement ||
            !requested.EnforceNamespaceBracePlacement))
    {
        const auto& parsed = currentTree();
        if (tree.TokenCount() == parsed.TokenCount()) {
            bool sameTokens = true;
            for (TokenIndex i = 0; i < parsed.TokenCount(); ++i) {
                sameTokens &= tree.Kind(i) == parsed.Kind(i) &&
                    tree.TokenText(i) == parsed.TokenText(i);
            }
            if (sameTokens) {
                std::vector<bool> changed(parsed.TokenCount(), false);
                std::vector<const parser::Node*> nodes{&parsed.Root()};
                while (!nodes.empty()) {
                    const auto* node = nodes.back();
                    nodes.pop_back();
                    for (auto* child = node->Child();
                        child;
                        child = child->NextSibling)
                    {
                        nodes.push_back(child);
                    }
                    if (node->Tokens.Empty() ||
                        (node->Kind != NK::RecordDeclaration &&
                            node->Kind != NK::NamespaceDeclaration &&
                            node->Kind != NK::FunctionDefinition &&
                            node->Kind != NK::LambdaExpression))
                    {
                        continue;
                    }
                    for (auto open = node->Tokens.Begin;
                        open < node->Tokens.End;
                        ++open)
                    {
                        if (parsed.Kind(open) != TK::Operator_OpenBrace ||
                            (node->Kind == NK::RecordDeclaration &&
                                parsed.Role(open) !=
                                    parser::PunctuationRole::RecordDelimiter))
                        {
                            continue;
                        }
                        for (auto i = node->Tokens.Begin + 1;
                            i < open;
                            ++i)
                        {
                            if (tree.WhitespaceBefore(i) !=
                                parsed.WhitespaceBefore(i) &&
                                (tree.WhitespaceBefore(i).find('\n') !=
                                        std::string_view::npos ||
                                    parsed.WhitespaceBefore(i).find('\n') !=
                                        std::string_view::npos))
                            {
                                changed[open] = true;
                            }
                        }
                        break;
                    }
                }
                if (std::ranges::any_of(changed, [] (bool value) {
                    return value;
                }))
                {
                    Style braceStyle;
                    braceStyle.ClassBracePlacement =
                        requested.ClassBracePlacement;
                    braceStyle.StructBracePlacement =
                        requested.StructBracePlacement;
                    braceStyle.UnionBracePlacement =
                        requested.UnionBracePlacement;
                    braceStyle.FunctionBracePlacement =
                        requested.FunctionBracePlacement;
                    braceStyle.LambdaBracePlacement =
                        requested.LambdaBracePlacement;
                    braceStyle.NamespaceBracePlacement =
                        requested.NamespaceBracePlacement;
                    braceStyle.ClassBraceSpacing =
                        SameLineBraceSpacing::Preserve;
                    braceStyle.StructBraceSpacing =
                        SameLineBraceSpacing::Preserve;
                    braceStyle.UnionBraceSpacing =
                        SameLineBraceSpacing::Preserve;
                    braceStyle.FunctionBraceSpacing =
                        SameLineBraceSpacing::Preserve;
                    braceStyle.LambdaBraceSpacing =
                        SameLineBraceSpacing::Preserve;
                    braceStyle.NamespaceBraceSpacing =
                        SameLineBraceSpacing::Preserve;
                    braceStyle.EnumBraceSpacing =
                        SameLineBraceSpacing::Preserve;
                    braceStyle.SpaceInsideCallArguments =
                        SpaceInsideCallArguments::Preserve;
                    braceStyle.SpaceInsideFunctionParameters =
                        SpaceInsideFunctionParameters::Preserve;
                    braceStyle.IndentWidth = style.IndentWidth;
                    braceStyle.IndentationKind = style.IndentationKind;
                    applyLayoutPass([&] (const auto& braced) {
                        return RenderFormatter(braced, braceStyle, &changed);
                    });
                }
            }
        }
    }
    if (hasMemberAccess && !style.ChainedCallRootNames.empty() &&
        style.ChainedCallSplitCondition != ChainedCallSplitCondition::Preserve)
    {
        // The outer chain is formatted first; revisit nested arguments
        // after refreshing syntax, without overlapping edits in one pass.
        for (size_t pass = 0; pass < 4; ++pass) {
            if (!applyOptionalPass([&] (const auto& parsed) {
                return FormatChainedCalls(parsed, style);
            }))
            {
                break;
            }
        }
    }
    if (style.PointerReferenceDeclaratorLocation !=
            PointerReferenceDeclaratorLocation::Preserve ||
        style.BinaryOperatorSpacing != TokenSpacing::Preserve ||
        style.AssignmentOperatorSpacing != TokenSpacing::Preserve ||
        style.CastSpacing != TokenSpacing::Preserve ||
        style.EnforceCommaSpacing ||
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
        style.ScopeResolutionSpacing != TightOperatorSpacing::Preserve ||
        style.InitializerBracesSpacing != TokenSpacing::Preserve ||
        style.CastArgumentSpacing != TokenSpacing::Preserve ||
        style.GroupingInteriorSpacing != TokenSpacing::Preserve ||
        style.ControlBlockConditionSpacing != TokenSpacing::Preserve ||
        style.AccessModifierColonSpacing != TokenSpacing::Preserve ||
        style.RangeBasedForColonSpacing != TokenSpacing::Preserve ||
        style.BlockInteriorSpacing != TokenSpacing::Preserve ||
        style.EmptyBlockInteriorSpacing != TokenSpacing::Preserve ||
        style.EmptyLambdaBodySpacing != EmptyLambdaBodySpacing::Inherit ||
        style.ControlBlockSpaceBeforeBrace != TokenSpacing::Preserve ||
        style.DeclarationSemicolonSpacing != TokenSpacing::Preserve ||
        style.BitFieldColonSpacing != ColonSpacing::Preserve ||
        style.EnumUnderlyingTypeColonSpacing != ColonSpacing::Preserve ||
        style.LabelColonSpacing != ColonSpacing::Preserve)
    {
        applyLayoutPass([&] (const auto& parsed) {
            return FormatTokenSpacing(parsed, existing);
        });
    }
    if (style.AmbiguousTemplateDeclarationSpacing !=
            AmbiguousTemplateDeclarationSpacing::Preserve)
    {
        applyPass([&] (const auto& parsed) {
            return FormatAmbiguousTemplateDeclarationSpacing(parsed, style);
        });
    }
    if (style.IncludeOrder != IncludeOrder::Preserve) {
        applyLayoutPass([&] (const auto& parsed) {
            return FormatIncludes(parsed);
        });
    }
    if (style.TrailingCommentSpacing != TokenSpacing::Preserve ||
        style.PreprocessorIndentation != PreprocessorIndentation::Preserve)
    {
        applyPass([&] (const auto& parsed) {
            return FormatLineOwnedSpacing(parsed, style);
        });
    }
    if ((style.IndentBlockBodies ||
        style.IndentCaseLabels != BodyIndentation::Preserve ||
        style.StatementAttributeIndent != StatementAttributeIndent::Preserve) &&
        (!indentedBeforeReflow || indentedRevision != revision))
    {
        applyLayoutPass([&] (const auto& parsed) {
            return FormatBlockIndentation(parsed, style);
        });
    }
    if (style.EnforceFunctionParameterClosingParenthesis ||
        style.EnforceFunctionCallArgumentClosingParenthesis ||
        style.EnforceInitializerClosingBrace ||
        style.EnforceBlockClosingBrace)
    {
        applyLayoutPass([&] (const auto& parsed) {
            return FormatClosingDelimiters(parsed, style);
        });
    }
    if (style.TrailingWhitespace != TrailingWhitespace::Preserve) {
        applyLayoutPass([&] (const auto& parsed) {
            return TrimTrailingWhitespace(parsed);
        });
    }
    if ((style.SeparatorLength != 0 && hasComment) ||
        (hasNamespace && style.NamespaceComments !=
            NamespaceComments::Preserve))
    {
        applyOptionalPass([&] (const auto& parsed)
            -> std::optional<std::string> {
            std::vector<Replacement> edits;
            if (style.SeparatorLength != 0 &&
                (hasComment || hasNamespace))
            {
                edits = CollectSeparatorEdits(
                    parsed,
                    style.SeparatorLength);
            }
            if (hasNamespace && style.NamespaceComments !=
                    NamespaceComments::Preserve)
            {
                auto comments = CollectNamespaceCommentEdits(
                    parsed,
                    style.NamespaceComments,
                    style.ShortNamespaceLines,
                    style.NamespaceCommentSpaces,
                    style.TrailingCommentSpacing);
                edits.insert(
                    edits.end(),
                    std::make_move_iterator(comments.begin()),
                    std::make_move_iterator(comments.end()));
            }
            if (edits.empty()) {
                return std::nullopt;
            }
            std::ranges::sort(edits, {}, &Replacement::Begin);
            return RenderLayoutEdits(
                parsed.Source(),
                std::move(edits)).Output;
        });
    }
    if (unsafeStructuralEdit) {
        return std::string(tree.Source());
    }
    formatted = RestoreDisabledRegions(tree, std::move(formatted));
    return RestoreSensitiveInvocations(tree, style, std::move(formatted));
}

std::string Format(const parser::SyntaxTree& tree)
{
    return Format(tree, Style{});
}

std::string Format(util::PaddedSourceView source, const Style& style)
{
    auto tree = [&] {
        HIT_FMT_PROFILE_PHASE("parser.initial");
        return parser::Parse(source, [] (parser::ParseError) { });
    }();
    return Format(tree, style);
}

std::string Format(util::PaddedSourceView source)
{
    return Format(source, Style{});
}

} // namespace formatter
