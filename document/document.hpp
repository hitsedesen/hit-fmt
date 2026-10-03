#pragma once

#include <cstddef>
#include <cstdint>
#include <memory_resource>
#include <string>
#include <string_view>
#include <vector>

namespace document {

using GroupId = std::uint32_t;

using AnchorId = std::uint32_t;

inline constexpr std::uint32_t InvalidId = UINT32_MAX;

inline constexpr std::uint64_t NoIdentity = UINT64_MAX;

// Half-open original byte offsets and immutable caller-assigned token/range ID.
// InvalidId offsets denote generated text. Borrowed text must outlive
// rendering.
struct Provenance
{
    std::uint32_t Begin = InvalidId;
    std::uint32_t End = InvalidId;
    std::uint64_t Identity = NoIdentity;
};

enum class GroupMode : std::uint8_t
{
    Consistent,
    Fill
};

enum class ContinuationNesting : std::uint8_t
{
    Shared,
    Additive
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
    ContinuationNesting Nesting = ContinuationNesting::Shared;
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
    // Shared reuses an active continuation level (including its offset).
    // Additive adds Indent; a first continuation uses Indent in either mode.
    // Non-continuation structural bodies fence the active continuation.
    bool Continuation = false;
    ContinuationNesting Nesting = ContinuationNesting::Shared;
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
};

struct Mapping
{
    std::size_t OutputBegin = 0;
    std::size_t OutputEnd = 0;
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
        std::uint64_t identity = NoIdentity,
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
        std::uint64_t identity = NoIdentity);
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

    struct Node
    {
        std::string_view FlatText;
        std::string_view BrokenText;
        Provenance Origin;
        std::uint32_t Match = InvalidId;
        std::uint32_t Owner = InvalidId;
        std::uint32_t Reference = InvalidId;
        std::uint32_t Anchor = InvalidId;
        std::int32_t Indent = 0;
        std::int32_t Fallback = 0;
        std::uint32_t Minimum = 0;

        // Kind-tagged, mutually exclusive leaf data. ConditionalText uses
        // FitWidthFloor; SourceWhitespace initializes/reads ChangedNewlines.
        // Both members are trivial scalars, copied with Node's active member.
        union
        {
            std::uint32_t FitWidthFloor = 0;
            std::uint32_t ChangedNewlines;
        };

        // SourceLineIndent stores its horizontal association in FlatGap's
        // IndentSource offsets and its delimiter demand in RequiredWidth.
        GapSpec FlatGap;
        GapSpec BrokenGap;
        Kind Type = Kind::Text;
        GroupMode Mode = GroupMode::Consistent;
        AnchorKind Alignment = AnchorKind::BlockRelative;
        bool Exact = false;
        bool ForceBreak = false;
        bool ExpressionContinuation = false;
        bool StructuralBase = false;
        bool Continuation = false;
        ContinuationNesting Nesting = ContinuationNesting::Shared;
    };

    std::uint32_t Add(Node node);
    bool CheckOrigin(Provenance origin);
    bool CheckGapData(GapSpec spec);
    bool CheckHorizontalSource(std::uint32_t begin, std::uint32_t end);
    bool CheckReference(GroupId reference);
    void Fail(Error error);

    std::string_view Source_;
    std::pmr::vector<Node> Nodes_;
    std::pmr::vector<std::uint32_t> Groups_;
    std::pmr::vector<std::uint32_t> Open_;
    std::uint32_t Anchors_ = 0;
    std::uint32_t Depth_ = 0;
    Status State_;
    bool Sealed_ = false;
};

} // namespace document
