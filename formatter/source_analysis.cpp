#include "source_analysis.hpp"

#include <algorithm>
#include <cassert>
#include <limits>
#include <stdexcept>

namespace formatter {
namespace {

using TK = tokenizer::TokenKind;
using Offset = parser::SourceOffset;
constexpr auto NoToken = std::numeric_limits<parser::TokenIndex>::max();

bool Whitespace(std::string_view text)
{
    return text.find_first_not_of(" \t\r\n\f\v") == std::string_view::npos;
}

bool Word(TK kind, std::string_view text)
{
    // Alternative operators are identifier spellings with operator kinds.
    return kind == TK::Identifier || kind >= TK::Keyword_alignas ||
        (kind >= TK::Operator_OpenBrace && kind <= TK::Operator_Comma &&
            !text.empty() && text.front() >= 'a' && text.front() <= 'z');
}

bool Literal(TK kind)
{
    return kind >= TK::Literal_UserDefinedInteger && kind <= TK::Literal_String;
}

bool Number(TK kind)
{
    return Literal(kind) && kind < TK::Literal_Character;
}

bool Punctuation(TK kind)
{
    return kind >= TK::Operator_OpenBrace && kind <= TK::Operator_Comma;
}

bool PlainDelimiter(std::string_view text)
{
    return text.size() == 1 && std::string_view("{}[]();,").find(
        text[0]) != std::string_view::npos;
}

// Conservative maximal-munch constraints, based on the tokenizer's categories
// and spellings. Extra separation is intentional: it handles comments,
// digraphs, <:: lookahead, ellipses spanning three tokens, literal prefixes and
// suffixes without allocating/retokenizing each pair. Template > > still needs
// separation: accepting >> would relax exact lexical identity, even if C++ can
// split that token contextually. Roles are used separately for grammar safety.
bool NeedsSeparation(const parser::SyntaxTree& tree, parser::TokenIndex right)
{
    const auto left = right - 1;
    const auto a = tree.Kind(left);
    const auto b = tree.Kind(right);
    const auto first = tree.TokenText(left);
    const auto second = tree.TokenText(right);
    if ((Word(a, first) || Literal(a)) && (Word(b, second) || Literal(b))) {
        return true;
    }
    if ((Number(a) && (second.starts_with(".") ||
        second.starts_with("+") || second.starts_with("-"))) ||
        (first.ends_with(".") && Number(b)))
    {
        return true;
    }
    if (Punctuation(a) && Punctuation(b)) {
        return !PlainDelimiter(first) && !PlainDelimiter(second);
    }
    // A slash before a comment can turn a block comment into a line comment.
    return a == TK::Operator_Divide && b == TK::Comment;
}

bool Recovery(parser::NodeKind kind)
{
    using NK = parser::NodeKind;
    return kind == NK::UnparsedSyntax || kind == NK::MissingToken ||
        kind == NK::UnexpectedToken || kind == NK::Error;
}

RegionStatus Combine(RegionStatus a, RegionStatus b)
{
    return static_cast<RegionStatus>(
        static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

} // namespace

bool TokenGapNeedsSeparation(
    const parser::SyntaxTree& tree,
    parser::TokenIndex right)
{
    assert(right > 0 && right < tree.TokenCount());
    return NeedsSeparation(tree, right);
}

parser::SourceOffset CheckedSourceAnalysisSize(size_t size)
{
    if (size > util::MaxSourceSize) {
        throw std::length_error("source analysis input exceeds maximum size");
    }
    return static_cast<parser::SourceOffset>(size);
}

SourceAnalysis::SourceAnalysis(std::pmr::memory_resource& fileMemory)
    : Gaps_(&fileMemory)
    , Protection_(&fileMemory)
    , Regions_(&fileMemory)
{ }

bool SourceAnalysis::IntersectsImmutable(parser::SourceRange source) const
{
    assert(source.Begin <= source.End);
    auto first = std::lower_bound(
        Protection_.begin(),
        Protection_.end(),
        source.Begin,
        [] (const ProtectedSourceRange& range, Offset begin) {
            return range.Source.End <= begin;
        });
    return first != Protection_.end() &&
        (source.Empty()
            ? first->Source.Begin <= source.Begin
            : first->Source.Begin < source.End);
}

bool SourceAnalysis::CanReplaceGap(GapId id, std::string_view replacement) const
{
    if (id >= Gaps_.size()) {
        return false;
    }
    const auto& gap = Gaps_[id];
    if (replacement == gap.Original) {
        return true;
    }
    if (gap.Preserve || !Whitespace(replacement) ||
        (gap.Separation == GapSeparation::Required && replacement.empty()))
    {
        return false;
    }
    if (gap.MustStartWithLineFeed && !replacement.starts_with('\n')) {
        return false;
    }
    const auto newline = replacement.find('\n') != std::string_view::npos;
    return (gap.LineBoundary != GapLineBoundary::Required || newline) &&
        (gap.LineBoundary != GapLineBoundary::Forbidden || !newline);
}

SourceAnalysis BuildSourceAnalysis(
    const parser::SyntaxTree& tree,
    const SourceAnalysisOptions& options,
    std::pmr::memory_resource& fileMemory,
    std::pmr::memory_resource& scratchMemory)
{
    const auto source = tree.Source();
    const auto size = CheckedSourceAnalysisSize(source.size());
    SourceAnalysis result(fileMemory);
    const auto count = tree.TokenCount();
    result.Gaps_.reserve(static_cast<size_t>(count) + 1);
    auto protect = [&] (Offset begin, Offset end, ProtectionReason reason) {
        assert(begin <= end && end <= size);
        if (begin != end) {
            result.Protection_.push_back({{begin, end}, reason});
        }
    };
    struct Line
    {
        Offset Begin;
        Offset FirstContent;
        Offset ContentEnd;
        bool Continued;
    };
    std::pmr::vector<Line> lines(&scratchMemory);
    lines.push_back({0, size, 0, false});
    Offset logicalBegin = 0;
    bool splice = false;
    for (Offset i = 0; i < size; ++i) {
        auto& line = lines.back();
        if (source[i] != '\n') {
            // Matches Tokenizer::TrySkipNewline's horizontal whitespace.
            // Cache both marker boundaries once; candidate comments must not
            // rescan a shared, arbitrarily long physical-line prefix/suffix.
            if (std::string_view(" \t\r\f\v").find(source[i]) ==
                std::string_view::npos)
            {
                if (line.FirstContent == size) {
                    line.FirstContent = i;
                }
                line.ContentEnd = i + 1;
            }
            continue;
        }
        const auto continued = line.ContentEnd > line.Begin &&
            source[line.ContentEnd - 1] == '\\';
        splice |= continued;
        lines.push_back({i + 1, size, i + 1, continued});
        if (!continued) {
            if (splice) {
                protect(logicalBegin, i + 1, ProtectionReason::LineSplice);
            }
            logicalBegin = i + 1;
            splice = false;
        }
    }
    if (splice) {
        protect(logicalBegin, size, ProtectionReason::LineSplice);
    }
    size_t lineIndex = 0;
    size_t endLineIndex = 0;
    std::pmr::vector<std::string_view> names(
        options.WhitespaceSensitiveMacros.begin(),
        options.WhitespaceSensitiveMacros.end(),
        &scratchMemory);
    std::sort(names.begin(), names.end());
    std::pmr::vector<uint32_t> recoveryPrefix(&scratchMemory);
    std::pmr::vector<uint32_t> ambiguousPrefix(&scratchMemory);
    recoveryPrefix.reserve(static_cast<size_t>(count) + 1);
    ambiguousPrefix.reserve(static_cast<size_t>(count) + 1);
    recoveryPrefix.push_back(0);
    ambiguousPrefix.push_back(0);
    Offset disabledBegin = 0;
    size_t disabledDepth = 0;
    // An outer configured invocation already protects all inner invocations.
    parser::TokenIndex macroEnd = 0;
    parser::TokenIndex pendingMacro = NoToken;
    for (parser::TokenIndex i = 0; i <= count; ++i) {
        const auto begin = i ? tree.Token(i - 1).Text.End : 0;
        const auto end = i < count ? tree.Token(i).Text.Begin : size;
        const auto original = source.substr(begin, end - begin);
        SourceGap gap{.Source = {begin, end}, .Original = original};
        gap.Preserve = !Whitespace(original);
        if (i && i < count && TokenGapNeedsSeparation(tree, i)) {
            gap.Separation = GapSeparation::Required;
        }
        // Preserve comment attachment topology, not its surrounding whitespace.
        // In particular a line comment must not swallow the following token.
        const auto commentOrDirective = [&] (parser::TokenIndex index) {
            return tree.Kind(index) == TK::Comment ||
                tree.Kind(index) == TK::PreprocessingDirective;
        };
        if ((i && commentOrDirective(i - 1)) ||
            (i < count && commentOrDirective(i)))
        {
            gap.LineBoundary = original.find('\n') == std::string_view::npos
                ? GapLineBoundary::Forbidden
                : GapLineBoundary::Required;
        }
        if (i && (tree.Kind(i - 1) == TK::PreprocessingDirective ||
            (tree.Kind(i - 1) == TK::Comment &&
                tree.TokenText(i - 1).starts_with("//"))))
        {
            gap.MustStartWithLineFeed = true;
            // No terminator at EOF: insertion could extend the line token,
            // even though its half-open protection ends at the insertion point.
            if (original.empty()) {
                gap.Preserve = true;
            }
        }
        if (i < count && tree.Kind(i) == TK::Operator_Remainder) {
            // %: is a directive only at the beginning of a line in our lexer.
            gap.LineBoundary = original.find('\n') == std::string_view::npos
                ? GapLineBoundary::Forbidden
                : GapLineBoundary::Required;
        }
        if (i && i < count && Literal(tree.Kind(i - 1)) &&
            Word(tree.Kind(i), tree.TokenText(i)) && original.empty())
        {
            // The tokenizer leaves string/character user-defined suffixes as
            // identifiers. Adding whitespace changes grammar, not token kinds.
            gap.ContextSensitive = true;
            gap.Preserve = true;
        }
        if (i && (tree.TokenText(i - 1).ends_with(
            "\\") || tree.TokenText(i - 1).ends_with(
            "\\\r")))
        {
            gap.Preserve = true;
            gap.Reasons = ProtectionReason::LineSplice;
        }
        if (i && i < count &&
            ((tree.Kind(i - 1) == TK::Operator_OpenBracket &&
                tree.Kind(i) == TK::Operator_OpenBracket) ||
                (tree.Kind(i - 1) == TK::Operator_CloseBracket &&
                    tree.Kind(i) == TK::Operator_CloseBracket)))
        {
            gap.ContextSensitive = true;
            gap.Preserve = true;
        }
        result.Gaps_.push_back(gap);
        if (i == count) {
            break;
        }
        const auto token = tree.Token(i);
        const auto text = tree.TokenText(i);
        while (lineIndex + 1 < lines.size() &&
            lines[lineIndex + 1].Begin <= token.Text.Begin)
        {
            ++lineIndex;
        }
        while (endLineIndex + 1 < lines.size() &&
            lines[endLineIndex + 1].Begin <= token.Text.End)
        {
            ++endLineIndex;
        }
        const parser::SourceRange tokenLine{
            lines[lineIndex].Begin,
            endLineIndex + 1 < lines.size()
                ? lines[endLineIndex + 1].Begin
                : size,
        };
        recoveryPrefix.push_back(
            recoveryPrefix.back() + (token.Kind == TK::Invalid));
        ambiguousPrefix.push_back(ambiguousPrefix.back() +
            (tree.Role(i) == parser::PunctuationRole::AmbiguousOperator));
        if (token.Kind == TK::PreprocessingDirective) {
            protect(
                tokenLine.Begin,
                tokenLine.End,
                ProtectionReason::Directive);
        }
        if (token.Kind == TK::Invalid ||
            tree.Role(i) == parser::PunctuationRole::AmbiguousOperator)
        {
            protect(begin, i + 1 < count ? tree.Token(i + 1).Text.Begin : size,
                token.Kind == TK::Invalid
                    ? ProtectionReason::Recovery
                    : ProtectionReason::Ambiguous);
        }
        if (token.Kind == TK::Comment) {
            auto marker = text;
            if (marker.starts_with("//")) {
                while (!marker.empty() && (marker.back() ==
                    ' ' || marker.back() == '\t' || marker.back() == '\r'))
                {
                    marker.remove_suffix(1);
                }
            }
            const bool off = marker == "// hit-fmt off" ||
                marker == "/* hit-fmt off */" ||
                (options.HonorClangFormatDirectives &&
                    (marker == "// clang-format off" ||
                    marker == "/* clang-format off */"));
            const bool on = marker == "// hit-fmt on" ||
                marker == "/* hit-fmt on */" ||
                (options.HonorClangFormatDirectives && (marker ==
                    "// clang-format on" || marker == "/* clang-format on */"));
            if (off || on) {
                const auto range = tokenLine;
                if (lineIndex == endLineIndex &&
                    !lines[lineIndex].Continued &&
                    token.Text.Begin == lines[lineIndex].FirstContent &&
                    token.Text.End >= lines[lineIndex].ContentEnd)
                {
                    if (off && disabledDepth++ == 0) {
                        disabledBegin = range.Begin;
                    } else if (on && disabledDepth && --disabledDepth == 0) {
                        protect(
                            disabledBegin,
                            range.End,
                            ProtectionReason::Disabled);
                    }
                }
            }
        }
        if (i >= macroEnd && token.Kind == TK::Operator_OpenParenthesis &&
            pendingMacro != NoToken)
        {
            const auto close = tree.MatchingDelimiter(i);
            const auto balanced = close > i && close < count &&
                tree.Kind(close) == TK::Operator_CloseParenthesis;
            macroEnd = balanced ? close + 1 : count;
            // An unclosed invocation has no trustworthy local end; retain its
            // suffix, not the unrelated prefix or a whole-file rollback.
            protect(tree.Token(pendingMacro).Text.Begin, balanced
                ? tree.Token(close).Text.End
                : size,
                balanced
                    ? ProtectionReason::SensitiveMacro
                    : ProtectionReason::SensitiveMacro |
                        ProtectionReason::Recovery);
        }
        if (token.Kind != TK::Comment) {
            // Comments are preprocessing whitespace between a macro name and
            // its opening parenthesis; directives and other tokens are not.
            pendingMacro = i >= macroEnd && token.Kind == TK::Identifier &&
                std::binary_search(names.begin(), names.end(), text)
                ? i
                : NoToken;
        }
    }
    if (disabledDepth) {
        protect(disabledBegin, size, ProtectionReason::Disabled);
    }
    for (const auto range : options.ImmutableRanges) {
        protect(range.Begin, range.End, ProtectionReason::Explicit);
    }
    struct Frame
    {
        const parser::Node* Next;
        RegionId Id;
    };
    std::pmr::vector<Frame> stack(&scratchMemory);
    auto visit = [&] (const parser::Node& node, RegionId parent) {
        const auto id = static_cast<RegionId>(result.Regions_.size());
        const auto tokens = node.Tokens;
        const auto begin = tokens.Begin < count
            ? tree.Token(tokens.Begin).Text.Begin
            : size;
        const auto end = tokens.Empty()
            ? begin
            : tree.Token(tokens.End - 1).Text.End;
        const bool recovery = Recovery(node.Kind);
        const bool ambiguous = node.Kind ==
            parser::NodeKind::AmbiguousStatement;
        const auto hasRecovery = recovery ||
            recoveryPrefix[tokens.End] != recoveryPrefix[tokens.Begin];
        const auto hasAmbiguity = ambiguous ||
            ambiguousPrefix[tokens.End] != ambiguousPrefix[tokens.Begin];
        const auto status = Combine(hasRecovery
            ? RegionStatus::Recovery
            : RegionStatus::Complete,
            hasAmbiguity ? RegionStatus::Ambiguous : RegionStatus::Complete);
        result.Regions_.push_back({
            tokens,
            {begin, end},
            node.Kind,
            parent,
            status,
        });
        if (node.Kind == parser::NodeKind::HeaderName) {
            // Header spelling includes internal whitespace. Ordinary lexical
            // identity alone cannot authorize edits inside angle delimiters.
            protect(begin, end, ProtectionReason::HeaderName);
        }
        if (recovery || ambiguous) {
            // Own the adjacent trivia too. Empty missing-token nodes still
            // fence the insertion point; parents report incomplete status.
            const auto before = tokens.Begin
                ? tree.Token(tokens.Begin - 1).Text.End
                : 0;
            const auto after = tokens.End < count
                ? tree.Token(tokens.End).Text.Begin
                : size;
            protect(before, after, recovery
                ? ProtectionReason::Recovery
                : ProtectionReason::Ambiguous);
            if (before == after && tokens.Begin < result.Gaps_.size()) {
                result.Gaps_[tokens.Begin].Preserve = true;
                result.Gaps_[tokens.Begin].Reasons = recovery
                    ? ProtectionReason::Recovery
                    : ProtectionReason::Ambiguous;
            }
        }
        stack.push_back({node.Child(), id});
    };
    visit(tree.Root(), NoRegion);
    while (!stack.empty()) {
        if (const auto* child = stack.back().Next) {
            const auto parent = stack.back().Id;
            stack.back().Next = child->NextSibling;
            visit(*child, parent);
        } else {
            const auto id = stack.back().Id;
            stack.pop_back();
            const auto& region = result.Regions_[id];
            if (region.Parent != NoRegion) {
                auto& parent = result.Regions_[region.Parent];
                parent.Status = Combine(parent.Status, region.Status);
            }
        }
    }
    std::sort(
        result.Protection_.begin(),
        result.Protection_.end(),
        [] (const auto& a, const auto& b) {
            return a.Source.Begin < b.Source.Begin;
        });
    size_t merged = 0;
    for (const auto range : result.Protection_) {
        if (merged &&
            range.Source.Begin <= result.Protection_[merged - 1].Source.End)
        {
            auto& previous = result.Protection_[merged - 1];
            previous.Source.End = std::max(
                previous.Source.End,
                range.Source.End);
            previous.Reasons = previous.Reasons | range.Reasons;
        } else {
            result.Protection_[merged++] = range;
        }
    }
    result.Protection_.resize(merged);
    size_t interval = 0;
    for (auto& gap : result.Gaps_) {
        while (interval < merged &&
            result.Protection_[interval].Source.End <= gap.Source.Begin)
        {
            ++interval;
        }
        if (interval < merged) {
            const auto& range = result.Protection_[interval];
            // An insertion before the opening header delimiter is outside
            // its spelling, despite sharing the delimiter's source offset.
            // Other protection reasons still own that boundary conservatively.
            const bool beforeHeader = gap.Source.Empty() &&
                gap.Source.Begin == range.Source.Begin &&
                range.Reasons == ProtectionReason::HeaderName;
            if (!beforeHeader && (gap.Source.Empty()
                ? range.Source.Begin <= gap.Source.Begin
                : range.Source.Begin < gap.Source.End))
            {
                gap.Preserve = true;
                gap.Reasons = gap.Reasons | range.Reasons;
                if ((static_cast<uint16_t>(range.Reasons) &
                    static_cast<uint16_t>(ProtectionReason::HeaderName)) != 0)
                {
                    gap.ContextSensitive = true;
                }
            }
        }
    }
    return result;
}

} // namespace formatter
