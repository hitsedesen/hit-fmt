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
    bool FitNextSegment : 1 = false;
    // Fit the flat suffix instead of a prefix shortened by child breaks.
    // Unconditional lines still terminate that suffix demand.
    bool FitFlatTail : 1 = false;
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
    bool ForceBreak = false;
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
    // Exact means borrowed source bytes, including protected verbatim atoms.
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
    explicit Result(std::pmr::memory_resource* resource);
    Status State;
    std::pmr::string Text;
    std::pmr::vector<Mapping> Map;
    std::pmr::vector<TokenPosition> Tokens;
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
    void Reserve(
        std::uint32_t nodes,
        std::uint32_t groups = 0,
        std::uint32_t sourceParts = 0);
    // Preserve search distance for unused pre-group markers omitted by a
    // producer. This grants no edit or validation authority; the producer must
    // prove that the omitted markers have no observable effects.
    GroupId BeginGroup(GroupSpec spec = {}, std::uint32_t elidedMarkers = 0);
    void EndGroup(GroupId group);
    void Text(std::string_view generated, Provenance origin = {});
    // Source slices require real offsets, never generated-provenance sentinels.
    void SourceText(
        std::uint32_t begin,
        std::uint32_t end,
        Identity identity = NoIdentity,
        bool verbatim = false);
    // Equivalent source emission with compact adjacent single-line slices and
    // flat gaps. Groups, anchors, conditional output and multiline text fence
    // runs. Every original piece retains its provenance and probe boundary.
    void AppendSourceText(
        std::uint32_t begin,
        std::uint32_t end,
        Identity identity = NoIdentity,
        bool verbatim = false);
    // Shift continuation-line indentation by the change in the first source
    // column. Preserve relative indentation, blank lines and newline spelling;
    // clamp outdented lines at column zero. The producer must authorize the
    // entire slice. A changed slice retains its identity but maps non-exactly.
    void ReindentedSourceText(
        std::uint32_t begin,
        std::uint32_t end,
        Identity identity = NoIdentity);
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
    // Borrow an exact whitespace prefix ending in LF, then generate indentation
    // without adding newlines. One node keeps both in the owning group's width
    // summary; it is multiline shape, not an editable break. Valid at root too.
    // indentBegin/indentEnd identify a horizontal source span or point.
    // Indentation maps there non-exactly with NoIdentity, even at zero width.
    // The prefix retains identity. Source must outlive Render. Anchors must be
    // marked already; anchor and requiredWidth follow Gap's fit/fallback rules.
    void SourceLineIndent(
        std::uint32_t begin,
        std::uint32_t end,
        std::uint32_t indentBegin,
        std::uint32_t indentEnd,
        AnchorId anchor = InvalidId,
        std::uint32_t requiredWidth = 0,
        Identity identity = NoIdentity);
    AnchorId MarkAnchor(AnchorSpec spec);
    // Record a source-whitespace line as realized expression continuation, not
    // an editable break. Requires preceding exact whitespace containing LF or
    // a SourceLineIndent prefix.
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
        std::pmr::memory_resource* resource =
            std::pmr::get_default_resource()) const;

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
        ReindentedText,
        ExpressionLine,
        SourceRun
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
        // ConditionalText: width floor; SourceWhitespace: changed LF count;
        // ReindentedText: first source line's beginning.
        std::uint32_t Detail = 0;
        GapNewline LineEnding = GapNewline::Inherit;
    };

    struct PrefixedTextData
    {
        GapSpec Gap;
        Provenance Origin;
    };

    enum class RunKind : std::uint8_t
    {
        Text,
        Verbatim,
        SourceGap,
        Spaces
    };

    struct RunPart
    {
        Provenance Origin;
        std::uint16_t Spaces = 0;
        RunKind Type = RunKind::Text;
        bool Plain = false;
    };

    struct RunData
    {
        std::uint32_t Begin = 0;
        std::uint32_t End = 0;
        std::uint32_t MaxText = 0;
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
        // Unfused position used to bound layout search.
        std::uint32_t LogicalPosition = 0;
        // Group identity, or an Anchor's optional SharedWith identity.
        std::uint32_t Reference = InvalidId;
        AnchorId Anchor = InvalidId;
        std::int32_t Indent = 0;
        std::int32_t Fallback = 0;

        union
        {
            std::uint32_t Minimum = 0; // Anchor alignment floor.
            std::uint32_t FitSuffixWidth; // Begin-group fitting suffix.
        };

        std::uint32_t FixedBase = InvalidId;
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
        bool Compact : 1 = false;
        bool Plain : 1 = false;

        // The constructor activates the kind's payload. All alternatives are
        // trivially copyable; SourceLineIndent uses GapData plus Origin.
        union
        {
            TextData Text;
            PrefixedTextData Prefix;
            GapData Gaps;
            GroupData Group;
            RunData Run;
        };
    };

    // Construction drafts keep the public specifications convenient on the
    // stack. Only compact atoms are retained in the file-scoped document.
    struct SimpleGapData
    {
        std::uint32_t RequiredWidth;
        AnchorId Anchor;
        std::uint16_t Spaces;
        std::uint16_t NewlineCount;
    };

    struct CompactTextData
    {
        std::uint32_t Flat;
        std::uint32_t Broken;
        std::uint32_t Detail;
    };

    struct EndData
    {
        std::uint32_t Match;
        std::uint32_t LogicalPosition;
        GroupId Reference;
    };

    struct Atom
    {
        explicit Atom(Kind kind)
            : Type(kind)
            , Text{}
        { }

        Provenance Origin;
        GroupId Owner = InvalidId;
        Kind Type;
        GapKind GapType = GapKind::Flat;
        GapNewline LineEnding = GapNewline::Inherit;
        bool Exact : 1 = false;
        bool HasPrefix : 1 = false;
        bool Compact : 1 = false;
        bool ComplexGap : 1 = false;
        bool FitNextSegment : 1 = false;
        bool FitFlatTail : 1 = false;
        bool Plain : 1 = false;

        union
        {
            CompactTextData Text;
            SimpleGapData Gap;
            RunData Run;
            EndData End;
            std::uint32_t Payload;
        };
    };

    static_assert(sizeof(Atom) <= 32);
    static_assert(sizeof(SimpleGapData) <= 12);
    static_assert(sizeof(CompactTextData) <= 12);
    static_assert(sizeof(Mapping) <= 24);
    static_assert(sizeof(RunPart) <= 16);

    Atom Pack(const Node& node);
    GapSpec GapFor(const Atom& atom, bool broken = false) const;
    AnchorId GapAnchor(const Atom& atom) const;
    const GroupData& Group(const Atom& atom) const;
    const PrefixedTextData& Prefix(const Atom& atom) const;
    std::uint32_t BorrowText(std::string_view text);
    std::string_view TextView(std::uint32_t id, Provenance origin) const;
    std::string_view FlatText(const Atom& node) const;
    std::string_view BrokenText(const Atom& node) const;

    void EmitSourceText(
        std::uint32_t begin,
        std::uint32_t end,
        Identity identity,
        bool verbatim,
        bool compact);
    void StartSourceRun();
    void AppendRunPart(RunPart part, bool logical = true);
    static RunPart FlatRunPart(GapSpec gap, Provenance origin);
    std::uint32_t Add(const Node& node);
    bool CheckOrigin(Provenance origin);
    bool CheckGapData(GapSpec spec);
    bool CheckHorizontalSource(std::uint32_t begin, std::uint32_t end);
    bool CheckReference(GroupId reference);
    void Fail(Error error);

    std::string_view Source_;
    std::pmr::vector<Atom> Nodes_;
    std::pmr::vector<CompactTextData> TextPayloads_;
    std::pmr::vector<GroupData> GroupPayloads_;
    std::pmr::vector<GapData> GapPayloads_;
    std::pmr::vector<PrefixedTextData> PrefixPayloads_;
    std::pmr::vector<BorrowedText> BorrowedTexts_;
    std::pmr::vector<RunPart> RunParts_;
    std::pmr::vector<std::uint32_t> Groups_;
    std::pmr::vector<std::uint32_t> Open_;
    std::uint32_t LogicalNodes_ = 0;
    std::uint32_t Anchors_ = 0;
    std::uint32_t RenderGaps_ = 0;
    std::uint32_t Depth_ = 0;
    Status State_;
    bool Sealed_ = false;
};

} // namespace document
