#include <document/document.hpp>

#include <algorithm>
#include <limits>

namespace document {

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
    return {
        a.Line || b.Line ? Infinity : AddWidth(a.Width, b.Width),
        a.Line ? a.Prefix : AddWidth(a.Prefix, b.Prefix),
        a.Line || b.Line,
        a.CanBreak || b.CanBreak,
    };
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
    GapSpec gap,
    std::string_view source,
    std::uint8_t tabWidth)
{
    if (gap.Kind == GapKind::Hard) {
        return {Infinity, 0, true, true};
    }
    if (gap.FlatSourceBegin != InvalidId) {
        auto measured = MeasureText(
            source.substr(
                gap.FlatSourceBegin,
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

// LF offsets identify complete authorized atoms. Contracting CRLF must not
// pair its original bare-CR predecessor with the surviving LF.
bool ChangesWhitespaceAtom(
    std::string_view source,
    std::size_t lf,
    bool targetCRLF)
{
    const bool crlf = lf && source[lf - 1] == '\r';
    return crlf != targetCRLF && (!crlf || lf < 2 || source[lf - 2] != '\r');
}

bool ValidGapNewline(GapNewline value)
{
    return value == GapNewline::Inherit || value == GapNewline::LF ||
        value == GapNewline::CRLF;
}

} // namespace

Result::Result(std::pmr::memory_resource* resource)
    : Text(resource)
    , Map(resource)
    , Broken(resource)
{ }

Document::Document(std::string_view source, std::pmr::memory_resource* resource)
    : Source_(source)
    , Nodes_(resource)
    , Groups_(resource)
    , Open_(resource)
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
    if (spec.ExpressionContinuation && (spec.Indent < 0 || spec.StructuralBase))
    {
        Fail(Error::InvalidPolicy);
    }
    Node node;
    node.Type = Kind::Begin;
    node.Mode = spec.Mode;
    node.Indent = spec.Indent;
    node.ForceBreak = spec.ForceBreak;
    node.ExpressionContinuation = spec.ExpressionContinuation;
    node.StructuralBase = spec.StructuralBase;
    node.Continuation = spec.Continuation;
    node.LineBase = spec.LineBase;
    if (spec.LineBase && (!spec.StructuralBase || spec.Continuation)) {
        Fail(Error::InvalidPolicy);
    }
    node.ChildBreaks = spec.ChildBreaks;
    if (spec.BreakPenalty > 3 || spec.FlatInBrokenParentPenalty > 3) {
        Fail(Error::InvalidPolicy);
    }
    node.BreakPenalty = spec.BreakPenalty;
    node.FlatInBrokenParentPenalty = spec.FlatInBrokenParentPenalty;
    if (spec.Continuation && spec.Indent < 0) {
        Fail(Error::InvalidPolicy);
    }
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

void Document::SourceText(
    std::uint32_t begin,
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

void Document::SourceWhitespace(
    std::uint32_t begin,
    std::uint32_t end,
    Newline ending)
{
    if (ending != Newline::LF && ending != Newline::CRLF) {
        Fail(Error::InvalidPolicy);
        return;
    }
    if (begin == InvalidId || end == InvalidId ||
        !CheckOrigin({begin, end, NoIdentity}))
    {
        Fail(Error::InvalidSourceRange);
        return;
    }
    const auto text = Source_.substr(begin, end - begin);
    if (text.find_first_not_of(" \t\r\n\f\v") != std::string_view::npos ||
        (begin && begin < end && Source_[begin - 1] == '\r' &&
            Source_[begin] == '\n') ||
        (begin < end && end < Source_.size() && Source_[end - 1] == '\r' &&
            Source_[end] == '\n'))
    {
        Fail(Error::InvalidSourceRange);
        return;
    }
    std::uint32_t changed = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\n') {
            changed += ChangesWhitespaceAtom(
                Source_,
                begin + i,
                ending == Newline::CRLF);
        }
    }
    if (text.empty()) {
        SourceText(begin, end, NoIdentity, true);
        return;
    }
    Node node;
    node.Type = Kind::SourceWhitespace;
    node.FlatText = text;
    node.Origin = {begin, end, NoIdentity};
    node.FlatGap.LineEnding =
        ending == Newline::LF ? GapNewline::LF : GapNewline::CRLF;
    node.ChangedNewlines = changed;
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

void Document::SourceLineIndent(
    std::uint32_t begin,
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
    node.Reference = spec.SharedWith;
    if (spec.SharedWith != InvalidId && spec.SharedWith >= Anchors_) {
        Fail(Error::InvalidAnchor);
    }
    node.Alignment = spec.Kind;
    node.Indent = spec.Offset;
    node.Fallback = spec.Fallback;
    node.Minimum = spec.Minimum;
    node.StructuralBase = spec.StructuralBase;
    node.Continuation = spec.Continuation;
    node.LineBase = spec.LineBase;
    if (spec.LineBase && (!spec.StructuralBase || spec.Continuation)) {
        Fail(Error::InvalidPolicy);
    }
    if (spec.Continuation && spec.Offset < 0) {
        Fail(Error::InvalidPolicy);
    }
    if (Add(node) == InvalidId) {
        return InvalidId;
    }
    return Anchors_++;
}

void Document::ExpressionLine()
{
    if (Open_.empty() || !Nodes_[Groups_[Open_.back()]].ExpressionContinuation)
    {
        Fail(Error::InvalidGroup);
        return;
    }
    if (Nodes_.empty()) {
        Fail(Error::InvalidLinePrefix);
        return;
    }
    const auto& previous = Nodes_.back();
    if ((previous.Type != Kind::Verbatim &&
        previous.Type != Kind::SourceWhitespace &&
        previous.Type != Kind::SourceLineIndent) ||
        previous.Owner != Open_.back() ||
        previous.Origin.Identity != NoIdentity ||
        previous.Origin.Begin == InvalidId ||
        previous.FlatText != Source_.substr(
            previous.Origin.Begin,
            previous.Origin.End - previous.Origin.Begin) ||
        previous.FlatText.find('\n') == std::string_view::npos ||
        previous.FlatText.find_first_not_of(" \t\r\n") !=
            std::string_view::npos)
    {
        Fail(Error::InvalidLinePrefix);
        return;
    }
    Node node;
    node.Type = Kind::ExpressionLine;
    Add(node);
}

void Document::ConditionalText(
    GroupId reference,
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

void Document::ConditionalGap(
    GroupId reference,
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

Result Document::Render(Policy policy, std::pmr::memory_resource* resource)
const
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

    // Summaries are built once bottom-up. Emission may additionally probe
    // small groups with a fixed work budget. The suffix
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
        bool ChildLayoutAllowed = false;
        bool ShapeSensitive = false;
    };
    std::pmr::vector<Cache> cache(Nodes_.size(), resource);
    struct SummaryFrame
    {
        Measure Flat;
        Measure Broken;
        std::uint32_t LastBreak = InvalidId;
        std::uint32_t OwnedLastBreak = InvalidId;
        bool GuaranteedLine = false;
        bool OwnedConditionalLine = false;
        bool OwnedShapeChoice = false;
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
            node.Type != Kind::Anchor && node.Type != Kind::ExpressionLine)
        {
            ++mappingCount;
            if (node.Type == Kind::SourceWhitespace) {
                // At most one copied run on either side of each changed atom.
                if (mappingCount > result.Map.max_size() ||
                    node.ChangedNewlines >
                        (result.Map.max_size() - mappingCount) / 2)
                {
                    result.State =
                        {Error::TooLarge, static_cast<std::uint32_t>(i)};
                    return result;
                }
                mappingCount += std::size_t(node.ChangedNewlines) * 2;
            }
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
            entry.ShapeSensitive = summary.OwnedShapeChoice;
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
            // Decide eligibility per owner, not for its entire subtree. An
            // outer list can require owned lines while an inner chain keeps
            // its links flat and lets a deeper argument list break.
            entry.ChildLayoutAllowed = node.Mode == GroupMode::Fill &&
                node.ChildBreaks && !forced && !summary.OwnedConditionalLine;
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
            node.Type == Kind::SourceLineIndent ||
            node.Type == Kind::SourceWhitespace)
        {
            // Fixed prefixes establish shape like Verbatim, but never publish
            // lastBreak/ownedBreak. Their indentation follows a guaranteed
            // line, and emission counts it before any following child fit.
            flat = MeasureText(node.FlatText, policy.TabWidth);
            if (node.Type == Kind::ConditionalText) {
                summary.OwnedShapeChoice = summary.OwnedShapeChoice ||
                    node.FlatText != node.BrokenText;
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
            const auto expandedFlat = node.Type == Kind::Gap
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
            if (node.Type == Kind::ConditionalGap) {
                summary.OwnedConditionalLine =
                    summary.OwnedConditionalLine || guaranteedLine;
                summary.OwnedShapeChoice = summary.OwnedShapeChoice ||
                    node.FlatGap.Spaces != node.BrokenGap.Spaces ||
                    node.FlatGap.FlatSourceBegin !=
                        node.BrokenGap.FlatSourceBegin ||
                    node.FlatGap.FlatSourceEnd != node.BrokenGap.FlatSourceEnd;
            }
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
        std::uint32_t SharedOffset = InvalidId;
    };
    std::pmr::vector<CapturedAnchor> anchors(Anchors_, resource);
    struct Frame
    {
        std::uint32_t Indent = 0;
        std::uint32_t Tail = 0;
        std::uint32_t Mandatory = InvalidId;
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
    auto structuralBase = [&] (const Node& node) {
        if (node.LineBase) {
            return lineIndent;
        }
        if (frame.HasContinuation) {
            if (frame.RealizedContinuationLine) {
                return frame.RealizedContinuationIndent;
            }
            if (!node.Continuation) {
                return frame.ContinuationBase;
            }
        }
        return frame.Indent - frame.PendingExpressionIndent;
    };
    auto sharesRealizedContinuation = [&] (const Node& node) {
        return !node.StructuralBase && frame.StructuralBase &&
            frame.RealizedContinuationLine;
    };
    auto sharesContinuation = [&] (const Node& node) {
        // A structural consumer already indented its contents. Contributors
        // reuse that realized level; another contributor starting on a later
        // line still creates a level, preserving expression precedence.
        return node.Continuation && frame.HasContinuation &&
            ((lines == frame.ContinuationStartLine &&
                !(node.StructuralBase && frame.RealizedContinuationLine)) ||
                sharesRealizedContinuation(node));
    };
    auto advance = [&] (std::string_view text) {
        for (char c : text) {
            if (c == '\r' || c == '\n') {
                column = 0;
                lineIndent = 0;
                linePrefix = true;
                ++lines;
            } else if (c == '\t') {
                column = AddWidth(
                    column,
                    policy.TabWidth - column % policy.TabWidth);
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
        return width != Infinity && column <= policy.Width &&
            width <= policy.Width - column;
    };
    auto tailWidth = [&] (Measure tail) {
        return tail.Line ? tail.Prefix : AddWidth(tail.Prefix, frame.Tail);
    };
    auto groupTail = [&] (const Cache& entry) {
        // Child-only candidates retain the owner's flat suffix demand.
        // Conditional broken text is checked by the complete probe, so an
        // underestimated suffix can reject a candidate but never accept an
        // overflowing one.
        return tailWidth(frame.OwnedFlat ? entry.FlatTail : entry.BrokenTail);
    };
    auto resolveIndent = [&] (AnchorId id, std::uint32_t requiredWidth) {
        auto indent = frame.Indent;
        if (id != InvalidId) {
            const auto& anchor = anchors[id];
            const auto& spec = Nodes_[anchor.Node];
            indent = anchor.Column;
            if (spec.Alignment == AnchorKind::DelimiterColumn) {
                const auto minimum = std::min(
                    AddWidth(
                        anchor.Block,
                        anchor.SharedOffset != InvalidId && spec.Minimum
                            ? anchor.SharedOffset
                            : spec.Minimum),
                    Infinity - 1);
                indent = std::max(indent, minimum);
                if (indent >= policy.Width ||
                    requiredWidth > policy.Width - indent)
                {
                    indent = Offset(
                        anchor.Block,
                        anchor.SharedOffset != InvalidId &&
                            spec.Fallback == int32_t(spec.Minimum)
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
    auto append = [&] (std::string_view text) {
        if (!probing) {
            result.Text.append(text);
        }
    };
    auto emitIndent = [&] (std::uint32_t indent) {
        lineIndent = indent;
        linePrefix = true;
        if (probing) {
            column = indent;
            return;
        }
        if (policy.Indentation == IndentationKind::Tabs) {
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
        result.State =
            {Error::IndentationOverflow, static_cast<std::uint32_t>(i)};
        result.Text.clear();
        result.Map.clear();
        result.Broken.clear();
    };
    // Probes and emission execute the same state transitions. A successful
    // probe is replayed with its retained depth limit, without nested probes.
    // Consistent groups retain their coupled broken topology; only Fill
    // groups may keep their own gaps flat around multiline descendants.
    const auto renderNode = [&] (std::size_t i) -> bool {
        const auto& node = Nodes_[i];
        const auto& entry = cache[i];
        if (node.Type == Kind::Begin) {
            const auto tail = groupTail(entry);
            const bool mandatory =
                frame.Mandatory != InvalidId && frame.Mandatory >= i &&
                    frame.Mandatory <= node.Match && lines == frame.StartLine;
            const bool broken = mandatory ||
                (!frame.Flat && entry.Flat.CanBreak &&
                    !fits(AddWidth(entry.Flat.Width, tail)));
            const bool keepOwnedFlat = broken && childLayoutDepth &&
                entry.ChildLayoutAllowed && i <= childLayoutEnd &&
                frames.size() - childLayoutBase < childLayoutDepth;
            result.Broken[node.Reference] = broken;
            frames.push_back(frame);
            const bool forceOwned =
                node.ForceBreak && entry.OwnedLastBreak != InvalidId;
            auto base = frame.Indent;
            auto pending = frame.PendingExpressionIndent;
            auto reversible = frame.ReversibleIndent;
            if (node.StructuralBase) {
                if (!reversible || pending > base) {
                    indentationError(i);
                    return false;
                }
                base = structuralBase(node);
                pending = 0;
                reversible = true;
            }
            auto indent = node.Indent;
            const bool shared = sharesContinuation(node);
            const bool newLineBase = node.Continuation && !shared &&
                frame.HasContinuation && lines != frame.ContinuationStartLine;
            if (newLineBase) {
                // A new starter uses this physical line, not an unrealized
                // ancestor offset or a previous child's continuation line.
                base = lineIndent;
                pending = 0;
            }
            if (shared) {
                const bool realized = sharesRealizedContinuation(node);
                const auto continuationIndent = realized
                    ? frame.RealizedContinuationIndent
                    : frame.Indent;
                base = realized
                    ? std::min(frame.ContinuationBase, continuationIndent)
                    : frame.ContinuationBase;
                if (continuationIndent < base ||
                    continuationIndent - base > uint32_t(INT32_MAX))
                {
                    indentationError(i);
                    return false;
                }
                indent = int32_t(continuationIndent - base);
            }
            const auto logical = std::int64_t(base) + indent;
            if (node.ExpressionContinuation) {
                const auto bias = std::uint64_t(pending) +
                    static_cast<std::uint32_t>(node.Indent);
                if (!reversible || logical >= Infinity || bias >= Infinity) {
                    indentationError(i);
                    return false;
                }
                if (node.Continuation) {
                    const auto expressionBase = newLineBase
                        ? base
                        : frame.Indent - frame.PendingExpressionIndent;
                    pending = logical > expressionBase
                        ? static_cast<std::uint32_t>(logical - expressionBase)
                        : 0;
                } else {
                    pending = static_cast<std::uint32_t>(bias);
                }
            }
            if (pending != 0 && (logical < 0 || logical >= Infinity)) {
                reversible = false;
            }
            frame =
                {
                    Offset(base, indent),
                    tail,
                    forceOwned
                        ? entry.OwnedLastBreak
                        : broken && !keepOwnedFlat && !entry.GuaranteedLine
                            ? entry.LastBreak
                            : InvalidId,
                    lines,
                    !broken,
                    !broken || keepOwnedFlat,
                    forceOwned,
                    false,
                    pending,
                    node.ExpressionContinuation,
                    node.StructuralBase,
                    false,
                    reversible,
                    node.Continuation ? base : frame.ContinuationBase,
                    node.Continuation ||
                        (!node.StructuralBase && frame.HasContinuation),
                    (shared || (!node.Continuation && !node.StructuralBase)) &&
                        frame.RealizedContinuationLine,
                    frame.RealizedContinuationIndent,
                    node.Continuation && !shared
                        ? lines
                        : frame.ContinuationStartLine,
                };
            return true;
        }
        if (node.Type == Kind::End) {
            // Shape consumers still observe multiline descendants, even when
            // this group keeps every owned gap flat.
            const bool multiline = lines != frame.StartLine;
            if (probing && cache[node.Match].ShapeSensitive &&
                bool(result.Broken[node.Reference]) != multiline)
            {
                // A later sibling may break without making this owner
                // multiline. Do not accept conditional text or spacing
                // chosen from that sibling's prospective shape.
                return false;
            }
            result.Broken[node.Reference] = multiline;
            if (probing) {
                if (multiline) {
                    ++probeBrokenGroups;
                    probeBreakPenalty += frame.FlatChildPenalty;
                } else if (const auto penalty =
                    Nodes_[node.Match].FlatInBrokenParentPenalty;
                    penalty != 0)
                {
                    // Defer this cost until the direct parent's shape is
                    // known; a wholly flat parent has no shape deviation.
                    frames.back().FlatChildPenalty += penalty;
                }
            }
            const bool realized =
                frame.RealizedExpressionLine && !frame.StructuralBase;
            const bool continuationRealized =
                frame.RealizedContinuationLine && !frame.StructuralBase;
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
            return true;
        }
        if (node.Type == Kind::Anchor) {
            const bool shared = sharesContinuation(node);
            auto base = frame.Indent;
            if (node.StructuralBase) {
                if (!frame.ReversibleIndent ||
                    frame.PendingExpressionIndent > base)
                {
                    indentationError(i);
                    return false;
                }
                base = structuralBase(node);
            }
            auto offset = node.Indent;
            auto sharedOffset = InvalidId;
            if (node.Continuation && !shared && frame.HasContinuation &&
                lines != frame.ContinuationStartLine)
            {
                base = lineIndent;
            }
            if (shared) {
                const bool realized = sharesRealizedContinuation(node);
                const auto continuationIndent = realized
                    ? frame.RealizedContinuationIndent
                    : frame.Indent;
                base = realized
                    ? std::min(frame.ContinuationBase, continuationIndent)
                    : frame.ContinuationBase;
                if (continuationIndent < base ||
                    continuationIndent - base > uint32_t(INT32_MAX))
                {
                    indentationError(i);
                    return false;
                }
                sharedOffset = continuationIndent - base;
                if (offset != 0) {
                    offset = int32_t(sharedOffset);
                }
            }
            if (shared && !sharesRealizedContinuation(node) &&
                node.Reference != InvalidId)
            {
                anchors[node.Anchor] = anchors[node.Reference];
                return true;
            }
            const auto target = Offset(
                node.Alignment == AnchorKind::BlockRelative ? base : column,
                offset);
            anchors[node.Anchor] =
                {target, base, static_cast<std::uint32_t>(i), sharedOffset};
            return true;
        }
        if (node.Type == Kind::ExpressionLine) {
            if (!frame.ReversibleIndent) {
                indentationError(i);
                return false;
            }
            frame.PendingExpressionIndent = 0;
            frame.RealizedExpressionLine = true;
            realizeContinuation();
            return true;
        }
        auto outputBegin = result.Text.size();
        auto emittedOrigin = node.Origin;
        bool exact = node.Exact;
        const bool ownerBroken =
            node.Owner != InvalidId && result.Broken[node.Owner];
        if (node.Type == Kind::SourceWhitespace) {
            const bool targetCRLF = node.FlatGap.LineEnding == GapNewline::CRLF;
            const auto text = node.FlatText;
            if (text.size() > result.Text.max_size() ||
                (targetCRLF &&
                    node.ChangedNewlines >
                        result.Text.max_size() - text.size()))
            {
                result.State = {Error::TooLarge, static_cast<std::uint32_t>(i)};
                return false;
            }
            const auto size = targetCRLF
                ? text.size() + std::size_t(node.ChangedNewlines)
                : text.size(); // Incoming boundaries may suppress contractions.
            if (size > result.Text.max_size() - result.Text.size()) {
                result.State = {Error::TooLarge, static_cast<std::uint32_t>(i)};
                return false;
            }
            std::size_t run = 0;
            const auto copy = [&] (std::size_t end) {
                if (run == end) {
                    return;
                }
                const auto begin = result.Text.size();
                result.Text.append(text.substr(run, end - run));
                if (policy.SourceMap) {
                    result.Map.push_back({
                        begin,
                        result.Text.size(),
                        {
                            node.Origin.Begin + static_cast<std::uint32_t>(run),
                            node.Origin.Begin + static_cast<std::uint32_t>(end),
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
                    : !result.Text.empty() && result.Text.back() == '\r';
                if (!crlf && !targetCRLF && incomingCR) {
                    // The producer omitted a necessary original separator.
                    // This slice has no authority to invent or recover it.
                    result.State = {
                        Error::InvalidSourceRange,
                        static_cast<std::uint32_t>(i),
                    };
                    return false;
                }
                if (!ChangesWhitespaceAtom(
                    Source_,
                    node.Origin.Begin + cursor,
                    targetCRLF) || (crlf && incomingCR))
                {
                    continue;
                }
                copy(atom);
                const auto begin = result.Text.size();
                result.Text.append(targetCRLF ? "\r\n" : "\n");
                if (policy.SourceMap) {
                    result.Map.push_back({
                        begin,
                        result.Text.size(),
                        {
                            node.Origin.Begin +
                                static_cast<std::uint32_t>(atom),
                            node.Origin.Begin +
                                static_cast<std::uint32_t>(cursor + 1),
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
            const bool continuingIndent = frame.RealizedContinuationLine &&
                column == frame.RealizedContinuationIndent;
            advance(text);
            if (entry.Flat.Line || continuingIndent) {
                realizeContinuation();
            }
            return true;
        }
        if (node.Type == Kind::Text || node.Type == Kind::Verbatim ||
            node.Type == Kind::ConditionalText)
        {
            auto text = node.Type == Kind::ConditionalText && ownerBroken
                ? node.BrokenText
                : node.FlatText;
            const bool continuingIndent = frame.RealizedContinuationLine &&
                column == frame.RealizedContinuationIndent;
            append(text);
            advance(text);
            if (frame.HasContinuation && node.Type == Kind::Verbatim &&
                node.Origin.Identity == NoIdentity &&
                (entry.Flat.Line || continuingIndent) &&
                text.find_first_not_of(" \t\r\n") == std::string_view::npos)
            {
                realizeContinuation();
            }
        } else if (node.Type == Kind::SourceLineIndent) {
            append(node.FlatText);
            advance(node.FlatText);
            if (policy.SourceMap && !probing) {
                result.Map.push_back(
                    {outputBegin, result.Text.size(), node.Origin, true, true});
            }
            outputBegin = result.Text.size();
            emittedOrigin = {
                node.FlatGap.IndentSourceBegin,
                node.FlatGap.IndentSourceEnd,
                NoIdentity,
            };
            emitIndent(resolveIndent(node.Anchor, node.FlatGap.RequiredWidth));
            realizeContinuation();
            // Do not set frame.OwnedLine: only editable gaps satisfy
            // ForceOwned.
        } else {
            if (frame.OwnedFlat && ownerBroken &&
                node.Type == Kind::ConditionalGap &&
                node.BrokenGap.Kind == GapKind::Hard)
            {
                // Conditional topology belongs to the owner's broken shape;
                // it cannot be suppressed by a child-only alternative.
                return false;
            }
            auto gap = node.Type == Kind::ConditionalGap && ownerBroken
                ? node.BrokenGap
                : node.FlatGap;
            const bool continuingIndent = frame.RealizedContinuationLine &&
                column == frame.RealizedContinuationIndent;
            bool line = gap.Kind == GapKind::Hard;
            if (gap.Kind == GapKind::Soft && ownerBroken && !frame.OwnedFlat) {
                if (Nodes_[Groups_[node.Owner]].Mode == GroupMode::Consistent) {
                    line = true;
                } else {
                    line = !fits(AddWidth(
                        entry.ExpandedGapWidth,
                        gap.FitNextSegment
                            ? gap.RequiredWidth
                            : tailWidth(entry.BrokenTail))) ||
                        (frame.Mandatory == i &&
                            (frame.ForceOwned
                                ? !frame.OwnedLine
                                : lines == frame.StartLine));
                }
            }
            if (line) {
                if (probing && node.Owner != InvalidId) {
                    probeBreakPenalty += std::size_t(gap.NewlineCount) *
                        Nodes_[Groups_[node.Owner]].BreakPenalty;
                }
                const bool lf = gap.LineEnding == GapNewline::LF ||
                    (gap.LineEnding == GapNewline::Inherit &&
                        policy.LineEnding == Newline::LF);
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
                    if (policy.SourceMap && !probing) {
                        result.Map.push_back({
                            outputBegin,
                            result.Text.size(),
                            node.Origin,
                            false,
                            false,
                        });
                    }
                    outputBegin = result.Text.size();
                    auto text = Source_.substr(
                        gap.IndentSourceBegin,
                        gap.IndentSourceEnd - gap.IndentSourceBegin);
                    append(text);
                    column = 0;
                    advance(text);
                    emittedOrigin.Begin = gap.IndentSourceBegin;
                    emittedOrigin.End = gap.IndentSourceEnd;
                    exact = true;
                } else {
                    emitIndent(resolveIndent(node.Anchor, gap.RequiredWidth));
                }
            } else if (gap.FlatSourceBegin != InvalidId) {
                auto text = Source_.substr(
                    gap.FlatSourceBegin,
                    gap.FlatSourceEnd - gap.FlatSourceBegin);
                append(text);
                advance(text);
                emittedOrigin.Begin = gap.FlatSourceBegin;
                emittedOrigin.End = gap.FlatSourceEnd;
                exact = true;
            } else {
                if (!probing) {
                    result.Text.append(gap.Spaces, ' ');
                }
                column = AddWidth(column, gap.Spaces);
                if (linePrefix) {
                    lineIndent = column;
                }
            }
            if (line || continuingIndent) {
                realizeContinuation();
            }
        }
        if (policy.SourceMap && !probing) {
            result.Map.push_back({
                outputBegin,
                result.Text.size(),
                emittedOrigin,
                exact,
                node.Type == Kind::Verbatim,
            });
        }
        return !probing || column <= policy.Width;
    };
    for (std::size_t i = 0; i < Nodes_.size(); ++i) {
        const auto& node = Nodes_[i];
        const auto& entry = cache[i];
        if (layoutSelected && i > childLayoutEnd) {
            layoutSelected = false;
            childLayoutDepth = 0;
        }
        if (!layoutSelected && node.Type == Kind::Begin &&
            entry.ChildLayoutAllowed && entry.OwnedLastBreak != InvalidId &&
            !frame.Flat && !entry.Flat.Line && entry.Flat.CanBreak &&
            !fits(AddWidth(entry.Flat.Width, groupTail(entry))))
        {
            // Groups without owned breaks have no parent/child choice; their
            // children plan independently rather than spending this budget.
            // A parent layout plus four child depths, at most 256 nodes each.
            // Minimize newlines plus break/shape penalties, then lines, then
            // multiline groups; retain the parent layout on a tie. Large/protected
            // multiline regions keep the existing conservative layout. No
            // recursive search, trial output, or per-candidate allocations.
            constexpr std::size_t MaxProbeNodes = 256;
            constexpr std::size_t MaxProbeDepth = 4;
            bool eligible = node.Match - i < MaxProbeNodes;
            bool child = false;
            std::size_t candidateDepth = 1;
            std::size_t nesting = 1;
            if (eligible) {
                for (std::size_t j = i + 1; j < node.Match; ++j) {
                    const auto& part = Nodes_[j];
                    if (part.Type == Kind::Begin) {
                        child = true;
                        if (cache[j].ChildLayoutAllowed) {
                            candidateDepth = std::max(
                                candidateDepth,
                                std::min(MaxProbeDepth, nesting + 1));
                        }
                        ++nesting;
                    } else if (part.Type == Kind::End) {
                        --nesting;
                    }
                    if (part.Type == Kind::SourceWhitespace ||
                        part.Type == Kind::SourceLineIndent ||
                        (part.Type != Kind::Begin && cache[j].Flat.Line) ||
                        part.FlatText.size() > 4096 ||
                        part.BrokenText.size() > 4096)
                    {
                        eligible = false;
                        break;
                    }
                }
            }
            if (eligible && child) {
                const auto savedFrame = frame;
                const auto savedColumn = column;
                const auto savedLines = lines;
                const auto savedLineIndent = lineIndent;
                const auto savedLinePrefix = linePrefix;
                const auto savedDepth = frames.size();
                const auto tail = groupTail(entry);
                childLayoutBase = savedDepth;
                childLayoutEnd = node.Match;
                std::size_t bestCost = 0;
                std::size_t bestLines = 0;
                std::size_t bestGroups = 0;
                std::size_t bestDepth = 0;
                // Trial zero is the normal parent-breaking layout. The other
                // trials retain successively shallower owned-flat prefixes.
                for (std::size_t trial = 0; trial <= candidateDepth; ++trial) {
                    const auto depth = trial == 0
                        ? 0
                        : candidateDepth - trial + 1;
                    childLayoutDepth = depth;
                    probeBrokenGroups = 0;
                    probeBreakPenalty = 0;
                    probing = true;
                    bool fitsCandidate = true;
                    for (std::size_t j = i; j <= node.Match; ++j) {
                        if (!renderNode(j) ||
                            (layoutSelected &&
                                lines - savedLines + probeBreakPenalty >
                                    bestCost))
                        {
                            // Cost is nonnegative and only grows. A candidate
                            // already worse than the incumbent cannot win.
                            fitsCandidate = false;
                            break;
                        }
                    }
                    fitsCandidate = fitsCandidate && fits(tail);
                    const auto candidateLines = lines - savedLines;
                    const auto candidateCost =
                        candidateLines + probeBreakPenalty;
                    if (fitsCandidate &&
                        (!layoutSelected || candidateCost < bestCost ||
                            (candidateCost == bestCost &&
                                (candidateLines < bestLines ||
                                    (candidateLines == bestLines &&
                                        probeBrokenGroups < bestGroups)))))
                    {
                        layoutSelected = true;
                        bestCost = candidateCost;
                        bestLines = candidateLines;
                        bestGroups = probeBrokenGroups;
                        bestDepth = depth;
                    }
                    // All touched decisions and anchors belong to not-yet-
                    // emitted nodes. Ancestor slots are read-only in a probe.
                    for (std::size_t j = i; j <= node.Match; ++j) {
                        const auto& part = Nodes_[j];
                        if (part.Type == Kind::Begin) {
                            result.Broken[part.Reference] = 0;
                        } else if (part.Type == Kind::Anchor) {
                            anchors[part.Anchor] = {};
                        }
                    }
                    frames.resize(savedDepth);
                    frame = savedFrame;
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
        if (!renderNode(i)) {
            if (result.State) {
                result.State =
                    {Error::InvalidPolicy, static_cast<std::uint32_t>(i)};
            }
            return result;
        }
    }
    return result;
}

} // namespace document
