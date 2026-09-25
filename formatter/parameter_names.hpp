#pragma once

#include "config.hpp"

#include <parser/syntax_tree.hpp>

#include <string>
#include <vector>

namespace formatter {

// Only checks parameters of parsed function declarations and definitions;
// abstract function types and ambiguous or recovered syntax are left alone.
struct ParameterNameDiagnostic
{
    util::Position Position;
    std::string Message;
    bool AllowCommentedName = false;
};

[[nodiscard]] std::vector<ParameterNameDiagnostic>
CheckParameterNames(const parser::SyntaxTree& tree,
    const Configuration& config);

} // namespace formatter
