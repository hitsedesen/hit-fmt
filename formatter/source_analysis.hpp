#pragma once

#include <parser/syntax_tree.hpp>

#include <cstddef>
#include <cstdint>
#include <memory_resource>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace formatter {

using GapId = parser::TokenIndex;

using RegionId = uint32_t;

inline constexpr RegionId NoRegion = UINT32_MAX;

// Reasons apply to immutable bytes, not to ordinary comment line barriers.
enum class ProtectionReason : uint16_t
{
    None = 0,
    Disabled = 1,
    SensitiveMacro = 2,
    Recovery = 4,
    Ambiguous = 8,
    Directive = 16,
    LineSplice = 32,
    Explicit = 64,
    HeaderName = 128,
    OutsideEditScope = 256,
};

constexpr ProtectionReason operator|(ProtectionReason a, ProtectionReason b)
{
    return static_cast<ProtectionReason>(static_cast<uint16_t>(a) |
        static_cast<uint16_t>(b));
}

struct ProtectedSourceRange
{
    parser::SourceRange Source;
    ProtectionReason Reasons = ProtectionReason::None;
};

enum class GapSeparation : uint8_t
{
    Optional,
    Required
};

enum class GapLineBoundary : uint8_t
{
    Any,
    Required,
    Forbidden
};

struct SourceGap
{
    parser::SourceRange Source;
    GapSeparation Separation = GapSeparation::Optional;
    GapLineBoundary LineBoundary = GapLineBoundary::Any;
    ProtectionReason Reasons = ProtectionReason::None;
    bool Preserve = false;
    // Whitespace before LF belongs to a preceding line comment/directive, not
    // to its gap. LF must be first to retain that token's exact spelling.
    bool MustStartWithLineFeed = false;
    // Lexically identical punctuation can still change grammar (e.g. [ [).
    bool ContextSensitive = false;
    // Intrinsic safety before semantic protection; a protected prefix may
    // still leave a safe, selected horizontal suffix.
    bool IntrinsicPreserve : 1 = false;
    bool IntrinsicContextSensitive : 1 = false;
    // Original gap-owned CR not immediately followed by LF. Classification
    // only: this grants no replacement, protection or partial-gap rights.
    bool HasBareCR : 1 = false;
    // Immutable byte facts shared by spacing and layout, not edit authority.
    bool HasLineFeed : 1 = false;
    bool Horizontal : 1 = true;
};

static_assert(sizeof(SourceGap) <= 16);

enum class RegionStatus : uint8_t
{
    Complete,
    Recovery,
    Ambiguous,
    RecoveryAndAmbiguous,
};

struct SourceRegion
{
    parser::TokenRange Tokens;
    parser::SourceRange Source;
    // Shared compact syntax topology; IDs are preorder indices, not pointers
    // into the parser arena. Attached includes leading/trailing trivia.
    parser::TokenRange Attached;
    RegionId Parent = NoRegion;
    RegionId FirstChild = NoRegion;
    RegionId NextSibling = NoRegion;
    parser::NodeKind Kind;
    RegionStatus Status = RegionStatus::Complete;
    parser::FunctionKind Function = parser::FunctionKind::Unknown;
};

static_assert(sizeof(SourceRegion) <= 40);

// Only these complete horizontal slices may change in a partly selected gap.
// In genuinely partly selected/protected gaps, every other byte, including
// LF/CRLF, stays exact. Fully byte-covered but insertion-fenced gaps may use
// CanSpellGapNewlines independently; these descriptors grant no such rights.
// These are properties, not edit instructions or editable owned line breaks.
enum class PartialGapPermissionKind : uint8_t
{
    FinalIndentation,
    TrimHorizontal,
};

struct PartialGapPermission
{
    GapId Gap;
    parser::SourceRange Source;
    PartialGapPermissionKind Kind;
};

struct SourceAnalysisOptions
{
    bool HonorClangFormatDirectives = false;
    std::span<const std::string_view> WhitespaceSensitiveMacros = {};
    // Half-open, valid source offsets; insertion inside a range is immutable.
    std::span<const parser::SourceRange> ImmutableRanges = {};
    // Immutable ranges in the current input. Normalization carries original
    // protection through provenance and merges it with rediscovered protection.
    std::span<const ProtectedSourceRange> InheritedProtection = {};
    // Current-input byte scope, borrowed during Build. nullopt is unrestricted;
    // an empty span permits no edits. Ranges must be nonempty, sorted, disjoint
    // and in bounds; touching ranges coalesce. Invalid ranges throw
    // std::invalid_argument. Insertion owns [Begin, End), never End or EOF.
    // Whole-gap replacement after LF also requires the following indent point.
    // After normalization, project original scope into current coordinates;
    // source-map anchors do not grant permission.
    std::optional<std::span<const parser::SourceRange>> EditScope = std::
        nullopt;
};

// Source and fileMemory must outlive a built, non-moved-from result; no tree or
// scratch pointers are retained. Gap i precedes token i; TokenCount() is the
// tail gap. Region IDs are preorder indices. Protection is sorted/disjoint:
// overlaps union reasons; touching equal reason masks coalesce.
// HeaderName protects internal gaps, not insertion before its opener; other
// reasons still apply there. Insertion at a protected range's beginning is
// fenced. Use IntersectsImmutable/Gaps for insertion fences, including EOF;
// Protection() describes bytes only.
// Validates whitespace edits, not semantic equivalence of structural edits.
// Complete means no known parser recovery or ambiguity.
class SourceAnalysis
{
public:
    explicit SourceAnalysis(std::pmr::memory_resource& fileMemory);
    SourceAnalysis(SourceAnalysis&& other) noexcept;
    SourceAnalysis(const SourceAnalysis&) = delete;

    [[nodiscard]] std::span<const SourceGap> Gaps() const
    {
        return Gaps_;
    }

    [[nodiscard]] std::string_view Source() const
    {
        return Source_;
    }

    // Resolve compact source offsets against this file's borrowed input.
    [[nodiscard]] std::string_view Original(const SourceGap& gap) const
    {
        return Source_
            .substr(gap.Source.Begin, gap.Source.End - gap.Source.Begin);
    }

    [[nodiscard]] std::string_view Original(GapId gap) const
    {
        return Original(Gaps_[gap]);
    }

    [[nodiscard]] std::span<const ProtectedSourceRange> Protection() const
    {
        return Protection_;
    }

    [[nodiscard]] std::span<const SourceRegion> Regions() const
    {
        return Regions_;
    }

    // Sparse permissions for partly selected gaps: indentation replaces a
    // complete horizontal suffix after fixed LF (or a zero-width point);
    // TrimHorizontal deletes a complete space/tab run before LF/CRLF.
    // Partial runs, newline changes and token edits remain forbidden.
    // Other safety constraints still apply.
    [[nodiscard]] std::span<const PartialGapPermission> PartialGapPermissions()
    const
    {
        return PartialGapPermissions_;
    }

    // Apply EditScope once after an unrestricted Build, in this input's byte
    // coordinates. Source and fileMemory stay live; caller ranges need not.
    // Success invalidates Protection/PartialGapPermissions views; Gaps/Regions
    // storage stays stable, but gap flags change. Exceptions change nothing.
    // Throws logic_error for unbuilt, moved-from or restricted objects;
    // invalid_argument for invalid ranges. Uses bounded temporary/file storage,
    // linear gap-byte scans and indexed scope/protection lookups.
    void RestrictEditScope(std::span<const parser::SourceRange> ranges);

    [[nodiscard]] bool IntersectsImmutable(parser::SourceRange source) const;
    // Specialized opt-in directive whitespace authority. Ignores only native
    // Directive/LineSplice protection, never real fences or edit-scope bytes.
    [[nodiscard]] bool CanRewriteDirective(parser::SourceRange source) const;
    [[nodiscard]] bool CanReplaceGap(
        GapId gap,
        std::string_view replacement) const;
    // Spelling-only authority for the ENTIRE nonempty original whitespace gap.
    // Full byte coverage can permit existing atom replacement at scope End/EOF
    // even when generic replacement lacks insertion-point ownership. No empty
    // edge, trim, indentation, topology or partial-gap permission is granted.
    // Intrinsic fences and any immutable or unselected byte reject the gap;
    // emission must also retain MustStartWithLineFeed/token-owned CR
    // boundaries.
    [[nodiscard]] bool CanSpellGapNewlines(GapId gap) const;

private:
    std::string_view Source_;
    std::pmr::vector<SourceGap> Gaps_;
    std::pmr::vector<ProtectedSourceRange> Protection_;
    // Retain precise exceptional fences before directive line coalescing.
    std::pmr::vector<ProtectedSourceRange> DirectiveFences_;
    std::pmr::vector<SourceRegion> Regions_;
    std::pmr::vector<parser::SourceRange> EditScope_;
    std::pmr::vector<PartialGapPermission> PartialGapPermissions_;
    bool HasEditScope_ = false;
    bool Built_ = false;

    friend SourceAnalysis BuildSourceAnalysis(
        const parser::SyntaxTree&,
        const SourceAnalysisOptions&,
        std::pmr::memory_resource&,
        std::pmr::memory_resource&);
};

// Lexical-only conservative maximal-munch predicate for adjacent tokens.
// Accepts parsed or TokenizeOnly trees. right must be in [1, TokenCount()).
// False does not authorize a gap edit: line/context/protection constraints also
// apply. This never relaxes token identity for contextually split template >>.
[[nodiscard]] bool TokenGapNeedsSeparation(
    const parser::SyntaxTree& tree,
    parser::TokenIndex right);

// Throws std::length_error for sizes exceeding util::MaxSourceSize. Exposed
// separately so callers/tests can validate lengths without constructing huge
// source buffers. BuildSourceAnalysis checks this before scanning/allocating.
[[nodiscard]] parser::SourceOffset CheckedSourceAnalysisSize(size_t size);

// Requires a parsed tree (TokenizeOnly has no syntax root).
// Throws std::length_error for oversized sources, before reading source bytes.
// One source scan, one token classification, one iterative tree traversal;
// O(bytes + tokens + nodes + intervals log intervals), plus name lookup in a
// scratch-sorted annotation index. Gap queries are O(replacement length), range
// queries O(log intervals). Scoped builds additionally scan gap bytes a bounded
// number of times, with O(log scope) per gap and
// O(log scope + log protection) lookup per horizontal candidate. Scope and
// sparse permission storage is file-PMR-owned; neither
// caller scope storage nor scratchMemory is retained after this call.
[[nodiscard]] SourceAnalysis BuildSourceAnalysis(
    const parser::SyntaxTree& tree,
    const SourceAnalysisOptions& options,
    std::pmr::memory_resource& fileMemory,
    std::pmr::memory_resource& scratchMemory);

} // namespace formatter
