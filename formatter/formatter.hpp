#pragma once

#include <parser/parser.hpp>
#include <util/input.hpp>

#include <cstddef>
#include <string>

namespace formatter {

// Retained configuration dimensions. Neither one changes source text.
struct Style
{
    size_t IndentWidth = 4;
    size_t ColumnLimit = 120;
};

// Parse and emit every token and trivia span without changing their bytes,
// including comments, line endings, and invalid syntax.
[[nodiscard]] std::string Format(const parser::SyntaxTree& tree, const Style& style);
[[nodiscard]] std::string Format(const parser::SyntaxTree& tree);
[[nodiscard]] std::string Format(util::PaddedSourceView source, const Style& style);
[[nodiscard]] std::string Format(util::PaddedSourceView source);

} // namespace formatter
