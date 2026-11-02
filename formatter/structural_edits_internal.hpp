#pragma once

#include "structural_edits.hpp"

#include <algorithm>

namespace formatter::detail {

// The pipeline built safety from this exact tree. Public planners retain the
// independent tree/analysis validation required for arbitrary callers.
[[nodiscard]] StructuralPlan PlanStructuralEditsCertified(
    const parser::SyntaxTree& tree,
    const SourceAnalysis& safety,
    StructuralOptions options,
    std::pmr::memory_resource& fileMemory,
    std::pmr::memory_resource& scratchMemory,
    IncludeSortContext includes = {});

[[nodiscard]] NamespaceCommentPlan PlanNamespaceCommentsCertified(
    const parser::SyntaxTree& tree,
    const SourceAnalysis& safety,
    NamespaceCommentOptions options,
    std::pmr::memory_resource& fileMemory,
    std::pmr::memory_resource& scratchMemory);

// Each interval is copied exactly once; intervals are sorted and disjoint in
// original token order. Byte positions translate within the borrowed piece.
// NormalizedBegin is set only after consuming syntax reuse proves that every
// token in the interval has retained its exact spelling and boundaries.
struct CopiedTokenRun
{
    parser::TokenRange Original;
    parser::SourceOffset OriginalBegin;
    parser::SourceOffset OutputBegin;
    parser::TokenIndex NormalizedBegin = UINT32_MAX;
};

[[nodiscard]] inline const CopiedTokenRun* FindCopiedTokenRun(
    std::span<const CopiedTokenRun> copied,
    parser::TokenIndex token)
{
    const auto found = std::lower_bound(
        copied.begin(),
        copied.end(),
        token,
        [] (const auto& run, auto id) {
            return run.Original.End <= id;
        });
    return found != copied.end() && found->Original.Begin <= token ? &*found : nullptr;
}

[[nodiscard]] inline TokenOutputOffset CopiedTokenPosition(
    const parser::SyntaxTree& original,
    const CopiedTokenRun& run,
    parser::TokenIndex token)
{
    const auto range = original.Token(token).Text;
    return {{run.OutputBegin + range.Begin - run.OriginalBegin, run.OutputBegin + range.End - run.OriginalBegin}, true};
}

// Same validation, text, provenance and generated adjacencies as the public
// materializer; dense OriginalTokens is deferred for private text-only use.
[[nodiscard]] StructuralMaterialization MaterializeStructuralEditsCompact(
    const parser::SyntaxTree& tree,
    const StructuralPlan& plan,
    std::pmr::memory_resource& fileMemory,
    std::pmr::memory_resource& scratchMemory,
    std::pmr::vector<CopiedTokenRun>& copied);

void MaterializeOriginalTokenOffsets(
    const parser::SyntaxTree& original,
    StructuralMaterialization& materialized,
    std::span<const CopiedTokenRun> copied);

} // namespace formatter::detail
