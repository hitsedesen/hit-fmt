#pragma once

#include "syntax_tree.hpp"

#include <optional>
#include <span>
#include <vector>

namespace parser::detail {

// Exact copied source slices and complete generated include/ruler atoms.
// The formatter's structural materializer has already validated the slices.
struct NormalizedPiece
{
    SourceRange Output;
    SourceRange Original;
    bool Generated;
};

// Borrowed topology from the exact original SourceAnalysis preorder. Sparse
// admission follows only existing namespace/root child lists, never all rows.
struct NormalizedRegionTopology
{
    const void* Records;
    uint32_t Count;
    uint32_t (*FirstChild)(const void*, uint32_t);
    uint32_t (*NextSibling)(const void*, uint32_t);
};

struct NormalizedCommentInsertion
{
    TokenIndex Token;
    uint32_t ParentRegion;
    // Original preorder position, including Count for an append at the end.
    uint32_t BeforeRegion;
};

// Private token-only result: Root() and AssociatedTrivia() are unavailable.
// Original syntax is unchanged. Success certifies the existing ruler guards,
// complete native-token survival and unchanged native comment attachment.
[[nodiscard]] std::optional<SyntaxTree> TryBuildNormalizedTokenView(
    const SyntaxTree& original,
    util::PaddedSourceView input,
    std::span<const NormalizedPiece> pieces,
    NormalizedRegionTopology topology,
    std::pmr::vector<TokenIndex>& originalToNormalized,
    std::pmr::vector<NormalizedCommentInsertion>& insertions);

// Internal consuming path: on success, result owns original's node arena and
// original retains only its source/token view, valid until result is destroyed.
// On refusal original is unchanged. The caller must keep both source buffers
// alive. originalToNormalized follows grammar order, not include identity.
[[nodiscard]] std::optional<SyntaxTree> TryConsumeNormalizedSyntax(
    SyntaxTree& original,
    util::PaddedSourceView input,
    std::span<const NormalizedPiece> pieces,
    std::pmr::vector<TokenIndex>& originalToNormalized);

} // namespace parser::detail
