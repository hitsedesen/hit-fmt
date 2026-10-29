#pragma once

#include <parser/syntax_tree.hpp>

namespace formatter {

struct TokenAnchor
{
    parser::TokenIndex Token;
    // Absolute byte offset, including a token's exclusive end.
    parser::SourceOffset Offset;
};

} // namespace formatter
