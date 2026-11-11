#include "source_analysis.hpp"

#include "source_analysis_internal.hpp"

#include <util/memory.hpp>
#include <util/types.hpp>

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

bool Covered(std::span<const parser::SourceRange> ranges, parser::SourceRange source)
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

bool WholeGapCovered(std::span<const parser::SourceRange> ranges, const SourceGap& gap, std::string_view original)
{
    // Replacing an LF-ended gap can synthesize indentation at the following
    // line's zero-width point. Owning the preceding LF is not
    // owning that point.
    return Covered(ranges, gap.Source) &&
        (!original.ends_with('\n') || Covered(ranges, {gap.Source.End, gap.Source.End}));
}

void MergeProtection(Vector<ProtectedSourceRange>& protection)
{
    std::sort(
        protection.begin(),
        protection.end(),
        [] (const auto& a, const auto& b) {
            return a.Source.Begin < b.Source.Begin;
        });
    us merged = 0;

    for (const auto range : protection) {
        if (merged &&
            (range.Source.Begin < protection[merged - 1].Source.End ||
                (range.Source.Begin == protection[merged - 1].Source.End &&
                    range.Reasons == protection[merged - 1].Reasons)))
        {
            auto& previous = protection[merged - 1];
            previous.Source.End = std::max(previous.Source.End, range.Source.End);
            previous.Reasons = previous.Reasons | range.Reasons;

            // A true overlap can make the union equal to its touching neighbor.
            while (merged > 1 && protection[merged - 2].Source.End == protection[merged - 1].Source.Begin &&
                protection[merged - 2].Reasons == protection[merged - 1].Reasons)
            {
                protection[merged - 2].Source.End = protection[merged - 1].Source.End;
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
        (kind >= TK::Operator_OpenBrace && kind <= TK::Operator_Comma && !text.empty() && text.front() >= 'a' &&
            text.front() <= 'z');
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
    return role == LambdaDelimiter || role == DeclarationDelimiter || role == CallDelimiter;
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
bool NeedsSeparation(TK a, std::string_view first, TK b, std::string_view second)
{
    if ((Word(a, first) || Literal(a)) && (Word(b, second) || Literal(b))) {
        return true;
    }

    if ((Number(a) && (second.starts_with(".") || second.starts_with("+") || second.starts_with("-"))) ||
        (first.ends_with(".") && Number(b)))
    {
        return true;
    }

    if (Punctuation(a) && Punctuation(b)) {
        return PunctuationNeedsSeparation(first, second);
    }

    // A slash before a comment can turn a block comment into a line comment.
    return a == TK::Operator_Divide && b == TK::Comment;
}

void ClassifyGap(
    SourceGap& gap,
    std::string_view source,
    parser::TokenIndex count,
    parser::TokenIndex i,
    const tokenizer::Token& token,
    std::string_view text,
    parser::PunctuationRole role,
    TK previousKind,
    std::string_view previousText,
    parser::PunctuationRole previousRole,
    Offset previousEnd)
{
    const auto begin = previousEnd;
    const auto end = i < count ? token.Text.Begin : static_cast<Offset>(source.size());
    const auto original = source.substr(begin, end - begin);
    gap = {.Source = {begin, end}};

    // Classify bare CR in the existing whitespace validation walk.
    for (us offset = begin; offset < end; ++offset) {
        const auto c = source[offset];

        if (c == ' ' || c == '\t') {
            continue;
        }

        gap.Horizontal = false;

        if (c == '\n') {
            gap.HasLineFeed = true;
        } else if (c == '\r') {
            gap.HasBareCR |= offset + 1 == source.size() || source[offset + 1] != '\n';
        } else if (c != '\f' && c != '\v') {
            gap.Preserve = true;
        }
    }

    if (i && i < count && NeedsSeparation(previousKind, previousText, token.Kind, text)) {
        // C++ permits adjacent template closers to be retokenized as >>.
        // Only confirmed delimiters may relax maximal-munch separation;
        // comparison/shift operators and unparsed syntax remain lexical.
        const auto templateCloser = [] (TK kind, parser::PunctuationRole role) {
            return role == parser::PunctuationRole::TemplateDelimiter &&
                (kind == TK::Operator_Greater || kind == TK::Operator_RightShift);
        };

        if (!templateCloser(previousKind, previousRole) || !templateCloser(token.Kind, role)) {
            gap.Separation = GapSeparation::Required;
        }
    }

    // Preserve comment attachment topology, not its surrounding whitespace.
    // In particular a line comment must not swallow the following token.
    const auto commentOrDirective = [] (TK kind) {
        return kind == TK::Comment || kind == TK::PreprocessingDirective;
    };

    if ((i && commentOrDirective(previousKind)) || (i < count && commentOrDirective(token.Kind))) {
        gap.LineBoundary = gap.HasLineFeed ? GapLineBoundary::Required : GapLineBoundary::Forbidden;
    }

    if (i &&
        (previousKind == TK::PreprocessingDirective || (previousKind == TK::Comment && previousText
            .starts_with("//"))))
    {
        gap.MustStartWithLineFeed = true;

        // No terminator at EOF: insertion could extend the line token,
        // even though its half-open protection ends at the insertion point.
        if (original.empty()) {
            gap.Preserve = true;
        }
    }

    if (i < count && token.Kind == TK::Operator_Remainder) {
        // %: is a directive only at the beginning of a line in our lexer.
        gap.LineBoundary = gap.HasLineFeed ? GapLineBoundary::Required : GapLineBoundary::Forbidden;
    }

    if (i && i < count && Literal(previousKind) && Word(token.Kind, text) && original.empty()) {
        // The tokenizer leaves string/character user-defined suffixes as
        // identifiers. Adding whitespace changes grammar, not token kinds.
        gap.ContextSensitive = true;
        gap.Preserve = true;
    }

    if (i && (previousText.ends_with("\\") || previousText.ends_with("\\\r"))) {
        gap.Preserve = true;
        gap.Reasons = ProtectionReason::LineSplice;
    }

    if (i && i < count &&
        ((previousKind == TK::Operator_OpenBracket && token.Kind == TK::Operator_OpenBracket) ||
            (previousKind == TK::Operator_CloseBracket && token.Kind == TK::Operator_CloseBracket &&
                !OrdinaryBracket(previousRole) && !OrdinaryBracket(role))))
    {
        // Uncertain bracket pairs may belong to attribute syntax. A
        // proven ordinary closer, such as a capture after a subscript,
        // leaves this gap available to its normal layout owner.
        gap.ContextSensitive = true;
        gap.Preserve = true;
    }

    // Keep intrinsic fences distinct from protection folded into the gap:
    // a protected exact prefix need not protect a selected horizontal slice.
    gap.IntrinsicPreserve = gap.Preserve;
    gap.IntrinsicContextSensitive = gap.ContextSensitive;
}

bool Recovery(parser::NodeKind kind)
{
    using NK = parser::NodeKind;
    return kind == NK::UnparsedSyntax || kind == NK::MissingToken || kind == NK::UnexpectedToken || kind == NK::Error;
}

bool SimpleStatementBoundary(const parser::SyntaxTree& tree, const parser::Node& node, parser::NodeKind parent)
{
    using NK = parser::NodeKind;
    const auto tokens = node.Tokens;

    if ((parent != NK::CompoundStatement && parent != NK::PreprocessingBody && parent != NK::LambdaBody) ||
        tokens.Empty() || tree.Kind(tokens.End - 1) != TK::Operator_Semicolon)
    {
        return false;
    }

    switch (node.Kind) {
        case NK::ExpressionStatement:
        case NK::DeclarationStatement:
        case NK::ReturnStatement:
        case NK::ThrowStatement:
        case NK::CoYieldStatement:
            // Parsed simple statements prove their own edges, even when an
            // expression descendant is opaque. Its interior remains exact.
            return true;
        case NK::UnparsedSyntax:
            break;
        default:
            return false;
    }

    // A semicolon inside unmatched delimiters is not a statement boundary.
    // Invalid tokens and directives retain their existing recovery fences.
    for (auto token = tokens.Begin; token < tokens.End; ++token) {
        const auto kind = tree.Kind(token);

        if (kind == TK::Invalid || kind == TK::PreprocessingDirective) {
            return false;
        }

        if (kind >= TK::Operator_OpenBrace && kind <= TK::Operator_CloseParenthesis) {
            const auto matching = tree.MatchingDelimiter(token);

            if (matching < tokens.Begin || matching >= tokens.End) {
                return false;
            }
        }
    }

    return true;
}

RegionStatus Combine(RegionStatus a, RegionStatus b)
{
    return static_cast<RegionStatus>(static_cast<u8>(a) | static_cast<u8>(b));
}

} // namespace
bool TokenGapNeedsSeparation(const parser::SyntaxTree& tree, parser::TokenIndex right)
{
    assert(right > 0 && right < tree.TokenCount());
    return NeedsSeparation(tree.Kind(right - 1), tree.TokenText(right - 1), tree.Kind(right), tree.TokenText(right));
}

parser::SourceOffset CheckedSourceAnalysisSize(us size)
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
        (source.Empty() ? first->Source.Begin <= source.Begin : first->Source.Begin < source.End);
}

bool SourceAnalysis::CanRewriteDirective(parser::SourceRange source) const
{
    if (!Built_ || source.Empty() || source.Begin > source.End || source.End > Source_.size() ||
        (HasEditScope_ && !Covered(EditScope_, source)))
    {
        return false;
    }

    const auto first = std::lower_bound(
        DirectiveFences_.begin(),
        DirectiveFences_.end(),
        source.Begin,
        [] (const auto& range, Offset begin) {
            return range.Source.End <= begin;
        });
    return first == DirectiveFences_.end() || first->Source.Begin >= source.End;
}

bool SourceAnalysis::CanSpellGapNewlines(GapId id) const
{
    if (!Built_ || id >= Gaps_.size()) {
        return false;
    }

    const auto& gap = Gaps_[id];
    return !gap.Source.Empty() && !gap.IntrinsicPreserve && !gap.IntrinsicContextSensitive &&
        !IntersectsImmutable(gap.Source);
}
Vector<PartialGapPermission> SourceAnalysis::CollectPartialGapPermissions(
    std::optional<std::span<const parser::SourceRange>> scope) const
{
    Vector<PartialGapPermission> permissions(Gaps_.get_allocator().resource());

    // A directive owns its terminating LF, not the next line's indentation.
    // Keep the same slice authority for unrestricted and scoped formatting.
    for (GapId id = 0; id < Gaps_.size(); ++id) {
        const auto& gap = Gaps_[id];

        if ((gap.Preserve || (scope && !WholeGapCovered(*scope, gap, Original(gap)))) && !gap.IntrinsicPreserve &&
            !gap.IntrinsicContextSensitive && (!gap.MustStartWithLineFeed || Original(gap).starts_with('\n')) &&
            Original(gap).find_first_not_of(" \t\r\n") == std::string_view::npos)
        {
            // Lone CR is not a supported horizontal slice or LF-ended prefix.
            bool supported = true;

            for (us i = 0; i < Original(gap).size(); ++i) {
                if (Original(gap)[i] == '\r' && (i + 1 == Original(gap).size() || Original(gap)[i + 1] != '\n')) {
                    supported = false;
                    break;
                }
            }

            auto permit = [&] (parser::SourceRange slice, PartialGapPermissionKind kind) {
                if (scope && !Covered(*scope, slice)) {
                    return;
                }

                // HeaderName does not own insertion before its opener.
                // A certified empty statement indent can insert before
                // opaque spelling, even if touching chunks merged.
                auto first = std::lower_bound(
                    Protection_.begin(),
                    Protection_.end(),
                    slice.Begin,
                    [] (const auto& range, Offset begin) {
                        return range.Source.End <= begin;
                    });
                const bool beforeSpelling = first != Protection_.end() && slice.Empty() &&
                    ((slice.Begin == first->Source.Begin && first->Reasons == ProtectionReason::HeaderName) ||
                        (kind == PartialGapPermissionKind::FinalIndentation && gap.MovableStatementIndent &&
                            (first->Reasons == ProtectionReason::Ambiguous ||
                                first->Reasons == ProtectionReason::Recovery)));

                if (first != Protection_.end() && !beforeSpelling &&
                    (slice.Empty() ? first->Source.Begin <= slice.Begin : first->Source.Begin < slice.End))
                {
                    return;
                }

                permissions.push_back({id, slice, kind});
            };

            if (supported) {
                us lineBegin = 0;

                for (us i = 0; i < Original(gap).size(); ++i) {
                    if (Original(gap)[i] != '\n') {
                        continue;
                    }

                    const auto end = i && Original(gap)[i - 1] == '\r' ? i - 1 : i;

                    if (lineBegin < end) {
                        permit(
                            {
                                gap.Source.Begin + static_cast<Offset>(lineBegin),
                                gap.Source.Begin + static_cast<Offset>(end),
                            },
                            PartialGapPermissionKind::TrimHorizontal);
                    }

                    lineBegin = i + 1;
                }

                if (lineBegin && id + 1 < Gaps_.size()) {
                    permit(
                        {gap.Source.Begin + static_cast<Offset>(lineBegin), gap.Source.End},
                        PartialGapPermissionKind::FinalIndentation);
                }
            }
        }
    }

    return permissions;
}

void SourceAnalysis::RestrictEditScope(std::span<const parser::SourceRange> ranges)
{
    if (!Built_ || Gaps_.empty() || HasEditScope_) {
        throw std::logic_error("edit scope requires built unrestricted analysis");
    }

    const auto size = Gaps_.back().Source.End;
    // Validate before allocating or changing any analysis facts.
    Offset previousEnd = 0;

    for (const auto range : ranges) {
        if (range.Begin >= range.End || range.End > size || range.Begin < previousEnd) {
            throw std::invalid_argument("invalid source edit scope");
        }

        previousEnd = range.End;
    }

    const auto allocator = Gaps_.get_allocator().resource();
    Vector<parser::SourceRange> scope(allocator);
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
    Vector<ProtectedSourceRange> protection(allocator);
    protection.reserve(Protection_.size() + scope.size() + 1);
    protection.insert(protection.end(), Protection_.begin(), Protection_.end());
    Offset begin = 0;
    auto outside = [&] (Offset end) {
        if (begin < end) {
            protection.push_back({{begin, end}, ProtectionReason::OutsideEditScope});
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

#if __has_cpp_attribute(gnu::flatten)
[[gnu::flatten]]
#endif
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
    result.Gaps_.reserve(static_cast<us>(count) + 1);
    result.Regions_.reserve(us(count) + count / 2 + 1);
    auto protect = [&] (Offset begin, Offset end, ProtectionReason reason) {
        if (begin > end || end > size) {
            throw std::invalid_argument("invalid immutable source range");
        }

        if (begin != end) {
            result.Protection_.push_back({{begin, end}, reason});
        }
    };
    const auto horizontal = [] (char c) {
        return c == ' ' || (c >= '\t' && c <= '\r');
    };

    for (us search = 0; search < size;) {
        const auto slash = source.find('\\', search);

        if (slash == std::string_view::npos) {
            break;
        }

        auto next = slash + 1;

        while (next < size && source[next] != '\n' && horizontal(source[next])) {
            ++next;
        }

        if (next == size || source[next] != '\n') {
            search = slash + 1;
            continue;
        }

        const auto previous = source.rfind('\n', slash);
        const auto logicalBegin = previous == std::string_view::npos ? 0 : previous + 1;
        auto begin = next + 1;

        for (;;) {
            const auto newline = source.find('\n', begin);
            const auto end = newline == std::string_view::npos ? size : newline;
            auto last = end;

            while (last > begin && horizontal(source[last - 1])) {
                --last;
            }

            if (newline != std::string_view::npos && last > begin && source[last - 1] == '\\') {
                begin = end + 1;
                continue;
            }

            search = newline == std::string_view::npos ? size : end + 1;
            protect(Offset(logicalBegin), Offset(search), ProtectionReason::LineSplice);
            break;
        }
    }

    struct LineBounds
    {
        Offset Begin;
        Offset ContentEnd;
    };

    LineBounds cachedLine{};
    bool cachedLineValid = false;
    const auto physicalLine = [&] (Offset offset) {
        // The LF belongs to its preceding line; its successor starts a new one.
        if (!cachedLineValid || offset < cachedLine.Begin || offset > cachedLine.ContentEnd) {
            const auto previous = offset ? source.rfind('\n', offset - 1) : std::string_view::npos;
            const auto next = source.find('\n', offset);
            cachedLine = {
                previous == std::string_view::npos ? 0 : Offset(previous + 1),
                next == std::string_view::npos ? size : Offset(next),
            };
            cachedLineValid = true;
        }

        return cachedLine;
    };
    Offset checkedMarkerEnd = 0;
    Vector<std::string_view> names(
        options.WhitespaceSensitiveMacros.begin(),
        options.WhitespaceSensitiveMacros.end(),
        &scratchMemory);
    std::sort(names.begin(), names.end());
    Vector<std::string_view> enumNames(&scratchMemory);
    enumNames.reserve(options.EnumMacroNames.size());

    for (const auto& name : options.EnumMacroNames) {
        enumNames.push_back(name);
    }

    std::sort(enumNames.begin(), enumNames.end());
    Vector<ProtectedSourceRange> enumInvocations(&scratchMemory);
    Vector<Offset> parsedEnumBegins(&scratchMemory);
    struct TokenMarkers
    {
        Vector<parser::TokenIndex> Positions;
        us Next = 0;
        parser::TokenIndex Begin = 0;

        bool Intersects(parser::TokenRange range)
        {
            // Syntax preorder normally advances through the source. Keep
            // overlapping/backward ranges correct without a per-token table.
            if (range.Begin < Begin) {
                Next = std::lower_bound(Positions.begin(), Positions.end(), range.Begin) - Positions.begin();
            } else {
                while (Next < Positions.size() && Positions[Next] < range.Begin) {
                    ++Next;
                }
            }

            Begin = range.Begin;
            return Next < Positions.size() && Positions[Next] < range.End;
        }
    };

    TokenMarkers recoveryTokens{Vector<parser::TokenIndex>(&scratchMemory)};
    TokenMarkers ambiguousTokens{Vector<parser::TokenIndex>(&scratchMemory)};
    Offset disabledBegin = 0;
    us disabledDepth = 0;
    // An outer configured invocation already protects all inner invocations.
    parser::TokenIndex macroEnd = 0;
    parser::TokenIndex pendingMacro = NoToken;

    TK previousKind = TK::Invalid;
    std::string_view previousText;
    auto previousRole = parser::PunctuationRole::None;
    Offset previousEnd = 0;

    for (parser::TokenIndex i = 0; i <= count; ++i) {
        const auto token = i < count ? tree.Token(i) : tokenizer::Token{};
        const auto text = i < count
            ? source.substr(token.Text.Begin, token.Text.End - token.Text.Begin)
            : std::string_view{};
        const auto role = i < count ? tree.Role(i) : parser::PunctuationRole::None;
        const auto begin = previousEnd;
        auto& gap = result.Gaps_.emplace_back();
        ClassifyGap(gap, source, count, i, token, text, role, previousKind, previousText, previousRole, previousEnd);

        if (i == count) {
            break;
        }

        const auto tokenLine = [&] {
            const auto first = physicalLine(token.Text.Begin);
            const auto last = physicalLine(token.Text.End);
            return parser::SourceRange{first.Begin, last.ContentEnd < size ? last.ContentEnd + 1 : size};
        };

        if (token.Kind == TK::Invalid) {
            recoveryTokens.Positions.push_back(i);
        }

        if (role == parser::PunctuationRole::AmbiguousOperator) {
            ambiguousTokens.Positions.push_back(i);
        }

        if (token.Kind == TK::PreprocessingDirective) {
            const auto line = tokenLine();
            protect(line.Begin, line.End, ProtectionReason::Directive);
        }

        if (token.Kind == TK::Invalid || role == parser::PunctuationRole::AmbiguousOperator) {
            protect(
                begin,
                i + 1 < count ? tree.Token(i + 1).Text.Begin : size,
                token.Kind == TK::Invalid ? ProtectionReason::Recovery : ProtectionReason::Ambiguous);
        }

        if (token.Kind == TK::Comment) {
            auto marker = text;

            if (marker.starts_with("//")) {
                while (!marker.empty() && (marker.back() == ' ' || marker.back() == '\t' || marker.back() == '\r')) {
                    marker.remove_suffix(1);
                }
            }

            const bool off = marker == "// hit-fmt off" || marker == "/* hit-fmt off */" ||
                (options.HonorClangFormatDirectives &&
                    (marker == "// clang-format off" || marker == "/* clang-format off */"));
            const bool on = marker == "// hit-fmt on" || marker == "/* hit-fmt on */" ||
                (options.HonorClangFormatDirectives &&
                    (marker == "// clang-format on" || marker == "/* clang-format on */"));

            if ((off || on) && token.Text.Begin >= checkedMarkerEnd) {
                const auto line = physicalLine(token.Text.Begin);
                const auto endLine = physicalLine(token.Text.End);
                checkedMarkerEnd = line.ContentEnd < size ? line.ContentEnd + 1 : size;

                // A later candidate on this line has this marker in its prefix,
                // so it cannot be standalone. Scan long indentation only once.
                if (line.Begin == endLine.Begin &&
                    std::all_of(source.begin() + line.Begin, source.begin() + token.Text.Begin, horizontal) &&
                    std::all_of(source.begin() + token.Text.End, source.begin() + line.ContentEnd, horizontal))
                {
                    auto previousContentEnd = line.Begin ? line.Begin - 1 : 0;

                    while (previousContentEnd && source[previousContentEnd - 1] != '\n' &&
                        horizontal(source[previousContentEnd - 1]))
                    {
                        --previousContentEnd;
                    }

                    const bool continued = previousContentEnd && source[previousContentEnd - 1] == '\\';

                    if (!continued) {
                        if (off && disabledDepth++ == 0) {
                            disabledBegin = line.Begin;
                        } else if (on && disabledDepth && --disabledDepth == 0) {
                            protect(disabledBegin, checkedMarkerEnd, ProtectionReason::Disabled);
                        }
                    }
                }
            }
        }

        if (i >= macroEnd && token.Kind == TK::Operator_OpenParenthesis && pendingMacro != NoToken) {
            const auto name = tree.TokenText(pendingMacro);
            const bool sensitive = std::binary_search(names.begin(), names.end(), name);
            const bool pendingEnum = !sensitive && std::binary_search(enumNames.begin(), enumNames.end(), name);

            if (sensitive || pendingEnum) {
                const auto close = tree.MatchingDelimiter(i);
                const auto balanced = close > i && close < count && tree.Kind(close) == TK::Operator_CloseParenthesis;
                macroEnd = balanced ? close + 1 : count;
                // An unclosed invocation has no trustworthy local end; retain its
                // suffix, not the unrelated prefix or a whole-file rollback.
                const auto begin = tree.Token(pendingMacro).Text.Begin;
                const auto end = balanced ? tree.Token(close).Text.End : size;
                const auto reason = balanced
                    ? ProtectionReason::SensitiveMacro
                    : ProtectionReason::SensitiveMacro | ProtectionReason::Recovery;

                if (pendingEnum) {
                    enumInvocations.push_back({{begin, end}, reason});
                } else {
                    protect(begin, end, reason);
                }
            }
        }

        if (token.Kind != TK::Comment) {
            // Comments are preprocessing whitespace between a macro name and
            // its opening parenthesis; directives and other tokens are not.
            pendingMacro = i >= macroEnd && token.Kind == TK::Identifier ? i : NoToken;
        }

        previousKind = token.Kind;
        previousText = text;
        previousRole = role;
        previousEnd = token.Text.End;
    }

    if (disabledDepth) {
        protect(disabledBegin, size, ProtectionReason::Disabled);
    }

    for (const auto range : options.ImmutableRanges) {
        protect(range.Begin, range.End, ProtectionReason::Explicit);
    }

    for (const auto& range : options.InheritedProtection) {
        if (range.Reasons == ProtectionReason::None) {
            throw std::invalid_argument("inherited protection requires a reason");
        }

        protect(range.Source.Begin, range.Source.End, range.Reasons);

        if ((u16(range.Reasons) & ~u16(ProtectionReason::Directive)) != 0) {
            result.DirectiveFences_.push_back(range);
        }
    }

    struct Frame
    {
        const parser::Node* Next;
        RegionId Id;
        // Conditional wrappers are transparent to statement boundaries.
        RegionId StatementParent;
        // Bounded statements may carry opaque expression continuations.
        RegionId EnclosingStatement = NoRegion;
        RegionId LastChild = NoRegion;
    };

    Vector<Frame> stack(&scratchMemory);
    const bool hasRecoveryTokens = !recoveryTokens.Positions.empty();
    const bool hasAmbiguousTokens = !ambiguousTokens.Positions.empty();
    auto visit = [&] (const parser::Node& node, RegionId parent) {
        if (result.Regions_.size() >= NoRegion) {
            throw std::length_error("too many source regions");
        }

        const auto id = static_cast<RegionId>(result.Regions_.size());
        const auto tokens = node.Tokens;
        const auto begin = tokens.Begin < count ? result.Gaps_[tokens.Begin].Source.End : size;
        const auto end = tokens.Empty() ? begin : result.Gaps_[tokens.End].Source.Begin;

        if (node.Kind == parser::NodeKind::MacroEnumDeclaration) {
            parsedEnumBegins.push_back(begin);
        }

        const bool recovery = Recovery(node.Kind);
        const bool ambiguous = node.Kind == parser::NodeKind::AmbiguousStatement;
        const auto hasRecovery = recovery || (hasRecoveryTokens && recoveryTokens.Intersects(tokens));
        const auto hasAmbiguity = ambiguous || (hasAmbiguousTokens && ambiguousTokens.Intersects(tokens));
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

        const auto boundaryParent = parent == NoRegion ? NoRegion : stack.back().StatementParent;
        const auto enclosingStatement = parent == NoRegion ? NoRegion : stack.back().EnclosingStatement;
        result.Regions_.emplace_back(
            tokens,
            parser::SourceRange{begin, end},
            attached,
            parent,
            NoRegion,
            NoRegion,
            node.Kind,
            status,
            node.Function,
            boundaryParent != NoRegion && SimpleStatementBoundary(tree, node, result.Regions_[boundaryParent].Kind));

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
                    next = node.FirstChild;
                }

                break;
            default:
                break;
        }

        if (prefix < count && tree.Kind(prefix) == TK::Comment && tree.TokenText(prefix).starts_with("/*")) {
            while (next && next->Kind == parser::NodeKind::Comment) {
                next = next->NextSibling;
            }

            if (next) {
                auto afterComments = prefix;

                while (afterComments < next->Tokens.End && tree.Kind(afterComments) == TK::Comment &&
                    tree.TokenText(afterComments).starts_with("/*"))
                {
                    ++afterComments;
                }

                // Some list items include their leading trivia in Tokens.
                // The first non-comment token must belong to the next item.
                if (afterComments >= next->Tokens.Begin && afterComments < next->Tokens.End) {
                    // First and subsequent items carry their block-comment
                    // prefixes, preserving internal links. Mixed prefixes stay
                    // put so a line comment cannot move with block comments.
                    result.Gaps_[prefix].LineBoundary = GapLineBoundary::Any;
                }
            }
        }

        if (node.Kind == parser::NodeKind::ParameterList && tokens.Size() >= 3 &&
            tree.Kind(tokens.Begin) == TK::Operator_OpenParenthesis &&
            tree.Kind(tokens.End - 1) == TK::Operator_CloseParenthesis &&
            tree.MatchingDelimiter(tokens.Begin) == tokens.End - 1 && tree.Kind(tokens.End - 2) == TK::Comment &&
            tree.TokenText(tokens.End - 2).starts_with("/*"))
        {
            // Moving the closer does not change a trailing block comment's
            // attachment to its parameter. Internal comment links stay fixed.
            result.Gaps_[tokens.End - 1].LineBoundary = GapLineBoundary::Any;
        }

        if (node.Kind == parser::NodeKind::HeaderName) {
            // Whitespace inside header delimiters is part of the filename.
            protect(begin, end, ProtectionReason::HeaderName);
        }

        if (node.Kind == parser::NodeKind::MacroFunctionHeader || node.Kind == parser::NodeKind::MacroRecordBegin ||
            node.Kind == parser::NodeKind::MacroRecordEnd)
        {
            // Hints describe only the enclosed grammar. Macro arguments can
            // be stringified or token-pasted; preserve complete invocations.
            protect(begin, end, ProtectionReason::SensitiveMacro);
        }

        if (node.Kind == parser::NodeKind::Argument && parent != NoRegion &&
            result.Regions_[parent].Kind == parser::NodeKind::ArgumentList)
        {
            const auto owner = result.Regions_[parent].Parent;

            if (owner != NoRegion && result.Regions_[owner].Kind == parser::NodeKind::AmbiguousStatement) {
                // These items have no expression/declaration children. Preserve
                // their internal gaps, including insertion after the first
                // token, while leaving the incoming list boundary mutable.
                protect(tree.Token(tokens.Begin).Text.End, end, ProtectionReason::Ambiguous);
            }
        }

        if (recovery || ambiguous) {
            // Complete ambiguous statements and bounded conditions have
            // proven edges, even when their interpretation is unknown.
            // Keep its uncertain head exact, but allow an independently
            // parsed initializer and let the body indent around it.
            // Other recovery still owns adjacent trivia and insertion points.
            auto protectedEnd = end;

            if (ambiguous) {
                for (auto* child = node.FirstChild; child; child = child->NextSibling) {
                    if (child->Kind == parser::NodeKind::Initializer || child->Kind == parser::NodeKind::ArgumentList) {
                        protectedEnd = tree.Token(child->Tokens.Begin).Text.Begin;

                        if (child->Kind == parser::NodeKind::ArgumentList) {
                            // The shared list starts after '(', not at its
                            // incoming gap, including a zero-width adjacency.
                            auto& gap = result.Gaps_[child->Tokens.Begin];
                            gap.Preserve = true;
                            gap.IntrinsicPreserve = true;
                            gap.Reasons = gap.Reasons | ProtectionReason::Ambiguous;
                        }

                        break;
                    }
                }
            }

            const bool conditionBoundary =
                parent != NoRegion && result.Regions_[parent].Kind == parser::NodeKind::Condition &&
                    result.Regions_[parent].Tokens.End == tokens.End + 1 &&
                    tree.MatchingDelimiter(result.Regions_[parent].Tokens.Begin) == tokens.End;
            const bool statementBoundary = (ambiguous && !hasRecovery && !tokens.Empty() &&
                (tree.Kind(tokens.End - 1) == TK::Operator_Semicolon || conditionBoundary)) ||
                result.Regions_[id].StatementBoundary;
            result.Regions_[id].StatementBoundary = statementBoundary;
            // A recovered callee has a proven start in its enclosing call.
            // Protect its spelling without freezing the preceding operator
            // or keyword gap, which can break independently of the callee.
            const bool calleeBoundary = node.Kind == parser::NodeKind::UnparsedSyntax && !tokens.Empty() &&
                parent != NoRegion && result.Regions_[parent].Kind == parser::NodeKind::CallExpression &&
                result.Regions_[parent].FirstChild == id && result.Regions_[parent].Tokens.Begin == tokens.Begin;

            if (calleeBoundary || (recovery && statementBoundary)) {
                result.Gaps_[tokens.Begin].RecoveryBoundary = true;
                result.Gaps_[tokens.Begin].MovableStatementIndent = statementBoundary;
            }

            const auto before = statementBoundary || calleeBoundary
                ? begin
                : tokens.Begin ? tree.Token(tokens.Begin - 1).Text.End : 0;
            const auto after = ambiguous && protectedEnd != end
                ? protectedEnd
                : statementBoundary ? end : tokens.End < count ? tree.Token(tokens.End).Text.Begin : size;
            // A preprocessing split cannot be classified as one expression,
            // but a bounded enclosing statement still proves its indentation
            // may move. Other recovery retains its existing opaque fences.
            bool preprocessingFragment = false;

            if (enclosingStatement != NoRegion && node.Kind == parser::NodeKind::UnparsedSyntax) {
                for (auto token = tokens.Begin; token < tokens.End; ++token) {
                    if (tree.Kind(token) == TK::PreprocessingDirective) {
                        preprocessingFragment = true;
                        break;
                    }
                }
            }

            if ((statementBoundary || preprocessingFragment) && ((ambiguous && !hasRecovery) || recovery)) {
                // Preserve opaque tokens, horizontal spelling and every
                // newline. Complete final indentation slices can still follow
                // the enclosing bounded statement when its body moves.
                auto protectedBegin = before;
                const auto firstGap = preprocessingFragment ? tokens.Begin : tokens.Begin + 1;
                const auto gapEnd = preprocessingFragment ? std::min(tokens.End, count) + 1 : tokens.End;

                for (auto token = firstGap; token < gapEnd; ++token) {
                    auto& gap = result.Gaps_[token];

                    if (gap.Source.End > after) {
                        break;
                    }

                    if (!gap.HasLineFeed || gap.Preserve || gap.ContextSensitive || gap.HasBareCR) {
                        continue;
                    }

                    const auto original = result.Original(gap);
                    const auto indentBegin = original.rfind('\n') + 1;

                    if (original.substr(indentBegin).find_first_not_of(" \t") != std::string_view::npos) {
                        continue;
                    }

                    const auto indent = gap.Source.Begin + Offset(indentBegin);

                    if (indent < protectedBegin) {
                        continue;
                    }

                    protect(
                        protectedBegin,
                        indent,
                        recovery ? ProtectionReason::Recovery : ProtectionReason::Ambiguous);
                    protectedBegin = gap.Source.End;
                    gap.MovableStatementIndent = true;
                }

                protect(protectedBegin, after, recovery ? ProtectionReason::Recovery : ProtectionReason::Ambiguous);
            } else {
                protect(before, after, recovery ? ProtectionReason::Recovery : ProtectionReason::Ambiguous);
            }

            if (before == after && tokens.Begin < result.Gaps_.size()) {
                result.Gaps_[tokens.Begin].Preserve = true;
                result.Gaps_[tokens.Begin].IntrinsicPreserve = true;
                result.Gaps_[tokens.Begin].Reasons =
                    recovery ? ProtectionReason::Recovery : ProtectionReason::Ambiguous;
            }
        }

        const auto* child = node.FirstChild;

        if (!child) {
            if (parent != NoRegion) {
                result.Regions_[parent].Status = Combine(result.Regions_[parent].Status, status);
            }

            return;
        }

        const bool statementBody =
            node.Kind == parser::NodeKind::CompoundStatement || node.Kind == parser::NodeKind::PreprocessingBody ||
                node.Kind == parser::NodeKind::LambdaBody || node.Kind == parser::NodeKind::RequirementBody;
        stack.push_back({
            child,
            id,
            node.Kind == parser::NodeKind::PreprocessingConditional ||
                node.Kind == parser::NodeKind::PreprocessingBranch
                ? boundaryParent
                : id,
            result.Regions_[id].StatementBoundary ? id : statementBody ? NoRegion : enclosingStatement,
        });
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

    // Both lists follow source order. Defer these protections until the
    // existing tree walk proves which invocations own the enum grammar.
    us parsedEnum = 0;

    for (const auto& range : enumInvocations) {
        while (parsedEnum < parsedEnumBegins.size() && parsedEnumBegins[parsedEnum] < range.Source.Begin) {
            ++parsedEnum;
        }

        if (parsedEnum == parsedEnumBegins.size() || parsedEnumBegins[parsedEnum] != range.Source.Begin) {
            protect(range.Source.Begin, range.Source.End, range.Reasons);
        }
    }

    for (const auto& range : result.Protection_) {
        if ((u16(range.Reasons) & ~u16(ProtectionReason::Directive | ProtectionReason::LineSplice)) != 0) {
            result.DirectiveFences_.push_back(range);
        }
    }

    MergeProtection(result.DirectiveFences_);
    MergeProtection(result.Protection_);
    auto nextGap = result.Gaps_.begin();
    const auto gapEnd = result.Gaps_.end();
    const auto beforeRange = [] (const SourceGap& gap, Offset begin) {
        // Empty gaps own insertion at a range's beginning; nonempty
        // gaps ending there do not intersect its protected bytes.
        return gap.Source.End < begin || (gap.Source.End == begin && !gap.Source.Empty());
    };

    for (const auto& range : result.Protection_) {
        if (nextGap == gapEnd) {
            break;
        }

        if (nextGap->Source.Begin >= range.Source.End) {
            continue;
        }

        if (beforeRange(*nextGap, range.Source.Begin)) {
            nextGap = std::lower_bound(nextGap, gapEnd, range.Source.Begin, beforeRange);
        }

        while (nextGap != gapEnd && nextGap->Source.Begin < range.Source.End) {
            auto& gap = *nextGap++;
            // A gap spanning multiple protections observes only its first
            // intersecting range, including a proven incoming-gap exception.
            const bool beforeSpelling = gap.Source.Empty() && gap.Source.Begin == range.Source.Begin &&
                (range.Reasons == ProtectionReason::HeaderName ||
                    (gap.RecoveryBoundary && range.Reasons == ProtectionReason::Recovery));

            if (!beforeSpelling) {
                gap.Preserve = true;
                gap.Reasons = gap.Reasons | range.Reasons;

                if ((static_cast<u16>(range.Reasons) & static_cast<u16>(ProtectionReason::HeaderName)) != 0) {
                    gap.ContextSensitive = true;
                }
            }
        }
    }

    result.Built_ = true;

    if (options.EditScope) {
        result.RestrictEditScope(*options.EditScope);
    } else {
        result.PartialGapPermissions_ = result.CollectPartialGapPermissions(std::nullopt);
    }

    return result;
}

bool CanReuseNormalizedSourceAnalysis(
    const parser::SyntaxTree& original,
    const SourceAnalysis& safety,
    std::span<const parser::SourceRange> edits,
    std::span<const parser::detail::NormalizedPiece> pieces,
    std::pmr::memory_resource& scratch)
{
    const auto firstAt = [&] (Offset offset) {
        parser::TokenIndex low = 0, high = original.TokenCount();

        while (low < high) {
            const auto mid = low + (high - low) / 2;

            if (original.Token(mid).Text.Begin < offset) {
                low = mid + 1;
            } else {
                high = mid;
            }
        }

        return low;
    };

    for (const auto edit : edits) {
        const auto begin = firstAt(edit.Begin);
        const auto end = firstAt(edit.End);
        const auto before = begin ? original.Token(begin - 1).Text.End : 0;
        const auto after = end < original.TokenCount()
            ? original.Token(end).Text.Begin
            : Offset(original.Source().size());
        const auto protections = safety.Protection();
        auto found = std::lower_bound(
            protections.begin(),
            protections.end(),
            before,
            [] (const auto& range, Offset point) {
                return range.Source.End < point;
            });

        for (; found != protections.end() && found->Source.Begin <= after; ++found) {
            if (found->Reasons != ProtectionReason::Directive) {
                return false;
            }
        }

        for (auto gap = begin; gap <= end; ++gap) {
            const auto& item = safety.Gaps()[gap];

            if (item.IntrinsicPreserve || item.IntrinsicContextSensitive || item.RecoveryBoundary ||
                item.MovableStatementIndent)
            {
                return false;
            }
        }
    }

    Vector<parser::SourceRange> copied(&scratch);
    copied.reserve(pieces.size());

    for (const auto& piece : pieces) {
        if (!piece.Generated) {
            copied.push_back(piece.Original);
        }
    }

    std::sort(
        copied.begin(),
        copied.end(),
        [] (const auto a, const auto b) {
            return a.Begin < b.Begin;
        });
    us next = 0;

    for (const auto& range : safety.Protection()) {
        if (range.Reasons == ProtectionReason::Directive) {
            continue;
        }

        while (next < copied.size() && copied[next].End <= range.Source.Begin) {
            ++next;
        }

        if (next == copied.size() || copied[next].Begin > range.Source.Begin || copied[next].End < range.Source.End) {
            return false;
        }
    }

    return true;
}

struct detail::NormalizationAnalysisReuse
{
    template <class Refresh>
    static bool TransportRegions(
        SourceAnalysis& result,
        std::span<const SourceRegion> cached,
        std::span<const parser::TokenIndex> mapping,
        std::span<const parser::detail::NormalizedCommentInsertion> insertions,
        bool inPlace,
        const Refresh& refresh,
        std::pmr::memory_resource& scratch)
    {
        if (inPlace) {
            for (auto& region : result.Regions_) {
                refresh(region);
            }

            return true;
        }

        struct Insertion
        {
            parser::detail::NormalizedCommentInsertion Proof;
            RegionId Region = NoRegion;
            RegionId Parent = NoRegion;
        };

        Vector<Insertion> added(&scratch);
        added.reserve(insertions.size());

        for (const auto& proof : insertions) {
            if (proof.ParentRegion >= cached.size() || proof.BeforeRegion <= proof.ParentRegion ||
                proof.BeforeRegion > cached.size())
            {
                return false;
            }

            added.push_back({proof});
        }

        std::ranges::sort(
            added,
            [] (const Insertion& a, const Insertion& b) {
                return a.Proof.BeforeRegion != b.Proof.BeforeRegion
                    ? a.Proof.BeforeRegion < b.Proof.BeforeRegion
                    : a.Proof.Token < b.Proof.Token;
            });

        if (added.size() > NoRegion || cached.size() > NoRegion - added.size()) {
            throw std::length_error("too many source regions");
        }

        const auto rebase = [&] (RegionId id) -> RegionId {
            if (id == NoRegion) {
                return NoRegion;
            }

            const auto end = std::upper_bound(
                added.begin(),
                added.end(),
                id,
                [] (RegionId key, const Insertion& item) {
                    return key < item.Proof.BeforeRegion;
                });
            return id + static_cast<RegionId>(end - added.begin());
        };
        const auto tokens = [&] (parser::TokenRange old) {
            return old.Empty()
                ? parser::TokenRange{mapping[old.Begin], mapping[old.Begin]}
                : parser::TokenRange{mapping[old.Begin], mapping[old.End - 1] + 1};
        };
        us next = 0;

        for (RegionId id = 0; id <= cached.size(); ++id) {
            while (next < added.size() && added[next].Proof.BeforeRegion == id) {
                auto& item = added[next++];
                SourceRegion region{};
                region.Tokens = {item.Proof.Token, item.Proof.Token + 1};
                region.Attached = region.Tokens;
                region.Parent = rebase(item.Proof.ParentRegion);
                region.Kind = parser::NodeKind::Comment;
                refresh(region);
                item.Region = static_cast<RegionId>(result.Regions_.size());
                result.Regions_.push_back(region);
            }

            if (id == cached.size()) {
                break;
            }

            auto region = cached[id];
            region.Tokens = region.Kind == parser::NodeKind::TranslationUnit
                ? parser::TokenRange{0, static_cast<parser::TokenIndex>(result.Gaps_.size() - 1)}
                : tokens(region.Tokens);
            region.Attached = tokens(region.Attached);
            region.Attached.Begin = std::min(region.Attached.Begin, region.Tokens.Begin);
            region.Attached.End = std::max(region.Attached.End, region.Tokens.End);
            region.Parent = rebase(region.Parent);
            region.FirstChild = rebase(region.FirstChild);
            region.NextSibling = rebase(region.NextSibling);
            refresh(region);
            result.Regions_.push_back(region);
        }

        if (next != added.size()) {
            return false;
        }

        Vector<RegionId> insertedIds(&scratch);
        insertedIds.reserve(added.size());

        for (auto& item : added) {
            insertedIds.push_back(item.Region);
            item.Parent = rebase(item.Proof.ParentRegion);
        }

        std::ranges::sort(
            added,
            [] (const Insertion& a, const Insertion& b) {
                return a.Proof.ParentRegion != b.Proof.ParentRegion
                    ? a.Proof.ParentRegion < b.Proof.ParentRegion
                    : a.Proof.Token < b.Proof.Token;
            });

        for (us first = 0; first < added.size();) {
            us end = first + 1;

            while (end < added.size() && added[end].Proof.ParentRegion == added[first].Proof.ParentRegion) {
                ++end;
            }

            auto& parent = result.Regions_[added[first].Parent];
            auto child = parent.FirstChild;
            auto previous = NoRegion;

            for (us i = first; i < end; ++i) {
                const auto id = added[i].Region;

                while (child != NoRegion && result.Regions_[child].Tokens.Begin < added[i].Proof.Token) {
                    previous = child;
                    child = result.Regions_[child].NextSibling;
                }

                result.Regions_[id].NextSibling = child;

                if (previous == NoRegion) {
                    parent.FirstChild = id;
                } else {
                    result.Regions_[previous].NextSibling = id;
                }

                previous = id;
            }

            // New standalone comments attach to the next syntax sibling. Native
            // attachment is certified by preserved same-line relationships.
            auto pending = NoToken;

            for (auto id = parent.FirstChild; id != NoRegion; id = result.Regions_[id].NextSibling) {
                auto& region = result.Regions_[id];

                if (std::binary_search(insertedIds.begin(), insertedIds.end(), id)) {
                    pending = std::min(pending, region.Tokens.Begin);
                } else if (region.Kind != parser::NodeKind::Comment && pending != NoToken) {
                    region.Attached.Begin = std::min(region.Attached.Begin, pending);
                    pending = NoToken;
                }
            }

            first = end;
        }

        return true;
    }

    template <bool Consume, bool TokenOnly = false>
    static std::optional<SourceAnalysis> Build(
        const parser::SyntaxTree& original,
        const parser::SyntaxTree& tree,
        const SourceAnalysis& safety,
        std::span<const parser::TokenIndex> mapping,
        std::span<const parser::detail::NormalizedPiece> pieces,
        std::span<const ProtectedSourceRange> inherited,
        std::pmr::memory_resource& file,
        std::pmr::memory_resource& scratch,
        SourceAnalysis* owned = nullptr,
        std::span<const parser::detail::NormalizedCommentInsertion> insertions = {})
    {
        if (!safety.Built_ || safety.HasEditScope_ || mapping.size() != us(original.TokenCount()) + 1) {
            return std::nullopt;
        }

        const auto source = tree.Source();
        const auto size = CheckedSourceAnalysisSize(source.size());
        const auto count = tree.TokenCount();

        if constexpr (Consume) {
            if (owned != &safety || (!TokenOnly && count != original.TokenCount())) {
                return std::nullopt;
            }

            if constexpr (TokenOnly) {
                if (us(count) != us(original.TokenCount()) + insertions.size()) {
                    return std::nullopt;
                }
            }
        }

        Vector<parser::TokenIndex> prior(&scratch);

        if constexpr (!Consume) {
            prior.resize(us(count) + 1, NoToken);

            for (parser::TokenIndex old = 0; old <= original.TokenCount(); ++old) {
                if (mapping[old] != NoToken) {
                    prior[mapping[old]] = old;
                }
            }
        }

        SourceAnalysis result = [&] {
            if constexpr (Consume) {
                return SourceAnalysis(std::move(*owned));
            } else {
                return SourceAnalysis(file);
            }
        }();
        const SourceAnalysis& saved = Consume ? result : safety;
        Vector<SourceGap> oldGaps(&file);
        Vector<SourceRegion> oldRegions(&file);
        const bool inPlace = Consume && (!TokenOnly || insertions.empty());

        if constexpr (TokenOnly) {
            if (!inPlace) {
                oldGaps = std::move(result.Gaps_);
                oldRegions = std::move(result.Regions_);
            }
        }

        const std::span<const SourceGap> cachedGaps = TokenOnly && !inPlace ? oldGaps : saved.Gaps_;
        const std::span<const SourceRegion> cachedRegions = TokenOnly && !inPlace ? oldRegions : saved.Regions_;

        if (!inPlace) {
            result.Gaps_.reserve(us(count) + 1);
            result.Regions_.reserve(cachedRegions.size() + insertions.size() +
                (!TokenOnly && count > original.TokenCount() ? count - original.TokenCount() : 0));
        }

        result.DirectiveFences_.clear();
        result.Protection_.assign(inherited.begin(), inherited.end());

        for (const auto range : inherited) {
            if ((u16(range.Reasons) & ~u16(ProtectionReason::Directive)) != 0) {
                result.DirectiveFences_.push_back(range);
            }
        }

        us piece = 0;
        TK previousKind = TK::Invalid;
        std::string_view previousText;
        auto previousRole = parser::PunctuationRole::None;
        Offset previousEnd = 0;
        parser::TokenIndex oldCursor = 0, previousOld = NoToken;
        us insertedCursor = 0;

        for (parser::TokenIndex i = 0; i <= count; ++i) {
            const auto token = i < count ? tree.Token(i) : tokenizer::Token{};
            const auto text = i < count ? tree.TokenText(i) : std::string_view{};
            const auto role = i < count ? tree.Role(i) : parser::PunctuationRole::None;
            const auto end = i < count ? token.Text.Begin : size;
            parser::TokenIndex old;

            if constexpr (TokenOnly) {
                if (oldCursor <= original.TokenCount() && mapping[oldCursor] == i) {
                    old = oldCursor++;
                } else {
                    if (oldCursor <= original.TokenCount() && mapping[oldCursor] < i) {
                        return std::nullopt;
                    }

                    if (insertedCursor == insertions.size() || insertions[insertedCursor++].Token != i) {
                        return std::nullopt;
                    }

                    old = NoToken;
                }
            } else if constexpr (Consume) {
                if (mapping[i] != i) {
                    return std::nullopt;
                }

                old = i;
            } else {
                old = prior[i];
            }

            while (piece < pieces.size() && pieces[piece].Output.End < previousEnd) {
                ++piece;
            }

            bool reuse;

            if constexpr (TokenOnly) {
                reuse = old != NoToken && (i ? previousOld != NoToken && previousOld + 1 == old : old == 0);
            } else {
                reuse =
                    Consume || (old != NoToken && (i ? prior[i - 1] != NoToken && prior[i - 1] + 1 == old : old == 0));
            }

            previousOld = old;

            if (reuse) {
                const auto& cached = cachedGaps[old];
                const bool copied = piece < pieces.size() && !pieces[piece].Generated &&
                    pieces[piece].Output.Begin <= previousEnd && end <= pieces[piece].Output.End &&
                    pieces[piece].Original.Begin + previousEnd - pieces[piece].Output.Begin == cached.Source.Begin &&
                    pieces[piece].Original.Begin + end - pieces[piece].Output.Begin == cached.Source.End;
                reuse = copied || saved.Original(cached) == source.substr(previousEnd, end - previousEnd);
            }

            // Consuming identity reuse can alias the cached gap. Finish its
            // source comparison and refusal checks before updating that row.
            if (reuse && cachedGaps[old].IntrinsicPreserve && cachedGaps[old].Source.Empty() &&
                (u16(cachedGaps[old].Reasons) & u16(ProtectionReason::Recovery | ProtectionReason::Ambiguous)))
            {
                return std::nullopt;
            }

            auto& gap = inPlace ? result.Gaps_[i] : result.Gaps_.emplace_back();

            if (reuse) {
                gap = cachedGaps[old];
                gap.Source = {previousEnd, end};
                gap.Preserve = gap.IntrinsicPreserve;
                gap.ContextSensitive = gap.IntrinsicContextSensitive;
                gap.Reasons = i && (previousText.ends_with("\\") || previousText.ends_with("\\\r"))
                    ? ProtectionReason::LineSplice
                    : ProtectionReason::None;
            } else {
                ClassifyGap(
                    gap,
                    source,
                    count,
                    i,
                    token,
                    text,
                    role,
                    previousKind,
                    previousText,
                    previousRole,
                    previousEnd);
            }

            if (i == count) {
                break;
            }

            if (token.Kind == TK::PreprocessingDirective) {
                const auto before = token.Text.Begin
                    ? source.rfind('\n', token.Text.Begin - 1)
                    : std::string_view::npos;
                const auto after = source.find('\n', token.Text.End);
                result.Protection_.push_back({
                    {
                        before == std::string_view::npos ? 0 : Offset(before + 1),
                        after == std::string_view::npos ? size : Offset(after + 1),
                    },
                    ProtectionReason::Directive,
                });
            }

            previousKind = token.Kind;
            previousText = text;
            previousRole = role;
            previousEnd = token.Text.End;
        }

        const auto refreshSource = [&] (SourceRegion& region) {
            const auto begin = region.Tokens.Begin < count ? tree.Token(region.Tokens.Begin).Text.Begin : size;
            region.Source = {begin, region.Tokens.Empty() ? begin : tree.Token(region.Tokens.End - 1).Text.End};
        };
        if constexpr (TokenOnly) {
            if (oldCursor != original.TokenCount() + 1 || insertedCursor != insertions.size()) {
                return std::nullopt;
            }

            if (!TransportRegions(result, cachedRegions, mapping, insertions, inPlace, refreshSource, scratch)) {
                return std::nullopt;
            }
        } else {
            const auto refresh = [&] (SourceRegion& region, const parser::Node* node) {
                refreshSource(region);
                region.Attached = node->Tokens;

                if (const auto* trivia = tree.AssociatedTrivia(*node)) {
                    for (auto* item = trivia->Leading.First; item; item = item->Next) {
                        region.Attached.Begin = std::min(region.Attached.Begin, item->Tokens.Begin);
                    }

                    for (auto* item = trivia->Trailing.First; item; item = item->Next) {
                        region.Attached.End = std::max(region.Attached.End, item->Tokens.End);
                    }
                }
            };
            struct Frame
            {
                const parser::Node* Next;
                RegionId Parent;
                RegionId Last = NoRegion;
            };

            Vector<Frame> stack(&scratch);
            stack.push_back({&tree.Root(), NoRegion});
            RegionId oldRegion = 0;
            const auto skipDeleted = [&] {
                if constexpr (Consume) {
                    return;
                }

                while (oldRegion < saved.Regions_.size()) {
                    const auto& region = saved.Regions_[oldRegion];

                    if (region.Kind != parser::NodeKind::Comment || region.Tokens.Size() != 1 ||
                        mapping[region.Tokens.Begin] != NoToken)
                    {
                        break;
                    }

                    ++oldRegion;
                }
            };
            while (!stack.empty()) {
                auto& frame = stack.back();
                const auto* node = frame.Next;

                if (!node) {
                    if constexpr (Consume) {
                        if (frame.Last != NoRegion && result.Regions_[frame.Last].NextSibling != NoRegion) {
                            return std::nullopt;
                        }
                    }

                    stack.pop_back();
                    continue;
                }

                frame.Next = node->NextSibling;
                skipDeleted();
                SourceRegion copiedRegion{};
                SourceRegion* target = &copiedRegion;
                const bool inserted = !Consume && node->Kind == parser::NodeKind::Comment && node->Tokens.Size() == 1 &&
                    prior[node->Tokens.Begin] == NoToken;

                if (!inserted) {
                    if (oldRegion == saved.Regions_.size()) {
                        return std::nullopt;
                    }

                    if constexpr (Consume) {
                        target = &result.Regions_[oldRegion++];
                    } else {
                        copiedRegion = saved.Regions_[oldRegion++];
                    }

                    const auto& region = *target;
                    parser::TokenRange tokens;

                    if constexpr (Consume) {
                        tokens = region.Tokens;
                    } else if (region.Kind == parser::NodeKind::TranslationUnit) {
                        tokens = {0, count};
                    } else if (region.Tokens.Empty()) {
                        tokens = {mapping[region.Tokens.Begin], mapping[region.Tokens.Begin]};
                    } else {
                        tokens = {mapping[region.Tokens.Begin], mapping[region.Tokens.End - 1] + 1};
                    }

                    if (region.Kind != node->Kind || tokens.Begin != node->Tokens.Begin ||
                        tokens.End != node->Tokens.End)
                    {
                        return std::nullopt;
                    }
                }

                auto& region = *target;

                if constexpr (!Consume) {
                    if (result.Regions_.size() >= NoRegion) {
                        throw std::length_error("too many source regions");
                    }

                    const auto id = static_cast<RegionId>(result.Regions_.size());
                    region.Tokens = node->Tokens;
                    refresh(region, node);
                    region.Parent = frame.Parent;
                    region.FirstChild = NoRegion;
                    region.NextSibling = NoRegion;
                    region.Kind = node->Kind;
                    region.Function = node->Function;
                    result.Regions_.push_back(region);

                    if (frame.Parent != NoRegion) {
                        if (frame.Last == NoRegion) {
                            result.Regions_[frame.Parent].FirstChild = id;
                        } else {
                            result.Regions_[frame.Last].NextSibling = id;
                        }

                        frame.Last = id;
                    }

                    if (node->FirstChild) {
                        stack.push_back({node->FirstChild, id});
                    }
                } else {
                    const auto id = oldRegion - 1;

                    // The owned AST retained every original production. Keep the
                    // public correspondence guard while updating no topology fields.
                    if (region.Parent != frame.Parent || region.FirstChild != (node->FirstChild ? id + 1 : NoRegion) ||
                        (frame.Last != NoRegion && result.Regions_[frame.Last].NextSibling != id))
                    {
                        return std::nullopt;
                    }

                    refresh(region, node);
                    frame.Last = id;

                    if (node->FirstChild) {
                        stack.push_back({node->FirstChild, id});
                    }
                }
            }
            skipDeleted();

            if (oldRegion != saved.Regions_.size()) {
                return std::nullopt;
            }
        }
        result.Source_ = source;
        MergeProtection(result.DirectiveFences_);
        MergeProtection(result.Protection_);
        auto nextGap = result.Gaps_.begin();
        const auto gapEnd = result.Gaps_.end();
        const auto beforeRange = [] (const SourceGap& gap, Offset begin) {
            return gap.Source.End < begin || (gap.Source.End == begin && !gap.Source.Empty());
        };

        for (const auto& range : result.Protection_) {
            if (nextGap == gapEnd) {
                break;
            }

            if (nextGap->Source.Begin >= range.Source.End) {
                continue;
            }

            if (beforeRange(*nextGap, range.Source.Begin)) {
                nextGap = std::lower_bound(nextGap, gapEnd, range.Source.Begin, beforeRange);
            }

            while (nextGap != gapEnd && nextGap->Source.Begin < range.Source.End) {
                auto& gap = *nextGap++;
                const bool beforeSpelling = gap.Source.Empty() && gap.Source.Begin == range.Source.Begin &&
                    (range.Reasons == ProtectionReason::HeaderName ||
                        (gap.RecoveryBoundary && range.Reasons == ProtectionReason::Recovery));

                if (!beforeSpelling) {
                    gap.Preserve = true;
                    gap.Reasons = gap.Reasons | range.Reasons;

                    if ((u16(range.Reasons) & u16(ProtectionReason::HeaderName)) != 0) {
                        gap.ContextSensitive = true;
                    }
                }
            }
        }

        result.Built_ = true;
        result.PartialGapPermissions_ = result.CollectPartialGapPermissions(std::nullopt);
        return result;
    }
};

std::optional<SourceAnalysis> ReuseNormalizedSourceAnalysis(
    const parser::SyntaxTree& original,
    const parser::SyntaxTree& tree,
    const SourceAnalysis& safety,
    std::span<const parser::TokenIndex> mapping,
    std::span<const parser::detail::NormalizedPiece> pieces,
    std::span<const ProtectedSourceRange> inherited,
    std::pmr::memory_resource& file,
    std::pmr::memory_resource& scratch)
{
    return detail::NormalizationAnalysisReuse::Build<false>(
        original,
        tree,
        safety,
        mapping,
        pieces,
        inherited,
        file,
        scratch);
}

std::optional<SourceAnalysis> detail::ConsumeIdentityNormalizedSourceAnalysis(
    const parser::SyntaxTree& original,
    const parser::SyntaxTree& tree,
    SourceAnalysis&& safety,
    std::span<const parser::TokenIndex> mapping,
    std::span<const parser::detail::NormalizedPiece> pieces,
    std::span<const ProtectedSourceRange> inherited,
    std::pmr::memory_resource& file,
    std::pmr::memory_resource& scratch)
{
    return detail::NormalizationAnalysisReuse::Build<true>(
        original,
        tree,
        safety,
        mapping,
        pieces,
        inherited,
        file,
        scratch,
        &safety);
}

std::optional<SourceAnalysis> detail::ConsumeNormalizedTokenViewAnalysis(
    const parser::SyntaxTree& original,
    const parser::SyntaxTree& tree,
    SourceAnalysis&& safety,
    std::span<const parser::TokenIndex> mapping,
    std::span<const parser::detail::NormalizedPiece> pieces,
    std::span<const parser::detail::NormalizedCommentInsertion> insertions,
    std::span<const ProtectedSourceRange> inherited,
    std::pmr::memory_resource& file,
    std::pmr::memory_resource& scratch)
{
    return detail::NormalizationAnalysisReuse::Build<true, true>(
        original,
        tree,
        safety,
        mapping,
        pieces,
        inherited,
        file,
        scratch,
        &safety,
        insertions);
}

} // namespace formatter
