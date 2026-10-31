#pragma once

#include "structural_edits.hpp"

namespace formatter {

// A ruler is a standalone line comment containing only three or more slashes.
[[nodiscard]] bool IsRuler(std::string_view text);

// Explicit directive policies may rewrite directive-owned text. Every other
// protection reason, including disabled formatting and edit scope, still wins.
[[nodiscard]] bool CanRewriteTextAtom(const SourceAnalysis& safety, parser::SourceRange source, bool directive);

void PlanTextPolicyEdits(
    const parser::SyntaxTree& tree,
    const SourceAnalysis& safety,
    StructuralOptions options,
    StructuralPlan& plan,
    std::pmr::memory_resource& fileMemory,
    std::pmr::memory_resource& scratchMemory,
    std::string_view inputFilePath = {});

} // namespace formatter
