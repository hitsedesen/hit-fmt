#include "layout_measure.hpp"
#include "layout_plan.hpp"

#include <util/memory.hpp>
#include <util/types.hpp>

#include <array>

namespace formatter::detail {

using namespace layout;
using namespace layout::detail;

void LayoutPlan::SourceWhitespace(Index begin, Index end, Newline ending)
{
    auto& gap = AddGap(Whitespace::SpellNewlines, {begin, end});
    gap.Ending = ending;

    for (auto i = begin; i < end; ++i) {
        if (Source_[i] == '\n') {
            gap.ChangedNewlines += ChangesNewline(Source_, i, ending == Newline::CRLF);
        }
    }
}

#if __has_cpp_attribute(gnu::flatten)
[[gnu::flatten]]
#endif
Result LayoutPlan::Render(Policy policy, std::pmr::memory_resource& memory) const
{
    auto* resource = &memory;
    Result result(resource);

    if (!State_) {
        result.State = State_;
        return result;
    }

    if (!Sealed_) {
        result.State = {Error::Unsealed, InvalidId};
        return result;
    }

    if (policy.Width == 0 || policy.Width == Infinity || policy.TabWidth == 0 || policy.TabWidth > 32 ||
        (policy.LineEnding != Newline::LF && policy.LineEnding != Newline::CRLF) ||
        (policy.Indentation != layout::IndentationKind::Spaces && policy.Indentation !=
            layout::IndentationKind::Tabs))
    {
        result.State = {Error::InvalidPolicy, InvalidId};
        return result;
    }

    u32 previousEnd = 0;

    for (const auto& range : policy.TrackSource) {
        if (range.Begin > range.End || range.End > Source_.size() || range.Begin < previousEnd) {
            result.State = {Error::InvalidPolicy, InvalidId};
            return result;
        }

        previousEnd = range.End;
    }

    const bool mappingRequested = policy.SourceMap || policy.TokenMap || policy.IndexedTokenMap ||
        !policy.TrackSource.empty();
    const bool indexedMappingsOnly = policy.IndexedTokenMap && !policy.SourceMap && !policy.TokenMap;
    const bool countMappings = mappingRequested && !indexedMappingsOnly;

    if (policy.IndexedTokenMap) {
        result.Tokens.resize(policy.TokenCount);
    }

    us trackedRange = 0;
    u32 trackedBegin = 0;
    const auto wantsMapping = [&] (Provenance origin, bool exact) {
        if (policy.SourceMap || (policy.TokenMap && origin.Identity != NoIdentity)) {
            return true;
        }

        if (!exact || origin.Begin == InvalidId || policy.TrackSource.empty()) {
            return false;
        }

        if (origin.Begin < trackedBegin) {
            trackedRange = std::lower_bound(
                policy.TrackSource.begin(),
                policy.TrackSource.end(),
                origin.Begin,
                [] (const Provenance& range, u32 begin) {
                    return range.End <= begin;
                }) - policy.TrackSource.begin();
        } else {
            while (trackedRange < policy.TrackSource.size() && policy.TrackSource[trackedRange].End <= origin.Begin) {
                ++trackedRange;
            }
        }

        trackedBegin = origin.Begin;
        return trackedRange < policy.TrackSource.size() && policy.TrackSource[trackedRange].Begin < origin.End;
    };
    const auto recordMapping = [&] (Mapping map) {
        if (policy.IndexedTokenMap && map.Source.Identity != NoIdentity) {
            if (map.Source.Identity >= result.Tokens.size()) {
                result.State = {Error::InvalidSourceRange};
                return;
            }

            result.Tokens[map.Source.Identity] = {map.OutputBegin, map.OutputEnd, true};
        }

        if (wantsMapping(map.Source, map.Exact)) {
            result.Map.push_back(map);
        }
    };

    // Summaries are built once bottom-up. Emission may additionally probe
    // small groups with a fixed work budget. The suffix
    // prefix stops at the next guaranteed/available boundary in broken mode;
    // flat mode includes every closer and same-line continuation. Group
    // suffixes preserve closer fitting and
    // cheaper following breaks, gap suffixes preserve Fill's next segment,
    // and owner-specific break/shape facts preserve child-only selection.
    // Text needs none of those specialized records. No fitting path rescans
    // an unbounded suffix; probes still use the source-boundary work limit.
    struct GroupCache
    {
        Measure Flat;
        Measure FlatTail;
        Measure BrokenTail;
        Order LastBreak = NoOrder;
        Order OwnedLastBreak = NoOrder;
        // First reachable break cheaper than each nonzero penalty (1..3).
        std::array<u32, 3> TailBreakPrefixes{Infinity, Infinity, Infinity};
        bool GuaranteedLine : 1 = false;
        bool ChildLayoutAllowed : 1 = false;
        bool ShapeSensitive : 1 = false;
        bool ProbeEligible : 1 = true;
        bool HasChild : 1 = false;
        bool HasConsistentAlternative : 1 = false;
        // Physical preservation is separate from flat geometry and fit tails.
        bool KeptChild : 1 = false;
        // Eligible layout depths 0..3, with all deeper levels in bit 4.
        u8 ProbeLayouts = 0;
        u8 LastBreakPenalty = 0;
    };

    struct GapCache
    {
        Measure FitTail;
        u32 ExpandedWidth;
    };

    static_assert(sizeof(GroupCache) <= 64);
    static_assert(sizeof(GapCache) <= 12);
    Vector<GroupCache> groupCache(Groups_.size(), resource);
    Vector<GapCache> gapCache(GapPolicies_.size(), resource);
    // Fixed source slices need only geometry flags during output. Fitting
    // tails belong to the much smaller table of actual layout choices.
    constexpr u8 TextLine = 1, TextPlain = 2;
    Vector<u8> textFlags(Gaps_.size() + ExtraGaps_.size(), resource);
    struct SummaryFrame
    {
        Measure Flat;
        Measure Broken;
        std::array<u32, 3> BreakPrefixes{Infinity, Infinity, Infinity};
        Order LastBreak = NoOrder;
        Order OwnedLastBreak = NoOrder;
        bool GuaranteedLine = false;
        bool OwnedConditionalLine = false;
        bool OwnedShapeChoice = false;
        bool ProbeEligible = true;
        bool HasChild = false;
        bool HasConsistentAlternative = false;
        bool KeptLine = false;
        u8 ProbeLayouts = 0;
        u8 LastBreakPenalty = 0;
    };

    Vector<SummaryFrame> summaries(resource);
    summaries.reserve(Depth_);
    SummaryFrame summary;
    Index summaryOwner = InvalidId;
    us mappingCount = 0;
    const auto accumulate = [&] (
        const Measure& flat,
        const Measure& broken,
        const Measure& flexible,
        u8 firstBreakPenalty,
        const std::array<u32, 3>& childBreakPrefixes,
        Order lastBreak,
        u8 lastBreakPenalty,
        Order ownedBreak,
        bool guaranteedLine) {
        // Retain the earliest affordable opportunity for each penalty, not
        // merely the first break or the globally cheapest one. An expensive
        // intervening list must not hide a cheaper suffix; a nearer medium-
        // cost break may still be needed when that suffix is too far away.
        for (us penalty = 1; penalty <= summary.BreakPrefixes.size(); ++penalty) {
            auto& prefix = summary.BreakPrefixes[penalty - 1];
            prefix = flat.Line
                ? flat.Prefix
                : std::min(
                    childBreakPrefixes[penalty - 1],
                    flexible.Line && firstBreakPenalty < penalty ? flexible.Prefix : AddWidth(flat.Prefix, prefix));
        }

        Prepend(summary.Flat, flat);
        Prepend(summary.Broken, broken);

        if (summary.LastBreak == NoOrder) {
            summary.LastBreak = lastBreak;
            summary.LastBreakPenalty = lastBreakPenalty;
        }

        if (summary.OwnedLastBreak == NoOrder) {
            summary.OwnedLastBreak = ownedBreak;
        }

        summary.GuaranteedLine = summary.GuaranteedLine || guaranteedLine;
    };
    Measure fixedRun;
    bool hasFixedRun = false;
    const auto accumulateFixed = [&] (const Measure& measured) {
        Prepend(fixedRun, measured);
        hasFixedRun = true;
    };
    const auto flushFixed = [&] {
        if (hasFixedRun) {
            accumulate(
                fixedRun,
                fixedRun,
                fixedRun,
                0,
                {Infinity, Infinity, Infinity},
                NoOrder,
                0,
                NoOrder,
                fixedRun.Line);
            fixedRun = {};
            hasFixedRun = false;
        }
    };
    const auto summarizeBegin = [&] (Index id) {
        flushFixed();
        const auto& group = GroupSpecs_[id];
        auto& entry = groupCache[id];
        Measure flat;
        Measure broken;
        Measure flexible;
        u8 firstBreakPenalty = 0;
        std::array<u32, 3> childBreakPrefixes{Infinity, Infinity, Infinity};
        Order lastBreak = NoOrder;
        Order ownedBreak = NoOrder;
        u32 expandedGapWidth = 0;
        bool guaranteedLine = false;

        auto& groupEntry = groupCache[id];
        flat = summary.Flat;
        groupEntry.LastBreak = summary.LastBreak;
        groupEntry.LastBreakPenalty = summary.LastBreakPenalty;
        groupEntry.OwnedLastBreak = summary.OwnedLastBreak;
        groupEntry.GuaranteedLine = summary.GuaranteedLine;
        groupEntry.ShapeSensitive = summary.OwnedShapeChoice;
        groupEntry.KeptChild = summary.KeptLine;
        lastBreak = groupEntry.LastBreak;
        // A child can stay flat in its parent's broken shape. Only its
        // unconditional lines are guaranteed without a child decision.
        guaranteedLine = flat.Line;
        // A conditional broken hard gap is a possible structural break
        // even when its flat alternative has no ordinary soft gap.
        flat.CanBreak = flat.CanBreak || summary.Broken.CanBreak;
        // An unconditional child line makes its broken alternatives
        // inevitable. Fold that prefix into enclosing fit obligations;
        // a potentially flat child still contributes its flat summary.
        const bool forced = group.ForceBreak && groupEntry.OwnedLastBreak != NoOrder;
        // Decide eligibility per owner, not for its entire subtree. An
        // outer list can require owned lines while an inner chain keeps
        // its links flat and lets a deeper argument list break.
        groupEntry.ChildLayoutAllowed = group.Mode == GroupMode::Fill && group.ChildBreaks && !forced &&
            !summary.OwnedConditionalLine;
        broken = flat.Line || forced ? summary.Broken : flat;

        if (forced) {
            flat = broken;
            flat.Line = true;
            guaranteedLine = true;
        }

        flexible = broken;

        if (!flat.Line && !forced && groupEntry.OwnedLastBreak != NoOrder) {
            flexible = summary.Broken;
            firstBreakPenalty = group.BreakPenalty;
        }

        // A wrapper without owned breaks still exposes cheaper breaks
        // in its descendants. Do not bypass coupled owned boundaries.
        if (groupEntry.OwnedLastBreak == NoOrder || groupEntry.ChildLayoutAllowed) {
            childBreakPrefixes = summary.BreakPrefixes;
        }

        entry.ProbeEligible = summary.ProbeEligible;
        entry.HasChild = summary.HasChild;
        entry.HasConsistentAlternative = summary.HasConsistentAlternative || group.ProbeConsistent;
        const auto levels = unsigned(summary.ProbeLayouts) << 1;
        entry.ProbeLayouts = u8((levels & 31) | (levels & 32 ? 16 : 0) | entry.ChildLayoutAllowed);
        summary = summaries.back();
        summaries.pop_back();
        summary.ProbeEligible &= entry.ProbeEligible;
        summary.HasChild = true;
        summary.HasConsistentAlternative |= entry.HasConsistentAlternative &&
            !(group.StructuralBase && !group.Continuation);
        summary.KeptLine = summary.KeptLine || groupEntry.KeptChild || group.KeepMultiline;
        summary.ProbeLayouts |= entry.ProbeLayouts;
        summaryOwner = Groups_[id].Parent;
        entry.Flat = flat;
        auto& tail = groupCache[id];
        tail.FlatTail = summary.Flat;
        tail.BrokenTail = summary.Broken;
        tail.TailBreakPrefixes = summary.BreakPrefixes;
        accumulate(
            flat,
            broken,
            flexible,
            firstBreakPenalty,
            childBreakPrefixes,
            lastBreak,
            groupEntry.LastBreakPenalty,
            ownedBreak,
            guaranteedLine);
        return true;
    };
    const auto summarizeEnd = [&] (Index id) {
        flushFixed();
        summaryOwner = id;
        summaries.push_back(summary);
        summary = {};
        return true;
    };
    const auto summarizeToken = [&] (Index id) {
        Measure flat;
        TokenMeasure(id, policy.TabWidth, flat);
        summary.ProbeEligible &= !flat.Line && (flat.Plain ? flat.Prefix <= 4096 : Tree_.TokenText(id).size() <= 4096);

        if (countMappings) {
            ++mappingCount;
        }

        accumulateFixed(flat);
        return true;
    };
    const auto probeGapAllowed = [&] (const GapLayout& gap, Measure flat) {
        return gap.Mode != Whitespace::SpellNewlines && gap.Mode != Whitespace::Indent && !flat.Line &&
            (gap.Mode != Whitespace::Preserve || gap.Origin.End - gap.Origin.Begin <= 4096);
    };
    const auto summarizeGap = [&] (Index id) {
        const auto& gap = Part(id);

        if ((gap.Conditional && summaryOwner != Gaps_[gap.SourceGap()].Owner) ||
            (gap.Mode == Whitespace::Comma && summaryOwner != Commas_[gap.SourceGap()].Owner))
        {
            result.State = {Error::InvalidPolicy, gap.SourceGap() * 2};
            return false;
        }

        const auto i = gap.Order;
        Measure flat;
        Measure broken;
        Measure flexible;
        u8 firstBreakPenalty = 0;
        std::array<u32, 3> childBreakPrefixes{Infinity, Infinity, Infinity};
        Order lastBreak = NoOrder;
        Order ownedBreak = NoOrder;
        u32 expandedGapWidth = 0;
        bool guaranteedLine = false;

        if (countMappings) {
            mappingCount += 2;

            if (gap.Mode == Whitespace::SpellNewlines) {
                mappingCount += us(gap.ChangedNewlines) * 2;
            }
        }

        if (gap.Mode != Whitespace::Decide && gap.Mode != Whitespace::Comma) {
            const auto measured = gap.Mode == Whitespace::Flat && !gap.FlatSource
                ? Measure{gap.Spaces, false, false}
                : MeasureText(Text(gap.Origin), policy.TabWidth);

            if (gap.Mode == Whitespace::Preserve || gap.Mode == Whitespace::SpellNewlines) {
                textFlags[id] = (measured.Line ? TextLine : 0) | (measured.Plain ? TextPlain : 0);
            }

            summary.ProbeEligible &= probeGapAllowed(gap, measured);
            accumulateFixed(measured);
            return true;
        }

        flushFixed();

        if (gap.Mode == Whitespace::Decide) {
            auto& entry = gapCache[gap.Policy];
            const auto& spec = GapPolicies_[gap.Policy];
            const auto& flatSpec = spec.Flat;
            const auto& brokenSpec = BrokenSpec(spec);
            const bool indentConditional = gap.Conditional && spec.MinLineIndentOffset != InvalidId;
            flat = MeasureFlatGap(flatSpec, Source_, policy.TabWidth);
            const auto expandedFlat = gap.Conditional ? MeasureFlatGap(brokenSpec, Source_, policy.TabWidth) : flat;
            broken = summaryOwner != InvalidId && brokenSpec.Kind == GapKind::Soft
                ? Measure{0, true, true}
                : expandedFlat;

            // An indentation-dependent brace can stay attached in a broken
            // header. Reserve its flat width when fitting preceding children.
            if (indentConditional) {
                broken = flat;
            }

            expandedGapWidth = expandedFlat.Width();

            if (summaryOwner != InvalidId && brokenSpec.Kind != GapKind::Flat && !indentConditional) {
                ownedBreak = i;
            }

            guaranteedLine = brokenSpec.Kind == GapKind::Hard && !indentConditional;

            if (gap.Conditional) {
                summary.OwnedConditionalLine = summary.OwnedConditionalLine || brokenSpec.Kind == GapKind::Hard;
                summary.OwnedShapeChoice = summary.OwnedShapeChoice || flatSpec.Spaces != brokenSpec.Spaces ||
                    flatSpec.FlatSourceBegin != brokenSpec.FlatSourceBegin ||
                    flatSpec.FlatSourceEnd != brokenSpec.FlatSourceEnd;
            }

            if (summaryOwner != InvalidId && !indentConditional &&
                (brokenSpec.Kind == GapKind::Soft || (gap.Conditional && guaranteedLine)))
            {
                lastBreak = i;
            }

            entry.FitTail = brokenSpec.FitFlatTail ? summary.Flat : summary.Broken;
            entry.ExpandedWidth = expandedGapWidth;
        } else if (gap.Mode == Whitespace::Comma) {
            broken = {1, false, false};
            summary.OwnedShapeChoice = true;
        } else {
            flat = MeasureText(Text(gap.Origin), policy.TabWidth);
            broken = flat;
            guaranteedLine = flat.Line;
        }

        flexible = broken;

        if (!flat.Line && summaryOwner != InvalidId) {
            firstBreakPenalty = GroupSpecs_[summaryOwner].BreakPenalty;
        }

        summary.ProbeEligible &= probeGapAllowed(gap, flat);
        accumulate(
            flat,
            broken,
            flexible,
            firstBreakPenalty,
            childBreakPrefixes,
            lastBreak,
            summaryOwner == InvalidId ? 0 : GroupSpecs_[summaryOwner].BreakPenalty,
            ownedBreak,
            guaranteedLine);
        return true;
    };
    Cursor reverse{Index(Tokens_.size() * 2 + 1), Index(Groups_.size()), Index(Ends_.size()), Index(Anchors_.size())};
    if (!Walk<true>(
        reverse,
        0,
        summarizeBegin,
        summarizeEnd,
        [] (Index) {
            return true;
        },
        summarizeToken,
        summarizeGap))
        return result;
    result.Broken.resize(Groups_.size(), 0);
    result.Text.reserve(Source_.size());

    if (mappingRequested) {
        result.Map.reserve(policy.SourceMap || policy.TokenMap ? mappingCount : policy.TrackSource.size());
    }

    struct CapturedAnchor
    {
        u32 Column = 0;
        u32 Block = 0;
        u32 Capture = InvalidId;
        u32 SharedOffset = InvalidId;
    };

    Vector<CapturedAnchor> anchors(Anchors_.size(), resource);
    struct Frame
    {
        u32 Indent = 0;
        u32 Tail = 0;
        Order Mandatory = NoOrder;
        u64 StartLine = 0;
        bool Flat = false;
        // Own gaps can stay flat without forcing descendants flat.
        bool OwnedFlat = false;
        bool ForceOwned = false;
        bool OwnedLine = false;
        bool ConsistentChain = false;
        u32 PendingExpressionIndent = 0;
        bool ExpressionContinuation = false;
        bool StructuralBase = false;
        bool RealizedExpressionLine = false;
        bool ReversibleIndent = true;
        u32 ContinuationBase = 0;
        u32 OpeningLineIndent = 0;
        bool HasContinuation = false;
        bool RealizedContinuationLine = false;
        u32 RealizedContinuationIndent = 0;
        u64 ContinuationStartLine = 0;
        us FlatChildPenalty = 0;
    };

    Vector<Frame> frames(resource);
    frames.reserve(us(Depth_) + 1);
    frames.emplace_back();
    Frame* current = &frames.back();
    Index activeGroup = InvalidId;
    Order currentOrder = 0;
    u32 column = 0;
    u64 lines = 0;
    u32 lineIndent = 0;
    bool linePrefix = true;
    u32 resumedLineIndent = InvalidId;
    const auto continuationLineIndent = [&] {
        return resumedLineIndent == InvalidId ? lineIndent : resumedLineIndent;
    };
    auto realizeContinuation = [&] {
        if (current->HasContinuation) {
            current->RealizedContinuationLine = true;
            current->RealizedContinuationIndent = column;
        }
    };

    auto structuralBase = [&] (const auto& spec) {
        if (spec.FixedBase != InvalidId) {
            return spec.FixedBase;
        }

        if (spec.LineBase) {
            // A compact body can have a logical indent before any line break.
            // Otherwise continuation starters use the callee's physical line.
            return spec.Continuation
                ? std::max(
                    current->HasContinuation
                    ? current->ContinuationBase
                    : current->Indent - current->PendingExpressionIndent,
                    continuationLineIndent())
                : spec.ResumedLineBase ? continuationLineIndent() : lineIndent;
        }

        if (current->HasContinuation) {
            if (current->RealizedContinuationLine) {
                return current->RealizedContinuationIndent;
            }

            if (!spec.Continuation) {
                return current->ContinuationBase;
            }
        }

        return current->Indent - current->PendingExpressionIndent;
    };
    auto sharesRealizedContinuation = [&] (const auto& spec) {
        return !spec.StructuralBase && current->StructuralBase && current->RealizedContinuationLine;
    };
    auto sharesContinuation = [&] (const auto& spec) {
        // A structural consumer already indented its contents. Contributors
        // reuse that realized level; another contributor starting on a later
        // line still creates a level, preserving expression precedence.
        return spec.Continuation && !spec.LineBase && current->HasContinuation &&
            ((lines == current->ContinuationStartLine && !(spec.StructuralBase && current->RealizedContinuationLine)) ||
                sharesRealizedContinuation(spec));
    };
    auto advance = [&] (std::string_view text, bool plain = false) {
        if (plain) {
            if (linePrefix) {
                us indent = 0;

                while (indent < text.size() && text[indent] == ' ') {
                    ++indent;
                }

                if (indent) {
                    lineIndent = AddWidth(column, u32(indent));
                }

                linePrefix = indent == text.size();
            }

            column = AddWidth(column, static_cast<u32>(text.size()));
            return;
        }

        for (us i = 0; i < text.size(); ++i) {
            const auto c = static_cast<unsigned char>(text[i]);

            if (c >= 0x80) {
                i += unicode::utf8::GetFirstCodePointSize(text.substr(i)).value_or(1) - 1;
            }

            if (c == '\r' || c == '\n') {
                column = 0;
                lineIndent = 0;
                linePrefix = true;
                resumedLineIndent = InvalidId;
                ++lines;
            } else if (c == '\t') {
                column = AddWidth(column, policy.TabWidth - column % policy.TabWidth);

                if (linePrefix) {
                    lineIndent = column;
                }
            } else {
                column = AddWidth(column, 1);

                if (linePrefix) {
                    if (c == ' ') {
                        lineIndent = column;
                    } else {
                        linePrefix = false;
                    }
                }
            }
        }
    };
    auto fits = [&] (u32 width) {
        return width != Infinity && column <= policy.Width && width <= policy.Width - column;
    };
    auto tailWidth = [&] (Measure tail) {
        return tail.Line ? tail.Prefix : AddWidth(tail.Prefix, current->Tail);
    };
    auto groupTail = [&] (const layout::GroupSpec& group, const GroupCache& entry) {
        if (group.FitSuffixWidth != InvalidId) {
            return group.FitSuffixWidth;
        }

        const auto penalty = group.BreakPenalty;
        const auto width = tailWidth(current->OwnedFlat ? entry.FlatTail : entry.BrokenTail);
        // Keep costly groups flat when a cheaper following group can break.
        // An owned-flat parent can still break in a following child; probes
        // validate that the complete candidate fits.
        return penalty != 0 ? std::min(width, entry.TailBreakPrefixes[penalty - 1]) : width;
    };
    auto resolveIndent = [&] (AnchorId id, u32 requiredWidth) {
        id = ResolveAnchor(id, currentOrder);
        auto indent = current->Indent;

        if (id != InvalidId) {
            const auto& anchor = anchors[id];
            const auto& spec = Anchors_[anchor.Capture];
            indent = anchor.Column;

            if (spec.Kind == AnchorKind::DelimiterColumn) {
                const auto minimum = std::min(
                    AddWidth(
                        anchor.Block,
                        anchor.SharedOffset != InvalidId && spec.Minimum ? anchor.SharedOffset : spec.Minimum),
                    Infinity - 1);
                indent = std::max(indent, minimum);

                if (indent >= policy.Width || requiredWidth > policy.Width - indent) {
                    indent = Offset(
                        anchor.Block,
                        anchor.SharedOffset != InvalidId && spec.Fallback == i32(spec.Minimum)
                        ? i32(anchor.SharedOffset)
                        : spec.Fallback);
                }
            }
        }

        return indent;
    };
    bool probing = false;
    bool layoutSelected = false;
    bool consistentLayout = false;
    us probeBrokenGroups = 0;
    us probeBreakPenalty = 0;
    u32 probeMaxIndent = 0;
    us childLayoutEnd = 0;
    us childLayoutBase = 0;
    us childLayoutDepth = 0;
    std::string_view pendingSource;
    const auto outputSize = [&] {
        return result.Text.size() + pendingSource.size();
    };
    const auto flushSource = [&] {
        if (!pendingSource.empty()) {
            result.Text.append(pendingSource);
            pendingSource = {};
        }
    };
    const auto outputRoom = [&] (us count) {
        if (!result.State) {
            return false;
        }

        if (!layout::detail::FitsOutput(outputSize(), count) || count > result.Text.max_size() - outputSize()) {
            result.State = {Error::TooLarge};
            return false;
        }

        return true;
    };
    auto append = [&] (std::string_view text) {
        if (!probing && !text.empty() && outputRoom(text.size())) {
            flushSource();
            result.Text.append(text);
        }
    };
    const auto appendSource = [&] (std::string_view text) {
        if (probing || text.empty() || !outputRoom(text.size())) {
            return;
        }

        if (!pendingSource.empty() && pendingSource.data() + pendingSource.size() == text.data()) {
            pendingSource = {pendingSource.data(), pendingSource.size() + text.size()};
        } else {
            flushSource();
            pendingSource = text;
        }
    };
    auto emitIndent = [&] (u32 indent) {
        lineIndent = indent;
        linePrefix = true;
        resumedLineIndent = InvalidId;

        if (probing) {
            column = indent;
            return;
        }

        const auto bytes = policy.Indentation == layout::IndentationKind::Tabs
            ? indent / policy.TabWidth + indent % policy.TabWidth
            : indent;

        if (!outputRoom(bytes)) {
            return;
        }

        flushSource();

        if (policy.Indentation == layout::IndentationKind::Tabs) {
            result.Text.append(indent / policy.TabWidth, '\t');
            result.Text.append(indent % policy.TabWidth, ' ');
        } else {
            result.Text.append(indent, ' ');
        }

        column = indent;
    };
    auto indentationError = [&] (us i) {
        if (probing) {
            return;
        }

        result.State = {Error::IndentationOverflow, static_cast<u32>(i >> 32)};
        pendingSource = {};
        result.Text.clear();
        result.Map.clear();
        result.Broken.clear();
    };
    const auto keepsOwnedLine = [&] (Index id) {
        const auto& spec = GroupSpecs_[id];
        const auto& entry = groupCache[id];
        return spec.KeepMultiline && !entry.GuaranteedLine && !(spec.KeepMultilineFallback && entry.KeptChild) &&
            entry.OwnedLastBreak != NoOrder;
    };
    const auto renderBegin = [&] (Index id) -> bool {
        const auto& spec = GroupSpecs_[id];
        const auto i = Groups_[id].BeginOrder;
        const auto& entry = groupCache[id];
        const auto& groupEntry = groupCache[id];
        const auto tail = groupTail(spec, groupEntry);
        const bool mandatory = current->Mandatory != NoOrder && current->Mandatory >= i &&
            current->Mandatory <= Groups_[id].EndOrder && lines == current->StartLine;
        const bool kept = spec.KeepMultiline && !probing;
        const bool broken = kept || mandatory ||
            (!current->Flat && entry.Flat.CanBreak && !fits(AddWidth(entry.Flat.Width(), tail)));
        const bool coupledChain = consistentLayout && spec.ProbeConsistent;
        // Keep the receiver prefix and call openers inline while chains
        // supply the alternative's breaks. Forced list topology still wins.
        const bool inlineForChain = consistentLayout && (spec.LineBase || (!current->ConsistentChain && !coupledChain));
        const bool keepOwnedFlat = broken && !kept && groupEntry.ChildLayoutAllowed && i <= childLayoutEnd &&
            (inlineForChain ||
                (childLayoutDepth && frames.size() - childLayoutBase < childLayoutDepth && !coupledChain));

        result.Broken[id] = broken;
        // Ordinary authored owner breaks stay stable when descendants later
        // wrap. Only a newly promoted compact boundary may defer to a kept child.
        const bool forceOwned = (spec.ForceBreak && groupEntry.OwnedLastBreak != NoOrder) ||
            (!probing && keepsOwnedLine(id));
        // A mandatory fallback must respect boundary costs too: an operator
        // group should use its own break before a costlier child chain link.
        const auto lastBreak = !spec.ChildBreaks && groupEntry.OwnedLastBreak != NoOrder &&
            groupEntry.LastBreakPenalty > spec.BreakPenalty
            ? groupEntry.OwnedLastBreak
            : groupEntry.LastBreak;
        auto base = current->Indent;
        auto pending = current->PendingExpressionIndent;
        auto reversible = current->ReversibleIndent;

        if (spec.StructuralBase) {
            if (!reversible || pending > base) {
                indentationError(i);
                return false;
            }

            base = structuralBase(spec);
            pending = 0;
            reversible = true;
        }

        auto indent = spec.Indent;
        const bool shared = sharesContinuation(spec);
        const bool newLineBase = spec.Continuation && !shared && current->HasContinuation &&
            lines != current->ContinuationStartLine;

        if (newLineBase) {
            // A new starter uses its enclosing line after closed children,
            // independently of their deeper physical continuation lines.
            base = continuationLineIndent();
            pending = 0;
        }

        if (shared) {
            const bool realized = sharesRealizedContinuation(spec);
            const auto continuationIndent = realized ? current->RealizedContinuationIndent : current->Indent;
            base = realized ? std::min(current->ContinuationBase, continuationIndent) : current->ContinuationBase;

            if (continuationIndent < base || continuationIndent - base > u32(INT32_MAX)) {
                indentationError(i);
                return false;
            }

            indent = i32(continuationIndent - base);
        }

        const auto logical = i64(base) + indent;

        if (spec.ExpressionContinuation) {
            const auto bias = u64(pending) + static_cast<u32>(spec.Indent);

            if (!reversible || logical >= Infinity || bias >= Infinity) {
                indentationError(i);
                return false;
            }

            if (spec.Continuation) {
                const auto expressionBase = newLineBase ? base : current->Indent - current->PendingExpressionIndent;
                pending = logical > expressionBase ? static_cast<u32>(logical - expressionBase) : 0;
            } else {
                pending = static_cast<u32>(bias);
            }
        }

        if (pending != 0 && (logical < 0 || logical >= Infinity)) {
            reversible = false;
        }

        // Materialize the child before growing storage so no parent reference
        // can survive reallocation, including if the depth bound is exceeded.
        frames
            .emplace_back(Frame{
                Offset(base, indent),
                tail,
                forceOwned
                    ? groupEntry.OwnedLastBreak
                    : broken && !keepOwnedFlat && !groupEntry.GuaranteedLine ? lastBreak : NoOrder,
                lines,
                !broken,
                !broken || keepOwnedFlat,
                forceOwned,
                false,
                current->ConsistentChain || coupledChain,
                pending,
                spec.ExpressionContinuation,
                spec.StructuralBase,
                false,
                reversible,
                spec.Continuation ? base : current->ContinuationBase,
                spec.ResumeOpeningLine ? base : continuationLineIndent(),
                spec.Continuation || (!spec.StructuralBase && current->HasContinuation),
                (shared || (!spec.Continuation && !spec.StructuralBase)) && current->RealizedContinuationLine,
                current->RealizedContinuationIndent,
                spec.Continuation && !shared ? lines : current->ContinuationStartLine,
            });
        current = &frames.back();
        activeGroup = id;
        return true;
    };
    const auto renderEnd = [&] (Index id) -> bool {
        // Shape consumers still observe multiline descendants, even when
        // this group keeps every owned gap flat.
        const bool multiline = lines != current->StartLine;

        // Kept lines constrain a trial rather than changing its shape. Otherwise
        // newly kept output can expose a different alternative on the next pass.
        if (probing && GroupSpecs_[id].KeepMultiline && (!multiline || (keepsOwnedLine(id) && !current->OwnedLine))) {
            return false;
        }

        if (probing && groupCache[id].ShapeSensitive && bool(result.Broken[id]) != multiline) {
            // A later sibling may break without making this owner
            // multiline. Do not accept conditional text or spacing
            // chosen from that sibling's prospective shape.
            return false;
        }

        result.Broken[id] = multiline;

        if (probing) {
            if (multiline) {
                ++probeBrokenGroups;
                probeBreakPenalty += current->FlatChildPenalty;
            } else if (const auto penalty = GroupSpecs_[id].FlatInBrokenParentPenalty; penalty != 0) {
                // Defer this cost until the direct parent's shape is
                // known; a wholly flat parent has no shape deviation.
                frames[frames.size() - 2].FlatChildPenalty += penalty;
            }
        }

        const bool realized = current->RealizedExpressionLine && !current->StructuralBase;
        const bool continuationRealized = current->RealizedContinuationLine && !current->StructuralBase;
        const auto continuationIndent = current->RealizedContinuationIndent;

        if (GroupSpecs_[id].ResumeOpeningLine ||
            (multiline && GroupSpecs_[id].LineBase && GroupSpecs_[id].Continuation))
        {
            // Headers and independent continuations resume their opening
            // line before wrapped children changed its physical indentation.
            resumedLineIndent = current->OpeningLineIndent;
        }

        frames.pop_back();
        current = &frames.back();

        if (continuationRealized) {
            current->RealizedContinuationLine = true;
            current->RealizedContinuationIndent = continuationIndent;
        }

        if (realized) {
            // Realize inherited contributions, not the closed child's
            // additional offset. Independent child consumers fence this.
            current->PendingExpressionIndent = 0;
            current->RealizedExpressionLine = true;
            current->ReversibleIndent = true;
        }

        activeGroup = Groups_[id].Parent;
        return true;
    };
    const auto renderAnchor = [&] (Index id) -> bool {
        const auto& spec = Anchors_[id];
        const auto i = spec.OrderKey;
        const auto sharedWith = ResolveAnchor(spec.SharedWith, i);

        if (!spec.Continuation && sharedWith != InvalidId) {
            const auto& referenced = anchors[sharedWith];
            anchors[id] = {Offset(referenced.Column, spec.Offset), referenced.Block, id, InvalidId};
            return true;
        }

        const bool shared = sharesContinuation(spec);
        auto base = current->Indent;

        if (spec.StructuralBase) {
            if (!current->ReversibleIndent || current->PendingExpressionIndent > base) {
                indentationError(i);
                return false;
            }

            base = structuralBase(spec);
        }

        auto offset = spec.Offset;
        auto sharedOffset = InvalidId;

        if (spec.Continuation && !shared && current->HasContinuation && lines != current->ContinuationStartLine) {
            base = continuationLineIndent();
        }

        if (shared) {
            const bool realized = sharesRealizedContinuation(spec);
            const auto continuationIndent = realized ? current->RealizedContinuationIndent : current->Indent;
            base = realized ? std::min(current->ContinuationBase, continuationIndent) : current->ContinuationBase;

            if (continuationIndent < base || continuationIndent - base > u32(INT32_MAX)) {
                indentationError(i);
                return false;
            }

            sharedOffset = continuationIndent - base;

            if (offset != 0) {
                offset = i32(sharedOffset);
            }
        }

        if (shared && !sharesRealizedContinuation(spec) && sharedWith != InvalidId) {
            anchors[id] = anchors[sharedWith];
            return true;
        }

        const auto target = Offset(spec.Kind == AnchorKind::BlockRelative ? base : column, offset);
        anchors[id] = {target, base, id, sharedOffset};
        return true;
    };
    const auto renderToken = [&] (Index id) {
        const auto& token = Tokens_[id];
        const auto origin = TokenOrigin(id);
        const auto outputBegin = u32(outputSize());
        bool exact = true;
        if (token.Reindent) {
            const auto preceding = origin.Begin
                ? Source_.find_last_of("\r\n", origin.Begin - 1)
                : std::string_view::npos;
            const auto lineBegin = preceding == std::string_view::npos ? 0 : preceding + 1;
            const auto text = Text(origin);
            const auto originalColumn = SourceColumn(
                Source_.substr(lineBegin, origin.Begin - lineBegin),
                policy.TabWidth);
            const auto shift = i64(column) - originalColumn;
            if (shift == 0) {
                appendSource(text);
                advance(text);
            } else {
                // Scan once, emitting borrowed runs between indentation edits.
                // Newline atoms and blank lines remain untouched, including
                // CRLF and bare CR. No temporary string is needed per line.
                us run = 0;
                for (us cursor = 0; cursor < text.size(); ++cursor) {
                    if (text[cursor] != '\r' && text[cursor] != '\n') {
                        continue;
                    }

                    if (text[cursor] == '\r' && cursor + 1 < text.size() && text[cursor + 1] == '\n') {
                        ++cursor;
                    }

                    const auto begin = cursor + 1;
                    auto end = begin;

                    while (end < text.size() && (text[end] == ' ' || text[end] == '\t')) {
                        ++end;
                    }

                    if (end == text.size() || text[end] == '\r' || text[end] == '\n') {
                        cursor = end - 1;
                        continue;
                    }

                    const auto indent = SourceColumn(text.substr(begin, end - begin), policy.TabWidth);
                    const auto shifted = std::clamp<i64>(i64(indent) + shift, 0, Infinity - 1);

                    if (shifted != indent) {
                        const auto copied = text.substr(run, begin - run);
                        appendSource(copied);
                        advance(copied);
                        emitIndent(static_cast<u32>(shifted));
                        exact = false;
                        run = end;
                    }

                    cursor = end - 1;
                }
                appendSource(text.substr(run));
                advance(text.substr(run));
            }

        } else {
            const auto text = Text(origin);
            appendSource(text);
            advance(text, token.Plain);
        }
        if (mappingRequested && !probing) {
            if (token.RunFirst) {
                for (auto original = id; original < token.RunPeer && result.State; ++original) {
                    const auto source = Tree_.Token(original).Text;
                    recordMapping({
                        outputBegin + source.Begin - origin.Begin,
                        outputBegin + source.End - origin.Begin,
                        {source.Begin, source.End, original},
                        true,
                        false,
                    });
                }
            } else {
                recordMapping({outputBegin, u32(outputSize()), origin, exact, token.Protected});
            }
        }

        return bool(result.State) && (!probing || column <= policy.Width);
    };
    const auto renderFlat = [&] (
        const GapLayout& gapPlan,
        u16 spaces,
        Provenance source,
        Provenance& emittedOrigin,
        bool& exact) {
        if (source.Begin != InvalidId) {
            auto text = Text(source);
            appendSource(text);
            advance(text);
            emittedOrigin.Begin = source.Begin;
            emittedOrigin.End = source.End;
            exact = true;
        } else {
            if (!probing) {
                if (spaces == 1 && gapPlan.Origin.End - gapPlan.Origin.Begin == 1 &&
                    Source_[gapPlan.Origin.Begin] == ' ')
                {
                    appendSource(Text(gapPlan.Origin));
                } else if (spaces && outputRoom(spaces)) {
                    flushSource();
                    result.Text.append(spaces, ' ');
                }
            }

            column = AddWidth(column, spaces);

            if (linePrefix) {
                lineIndent = column;
            }
        }
    };
    const auto renderGap = [&] (Index id) {
        const auto& gapPlan = Part(id);
        const auto i = gapPlan.Order;
        currentOrder = i;

        if (gapPlan.Mode == Whitespace::Flat && !gapPlan.FlatSource && gapPlan.Spaces == 0 &&
            !gapPlan.RealizeExpression && !policy.SourceMap)
        {
            return bool(result.State) && (!probing || column <= policy.Width);
        }

        auto outputBegin = u32(outputSize());
        auto emittedOrigin = gapPlan.Provenance();
        bool exact = gapPlan.Mode == Whitespace::Preserve;
        const bool ownerBroken = activeGroup != InvalidId && result.Broken[activeGroup];

        if (gapPlan.Mode == Whitespace::Flat) {
            const bool continuingIndent = current->RealizedContinuationLine &&
                column == current->RealizedContinuationIndent;
            renderFlat(
                gapPlan,
                gapPlan.Spaces,
                gapPlan.FlatSource ? gapPlan.Provenance() : Provenance{},
                emittedOrigin,
                exact);

            if (continuingIndent) {
                realizeContinuation();
            }
        } else if (gapPlan.Mode == Whitespace::SpellNewlines) {
            const bool targetCRLF = gapPlan.Ending == Newline::CRLF;
            const auto text = Text(gapPlan.Origin);

            if (text.size() > result.Text.max_size() ||
                (targetCRLF && gapPlan.ChangedNewlines > result.Text.max_size() - text.size()))
            {
                result.State = {Error::TooLarge, static_cast<u32>(i >> 32)};
                return false;
            }

            const auto size = targetCRLF
                ? text.size() + us(gapPlan.ChangedNewlines)
                : text.size(); // Incoming boundaries may suppress contractions.

            if (!outputRoom(size)) {
                result.State = {Error::TooLarge, static_cast<u32>(i >> 32)};
                return false;
            }

            us run = 0;
            const auto copy = [&] (us end) {
                if (run == end) {
                    return;
                }

                const auto begin = static_cast<u32>(outputSize());
                appendSource(text.substr(run, end - run));

                if (mappingRequested) {
                    recordMapping({
                        begin,
                        static_cast<u32>(outputSize()),
                        {
                            gapPlan.Origin.Begin + static_cast<u32>(run),
                            gapPlan.Origin.Begin + static_cast<u32>(end),
                            NoIdentity,
                        },
                        true,
                        true,
                    });
                }
            };

            for (us cursor = 0; cursor < text.size(); ++cursor) {
                if (text[cursor] != '\n') {
                    continue;
                }

                const bool crlf = cursor && text[cursor - 1] == '\r';
                const auto atom = cursor - (crlf ? 1 : 0);
                const bool incomingCR = atom > run
                    ? text[atom - 1] == '\r'
                    : !pendingSource.empty()
                        ? pendingSource.back() == '\r'
                        : !result.Text.empty() && result.Text.back() == '\r';

                if (!crlf && !targetCRLF && incomingCR) {
                    // The producer omitted a necessary original separator.
                    // This slice has no authority to invent or recover it.
                    result.State = {Error::InvalidSourceRange, static_cast<u32>(i >> 32)};
                    return false;
                }

                if (!ChangesNewline(Source_, gapPlan.Origin.Begin + cursor, targetCRLF) || (crlf && incomingCR)) {
                    continue;
                }

                copy(atom);
                const auto begin = static_cast<u32>(outputSize());
                append(targetCRLF ? "\r\n" : "\n");

                if (mappingRequested) {
                    recordMapping({
                        begin,
                        static_cast<u32>(outputSize()),
                        {
                            gapPlan.Origin.Begin + static_cast<u32>(atom),
                            gapPlan.Origin.Begin + static_cast<u32>(cursor + 1),
                            NoIdentity,
                        },
                        false,
                        false,
                    });
                }

                run = cursor + 1;
            }

            copy(text.size());
            // Preserve the source geometry used by fixed-prefix summaries;
            // spelling is not a new layout or owned-break decision.
            const bool continuingIndent = current->RealizedContinuationLine &&
                column == current->RealizedContinuationIndent;
            advance(text, textFlags[id] & TextPlain);

            if ((textFlags[id] & TextLine) || continuingIndent) {
                realizeContinuation();
            }

        } else if (gapPlan.Mode == Whitespace::Indent) {
            const auto& gaps = GapPolicies_[gapPlan.Policy];
            appendSource(Text(gapPlan.Origin));
            advance(Text(gapPlan.Origin));

            if (mappingRequested && !probing) {
                recordMapping({outputBegin, static_cast<u32>(outputSize()), gapPlan.Provenance(), true, true});
            }

            outputBegin = static_cast<u32>(outputSize());
            emittedOrigin = {gaps.Flat.IndentSourceBegin, gaps.Flat.IndentSourceEnd, NoIdentity};
            const auto indent = resolveIndent(gaps.Anchor, gaps.Flat.RequiredWidth);
            const auto original = Source_.substr(
                gaps.Flat.IndentSourceBegin,
                gaps.Flat.IndentSourceEnd - gaps.Flat.IndentSourceBegin);

            if (gaps.Flat.PreserveIndentSpelling && SourceColumn(original, policy.TabWidth) == indent) {
                appendSource(original);
                advance(original);
                exact = true;
            } else {
                emitIndent(indent);
            }

            realizeContinuation();
            // Do not set current->OwnedLine: only editable gaps satisfy
            // ForceOwned.

        } else if (gapPlan.Mode == Whitespace::Decide) {
            const auto& gaps = GapPolicies_[gapPlan.Policy];
            const auto anchor = gaps.Anchor;
            bool broken = ownerBroken;

            if (gapPlan.Conditional && gaps.MinLineIndentOffset != InvalidId) {
                broken = lineIndent >= AddWidth(resolveIndent(anchor, 0), gaps.MinLineIndentOffset);
            }

            const auto& gap = gapPlan.Conditional && broken ? BrokenSpec(gaps) : gaps.Flat;
            const auto& gapEntry = gapCache[gapPlan.Policy];

            if (current->OwnedFlat && broken && gapPlan.Conditional && gap.Kind == GapKind::Hard) {
                // Conditional topology belongs to the owner's broken shape;
                // it cannot be suppressed by a child-only alternative.
                return false;
            }

            const bool continuingIndent = current->RealizedContinuationLine &&
                column == current->RealizedContinuationIndent;
            bool line = gap.Kind == GapKind::Hard;

            if (gap.Kind == GapKind::Soft && ownerBroken && !current->OwnedFlat) {
                if (GroupSpecs_[activeGroup].Mode == GroupMode::Consistent ||
                    (consistentLayout && GroupSpecs_[activeGroup].ProbeConsistent))
                {
                    line = true;
                } else {
                    const auto tail = tailWidth(gapEntry.FitTail);
                    const bool segment = gap.FitNextSegment ||
                        (gap.FitNextSegmentIfFinite && gap.RequiredWidth != Infinity);
                    line = !fits(AddWidth(gapEntry.ExpandedWidth, segment ? gap.RequiredWidth : tail)) ||
                        (current->Mandatory == i &&
                            (current->ForceOwned ? !current->OwnedLine : lines == current->StartLine));
                }
            }

            if (line) {
                if (probing && activeGroup != InvalidId) {
                    probeBreakPenalty += us(gap.NewlineCount) * GroupSpecs_[activeGroup].BreakPenalty;
                }

                const bool lf = gap.LineEnding == GapNewline::LF ||
                    (gap.LineEnding == GapNewline::Inherit && policy.LineEnding == Newline::LF);
                append(lf ? "\n" : "\r\n");

                for (u32 n = 1; n < gap.NewlineCount; ++n) {
                    append(policy.LineEnding == Newline::LF ? "\n" : "\r\n");
                }

                lines += gap.NewlineCount;
                current->OwnedLine = true;

                if (current->ExpressionContinuation) {
                    if (!current->ReversibleIndent) {
                        indentationError(i);
                        return false;
                    }

                    current->PendingExpressionIndent = 0;
                    current->RealizedExpressionLine = true;
                }

                if (gap.IndentSourceBegin != InvalidId) {
                    if (mappingRequested && !probing) {
                        recordMapping({
                            outputBegin,
                            static_cast<u32>(outputSize()),
                            gapPlan.Provenance(),
                            false,
                            false,
                        });
                    }

                    outputBegin = static_cast<u32>(outputSize());
                    auto text = Source_.substr(gap.IndentSourceBegin, gap.IndentSourceEnd - gap.IndentSourceBegin);
                    emittedOrigin.Begin = gap.IndentSourceBegin;
                    emittedOrigin.End = gap.IndentSourceEnd;
                    const auto indent = resolveIndent(anchor, gap.RequiredWidth);

                    if (gap.PreserveIndentSpelling && SourceColumn(text, policy.TabWidth) != indent) {
                        emitIndent(indent);
                    } else {
                        appendSource(text);
                        column = 0;
                        lineIndent = 0;
                        linePrefix = true;
                        resumedLineIndent = InvalidId;
                        advance(text);
                        exact = true;
                    }
                } else {
                    emitIndent(resolveIndent(anchor, gap.RequiredWidth));
                }

                if (probing) {
                    probeMaxIndent = std::max(probeMaxIndent, column);
                }
            } else {
                renderFlat(gapPlan, gap.Spaces, {gap.FlatSourceBegin, gap.FlatSourceEnd}, emittedOrigin, exact);
            }

            if (line || continuingIndent) {
                realizeContinuation();
            }

        } else {
            const auto text = gapPlan.Mode == Whitespace::Comma
                ? (ownerBroken ? std::string_view(",") : std::string_view{})
                : Text(gapPlan.Origin);
            const bool continuingIndent = current->RealizedContinuationLine &&
                column == current->RealizedContinuationIndent;

            if (gapPlan.Mode == Whitespace::Comma) {
                append(text);
            } else {
                appendSource(text);
            }

            advance(text, gapPlan.Mode == Whitespace::Comma || (textFlags[id] & TextPlain));

            if (gapPlan.Mode == Whitespace::Preserve && current->HasContinuation &&
                ((textFlags[id] & TextLine) || continuingIndent) &&
                text.find_first_not_of(" \t\r\n") == std::string_view::npos)
            {
                realizeContinuation();
            }
        }

        if (gapPlan.RealizeExpression && activeGroup == Gaps_[gapPlan.SourceGap()].Owner) {
            if (!current->ReversibleIndent) {
                indentationError(i);
                return false;
            }

            current->PendingExpressionIndent = 0;
            current->RealizedExpressionLine = true;
            realizeContinuation();
        }

        if (mappingRequested && !probing && gapPlan.Mode != Whitespace::SpellNewlines) {
            recordMapping({outputBegin, u32(outputSize()), emittedOrigin, exact, gapPlan.Mode == Whitespace::Preserve});
        }

        return bool(result.State) && (!probing || column <= policy.Width);
    };
    Cursor cursor;
    const auto selectLayout = [&] (Index id) {
        const auto& spec = GroupSpecs_[id];
        const auto& entry = groupCache[id];
        const auto& groupEntry = groupCache[id];

        if (layoutSelected && Groups_[id].BeginOrder > childLayoutEnd) {
            layoutSelected = false;
            childLayoutDepth = 0;
            consistentLayout = false;
        }

        if (!layoutSelected && groupEntry.ChildLayoutAllowed && groupEntry.OwnedLastBreak != NoOrder &&
            !current->Flat && !entry.Flat.Line && entry.Flat.CanBreak &&
            !fits(AddWidth(entry.Flat.Width(), groupTail(spec, groupEntry))))
        {
            constexpr us MaxProbePositions = 256;
            constexpr us MaxProbeDepth = 4;
            const bool eligible = Groups_[id].ProbeWork < MaxProbePositions && groupEntry.ProbeEligible;
            const auto levels = groupEntry.ProbeLayouts;
            const u32 childLayoutLevels = levels & 14;
            const us candidateDepth = (levels & 24) ? MaxProbeDepth : (levels & 4) ? 3 : (levels & 2) ? 2 : 1;
            auto checkpoint = cursor;
            --checkpoint.Begin;

            if (eligible && groupEntry.HasChild) {
                const auto savedFrame = *current;
                const auto savedGroup = activeGroup;
                const auto savedColumn = column;
                const auto savedLines = lines;
                const auto savedLineIndent = lineIndent;
                const auto savedLinePrefix = linePrefix;
                const auto savedResumedLineIndent = resumedLineIndent;
                const auto savedDepth = frames.size();
                const auto tail = groupTail(spec, groupEntry);
                childLayoutBase = savedDepth;
                childLayoutEnd = Groups_[id].EndOrder;
                std::array<u64, 4> bestCost{};
                us bestDepth = 0;
                bool bestConsistent = false;
                const auto cost = [&] () -> std::array<u64, 4> {
                    const auto count = lines - savedLines;

                    // Compare complete chain layouts by line count and depth
                    // before applying local boundary preferences.
                    if (groupEntry.HasConsistentAlternative) {
                        return {count, probeMaxIndent, count + probeBreakPenalty, probeBrokenGroups};
                    }

                    return {count + probeBreakPenalty, 0, count, probeBrokenGroups};
                };
                const auto withinCost = [&] {
                    return !layoutSelected || cost() <= bestCost;
                };

                // Trial zero uses ordinary Fill. Prefix trials retain more
                // enclosing groups inline; the final alternative couples
                // eligible chain boundaries instead of breaking deeper calls.
                const auto consistentTrial = candidateDepth + 1;
                const auto trials = candidateDepth + groupEntry.HasConsistentAlternative;

                for (us trial = 0; trial <= trials; ++trial) {
                    consistentLayout = trial == consistentTrial;
                    const auto depth = trial == 0 || consistentLayout ? 0 : candidateDepth - trial + 1;

                    // Limits d and d+1 differ only for eligible groups at
                    // level d. Keep the earlier, deeper trial on ties.
                    if (trial > 1 && !consistentLayout && !(childLayoutLevels & (1u << depth))) {
                        continue;
                    }

                    childLayoutDepth = depth;
                    probeBrokenGroups = 0;
                    probeBreakPenalty = 0;
                    probeMaxIndent = 0;
                    probing = true;
                    bool fitsCandidate = true;

                    auto trialCursor = checkpoint;
                    fitsCandidate = Walk(
                        trialCursor,
                        Groups_[id].EndOrder,
                        [&] (Index g) {
                            return renderBegin(g) && withinCost();
                        },
                        [&] (Index g) {
                            return renderEnd(g) && withinCost();
                        },
                        renderAnchor,
                        renderToken,
                        renderGap);

                    fitsCandidate = fitsCandidate && fits(tail);
                    const auto candidateCost = cost();

                    if (fitsCandidate && (!layoutSelected || candidateCost < bestCost)) {
                        layoutSelected = true;
                        bestCost = candidateCost;
                        bestDepth = depth;
                        bestConsistent = consistentLayout;
                    }

                    // All touched decisions and anchors belong to not-yet-
                    // visited ranges. Ancestor slots are read-only in a probe.
                    for (auto b = checkpoint.Begin; b < trialCursor.Begin; ++b) {
                        result.Broken[Starts_[b]] = 0;
                    }

                    for (auto a = checkpoint.Anchor; a < trialCursor.Anchor; ++a) {
                        anchors[AnchorOrder_[a]] = {};
                    }

                    frames.resize(savedDepth);
                    current = &frames.back();
                    *current = savedFrame;
                    activeGroup = savedGroup;
                    column = savedColumn;
                    lines = savedLines;
                    lineIndent = savedLineIndent;
                    linePrefix = savedLinePrefix;
                    resumedLineIndent = savedResumedLineIndent;
                    probing = false;
                }

                // Replay even a selected depth-zero layout without nested
                // replanning, so the measured cost describes emitted output.
                childLayoutDepth = layoutSelected ? bestDepth : 0;
                consistentLayout = layoutSelected && bestConsistent;

            }
        }

        return renderBegin(id);
    };
    const auto leaveSelected = [&] (Order order) {
        if (layoutSelected && order > childLayoutEnd) {
            layoutSelected = false;
            childLayoutDepth = 0;
            consistentLayout = false;
        }
    };

    if (!Walk(
        cursor,
        NoOrder,
        selectLayout,
        renderEnd,
        renderAnchor,
        [&] (Index id) {
            leaveSelected(SourceOrder(Position(id) * 2 + 1));
            return renderToken(id);
        },
        [&] (Index id) {
            leaveSelected(Part(id).Order);
            return renderGap(id);
        }))
    {
        if (result.State) {
            result.State = {Error::InvalidPolicy};
        }
    }

    flushSource();
    return result;
}

} // namespace formatter::detail
