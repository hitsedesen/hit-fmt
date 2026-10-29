#include "diagnostic_locations.hpp"

#include <unicode/utf8.hpp>

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace formatter::detail {

namespace {

class OriginalCursor
{
public:
    explicit OriginalCursor(const parser::SyntaxTree& tree)
        : Tree_(tree)
    { }

    util::Position Position(TokenAnchor anchor)
    {
        const auto range = Tree_.Token(anchor.Token).Text;
        if (anchor.Offset < range.Begin || anchor.Offset > range.End) {
            throw std::runtime_error("diagnostic anchor is outside its token");
        }
        if (anchor.Token != Token_ || anchor.Offset < Offset_) {
            Token_ = anchor.Token;
            Offset_ = range.Begin;
            Position_ = Tree_.TokenPosition(Token_);
        }
        const auto text = Tree_.Source();
        while (Offset_ < anchor.Offset) {
            const auto c = static_cast<unsigned char>(text[Offset_]);
            const auto size = c < 0x80 ? 1 : unicode::utf8::GetFirstCodePointSize(text.substr(Offset_)).value_or(1);
            if (Offset_ + size > anchor.Offset) {
                break;
            }
            Offset_ += size;
            if (c == '\n') {
                ++Position_.Line;
                Position_.Column = 1;
            } else {
                ++Position_.Column;
            }
        }
        return Position_;
    }

private:
    const parser::SyntaxTree& Tree_;
    parser::TokenIndex Token_ = std::numeric_limits<parser::TokenIndex>::max();
    parser::SourceOffset Offset_ = 0;
    util::Position Position_{};
};

} // namespace

util::Position OriginalPosition(const parser::SyntaxTree& tree, TokenAnchor anchor)
{
    return OriginalCursor(tree).Position(anchor);
}

void MapWidthLocations(const parser::SyntaxTree& tree, FormatResult& result, std::pmr::memory_resource& scratch)
{
    if (result.Diagnostics.empty()) {
        return;
    }
    std::pmr::vector<parser::TokenIndex> tokens(&scratch);
    tokens.reserve(tree.TokenCount());
    const auto& mapped = result.OriginalTokens;
    for (parser::TokenIndex token = 0; token < tree.TokenCount(); ++token) {
        if (mapped[token].Present) {
            tokens.push_back(token);
        }
    }
    std::sort(
        tokens.begin(),
        tokens.end(),
        [&] (auto a, auto b) {
            return mapped[a].Output.Begin < mapped[b].Output.Begin;
        });
    struct Point
    {
        TokenAnchor Anchor;
        size_t Diagnostic;
    };
    std::pmr::vector<Point> points(&scratch);
    points.reserve(result.Diagnostics.size());
    for (size_t i = 0; i < result.Diagnostics.size(); ++i) {
        const auto offset = result.Diagnostics[i].Offset;
        auto token = std::upper_bound(
            tokens.begin(),
            tokens.end(),
            offset,
            [&] (auto point, auto id) { return point < mapped[id].Output.Begin; });
        if (token != tokens.begin()) {
            --token;
        }
        if (token == tokens.end()) {
            result.Diagnostics[i].Original = util::Position{1, 1};
            continue;
        }
        const auto source = tree.Token(*token).Text;
        const auto output = mapped[*token].Output;
        // Generated or rewritten bytes use the original token's start.
        auto original = source.Begin;
        if (output.Begin <= offset && offset < output.End && output.End - output.Begin == source.End - source.Begin) {
            original += offset - output.Begin;
        }
        points.push_back({{*token, original}, i});
    }
    std::sort(
        points.begin(),
        points.end(),
        [] (const auto& a, const auto& b) {
            return a.Anchor.Offset < b.Anchor.Offset;
        });
    OriginalCursor cursor(tree);
    for (const auto& point : points) {
        result.Diagnostics[point.Diagnostic].Original = cursor.Position(point.Anchor);
    }
}

} // namespace formatter::detail
