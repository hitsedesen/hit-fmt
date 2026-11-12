#pragma once

#include <util/memory.hpp>
#include <util/types.hpp>

#include <cstddef>
#include <cstdint>
#include <memory_resource>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace formatter::layout {

using GroupId = u32;

using AnchorId = u32;

inline constexpr u32 InvalidId = UINT32_MAX;

using Identity = u32;

inline constexpr Identity NoIdentity = UINT32_MAX;

// Half-open original byte offsets and immutable caller-assigned token/range ID.
// InvalidId offsets denote generated text. Borrowed text must outlive
// rendering.
struct Provenance
{
    u32 Begin = InvalidId;
    u32 End = InvalidId;
    layout::Identity Identity = NoIdentity;
};

enum class GroupMode : u8
{
    Consistent,
    Fill
};

enum class GapKind : u8
{
    Flat,
    Soft,
    Hard
};

enum class AnchorKind : u8
{
    BlockRelative,
    DelimiterColumn
};

enum class Newline : u8
{
    LF,
    CRLF
};

enum class GapNewline : u8
{
    Inherit,
    LF,
    CRLF
};

enum class IndentationKind : u8
{
    Spaces,
    Tabs
};

struct GapSpec
{
    GapKind Kind = GapKind::Soft;
    u16 Spaces = 1;
    // Next indivisible segment plus its same-line suffix, supplied by lowering.
    // Used on a newline with a delimiter-column anchor; 0 adds no demand.
    u32 RequiredWidth = 0;
    // Override generated newline bytes only; flat gaps emit no newline.
    GapNewline LineEnding = GapNewline::Inherit;
    // Fill may keep independently breakable children on this line. Fit the
    // supplied segment instead of charging their complete flat alternatives.
    bool FitNextSegment : 1 = false;
    // Prefer child boundaries when the segment has a finite single-line width.
    // Embedded token newlines retain ordinary suffix-prefix measurement.
    bool FitNextSegmentIfFinite : 1 = false;
    // Fit the flat suffix instead of a prefix shortened by child breaks.
    // Unconditional lines still terminate that suffix demand.
    bool FitFlatTail : 1 = false;
    // Retain source indentation spelling while its width matches the anchor.
    bool PreserveIndentSpelling : 1 = false;
    // First newline honors LineEnding; remaining newlines inherit Policy.
    u16 NewlineCount = 1;
    // Optional exact horizontal source slice used instead of Spaces when flat.
    u32 FlatSourceBegin = InvalidId;
    u32 FlatSourceEnd = InvalidId;
    // Optional exact indentation after the final newline, instead of
    // generation.
    u32 IndentSourceBegin = InvalidId;
    u32 IndentSourceEnd = InvalidId;
};

struct AnchorSpec
{
    AnchorKind Kind = AnchorKind::BlockRelative;
    i32 Offset = 0;
    // Relative to the block indentation captured when the anchor is marked.
    i32 Fallback = 0;
    // Delimiter alignment floor relative to that block; ignored by
    // BlockRelative.
    u32 Minimum = 0;
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
    // Use the opening line resumed by a preceding header, rather than its
    // final physical continuation line. Requires LineBase.
    bool ResumedLineBase = false;
    // Immutable layout can constrain the structural base to a known column.
    // Requires StructuralBase; excludes LineBase and continuation sharing.
    u32 FixedBase = InvalidId;
};

struct GroupSpec
{
    GroupMode Mode = GroupMode::Consistent;
    i32 Indent = 0;
    // Require an editable owned break, not merely a multiline child.
    bool ForceBreak : 1 = false;
    // Retain an authored multiline shape without changing its flat fit cost.
    bool KeepMultiline : 1 = false;
    // A compact/pinned owner had no legal break until one was promoted.
    bool KeepMultilineFallback : 1 = false;
    // Resume this group's opening indentation for a following body.
    bool ResumeOpeningLine : 1 = false;
    // LineBase consumes the opening indentation resumed by its header.
    bool ResumedLineBase : 1 = false;
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
    // Without a selected layout, fit the next segment at owned soft gaps
    // and let independently breakable children handle their own width.
    bool FitPrefixFallback = false;
    // Extra cost per owned newline in bounded layout selection (0..3).
    // Higher-cost groups may defer to a cheaper following group's break.
    u8 BreakPenalty = 0;
    // Use this physical line's indentation as an independent structural base.
    // Requires StructuralBase and excludes sharing an enclosing continuation.
    // Continuation starters retain logical compact-body indentation as a floor.
    bool LineBase = false;
    // Extra cost for staying flat inside a multiline direct parent (0..3).
    // A soft shape preference, without forcing breaks in fitting layouts.
    u8 FlatInBrokenParentPenalty = 0;
    // Same fixed structural-base contract as AnchorSpec. Indent remains a
    // relative offset from this base, not an absolute contents column.
    u32 FixedBase = InvalidId;
    // Fit a standalone prefix against this many required suffix columns,
    // rather than letting the following expression force a prefix break.
    // InvalidId retains ordinary following-layout fitting.
    u32 FitSuffixWidth = InvalidId;
};

enum class Error : u8
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
    u32 Position = InvalidId;

    explicit operator bool() const
    {
        return Code == Error::None;
    }
};

struct Policy
{
    // Unicode code points; invalid UTF-8 bytes each occupy one column.
    u32 Width = 80;
    u8 TabWidth = 8; // 1..32; tabs advance to output tab stops.
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
    u32 TokenCount = 0;
};

namespace detail {

// Output positions use the same bounded 32-bit domain as source positions.
// Check in size_t before narrowing, without overflowing the addition.
[[nodiscard]] constexpr bool FitsOutput(us size, us added)
{
    return size < InvalidId && added < InvalidId - size;
}

} // namespace detail

struct Mapping
{
    u32 OutputBegin = 0;
    u32 OutputEnd = 0;
    Provenance Source;
    // Exact means borrowed source bytes, including protected source spans.
    bool Exact = false;
    bool Protected = false;
};

struct TokenPosition
{
    u32 Begin = 0;
    u32 End = 0;
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
    Vector<Mapping> Map;
    Vector<TokenPosition> Tokens;
    // A broken decision denotes output shape, never input whitespace history.
    Vector<u8> Broken;
};

} // namespace formatter::layout
