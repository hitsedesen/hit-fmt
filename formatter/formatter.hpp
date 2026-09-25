#pragma once

#include <parser/parser.hpp>
#include <util/input.hpp>

#include <cstddef>
#include <string>

namespace formatter {

enum class BracePlacement
{
    Preserve,
    Attach,
    NewLine,
};

struct Style
{
    size_t IndentWidth = 4;
    size_t ColumnLimit = 120;
    BracePlacement ClassBracePlacement = BracePlacement::Preserve;
    BracePlacement StructBracePlacement = BracePlacement::Preserve;
    BracePlacement UnionBracePlacement = BracePlacement::Preserve;
    BracePlacement FunctionBracePlacement = BracePlacement::Preserve;
    BracePlacement LambdaBracePlacement = BracePlacement::Preserve;
    BracePlacement NamespaceBracePlacement = BracePlacement::Preserve;
};

// Preserve source-owned spans except for configured declaration brace trivia.
[[nodiscard]] std::string Format(const parser::SyntaxTree& tree, const Style& style);
[[nodiscard]] std::string Format(const parser::SyntaxTree& tree);
[[nodiscard]] std::string Format(util::PaddedSourceView source, const Style& style);
[[nodiscard]] std::string Format(util::PaddedSourceView source);

} // namespace formatter
