#pragma once

#include "config.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <parser/syntax_tree.hpp>

namespace formatter {

// Only checks parameters of parsed function declarations and definitions;
// abstract function types and ambiguous or recovered syntax are left alone.
struct ParameterNameDiagnostic
{
    util::Position Position;
    parser::SourceRange Signature;
    uint32_t SignatureBeginLine;
    uint32_t SignatureEndLine;
    std::string Function;
    size_t ParameterNumber;
    bool AllowCommentedName = false;
};

[[nodiscard]] std::vector<ParameterNameDiagnostic> CheckParameterNames(
    const parser::SyntaxTree& tree,
    const Configuration& config);

} // namespace formatter
