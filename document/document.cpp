#include <util/ascii.hpp>

#include <document/document.hpp>

#include <unicode/utf8.hpp>

#include <algorithm>
#include <array>
#include <limits>
#include <memory>

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
    std::uint32_t Prefix = 0;
    bool Line = false;
    bool CanBreak = false;
    // ASCII bytes without tabs/newlines advance by their length. Leading
    // indentation still needs inspection; Unicode retains code-point rules.
    bool Plain = true;

    std::uint32_t Width() const
    {
        return Line ? Infinity : Prefix;
    }
};

static_assert(sizeof(Measure) <= 8);

Measure Join(Measure a, Measure b)
{
    return {
        a.Line ? a.Prefix : AddWidth(a.Prefix, b.Prefix),
        a.Line || b.Line,
        a.CanBreak || b.CanBreak,
        a.Plain && b.Plain,
    };
}

Measure MeasureText(std::string_view text, std::uint8_t tabWidth)
{
    Measure result;
    // Source ranges and borrowed text are bounded by 32-bit byte offsets.
    // Even all tabs fit a 64-bit sum; saturate once, not per code point.
    const auto ascii = util::PlainAsciiPrefix(text);
    if (ascii == text.size()) {
        return {static_cast<std::uint32_t>(ascii), false, false};
    }
    std::uint64_t prefix = ascii;
    for (std::size_t i = ascii; i < text.size(); ++i) {
        const auto c = static_cast<unsigned char>(text[i]);
        if (c >= 0x80) {
            i += unicode::utf8::GetFirstCodePointSize(text.substr(i)).value_or(1) - 1;
        }
        if (c == '\n' || c == '\r') {
            result.Line = true;
            result.CanBreak = true;
            result.Plain = false;
            // No suffix fact is consumed after the first physical boundary.
            result.Prefix = static_cast<std::uint32_t>(
                std::min<std::uint64_t>(prefix, Infinity));
            return result;
        } else {
            result.Plain = result.Plain && c != '\t' && static_cast<unsigned char>(c) < 0x80;
            // Conservative only for literal/source tabs: actual emission uses
            // tab stops. This summary never underestimates flat width.
            prefix += c == '\t' ? tabWidth : 1;
        }
    }
    result.Prefix = static_cast<std::uint32_t>(
        std::min<std::uint64_t>(prefix, Infinity));
    return result;
}

Measure MeasureFlatGap(GapSpec gap, std::string_view source, std::uint8_t tabWidth)
{
    if (gap.Kind == GapKind::Hard) {
        return {0, true, true};
    }
    if (gap.FlatSourceBegin != InvalidId) {
        auto measured =
            MeasureText(source.substr(gap.FlatSourceBegin, gap.FlatSourceEnd - gap.FlatSourceBegin), tabWidth);
        measured.CanBreak = gap.Kind == GapKind::Soft;
        return measured;
    }
    return {gap.Spaces, false, gap.Kind == GapKind::Soft};
}

std::uint32_t SourceColumn(std::string_view text, std::uint8_t tabWidth)
{
    std::uint32_t column = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const auto c = static_cast<unsigned char>(text[i]);
        if (c >= 0x80) {
            i += unicode::utf8::GetFirstCodePointSize(text.substr(i)).value_or(1) - 1;
        }
        column = AddWidth(column, c == '\t' ? tabWidth - column % tabWidth : 1);
    }
    return column;
}

bool Multiline(std::string_view text)
{
    for (const auto c : text) {
        if (c == '\r' || c == '\n') {
            return true;
        }
    }
    return false;
}

// LF offsets identify complete authorized atoms. Contracting CRLF must not
// pair its original bare-CR predecessor with the surviving LF.
bool ChangesWhitespaceAtom(std::string_view source, std::size_t lf, bool targetCRLF)
{
    const bool crlf = lf && source[lf - 1] == '\r';
    return crlf != targetCRLF && (!crlf || lf < 2 || source[lf - 2] != '\r');
}

bool ValidGapNewline(GapNewline value)
{
    return value == GapNewline::Inherit || value == GapNewline::LF || value == GapNewline::CRLF;
}

} // namespace

Result::Result(std::pmr::memory_resource* resource)
    : Text(resource)
    , Map(resource)
    , Tokens(resource)
    , Broken(resource)
{ }

Document::Document(std::string_view source, std::pmr::memory_resource* resource)
    : Source_(source)
    , Nodes_(resource)
    , TextPayloads_(resource)
    , GroupPayloads_(resource)
    , GapPayloads_(resource)
    , PrefixPayloads_(resource)
    , BorrowedTexts_(resource)
    , RunParts_(resource)
    , Groups_(resource)
    , Open_(resource)
{
    if (source.size() >= InvalidId) {
        Fail(Error::TooLarge);
    }
}

Document::Node::Node(Kind kind, bool prefixed)
    : Type(kind)
    , HasPrefix(prefixed)
{
    switch (kind) {
        case Kind::Begin:
        case Kind::End:
        case Kind::Anchor:
            std::construct_at(&Group);
            break;
        case Kind::Gap:
        case Kind::ConditionalGap:
        case Kind::SourceLineIndent:
            std::construct_at(&Gaps);
            break;
        case Kind::SourceRun:
            std::construct_at(&Run);
            break;
        default:
            if (HasPrefix) {
                std::construct_at(&Prefix);
            } else {
                std::construct_at(&Text);
            }
            break;
    }
}

inline Document::Atom Document::Pack(const Node& node)
{
    Atom atom(node.Type);
    atom.Origin = node.Origin;
    atom.Owner = node.Owner;
    atom.Exact = node.Exact;
    atom.HasPrefix = node.HasPrefix;
    atom.Compact = node.Compact;
    atom.Plain = node.Plain;
    if (node.HasPrefix) {
        if (PrefixPayloads_.empty()) {
            PrefixPayloads_.reserve(Nodes_.capacity() / 16);
        }
        atom.Payload = static_cast<std::uint32_t>(PrefixPayloads_.size());
        PrefixPayloads_.push_back(node.Prefix);
    } else if (node.Type == Kind::Begin || node.Type == Kind::Anchor) {
        atom.Payload = static_cast<std::uint32_t>(GroupPayloads_.size());
        GroupPayloads_.push_back(node.Group);
    } else if (node.Type == Kind::End) {
        atom.End = {node.Group.Match, node.Group.LogicalPosition, node.Group.Reference};
    } else if (node.Type == Kind::SourceRun) {
        std::construct_at(&atom.Run, node.Run);
    } else if (node.Type == Kind::ConditionalText) {
        if (TextPayloads_.empty()) {
            TextPayloads_.reserve(Nodes_.capacity() / 32);
        }
        atom.Payload = static_cast<std::uint32_t>(TextPayloads_.size());
        TextPayloads_.push_back({node.Text.Flat, node.Text.Broken, node.Text.Detail});
    } else if (node.Type == Kind::Gap || node.Type == Kind::ConditionalGap || node.Type == Kind::SourceLineIndent) {
        RenderGaps_ += node.Type != Kind::SourceLineIndent;
        const auto& gap = node.Gaps.Flat;
        atom.ComplexGap =
            node.Type != Kind::Gap || gap.FlatSourceBegin != InvalidId || gap.IndentSourceBegin != InvalidId;
        if (atom.ComplexGap) {
            if (GapPayloads_.empty()) {
                GapPayloads_.reserve(Nodes_.capacity() / 8);
            }
            atom.Payload = static_cast<std::uint32_t>(GapPayloads_.size());
            GapPayloads_.push_back(node.Gaps);
        } else {
            atom.GapType = gap.Kind;
            atom.LineEnding = gap.LineEnding;
            atom.FitNextSegment = gap.FitNextSegment;
            atom.FitFlatTail = gap.FitFlatTail;
            atom.Gap = {gap.RequiredWidth, node.Gaps.Anchor, gap.Spaces, gap.NewlineCount};
        }
    } else {
        atom.Text = {node.Text.Flat, node.Text.Broken, node.Text.Detail};
        atom.LineEnding = node.Text.LineEnding;
    }
    return atom;
}

inline GapSpec Document::GapFor(const Atom& atom, bool broken) const
{
    if (atom.ComplexGap) {
        const auto& gaps = GapPayloads_[atom.Payload];
        return broken ? gaps.Broken : gaps.Flat;
    }
    GapSpec gap;
    gap.Kind = atom.GapType;
    gap.Spaces = atom.Gap.Spaces;
    gap.RequiredWidth = atom.Gap.RequiredWidth;
    gap.LineEnding = atom.LineEnding;
    gap.FitNextSegment = atom.FitNextSegment;
    gap.FitFlatTail = atom.FitFlatTail;
    gap.NewlineCount = atom.Gap.NewlineCount;
    return gap;
}

inline AnchorId Document::GapAnchor(const Atom& atom) const
{
    return atom.ComplexGap ? GapPayloads_[atom.Payload].Anchor : atom.Gap.Anchor;
}

const Document::GroupData& Document::Group(const Atom& atom) const
{
    return GroupPayloads_[atom.Payload];
}

const Document::PrefixedTextData& Document::Prefix(const Atom& atom) const
{
    return PrefixPayloads_[atom.Payload];
}

std::uint32_t Document::BorrowText(std::string_view text)
{
    if (text.empty()) {
        return InvalidId;
    }
    if (text.size() >= InvalidId || BorrowedTexts_.size() >= SourceTextId) {
        Fail(Error::TooLarge);
        return InvalidId;
    }
    // Repeated generated punctuation often borrows the same literal.
    if (!BorrowedTexts_.empty() && BorrowedTexts_.back().Data == text.data() &&
        BorrowedTexts_.back().Size == text.size())
    {
        return static_cast<std::uint32_t>(BorrowedTexts_.size() - 1);
    }
    const auto id = static_cast<std::uint32_t>(BorrowedTexts_.size());
    BorrowedTexts_.push_back({text.data(), static_cast<std::uint32_t>(text.size())});
    return id;
}

std::string_view Document::TextView(std::uint32_t id, Provenance origin) const
{
    if (id == InvalidId) {
        return {};
    }
    if (id == SourceTextId) {
        return Source_.substr(origin.Begin, origin.End - origin.Begin);
    }
    const auto& text = BorrowedTexts_[id];
    return {text.Data, text.Size};
}

std::string_view Document::FlatText(const Atom& node) const
{
    switch (node.Type) {
        case Kind::SourceLineIndent:
            return TextView(SourceTextId, node.Origin);
        case Kind::ConditionalText:
            return TextView(TextPayloads_[node.Payload].Flat, node.Origin);
        case Kind::Text:
        case Kind::Verbatim:
        case Kind::SourceWhitespace:
        case Kind::ReindentedText:
            return TextView(node.HasPrefix ? SourceTextId : node.Text.Flat, node.Origin);
        default:
            return {};
    }
}

std::string_view Document::BrokenText(const Atom& node) const
{
    return node.Type == Kind::ConditionalText
        ? TextView(TextPayloads_[node.Payload].Broken, node.Origin)
        : std::string_view{};
}

Document::~Document() = default;

void Document::Reserve(std::uint32_t nodes, std::uint32_t groups, std::uint32_t sourceParts)
{
    if (Sealed_) {
        Fail(Error::Sealed);
    }
    if (nodes == InvalidId || groups == InvalidId || sourceParts == InvalidId) {
        Fail(Error::TooLarge);
    }
    if (State_) {
        Nodes_.reserve(nodes);
        Groups_.reserve(groups);
        // A lowering group commonly has both a block and a continuation
        // anchor. Avoid retaining successive payload capacities in the arena.
        GroupPayloads_.reserve(std::size_t(groups) * 3);
        RunParts_.reserve(sourceParts);
    }
}

void Document::Fail(Error error)
{
    if (State_) {
        State_ = {error, static_cast<std::uint32_t>(Nodes_.size())};
    }
}

std::uint32_t Document::Add(const Node& node)
{
    if (Sealed_) {
        Fail(Error::Sealed);
    }
    if (!State_) {
        return InvalidId;
    }
    if (LogicalNodes_ >= InvalidId) {
        Fail(Error::TooLarge);
        return InvalidId;
    }
    const auto position = LogicalNodes_++;
    auto atom = Pack(node);
    atom.Owner = Open_.empty() ? InvalidId : Open_.back();
    if (node.Type == Kind::Begin) {
        GroupPayloads_[atom.Payload].LogicalPosition = position;
    } else if (node.Type == Kind::End) {
        atom.End.LogicalPosition = position;
    }
    const auto id = static_cast<std::uint32_t>(Nodes_.size());
    Nodes_.push_back(atom);
    return id;
}

bool Document::CheckOrigin(Provenance origin)
{
    if ((origin.Begin == InvalidId) != (origin.End == InvalidId) ||
        (origin.Begin != InvalidId && (origin.Begin > origin.End || origin.End > Source_.size())))
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

GroupId Document::BeginGroup(GroupSpec spec, std::uint32_t elidedMarkers)
{
    if (spec.Mode != GroupMode::Consistent && spec.Mode != GroupMode::Fill) {
        Fail(Error::InvalidPolicy);
    }
    if (spec.ExpressionContinuation && (spec.Indent < 0 || spec.StructuralBase)) {
        Fail(Error::InvalidPolicy);
    }
    Node node(Kind::Begin);
    node.Group.Mode = spec.Mode;
    node.Group.Indent = spec.Indent;
    node.Group.ForceBreak = spec.ForceBreak;
    node.Group.ExpressionContinuation = spec.ExpressionContinuation;
    node.Group.StructuralBase = spec.StructuralBase;
    node.Group.Continuation = spec.Continuation;
    node.Group.LineBase = spec.LineBase;
    if (spec.LineBase && !spec.StructuralBase) {
        Fail(Error::InvalidPolicy);
    }
    node.Group.FixedBase = spec.FixedBase;
    if (spec.FixedBase != InvalidId && (!spec.StructuralBase || spec.Continuation || spec.LineBase)) {
        Fail(Error::InvalidPolicy);
    }
    node.Group.ChildBreaks = spec.ChildBreaks;
    node.Group.FitSuffixWidth = spec.FitSuffixWidth;
    if (spec.BreakPenalty > 3 || spec.FlatInBrokenParentPenalty > 3) {
        Fail(Error::InvalidPolicy);
    }
    node.Group.BreakPenalty = spec.BreakPenalty;
    node.Group.FlatInBrokenParentPenalty = spec.FlatInBrokenParentPenalty;
    if (spec.Continuation && spec.Indent < 0) {
        Fail(Error::InvalidPolicy);
    }
    node.Group.Reference = static_cast<GroupId>(Groups_.size());
    if (State_ && !Sealed_) {
        if (elidedMarkers > InvalidId - LogicalNodes_) {
            Fail(Error::TooLarge);
        } else {
            LogicalNodes_ += elidedMarkers;
        }
    }
    auto index = Add(node);
    if (index == InvalidId) {
        return InvalidId;
    }
    Groups_.push_back(index);
    Open_.push_back(node.Group.Reference);
    Depth_ = std::max(Depth_, static_cast<std::uint32_t>(Open_.size()));
    return node.Group.Reference;
}

void Document::EndGroup(GroupId group)
{
    if (Open_.empty() || Open_.back() != group) {
        Fail(Error::UnbalancedGroup);
        return;
    }
    Node node(Kind::End);
    node.Group.Reference = group;
    node.Group.Match = Groups_[group];
    auto index = Add(node);
    if (index != InvalidId) {
        GroupPayloads_[Nodes_[node.Group.Match].Payload].Match = index;
        Open_.pop_back();
    }
}

void Document::Text(std::string_view generated, Provenance origin)
{
    if (generated.size() >= InvalidId) {
        Fail(Error::TooLarge);
        return;
    }
    if (Multiline(generated)) {
        Fail(Error::MultilineText);
    }
    if (!CheckOrigin(origin)) {
        return;
    }
    Node node(Kind::Text);
    node.Text.Flat = BorrowText(generated);
    node.Origin = origin;
    Add(node);
}

void Document::SourceText(std::uint32_t begin, std::uint32_t end, Identity identity, bool verbatim)
{
    EmitSourceText(begin, end, identity, verbatim, false);
}

void Document::AppendSourceText(std::uint32_t begin, std::uint32_t end, Identity identity, bool verbatim)
{
    EmitSourceText(begin, end, identity, verbatim, true);
}

Document::RunPart Document::FlatRunPart(GapSpec gap, Provenance origin)
{
    if (gap.FlatSourceBegin != InvalidId) {
        origin.Begin = gap.FlatSourceBegin;
        origin.End = gap.FlatSourceEnd;
        return {origin, 0, RunKind::SourceGap};
    }
    return {origin, gap.Spaces, RunKind::Spaces};
}

void Document::StartSourceRun()
{
    const auto previous = Nodes_.back();
    Node run(Kind::SourceRun);
    run.Owner = previous.Owner;
    run.Run.Begin = run.Run.End = static_cast<std::uint32_t>(RunParts_.size());
    Nodes_.back() = Pack(run);
    if (previous.HasPrefix) {
        AppendRunPart(FlatRunPart(Prefix(previous).Gap, Prefix(previous).Origin), false);
        if (previous.Payload + 1 == PrefixPayloads_.size()) {
            PrefixPayloads_.pop_back();
        }
    }
    AppendRunPart(
        {previous.Origin, 0, previous.Type == Kind::Verbatim ? RunKind::Verbatim : RunKind::Text, previous.Plain},
        false);
}

void Document::AppendRunPart(RunPart part, bool logical)
{
    if (RunParts_.size() >= InvalidId || (logical && LogicalNodes_ >= InvalidId)) {
        Fail(Error::TooLarge);
        return;
    }
    RunParts_.push_back(part);
    auto& run = Nodes_.back().Run;
    run.End = static_cast<std::uint32_t>(RunParts_.size());
    if (part.Type == RunKind::Text || part.Type == RunKind::Verbatim) {
        run.MaxText = std::max(run.MaxText, part.Origin.End - part.Origin.Begin);
    }
    LogicalNodes_ += logical;
}

void Document::EmitSourceText(std::uint32_t begin, std::uint32_t end, Identity identity, bool verbatim, bool compact)
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
    const auto ascii = util::PlainAsciiPrefix(text);
    const bool plain = ascii == text.size();
    const bool multiline = !plain && Multiline(text.substr(ascii));
    if (!verbatim && multiline) {
        Fail(Error::MultilineText);
        return;
    }
    const auto owner = Open_.empty() ? InvalidId : Open_.back();
    if (compact && !multiline && !Sealed_ && !Nodes_.empty() && Nodes_.back().Owner == owner) {
        const RunPart part{origin, 0, verbatim ? RunKind::Verbatim : RunKind::Text, plain};
        if (Nodes_.back().Type == Kind::SourceRun) {
            AppendRunPart(part);
            return;
        }
        const bool gap = Nodes_.back().Type == Kind::Gap && GapFor(Nodes_.back()).Kind == GapKind::Flat;
        const auto first = Nodes_.size() - 1 - gap;
        if ((!gap || Nodes_.size() > 1) && Nodes_[first].Owner == owner &&
            (Nodes_[first].Type == Kind::Text || Nodes_[first].Type == Kind::Verbatim) &&
            (Nodes_[first].HasPrefix || Nodes_[first].Text.Flat == SourceTextId) &&
            (Nodes_[first].Plain || !Multiline(FlatText(Nodes_[first]))))
        {
            if (LogicalNodes_ >= InvalidId || RunParts_.size() > InvalidId - 5) {
                Fail(Error::TooLarge);
                return;
            }
            const auto prefix = gap ? FlatRunPart(GapFor(Nodes_.back()), Nodes_.back().Origin) : RunPart{};
            if (gap) {
                Nodes_.pop_back();
                --RenderGaps_;
            }
            StartSourceRun();
            if (gap) {
                AppendRunPart(prefix, false);
            }
            AppendRunPart(part);
            return;
        }
    }
    if (!Sealed_ && !Nodes_.empty() && Nodes_.back().Type == Kind::Gap && GapFor(Nodes_.back()).Kind == GapKind::Flat &&
        Nodes_.back().Owner == owner)
    {
        if (LogicalNodes_ >= InvalidId) {
            Fail(Error::TooLarge);
            return;
        }
        const auto prefix = GapFor(Nodes_.back());
        const auto prefixOrigin = Nodes_.back().Origin;
        Node node(verbatim ? Kind::Verbatim : Kind::Text, true);
        node.Prefix = {prefix, prefixOrigin};
        node.Origin = origin;
        node.Owner = owner;
        node.Exact = true;
        node.Compact = compact && !multiline;
        node.Plain = plain;
        --RenderGaps_;
        Nodes_.back() = Pack(node);
        ++LogicalNodes_;
    } else {
        Node node(verbatim ? Kind::Verbatim : Kind::Text);
        node.Text.Flat = SourceTextId;
        node.Origin = origin;
        node.Exact = true;
        node.Compact = compact && !multiline;
        node.Plain = plain;
        Add(node);
    }
}

void Document::ReindentedSourceText(std::uint32_t begin, std::uint32_t end, Identity identity)
{
    if (begin == InvalidId || end == InvalidId || !CheckOrigin({begin, end, identity})) {
        Fail(Error::InvalidSourceRange);
        return;
    }
    const auto newline = begin ? Source_.find_last_of("\r\n", begin - 1) : std::string_view::npos;
    Node node(Kind::ReindentedText);
    node.Text.Flat = SourceTextId;
    node.Text.Detail = newline == std::string_view::npos ? 0 : newline + 1;
    node.Origin = {begin, end, identity};
    node.Exact = true;
    Add(node);
}

void Document::SourceWhitespace(std::uint32_t begin, std::uint32_t end, Newline ending)
{
    if (ending != Newline::LF && ending != Newline::CRLF) {
        Fail(Error::InvalidPolicy);
        return;
    }
    if (begin == InvalidId || end == InvalidId || !CheckOrigin({begin, end, NoIdentity})) {
        Fail(Error::InvalidSourceRange);
        return;
    }
    const auto text = Source_.substr(begin, end - begin);
    if (text.find_first_not_of(" \t\r\n\f\v") != std::string_view::npos ||
        (begin && begin < end && Source_[begin - 1] == '\r' && Source_[begin] == '\n') ||
        (begin < end && end < Source_.size() && Source_[end - 1] == '\r' && Source_[end] == '\n'))
    {
        Fail(Error::InvalidSourceRange);
        return;
    }
    std::uint32_t changed = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\n') {
            changed += ChangesWhitespaceAtom(Source_, begin + i, ending == Newline::CRLF);
        }
    }
    if (text.empty()) {
        SourceText(begin, end, NoIdentity, true);
        return;
    }
    Node node(Kind::SourceWhitespace);
    node.Text.Flat = SourceTextId;
    node.Origin = {begin, end, NoIdentity};
    node.Text.LineEnding = ending == Newline::LF ? GapNewline::LF : GapNewline::CRLF;
    node.Text.Detail = changed;
    Add(node);
}

void Document::Verbatim(std::string_view borrowed, Provenance origin)
{
    if (borrowed.size() >= InvalidId) {
        Fail(Error::TooLarge);
        return;
    }
    if (!CheckOrigin(origin)) {
        return;
    }
    Node node(Kind::Verbatim);
    node.Text.Flat = BorrowText(borrowed);
    node.Origin = origin;
    node.Exact = origin.Begin != InvalidId;
    if (node.Exact && borrowed != Source_.substr(origin.Begin, origin.End - origin.Begin)) {
        Fail(Error::InvalidSourceRange);
        return;
    }
    Add(node);
}

void Document::Gap(GapSpec spec, AnchorId anchor, Provenance origin)
{
    if (spec.Kind != GapKind::Flat && spec.Kind != GapKind::Soft && spec.Kind != GapKind::Hard) {
        Fail(Error::InvalidPolicy);
    }
    CheckGapData(spec);
    if (anchor != InvalidId && anchor >= Anchors_) {
        Fail(Error::InvalidAnchor);
    }
    if (!CheckOrigin(origin)) {
        return;
    }
    if (!Sealed_ && State_ && !Nodes_.empty() && spec.Kind == GapKind::Flat &&
        Nodes_.back().Owner == (Open_.empty() ? InvalidId : Open_.back()) &&
        (Nodes_.back().Type == Kind::SourceRun || Nodes_.back().Compact))
    {
        if (LogicalNodes_ >= InvalidId || RunParts_.size() > InvalidId - 4) {
            Fail(Error::TooLarge);
            return;
        }
        if (Nodes_.back().Type != Kind::SourceRun) {
            StartSourceRun();
        }
        AppendRunPart(FlatRunPart(spec, origin));
        return;
    }
    Node node(Kind::Gap);
    node.Gaps.Flat = node.Gaps.Broken = spec;
    node.Gaps.Anchor = anchor;
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
    Identity identity)
{
    if (begin == InvalidId || end == InvalidId || indentBegin == InvalidId || indentEnd == InvalidId) {
        Fail(Error::InvalidSourceRange);
        return;
    }
    if (!CheckOrigin({begin, end, identity}) || !CheckHorizontalSource(indentBegin, indentEnd)) {
        return;
    }
    const auto prefix = Source_.substr(begin, end - begin);
    if (prefix.empty() || prefix.back() != '\n' || prefix.find_first_not_of(" \t\r\n") != std::string_view::npos) {
        Fail(Error::InvalidLinePrefix);
    }
    if (anchor != InvalidId && anchor >= Anchors_) {
        Fail(Error::InvalidAnchor);
    }
    Node node(Kind::SourceLineIndent);
    node.Origin = {begin, end, identity};
    node.Gaps.Anchor = anchor;
    node.Gaps.Flat.RequiredWidth = requiredWidth;
    node.Gaps.Flat.IndentSourceBegin = indentBegin;
    node.Gaps.Flat.IndentSourceEnd = indentEnd;
    Add(node);
}

AnchorId Document::MarkAnchor(AnchorSpec spec)
{
    if (spec.Kind != AnchorKind::BlockRelative && spec.Kind != AnchorKind::DelimiterColumn) {
        Fail(Error::InvalidPolicy);
    }
    Node node(Kind::Anchor);
    node.Group.Anchor = Anchors_;
    node.Group.Reference = spec.SharedWith;
    if (spec.SharedWith != InvalidId && spec.SharedWith >= Anchors_) {
        Fail(Error::InvalidAnchor);
    }
    node.Group.Alignment = spec.Kind;
    node.Group.Indent = spec.Offset;
    node.Group.Fallback = spec.Fallback;
    node.Group.Minimum = spec.Minimum;
    node.Group.StructuralBase = spec.StructuralBase;
    node.Group.Continuation = spec.Continuation;
    node.Group.LineBase = spec.LineBase;
    if (spec.LineBase && !spec.StructuralBase) {
        Fail(Error::InvalidPolicy);
    }
    node.Group.FixedBase = spec.FixedBase;
    if (spec.FixedBase != InvalidId &&
        (!spec.StructuralBase || spec.Continuation || spec.LineBase || spec.Kind != AnchorKind::BlockRelative))
    {
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
    if (Open_.empty() || !Group(Nodes_[Groups_[Open_.back()]]).ExpressionContinuation) {
        Fail(Error::InvalidGroup);
        return;
    }
    if (Nodes_.empty()) {
        Fail(Error::InvalidLinePrefix);
        return;
    }
    const auto& previous = Nodes_.back();
    if ((previous.Type != Kind::Verbatim && previous.Type != Kind::SourceWhitespace &&
        previous.Type != Kind::SourceLineIndent) || previous.Owner != Open_.back() ||
        previous.Origin.Identity != NoIdentity || previous.Origin.Begin == InvalidId ||
        FlatText(previous) != Source_.substr(previous.Origin.Begin, previous.Origin.End - previous.Origin.Begin) ||
        FlatText(previous).find('\n') == std::string_view::npos ||
        FlatText(previous).find_first_not_of(" \t\r\n") != std::string_view::npos)
    {
        Fail(Error::InvalidLinePrefix);
        return;
    }
    Node node(Kind::ExpressionLine);
    Add(node);
}

void Document::ConditionalText(
    GroupId reference,
    std::string_view flat,
    std::string_view broken,
    Provenance origin,
    std::uint32_t fitWidthFloor)
{
    if (flat.size() >= InvalidId || broken.size() >= InvalidId) {
        Fail(Error::TooLarge);
        return;
    }
    if (Multiline(flat) || Multiline(broken)) {
        Fail(Error::MultilineText);
    }
    if (!CheckReference(reference) || !CheckOrigin(origin)) {
        return;
    }
    Node node(Kind::ConditionalText);
    node.Text.Flat = BorrowText(flat);
    node.Text.Broken = BorrowText(broken);
    node.Origin = origin;
    node.Text.Detail = fitWidthFloor;
    Add(node);
}

void Document::ConditionalGap(GroupId reference, GapSpec flat, GapSpec broken, AnchorId anchor, Provenance origin)
{
    if (flat.Kind != GapKind::Flat) {
        Fail(Error::InvalidConditionalGap);
    }
    if (broken.Kind != GapKind::Flat && broken.Kind != GapKind::Soft && broken.Kind != GapKind::Hard) {
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
    Node node(Kind::ConditionalGap);
    node.Gaps.Flat = flat;
    node.Gaps.Broken = broken;
    node.Gaps.Anchor = anchor;
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

Result Document::Render(Policy policy, std::pmr::memory_resource* resource) const
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
    if (policy.Width == 0 || policy.Width == Infinity || policy.TabWidth == 0 || policy.TabWidth > 32 ||
        (policy.LineEnding != Newline::LF && policy.LineEnding != Newline::CRLF) ||
        (policy.Indentation != IndentationKind::Spaces && policy.Indentation != IndentationKind::Tabs))
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
    if (policy.IndexedTokenMap) {
        result.Tokens.resize(policy.TokenCount);
    }
    const auto wantsMapping = [&] (Provenance origin, bool exact) {
        if (policy.SourceMap || (policy.TokenMap && origin.Identity != NoIdentity)) {
            return true;
        }
        if (!exact || origin.Begin == InvalidId || policy.TrackSource.empty()) {
            return false;
        }
        const auto first = std::lower_bound(
            policy.TrackSource.begin(),
            policy.TrackSource.end(),
            origin.Begin,
            [] (const Provenance& range, std::uint32_t begin) {
                return range.End <= begin;
            });
        return first != policy.TrackSource.end() && first->Begin < origin.End;
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
    // flat mode includes every closer and same-line continuation. These
    // summaries are unchanged: group suffixes preserve closer fitting and
    // cheaper following breaks, gap suffixes preserve Fill's next segment,
    // and owner-specific break/shape facts preserve child-only selection.
    // Text needs none of those specialized records. No fitting path rescans
    // an unbounded suffix; probes still use the logical-node work limit.
    struct Cache
    {
        Measure Flat;
        std::uint32_t Gap = InvalidId;
    };
    struct GroupCache
    {
        Measure FlatTail;
        Measure BrokenTail;
        std::uint32_t LastBreak = InvalidId;
        std::uint32_t OwnedLastBreak = InvalidId;
        // First reachable break cheaper than each nonzero penalty (1..3).
        std::array<std::uint32_t, 3> TailBreakPrefixes{Infinity, Infinity, Infinity};
        bool GuaranteedLine = false;
        bool ChildLayoutAllowed = false;
        bool ShapeSensitive = false;
    };
    struct GapCache
    {
        Measure FitTail;
        std::uint32_t ExpandedWidth;
    };
    static_assert(sizeof(Cache) <= 16);
    static_assert(sizeof(GroupCache) <= 40);
    static_assert(sizeof(GapCache) <= 16);
    std::pmr::vector<Cache> cache(Nodes_.size(), resource);
    std::pmr::vector<GroupCache> groupCache(Groups_.size(), resource);
    std::pmr::vector<GapCache> gapCache(resource);
    gapCache.reserve(RenderGaps_);
    struct SummaryFrame
    {
        Measure Flat;
        Measure Broken;
        std::array<std::uint32_t, 3> BreakPrefixes{Infinity, Infinity, Infinity};
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
        if (mappingRequested &&
            (policy.SourceMap || !policy.TrackSource.empty() || node.Origin.Identity != NoIdentity) &&
            node.Type != Kind::Begin && node.Type != Kind::End && node.Type != Kind::Anchor &&
            node.Type != Kind::ExpressionLine && node.Type != Kind::SourceRun)
        {
            mappingCount += node.HasPrefix &&
                (policy.SourceMap || !policy.TrackSource.empty() || Prefix(node).Origin.Identity != NoIdentity)
                ? 2
                : 1;
            if (node.Type == Kind::SourceWhitespace) {
                // At most one copied run on either side of each changed atom.
                if (mappingCount > result.Map.max_size() ||
                    node.Text.Detail > (result.Map.max_size() - mappingCount) / 2)
                {
                    result.State = {Error::TooLarge, static_cast<std::uint32_t>(i)};
                    return result;
                }
                mappingCount += std::size_t(node.Text.Detail) * 2;
            }
            if (node.Type == Kind::SourceLineIndent || ((node.Type == Kind::Gap || node.Type == Kind::
                    ConditionalGap) && node.ComplexGap &&
                (GapPayloads_[node.Payload].Flat.IndentSourceBegin != InvalidId ||
                    GapPayloads_[node.Payload].Broken.IndentSourceBegin != InvalidId)))
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
        Measure flexible;
        std::uint8_t firstBreakPenalty = 0;
        std::array<std::uint32_t, 3> childBreakPrefixes{
            Infinity,
            Infinity,
            Infinity,
        };
        std::uint32_t lastBreak = InvalidId;
        std::uint32_t ownedBreak = InvalidId;
        std::uint32_t expandedGapWidth = 0;
        bool guaranteedLine = false;
        if (node.Type == Kind::Begin) {
            auto& groupEntry = groupCache[Group(node).Reference];
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
            const bool forced = Group(node).ForceBreak && groupEntry.OwnedLastBreak != InvalidId;
            // Decide eligibility per owner, not for its entire subtree. An
            // outer list can require owned lines while an inner chain keeps
            // its links flat and lets a deeper argument list break.
            groupEntry.ChildLayoutAllowed = Group(node).Mode == GroupMode::Fill && Group(node).ChildBreaks && !forced &&
                !summary.OwnedConditionalLine;
            broken = flat.Line || forced ? summary.Broken : flat;
            if (forced) {
                flat = broken;
                flat.Line = true;
                guaranteedLine = true;
            }
            flexible = broken;
            if (!flat.Line && !forced && groupEntry.OwnedLastBreak != InvalidId) {
                flexible = summary.Broken;
                firstBreakPenalty = Group(node).BreakPenalty;
            }
            // A wrapper without owned breaks still exposes cheaper breaks
            // in its descendants. Do not bypass coupled owned boundaries.
            if (groupEntry.OwnedLastBreak == InvalidId || groupEntry.ChildLayoutAllowed) {
                childBreakPrefixes = summary.BreakPrefixes;
            }
            summary = summaries.back();
            summaries.pop_back();
        } else if (node.Type == Kind::SourceRun) {
            for (auto p = node.Run.Begin; p < node.Run.End; ++p) {
                const auto& part = RunParts_[p];
                const auto measured = part.Type == RunKind::Spaces
                    ? Measure{part.Spaces, false, false}
                    : part.Plain
                        ? Measure{part.Origin.End - part.Origin.Begin, false, false}
                        : MeasureText(
                            Source_.substr(part.Origin.Begin, part.Origin.End - part.Origin.Begin),
                            policy.TabWidth);
                flat = Join(flat, measured);
                if (mappingRequested &&
                    (policy.SourceMap || !policy.TrackSource.empty() || part.Origin.Identity != NoIdentity))
                {
                    ++mappingCount;
                }
            }
            broken = flat;
        } else if (node.Type == Kind::Text || node.Type == Kind::Verbatim || node.Type == Kind::ConditionalText ||
            node.Type == Kind::SourceLineIndent || node.Type == Kind::SourceWhitespace ||
            node.Type == Kind::ReindentedText)
        {
            // Fixed prefixes establish shape like Verbatim, but never publish
            // lastBreak/ownedBreak. Their indentation follows a guaranteed
            // line, and emission counts it before any following child fit.
            flat = node.Plain
                ? Measure{node.Origin.End - node.Origin.Begin, false, false}
                : MeasureText(FlatText(node), policy.TabWidth);
            if (node.Type == Kind::ConditionalText) {
                summary.OwnedShapeChoice = summary.OwnedShapeChoice || FlatText(node) != BrokenText(node);
                flat.Prefix = std::max(flat.Prefix, TextPayloads_[node.Payload].Detail);
            }
            broken = node.Type == Kind::ConditionalText ? MeasureText(BrokenText(node), policy.TabWidth) : flat;
            if (node.HasPrefix) {
                const auto prefix = MeasureFlatGap(Prefix(node).Gap, Source_, policy.TabWidth);
                flat = Join(prefix, flat);
                broken = Join(prefix, broken);
            }
            guaranteedLine = flat.Line;
        } else if (node.Type == Kind::Gap || node.Type == Kind::ConditionalGap) {
            const auto flatSpec = GapFor(node);
            const auto& brokenSpec = node.Type == Kind::ConditionalGap ? GapPayloads_[node.Payload].Broken : flatSpec;
            flat = MeasureFlatGap(flatSpec, Source_, policy.TabWidth);
            const auto expandedFlat = node.Type == Kind::Gap
                ? flat
                : MeasureFlatGap(brokenSpec, Source_, policy.TabWidth);
            // Reuse each flat whitespace measurement for fill and summaries.
            broken =
                node.Owner != InvalidId && brokenSpec.Kind == GapKind::Soft ? Measure{0, true, true} : expandedFlat;
            expandedGapWidth = expandedFlat.Width();
            if (node.Owner != InvalidId && brokenSpec.Kind != GapKind::Flat) {
                ownedBreak = static_cast<std::uint32_t>(i);
            }
            guaranteedLine = brokenSpec.Kind == GapKind::Hard;
            if (node.Type == Kind::ConditionalGap) {
                summary.OwnedConditionalLine = summary.OwnedConditionalLine || guaranteedLine;
                summary.OwnedShapeChoice = summary.OwnedShapeChoice || flatSpec.Spaces != brokenSpec.Spaces ||
                    flatSpec.FlatSourceBegin != brokenSpec.FlatSourceBegin ||
                    flatSpec.FlatSourceEnd != brokenSpec.FlatSourceEnd;
            }
            if (node.Owner != InvalidId &&
                (brokenSpec.Kind == GapKind::Soft || (node.Type == Kind::ConditionalGap && guaranteedLine)))
            {
                lastBreak = static_cast<std::uint32_t>(i);
            }
        }
        if (node.Type != Kind::Begin) {
            flexible = broken;
            if (!flat.Line && node.Owner != InvalidId) {
                firstBreakPenalty = Group(Nodes_[Groups_[node.Owner]]).BreakPenalty;
            }
        }
        entry.Flat = flat;
        if (node.Type == Kind::Begin) {
            auto& groupEntry = groupCache[Group(node).Reference];
            groupEntry.FlatTail = summary.Flat;
            groupEntry.BrokenTail = summary.Broken;
            groupEntry.TailBreakPrefixes = summary.BreakPrefixes;
        } else if (node.Type == Kind::Gap || node.Type == Kind::ConditionalGap) {
            entry.Gap = static_cast<std::uint32_t>(gapCache.size());
            // Soft gaps break only in the owner's broken shape. Conditional
            // gaps therefore use the broken alternative's fitting policy.
            const auto gap = GapFor(node, true);
            gapCache.push_back({gap.FitFlatTail ? summary.Flat : summary.Broken, expandedGapWidth});
        }
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
        if (summary.LastBreak == InvalidId) {
            summary.LastBreak = lastBreak;
        }
        if (summary.OwnedLastBreak == InvalidId) {
            summary.OwnedLastBreak = ownedBreak;
        }
        summary.GuaranteedLine = summary.GuaranteedLine || guaranteedLine;
    }

    result.Broken.resize(Groups_.size(), 0);
    if (mappingRequested) {
        result.Map.reserve(
            policy.IndexedTokenMap && !policy.SourceMap && !policy.TokenMap ? policy.TrackSource.size() : mappingCount);
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
    struct GroupNode
    {
        const GroupData& Group;
    };
    auto structuralBase = [&] (const GroupNode& node) {
        if (node.Group.FixedBase != InvalidId) {
            return node.Group.FixedBase;
        }
        if (node.Group.LineBase) {
            // A compact body can have a logical indent before any line break.
            // Otherwise continuation starters use the callee's physical line.
            return node.Group.Continuation
                ? std::max(
                    frame.HasContinuation ? frame.ContinuationBase : frame.Indent - frame.PendingExpressionIndent,
                    lineIndent)
                : lineIndent;
        }
        if (frame.HasContinuation) {
            if (frame.RealizedContinuationLine) {
                return frame.RealizedContinuationIndent;
            }
            if (!node.Group.Continuation) {
                return frame.ContinuationBase;
            }
        }
        return frame.Indent - frame.PendingExpressionIndent;
    };
    auto sharesRealizedContinuation = [&] (const GroupNode& node) {
        return !node.Group.StructuralBase && frame.StructuralBase && frame.RealizedContinuationLine;
    };
    auto sharesContinuation = [&] (const GroupNode& node) {
        // A structural consumer already indented its contents. Contributors
        // reuse that realized level; another contributor starting on a later
        // line still creates a level, preserving expression precedence.
        return node.Group.Continuation && !node.Group.LineBase && frame.HasContinuation &&
            ((lines == frame.ContinuationStartLine && !(node.Group.StructuralBase && frame.RealizedContinuationLine)) ||
                sharesRealizedContinuation(node));
    };
    auto advance = [&] (std::string_view text, bool plain = false) {
        if (plain && !linePrefix) {
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
    auto groupTail = [&] (const GroupData& group, const GroupCache& entry) {
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
        auto indent = frame.Indent;
        if (id != InvalidId) {
            const auto& anchor = anchors[id];
            const GroupNode spec{Group(Nodes_[anchor.Node])};
            indent = anchor.Column;
            if (spec.Group.Alignment == AnchorKind::DelimiterColumn) {
                const auto minimum = std::min(
                    AddWidth(
                        anchor.Block,
                        anchor.SharedOffset != InvalidId && spec.Group.Minimum
                        ? anchor.SharedOffset
                        : spec.Group.Minimum),
                    Infinity - 1);
                indent = std::max(indent, minimum);
                if (indent >= policy.Width || requiredWidth > policy.Width - indent) {
                    indent = Offset(
                        anchor.Block,
                        anchor.SharedOffset != InvalidId && spec.Group.Fallback == int32_t(spec.Group.Minimum)
                        ? int32_t(anchor.SharedOffset)
                        : spec.Group.Fallback);
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
    const auto outputRoom = [&] (std::size_t count) {
        if (!result.State) {
            return false;
        }
        if (!detail::FitsOutput(result.Text.size(), count) || count > result.Text.max_size() - result.Text.size()) {
            result.State = {Error::TooLarge};
            return false;
        }
        return true;
    };
    auto append = [&] (std::string_view text) {
        if (!probing && outputRoom(text.size())) {
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
        const auto bytes = policy.Indentation == IndentationKind::Tabs
            ? indent / policy.TabWidth + indent % policy.TabWidth
            : indent;
        if (!outputRoom(bytes)) {
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
        result.State = {Error::IndentationOverflow, static_cast<std::uint32_t>(i)};
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
            const GroupNode node{Group(Nodes_[i])};
            const auto& groupEntry = groupCache[node.Group.Reference];
            const auto tail = groupTail(node.Group, groupEntry);
            const bool mandatory = frame.Mandatory != InvalidId && frame.Mandatory >= i &&
                frame.Mandatory <= node.Group.Match && lines == frame.StartLine;
            const bool broken =
                mandatory || (!frame.Flat && entry.Flat.CanBreak && !fits(AddWidth(entry.Flat.Width(), tail)));
            const bool keepOwnedFlat = broken && childLayoutDepth && groupEntry.ChildLayoutAllowed &&
                i <= childLayoutEnd && frames.size() - childLayoutBase < childLayoutDepth;
            result.Broken[node.Group.Reference] = broken;
            frames.push_back(frame);
            const bool forceOwned = node.Group.ForceBreak && groupEntry.OwnedLastBreak != InvalidId;
            auto base = frame.Indent;
            auto pending = frame.PendingExpressionIndent;
            auto reversible = frame.ReversibleIndent;
            if (node.Group.StructuralBase) {
                if (!reversible || pending > base) {
                    indentationError(i);
                    return false;
                }
                base = structuralBase(node);
                pending = 0;
                reversible = true;
            }
            auto indent = node.Group.Indent;
            const bool shared = sharesContinuation(node);
            const bool newLineBase =
                node.Group.Continuation && !shared && frame.HasContinuation && lines != frame.ContinuationStartLine;
            if (newLineBase) {
                // A new starter uses this physical line, not an unrealized
                // ancestor offset or a previous child's continuation line.
                base = lineIndent;
                pending = 0;
            }
            if (shared) {
                const bool realized = sharesRealizedContinuation(node);
                const auto continuationIndent = realized ? frame.RealizedContinuationIndent : frame.Indent;
                base = realized ? std::min(frame.ContinuationBase, continuationIndent) : frame.ContinuationBase;
                if (continuationIndent < base || continuationIndent - base > uint32_t(INT32_MAX)) {
                    indentationError(i);
                    return false;
                }
                indent = int32_t(continuationIndent - base);
            }
            const auto logical = std::int64_t(base) + indent;
            if (node.Group.ExpressionContinuation) {
                const auto bias = std::uint64_t(pending) + static_cast<std::uint32_t>(node.Group.Indent);
                if (!reversible || logical >= Infinity || bias >= Infinity) {
                    indentationError(i);
                    return false;
                }
                if (node.Group.Continuation) {
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
                    : broken && !keepOwnedFlat && !groupEntry.GuaranteedLine ? groupEntry.LastBreak : InvalidId,
                lines,
                !broken,
                !broken || keepOwnedFlat,
                forceOwned,
                false,
                pending,
                node.Group.ExpressionContinuation,
                node.Group.StructuralBase,
                false,
                reversible,
                node.Group.Continuation ? base : frame.ContinuationBase,
                node.Group.Continuation || (!node.Group.StructuralBase && frame.HasContinuation),
                (shared || (!node.Group.Continuation && !node.Group.StructuralBase)) && frame.RealizedContinuationLine,
                frame.RealizedContinuationIndent,
                node.Group.Continuation && !shared ? lines : frame.ContinuationStartLine,
            };
            return true;
        }
        if (node.Type == Kind::End) {
            GroupData end;
            end.Match = node.End.Match;
            end.Reference = node.End.Reference;
            const GroupNode node{end};
            // Shape consumers still observe multiline descendants, even when
            // this group keeps every owned gap flat.
            const bool multiline = lines != frame.StartLine;
            if (probing && groupCache[node.Group.Reference].ShapeSensitive &&
                bool(result.Broken[node.Group.Reference]) != multiline)
            {
                // A later sibling may break without making this owner
                // multiline. Do not accept conditional text or spacing
                // chosen from that sibling's prospective shape.
                return false;
            }
            result.Broken[node.Group.Reference] = multiline;
            if (probing) {
                if (multiline) {
                    ++probeBrokenGroups;
                    probeBreakPenalty += frame.FlatChildPenalty;
                } else if (const auto penalty = Group(Nodes_[node.Group.Match]).FlatInBrokenParentPenalty; penalty != 0)
                {
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
            return true;
        }
        if (node.Type == Kind::Anchor) {
            const GroupNode node{Group(Nodes_[i])};
            const bool shared = sharesContinuation(node);
            auto base = frame.Indent;
            if (node.Group.StructuralBase) {
                if (!frame.ReversibleIndent || frame.PendingExpressionIndent > base) {
                    indentationError(i);
                    return false;
                }
                base = structuralBase(node);
            }
            auto offset = node.Group.Indent;
            auto sharedOffset = InvalidId;
            if (node.Group.Continuation && !shared && frame.HasContinuation && lines != frame.ContinuationStartLine) {
                base = lineIndent;
            }
            if (shared) {
                const bool realized = sharesRealizedContinuation(node);
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
            if (shared && !sharesRealizedContinuation(node) && node.Group.Reference != InvalidId) {
                anchors[node.Group.Anchor] = anchors[node.Group.Reference];
                return true;
            }
            const auto target = Offset(node.Group.Alignment == AnchorKind::BlockRelative ? base : column, offset);
            anchors[node.Group.Anchor] = {target, base, static_cast<std::uint32_t>(i), sharedOffset};
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
        if (node.Type == Kind::SourceRun) {
            for (auto p = node.Run.Begin; p < node.Run.End; ++p) {
                const auto& part = RunParts_[p];
                const auto begin = static_cast<std::uint32_t>(result.Text.size());
                const bool continuingIndent =
                    frame.RealizedContinuationLine && column == frame.RealizedContinuationIndent;
                if (part.Type == RunKind::Spaces) {
                    if (!probing && outputRoom(part.Spaces)) {
                        result.Text.append(part.Spaces, ' ');
                    }
                    column = AddWidth(column, part.Spaces);
                    if (linePrefix) {
                        lineIndent = column;
                    }
                    if (continuingIndent) {
                        realizeContinuation();
                    }
                } else {
                    const auto text = Source_.substr(part.Origin.Begin, part.Origin.End - part.Origin.Begin);
                    append(text);
                    advance(text, part.Plain || entry.Flat.Plain);
                    if (continuingIndent &&
                        (part.Type == RunKind::SourceGap ||
                            (part.Type == RunKind::Verbatim && part.Origin.Identity == NoIdentity &&
                                text.find_first_not_of(" \t\r\n") == std::string_view::npos)))
                    {
                        realizeContinuation();
                    }
                }
                if (!result.State || (probing && column > policy.Width)) {
                    return false;
                }
                if (mappingRequested && !probing) {
                    recordMapping({
                        begin,
                        static_cast<std::uint32_t>(result.Text.size()),
                        part.Origin,
                        part.Type != RunKind::Spaces,
                        part.Type == RunKind::Verbatim,
                    });
                }
            }
            return true;
        }
        auto outputBegin = static_cast<std::uint32_t>(result.Text.size());
        auto emittedOrigin = node.Origin;
        bool exact = node.Exact;
        const bool ownerBroken = node.Owner != InvalidId && result.Broken[node.Owner];
        if (node.HasPrefix) {
            const auto& gap = Prefix(node).Gap;
            auto prefixOrigin = Prefix(node).Origin;
            const bool continuingIndent = frame.RealizedContinuationLine && column == frame.RealizedContinuationIndent;
            const bool prefixExact = gap.FlatSourceBegin != InvalidId;
            if (prefixExact) {
                const auto text = Source_.substr(gap.FlatSourceBegin, gap.FlatSourceEnd - gap.FlatSourceBegin);
                append(text);
                advance(text);
                prefixOrigin.Begin = gap.FlatSourceBegin;
                prefixOrigin.End = gap.FlatSourceEnd;
            } else {
                if (!probing && outputRoom(gap.Spaces)) {
                    result.Text.append(gap.Spaces, ' ');
                }
                column = AddWidth(column, gap.Spaces);
                if (linePrefix) {
                    lineIndent = column;
                }
            }
            if (continuingIndent) {
                realizeContinuation();
            }
            // Preserve the probe boundary between the original gap and text.
            if (!result.State || (probing && column > policy.Width)) {
                return false;
            }
            if (mappingRequested && !probing) {
                recordMapping(
                    {outputBegin, static_cast<std::uint32_t>(result.Text.size()), prefixOrigin, prefixExact, false});
            }
            outputBegin = static_cast<std::uint32_t>(result.Text.size());
        }
        if (node.Type == Kind::SourceWhitespace) {
            const bool targetCRLF = node.LineEnding == GapNewline::CRLF;
            const auto text = FlatText(node);
            if (text.size() > result.Text.max_size() ||
                (targetCRLF && node.Text.Detail > result.Text.max_size() - text.size()))
            {
                result.State = {Error::TooLarge, static_cast<std::uint32_t>(i)};
                return false;
            }
            const auto size = targetCRLF
                ? text.size() + std::size_t(node.Text.Detail)
                : text.size(); // Incoming boundaries may suppress contractions.
            if (!outputRoom(size)) {
                result.State = {Error::TooLarge, static_cast<std::uint32_t>(i)};
                return false;
            }
            std::size_t run = 0;
            const auto copy = [&] (std::size_t end) {
                if (run == end) {
                    return;
                }
                const auto begin = static_cast<std::uint32_t>(result.Text.size());
                result.Text.append(text.substr(run, end - run));
                if (mappingRequested) {
                    recordMapping({
                        begin,
                        static_cast<std::uint32_t>(result.Text.size()),
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
                    result.State = {Error::InvalidSourceRange, static_cast<std::uint32_t>(i)};
                    return false;
                }
                if (!ChangesWhitespaceAtom(Source_, node.Origin.Begin + cursor, targetCRLF) || (crlf && incomingCR)) {
                    continue;
                }
                copy(atom);
                const auto begin = static_cast<std::uint32_t>(result.Text.size());
                result.Text.append(targetCRLF ? "\r\n" : "\n");
                if (mappingRequested) {
                    recordMapping({
                        begin,
                        static_cast<std::uint32_t>(result.Text.size()),
                        {
                            node.Origin.Begin + static_cast<std::uint32_t>(atom),
                            node.Origin.Begin + static_cast<std::uint32_t>(cursor + 1),
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
            advance(text, entry.Flat.Plain);
            if (entry.Flat.Line || continuingIndent) {
                realizeContinuation();
            }
            return true;
        }
        if (node.Type == Kind::ReindentedText) {
            const auto text = FlatText(node);
            const auto originalColumn =
                SourceColumn(Source_.substr(node.Text.Detail, node.Origin.Begin - node.Text.Detail), policy.TabWidth);
            const auto shift = std::int64_t(column) - originalColumn;
            if (shift == 0) {
                append(text);
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
                        append(copied);
                        advance(copied);
                        emitIndent(static_cast<std::uint32_t>(shifted));
                        exact = false;
                        run = end;
                    }
                    cursor = end - 1;
                }
                append(text.substr(run));
                advance(text.substr(run));
            }
        } else if (node.Type == Kind::Text || node.Type == Kind::Verbatim || node.Type == Kind::ConditionalText) {
            auto text = node.Type == Kind::ConditionalText && ownerBroken ? BrokenText(node) : FlatText(node);
            const bool continuingIndent = frame.RealizedContinuationLine && column == frame.RealizedContinuationIndent;
            append(text);
            advance(text, node.Type != Kind::ConditionalText && entry.Flat.Plain);
            if (frame.HasContinuation && node.Type == Kind::Verbatim && node.Origin.Identity == NoIdentity &&
                (entry.Flat.Line || continuingIndent) && text.find_first_not_of(" \t\r\n") == std::string_view::npos)
            {
                realizeContinuation();
            }
        } else if (node.Type == Kind::SourceLineIndent) {
            const auto& gaps = GapPayloads_[node.Payload];
            append(FlatText(node));
            advance(FlatText(node));
            if (mappingRequested && !probing) {
                recordMapping({outputBegin, static_cast<std::uint32_t>(result.Text.size()), node.Origin, true, true});
            }
            outputBegin = static_cast<std::uint32_t>(result.Text.size());
            emittedOrigin = {gaps.Flat.IndentSourceBegin, gaps.Flat.IndentSourceEnd, NoIdentity};
            emitIndent(resolveIndent(gaps.Anchor, gaps.Flat.RequiredWidth));
            realizeContinuation();
            // Do not set frame.OwnedLine: only editable gaps satisfy
            // ForceOwned.
        } else {
            const auto gap = GapFor(node, node.Type == Kind::ConditionalGap && ownerBroken);
            const auto anchor = GapAnchor(node);
            const auto& gapEntry = gapCache[entry.Gap];
            if (frame.OwnedFlat && ownerBroken && node.Type == Kind::ConditionalGap && gap.Kind == GapKind::Hard) {
                // Conditional topology belongs to the owner's broken shape;
                // it cannot be suppressed by a child-only alternative.
                return false;
            }
            const bool continuingIndent = frame.RealizedContinuationLine && column == frame.RealizedContinuationIndent;
            bool line = gap.Kind == GapKind::Hard;
            if (gap.Kind == GapKind::Soft && ownerBroken && !frame.OwnedFlat) {
                if (Group(Nodes_[Groups_[node.Owner]]).Mode == GroupMode::Consistent) {
                    line = true;
                } else {
                    line = !fits(AddWidth(
                        gapEntry.ExpandedWidth,
                        gap.FitNextSegment ? gap.RequiredWidth : tailWidth(gapEntry.FitTail))) ||
                        (frame.Mandatory == i && (frame.ForceOwned ? !frame.OwnedLine : lines == frame.StartLine));
                }
            }
            if (line) {
                if (probing && node.Owner != InvalidId) {
                    probeBreakPenalty +=
                        std::size_t(gap.NewlineCount) * Group(Nodes_[Groups_[node.Owner]]).BreakPenalty;
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
                        recordMapping(
                            {outputBegin, static_cast<std::uint32_t>(result.Text.size()), node.Origin, false, false});
                    }
                    outputBegin = static_cast<std::uint32_t>(result.Text.size());
                    auto text = Source_.substr(gap.IndentSourceBegin, gap.IndentSourceEnd - gap.IndentSourceBegin);
                    append(text);
                    column = 0;
                    advance(text);
                    emittedOrigin.Begin = gap.IndentSourceBegin;
                    emittedOrigin.End = gap.IndentSourceEnd;
                    exact = true;
                } else {
                    emitIndent(resolveIndent(anchor, gap.RequiredWidth));
                }
            } else if (gap.FlatSourceBegin != InvalidId) {
                auto text = Source_.substr(gap.FlatSourceBegin, gap.FlatSourceEnd - gap.FlatSourceBegin);
                append(text);
                advance(text);
                emittedOrigin.Begin = gap.FlatSourceBegin;
                emittedOrigin.End = gap.FlatSourceEnd;
                exact = true;
            } else {
                if (!probing) {
                    if (outputRoom(gap.Spaces)) {
                        result.Text.append(gap.Spaces, ' ');
                    }
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
        if (!result.State) {
            return false;
        }
        if (mappingRequested && !probing) {
            recordMapping({
                outputBegin,
                static_cast<std::uint32_t>(result.Text.size()),
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
        if (!layoutSelected && node.Type == Kind::Begin && groupCache[Group(node).Reference].ChildLayoutAllowed &&
            groupCache[Group(node).Reference].OwnedLastBreak != InvalidId && !frame.Flat && !entry.Flat.Line &&
            entry.Flat.CanBreak &&
            !fits(AddWidth(entry.Flat.Width(), groupTail(Group(node), groupCache[Group(node).Reference]))))
        {
            const GroupNode node{Group(Nodes_[i])};
            const auto& groupEntry = groupCache[node.Group.Reference];
            // Compare parent breaks with bounded child-only layouts. Minimize
            // penalized newlines, then lines, then multiline groups; ties keep
            // the parent layout. Oversized groups use ordinary group fitting.
            constexpr std::size_t MaxProbeNodes = 256;
            constexpr std::size_t MaxProbeDepth = 4;
            bool eligible = Nodes_[node.Group.Match].End.LogicalPosition - node.Group.LogicalPosition < MaxProbeNodes;
            bool child = false;
            std::size_t candidateDepth = 1;
            std::size_t nesting = 1;
            if (eligible) {
                for (std::size_t j = i + 1; j < node.Group.Match; ++j) {
                    const auto& part = Nodes_[j];
                    if (part.Type == Kind::Begin) {
                        child = true;
                        if (groupCache[Group(part).Reference].ChildLayoutAllowed) {
                            candidateDepth = std::max(candidateDepth, std::min(MaxProbeDepth, nesting + 1));
                        }
                        ++nesting;
                    } else if (part.Type == Kind::End) {
                        --nesting;
                    }
                    if (part.Type == Kind::SourceWhitespace || part.Type == Kind::SourceLineIndent ||
                        (part.Type != Kind::Begin && cache[j].Flat.Line) || FlatText(part).size() > 4096 ||
                        BrokenText(part).size() > 4096 || (part.Type == Kind::SourceRun && part.Run.MaxText > 4096))
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
                const auto tail = groupTail(node.Group, groupEntry);
                childLayoutBase = savedDepth;
                childLayoutEnd = node.Group.Match;
                std::size_t bestCost = 0;
                std::size_t bestLines = 0;
                std::size_t bestGroups = 0;
                std::size_t bestDepth = 0;
                // Trial zero is the normal parent-breaking layout. The other
                // trials retain successively shallower owned-flat prefixes.
                for (std::size_t trial = 0; trial <= candidateDepth; ++trial) {
                    const auto depth = trial == 0 ? 0 : candidateDepth - trial + 1;
                    childLayoutDepth = depth;
                    probeBrokenGroups = 0;
                    probeBreakPenalty = 0;
                    probing = true;
                    bool fitsCandidate = true;
                    for (std::size_t j = i; j <= node.Group.Match; ++j) {
                        if (!renderNode(j) || (layoutSelected && lines - savedLines + probeBreakPenalty > bestCost)) {
                            // Cost is nonnegative and only grows. A candidate
                            // already worse than the incumbent cannot win.
                            fitsCandidate = false;
                            break;
                        }
                    }
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
                    // emitted nodes. Ancestor slots are read-only in a probe.
                    for (std::size_t j = i; j <= node.Group.Match; ++j) {
                        const auto& part = Nodes_[j];
                        if (part.Type == Kind::Begin) {
                            result.Broken[Group(part).Reference] = 0;
                        } else if (part.Type == Kind::Anchor) {
                            anchors[Group(part).Anchor] = {};
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
                result.State = {Error::InvalidPolicy, static_cast<std::uint32_t>(i)};
            }
            return result;
        }
    }
    return result;
}

} // namespace document
