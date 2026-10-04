#pragma once

#include <formatter/policy.hpp>
#include <formatter/source_analysis.hpp>

#include <memory_resource>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace formatter {

// An option identity, not a second configuration schema. PointerBinding is the
// separately compiled scalar. None has no configurable preference; it may
// still identify a syntax-owned Preserve boundary, including direct sequence
// edges with a space fallback and the gap before a header semicolon.
enum class SpacingField : uint8_t
{
    None,
#define SYNTAX_SPACING_FIELD(type, name, initial) name,
    FORMATTER_GAP_OPTIONS(SYNTAX_SPACING_FIELD)
#undef SYNTAX_SPACING_FIELD
    PointerBinding,
    Count,
};

[[nodiscard]] std::string_view SpacingFieldName(SpacingField field);

// AmbiguousTemplateDeclarationSpacing is deliberately unsupported: parser
// ambiguity never authorizes whitespace normalization. All other fields have
// syntax sites. BuildSyntaxSpacing rejects a non-Preserve unsupported setting.
[[nodiscard]] bool SupportsSpacingField(SpacingField field);

enum class SpacingOwner : uint8_t
{
    None,
    Calls,
    Parameters,
    Initializers,
    Captures,
    ControlHeaders,
    BaseLists,
    ConstructorLists,
    Expressions,
    Chains,
    DeclarationHeaders,
    Bodies,
    LambdaBodies,
};

enum class SpacingConstraint : uint8_t
{
    None = 0,
    UncertainSyntax = 1,
    SourceSafety = 2,
};

// Owner is the combinator whose own edge/separator/operator is being rebuilt,
// not necessarily the deepest node at this offset. The tree must outlive it.
// Priority makes precedence inspectable: generic punctuation/scalar binding 1,
// edges 2, specific edges 3, overriding syntax 4. Empty interiors use their
// base rule's priority + 1. At equal priority the later/more local rule wins.
struct SyntaxGapPreference
{
    const parser::Node* Owner = nullptr;
    SpacingField Field = SpacingField::None;
    SpacingOwner Category = SpacingOwner::None;
    GapPolicy Policy = GapPolicy::Preserve;
    bool FallbackSpace = false;
    uint8_t Priority = 0;
    // Bit flags. Uncertain syntax has no generated flat fallback; SourceSafety
    // requires retaining the separate SourceAnalysis constraints. Priority 255
    // is reserved for an uncertain-role Preserve boundary.
    SpacingConstraint Constraint = SpacingConstraint::None;
    // Actual namespace matching closer followed by a same-line // token.
    bool NamespaceClosingComment = false;
};

static_assert(sizeof(SyntaxGapPreference) <= 16);

[[nodiscard]] GapContext SpacingOwnerContext(
    SpacingOwner owner,
    const CompiledPolicies& policies);

// File-arena storage; gap i precedes token i, including the tail. No source,
// policy or scratch references are retained; Owner borrows the parsed tree.
class SyntaxSpacing
{
public:
    explicit SyntaxSpacing(std::pmr::memory_resource& fileMemory);
    SyntaxSpacing(SyntaxSpacing&&) = default;
    SyntaxSpacing(const SyntaxSpacing&) = delete;

    [[nodiscard]] std::span<const SyntaxGapPreference> Gaps() const
    {
        return Gaps_;
    }

    // O(1). nullopt means no explicit preference, not permission to borrow
    // source bytes. Lowering's selected combinator owns topology and context:
    // canonical topology-changing boundaries use syntax-safe flat fallback
    // before fitting, even with source horizontal bytes or a flat result.
    // Preserved topology and genuinely flat-only, non-rebuilt adjacencies
    // retain source horizontal bytes. Uncertainty/protection prohibit
    // unauthorized fallback; immutable, context-sensitive and hard-line
    // constraints override every policy. Required lexical separation overrides
    // removal, including word operators. Safety accepts unchanged empty gaps
    // even when its conservative punctuation predicate requires changed
    // spelling to separate. Does not authorize removing a source newline.
    // Every actual replacement must pass SourceAnalysis::CanReplaceGap.
    [[nodiscard]] std::optional<bool> SpacePreference(
        GapId gap,
        GapContext ownerContext,
        const SourceAnalysis& safety) const;

    // Scalar demand only for ordinary trailing horizontal // gaps. Safety
    // resolves count zero to one where changing adjacency needs separation.
    [[nodiscard]] std::optional<std::uint16_t> TrailingCommentCount(
        GapId gap,
        const SourceAnalysis& safety) const;

private:
    std::pmr::vector<SyntaxGapPreference> Gaps_;
    std::optional<TrailingCommentSpaceSetting> TrailingCommentSpaces_;
    friend SyntaxSpacing BuildSyntaxSpacing(
        const parser::SyntaxTree&,
        const SourceAnalysis&,
        const GapPolicies&,
        PointerReferenceDeclaratorLocation,
        std::pmr::memory_resource&,
        std::pmr::memory_resource&,
        std::optional<TrailingCommentSpaceSetting>);
};

// One explicit-stack tree traversal of nodes/direct-token spans, followed by
// one gap safety pass. No recursive walks, retokenization or per-option arrays.
// SourceAnalysis must describe this tree. Scratch may be released on return.
// Preserve delegates horizontal spelling to lowering, not unconditional source
// borrowing. At canonical topology-changing boundaries lowering uses the
// recorded syntax-safe FallbackSpace before fitting, even when horizontal
// source bytes exist and the result stays flat. Preserved topology and
// genuinely flat-only, non-rebuilt adjacencies retain source horizontal bytes.
// The actual selected combinator owns topology/context. Fallback remains
// subject to lexical/protection checks; uncertain syntax and unknown roles
// must be preserved, not flattened using a guessed role.
[[nodiscard]] SyntaxSpacing BuildSyntaxSpacing(
    const parser::SyntaxTree& tree,
    const SourceAnalysis& safety,
    const GapPolicies& spacing,
    PointerReferenceDeclaratorLocation pointerBinding,
    std::pmr::memory_resource& fileMemory,
    std::pmr::memory_resource& scratchMemory,
    std::optional<TrailingCommentSpaceSetting> trailingCommentSpaces =
        std::nullopt);

[[nodiscard]] inline SyntaxSpacing BuildSyntaxSpacing(
    const parser::SyntaxTree& tree,
    const SourceAnalysis& safety,
    const CompiledPolicies& policies,
    std::pmr::memory_resource& fileMemory,
    std::pmr::memory_resource& scratchMemory)
{
    return BuildSyntaxSpacing(
        tree,
        safety,
        policies.Spacing,
        policies.PointerReferenceDeclaratorLocation,
        fileMemory,
        scratchMemory,
        policies.TrailingCommentSpaces);
}

} // namespace formatter
