#pragma once

#include <cstddef>
#include <cstdint>
#include <memory_resource>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace document {

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
    document::Identity Identity = NoIdentity;
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
    bool FitNextSegment = false;
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
    // Reuse this captured anchor only while sharing the enclosing level.
    AnchorId SharedWith = InvalidId;
    // Capture physical leading indentation, even across preserved layout gaps.
    // Requires StructuralBase and excludes continuation sharing.
    bool LineBase = false;
};

struct GroupSpec
{
    GroupMode Mode = GroupMode::Consistent;
    std::int32_t Indent = 0;
    // Require an editable owned break, not merely a multiline child.
    bool ForceBreak = false;
    // Prospective visual continuation, realized only by this group's line gaps.
    // Contributors require nonnegative Indent and cannot be structural consumers.
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
    // Syntax can discourage breaks through tightly coupled constructs.
    std::uint8_t BreakPenalty = 0;
    // Use this physical line's indentation as an independent structural base.
    // Requires StructuralBase and excludes continuation sharing.
    bool LineBase = false;
    // Extra cost for staying flat inside a multiline direct parent (0..3).
    // A soft shape preference, without forcing breaks in fitting layouts.
    std::uint8_t FlatInBrokenParentPenalty = 0;
};

enum class Error : std::uint8_t
{
    None,
    Sealed,
    Unsealed,
    UnbalancedGroup,
    InvalidGroup,
    InvalidAnchor,
    InvalidSourceRange,
    MultilineText,
    InvalidConditionalDependency,
    InvalidConditionalGap,
    InvalidPolicy,
    TooLarge,
    InvalidLinePrefix,
    IndentationOverflow
};

struct Status
{
    Error Code = Error::None;
    std::uint32_t Node = InvalidId;

    explicit operator bool() const
    {
        return Code == Error::None;
    }
};

struct Policy
{
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
    // Exact means borrowed source bytes, including protected verbatim atoms.
    bool Exact = false;
    bool Protected = false;
};

struct Result
{
    explicit Result(std::pmr::memory_resource* resource);
    Status State;
    std::pmr::string Text;
    std::pmr::vector<Mapping> Map;
    // A broken decision denotes output shape, never input whitespace history.
    std::pmr::vector<std::uint8_t> Broken;
};

// One file-scoped document. All storage uses resource; source/literal views and
// resource must outlive Document and every Render call. No per-atom string
// copy.
class Document
{
public:
    explicit Document(
        std::string_view source,
        std::pmr::memory_resource* resource = std::pmr::get_default_resource());
    ~Document();
    Document(const Document&) = delete;
    Document& operator=(const Document&) = delete;

    // Optional capacity hint avoids geometric-growth retention in arenas.
    void Reserve(std::uint32_t nodes, std::uint32_t groups = 0);
    GroupId BeginGroup(GroupSpec spec = {});
    void EndGroup(GroupId group);
    void Text(std::string_view generated, Provenance origin = {});
    // Source slices require real offsets, never generated-provenance sentinels.
    void SourceText(
        std::uint32_t begin,
        std::uint32_t end,
        Identity identity = NoIdentity,
        bool verbatim = false);
    // Producer must authorize the entire whitespace slice, including whole
    // CRLF atoms; a token-owned CR plus following gap LF must be excluded.
    // Changes spelling only, never topology/indentation/owned-break rights.
    // Retains CRLF if contraction would pair a retained bare CR with LF.
    // Even unchanged LF requires a safe incoming boundary: a producer that
    // omits its necessary separator receives InvalidSourceRange at Render.
    // Borrowed source and resource survive rendering, like SourceText.
    void SourceWhitespace(
        std::uint32_t begin,
        std::uint32_t end,
        Newline ending);
    void Verbatim(std::string_view borrowed, Provenance origin = {});
    void Gap(
        GapSpec spec = {},
        AnchorId anchor = InvalidId,
        Provenance origin = {});
    // Borrow an exact, protected whitespace/newline prefix, then generate only
    // indentation (no new newline). The nonempty prefix must end in LF;
    // it may contain spaces/tabs and intermediate blank lines. Keeping prefix
    // and indentation in one node puts the fixed line inside its owning groups,
    // so flat summaries cannot omit leading indentation. It is multiline shape,
    // never an editable owned break, and may also be used outside a group.
    // indentBegin/indentEnd must be a real horizontal source span or point;
    // generated indentation maps there non-exactly with NoIdentity, even at 0
    // width. The prefix retains identity. Source must outlive Render; anchors
    // must already be marked and resolve at their rendered position as for Gap.
    // requiredWidth has GapSpec::RequiredWidth's delimiter-demand semantics:
    // positive demand equality fits, but a target at/above Width falls back
    // even with no demand (0), as for existing gaps.
    void SourceLineIndent(
        std::uint32_t begin,
        std::uint32_t end,
        std::uint32_t indentBegin,
        std::uint32_t indentEnd,
        AnchorId anchor = InvalidId,
        std::uint32_t requiredWidth = 0,
        Identity identity = NoIdentity);
    AnchorId MarkAnchor(AnchorSpec spec);
    // Record a fixed source GAP line for the current expression contributor.
    // The preceding node must be exact source whitespace containing LF, or a
    // SourceLineIndent prefix. This is provenance, never an editable break site.
    void ExpressionLine();
    // reference must be the current owning group. No external, forward,
    // ancestor/descendant or mutually dependent shape references are accepted.
    // Text alternatives must be single-line; a gap's flat branch must be Flat.
    void ConditionalText(
        GroupId reference,
        std::string_view flat,
        std::string_view broken,
        Provenance origin = {},
        std::uint32_t fitWidthFloor = 0);
    void ConditionalGap(
        GroupId reference,
        GapSpec flat,
        GapSpec broken,
        AnchorId anchor = InvalidId,
        Provenance origin = {});
    Status Seal();
    Result Render(
        Policy policy = {},
        std::pmr::memory_resource* resource = std::pmr::get_default_resource())
    const;

private:
    enum class Kind : std::uint8_t
    {
        Begin,
        End,
        Text,
        Verbatim,
        Gap,
        Anchor,
        ConditionalText,
        ConditionalGap,
        SourceLineIndent,
        SourceWhitespace,
        ExpressionLine
    };

    // Source atoms use Origin offsets directly. Only external borrowed text
    // needs a file-scoped table entry; no pointer or size_t lives in an atom.
    inline static constexpr std::uint32_t SourceTextId = InvalidId - 1;

    struct BorrowedText
    {
        const char* Data;
        std::uint32_t Size;
    };

    struct TextData
    {
        std::uint32_t Flat = InvalidId;
        std::uint32_t Broken = InvalidId;
        // ConditionalText: width floor; SourceWhitespace: changed LF count.
        std::uint32_t Detail = 0;
        GapNewline LineEnding = GapNewline::Inherit;
    };

    struct PrefixedTextData
    {
        GapSpec Gap;
        Provenance Origin;
    };

    struct GapData
    {
        GapSpec Flat;
        GapSpec Broken;
        AnchorId Anchor = InvalidId;
    };

    struct GroupData
    {
        std::uint32_t Match = InvalidId;
        // Logical (unfused) position keeps bounded layout search unchanged.
        std::uint32_t LogicalPosition = 0;
        // Group identity, or an Anchor's optional SharedWith identity.
        std::uint32_t Reference = InvalidId;
        AnchorId Anchor = InvalidId;
        std::int32_t Indent = 0;
        std::int32_t Fallback = 0;
        std::uint32_t Minimum = 0;
        GroupMode Mode = GroupMode::Consistent;
        AnchorKind Alignment = AnchorKind::BlockRelative;
        bool ForceBreak : 1 = false;
        bool ExpressionContinuation : 1 = false;
        bool StructuralBase : 1 = false;
        bool Continuation : 1 = false;
        bool ChildBreaks : 1 = true;
        bool LineBase : 1 = false;
        std::uint8_t BreakPenalty : 2 = 0;
        std::uint8_t FlatInBrokenParentPenalty : 2 = 0;
    };

    struct Node
    {
        explicit Node(Kind kind, bool prefixed = false);
        Provenance Origin;
        GroupId Owner = InvalidId;
        Kind Type;
        bool Exact : 1 = false;
        bool HasPrefix : 1 = false;

        // The constructor activates the kind's payload. All alternatives are
        // trivially copyable; SourceLineIndent uses GapData plus Origin.
        union
        {
            TextData Text;
            PrefixedTextData Prefix;
            GapData Gaps;
            GroupData Group;
        };
    };

    static_assert(sizeof(Node) <= 80);
    static_assert(sizeof(Mapping) <= 24);

    std::uint32_t BorrowText(std::string_view text);
    std::string_view TextView(std::uint32_t id, Provenance origin) const;
    std::string_view FlatText(const Node& node) const;
    std::string_view BrokenText(const Node& node) const;

    std::uint32_t Add(Node node);
    bool CheckOrigin(Provenance origin);
    bool CheckGapData(GapSpec spec);
    bool CheckHorizontalSource(std::uint32_t begin, std::uint32_t end);
    bool CheckReference(GroupId reference);
    void Fail(Error error);

    std::string_view Source_;
    std::pmr::vector<Node> Nodes_;
    std::pmr::vector<BorrowedText> BorrowedTexts_;
    std::pmr::vector<std::uint32_t> Groups_;
    std::pmr::vector<std::uint32_t> Open_;
    std::uint32_t LogicalNodes_ = 0;
    std::uint32_t Anchors_ = 0;
    std::uint32_t Depth_ = 0;
    Status State_;
    bool Sealed_ = false;
};

} // namespace document
