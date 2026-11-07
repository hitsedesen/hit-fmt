#pragma once

#include "config.hpp"
#include "diagnostics.hpp"

#include <util/types.hpp>

#include <parser/syntax_tree.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace formatter {

// Only checks parameters of parsed function declarations and definitions;
// abstract function types and ambiguous or recovered syntax are left alone.
struct ParameterNameDiagnostic
{
    util::Position Position;
    parser::SourceRange Signature;
    u32 SignatureBeginLine;
    u32 SignatureEndLine;
    std::string Function;
    us ParameterNumber;
    bool AllowCommentedName = false;
    TokenAnchor Anchor;
};

[[nodiscard]] std::vector<ParameterNameDiagnostic> CheckParameterNames(
    const parser::SyntaxTree& tree,
    const Configuration& config);

} // namespace formatter
