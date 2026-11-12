#pragma once
#include "layout_measure.hpp"
#include "policy.hpp"
#include "syntax_spacing.hpp"

#include <util/memory.hpp>
#include <util/types.hpp>

#include <parser/syntax_tree.hpp>

#include <algorithm>

namespace formatter::detail {

using Index = u32;
using Position = u64;
inline constexpr Index None = UINT32_MAX;
inline constexpr u64 NoOrder = UINT64_MAX;

// The high word is a source token/gap position. Within one position, interval
// ends precede group captures/beginnings, which precede source content. Eight
// slots per group keep its captures ordered before its beginning. Planning
// bounds both token and group counts before forming these keys.
constexpr u64 SourceOrder(Position position, Index sequence = 0x80000000u)
{
    return (position << 32) | sequence;
}

enum class Whitespace : u8
{
    Empty,
    Preserve,
    SpellNewlines,
    Indent,
    Flat,
    Decide,
    Comma
};

struct WhitespacePolicy
{
    layout::GapSpec Flat;
    Index Broken = None;
    layout::AnchorId Anchor = None;
    Index MinLineIndentOffset = None;
};

// Partial edits and inserted punctuation can split a gap into fragments.
// Ordinary gaps are indexed by their source gap; fragments never contain
// token or group events. Detailed line policies are needed only for choices.
struct GapLayout
{
    u64 Order = NoOrder;
    parser::SourceRange Origin{None, None};

    // The mode selects one payload: a line policy, newline-spelling count,
    // or unconditional horizontal spacing.
    union
    {
        Index Policy;
        Index ChangedNewlines = 0;
        u16 Spaces;
    };

    Index Next = None;
    // A source-gap head stores its tail here; extra fragments store their predecessor.
    Index Previous = None;
    Whitespace Mode = Whitespace::Empty;
    layout::Newline Ending = layout::Newline::LF;
    bool FlatSource : 1 = false;
    bool Conditional : 1 = false;
    bool RealizeExpression : 1 = false;

    Index SourceGap() const
    {
        return Index(Order >> 33);
    }

    layout::Provenance Provenance() const
    {
        return {Origin.Begin, Origin.End};
    }
};

static_assert(sizeof(GapLayout) <= 32);

enum class Boundary : u8
{
    Preserve,
    Flat,
    Soft,
    Hard,
    Conditional
};

struct Gap
{
    Index Owner = None;
    Index RelativeGroup = None;
    Index BraceIndentOffset = None;
    Boundary Shape = Boundary::Preserve;
    layout::GapKind Broken = layout::GapKind::Flat;
    GapAdjacency BrokenAdjacency = GapAdjacency::Source;
    bool Closing : 1 = false;
    bool BodyOpening : 1 = false;
    // Explicit attachment takes precedence over KeepMultiline fallback.
    bool Attached : 1 = false;
    bool Generated : 1 = false;
    bool Reindent : 1 = false;
    // Style-owned indentation demand survives a scope fence on topology.
    bool PartialIndent : 1 = false;
    bool ParagraphBoundary : 1 = false;
    bool Reassembled : 1 = false;
    i32 ExtraIndent = 0;
    bool OverrideIndent : 1 = false;
    // Only final line indentation may change in otherwise frozen syntax.
    bool RelativeIndent : 1 = false;
    bool Frozen : 1 = false;
    u16 NewlineCount = 1;
    bool Vertical : 1 = false;
    bool VerticalMinimum : 1 = false;
    bool MemberGroup : 1 = false;
    bool PreserveIndent : 1 = false;
    u8 VerticalPriority = 0;
    Index MarkDelimiter = None;
    // Fit a complete syntax prefix before considering optional child breaks.
    Index FitThrough = None;
    GapContext Context = GapContext::Preserved;
    bool ContextKnown : 1 = false;
    // A flat header boundary still rebuilds source topology, so horizontal
    // Preserve spelling must use the same safe fallback before fitting.
    bool CanonicalFlat : 1 = false;
    bool ControlSeparator : 1 = false;
    bool FitNextSegment : 1 = false;
    bool FitNextSegmentIfFinite : 1 = false;
    bool FitFlatTail : 1 = false;
    bool KeepFlatSpacing : 1 = false;
    bool SpaceFallback : 1 = false;
    u16 FixedSpaces = UINT16_MAX;

};

// Cached preferences depend on immutable spacing/source analysis; safety
// invalidates widths and permissions when it changes a gap shape.
struct GapFacts
{
    Index Work = 0;
    Index SuffixWork = 0;
    Index PrefixSequence = 0;

    Index RequiredWidth = 0;
    mutable Index CachedWidth = 0;
    mutable u16 CachedCommentCount = UINT16_MAX;
    // 0 unknown, 1 preserve, 2 no space, 3 space.
    mutable u16 PreservedPreference : 2 = 0;
    mutable u16 RebuiltPreference : 2 = 0;
    mutable u16 WidthKnown : 1 = false;
    mutable u16 FlatKnown : 1 = false;
    mutable u16 LineKnown : 1 = false;
    mutable u16 FlatAllowed : 1 = false;
    mutable u16 LineAllowed : 1 = false;
};

// Style offsets fit in a byte, but preserved source indentation can be much
// wider. Keep that distinct from the validated compiled policy representation.
struct LayoutIndentation
{
    ContinuationAlignment Alignment;
    u32 ContinuationOffset;
    u32 FallbackOffset;

    LayoutIndentation(ContinuationAlignment alignment, u32 continuation, u32 fallback)
        : Alignment(alignment)
        , ContinuationOffset(continuation)
        , FallbackOffset(fallback)
    { }

    LayoutIndentation(IndentationPolicy policy)
        : LayoutIndentation(policy.Alignment, policy.ContinuationOffset, policy.FallbackOffset)
    { }
};

struct Group
{
    // Keep the fields consumed together by renderer traversal contiguous.
    u64 BeginOrder = 0, EndOrder = 0, PrefixOrder = UINT64_MAX;
    Index Parent = None;
    Index ProbeWork = 0;
    layout::AnchorId Anchor = layout::InvalidId;
    layout::AnchorId Block = layout::InvalidId;
    layout::AnchorId InitialAnchor = None;
    layout::AnchorId InitialContinuationAnchor = None;
    Position Begin;
    Position End;
    LayoutIndentation Indent;
    bool HasAnchor = false;
    bool SyntaxBody = false;
    // Delimiter placement may differ from the level shared by the condition.
    layout::AnchorId ContinuationAnchor = layout::InvalidId;
    bool IndentTernaryOperators = false;
    bool ShareBranchContinuation = false;
    Index PrefixGap = None;
    bool AnchorAfterGap = false;
    u32 LogicalIndent = 0;
    bool SaturatedIndent = false;
    Index PreviousContinuation = None;
    Index IndentGroup = None;
    bool NeedsBlock = false;
    Index PreservedSourceIndent = None;
    Index Preorder = 0;
    Index BoundaryWork = 0;

    Group(Position begin, Position end, LayoutIndentation indent, bool hasAnchor)
        : Begin(begin)
        , End(end)
        , Indent(indent)
        , HasAnchor(hasAnchor)
    { }
};

struct Comma
{
    Index Owner = None;
    Index Existing = None;
};

class Planner;

class LayoutPlan
{
    friend class Planner;
    using Order = u64;
    static constexpr auto Invalid = layout::InvalidId;

    struct TokenLayout
    {
        union
        {
            Index Prefix = 0;
            Index RunPeer;
        };

        bool Line : 1 = false;
        bool Plain : 1 = true;
        bool Protected : 1 = false;
        bool Reindent : 1 = false;
        bool RunFirst : 1 = false;
        bool RunLast : 1 = false;
    };

    struct AnchorLayout
        : layout::AnchorSpec
    {
        Order OrderKey;
    };

    struct Cursor
    {
        Index Source = 0, Begin = 0, End = 0, Anchor = 0;
        Index Part = Invalid;
    };
public:
    LayoutPlan(
        const parser::SyntaxTree& tree,
        std::pmr::memory_resource& memory,
        std::span<const FlatSpan> flatSpans = {})
        : Tree_(tree)
        , Source_(tree.Source())
        , FlatSpans_(flatSpans)
        , Tokens_(tree.TokenCount(), &memory)
        , Removed_(tree.TokenCount(), &memory)
        , Gaps_(us(tree.TokenCount()) + 1, &memory)
        , GapLayouts_(us(tree.TokenCount()) + 1, &memory)
        , Commas_(us(tree.TokenCount()) + 1, &memory)
        , Groups_(&memory)
        , GroupSpecs_(&memory)
        , ExtraGaps_(&memory)
        , GapPolicies_(&memory)
        , BrokenGaps_(&memory)
        , Starts_(&memory)
        , Ends_(&memory)
        , Anchors_(&memory)
        , AnchorOrder_(&memory)
    {
        // Layout owners are substantially fewer than tokens. Reserving their
        // common density avoids retaining successive geometric-growth buffers
        // in the file arena; denser syntax still grows normally.
        const auto groups = tree.TokenCount() / 3 + 16;
        Groups_.reserve(groups);
        GroupSpecs_.reserve(groups);
        GapPolicies_.reserve(tree.TokenCount() / 2 + 16);
    }

    [[nodiscard]] bool TokenSurvives(Index id) const
    {
        return Sealed_ && id < Tokens_.size() && !Removed_[id];
    }

    [[nodiscard]] bool TokenSpellingExact(Index id) const
    {
        return Sealed_ && id < Tokens_.size() && !Tokens_[id].Reindent;
    }

    [[nodiscard]] bool GapEmitsSpaces(Index id, us spaces) const
    {
        if (!Sealed_ || id >= GapLayouts_.size()) {
            return false;
        }

        const auto last = GapLayouts_[id].Previous;

        if (last == None) {
            return spaces == 0;
        }

        if (last != id) {
            return false;
        }

        const auto& part = Part(id);

        if (part.Next != None) {
            return false;
        }

        if (part.Mode == Whitespace::Flat && !part.FlatSource) {
            return part.Spaces == spaces;
        }

        if ((part.Mode != Whitespace::Flat || !part.FlatSource) && part.Mode != Whitespace::Preserve) {
            return false;
        }

        if (part.Origin.Begin > part.Origin.End || part.Origin.End > Source_.size()) {
            return false;
        }

        const auto text = Text(part.Origin);
        return text.size() == spaces && std::all_of(
            text.begin(),
            text.end(),
            [] (char byte) {
                return byte == ' ';
            });
    }

    layout::Result Render(layout::Policy policy, std::pmr::memory_resource& memory) const;
private:
    void BeginGap(Index id)
    {
        CurrentGap_ = id;
        Sequence_ = 0;
    }

    GapLayout& Part(Index id)
    {
        return id < Gaps_.size() ? GapLayouts_[id] : ExtraGaps_[id - Gaps_.size()];
    }

    const GapLayout& Part(Index id) const
    {
        return id < Gaps_.size() ? GapLayouts_[id] : ExtraGaps_[id - Gaps_.size()];
    }

    GapLayout& AddGap(Whitespace mode, layout::Provenance origin)
    {
        auto& last = GapLayouts_[CurrentGap_].Previous;
        auto id = CurrentGap_;

        if (last != Invalid) {
            id = Index(Gaps_.size() + ExtraGaps_.size());
            ExtraGaps_.emplace_back();
            Part(id).Previous = last;
            Part(last).Next = id;
        }

        last = id;
        auto& part = Part(id);
        part.Order = SourceOrder(Position(CurrentGap_) * 2, 0x80000000u + Sequence_++);
        part.Origin = {origin.Begin, origin.End};
        part.Mode = mode;

        if (mode == Whitespace::Flat) {
            part.Spaces = 0;
        }

        return part;
    }

    WhitespacePolicy& AddPolicy(GapLayout& gap)
    {
        gap.Policy = Index(GapPolicies_.size());
        return GapPolicies_.emplace_back();
    }

    void SourceSlice(Index begin, Index end)
    {
        AddGap(Whitespace::Preserve, {begin, end});
    }

    void SourceLineIndent(
        Index begin,
        Index end,
        Index indentBegin,
        Index indentEnd,
        layout::AnchorId anchor,
        Index width,
        bool spelling)
    {
        auto& data = AddPolicy(AddGap(Whitespace::Indent, {begin, end}));
        data.Anchor = anchor;
        data.Flat.RequiredWidth = width;
        data.Flat.PreserveIndentSpelling = spelling;
        data.Flat.IndentSourceBegin = indentBegin;
        data.Flat.IndentSourceEnd = indentEnd;
    }

    void SourceWhitespace(Index begin, Index end, layout::Newline ending);

    void ExpressionLine()
    {
        Part(GapLayouts_[CurrentGap_].Previous).RealizeExpression = true;
        ++Sequence_;
    }

    void ConditionalComma(Index offset)
    {
        AddGap(Whitespace::Comma, {offset, offset});
    }

    layout::AnchorId MarkAnchor(layout::AnchorSpec spec)
    {
        return Capture(spec, SourceOrder(Position(CurrentGap_) * 2, 0x80000000u + Sequence_++));
    }

    layout::AnchorId Capture(layout::AnchorSpec spec, Order order)
    {
        const auto id = Index(Anchors_.size());
        Anchors_.push_back({spec, order});
        return id;
    }

    static layout::AnchorId GroupAnchor(Index group, bool block = false)
    {
        return group == Invalid ? Invalid : 0x80000000u | (group << 1) | block;
    }

    layout::AnchorId AnchorAt(Index group, Order order, bool continuation = false) const
    {
        const auto& spec = Groups_[group];
        return order > spec.PrefixOrder
            ? spec.Anchor
            : continuation ? spec.InitialContinuationAnchor : spec.InitialAnchor;
    }

    const layout::GapSpec& BrokenSpec(const WhitespacePolicy& gap) const
    {
        return gap.Broken == Invalid ? gap.Flat : BrokenGaps_[gap.Broken];
    }

    layout::AnchorId ResolveAnchor(layout::AnchorId id, Order order) const
    {
        if (id == Invalid || !(id & 0x80000000u)) {
            return id;
        }

        const auto group = (id & 0x7fffffffu) >> 1;
        return id & 1 ? Groups_[group].Block : AnchorAt(group, order);
    }

    layout::Provenance TokenOrigin(Index i) const
    {
        auto range = Tree_.Token(i).Text;

        if (Tokens_[i].RunFirst) {
            range.End = Tree_.Token(Tokens_[i].RunPeer - 1).Text.End;
            return {range.Begin, range.End, layout::NoIdentity};
        }

        return {range.Begin, range.End, i};
    }

    void TokenMeasure(Index i, u8 tabWidth, layout::detail::Measure& result) const
    {
        if (Tokens_[i].RunFirst) {
            const auto source = TokenOrigin(i);
            result.Prefix = source.End - source.Begin;
            result.Line = false;
            result.CanBreak = false;
            result.Plain = true;
            return;
        }

        if (tabWidth != TabWidth_) {
            result = layout::detail::MeasureText(Tree_.TokenText(i), tabWidth);
            return;
        }

        const auto& token = Tokens_[i];
        result.Prefix = token.Prefix;
        result.Line = token.Line;
        result.CanBreak = token.Line;
        result.Plain = token.Plain;
    }

    std::string_view Text(layout::Provenance origin) const
    {
        return Source_.substr(origin.Begin, origin.End - origin.Begin);
    }

    std::string_view Text(parser::SourceRange origin) const
    {
        return Source_.substr(origin.Begin, origin.End - origin.Begin);
    }

    template <bool Reverse = false, class Begin, class End, class Anchor, class Token, class Gap>
    bool Walk(Cursor& cursor, Order limit, Begin&& begin, End&& end, Anchor&& anchor, Token&& token, Gap&& gap) const
    {
        const auto available = [] (Index i, us size) {
            return Reverse ? i != 0 : i < size;
        };
        const auto at = [] (Index i) {
            return Reverse ? i - 1 : i;
        };
        const auto consume = [] (Index& i) {
            if constexpr (Reverse) {
                --i;
            } else {
                ++i;
            }
        };

        const Order absent = Reverse ? 0 : NoOrder;
        const auto before = [] (Order a, Order b) {
            return Reverse ? a > b : a < b;
        };
        const auto outside = [&] (Order order) {
            return Reverse ? order < limit : order > limit;
        };

        const auto nextBegin = [&] {
            return available(cursor.Begin, Starts_.size()) ? Groups_[Starts_[at(cursor.Begin)]].BeginOrder : absent;
        };
        const auto nextEnd = [&] {
            return available(cursor.End, Ends_.size()) ? Groups_[Ends_[at(cursor.End)]].EndOrder : absent;
        };
        const auto nextAnchor = [&] {
            return available(cursor.Anchor, Anchors_.size())
                ? Anchors_[AnchorOrder_[at(cursor.Anchor)]].OrderKey
                : absent;
        };
        auto b = nextBegin(), e = nextEnd(), a = nextAnchor();

        for (;;) {
            const auto boundary = Reverse ? std::max({b, e, a}) : std::min({b, e, a});

            // Ordinary source positions require no group/anchor merge. The
            // next sparse boundary remains fixed until this run is consumed.
            for (;;) {
                if (!available(cursor.Source, Tokens_.size() * 2 + 1)) {
                    break;
                }

                const auto position = at(cursor.Source);
                const auto id = position / 2;
                const bool sourceToken = position & 1;

                if (sourceToken) {
                    if (Removed_[id]) {
                        consume(cursor.Source);
                        continue;
                    }
                } else if (cursor.Part == Invalid) {
                    if (GapLayouts_[id].Previous == Invalid) {
                        consume(cursor.Source);
                        continue;
                    }

                    cursor.Part = Reverse ? GapLayouts_[id].Previous : id;
                }

                const auto order = sourceToken ? SourceOrder(position) : Part(cursor.Part).Order;

                if (!before(order, boundary)) {
                    break;
                }

                if (outside(order)) {
                    return true;
                }

                if (sourceToken) {
                    auto emitted = id;

                    if constexpr (Reverse) {
                        if (Tokens_[id].RunLast) {
                            emitted = Tokens_[id].RunPeer;
                            cursor.Source = emitted * 2 + 1;
                        } else {
                            consume(cursor.Source);
                        }
                    } else {
                        if (Tokens_[id].RunFirst) {
                            cursor.Source = Tokens_[id].RunPeer * 2;
                        } else {
                            consume(cursor.Source);
                        }
                    }

                    if (!token(emitted)) {
                        return false;
                    }
                } else {
                    const auto part = cursor.Part;
                    cursor.Part = Reverse ? (part == id ? Invalid : Part(part).Previous) : Part(part).Next;

                    if (cursor.Part == Invalid) {
                        consume(cursor.Source);
                    }

                    if (!gap(part)) {
                        return false;
                    }
                }
            }

            if (boundary == absent || outside(boundary)) {
                return true;
            }

            if (b == boundary) {
                const auto id = Starts_[at(cursor.Begin)];
                consume(cursor.Begin);

                if (!begin(id)) {
                    return false;
                }

                b = nextBegin();
            } else if (e == boundary) {
                const auto id = Ends_[at(cursor.End)];
                consume(cursor.End);

                if (!end(id)) {
                    return false;
                }

                e = nextEnd();
            } else {
                const auto id = AnchorOrder_[at(cursor.Anchor)];
                consume(cursor.Anchor);

                if (!anchor(id)) {
                    return false;
                }

                a = nextAnchor();
            }
        }
    }


    const parser::SyntaxTree& Tree_;
    std::string_view Source_;
    std::span<const FlatSpan> FlatSpans_;
    Vector<TokenLayout> Tokens_;
    Vector<u8> Removed_;
    Vector<detail::Gap> Gaps_;
    Vector<GapLayout> GapLayouts_;
    Vector<Comma> Commas_;
    Vector<Group> Groups_;
    Vector<layout::GroupSpec> GroupSpecs_;
    Vector<GapLayout> ExtraGaps_;
    Vector<WhitespacePolicy> GapPolicies_;
    Vector<layout::GapSpec> BrokenGaps_;
    Vector<Index> Starts_, Ends_;
    Vector<AnchorLayout> Anchors_;
    Vector<Index> AnchorOrder_;
    Index CurrentGap_ = 0, Sequence_ = 0, Depth_ = 0;
    u8 TabWidth_ = 4;
    layout::Status State_;
    bool Sealed_ = false;
};

} // namespace formatter::detail
