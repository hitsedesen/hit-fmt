#pragma once

#include "config.hpp"

#include <parser/syntax_tree.hpp>

#include <string>
#include <vector>

namespace formatter {

struct NameDiagnostic
{
    util::Position Position;
    std::string Message;
};

// Reports only names whose declaration kind is established by the syntax tree.
// Never rewrites an identifier: references may be outside the current file.
[[nodiscard]] std::vector<NameDiagnostic> CheckNames(
    const parser::SyntaxTree& tree, const NamingStyle& style);

} // namespace formatter
