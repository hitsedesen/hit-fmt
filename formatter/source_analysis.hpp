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
    return static_cast<ProtectionReason>(
        static_cast<uint16_t>(a) | static_cast<uint16_t>(b));
}

struct ProtectedSourceRange
{
    parser::SourceRange Source;
    ProtectionReason Reasons = ProtectionReason::None;
};

enum class GapSeparation : uint8_t { Optional, Required };
enum class GapLineBoundary : uint8_t { Any, Required, Forbidden };

struct SourceGap
{
    parser::SourceRange Source;
    std::string_view Original;
    GapSeparation Separation = GapSeparation::Optional;
    GapLineBoundary LineBoundary = GapLineBoundary::Any;
    ProtectionReason Reasons = ProtectionReason::None;
    bool Preserve = false;
    // Whitespace before LF belongs to a preceding line comment/directive, not
    // to its gap. LF must be first to retain that token's exact spelling.
    bool MustStartWithLineFeed = false;
    // Lexically identical punctuation can still change grammar (e.g. [ [).
    bool ContextSensitive = false;
};

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
    parser::NodeKind Kind;
    RegionId Parent = NoRegion;
    RegionStatus Status = RegionStatus::Complete;
};

// Only these complete horizontal slices may change in a partly selected gap.
// Everything else in the gap, including every LF/CRLF byte, must stay exact.
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
    // Reason-bearing immutable ranges in THIS input's coordinates. Used after
    // structural normalization to retain original reasons through provenance.
    // Rediscovered protection is merged conservatively, never substituted.
    std::span<const ProtectedSourceRange> InheritedProtection = {};
    // Current-input BYTE coordinates, borrowed only during Build. nullopt is
    // unrestricted; an engaged empty span permits no edits or insertions.
    // Ranges must be nonempty, sorted, disjoint and within the source. Touching
    // ranges are coalesced. Insertion belongs to [Begin, End), never its end
    // (including EOF). An LF-ended gap also needs ownership of its end point
    // for whole-gap replacement: LF ownership alone cannot authorize new
    // indentation on an unselected following line. Otherwise topology stays
    // fenced, even if all existing gap bytes are selected.
    // Invalid ranges throw std::invalid_argument.
    // After normalization the caller must project original authorization into
    // these coordinates; source-map anchors alone do not confer permission.
    std::optional<std::span<const parser::SourceRange>> EditScope = std::
        nullopt;
};

// Source and fileMemory must outlive this result. No tree/scratch pointers are
// retained. Gap i precedes token i; gap TokenCount() is the file tail. Region
// IDs are preorder indices. Protection intervals are sorted/disjoint, merging
// touching intervals and unioning their reasons conservatively. HeaderName
// protection owns internal gaps, not an insertion before its opening token;
// other protection reasons still own that boundary. Range queries include
// insertion at an interval's beginning conservatively. Protection() describes
// bytes; use IntersectsImmutable/Gaps for scoped insertion fences, including
// EOF, which cannot be represented by a nonempty protection interval.
// This validates whitespace edits between unchanged tokens only. It does not
// establish semantic equivalence of token insertion/removal/reordering, macro
// expansion, or structural edits. Complete means no known parser recovery or
// ambiguity, not a proof that the parser understands all C++ semantics.
class SourceAnalysis
{
public:
    explicit SourceAnalysis(std::pmr::memory_resource& fileMemory);
    SourceAnalysis(SourceAnalysis&&) = default;
    SourceAnalysis(const SourceAnalysis&) = delete;

    [[nodiscard]] std::span<const SourceGap> Gaps() const
    {
        return Gaps_;
    }
    [[nodiscard]] std::span<const ProtectedSourceRange> Protection() const
    {
        return Protection_;
    }
    [[nodiscard]] std::span<const SourceRegion> Regions() const
    {
        return Regions_;
    }
    // Sparse, source-ordered permissions only for partly selected gaps. Final
    // indentation emits only spaces/tabs in a complete suffix after a fixed
    // LF-ended prefix (possibly a zero-width point before a token).
    // TrimHorizontal may only delete a complete space/tab run immediately
    // before LF/CRLF. No token, terminator, prefix/suffix or topology edits
    // are authorized. A cut
    // horizontal run gets no permission. All other safety constraints win.
    [[nodiscard]] std::span<const PartialGapPermission>
    PartialGapPermissions() const
    {
        return PartialGapPermissions_;
    }
    [[nodiscard]] bool IntersectsImmutable(parser::SourceRange source) const;
    [[nodiscard]] bool CanReplaceGap(
        GapId gap,
        std::string_view replacement) const;

private:
    std::pmr::vector<SourceGap> Gaps_;
    std::pmr::vector<ProtectedSourceRange> Protection_;
    std::pmr::vector<SourceRegion> Regions_;
    std::pmr::vector<parser::SourceRange> EditScope_;
    std::pmr::vector<PartialGapPermission> PartialGapPermissions_;
    bool HasEditScope_ = false;

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
