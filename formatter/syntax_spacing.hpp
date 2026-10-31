#pragma once

#include "formatter/policy.hpp"
#include "formatter/source_analysis.hpp"

#include <memory_resource>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace formatter {

// An option identity, not a second configuration schema. PointerBinding is the
// separately compiled scalar. None has no configurable preference; it may
// identify an unclassified or uncertain boundary with a safe layout fallback.
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
    TemplateArguments,
    TemplateParameters,
};

enum class SpacingConstraint : uint8_t
{
    None = 0,
    UncertainSyntax = 1,
    SourceSafety = 2,
};

// Owner is the combinator whose own edge/separator/operator is being rebuilt,
// not necessarily the deepest node at this offset. Owner indexes the matching
// SourceAnalysis::Regions(), so no parser-arena pointer is retained.
// Priority makes precedence inspectable: generic punctuation/scalar binding 1,
// edges 2, specific edges 3, overriding syntax 4. Empty interiors use their
// base rule's priority + 1. At equal priority the later/more local rule wins.
struct SyntaxGapPreference
{
    RegionId Owner = NoRegion;
    SpacingField Field = SpacingField::None;
    SpacingOwner Category : 4 = SpacingOwner::None;
    GapPolicy Policy : 3 = GapPolicy::Preserve;
    bool FallbackSpace : 1 = false;
    uint8_t Priority = 0;
    // Bit flags. Uncertain syntax has no generated flat fallback; SourceSafety
    // requires retaining the separate SourceAnalysis constraints. Priority 255
    // is reserved for an uncertain-role Preserve boundary.
    SpacingConstraint Constraint : 2 = SpacingConstraint::None;
    // Actual namespace matching closer followed by a same-line // token.
    bool NamespaceClosingComment : 1 = false;
};

static_assert(sizeof(SyntaxGapPreference) <= 8);

[[nodiscard]] GapContext SpacingOwnerContext(SpacingOwner owner, const CompiledPolicies& policies);

enum class GapAdjacency : uint8_t
{
    Source,
    AfterComma,
};

// File-arena storage; gap i precedes token i, including the tail. No source,
// policy or scratch references are retained; Owner indexes source regions.
class SyntaxSpacing
{
public:
    explicit SyntaxSpacing(std::pmr::memory_resource& fileMemory);
    SyntaxSpacing(SyntaxSpacing&& other) = default;
    SyntaxSpacing(const SyntaxSpacing& other) = delete;

    [[nodiscard]] std::span<const SyntaxGapPreference> Gaps() const
    {
        return Gaps_;
    }

    // O(1). nullopt delegates spelling to lowering. Canonical topology-changing
    // boundaries use safe flat fallback before fitting; preserved and flat-only
    // gaps retain source bytes. Syntax-boundary Preserve policies retain
    // horizontal spelling even during rebuilding. Safety and lexical separation override policy,
    // except unchanged empty gaps remain valid. This does not remove newlines;
    // replacements must pass SourceAnalysis::CanReplaceGap. Adjacency selects
    // the rule after punctuation edits without changing source safety.
    [[nodiscard]] std::optional<bool> SpacePreference(
        GapId gap,
        GapContext ownerContext,
        const SourceAnalysis& safety,
        GapAdjacency adjacency = GapAdjacency::Source) const;

    // Scalar demand only for ordinary trailing horizontal // gaps. Safety
    // resolves count zero to one where changing adjacency needs separation.
    [[nodiscard]] std::optional<std::uint16_t> TrailingCommentCount(GapId gap, const SourceAnalysis& safety) const;

private:
    std::pmr::vector<SyntaxGapPreference> Gaps_;
    GapPolicy CommaSpaceAfter_ = GapPolicy::Preserve;
    std::optional<TrailingCommentSpaceSetting> TrailingCommentSpaces_;
    friend SyntaxSpacing BuildSyntaxSpacing(
        const parser::SyntaxTree& tree,
        const SourceAnalysis& safety,
        const GapPolicies& spacing,
        PointerReferenceDeclaratorLocation pointerBinding,
        std::pmr::memory_resource& fileMemory,
        std::pmr::memory_resource& scratchMemory,
        std::optional<TrailingCommentSpaceSetting> trailingCommentSpaces);
};

// One iterative tree traversal followed by a gap safety pass. SourceAnalysis
// must describe this tree; scratch may be released on return. Records a safe
// flat fallback for lowering rebuilt boundaries, including Preserve policies.
// Uncertain syntax and unknown roles retain source bytes.
[[nodiscard]] SyntaxSpacing BuildSyntaxSpacing(
    const parser::SyntaxTree& tree,
    const SourceAnalysis& safety,
    const GapPolicies& spacing,
    PointerReferenceDeclaratorLocation pointerBinding,
    std::pmr::memory_resource& fileMemory,
    std::pmr::memory_resource& scratchMemory,
    std::optional<TrailingCommentSpaceSetting> trailingCommentSpaces = std::nullopt);

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
