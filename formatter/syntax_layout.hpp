#pragma once

#include <formatter/config_annotations.hpp>
#include <formatter/layout.hpp>
#include <formatter/policy.hpp>
#include <formatter/source_analysis.hpp>
#include <formatter/syntax_spacing.hpp>

#include <memory_resource>
#include <span>

namespace formatter {

// Structural normalization precedes lowering. These are gap IDs in the supplied
// tree, not offsets in an earlier source: inserted adjacencies always use
// Rebuilt spacing, independently of the containing property's layout mode.
struct SyntaxLayoutOptions
{
    std::span<const GapId> GeneratedAdjacencies = {};
    layout::Newline LineEnding = layout::Newline::LF;
    uint8_t TabWidth = 8;
};

enum class SyntaxLayoutError : uint8_t
{
    None,
    MismatchedInputs,
    InvalidGeneratedAdjacency,
    CrossingGroups,
    ConflictingGapOwners,
    IndentationOverflow,
    CoreFailure,
};

struct SyntaxLayoutStatus
{
    SyntaxLayoutError Code = SyntaxLayoutError::None;
    parser::TokenIndex Token = parser::TokenIndex(-1);
    layout::Status Core = {};
    explicit operator bool() const
    {
        return Code == SyntaxLayoutError::None;
    }
};

// Requires a validated policy snapshot and safety/spacing for this exact tree.
// Build appends to an empty, unsealed document borrowing tree.Source(), and
// seals exactly once. Render once with SyntaxRenderPolicy. The tree/source,
// document arena and any literal views must survive rendering. Policies,
// annotations, safety, spacing and scratch may be released after Build returns.
// Tokens retain their tree indices as Mapping.Source.Identity; gaps have no
// token identity. Generated punctuation has NoIdentity and a zero-width
// current-source insertion anchor at its syntax item's end (never Exact).
// Protected/recovery bytes always win. OutsideEditScope-only bytes retain
// syntax context, not edit authority. Sparse partial-gap permissions permit
// only policy-requested horizontal trim/indentation, never newline rights.
// Fully byte-covered but insertion-fenced gaps may spell existing atoms via
// CanSpellGapNewlines; neither fixed nor respelled atoms become owned breaks.
// Groups form a laminar token/gap event
// forest; crossing intervals and competing explicit gap owners are errors,
// never a request to invoke a legacy formatter or retry a reparse.
// Prefixed delimiter anchors are rebased inside their continuation group;
// IndentationOverflow rejects saturation in that group or any ancestor, since
// a later negative offset cannot recover a saturated block basis. On failure,
// discard the incomplete document; it is not a sealed preservation fallback.
[[nodiscard]] SyntaxLayoutStatus BuildSyntaxDocument(
    const parser::SyntaxTree& tree,
    const CompiledPolicies& policies,
    const LibraryAnnotations& annotations,
    const SourceAnalysis& safety,
    const SyntaxSpacing& spacing,
    layout::Document& document,
    std::pmr::memory_resource& scratch,
    const SyntaxLayoutOptions& options = {});

[[nodiscard]] layout::Policy
SyntaxRenderPolicy(
    const CompiledPolicies& policies,
    const SyntaxLayoutOptions& options = {});

} // namespace formatter
