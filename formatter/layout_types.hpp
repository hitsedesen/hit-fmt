#pragma once

#include <cstddef>
#include <cstdint>
#include <memory_resource>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace formatter::layout {

using GroupId = std::uint32_t;

using AnchorId = std::uint32_t;

inline constexpr std::uint32_t InvalidId = UINT32_MAX;

using Identity = std::uint32_t;

inline constexpr Identity NoIdentity = UINT32_MAX;

// Half-open original byte offsets and immutable caller-assigned token/range ID.
// InvalidId offsets denote generated text. Borrowed text must outlive
// rendering.
struct Provenance
{
    std::uint32_t Begin = InvalidId;
    std::uint32_t End = InvalidId;
    layout::Identity Identity = NoIdentity;
};

enum class GroupMode : std::uint8_t
{
    Consistent,
    Fill
};

enum class GapKind : std::uint8_t
{
    Flat,
    Soft,
    Hard
};

enum class AnchorKind : std::uint8_t
{
    BlockRelative,
    DelimiterColumn
};

enum class Newline : std::uint8_t
{
    LF,
    CRLF
};

enum class GapNewline : std::uint8_t
{
    Inherit,
    LF,
    CRLF
};

enum class IndentationKind : std::uint8_t
{
    Spaces,
    Tabs
};

struct GapSpec
{
    GapKind Kind = GapKind::Soft;
    std::uint16_t Spaces = 1;
    // Next indivisible segment plus its same-line suffix, supplied by lowering.
    // Used on a newline with a delimiter-column anchor; 0 adds no demand.
    std::uint32_t RequiredWidth = 0;
    // Override generated newline bytes only; flat gaps emit no newline.
    GapNewline LineEnding = GapNewline::Inherit;
    // Fill may keep independently breakable children on this line. Fit the
    // supplied segment instead of charging their complete flat alternatives.
    bool FitNextSegment : 1 = false;
    // Ordinary fitting may use the segment when moving the whole suffix to
    // a new line would still overflow. Bounded probes compare both layouts.
    bool FitNextSegmentIfOverflow : 1 = false;
    // Fit the flat suffix instead of a prefix shortened by child breaks.
    // Unconditional lines still terminate that suffix demand.
    bool FitFlatTail : 1 = false;
    // Retain source indentation spelling while its width matches the anchor.
    bool PreserveIndentSpelling : 1 = false;
    // First newline honors LineEnding; remaining newlines inherit Policy.
    std::uint16_t NewlineCount = 1;
    // Optional exact horizontal source slice used instead of Spaces when flat.
    std::uint32_t FlatSourceBegin = InvalidId;
    std::uint32_t FlatSourceEnd = InvalidId;
    // Optional exact indentation after the final newline, instead of
    // generation.
    std::uint32_t IndentSourceBegin = InvalidId;
    std::uint32_t IndentSourceEnd = InvalidId;
};

struct AnchorSpec
{
    AnchorKind Kind = AnchorKind::BlockRelative;
    std::int32_t Offset = 0;
    // Relative to the block indentation captured when the anchor is marked.
    std::int32_t Fallback = 0;
    // Delimiter alignment floor relative to that block; ignored by
    // BlockRelative.
    std::uint32_t Minimum = 0;
    // Capture the enclosing base without unrealized expression continuation.
    // DelimiterColumn still captures the unchanged physical output column.
    bool StructuralBase = false;
    // Match the following continuation group's base and effective offset.
    bool Continuation = false;
    // Continuations reuse this anchor while sharing the enclosing level.
    // Otherwise BlockRelative derives an unconditional Offset from it.
    AnchorId SharedWith = InvalidId;
    // Capture physical leading indentation, even across preserved layout gaps.
    // Requires StructuralBase and excludes sharing an enclosing continuation.
    // Continuation starters retain logical compact-body indentation as a floor.
    bool LineBase = false;
    // Immutable layout can constrain the structural base to a known column.
    // Requires StructuralBase; excludes LineBase and continuation sharing.
    std::uint32_t FixedBase = InvalidId;
};

struct GroupSpec
{
    GroupMode Mode = GroupMode::Consistent;
    std::int32_t Indent = 0;
    // Require an editable owned break, not merely a multiline child.
    bool ForceBreak : 1 = false;
    // Retain an authored multiline shape without changing its flat fit cost.
    bool KeepMultiline : 1 = false;
    // A compact/pinned owner had no legal break until one was promoted.
    bool KeepMultilineFallback : 1 = false;
    // Compare coupled owned breaks with Fill during bounded layout selection.
    bool ProbeConsistent : 1 = false;
    // Prospective visual continuation, realized only by this group's line gaps.
    // Contributors require nonnegative Indent and cannot be structural
    // consumers.
    bool ExpressionContinuation = false;
    // Independent child layout uses only realized enclosing continuation.
    bool StructuralBase = false;
    // Starters on the same physical line share the first continuation offset.
    // Expression contributors also share a structural consumer's realized
    // continuation. Otherwise a new-line starter begins a new level, retaining
    // distinct expression precedence and nested structural consumer scopes.
    // Structural bodies exclude still-prospective levels and fence sharing.
    bool Continuation = false;
    // Fill may keep owned gaps flat while descendants break. Syntax can
    // disable this when the owner's breaks express a precedence-level choice.
    // Consistent groups always retain their coupled topology.
    bool ChildBreaks = true;
    // Extra cost per owned newline in bounded layout selection (0..3).
    // Higher-cost groups may defer to a cheaper following group's break.
    std::uint8_t BreakPenalty = 0;
    // Use this physical line's indentation as an independent structural base.
    // Requires StructuralBase and excludes sharing an enclosing continuation.
    // Continuation starters retain logical compact-body indentation as a floor.
    bool LineBase = false;
    // Extra cost for staying flat inside a multiline direct parent (0..3).
    // A soft shape preference, without forcing breaks in fitting layouts.
    std::uint8_t FlatInBrokenParentPenalty = 0;
    // Same fixed structural-base contract as AnchorSpec. Indent remains a
    // relative offset from this base, not an absolute contents column.
    std::uint32_t FixedBase = InvalidId;
    // Fit a standalone prefix against this many required suffix columns,
    // rather than letting the following expression force a prefix break.
    // InvalidId retains ordinary following-layout fitting.
    std::uint32_t FitSuffixWidth = InvalidId;
};

enum class Error : std::uint8_t
{
    None,
    Unsealed,
    UnbalancedGroup,
    InvalidSourceRange,
    InvalidPolicy,
    TooLarge,
    IndentationOverflow
};

struct Status
{
    Error Code = Error::None;
    std::uint32_t Position = InvalidId;

    explicit operator bool() const
    {
        return Code == Error::None;
    }
};

struct Policy
{
    // Unicode code points; invalid UTF-8 bytes each occupy one column.
    std::uint32_t Width = 80;
    std::uint8_t TabWidth = 8; // 1..32; tabs advance to output tab stops.
    Newline LineEnding = Newline::LF;
    bool SourceMap = true;
    IndentationKind Indentation = IndentationKind::Spaces;
    // When a full map is not requested, retain only supplied identities and
    // exact copies intersecting these sorted, disjoint source ranges. Views
    // are borrowed for this Render call only; they grant no edit authority.
    bool TokenMap = false;
    std::span<const Provenance> TrackSource = {};
    // Store identity-indexed output positions directly, without intermediate
    // Mapping records. Full maps and tracked exact copies remain independent.
    bool IndexedTokenMap = false;
    std::uint32_t TokenCount = 0;
};

namespace detail {

// Output positions use the same bounded 32-bit domain as source positions.
// Check in size_t before narrowing, without overflowing the addition.
[[nodiscard]] constexpr bool FitsOutput(std::size_t size, std::size_t added)
{
    return size < InvalidId && added < InvalidId - size;
}

} // namespace detail

struct Mapping
{
    std::uint32_t OutputBegin = 0;
    std::uint32_t OutputEnd = 0;
    Provenance Source;
    // Exact means borrowed source bytes, including protected source spans.
    bool Exact = false;
    bool Protected = false;
};

struct TokenPosition
{
    std::uint32_t Begin = 0;
    std::uint32_t End = 0;
    bool Present = false;
};

struct Result
{
    explicit Result(std::pmr::memory_resource* resource)
        : Map(resource)
        , Tokens(resource)
        , Broken(resource)
    { }

    Status State;
    std::string Text;
    std::pmr::vector<Mapping> Map;
    std::pmr::vector<TokenPosition> Tokens;
    // A broken decision denotes output shape, never input whitespace history.
    std::pmr::vector<std::uint8_t> Broken;
};

} // namespace formatter::layout
