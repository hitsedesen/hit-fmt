#pragma once

#include "syntax_tree.hpp"

#include <util/input.hpp>

#include <functional>
#include <string>
#include <string_view>

namespace parser {

struct ParseError
{
    std::string Message;
    util::Position Position;
};

using ErrorHandler = std::function<void(ParseError)>;

// Build a recovery-oriented, lossless C++ syntax tree. The parser recognizes
// the structure needed by a formatter and does not do type checking, macro
// expansion, or conditional preprocessing.
[[nodiscard]] SyntaxTree Parse(
    util::PaddedSourceView input,
    ErrorHandler errorHandler = {});

} // namespace parser
