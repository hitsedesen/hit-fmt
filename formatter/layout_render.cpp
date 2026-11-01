#include "layout_measure.hpp"
#include "layout_plan.hpp"

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
        (policy.Indentation != layout::IndentationKind::Spaces && policy.Indentation != layout::IndentationKind::Tabs))
    {
        result.State = {Error::InvalidPolicy, InvalidId};
        return result;
    }

    std::uint32_t previousEnd = 0;

    for (const auto& range : policy.TrackSource) {
        if (range.Begin > range.End || range.End > Source_.size() || range.Begin < previousEnd) {
            result.State = {Error::InvalidPolicy, InvalidId};
            return result;
        }

        previousEnd = range.End;
    }

    const bool mappingRequested =
        policy.SourceMap || policy.TokenMap || policy.IndexedTokenMap || !policy.TrackSource.empty();
    const bool indexedMappingsOnly = policy.IndexedTokenMap && !policy.SourceMap && !policy.TokenMap;
    const bool countMappings = mappingRequested && !indexedMappingsOnly;

    if (policy.IndexedTokenMap) {
        result.Tokens.resize(policy.TokenCount);
    }

    size_t trackedRange = 0;
    std::uint32_t trackedBegin = 0;
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
                [] (const Provenance& range, std::uint32_t begin) {
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
        std::array<std::uint32_t, 3> TailBreakPrefixes{Infinity, Infinity, Infinity};
        bool GuaranteedLine : 1 = false;
        bool ChildLayoutAllowed : 1 = false;
        bool ShapeSensitive : 1 = false;
        bool ProbeEligible : 1 = true;
        bool HasChild : 1 = false;
        // Eligible layout depths 0..3, with all deeper levels in bit 4.
        uint8_t ProbeLayouts = 0;
    };

    struct GapCache
    {
        Measure FitTail;
        std::uint32_t ExpandedWidth;
    };

    static_assert(sizeof(GroupCache) <= 64);
    static_assert(sizeof(GapCache) <= 12);
    std::pmr::vector<GroupCache> groupCache(Groups_.size(), resource);
    std::pmr::vector<GapCache> gapCache(GapPolicies_.size(), resource);
    // Fixed source slices need only geometry flags during output. Fitting
    // tails belong to the much smaller table of actual layout choices.
    constexpr uint8_t TextLine = 1, TextPlain = 2;
    std::pmr::vector<uint8_t> textFlags(Gaps_.size() + ExtraGaps_.size(), resource);
    struct SummaryFrame
    {
        Measure Flat;
        Measure Broken;
        std::array<std::uint32_t, 3> BreakPrefixes{Infinity, Infinity, Infinity};
        Order LastBreak = NoOrder;
        Order OwnedLastBreak = NoOrder;
        bool GuaranteedLine = false;
        bool OwnedConditionalLine = false;
        bool OwnedShapeChoice = false;
        bool ProbeEligible = true;
        bool HasChild = false;
        uint8_t ProbeLayouts = 0;
    };

    std::pmr::vector<SummaryFrame> summaries(resource);
    summaries.reserve(Depth_);
    SummaryFrame summary;
    Index summaryOwner = InvalidId;
    std::size_t mappingCount = 0;
    const auto accumulate = [&] (
        Measure flat,
        Measure broken,
        Measure flexible,
        std::uint8_t firstBreakPenalty,
        const std::array<uint32_t, 3>& childBreakPrefixes,
        Order lastBreak,
        Order ownedBreak,
        bool guaranteedLine) {
            // Retain the earliest affordable opportunity for each penalty, not
            // merely the first break or the globally cheapest one. An expensive
            // intervening list must not hide a cheaper suffix; a nearer medium-
            // cost break may still be needed when that suffix is too far away.
            for (std::size_t penalty = 1; penalty <= summary.BreakPrefixes.size(); ++penalty) {
                auto& prefix = summary.BreakPrefixes[penalty - 1];
                prefix = flat.Line
                    ? flat.Prefix
                    : std::min(
                        childBreakPrefixes[penalty - 1],
                        flexible.Line && firstBreakPenalty < penalty ? flexible.Prefix : AddWidth(flat.Prefix, prefix));
            }

            summary.Flat = Join(flat, summary.Flat);
            summary.Broken = Join(broken, summary.Broken);

            if (summary.LastBreak == NoOrder) {
                summary.LastBreak = lastBreak;
            }

            if (summary.OwnedLastBreak == NoOrder) {
                summary.OwnedLastBreak = ownedBreak;
            }

            summary.GuaranteedLine = summary.GuaranteedLine || guaranteedLine;
        };
    Measure fixedRun;
    bool hasFixedRun = false;
    const auto accumulateFixed = [&] (Measure measured) {
        fixedRun = Join(measured, fixedRun);
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
        std::uint8_t firstBreakPenalty = 0;
        std::array<std::uint32_t, 3> childBreakPrefixes{
            Infinity,
            Infinity,
            Infinity,
        };
        Order lastBreak = NoOrder;
        Order ownedBreak = NoOrder;
        std::uint32_t expandedGapWidth = 0;
        bool guaranteedLine = false;

        auto& groupEntry = groupCache[id];
        flat = summary.Flat;
        groupEntry.LastBreak = summary.LastBreak;
        groupEntry.OwnedLastBreak = summary.OwnedLastBreak;
        groupEntry.GuaranteedLine = summary.GuaranteedLine;
        groupEntry.ShapeSensitive = summary.OwnedShapeChoice;
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
        groupEntry.ChildLayoutAllowed =
            group.Mode == GroupMode::Fill && group.ChildBreaks && !forced && !summary.OwnedConditionalLine;
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
        const auto levels = unsigned(summary.ProbeLayouts) << 1;
        entry.ProbeLayouts = uint8_t((levels & 31) | (levels & 32 ? 16 : 0) | entry.ChildLayoutAllowed);
        summary = summaries.back();
        summaries.pop_back();
        summary.ProbeEligible &= entry.ProbeEligible;
        summary.HasChild = true;
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
        const auto flat = TokenMeasure(id, policy.TabWidth);
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
        std::uint8_t firstBreakPenalty = 0;
        std::array<std::uint32_t, 3> childBreakPrefixes{
            Infinity,
            Infinity,
            Infinity,
        };
        Order lastBreak = NoOrder;
        Order ownedBreak = NoOrder;
        std::uint32_t expandedGapWidth = 0;
        bool guaranteedLine = false;

        if (countMappings) {
            mappingCount += 2;

            if (gap.Mode == Whitespace::SpellNewlines) {
                mappingCount += size_t(gap.ChangedNewlines) * 2;
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
            flat = MeasureFlatGap(flatSpec, Source_, policy.TabWidth);
            const auto expandedFlat = gap.Conditional ? MeasureFlatGap(brokenSpec, Source_, policy.TabWidth) : flat;
            broken =
                summaryOwner != InvalidId && brokenSpec.Kind == GapKind::Soft ? Measure{0, true, true} : expandedFlat;
            expandedGapWidth = expandedFlat.Width();

            if (summaryOwner != InvalidId && brokenSpec.Kind != GapKind::Flat) {
                ownedBreak = i;
            }

            guaranteedLine = brokenSpec.Kind == GapKind::Hard;

            if (gap.Conditional) {
                summary.OwnedConditionalLine = summary.OwnedConditionalLine || guaranteedLine;
                summary.OwnedShapeChoice = summary.OwnedShapeChoice || flatSpec.Spaces != brokenSpec.Spaces ||
                    flatSpec.FlatSourceBegin != brokenSpec.FlatSourceBegin ||
                    flatSpec.FlatSourceEnd != brokenSpec.FlatSourceEnd;
            }

            if (summaryOwner != InvalidId && (brokenSpec.Kind == GapKind::Soft || (gap.Conditional && guaranteedLine)))
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
        result.Map.reserve(indexedMappingsOnly ? policy.TrackSource.size() : mappingCount);
    }

    struct CapturedAnchor
    {
        std::uint32_t Column = 0;
        std::uint32_t Block = 0;
        std::uint32_t Capture = InvalidId;
        std::uint32_t SharedOffset = InvalidId;
    };

    std::pmr::vector<CapturedAnchor> anchors(Anchors_.size(), resource);
    struct Frame
    {
        std::uint32_t Indent = 0;
        std::uint32_t Tail = 0;
        Order Mandatory = NoOrder;
        std::uint64_t StartLine = 0;
        bool Flat = false;
        // Own gaps can stay flat without forcing descendants flat.
        bool OwnedFlat = false;
        bool ForceOwned = false;
        bool OwnedLine = false;
        std::uint32_t PendingExpressionIndent = 0;
        bool ExpressionContinuation = false;
        bool StructuralBase = false;
        bool RealizedExpressionLine = false;
        bool ReversibleIndent = true;
        std::uint32_t ContinuationBase = 0;
        bool HasContinuation = false;
        bool RealizedContinuationLine = false;
        std::uint32_t RealizedContinuationIndent = 0;
        std::uint64_t ContinuationStartLine = 0;
        std::size_t FlatChildPenalty = 0;
    };

    std::pmr::vector<Frame> frames(resource);
    frames.reserve(Depth_);
    Frame frame;
    Index activeGroup = InvalidId;
    Order currentOrder = 0;
    std::uint32_t column = 0;
    std::uint64_t lines = 0;
    std::uint32_t lineIndent = 0;
    bool linePrefix = true;
    auto realizeContinuation = [&] {
        if (frame.HasContinuation) {
            frame.RealizedContinuationLine = true;
            frame.RealizedContinuationIndent = column;
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
                    frame.HasContinuation ? frame.ContinuationBase : frame.Indent - frame.PendingExpressionIndent,
                    lineIndent)
                : lineIndent;
        }

        if (frame.HasContinuation) {
            if (frame.RealizedContinuationLine) {
                return frame.RealizedContinuationIndent;
            }

            if (!spec.Continuation) {
                return frame.ContinuationBase;
            }
        }

        return frame.Indent - frame.PendingExpressionIndent;
    };
    auto sharesRealizedContinuation = [&] (const auto& spec) {
        return !spec.StructuralBase && frame.StructuralBase && frame.RealizedContinuationLine;
    };
    auto sharesContinuation = [&] (const auto& spec) {
        // A structural consumer already indented its contents. Contributors
        // reuse that realized level; another contributor starting on a later
        // line still creates a level, preserving expression precedence.
        return spec.Continuation && !spec.LineBase && frame.HasContinuation &&
            ((lines == frame.ContinuationStartLine && !(spec.StructuralBase && frame.RealizedContinuationLine)) ||
                sharesRealizedContinuation(spec));
    };
    auto advance = [&] (std::string_view text, bool plain = false) {
        if (plain) {
            if (linePrefix) {
                size_t indent = 0;

                while (indent < text.size() && text[indent] == ' ') {
                    ++indent;
                }

                if (indent) {
                    lineIndent = AddWidth(column, uint32_t(indent));
                }

                linePrefix = indent == text.size();
            }

            column = AddWidth(column, static_cast<std::uint32_t>(text.size()));
            return;
        }

        for (std::size_t i = 0; i < text.size(); ++i) {
            const auto c = static_cast<unsigned char>(text[i]);

            if (c >= 0x80) {
                i += unicode::utf8::GetFirstCodePointSize(text.substr(i)).value_or(1) - 1;
            }

            if (c == '\r' || c == '\n') {
                column = 0;
                lineIndent = 0;
                linePrefix = true;
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
    auto fits = [&] (std::uint32_t width) {
        return width != Infinity && column <= policy.Width && width <= policy.Width - column;
    };
    auto tailWidth = [&] (Measure tail) {
        return tail.Line ? tail.Prefix : AddWidth(tail.Prefix, frame.Tail);
    };
    auto groupTail = [&] (const layout::GroupSpec& group, const GroupCache& entry) {
        if (group.FitSuffixWidth != InvalidId) {
            return group.FitSuffixWidth;
        }

        const auto penalty = group.BreakPenalty;
        const auto width = tailWidth(frame.OwnedFlat ? entry.FlatTail : entry.BrokenTail);
        // Keep costly groups flat when a cheaper following group can break.
        // An owned-flat parent still requires its complete flat suffix.
        return !frame.OwnedFlat && penalty != 0 ? std::min(width, entry.TailBreakPrefixes[penalty - 1]) : width;
    };
    auto resolveIndent = [&] (AnchorId id, std::uint32_t requiredWidth) {
        id = ResolveAnchor(id, currentOrder);
        auto indent = frame.Indent;

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
                        anchor.SharedOffset != InvalidId && spec.Fallback == int32_t(spec.Minimum)
                        ? int32_t(anchor.SharedOffset)
                        : spec.Fallback);
                }
            }
        }

        return indent;
    };
    bool probing = false;
    bool layoutSelected = false;
    std::size_t probeBrokenGroups = 0;
    std::size_t probeBreakPenalty = 0;
    std::size_t childLayoutEnd = 0;
    std::size_t childLayoutBase = 0;
    std::size_t childLayoutDepth = 0;
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
    const auto outputRoom = [&] (std::size_t count) {
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
    auto emitIndent = [&] (std::uint32_t indent) {
        lineIndent = indent;
        linePrefix = true;

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
    auto indentationError = [&] (std::size_t i) {
        if (probing) {
            return;
        }

        result.State = {Error::IndentationOverflow, static_cast<std::uint32_t>(i >> 32)};
        pendingSource = {};
        result.Text.clear();
        result.Map.clear();
        result.Broken.clear();
    };
    const auto renderBegin = [&] (Index id) -> bool {
        const auto& spec = GroupSpecs_[id];
        const auto i = Groups_[id].BeginOrder;
        const auto& entry = groupCache[id];
        const auto& groupEntry = groupCache[id];
        const auto tail = groupTail(spec, groupEntry);
        const bool mandatory = frame.Mandatory != NoOrder && frame.Mandatory >= i &&
            frame.Mandatory <= Groups_[id].EndOrder && lines == frame.StartLine;
        const bool broken =
            mandatory || (!frame.Flat && entry.Flat.CanBreak && !fits(AddWidth(entry.Flat.Width(), tail)));
        const bool keepOwnedFlat = broken && childLayoutDepth && groupEntry.ChildLayoutAllowed && i <= childLayoutEnd &&
            frames.size() - childLayoutBase < childLayoutDepth;
        result.Broken[id] = broken;
        frames.push_back(frame);
        const bool forceOwned = spec.ForceBreak && groupEntry.OwnedLastBreak != NoOrder;
        auto base = frame.Indent;
        auto pending = frame.PendingExpressionIndent;
        auto reversible = frame.ReversibleIndent;

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
        const bool newLineBase =
            spec.Continuation && !shared && frame.HasContinuation && lines != frame.ContinuationStartLine;

        if (newLineBase) {
            // A new starter uses this physical line, not an unrealized
            // ancestor offset or a previous child's continuation line.
            base = lineIndent;
            pending = 0;
        }

        if (shared) {
            const bool realized = sharesRealizedContinuation(spec);
            const auto continuationIndent = realized ? frame.RealizedContinuationIndent : frame.Indent;
            base = realized ? std::min(frame.ContinuationBase, continuationIndent) : frame.ContinuationBase;

            if (continuationIndent < base || continuationIndent - base > uint32_t(INT32_MAX)) {
                indentationError(i);
                return false;
            }

            indent = int32_t(continuationIndent - base);
        }

        const auto logical = std::int64_t(base) + indent;

        if (spec.ExpressionContinuation) {
            const auto bias = std::uint64_t(pending) + static_cast<std::uint32_t>(spec.Indent);

            if (!reversible || logical >= Infinity || bias >= Infinity) {
                indentationError(i);
                return false;
            }

            if (spec.Continuation) {
                const auto expressionBase = newLineBase ? base : frame.Indent - frame.PendingExpressionIndent;
                pending = logical > expressionBase ? static_cast<std::uint32_t>(logical - expressionBase) : 0;
            } else {
                pending = static_cast<std::uint32_t>(bias);
            }
        }

        if (pending != 0 && (logical < 0 || logical >= Infinity)) {
            reversible = false;
        }

        frame = {
            Offset(base, indent),
            tail,
            forceOwned
                ? groupEntry.OwnedLastBreak
                : broken && !keepOwnedFlat && !groupEntry.GuaranteedLine ? groupEntry.LastBreak : NoOrder,
            lines,
            !broken,
            !broken || keepOwnedFlat,
            forceOwned,
            false,
            pending,
            spec.ExpressionContinuation,
            spec.StructuralBase,
            false,
            reversible,
            spec.Continuation ? base : frame.ContinuationBase,
            spec.Continuation || (!spec.StructuralBase && frame.HasContinuation),
            (shared || (!spec.Continuation && !spec.StructuralBase)) && frame.RealizedContinuationLine,
            frame.RealizedContinuationIndent,
            spec.Continuation && !shared ? lines : frame.ContinuationStartLine,
        };
        activeGroup = id;
        return true;
    };
    const auto renderEnd = [&] (Index id) -> bool {
        // Shape consumers still observe multiline descendants, even when
        // this group keeps every owned gap flat.
        const bool multiline = lines != frame.StartLine;

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
                probeBreakPenalty += frame.FlatChildPenalty;
            } else if (const auto penalty = GroupSpecs_[id].FlatInBrokenParentPenalty; penalty != 0) {
                // Defer this cost until the direct parent's shape is
                // known; a wholly flat parent has no shape deviation.
                frames.back().FlatChildPenalty += penalty;
            }
        }

        const bool realized = frame.RealizedExpressionLine && !frame.StructuralBase;
        const bool continuationRealized = frame.RealizedContinuationLine && !frame.StructuralBase;
        const auto continuationIndent = frame.RealizedContinuationIndent;
        frame = frames.back();
        frames.pop_back();

        if (continuationRealized) {
            frame.RealizedContinuationLine = true;
            frame.RealizedContinuationIndent = continuationIndent;
        }

        if (realized) {
            // Realize inherited contributions, not the closed child's
            // additional offset. Independent child consumers fence this.
            frame.PendingExpressionIndent = 0;
            frame.RealizedExpressionLine = true;
            frame.ReversibleIndent = true;
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
        auto base = frame.Indent;

        if (spec.StructuralBase) {
            if (!frame.ReversibleIndent || frame.PendingExpressionIndent > base) {
                indentationError(i);
                return false;
            }

            base = structuralBase(spec);
        }

        auto offset = spec.Offset;
        auto sharedOffset = InvalidId;

        if (spec.Continuation && !shared && frame.HasContinuation && lines != frame.ContinuationStartLine) {
            base = lineIndent;
        }

        if (shared) {
            const bool realized = sharesRealizedContinuation(spec);
            const auto continuationIndent = realized ? frame.RealizedContinuationIndent : frame.Indent;
            base = realized ? std::min(frame.ContinuationBase, continuationIndent) : frame.ContinuationBase;

            if (continuationIndent < base || continuationIndent - base > uint32_t(INT32_MAX)) {
                indentationError(i);
                return false;
            }

            sharedOffset = continuationIndent - base;

            if (offset != 0) {
                offset = int32_t(sharedOffset);
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
        const auto outputBegin = uint32_t(outputSize());
        bool exact = true;
        if (token.Reindent) {
            const auto preceding = origin.Begin
                ? Source_.find_last_of("\r\n", origin.Begin - 1)
                : std::string_view::npos;
            const auto lineBegin = preceding == std::string_view::npos ? 0 : preceding + 1;
            const auto text = Text(origin);
            const auto originalColumn =
                SourceColumn(Source_.substr(lineBegin, origin.Begin - lineBegin), policy.TabWidth);
            const auto shift = std::int64_t(column) - originalColumn;
            if (shift == 0) {
                appendSource(text);
                advance(text);
            } else {
                // Scan once, emitting borrowed runs between indentation edits.
                // Newline atoms and blank lines remain untouched, including
                // CRLF and bare CR. No temporary string is needed per line.
                std::size_t run = 0;
                for (std::size_t cursor = 0; cursor < text.size(); ++cursor) {
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
                    const auto shifted = std::clamp<std::int64_t>(std::int64_t(indent) + shift, 0, Infinity - 1);
                    if (shifted != indent) {
                        const auto copied = text.substr(run, begin - run);
                        appendSource(copied);
                        advance(copied);
                        emitIndent(static_cast<std::uint32_t>(shifted));
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
            recordMapping({outputBegin, uint32_t(outputSize()), origin, exact, token.Protected});
        }

        return bool(result.State) && (!probing || column <= policy.Width);
    };
    const auto renderFlat =
        [&] (const GapLayout& gapPlan, uint16_t spaces, Provenance source, Provenance& emittedOrigin, bool& exact) {
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

        auto outputBegin = uint32_t(outputSize());
        auto emittedOrigin = gapPlan.Provenance();
        bool exact = gapPlan.Mode == Whitespace::Preserve;
        const bool ownerBroken = activeGroup != InvalidId && result.Broken[activeGroup];

        if (gapPlan.Mode == Whitespace::Flat) {
            const bool continuingIndent = frame.RealizedContinuationLine && column == frame.RealizedContinuationIndent;
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
                result.State = {Error::TooLarge, static_cast<std::uint32_t>(i >> 32)};
                return false;
            }

            const auto size = targetCRLF
                ? text.size() + std::size_t(gapPlan.ChangedNewlines)
                : text.size(); // Incoming boundaries may suppress contractions.

            if (!outputRoom(size)) {
                result.State = {Error::TooLarge, static_cast<std::uint32_t>(i >> 32)};
                return false;
            }

            std::size_t run = 0;
            const auto copy = [&] (std::size_t end) {
                if (run == end) {
                    return;
                }

                const auto begin = static_cast<std::uint32_t>(outputSize());
                appendSource(text.substr(run, end - run));

                if (mappingRequested) {
                    recordMapping({
                        begin,
                        static_cast<std::uint32_t>(outputSize()),
                        {
                            gapPlan.Origin.Begin + static_cast<std::uint32_t>(run),
                            gapPlan.Origin.Begin + static_cast<std::uint32_t>(end),
                            NoIdentity,
                        },
                        true,
                        true,
                    });
                }
            };

            for (std::size_t cursor = 0; cursor < text.size(); ++cursor) {
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
                    result.State = {Error::InvalidSourceRange, static_cast<std::uint32_t>(i >> 32)};
                    return false;
                }

                if (!ChangesNewline(Source_, gapPlan.Origin.Begin + cursor, targetCRLF) || (crlf && incomingCR)) {
                    continue;
                }

                copy(atom);
                const auto begin = static_cast<std::uint32_t>(outputSize());
                append(targetCRLF ? "\r\n" : "\n");

                if (mappingRequested) {
                    recordMapping({
                        begin,
                        static_cast<std::uint32_t>(outputSize()),
                        {
                            gapPlan.Origin.Begin + static_cast<std::uint32_t>(atom),
                            gapPlan.Origin.Begin + static_cast<std::uint32_t>(cursor + 1),
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
            const bool continuingIndent = frame.RealizedContinuationLine && column == frame.RealizedContinuationIndent;
            advance(text, textFlags[id] & TextPlain);

            if ((textFlags[id] & TextLine) || continuingIndent) {
                realizeContinuation();
            }

        } else if (gapPlan.Mode == Whitespace::Indent) {
            const auto& gaps = GapPolicies_[gapPlan.Policy];
            appendSource(Text(gapPlan.Origin));
            advance(Text(gapPlan.Origin));

            if (mappingRequested && !probing) {
                recordMapping({
                    outputBegin,
                    static_cast<std::uint32_t>(outputSize()),
                    gapPlan.Provenance(),
                    true,
                    true,
                });
            }

            outputBegin = static_cast<std::uint32_t>(outputSize());
            emittedOrigin = {gaps.Flat.IndentSourceBegin, gaps.Flat.IndentSourceEnd, NoIdentity};
            const auto indent = resolveIndent(gaps.Anchor, gaps.Flat.RequiredWidth);
            const auto original =
                Source_.substr(gaps.Flat.IndentSourceBegin, gaps.Flat.IndentSourceEnd - gaps.Flat.IndentSourceBegin);

            if (gaps.Flat.PreserveIndentSpelling && SourceColumn(original, policy.TabWidth) == indent) {
                appendSource(original);
                advance(original);
                exact = true;
            } else {
                emitIndent(indent);
            }

            realizeContinuation();
            // Do not set frame.OwnedLine: only editable gaps satisfy
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

            if (frame.OwnedFlat && broken && gapPlan.Conditional && gap.Kind == GapKind::Hard) {
                // Conditional topology belongs to the owner's broken shape;
                // it cannot be suppressed by a child-only alternative.
                return false;
            }

            const bool continuingIndent = frame.RealizedContinuationLine && column == frame.RealizedContinuationIndent;
            bool line = gap.Kind == GapKind::Hard;

            if (gap.Kind == GapKind::Soft && ownerBroken && !frame.OwnedFlat) {
                if (GroupSpecs_[activeGroup].Mode == GroupMode::Consistent) {
                    line = true;
                } else {
                    const auto tail = tailWidth(gapEntry.FitTail);
                    bool segment = gap.FitNextSegment;

                    if (!segment && gap.FitNextSegmentIfOverflow && !probing && !layoutSelected) {
                        const auto indent = resolveIndent(anchor, gap.RequiredWidth);
                        segment = indent >= policy.Width || tail > policy.Width - indent;
                    }

                    line = !fits(AddWidth(gapEntry.ExpandedWidth, segment ? gap.RequiredWidth : tail)) ||
                        (frame.Mandatory == i && (frame.ForceOwned ? !frame.OwnedLine : lines == frame.StartLine));
                }
            }

            if (line) {
                if (probing && activeGroup != InvalidId) {
                    probeBreakPenalty += std::size_t(gap.NewlineCount) * GroupSpecs_[activeGroup].BreakPenalty;
                }

                const bool lf = gap.LineEnding == GapNewline::LF ||
                    (gap.LineEnding == GapNewline::Inherit && policy.LineEnding == Newline::LF);
                append(lf ? "\n" : "\r\n");

                for (std::uint32_t n = 1; n < gap.NewlineCount; ++n) {
                    append(policy.LineEnding == Newline::LF ? "\n" : "\r\n");
                }

                lines += gap.NewlineCount;
                frame.OwnedLine = true;

                if (frame.ExpressionContinuation) {
                    if (!frame.ReversibleIndent) {
                        indentationError(i);
                        return false;
                    }

                    frame.PendingExpressionIndent = 0;
                    frame.RealizedExpressionLine = true;
                }

                if (gap.IndentSourceBegin != InvalidId) {
                    if (mappingRequested && !probing) {
                        recordMapping({
                            outputBegin,
                            static_cast<std::uint32_t>(outputSize()),
                            gapPlan.Provenance(),
                            false,
                            false,
                        });
                    }

                    outputBegin = static_cast<std::uint32_t>(outputSize());
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
                        advance(text);
                        exact = true;
                    }
                } else {
                    emitIndent(resolveIndent(anchor, gap.RequiredWidth));
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
            const bool continuingIndent = frame.RealizedContinuationLine && column == frame.RealizedContinuationIndent;

            if (gapPlan.Mode == Whitespace::Comma) {
                append(text);
            } else {
                appendSource(text);
            }

            advance(text, gapPlan.Mode == Whitespace::Comma || (textFlags[id] & TextPlain));

            if (gapPlan.Mode == Whitespace::Preserve && frame.HasContinuation &&
                ((textFlags[id] & TextLine) || continuingIndent) &&
                text.find_first_not_of(" \t\r\n") == std::string_view::npos)
            {
                realizeContinuation();
            }
        }

        if (gapPlan.RealizeExpression && activeGroup == Gaps_[gapPlan.SourceGap()].Owner) {
            if (!frame.ReversibleIndent) {
                indentationError(i);
                return false;
            }

            frame.PendingExpressionIndent = 0;
            frame.RealizedExpressionLine = true;
            realizeContinuation();
        }

        if (mappingRequested && !probing && gapPlan.Mode != Whitespace::SpellNewlines) {
            recordMapping({
                outputBegin,
                uint32_t(outputSize()),
                emittedOrigin,
                exact,
                gapPlan.Mode == Whitespace::Preserve,
            });
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
        }

        if (!layoutSelected && groupEntry.ChildLayoutAllowed && groupEntry.OwnedLastBreak != NoOrder && !frame.Flat &&
            !entry.Flat.Line && entry.Flat.CanBreak && !fits(AddWidth(entry.Flat.Width(), groupTail(spec, groupEntry))))
        {
            constexpr size_t MaxProbePositions = 256;
            constexpr size_t MaxProbeDepth = 4;
            const bool eligible = Groups_[id].ProbeWork < MaxProbePositions && groupEntry.ProbeEligible;
            const auto levels = groupEntry.ProbeLayouts;
            const uint32_t childLayoutLevels = levels & 14;
            const size_t candidateDepth = (levels & 24) ? MaxProbeDepth : (levels & 4) ? 3 : (levels & 2) ? 2 : 1;
            auto checkpoint = cursor;
            --checkpoint.Begin;

            if (eligible && groupEntry.HasChild) {
                const auto savedFrame = frame;
                const auto savedGroup = activeGroup;
                const auto savedColumn = column;
                const auto savedLines = lines;
                const auto savedLineIndent = lineIndent;
                const auto savedLinePrefix = linePrefix;
                const auto savedDepth = frames.size();
                const auto tail = groupTail(spec, groupEntry);
                childLayoutBase = savedDepth;
                childLayoutEnd = Groups_[id].EndOrder;
                std::size_t bestCost = 0;
                std::size_t bestLines = 0;
                std::size_t bestGroups = 0;
                std::size_t bestDepth = 0;

                const auto withinCost = [&] {
                    return !layoutSelected || lines - savedLines + probeBreakPenalty <= bestCost;
                };

                // Trial zero is the normal parent-breaking layout. The other
                // trials retain successively shallower owned-flat prefixes.
                for (std::size_t trial = 0; trial <= candidateDepth; ++trial) {
                    const auto depth = trial == 0 ? 0 : candidateDepth - trial + 1;

                    // Limits d and d+1 differ only for eligible groups at
                    // level d. Keep the earlier, deeper trial on ties.
                    if (trial > 1 && !(childLayoutLevels & (1u << depth))) {
                        continue;
                    }

                    childLayoutDepth = depth;
                    probeBrokenGroups = 0;
                    probeBreakPenalty = 0;
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
                    const auto candidateLines = lines - savedLines;
                    const auto candidateCost = candidateLines + probeBreakPenalty;

                    if (fitsCandidate &&
                        (!layoutSelected || candidateCost < bestCost ||
                            (candidateCost == bestCost &&
                                (candidateLines < bestLines ||
                                    (candidateLines == bestLines && probeBrokenGroups < bestGroups)))))
                    {
                        layoutSelected = true;
                        bestCost = candidateCost;
                        bestLines = candidateLines;
                        bestGroups = probeBrokenGroups;
                        bestDepth = depth;
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
                    frame = savedFrame;
                    activeGroup = savedGroup;
                    column = savedColumn;
                    lines = savedLines;
                    lineIndent = savedLineIndent;
                    linePrefix = savedLinePrefix;
                    probing = false;
                }

                // Replay even a selected depth-zero layout without nested
                // replanning, so the measured cost describes emitted output.
                childLayoutDepth = layoutSelected ? bestDepth : 0;

            }
        }

        return renderBegin(id);
    };
    const auto leaveSelected = [&] (Order order) {
        if (layoutSelected && order > childLayoutEnd) {
            layoutSelected = false;
            childLayoutDepth = 0;
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
