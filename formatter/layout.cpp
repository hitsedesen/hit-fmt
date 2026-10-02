#include <formatter/layout.hpp>

#include <algorithm>
#include <limits>

namespace formatter::layout {
namespace {

constexpr auto Infinity = UINT32_MAX;

std::uint32_t AddWidth(std::uint32_t a, std::uint32_t b)
{
    return b > Infinity - a ? Infinity : a + b;
}

std::uint32_t Offset(std::uint32_t base, std::int32_t offset)
{
    const auto value = static_cast<std::int64_t>(base) + offset;
    return static_cast<std::uint32_t>(
        std::clamp<std::int64_t>(value, 0, Infinity - 1));
}

struct Measure
{
    std::uint32_t Width = 0;
    std::uint32_t Prefix = 0;
    bool Line = false;
    bool CanBreak = false;
};

Measure Join(Measure a, Measure b)
{
    return {a.Line || b.Line ? Infinity : AddWidth(a.Width, b.Width),
        a.Line ? a.Prefix : AddWidth(a.Prefix, b.Prefix),
        a.Line || b.Line,
        a.CanBreak || b.CanBreak};
}

Measure MeasureText(std::string_view text, std::uint8_t tabWidth)
{
    Measure result;
    for (char c : text) {
        if (c == '\n' || c == '\r') {
            result.Line = true;
            result.CanBreak = true;
            result.Width = Infinity;
        } else if (!result.Line) {
            // Conservative only for literal/source tabs: actual emission uses
            // tab stops. This summary never underestimates flat width.
            result.Width = AddWidth(result.Width, c == '\t' ? tabWidth : 1);
            result.Prefix = result.Width;
        }
    }
    return result;
}

Measure MeasureFlatGap(
    GapSpec gap, std::string_view source, std::uint8_t tabWidth)
{
    if (gap.Kind == GapKind::Hard) {
        return {Infinity, 0, true, true};
    }
    if (gap.FlatSourceBegin != InvalidId) {
        auto measured =
            MeasureText(source.substr(gap.FlatSourceBegin,
                            gap.FlatSourceEnd - gap.FlatSourceBegin),
                tabWidth);
        measured.CanBreak = gap.Kind == GapKind::Soft;
        return measured;
    }
    return {gap.Spaces, gap.Spaces, false, gap.Kind == GapKind::Soft};
}

bool Multiline(std::string_view text)
{
    return text.find_first_of("\r\n") != std::string_view::npos;
}

bool ValidGapNewline(GapNewline value)
{
    return value == GapNewline::Inherit || value == GapNewline::LF ||
           value == GapNewline::CRLF;
}

} // namespace

Result::Result(std::pmr::memory_resource* resource)
    : Text(resource), Map(resource), Broken(resource)
{}

Document::Document(std::string_view source, std::pmr::memory_resource* resource)
    : Source_(source), Nodes_(resource), Groups_(resource), Open_(resource)
{
    if (source.size() >= InvalidId) {
        Fail(Error::TooLarge);
    }
}

Document::~Document() = default;

void Document::Reserve(std::uint32_t nodes, std::uint32_t groups)
{
    if (Sealed_) {
        Fail(Error::Sealed);
    }
    if (nodes == InvalidId || groups == InvalidId) {
        Fail(Error::TooLarge);
    }
    if (State_) {
        Nodes_.reserve(nodes);
        Groups_.reserve(groups);
    }
}

void Document::Fail(Error error)
{
    if (State_) {
        State_ = {error, static_cast<std::uint32_t>(Nodes_.size())};
    }
}

std::uint32_t Document::Add(Node node)
{
    if (Sealed_) {
        Fail(Error::Sealed);
    }
    if (!State_) {
        return InvalidId;
    }
    if (Nodes_.size() >= InvalidId) {
        Fail(Error::TooLarge);
        return InvalidId;
    }
    node.Owner = Open_.empty() ? InvalidId : Open_.back();
    const auto id = static_cast<std::uint32_t>(Nodes_.size());
    Nodes_.push_back(node);
    return id;
}

bool Document::CheckOrigin(Provenance origin)
{
    if ((origin.Begin == InvalidId) != (origin.End == InvalidId) ||
        (origin.Begin != InvalidId &&
            (origin.Begin > origin.End || origin.End > Source_.size())))
    {
        Fail(Error::InvalidSourceRange);
    }
    return static_cast<bool>(State_);
}

bool Document::CheckGapData(GapSpec spec)
{
    if (!ValidGapNewline(spec.LineEnding) || spec.NewlineCount == 0) {
        Fail(Error::InvalidPolicy);
    }
    CheckHorizontalSource(spec.FlatSourceBegin, spec.FlatSourceEnd);
    CheckHorizontalSource(spec.IndentSourceBegin, spec.IndentSourceEnd);
    return static_cast<bool>(State_);
}

bool Document::CheckHorizontalSource(std::uint32_t begin, std::uint32_t end)
{
    if (!CheckOrigin({begin, end})) {
        return false;
    }
    if (begin != InvalidId) {
        for (char c : Source_.substr(begin, end - begin)) {
            if (c != ' ' && c != '\t') {
                Fail(Error::InvalidSourceRange);
                break;
            }
        }
    }
    return static_cast<bool>(State_);
}

bool Document::CheckReference(GroupId reference)
{
    if (reference >= Groups_.size()) {
        Fail(Error::InvalidGroup);
    } else if (Open_.empty() || Open_.back() != reference) {
        Fail(Error::InvalidConditionalDependency);
    }
    return static_cast<bool>(State_);
}

GroupId Document::BeginGroup(GroupSpec spec)
{
    if (spec.Mode != GroupMode::Consistent && spec.Mode != GroupMode::Fill) {
        Fail(Error::InvalidPolicy);
    }
    Node node;
    node.Type = Kind::Begin;
    node.Mode = spec.Mode;
    node.Indent = spec.Indent;
    node.ForceBreak = spec.ForceBreak;
    node.Reference = static_cast<GroupId>(Groups_.size());
    auto index = Add(node);
    if (index == InvalidId) {
        return InvalidId;
    }
    Groups_.push_back(index);
    Open_.push_back(node.Reference);
    Depth_ = std::max(Depth_, static_cast<std::uint32_t>(Open_.size()));
    return node.Reference;
}

void Document::EndGroup(GroupId group)
{
    if (Open_.empty() || Open_.back() != group) {
        Fail(Error::UnbalancedGroup);
        return;
    }
    Node node;
    node.Type = Kind::End;
    node.Reference = group;
    node.Match = Groups_[group];
    auto index = Add(node);
    if (index != InvalidId) {
        Nodes_[node.Match].Match = index;
        Open_.pop_back();
    }
}

void Document::Text(std::string_view generated, Provenance origin)
{
    if (Multiline(generated)) {
        Fail(Error::MultilineText);
    }
    if (!CheckOrigin(origin)) {
        return;
    }
    Node node;
    node.Type = Kind::Text;
    node.FlatText = generated;
    node.Origin = origin;
    Add(node);
}

void Document::SourceText(std::uint32_t begin,
    std::uint32_t end,
    std::uint64_t identity,
    bool verbatim)
{
    // Generated provenance permits paired sentinels, but a source slice must
    // always have real offsets before calling string_view::substr.
    if (begin == InvalidId || end == InvalidId) {
        Fail(Error::InvalidSourceRange);
        return;
    }
    Provenance origin{begin, end, identity};
    if (!CheckOrigin(origin)) {
        return;
    }
    auto text = Source_.substr(begin, end - begin);
    if (!verbatim && Multiline(text)) {
        Fail(Error::MultilineText);
        return;
    }
    Node node;
    node.Type = verbatim ? Kind::Verbatim : Kind::Text;
    node.FlatText = text;
    node.Origin = origin;
    node.Exact = true;
    Add(node);
}

void Document::Verbatim(std::string_view borrowed, Provenance origin)
{
    if (!CheckOrigin(origin)) {
        return;
    }
    Node node;
    node.Type = Kind::Verbatim;
    node.FlatText = borrowed;
    node.Origin = origin;
    node.Exact = origin.Begin != InvalidId;
    if (node.Exact &&
        borrowed != Source_.substr(origin.Begin, origin.End - origin.Begin))
    {
        Fail(Error::InvalidSourceRange);
        return;
    }
    Add(node);
}

void Document::Gap(GapSpec spec, AnchorId anchor, Provenance origin)
{
    if (spec.Kind != GapKind::Flat && spec.Kind != GapKind::Soft &&
        spec.Kind != GapKind::Hard)
    {
        Fail(Error::InvalidPolicy);
    }
    CheckGapData(spec);
    if (anchor != InvalidId && anchor >= Anchors_) {
        Fail(Error::InvalidAnchor);
    }
    if (!CheckOrigin(origin)) {
        return;
    }
    Node node;
    node.Type = Kind::Gap;
    node.FlatGap = node.BrokenGap = spec;
    node.Anchor = anchor;
    node.Origin = origin;
    Add(node);
}

void Document::SourceLineIndent(std::uint32_t begin,
    std::uint32_t end,
    std::uint32_t indentBegin,
    std::uint32_t indentEnd,
    AnchorId anchor,
    std::uint32_t requiredWidth,
    std::uint64_t identity)
{
    if (begin == InvalidId || end == InvalidId || indentBegin == InvalidId ||
        indentEnd == InvalidId)
    {
        Fail(Error::InvalidSourceRange);
        return;
    }
    if (!CheckOrigin({begin, end, identity}) ||
        !CheckHorizontalSource(indentBegin, indentEnd))
    {
        return;
    }
    const auto prefix = Source_.substr(begin, end - begin);
    if (prefix.empty() || prefix.back() != '\n' ||
        prefix.find_first_not_of(" \t\r\n") != std::string_view::npos)
    {
        Fail(Error::InvalidLinePrefix);
    }
    if (anchor != InvalidId && anchor >= Anchors_) {
        Fail(Error::InvalidAnchor);
    }
    Node node;
    node.Type = Kind::SourceLineIndent;
    node.FlatText = prefix;
    node.Origin = {begin, end, identity};
    node.Anchor = anchor;
    node.FlatGap.RequiredWidth = requiredWidth;
    node.FlatGap.IndentSourceBegin = indentBegin;
    node.FlatGap.IndentSourceEnd = indentEnd;
    Add(node);
}

AnchorId Document::MarkAnchor(AnchorSpec spec)
{
    if (spec.Kind != AnchorKind::BlockRelative &&
        spec.Kind != AnchorKind::DelimiterColumn)
    {
        Fail(Error::InvalidPolicy);
    }
    Node node;
    node.Type = Kind::Anchor;
    node.Anchor = Anchors_;
    node.Alignment = spec.Kind;
    node.Indent = spec.Offset;
    node.Fallback = spec.Fallback;
    node.Minimum = spec.Minimum;
    if (Add(node) == InvalidId) {
        return InvalidId;
    }
    return Anchors_++;
}

void Document::ConditionalText(GroupId reference,
    std::string_view flat,
    std::string_view broken,
    Provenance origin,
    std::uint32_t fitWidthFloor)
{
    if (Multiline(flat) || Multiline(broken)) {
        Fail(Error::MultilineText);
    }
    if (!CheckReference(reference) || !CheckOrigin(origin)) {
        return;
    }
    Node node;
    node.Type = Kind::ConditionalText;
    node.Reference = reference;
    node.FlatText = flat;
    node.BrokenText = broken;
    node.Origin = origin;
    node.FitWidthFloor = fitWidthFloor;
    Add(node);
}

void Document::ConditionalGap(GroupId reference,
    GapSpec flat,
    GapSpec broken,
    AnchorId anchor,
    Provenance origin)
{
    if (flat.Kind != GapKind::Flat) {
        Fail(Error::InvalidConditionalGap);
    }
    if (broken.Kind != GapKind::Flat && broken.Kind != GapKind::Soft &&
        broken.Kind != GapKind::Hard)
    {
        Fail(Error::InvalidPolicy);
    }
    CheckGapData(flat);
    CheckGapData(broken);
    if (anchor != InvalidId && anchor >= Anchors_) {
        Fail(Error::InvalidAnchor);
    }
    if (!CheckReference(reference) || !CheckOrigin(origin)) {
        return;
    }
    Node node;
    node.Type = Kind::ConditionalGap;
    node.Reference = reference;
    node.FlatGap = flat;
    node.BrokenGap = broken;
    node.Anchor = anchor;
    node.Origin = origin;
    Add(node);
}

Status Document::Seal()
{
    if (!Open_.empty()) {
        Fail(Error::UnbalancedGroup);
    }
    Sealed_ = true;
    return State_;
}

Result Document::Render(
    Policy policy, std::pmr::memory_resource* resource) const
{
    Result result(resource);
    if (!State_) {
        result.State = State_;
        return result;
    }
    if (!Sealed_) {
        result.State = {Error::Unsealed, InvalidId};
        return result;
    }
    if (policy.Width == 0 || policy.Width == Infinity || policy.TabWidth == 0 ||
        policy.TabWidth > 32 ||
        (policy.LineEnding != Newline::LF &&
            policy.LineEnding != Newline::CRLF) ||
        (policy.Indentation != IndentationKind::Spaces &&
            policy.Indentation != IndentationKind::Tabs))
    {
        result.State = {Error::InvalidPolicy, InvalidId};
        return result;
    }

    // Each node is visited once bottom-up and once for emission. The suffix
    // prefix stops at the next guaranteed/available boundary in broken mode;
    // flat mode includes every closer and same-line continuation.
    struct Cache
    {
        Measure Flat;
        Measure FlatTail;
        Measure BrokenTail;
        std::uint32_t LastBreak = InvalidId;
        std::uint32_t ExpandedGapWidth = 0;
        std::uint32_t OwnedLastBreak = InvalidId;
        bool GuaranteedLine = false;
    };
    std::pmr::vector<Cache> cache(Nodes_.size(), resource);
    struct SummaryFrame
    {
        Measure Flat;
        Measure Broken;
        std::uint32_t LastBreak = InvalidId;
        std::uint32_t OwnedLastBreak = InvalidId;
        bool GuaranteedLine = false;
    };
    std::pmr::vector<SummaryFrame> summaries(resource);
    summaries.reserve(Depth_);
    SummaryFrame summary;
    std::size_t mappingCount = 0;
    for (std::size_t position = Nodes_.size(); position != 0;) {
        const auto i = --position;
        const auto& node = Nodes_[i];
        auto& entry = cache[i];
        if (node.Type != Kind::Begin && node.Type != Kind::End &&
            node.Type != Kind::Anchor)
        {
            ++mappingCount;
            if (node.Type == Kind::SourceLineIndent ||
                ((node.Type == Kind::Gap || node.Type == Kind::
                    ConditionalGap) &&
                    (node.FlatGap.IndentSourceBegin != InvalidId ||
                        node.BrokenGap.IndentSourceBegin != InvalidId)))
            {
                ++mappingCount;
            }
        }
        if (node.Type == Kind::End) {
            summaries.push_back(summary);
            summary = {};
            continue;
        }
        Measure flat;
        Measure broken;
        std::uint32_t lastBreak = InvalidId;
        std::uint32_t ownedBreak = InvalidId;
        bool guaranteedLine = false;
        if (node.Type == Kind::Begin) {
            flat = summary.Flat;
            entry.LastBreak = summary.LastBreak;
            entry.OwnedLastBreak = summary.OwnedLastBreak;
            entry.GuaranteedLine = summary.GuaranteedLine;
            lastBreak = entry.LastBreak;
            // A child can stay flat in its parent's broken shape. Only its
            // unconditional lines are guaranteed without a child decision.
            guaranteedLine = flat.Line;
            // A conditional broken hard gap is a possible structural break
            // even when its flat alternative has no ordinary soft gap.
            flat.CanBreak = flat.CanBreak || summary.Broken.CanBreak;
            // An unconditional child line makes its broken alternatives
            // inevitable. Fold that prefix into enclosing fit obligations;
            // a potentially flat child still contributes its flat summary.
            const bool forced =
                node.ForceBreak && entry.OwnedLastBreak != InvalidId;
            broken = flat.Line || forced ? summary.Broken : flat;
            if (forced) {
                flat = broken;
                flat.Width = Infinity;
                flat.Line = true;
                guaranteedLine = true;
            }
            summary = summaries.back();
            summaries.pop_back();
        } else if (node.Type == Kind::Text || node.Type == Kind::Verbatim ||
                   node.Type == Kind::ConditionalText ||
                   node.Type == Kind::SourceLineIndent)
        {
            // Fixed prefixes establish shape like Verbatim, but never publish
            // lastBreak/ownedBreak. Their indentation follows a guaranteed
            // line, and emission counts it before any following child fit.
            flat = MeasureText(node.FlatText, policy.TabWidth);
            if (node.Type == Kind::ConditionalText) {
                flat.Width = flat.Prefix =
                    std::max(flat.Width, node.FitWidthFloor);
            }
            broken = node.Type == Kind::ConditionalText
                         ? MeasureText(node.BrokenText, policy.TabWidth)
                         : flat;
            guaranteedLine = flat.Line;
        } else if (node.Type == Kind::Gap || node.Type == Kind::ConditionalGap)
        {
            flat = MeasureFlatGap(node.FlatGap, Source_, policy.TabWidth);
            const auto expandedFlat =
                node.Type == Kind::Gap
                    ? flat
                    : MeasureFlatGap(node.BrokenGap, Source_, policy.TabWidth);
            // Reuse each flat whitespace measurement for fill and summaries.
            broken =
                node.Owner != InvalidId && node.BrokenGap.Kind == GapKind::Soft
                    ? Measure{Infinity, 0, true, true}
                    : expandedFlat;
            entry.ExpandedGapWidth = expandedFlat.Width;
            if (node.Owner != InvalidId && node.BrokenGap.Kind != GapKind::Flat)
            {
                ownedBreak = static_cast<std::uint32_t>(i);
            }
            guaranteedLine = node.BrokenGap.Kind == GapKind::Hard;
            if (node.Owner != InvalidId &&
                (node.BrokenGap.Kind == GapKind::Soft ||
                    (node.Type == Kind::ConditionalGap && guaranteedLine)))
            {
                lastBreak = static_cast<std::uint32_t>(i);
            }
        }
        entry.Flat = flat;
        entry.FlatTail = summary.Flat;
        entry.BrokenTail = summary.Broken;
        summary.Flat = Join(flat, summary.Flat);
        summary.Broken = Join(broken, summary.Broken);
        if (summary.LastBreak == InvalidId) {
            summary.LastBreak = lastBreak;
        }
        if (summary.OwnedLastBreak == InvalidId) {
            summary.OwnedLastBreak = ownedBreak;
        }
        summary.GuaranteedLine = summary.GuaranteedLine || guaranteedLine;
    }

    result.Broken.resize(Groups_.size(), 0);
    if (policy.SourceMap) {
        result.Map.reserve(mappingCount);
    }
    struct CapturedAnchor
    {
        std::uint32_t Column = 0;
        std::uint32_t Block = 0;
        std::uint32_t Node = InvalidId;
    };
    std::pmr::vector<CapturedAnchor> anchors(Anchors_, resource);
    struct Frame
    {
        std::uint32_t Indent = 0;
        std::uint32_t Tail = 0;
        std::uint32_t Mandatory = InvalidId;
        std::uint64_t StartLine = 0;
        bool Flat = false;
        bool ForceOwned = false;
        bool OwnedLine = false;
    };
    std::pmr::vector<Frame> frames(resource);
    frames.reserve(Depth_);
    Frame frame;
    std::uint32_t column = 0;
    std::uint64_t lines = 0;
    auto advance = [&](std::string_view text) {
        for (char c : text) {
            if (c == '\r' || c == '\n') {
                column = 0;
                ++lines;
            } else if (c == '\t') {
                column = AddWidth(
                    column, policy.TabWidth - column % policy.TabWidth);
            } else {
                column = AddWidth(column, 1);
            }
        }
    };
    auto fits = [&](std::uint32_t width) {
        return width != Infinity && column <= policy.Width &&
               width <= policy.Width - column;
    };
    auto tailWidth = [&](Measure tail) {
        return tail.Line ? tail.Prefix : AddWidth(tail.Prefix, frame.Tail);
    };
    auto resolveIndent = [&](AnchorId id, std::uint32_t requiredWidth) {
        auto indent = frame.Indent;
        if (id != InvalidId) {
            const auto& anchor = anchors[id];
            const auto& spec = Nodes_[anchor.Node];
            indent = anchor.Column;
            if (spec.Alignment == AnchorKind::DelimiterColumn) {
                const auto minimum = std::min(
                    AddWidth(anchor.Block, spec.Minimum), Infinity - 1);
                indent = std::max(indent, minimum);
                if (indent >= policy.Width ||
                    requiredWidth > policy.Width - indent)
                {
                    indent = Offset(anchor.Block, spec.Fallback);
                }
            }
        }
        return indent;
    };
    auto emitIndent = [&](std::uint32_t indent) {
        if (policy.Indentation == IndentationKind::Tabs) {
            result.Text.append(indent / policy.TabWidth, '\t');
            result.Text.append(indent % policy.TabWidth, ' ');
        } else {
            result.Text.append(indent, ' ');
        }
        column = indent;
    };
    for (std::size_t i = 0; i < Nodes_.size(); ++i) {
        const auto& node = Nodes_[i];
        const auto& entry = cache[i];
        if (node.Type == Kind::Begin) {
            const auto tail =
                tailWidth(frame.Flat ? entry.FlatTail : entry.BrokenTail);
            const bool mandatory =
                frame.Mandatory != InvalidId && frame.Mandatory >= i &&
                frame.Mandatory <= node.Match && lines == frame.StartLine;
            const bool broken =
                mandatory || (!frame.Flat && entry.Flat.CanBreak &&
                                 !fits(AddWidth(entry.Flat.Width, tail)));
            result.Broken[node.Reference] = broken;
            frames.push_back(frame);
            const bool forceOwned =
                node.ForceBreak && entry.OwnedLastBreak != InvalidId;
            frame = {Offset(frame.Indent, node.Indent),
                tail,
                forceOwned
                    ? entry.OwnedLastBreak
                    : broken && !entry.GuaranteedLine
                        ? entry.LastBreak
                        : InvalidId,
                lines,
                !broken,
                forceOwned,
                false};
            continue;
        }
        if (node.Type == Kind::End) {
            frame = frames.back();
            frames.pop_back();
            continue;
        }
        if (node.Type == Kind::Anchor) {
            const auto target = Offset(
                node.Alignment == AnchorKind::BlockRelative
                    ? frame.Indent
                    : column,
                node.Indent);
            anchors[node.Anchor] = {
                target, frame.Indent, static_cast<std::uint32_t>(i)};
            continue;
        }
        auto outputBegin = result.Text.size();
        auto emittedOrigin = node.Origin;
        bool exact = node.Exact;
        const bool ownerBroken =
            node.Owner != InvalidId && result.Broken[node.Owner];
        if (node.Type == Kind::Text || node.Type == Kind::Verbatim ||
            node.Type == Kind::ConditionalText)
        {
            auto text = node.Type == Kind::ConditionalText && ownerBroken
                            ? node.BrokenText
                            : node.FlatText;
            result.Text.append(text);
            advance(text);
        } else if (node.Type == Kind::SourceLineIndent) {
            result.Text.append(node.FlatText);
            advance(node.FlatText);
            if (policy.SourceMap) {
                result.Map.push_back({outputBegin,
                    result.Text.size(),
                    node.Origin,
                    true,
                    true});
            }
            outputBegin = result.Text.size();
            emittedOrigin = {node.FlatGap.IndentSourceBegin,
                node.FlatGap.IndentSourceEnd,
                NoIdentity};
            emitIndent(resolveIndent(node.Anchor, node.FlatGap.RequiredWidth));
            // Do not set frame.OwnedLine: only editable gaps satisfy
            // ForceOwned.
        } else {
            auto gap = node.Type == Kind::ConditionalGap && ownerBroken
                           ? node.BrokenGap
                           : node.FlatGap;
            bool line = gap.Kind == GapKind::Hard;
            if (gap.Kind == GapKind::Soft && ownerBroken) {
                if (Nodes_[Groups_[node.Owner]].Mode == GroupMode::Consistent) {
                    line = true;
                } else {
                    line = !fits(AddWidth(entry.ExpandedGapWidth,
                               tailWidth(entry.BrokenTail))) ||
                           (frame.Mandatory == i &&
                               (frame.ForceOwned
                                   ? !frame.OwnedLine
                                   : lines == frame.StartLine));
                }
            }
            if (line) {
                const bool lf = gap.LineEnding == GapNewline::LF ||
                                (gap.LineEnding == GapNewline::Inherit &&
                                    policy.LineEnding == Newline::LF);
                result.Text.append(lf ? "\n" : "\r\n");
                for (std::uint32_t n = 1; n < gap.NewlineCount; ++n) {
                    result.Text.append(
                        policy.LineEnding == Newline::LF ? "\n" : "\r\n");
                }
                lines += gap.NewlineCount;
                frame.OwnedLine = true;
                if (gap.IndentSourceBegin != InvalidId) {
                    if (policy.SourceMap) {
                        result.Map.push_back({outputBegin,
                            result.Text.size(),
                            node.Origin,
                            false,
                            false});
                    }
                    outputBegin = result.Text.size();
                    auto text = Source_.substr(gap.IndentSourceBegin,
                        gap.IndentSourceEnd - gap.IndentSourceBegin);
                    result.Text.append(text);
                    column = 0;
                    advance(text);
                    emittedOrigin.Begin = gap.IndentSourceBegin;
                    emittedOrigin.End = gap.IndentSourceEnd;
                    exact = true;
                } else {
                    emitIndent(resolveIndent(node.Anchor, gap.RequiredWidth));
                }
            } else if (gap.FlatSourceBegin != InvalidId) {
                auto text = Source_.substr(gap.FlatSourceBegin,
                    gap.FlatSourceEnd - gap.FlatSourceBegin);
                result.Text.append(text);
                advance(text);
                emittedOrigin.Begin = gap.FlatSourceBegin;
                emittedOrigin.End = gap.FlatSourceEnd;
                exact = true;
            } else {
                result.Text.append(gap.Spaces, ' ');
                column = AddWidth(column, gap.Spaces);
            }
        }
        if (policy.SourceMap) {
            result.Map.push_back({outputBegin,
                result.Text.size(),
                emittedOrigin,
                exact,
                node.Type == Kind::Verbatim});
        }
    }
    return result;
}

} // namespace formatter::layout
