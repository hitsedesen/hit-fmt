#include "source_analysis.hpp"

#include <algorithm>
#include <cassert>
#include <limits>
#include <stdexcept>
#include <utility>
namespace formatter {

namespace {

using TK = tokenizer::TokenKind;

using Offset = parser::SourceOffset;
constexpr auto NoToken = std::numeric_limits<parser::TokenIndex>::max();
bool Whitespace(std::string_view text)
{
    return text.find_first_not_of(" \t\r\n\f\v") == std::string_view::npos;
}

bool Covered(
    std::span<const parser::SourceRange> ranges,
    parser::SourceRange source)
{
    const auto first = std::lower_bound(
        ranges.begin(),
        ranges.end(),
        source.Begin,
        [] (parser::SourceRange range, Offset begin) {
            return range.End <= begin;
        });
    return first != ranges.end() && first->Begin <= source.Begin &&
        (source.Empty() ? source.Begin < first->End : source.End <= first->End);
}

bool WholeGapCovered(
    std::span<const parser::SourceRange> ranges,
    const SourceGap& gap,
    std::string_view original)
{
    // Replacing an LF-ended gap can synthesize indentation at the following
    // line's zero-width point. Owning the preceding LF is not
    // owning that point.
    return Covered(ranges, gap.Source) &&
        (!original.ends_with('\n') ||
            Covered(ranges, {gap.Source.End, gap.Source.End}));
}

void MergeProtection(std::pmr::vector<ProtectedSourceRange>& protection)
{
    std::sort(
        protection.begin(),
        protection.end(),
        [] (const auto& a, const auto& b) {
            return a.Source.Begin < b.Source.Begin;
        });
    size_t merged = 0;
    for (const auto range : protection) {
        if (merged &&
            (range.Source.Begin < protection[merged - 1].Source.End ||
                (range.Source.Begin == protection[merged - 1].Source.End &&
                    range.Reasons == protection[merged - 1].Reasons)))
        {
            auto& previous = protection[merged - 1];
            previous.Source.End =
                std::max(previous.Source.End, range.Source.End);
            previous.Reasons = previous.Reasons | range.Reasons;
            // A true overlap can make the union equal to its touching neighbor.
            while (merged > 1 &&
                protection[merged - 2].Source.End ==
                    protection[merged - 1].Source.Begin &&
                protection[merged - 2].Reasons ==
                    protection[merged - 1].Reasons)
            {
                protection[merged - 2].Source.End =
                    protection[merged - 1].Source.End;
                --merged;
            }
        } else {
            protection[merged++] = range;
        }
    }
    protection.resize(merged);
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

bool OrdinaryBracket(parser::PunctuationRole role)
{
    using enum parser::PunctuationRole;
    return role == LambdaDelimiter || role == DeclarationDelimiter ||
        role == CallDelimiter;
}

bool Punctuation(TK kind)
{
    return kind >= TK::Operator_OpenBrace && kind <= TK::Operator_Comma;
}

bool PunctuationNeedsSeparation(std::string_view first, std::string_view second)
{
    const auto next = second.front();
    if (first.size() == 1) {
        switch (first.front()) {
            case ':':
                return next == ':' || next == '>';
            case '.':
                // Even two dots are consumed as an unfinished ellipsis.
                return next == '.' || next == '*';
            case '-':
                return next == '-' || next == '=' || next == '>';
            case '+':
                return next == '+' || next == '=';
            case '/':
                return next == '/' || next == '*' || next == '=';
            case '%':
                return next == ':' || next == '>' || next == '=';
            case '&':
                return next == '&' || next == '=';
            case '|':
                return next == '|' || next == '=';
            case '<':
                // Keep < :: separated regardless of the following token:
                // the maximal-munch exception depends on further lookahead.
                return next == '<' || next == '=' || next == ':' || next == '%';
            case '>':
                return next == '>' || next == '=';
            case '*':
            case '^':
            case '!':
            case '=':
                return next == '=';
            default:
                return false;
        }
    }
    if (first == "->") {
        return next == '*';
    }
    if (first == "<<" || first == ">>") {
        return next == '=';
    }
    if (first == "<=") {
        return next == '>';
    }
    // Joining <: and : can invoke the <:: exception and split the digraph.
    return first == "<:" && next == ':';
}

// Conservative maximal-munch constraints, based on the tokenizer's categories
// and spellings. Only punctuation that can extend or change the left token
// needs separation. Comments, digraphs, <:: lookahead and ellipses are handled
// without allocating/retokenizing each pair. Template > > still needs
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
    if ((Number(a) &&
        (second.starts_with(".") || second.starts_with("+") ||
            second.starts_with("-"))) || (first.ends_with(".") && Number(b)))
    {
        return true;
    }
    if (Punctuation(a) && Punctuation(b)) {
        return PunctuationNeedsSeparation(first, second);
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
    return static_cast<RegionStatus>(static_cast<uint8_t>(a) |
        static_cast<uint8_t>(b));
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
    , DirectiveFences_(&fileMemory)
    , Regions_(&fileMemory)
    , EditScope_(&fileMemory)
    , PartialGapPermissions_(&fileMemory)
{ }

SourceAnalysis::SourceAnalysis(SourceAnalysis&& other) noexcept
    : Source_(std::exchange(other.Source_, {}))
    , Gaps_(std::move(other.Gaps_))
    , Protection_(std::move(other.Protection_))
    , DirectiveFences_(std::move(other.DirectiveFences_))
    , Regions_(std::move(other.Regions_))
    , EditScope_(std::move(other.EditScope_))
    , PartialGapPermissions_(std::move(other.PartialGapPermissions_))
    , HasEditScope_(other.HasEditScope_)
    , Built_(std::exchange(other.Built_, false))
{ }

bool SourceAnalysis::IntersectsImmutable(parser::SourceRange source) const
{
    assert(source.Begin <= source.End);
    if (HasEditScope_ && !Covered(EditScope_, source)) {
        return true;
    }
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

bool SourceAnalysis::CanRewriteDirective(parser::SourceRange source) const
{
    if (!Built_ || source.Empty() || source.Begin > source.End ||
        source.End > Source_.size() ||
        (HasEditScope_ && !Covered(EditScope_, source)))
    {
        return false;
    }
    const auto first = std::lower_bound(
        DirectiveFences_.begin(),
        DirectiveFences_.end(),
        source.Begin,
        [] (
            const auto& range,
            Offset begin) { return range.Source.End <= begin; });
    return first == DirectiveFences_.end() || first->Source.Begin >= source.End;
}

bool SourceAnalysis::CanReplaceGap(GapId id, std::string_view replacement) const
{
    if (id >= Gaps_.size()) {
        return false;
    }
    const auto& gap = Gaps_[id];
    if (replacement == Original(gap)) {
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

bool SourceAnalysis::CanSpellGapNewlines(GapId id) const
{
    if (!Built_ || id >= Gaps_.size()) {
        return false;
    }
    const auto& gap = Gaps_[id];
    return !gap.Source.Empty() && !gap.IntrinsicPreserve &&
        !gap.IntrinsicContextSensitive && !IntersectsImmutable(gap.Source);
}
std::pmr::vector<PartialGapPermission>
SourceAnalysis::CollectPartialGapPermissions(
    std::optional<std::span<const parser::SourceRange>> scope) const
{
    std::pmr::vector<PartialGapPermission> permissions(
        Gaps_.get_allocator().resource());
    // A directive owns its terminating LF, not the next line's indentation.
    // Keep the same slice authority for unrestricted and scoped formatting.
    for (GapId id = 0; id < Gaps_.size(); ++id) {
        const auto& gap = Gaps_[id];
        if ((gap.Preserve ||
            (scope && !WholeGapCovered(*scope, gap, Original(gap)))) &&
            !gap.IntrinsicPreserve && !gap.IntrinsicContextSensitive &&
            (!gap.MustStartWithLineFeed || Original(gap).starts_with('\n')) &&
            Original(gap).find_first_not_of(" \t\r\n") ==
                std::string_view::npos)
        {
            // Lone CR is not a supported horizontal slice or LF-ended prefix.
            bool supported = true;
            for (size_t i = 0; i < Original(gap).size(); ++i) {
                if (Original(gap)[i] == '\r' &&
                    (i + 1 == Original(gap).size() ||
                        Original(gap)[i + 1] != '\n'))
                {
                    supported = false;
                    break;
                }
            }
            auto permit =
                [&] (parser::SourceRange slice, PartialGapPermissionKind kind) {
                    if (scope && !Covered(*scope, slice)) {
                        return;
                    }
                    // HeaderName alone does not own insertion before its
                    // opener.
                    auto first = std::lower_bound(
                        Protection_.begin(),
                        Protection_.end(),
                        slice.Begin,
                        [] (const auto& range, Offset begin) {
                            return range.Source.End <= begin;
                        });
                    const bool beforeHeader = first != Protection_.end() &&
                        slice.Empty() && slice.Begin == first->Source.Begin &&
                        first->Reasons == ProtectionReason::HeaderName;
                    if (first != Protection_.end() && !beforeHeader &&
                        (slice.Empty()
                            ? first->Source.Begin <= slice.Begin
                            : first->Source.Begin < slice.End))
                    {
                        return;
                    }
                    permissions.push_back({id, slice, kind});
                };
            if (supported) {
                size_t lineBegin = 0;
                for (size_t i = 0; i < Original(gap).size(); ++i) {
                    if (Original(gap)[i] != '\n') {
                        continue;
                    }
                    const auto end = i && Original(gap)[i - 1] == '\r'
                        ? i - 1
                        : i;
                    if (lineBegin < end) {
                        permit(
                            {
                                gap.Source.Begin +
                                    static_cast<Offset>(lineBegin),
                                gap.Source.Begin + static_cast<Offset>(end),
                            },
                            PartialGapPermissionKind::TrimHorizontal);
                    }
                    lineBegin = i + 1;
                }
                if (lineBegin && id + 1 < Gaps_.size()) {
                    permit(
                        {
                            gap.Source.Begin + static_cast<Offset>(lineBegin),
                            gap.Source.End,
                        },
                        PartialGapPermissionKind::FinalIndentation);
                }
            }
        }
    }
    return permissions;
}

void SourceAnalysis::RestrictEditScope(
    std::span<const parser::SourceRange> ranges)
{
    if (!Built_ || Gaps_.empty() || HasEditScope_) {
        throw std::logic_error(
            "edit scope requires built unrestricted analysis");
    }
    const auto size = Gaps_.back().Source.End;
    // Validate before allocating or changing any analysis facts.
    Offset previousEnd = 0;
    for (const auto range : ranges) {
        if (range.Begin >= range.End || range.End > size ||
            range.Begin < previousEnd)
        {
            throw std::invalid_argument("invalid source edit scope");
        }
        previousEnd = range.End;
    }
    const auto allocator = Gaps_
        .get_allocator()
        .resource();
    std::pmr::vector<parser::SourceRange> scope(allocator);
    scope.reserve(ranges.size());
    for (const auto range : ranges) {
        if (!scope.empty() && range.Begin == scope.back().End) {
            scope.back().End = range.End;
        } else {
            scope.push_back(range);
        }
    }
    // All potentially throwing work precedes commit. Protection still contains
    // semantic/explicit reasons only; the complement must not hide safe slices.
    auto permissions = CollectPartialGapPermissions(scope);
    std::pmr::vector<ProtectedSourceRange> protection(allocator);
    protection.reserve(Protection_.size() + scope.size() + 1);
    protection.insert(protection.end(), Protection_.begin(), Protection_.end());
    Offset begin = 0;
    auto outside = [&] (Offset end) {
        if (begin < end) {
            protection
                .push_back({{begin, end}, ProtectionReason::OutsideEditScope});
        }
    };
    for (const auto range : scope) {
        outside(range.Begin);
        begin = range.End;
    }
    outside(size);
    MergeProtection(protection);
    for (auto& gap : Gaps_) {
        if (!WholeGapCovered(scope, gap, Original(gap))) {
            gap.Preserve = true;
            gap.Reasons = gap.Reasons | ProtectionReason::OutsideEditScope;
        }
    }
    EditScope_.swap(scope);
    PartialGapPermissions_.swap(permissions);
    Protection_.swap(protection);
    HasEditScope_ = true;
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
    result.Source_ = source;
    const auto count = tree.TokenCount();
    result.Gaps_.reserve(static_cast<size_t>(count) + 1);
    auto protect = [&] (Offset begin, Offset end, ProtectionReason reason) {
        if (begin > end || end > size) {
            throw std::invalid_argument("invalid immutable source range");
        }
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
            // LF was handled above; the remaining horizontal whitespace is
            // space or one of the contiguous ASCII control codes HT..CR.
            const auto c = source[i];
            if (c != ' ' && (c < '\t' || c > '\r')) {
                if (line.FirstContent == size) {
                    line.FirstContent = i;
                }
                line.ContentEnd = i + 1;
            }
            continue;
        }
        const auto continued =
            line.ContentEnd > line.Begin && source[line.ContentEnd - 1] == '\\';
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
        SourceGap gap{.Source = {begin, end}};
        // Classify bare CR in the existing whitespace validation walk.
        for (size_t offset = begin; offset < end; ++offset) {
            const auto c = source[offset];
            gap.Preserve |= c != ' ' && c != '\t' && c != '\r' && c != '\n' &&
                c != '\f' && c != '\v';
            gap.HasBareCR |= c == '\r' &&
                (offset + 1 == source.size() || source[offset + 1] != '\n');
            gap.HasLineFeed |= c == '\n';
            gap.Horizontal &= c == ' ' || c == '\t';
        }
        if (i && i < count && TokenGapNeedsSeparation(tree, i)) {
            // C++ permits adjacent template closers to be retokenized as >>.
            // Only confirmed delimiters may relax maximal-munch separation;
            // comparison/shift operators and unparsed syntax remain lexical.
            const auto templateCloser = [&] (parser::TokenIndex index) {
                return tree.Role(index) ==
                    parser::PunctuationRole::TemplateDelimiter &&
                    (tree.Kind(index) == TK::Operator_Greater ||
                        tree.Kind(index) == TK::Operator_RightShift);
            };
            if (!templateCloser(i - 1) || !templateCloser(i)) {
                gap.Separation = GapSeparation::Required;
            }
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
            gap.LineBoundary = gap.HasLineFeed
                ? GapLineBoundary::Required
                : GapLineBoundary::Forbidden;
        }
        if (i &&
            (tree.Kind(i - 1) == TK::PreprocessingDirective ||
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
            gap.LineBoundary = gap.HasLineFeed
                ? GapLineBoundary::Required
                : GapLineBoundary::Forbidden;
        }
        if (i && i < count && Literal(tree.Kind(i - 1)) &&
            Word(tree.Kind(i), tree.TokenText(i)) && original.empty())
        {
            // The tokenizer leaves string/character user-defined suffixes as
            // identifiers. Adding whitespace changes grammar, not token kinds.
            gap.ContextSensitive = true;
            gap.Preserve = true;
        }
        if (i &&
            (tree.TokenText(i - 1).ends_with("\\") ||
                tree.TokenText(i - 1).ends_with("\\\r")))
        {
            gap.Preserve = true;
            gap.Reasons = ProtectionReason::LineSplice;
        }
        if (i && i < count &&
            ((tree.Kind(i - 1) == TK::Operator_OpenBracket &&
                tree.Kind(i) == TK::Operator_OpenBracket) ||
                (tree.Kind(i - 1) == TK::Operator_CloseBracket &&
                    tree.Kind(i) == TK::Operator_CloseBracket &&
                    !OrdinaryBracket(tree.Role(i - 1)) &&
                    !OrdinaryBracket(tree.Role(i)))))
        {
            // Uncertain bracket pairs may belong to attribute syntax. A
            // proven ordinary closer, such as a capture after a subscript,
            // leaves this gap available to its normal layout owner.
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
        recoveryPrefix
            .push_back(recoveryPrefix.back() + (token.Kind == TK::Invalid));
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
            protect(
                begin,
                i + 1 < count ? tree.Token(i + 1).Text.Begin : size,
                token.Kind == TK::Invalid
                ? ProtectionReason::Recovery
                : ProtectionReason::Ambiguous);
        }
        if (token.Kind == TK::Comment) {
            auto marker = text;
            if (marker.starts_with("//")) {
                while (!marker.empty() &&
                    (marker.back() == ' ' || marker.back() == '\t' ||
                        marker.back() == '\r'))
                {
                    marker.remove_suffix(1);
                }
            }
            const bool off =
                marker == "// hit-fmt off" || marker == "/* hit-fmt off */" ||
                    (options.HonorClangFormatDirectives &&
                        (marker == "// clang-format off" ||
                            marker == "/* clang-format off */"));
            const bool on =
                marker == "// hit-fmt on" || marker == "/* hit-fmt on */" ||
                    (options.HonorClangFormatDirectives &&
                        (marker == "// clang-format on" ||
                            marker == "/* clang-format on */"));
            if (off || on) {
                const auto range = tokenLine;
                if (lineIndex == endLineIndex && !lines[lineIndex].Continued &&
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
            protect(
                tree.Token(pendingMacro).Text.Begin,
                balanced ? tree.Token(close).Text.End : size,
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
    for (const auto& range : options.InheritedProtection) {
        if (range.Reasons == ProtectionReason::None) {
            throw std::invalid_argument(
                "inherited protection requires a reason");
        }
        protect(range.Source.Begin, range.Source.End, range.Reasons);
        if ((uint16_t(range.Reasons) &
            ~uint16_t(ProtectionReason::Directive)) != 0)
        {
            result.DirectiveFences_.push_back(range);
        }
    }
    struct Frame
    {
        const parser::Node* Next;
        RegionId Id;
        RegionId LastChild = NoRegion;
    };
    std::pmr::vector<Frame> stack(&scratchMemory);
    auto visit = [&] (const parser::Node& node, RegionId parent) {
        if (result.Regions_.size() >= NoRegion) {
            throw std::length_error("too many source regions");
        }
        const auto id = static_cast<RegionId>(result.Regions_.size());
        const auto tokens = node.Tokens;
        const auto begin = tokens.Begin < count
            ? tree.Token(tokens.Begin).Text.Begin
            : size;
        const auto end = tokens.Empty()
            ? begin
            : tree.Token(tokens.End - 1).Text.End;
        const bool recovery = Recovery(node.Kind);
        const bool ambiguous =
            node.Kind == parser::NodeKind::AmbiguousStatement;
        const auto hasRecovery = recovery ||
            recoveryPrefix[tokens.End] != recoveryPrefix[tokens.Begin];
        const auto hasAmbiguity = ambiguous ||
            ambiguousPrefix[tokens.End] != ambiguousPrefix[tokens.Begin];
        const auto status = Combine(
            hasRecovery ? RegionStatus::Recovery : RegionStatus::Complete,
            hasAmbiguity ? RegionStatus::Ambiguous : RegionStatus::Complete);
        auto attached = tokens;
        if (const auto* trivia = tree.AssociatedTrivia(node)) {
            for (auto* item = trivia->Leading.First; item; item = item->Next) {
                attached.Begin = std::min(attached.Begin, item->Tokens.Begin);
            }
            for (auto* item = trivia->Trailing.First; item; item = item->Next) {
                attached.End = std::max(attached.End, item->Tokens.End);
            }
        }
        result.Regions_.push_back({
            tokens,
            {begin, end},
            attached,
            parent,
            NoRegion,
            NoRegion,
            node.Kind,
            status,
            node.GetFunctionKind(),
        });
        if (parent != NoRegion) {
            auto& frame = stack.back();
            if (frame.LastChild == NoRegion) {
                result.Regions_[parent].FirstChild = id;
            } else {
                result.Regions_[frame.LastChild].NextSibling = id;
            }
            frame.LastChild = id;
        }
        auto prefix = NoToken;
        const parser::Node* next = nullptr;
        switch (node.Kind) {
            case parser::NodeKind::ListSeparator:
                if (tokens.Size() == 1) {
                    prefix = tokens.End;
                    next = node.NextSibling;
                }
                break;
            case parser::NodeKind::ArgumentList:
            case parser::NodeKind::ParameterList:
            case parser::NodeKind::TemplateArgumentList:
            case parser::NodeKind::TemplateParameterList:
            case parser::NodeKind::InitializerList:
            case parser::NodeKind::EnumeratorList:
            case parser::NodeKind::LambdaCaptureList:
            case parser::NodeKind::Condition:
            case parser::NodeKind::ForHeader:
            case parser::NodeKind::BaseClause:
            case parser::NodeKind::ConstructorInitializerList:
                if (!tokens.Empty()) {
                    prefix = tokens.Begin + 1;
                    next = node.Child();
                }
                break;
            default:
                break;
        }
        if (prefix < count && tree.Kind(prefix) == TK::Comment &&
            tree.TokenText(prefix).starts_with("/*"))
        {
            while (next && next->Kind == parser::NodeKind::Comment) {
                next = next->NextSibling;
            }
            if (next) {
                auto afterComments = prefix;
                while (afterComments < next->Tokens.End &&
                    tree.Kind(afterComments) == TK::Comment &&
                    tree.TokenText(afterComments).starts_with("/*"))
                {
                    ++afterComments;
                }
                // Some list items include their leading trivia in Tokens.
                // The first non-comment token must belong to the next item.
                if (afterComments >= next->Tokens.Begin &&
                    afterComments < next->Tokens.End)
                {
                    // First and subsequent items carry their block-comment
                    // prefixes, preserving internal links. Mixed prefixes stay
                    // put so a line comment cannot move with block comments.
                    result.Gaps_[prefix].LineBoundary = GapLineBoundary::Any;
                }
            }
        }
        if (node.Kind == parser::NodeKind::HeaderName) {
            // Whitespace inside header delimiters is part of the filename.
            protect(begin, end, ProtectionReason::HeaderName);
        }
        if (node.Kind == parser::NodeKind::MacroFunctionHeader ||
            node.Kind == parser::NodeKind::MacroRecordBegin ||
            node.Kind == parser::NodeKind::MacroRecordEnd)
        {
            // Hints describe only the enclosed grammar. Macro arguments can
            // be stringified or token-pasted; preserve complete invocations.
            protect(begin, end, ProtectionReason::SensitiveMacro);
        }
        if (recovery || ambiguous) {
            // Complete ambiguous statements and bounded conditions have
            // proven edges, even when their interpretation is unknown.
            // Keep its uncertain head exact, but allow an independently
            // parsed assignment RHS and let the body indent around it.
            // Recovery still owns adjacent trivia and empty insertion points.
            auto protectedEnd = end;
            if (ambiguous) {
                for (auto* child = node.Child();
                    child;
                    child = child->NextSibling)
                {
                    if (child->Kind == parser::NodeKind::Initializer) {
                        protectedEnd =
                            tree.Token(child->Tokens.Begin).Text.Begin;
                        break;
                    }
                }
            }
            const bool conditionBoundary = parent != NoRegion &&
                result.Regions_[parent].Kind == parser::NodeKind::Condition &&
                result.Regions_[parent].Tokens.End == tokens.End + 1 &&
                tree.MatchingDelimiter(result.Regions_[parent].Tokens.Begin) ==
                    tokens.End;
            const bool statementBoundary =
                ambiguous && !hasRecovery && !tokens.Empty() &&
                    (tree.Kind(tokens.End - 1) == TK::Operator_Semicolon ||
                        conditionBoundary);
            // A recovered callee has a proven start in its enclosing call.
            // Protect its spelling without freezing the preceding operator
            // or keyword gap, which can break independently of the callee.
            const bool calleeBoundary =
                node.Kind == parser::NodeKind::UnparsedSyntax &&
                    !tokens.Empty() && parent != NoRegion &&
                    result.Regions_[parent].Kind ==
                        parser::NodeKind::CallExpression &&
                    result.Regions_[parent].FirstChild == id &&
                    result.Regions_[parent].Tokens.Begin == tokens.Begin;
            if (calleeBoundary) {
                result.Gaps_[tokens.Begin].RecoveryCalleeBoundary = true;
            }
            const auto before = statementBoundary || calleeBoundary
                ? begin
                : tokens.Begin ? tree.Token(tokens.Begin - 1).Text.End : 0;
            const auto after = ambiguous && protectedEnd != end
                ? protectedEnd
                : statementBoundary
                    ? end
                    : tokens.End < count
                        ? tree.Token(tokens.End).Text.Begin
                        : size;
            protect(
                before,
                after,
                recovery
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
    for (const auto& range : result.Protection_) {
        if ((uint16_t(range.Reasons) &
            ~uint16_t(
                ProtectionReason::Directive | ProtectionReason::LineSplice)) !=
            0)
        {
            result.DirectiveFences_.push_back(range);
        }
    }
    MergeProtection(result.DirectiveFences_);
    MergeProtection(result.Protection_);
    size_t interval = 0;
    for (auto& gap : result.Gaps_) {
        // Keep intrinsic fences distinct from protection folded into the gap:
        // a protected exact prefix need not protect a selected
        // horizontal slice.
        gap.IntrinsicPreserve = gap.Preserve;
        gap.IntrinsicContextSensitive = gap.ContextSensitive;
        while (interval < result.Protection_.size() &&
            result.Protection_[interval].Source.End <= gap.Source.Begin)
        {
            ++interval;
        }
        if (interval < result.Protection_.size()) {
            const auto& range = result.Protection_[interval];
            // Proven incoming gaps are outside the protected spelling, even
            // when empty and sharing its first byte's offset. Do not bypass
            // overlapping protection from any other reason.
            const bool beforeSpelling =
                gap.Source.Empty() && gap.Source.Begin == range.Source.Begin &&
                    (range.Reasons == ProtectionReason::HeaderName ||
                        (gap.RecoveryCalleeBoundary &&
                            range.Reasons == ProtectionReason::Recovery));
            if (!beforeSpelling &&
                (gap.Source.Empty()
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
    result.Built_ = true;
    if (options.EditScope) {
        result.RestrictEditScope(*options.EditScope);
    } else {
        result.PartialGapPermissions_ =
            result.CollectPartialGapPermissions(std::nullopt);
    }
    return result;
}

} // namespace formatter
