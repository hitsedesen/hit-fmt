#pragma once

#include "source_analysis.hpp"

namespace formatter::detail {

// Owned source+Style pipeline only, after consuming parser reuse and after
// original safety's protection/scope projections. Identity indices preserve
// every AST production and all region topology. Refusal may consume safety;
// the caller then rebuilds fresh analysis from tree and inherited protection.
// The original analysis arena remains alive through the returned analysis.
[[nodiscard]] std::optional<SourceAnalysis> ConsumeIdentityNormalizedSourceAnalysis(
    const parser::SyntaxTree& original,
    const parser::SyntaxTree& tree,
    SourceAnalysis&& safety,
    std::span<const parser::TokenIndex> mapping,
    std::span<const parser::detail::NormalizedPiece> pieces,
    std::span<const ProtectedSourceRange> inherited,
    std::pmr::memory_resource& file,
    std::pmr::memory_resource& scratch);

// Requires the parser's native-attachment and sparse-insertion certificate.
// Refusal may consume safety; rebuild a real normalized syntax tree before
// falling back to fresh analysis. The original tree remains unchanged.
[[nodiscard]] std::optional<SourceAnalysis> ConsumeNormalizedTokenViewAnalysis(
    const parser::SyntaxTree& original,
    const parser::SyntaxTree& tree,
    SourceAnalysis&& safety,
    std::span<const parser::TokenIndex> mapping,
    std::span<const parser::detail::NormalizedPiece> pieces,
    std::span<const parser::detail::NormalizedCommentInsertion> insertions,
    std::span<const ProtectedSourceRange> inherited,
    std::pmr::memory_resource& file,
    std::pmr::memory_resource& scratch);

} // namespace formatter::detail
