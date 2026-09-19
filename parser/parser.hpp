#pragma once

#include "ast.hpp"

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

using ErrorHandler = std::move_only_function<void(ParseError)>;

// Build a recovery-oriented, lossless C++ syntax tree. The parser recognizes
// the structure needed by a formatter and does not do type checking, macro
// expansion, or conditional preprocessing.
[[nodiscard]] Ast Parse(std::string_view input, ErrorHandler errorHandler = {});

} // namespace parser
