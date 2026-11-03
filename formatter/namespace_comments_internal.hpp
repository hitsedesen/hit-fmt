#pragma once

#include "structural_edits.hpp"

namespace formatter::detail {

struct NamespaceTokenPosition
{
    parser::TokenIndex Original;
    TokenOutputOffset Position;
};

// Internal rendering has proved that every surviving original token is emitted
// verbatim or differs only in block-comment indentation after newline atoms.
// Normalization also proved that namespace interiors remain inside their exact
// original braces. Positions contains the required original header, brace,
// existing-comment, continuation and nearest surviving neighbor identities,
// sorted by ID.
// This contract grants no authority to edit protected bytes or namespace ends;
// the ordinary resolver's remaining checks and diagnostics still apply.
[[nodiscard]] StructuralPlan ResolveNamespaceCommentsCertified(
    const parser::SyntaxTree& original,
    const NamespaceCommentPlan& plan,
    std::string_view output,
    std::span<const NamespaceTokenPosition> positions,
    std::span<const parser::SourceRange> outputProtection,
    std::pmr::memory_resource& fileMemory,
    std::pmr::memory_resource& scratchMemory);

} // namespace formatter::detail
