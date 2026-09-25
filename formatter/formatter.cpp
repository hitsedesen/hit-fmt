#include "formatter.hpp"

#include <parser/parser.hpp>
#include <tokenizer/token.hpp>

#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace formatter {
namespace {

using NK = parser::NodeKind;
using TK = tokenizer::TokenKind;
using TokenIndex = parser::TokenIndex;
constexpr TokenIndex NoToken = std::numeric_limits<TokenIndex>::max();

// Token spelling and all trivia other than the selected opening-brace gap
// remain source-owned. Classification uses syntax nodes, not neighboring
// keywords (which also occur in expressions and template arguments).
class Formatter
{
public:
    Formatter(const parser::SyntaxTree& tree, const Style& style)
        : Tree_(tree)
        , Style_(style)
        , Placements_(tree.TokenCount(), BracePlacement::Preserve)
    {
        Output_.reserve(tree.Source().size());
        if (style.ClassBracePlacement != BracePlacement::Preserve ||
            style.StructBracePlacement != BracePlacement::Preserve ||
            style.UnionBracePlacement != BracePlacement::Preserve ||
            style.FunctionBracePlacement != BracePlacement::Preserve ||
            style.LambdaBracePlacement != BracePlacement::Preserve ||
            style.NamespaceBracePlacement != BracePlacement::Preserve)
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
                    Output_.append(Indentation(
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
        TokenIndex header = NoToken)
    {
        if (placement == BracePlacement::Preserve) {
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
            // The declaration keyword, not a keyword in a base clause or
            // attribute, selects the record's independent setting.
            if (node.Tokens.Begin < Tree_.TokenCount()) {
                switch (Tree_.Kind(node.Tokens.Begin)) {
                case TK::Keyword_class:
                    MarkOpening(node, Style_.ClassBracePlacement);
                    break;
                case TK::Keyword_struct:
                    MarkOpening(node, Style_.StructBracePlacement);
                    break;
                case TK::Keyword_union:
                    MarkOpening(node, Style_.UnionBracePlacement);
                    break;
                default:
                    break;
                }
            }
            break;
        case NK::NamespaceDeclaration:
            MarkOpening(node, Style_.NamespaceBracePlacement);
            break;
        case NK::FunctionDefinition:
            for (auto* child = node.FirstChild; child;
                child = child->NextSibling)
            {
                if (child->Kind == NK::CompoundStatement) {
                    MarkOpening(*child, Style_.FunctionBracePlacement,
                        node.Tokens.Begin);
                } else if (child->Kind == NK::FunctionTryBlock) {
                    for (auto* body = child->FirstChild; body;
                        body = body->NextSibling)
                    {
                        if (body->Kind == NK::CompoundStatement) {
                            MarkOpening(*body, Style_.FunctionBracePlacement,
                                node.Tokens.Begin);
                            break;
                        }
                    }
                }
            }
            break;
        case NK::LambdaBody:
            MarkOpening(node, Style_.LambdaBracePlacement);
            // The capture can start before qualifiers and return types.
            break;
        default:
            break;
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
        auto placement = Placements_[index];
        if (placement == BracePlacement::Preserve || index == 0 ||
            // Only a trailing header comment is safe to cross; leave other
            // comments and preprocessing directives where they are.
            (Tree_.Kind(index - 1) == TK::Comment &&
                (placement == BracePlacement::Attach ||
                    Tree_.TokenText(index - 1).starts_with("//"))) ||
            Tree_.Kind(index - 1) == TK::PreprocessingDirective ||
            gap.find_first_not_of(" \t\r\n") != std::string_view::npos)
        {
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
            Output_.append(Indentation(header == NoToken ? index : header));
        }
    }

    const parser::SyntaxTree& Tree_;
    const Style& Style_;
    std::vector<BracePlacement> Placements_;
    // Only initialized for openings with an active placement.
    std::vector<TokenIndex> HeaderStarts_ =
        std::vector<TokenIndex>(Tree_.TokenCount(), NoToken);
    std::string Output_;
};

} // namespace

std::string Format(const parser::SyntaxTree& tree, const Style& style)
{
    return Formatter(tree, style).Run();
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
