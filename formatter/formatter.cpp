#include "formatter.hpp"

#include <parser/parser.hpp>

#include <string>
#include <string_view>
#include <utility>

namespace formatter {
namespace {

// The same parsed token/trivia emission path handles every style. With no
// enabled formatting rule, each source-owned span is emitted unchanged.
class Formatter
{
public:
    explicit Formatter(const parser::SyntaxTree& tree)
        : Tree_(tree)
    {
        Output_.reserve(tree.Source().size());
    }

    std::string Run()
    {
        const auto count = Tree_.TokenCount();
        if (count == 0) {
            EmitTrivia(Tree_.Source());
        } else {
            for (parser::TokenIndex index = 0; index < count; ++index) {
                EmitTrivia(Tree_.WhitespaceBefore(index));
                EmitToken(Tree_.TokenText(index));
            }
            EmitTrivia(Tree_.WhitespaceAfter(count - 1));
        }
        return std::move(Output_);
    }

private:
    void EmitTrivia(std::string_view trivia)
    {
        Output_.append(trivia);
    }

    void EmitToken(std::string_view token)
    {
        Output_.append(token);
    }

    const parser::SyntaxTree& Tree_;
    std::string Output_;
};

} // namespace

std::string Format(const parser::SyntaxTree& tree, const Style&)
{
    return Formatter(tree).Run();
}

std::string Format(const parser::SyntaxTree& tree)
{
    return Formatter(tree).Run();
}

std::string Format(util::PaddedSourceView source, const Style& style)
{
    auto tree = parser::Parse(source, [] (parser::ParseError) { });
    return Format(tree, style);
}

std::string Format(util::PaddedSourceView source)
{
    auto tree = parser::Parse(source, [] (parser::ParseError) { });
    return Format(tree);
}

} // namespace formatter
