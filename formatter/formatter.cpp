#include "formatter.hpp"

#include "diagnostic_locations.hpp"
#include "flat_span_internal.hpp"
#include "format_changes_internal.hpp"
#include "format_pipeline.hpp"
#include "layout_plan.hpp"
#include "namespace_comments_internal.hpp"
#include "source_analysis_internal.hpp"
#include "structural_edits_internal.hpp"
#include "syntax_layout.hpp"
#include "text_policies.hpp"

#include <util/ascii.hpp>

#include <unicode/utf8.hpp>

#include <algorithm>
#include <format>
#include <limits>
#include <memory_resource>
#include <optional>
#include <stdexcept>

namespace formatter {

namespace {

using Range = parser::SourceRange;

using Offset = parser::SourceOffset;

CompiledPolicies PreparePolicies(const Style& style)
{
    auto policies = CompilePolicies(style);

    if (!style.CallLikeMacroNames.empty()) {
        throw std::invalid_argument("CallLikeMacroNames syntax hints are not yet supported by the " "public pipeline");
    }

    if (style.AmbiguousTemplateDeclarationSpacing != GapPolicy::Preserve) {
        throw std::invalid_argument("AmbiguousTemplateDeclarationSpacing is unsupported");
    }

    return policies;
}

NamespaceCommentOptions CommentOptions(const Style& style)
{
    NamespaceCommentPolicy policy = NamespaceCommentPolicy::Preserve;
    switch (style.NamespaceComments) {
        case NamespaceComments::Preserve:
            break;
        case NamespaceComments::AddMissing:
            policy = NamespaceCommentPolicy::AddMissing;
            break;
        case NamespaceComments::Fix:
            policy = NamespaceCommentPolicy::Fix;
            break;
        case NamespaceComments::Remove:
            policy = NamespaceCommentPolicy::Remove;
            break;
    }

    const auto space = style.TrailingCommentSpaces
        ? std::nullopt
        : GapUsesSpace(style.TrailingCommentSpacing, GapContext::Rebuilt);
    return {
        policy,
        style.ShortNamespaceLines,
        space ? (*space ? 1u : 0u) : style.NamespaceCommentSpaces,
        style.LineEnding != LineEnding::Preserve,
    };
}

// Normalized copied pieces can be permuted. Each lookup starts with the
// sorted original protection index, then visits only intersections with
// that piece.
std::pmr::vector<ProtectedSourceRange> ProjectProtection(
    std::span<const ProtectedSourceRange> original,
    std::span<const StructuralProvenance> provenance,
    std::pmr::memory_resource& memory)
{
    std::pmr::vector<ProtectedSourceRange> result(&memory);
    for (const auto& piece : provenance) {
        if (piece.Generated) {
            continue;
        }

        auto it = std::lower_bound(
            original.begin(),
            original.end(),
            piece.Original.Begin,
            [] (const auto& range, Offset begin) {
                return range.Source.End <= begin;
            });

        for (; it != original.end() && it->Source.Begin < piece.Original.End; ++it) {
            const auto begin = std::max(it->Source.Begin, piece.Original.Begin);
            const auto end = std::min(it->Source.End, piece.Original.End);
            result.push_back({
                {piece.Output.Begin + begin - piece.Original.Begin, piece.Output.Begin + end - piece.Original.Begin},
                it->Reasons,
            });
        }
    }

    return result;
}

// Copied bytes retain precisely their selected intersections, even when an
// include permutation changes their order. Literal bytes are authorized by the
// safe atomic original plan: materialization validates operation ownership and
// gives every literal the consuming edit (or strict insertion point). Neither
// map anchors nor GeneratedAdjacencies confer byte or endpoint authority.
std::pmr::vector<Range> ProjectEditScope(
    std::span<const Range> original,
    const SourceAnalysis& safety,
    const StructuralPlan& plan,
    std::span<const StructuralProvenance> provenance,
    std::pmr::memory_resource& scratch)
{
    std::pmr::vector<Range> result(&scratch);

    for (const auto& piece : provenance) {
        if (piece.Generated) {
            // The planner accepted the whole operation, including both brace
            // insertions. Replacement literals inherit only its byte authority;
            // their output End is still excluded unless selected copied bytes
            // follow. A deletion emits no literal and grants no new authority.
            if (piece.Operation == NoStructuralAction ||
                ((plan.Operations[piece.Operation].Kind == StructuralAction::DirectiveWhitespace ||
                    plan.Operations[piece.Operation].Kind == StructuralAction::IncludeDelimiters ||
                    plan.Operations[piece.Operation].Kind == StructuralAction::IncludeOrder)
                    ? !CanRewriteTextAtom(safety, piece.Original, true)
                    : safety.IntersectsImmutable(piece.Original)))
            {
                throw std::runtime_error("unauthorized structural literal");
            }

            if (!piece.Output.Empty()) {
                result.push_back(piece.Output);
            }

            continue;
        }

        auto it = std::lower_bound(
            original.begin(),
            original.end(),
            piece.Original.Begin,
            [] (Range range, Offset begin) {
                return range.End <= begin;
            });

        for (; it != original.end() && it->Begin < piece.Original.End; ++it) {
            const auto begin = std::max(it->Begin, piece.Original.Begin);
            const auto end = std::min(it->End, piece.Original.End);
            result.push_back({
                piece.Output.Begin + begin - piece.Original.Begin,
                piece.Output.Begin + end - piece.Original.Begin,
            });
        }
    }

    std::sort(
        result.begin(),
        result.end(),
        [] (Range a, Range b) {
            return a.Begin < b.Begin;
        });
    size_t count = 0;

    for (const auto range : result) {
        if (count && range.Begin <= result[count - 1].End) {
            result[count - 1].End = std::max(result[count - 1].End, range.End);
        } else {
            result[count++] = range;
        }
    }

    result.resize(count);
    return result;
}

std::pmr::vector<GapId> GeneratedGaps(
    const SourceAnalysis& safety,
    std::span<const GeneratedAdjacency> adjacency,
    std::span<const StructuralProvenance> provenance,
    std::pmr::vector<GapId>& reassembled,
    std::pmr::memory_resource& scratch)
{
    if (adjacency.empty()) {
        return std::pmr::vector<GapId>(&scratch);
    }
    std::pmr::vector<Range> ranges(&scratch);

    for (const auto& a : adjacency) {
        ranges.push_back(a.Output);
    }

    std::sort(
        ranges.begin(),
        ranges.end(),
        [] (Range a, Range b) {
            return a.Begin < b.Begin;
        });
    size_t merged = 0;

    for (const auto range : ranges) {
        if (merged && range.Begin <= ranges[merged - 1].End) {
            ranges[merged - 1].End = std::max(ranges[merged - 1].End, range.End);
        } else {
            ranges[merged++] = range;
        }
    }

    ranges.resize(merged);
    std::pmr::vector<GapId> result(&scratch);
    size_t interval = 0;
    size_t piece = 0;

    for (GapId i = 0; i < safety.Gaps().size(); ++i) {
        const auto range = safety.Gaps()[i].Source;

        while (interval < ranges.size() && ranges[interval].End < range.Begin) {
            ++interval;
        }

        // Inclusive touching is intentional, especially at zero-width
        // deletion.
        if (interval < ranges.size() && ranges[interval].Begin <= range.End && !safety.Gaps()[i].Preserve) {
            result.push_back(i);

            // Provenance is emitted in output order. A copied gap inside a
            // larger structural replacement still has authored line breaks;
            // whitespace joined across a deletion does not.
            while (piece < provenance.size() && provenance[piece].Output.End <= range.Begin) {
                ++piece;
            }

            if (range.Begin < range.End &&
                (piece == provenance.size() || provenance[piece].Generated ||
                    range.Begin < provenance[piece].Output.Begin || range.End > provenance[piece].Output.End))
            {
                reassembled.push_back(i);
            }
        }
    }

    return result;
}

std::pmr::vector<layout::Identity> OriginalIdentities(
    const parser::SyntaxTree& tree,
    const parser::SyntaxTree& original,
    const StructuralMaterialization* normalized,
    std::pmr::memory_resource& scratch)
{
    std::pmr::vector<layout::Identity> identities(&scratch);
    if (!normalized) {
        // Empty storage represents unchanged original token numbering.
        return identities;
    }

    identities.resize(tree.TokenCount(), layout::NoIdentity);

    parser::TokenIndex next = 0;

    // Copied provenance is already in output order, including include
    // permutations. Recover identities in that order instead of sorting a
    // second full-file survivor table.
    for (const auto& piece : normalized->Provenance) {
        if (piece.Generated) {
            continue;
        }

        parser::TokenIndex first = 0, last = original.TokenCount();

        while (first < last) {
            const auto middle = first + (last - first) / 2;

            if (original.Token(middle).Text.Begin < piece.Original.Begin) {
                first = middle + 1;
            } else {
                last = middle;
            }
        }

        for (auto id = first; id < original.TokenCount(); ++id) {
            if (original.Token(id).Text.Begin >= piece.Original.End) {
                break;
            }

            const auto& expected = normalized->OriginalTokens[id];

            while (next < tree.TokenCount() && tree.Token(next).Text.End <= expected.Output.Begin) {
                ++next;
            }

            if (!expected.Present || next == tree.TokenCount()) {
                throw std::runtime_error("normalization lost an original token identity");
            }

            const auto range = tree.Token(next).Text;

            if (range.Begin != expected.Output.Begin || range.End != expected.Output.End) {
                throw std::runtime_error("normalization changed an original token identity");
            }

            identities[next++] = id;
        }
    }

    return identities;
}

void MergeProtection(std::vector<ProtectedSourceRange>& ranges)
{
    std::sort(
        ranges.begin(),
        ranges.end(),
        [] (const auto& a, const auto& b) {
            return a.Source.Begin < b.Source.Begin;
        });
    size_t count = 0;

    for (const auto& range : ranges) {
        if (count && range.Source.Begin <= ranges[count - 1].Source.End) {
            ranges[count - 1].Source.End = std::max(ranges[count - 1].Source.End, range.Source.End);
            ranges[count - 1].Reasons = ranges[count - 1].Reasons | range.Reasons;
        } else {
            ranges[count++] = range;
        }
    }

    ranges.resize(count);
}

layout::Identity RecordRenderAnchors(
    FormatResult& result,
    const layout::Mapping& map,
    Range output,
    const SourceAnalysis& safety,
    std::span<const layout::Identity> identities)
{
    layout::Identity original = layout::NoIdentity;

    if (map.Source.Identity != layout::NoIdentity) {
        const auto count = identities.empty() ? result.OriginalTokens.size() : identities.size();

        if (map.Source.Identity >= count) {
            throw std::runtime_error("invalid rendered token identity");
        }

        original = identities.empty() ? map.Source.Identity : identities[map.Source.Identity];

        if (original != layout::NoIdentity) {
            result.OriginalTokens[original] = {output, true};
        }
    }

    if (map.Exact && map.Source.Begin != layout::InvalidId) {
        const auto source = Range{map.Source.Begin, map.Source.End};

        if (output.End - output.Begin != source.End - source.Begin) {
            throw std::runtime_error("invalid exact render mapping");
        }

        auto it = std::lower_bound(
            safety.Protection().begin(),
            safety.Protection().end(),
            source.Begin,
            [] (const auto& range, Offset begin) {
                return range.Source.End <= begin;
            });

        for (; it != safety.Protection().end() && it->Source.Begin < source.End; ++it) {
            const auto begin = std::max(source.Begin, it->Source.Begin);
            const auto end = std::min(source.End, it->Source.End);
            result.OutputProtection
                .push_back({{output.Begin + begin - source.Begin, output.Begin + end - source.Begin}, it->Reasons});
        }
    }

    return original;
}

void PruneTextOnlyNamespaceNoOps(
    NamespaceCommentPlan& plan,
    const parser::SyntaxTree& original,
    const parser::SyntaxTree& tree,
    const StructuralMaterialization* normalized,
    const detail::LayoutPlan& layout,
    std::pmr::memory_resource& scratch,
    const std::pmr::vector<detail::CopiedTokenRun>* copied = nullptr)
{
    using TI = parser::TokenIndex;
    constexpr auto none = std::numeric_limits<TI>::max();
    const auto count = original.TokenCount();

    if (plan.Namespaces.empty() || (normalized && !copied && normalized->OriginalTokens.size() != count)) {
        return;
    }

    // Keep malformed identity plans on the ordinary resolver's error path.
    std::pmr::vector<size_t> active(&scratch);
    TI previousOpen = 0;

    for (size_t i = 0; i < plan.Namespaces.size(); ++i) {
        const auto& ns = plan.Namespaces[i];

        if (ns.Open >= ns.Close || ns.Close >= count || ns.Header.Begin >= ns.Header.End || ns.Header.End != ns.Open ||
            ns.Comment > count || (i && ns.Open <= previousOpen))
        {
            return;
        }

        previousOpen = ns.Open;

        while (!active.empty() && plan.Namespaces[active.back()].Close < ns.Open) {
            active.pop_back();
        }

        if (!active.empty() && ns.Close >= plan.Namespaces[active.back()].Close) {
            return;
        }

        active.push_back(i);
    }

    const auto currentToken = [&] (TI token) -> TI {
        if (token >= count) {
            return none;
        }

        if (!normalized) {
            return token;
        }

        TokenOutputOffset expected;

        if (copied) {
            const auto* run = detail::FindCopiedTokenRun(*copied, token);

            if (!run) {
                return none;
            }

            expected = detail::CopiedTokenPosition(original, *run, token);
        } else {
            expected = normalized->OriginalTokens[token];
        }

        if (!expected.Present) {
            return none;
        }

        TI first = 0, last = tree.TokenCount();

        while (first < last) {
            const auto middle = first + (last - first) / 2;

            if (tree.Token(middle).Text.Begin < expected.Output.Begin) {
                first = middle + 1;
            } else {
                last = middle;
            }
        }

        if (first == tree.TokenCount()) {
            return none;
        }

        const auto actual = tree.Token(first).Text;
        return actual.Begin == expected.Output.Begin && actual.End == expected.Output.End ? first : none;
    };
    const auto noOp = [&] (const NamespaceIdentity& ns) {
        const bool existing = ns.Comment != count;

        if (ns.ExplanatoryComment) {
            return plan.Options.Policy == NamespaceCommentPolicy::Remove;
        }

        if (plan.Options.Policy == NamespaceCommentPolicy::AddMissing && existing) {
            return true;
        }

        if (plan.Options.Policy == NamespaceCommentPolicy::Remove && !existing) {
            return true;
        }

        if (plan.Options.Policy != NamespaceCommentPolicy::Fix || !existing) {
            return false;
        }

        const auto close = currentToken(ns.Close);
        const auto comment = currentToken(ns.Comment);

        if (close == none || comment == none || close + 1 != comment || !layout.TokenSurvives(close) ||
            !layout.TokenSurvives(comment) || !layout.TokenSpellingExact(close) ||
            !layout.TokenSpellingExact(comment) || tree.Kind(close) != tokenizer::TokenKind::Operator_CloseBrace ||
            tree.Kind(comment) != tokenizer::TokenKind::Comment || !layout.GapEmitsSpaces(comment, plan.Options.Spaces))
        {
            return false;
        }

        auto text = tree.TokenText(comment);
        const auto consume = [&] (std::string_view expected) {
            if (!text.starts_with(expected)) {
                return false;
            }

            text.remove_prefix(expected.size());
            return true;
        };

        if (!consume("// namespace")) {
            return false;
        }

        bool firstName = true;

        for (TI token = ns.Header.Begin; token < ns.Header.End; ++token) {
            const auto kind = original.Kind(token);

            if (kind != tokenizer::TokenKind::Identifier && kind != tokenizer::TokenKind::Operator_ScopeResolution) {
                continue;
            }

            if (firstName) {
                if (!consume(" ")) {
                    return false;
                }

                firstName = false;
            }

            if (!consume(original.TokenText(token))) {
                return false;
            }
        }

        // CRLF and noncanonical suffixes use the ordinary resolver. Any
        // failed ownership/validity check there skips this identical edit.
        return text.empty();
    };
    size_t retained = 0;

    for (const auto& ns : plan.Namespaces) {
        if (!noOp(ns)) {
            plan.Namespaces[retained++] = ns;
        }
    }

    plan.Namespaces.resize(retained);
}

// Successful consuming reuse built copied token records directly from these
// source pieces. Check the compact endpoints/count before exposing affine
// token indices; this certificate is never inferred from endpoint checks alone.
bool CertifyCopiedTokenRuns(
    const parser::SyntaxTree& tree,
    const parser::SyntaxTree& original,
    std::span<detail::CopiedTokenRun> copied)
{
    for (auto& run : copied) {
        const auto begin = detail::CopiedTokenPosition(original, run, run.Original.Begin).Output;
        const auto end = detail::CopiedTokenPosition(original, run, run.Original.End - 1).Output;
        parser::TokenIndex first = 0, last = tree.TokenCount();

        while (first < last) {
            const auto middle = first + (last - first) / 2;

            if (tree.Token(middle).Text.Begin < begin.Begin) {
                first = middle + 1;
            } else {
                last = middle;
            }
        }

        if (first == tree.TokenCount() || run.Original.Size() > tree.TokenCount() - first) {
            return false;
        }

        const auto firstToken = tree.Token(first).Text;
        const auto lastToken = tree.Token(first + run.Original.Size() - 1).Text;

        if (firstToken.Begin != begin.Begin || firstToken.End != begin.End || lastToken.Begin != end.Begin ||
            lastToken.End != end.End)
        {
            return false;
        }

        run.NormalizedBegin = first;
    }

    return true;
}

struct SparseNamespaceAnchors
{
    struct Token
    {
        parser::TokenIndex Normalized;
        uint32_t Position;
    };

    std::pmr::vector<detail::NamespaceTokenPosition> Positions;
    std::pmr::vector<Token> Tokens;

    explicit SparseNamespaceAnchors(std::pmr::memory_resource& memory)
        : Positions(&memory)
        , Tokens(&memory)
    { }
};

std::optional<SparseNamespaceAnchors> PrepareSparseNamespaceAnchors(
    const parser::SyntaxTree& tree,
    const parser::SyntaxTree& original,
    const StructuralMaterialization* normalized,
    const NamespaceCommentPlan& namespaces,
    const detail::LayoutPlan& layout,
    std::pmr::memory_resource& file,
    std::pmr::memory_resource& scratch,
    const std::pmr::vector<detail::CopiedTokenRun>* copied = nullptr)
{
    using TI = parser::TokenIndex;
    constexpr auto none = std::numeric_limits<TI>::max();
    const auto count = original.TokenCount();

    if (normalized && !copied && normalized->OriginalTokens.size() != count) {
        return std::nullopt;
    }

    // A present identity must still designate one complete normalized token.
    // The materializer proves its bytes are copied verbatim from original;
    // validating endpoints rules out a token formed across a piece splice.
    if (normalized && !copied) {
        TI next = 0;

        for (const auto& piece : normalized->Provenance) {
            if (piece.Generated) {
                continue;
            }

            TI first = 0, last = count;

            while (first < last) {
                const auto middle = first + (last - first) / 2;

                if (original.Token(middle).Text.Begin < piece.Original.Begin) {
                    first = middle + 1;
                } else {
                    last = middle;
                }
            }

            for (auto id = first; id < count && original.Token(id).Text.Begin < piece.Original.End; ++id) {
                const auto& expected = normalized->OriginalTokens[id];

                while (next < tree.TokenCount() && tree.Token(next).Text.End <= expected.Output.Begin) {
                    ++next;
                }

                if (!expected.Present || next == tree.TokenCount()) {
                    return std::nullopt;
                }

                const auto actual = tree.Token(next++).Text;

                if (actual.Begin != expected.Output.Begin || actual.End != expected.Output.End) {
                    return std::nullopt;
                }
            }
        }
    }

    const auto normalizedToken = [&] (TI token) -> TI {
        if (token >= count) {
            return none;
        }

        if (!normalized) {
            return token < tree.TokenCount() ? token : none;
        }

        if (copied) {
            const auto* run = detail::FindCopiedTokenRun(*copied, token);
            return run ? run->NormalizedBegin + token - run->Original.Begin : none;
        }

        const auto& position = normalized->OriginalTokens[token];

        if (!position.Present) {
            return none;
        }

        TI first = 0, last = tree.TokenCount();

        while (first < last) {
            const auto middle = first + (last - first) / 2;

            if (tree.Token(middle).Text.Begin < position.Output.Begin) {
                first = middle + 1;
            } else {
                last = middle;
            }
        }

        if (first == tree.TokenCount()) {
            return none;
        }

        const auto actual = tree.Token(first).Text;
        return actual.Begin == position.Output.Begin && actual.End == position.Output.End ? first : none;
    };
    const auto positionOf = [&] (TI token) {
        if (copied) {
            const auto* run = detail::FindCopiedTokenRun(*copied, token);
            return run ? detail::CopiedTokenPosition(original, *run, token) : TokenOutputOffset{};
        }

        return normalized->OriginalTokens[token];
    };
    const auto survives = [&] (TI token) {
        const auto id = normalizedToken(token);
        return id != none && layout.TokenSurvives(id);
    };

    const auto previousSurvivor = [&] (TI before) {
        if (!copied) {
            while (before) {
                if (survives(--before)) {
                    return before;
                }
            }

            return none;
        }

        auto run = std::lower_bound(
            copied->begin(),
            copied->end(),
            before,
            [] (const auto& item, TI token) {
                return item.Original.Begin < token;
            });

        while (run != copied->begin()) {
            --run;
            auto token = std::min(before, run->Original.End);

            while (token > run->Original.Begin) {
                --token;

                if (layout.TokenSurvives(run->NormalizedBegin + token - run->Original.Begin)) {
                    return token;
                }
            }

            before = run->Original.Begin;
        }

        return none;
    };
    const auto nextSurvivor = [&] (TI after) {
        if (!copied) {
            for (; after < count; ++after) {
                if (survives(after)) {
                    return after;
                }
            }

            return none;
        }

        auto run = std::lower_bound(
            copied->begin(),
            copied->end(),
            after,
            [] (const auto& item, TI token) {
                return item.Original.End <= token;
            });

        for (; run != copied->end(); ++run) {
            for (auto token = std::max(after, run->Original.Begin); token < run->Original.End; ++token) {
                if (layout.TokenSurvives(run->NormalizedBegin + token - run->Original.Begin)) {
                    return token;
                }
            }

            after = run->Original.End;
        }

        return none;
    };

    struct Interior
    {
        Offset Begin = UINT32_MAX;
        Offset End = 0;

        void Merge(const Interior& other)
        {
            Begin = std::min(Begin, other.Begin);
            End = std::max(End, other.End);
        }
    };

    std::pmr::vector<Interior> interiors(namespaces.Namespaces.size(), &scratch);
    std::pmr::vector<size_t> active(&scratch);
    size_t nextNamespace = 0;

    // Include permutations may reorder identities. Prove containment in
    // normalized coordinates once, before relying on monotonic rendering.
    for (TI token = 0; normalized && !copied && token < count; ++token) {
        while (!active.empty() && namespaces.Namespaces[active.back()].Close == token) {
            const auto closed = active.back();
            active.pop_back();

            if (!active.empty()) {
                interiors[active.back()].Merge(interiors[closed]);
            }
        }

        if (!active.empty() && normalized->OriginalTokens[token].Present) {
            const auto range = normalized->OriginalTokens[token].Output;
            interiors[active.back()].Merge({range.Begin, range.End});
        }

        while (nextNamespace < namespaces.Namespaces.size() && namespaces.Namespaces[nextNamespace].Open == token) {
            const auto& ns = namespaces.Namespaces[nextNamespace];

            if (ns.Close <= token || ns.Close >= count || ns.Header.Begin >= ns.Header.End || ns.Header.End != token ||
                (!active.empty() && ns.Close >= namespaces.Namespaces[active.back()].Close))
            {
                return std::nullopt;
            }

            active.push_back(nextNamespace++);
        }
    }

    if (normalized && !copied && (!active.empty() || nextNamespace != namespaces.Namespaces.size())) {
        return std::nullopt;
    }

    if (copied) {
        // Within an exact copied run, token order is affine. Its two extrema
        // prove containment for the whole intersecting namespace interior.
        // Bound repeated overlap work for deeply nested scopes; the dense
        // resolver remains the linear fallback for that unusual shape.
        size_t work = 0;
        const auto budget = 4 * (copied->size() + namespaces.Namespaces.size() + 1);

        for (size_t i = 0; i < namespaces.Namespaces.size(); ++i) {
            const auto& ns = namespaces.Namespaces[i];

            if (ns.Open >= ns.Close || ns.Close >= count) {
                return std::nullopt;
            }

            auto run = std::lower_bound(
                copied->begin(),
                copied->end(),
                ns.Open + 1,
                [] (const auto& item, TI token) {
                    return item.Original.End <= token;
                });

            for (; run != copied->end() && run->Original.Begin < ns.Close; ++run) {
                if (++work > budget) {
                    return std::nullopt;
                }

                const auto begin = std::max(run->Original.Begin, ns.Open + 1);
                const auto end = std::min(run->Original.End, ns.Close);

                if (begin == end) {
                    continue;
                }

                const auto first = tree.Token(run->NormalizedBegin + begin - run->Original.Begin).Text;
                const auto last = tree.Token(run->NormalizedBegin + end - 1 - run->Original.Begin).Text;
                interiors[i].Merge({first.Begin, last.End});
            }
        }
    }

    SparseNamespaceAnchors result(file);
    bool supported = true;
    const auto add = [&] (TI originalId) {
        const auto id = normalizedToken(originalId);

        if (id == none || !layout.TokenSurvives(id)) {
            return;
        }

        // Required anchor tokens must have exact maps. A changed block-comment
        // neighbor uses the dense resolver, whose spelling allowance remains.
        if (!layout.TokenSpellingExact(id)) {
            supported = false;
            return;
        }

        result.Positions.push_back({originalId, {}});
    };
    TI previousOpen = 0;
    active.clear();

    for (size_t i = 0; i < namespaces.Namespaces.size(); ++i) {
        const auto& ns = namespaces.Namespaces[i];

        if (ns.Open >= ns.Close || ns.Close >= count || ns.Header.Begin >= ns.Header.End || ns.Header.End != ns.Open ||
            (i && ns.Open <= previousOpen))
        {
            return std::nullopt;
        }

        previousOpen = ns.Open;

        while (!active.empty() && namespaces.Namespaces[active.back()].Close < ns.Open) {
            active.pop_back();
        }

        if (!active.empty() && ns.Close >= namespaces.Namespaces[active.back()].Close) {
            return std::nullopt;
        }

        active.push_back(i);

        if (normalized) {
            const auto open = positionOf(ns.Open);
            const auto close = positionOf(ns.Close);
            const auto& interior = interiors[i];

            if (!open.Present || !close.Present || open.Output.End > close.Output.Begin ||
                (interior.Begin != UINT32_MAX &&
                    (interior.Begin < open.Output.End || interior.End > close.Output.Begin)))
            {
                return std::nullopt;
            }
        }

        for (TI token = ns.Header.Begin; token < ns.Header.End; ++token) {
            add(token);
        }

        add(ns.Open);
        add(ns.Close);

        if (ns.Comment != count) {
            add(ns.Comment);
        }

        const auto previous = previousSurvivor(ns.Header.Begin);
        const auto next = nextSurvivor(ns.Close + 1);

        if (previous != none) {
            add(previous);
        }

        if (next != none) {
            add(next);
        }
    }

    if (!supported) {
        return std::nullopt;
    }

    std::sort(
        result.Positions.begin(),
        result.Positions.end(),
        [] (const auto& a, const auto& b) {
            return a.Original < b.Original;
        });
    result.Positions.erase(
        std::unique(
            result.Positions.begin(),
            result.Positions.end(),
            [] (const auto& a, const auto& b) {
                return a.Original == b.Original;
            }),
        result.Positions.end());

    for (uint32_t i = 0; i < result.Positions.size(); ++i) {
        result.Tokens.push_back({normalizedToken(result.Positions[i].Original), i});
    }

    std::sort(
        result.Tokens.begin(),
        result.Tokens.end(),
        [] (const auto& a, const auto& b) {
            return a.Normalized < b.Normalized;
        });
    return result;
}

void ComposeSparseNamespaceAnchors(
    FormatResult& result,
    const layout::Result& rendered,
    const SourceAnalysis& safety,
    SparseNamespaceAnchors& anchors)
{
    size_t next = 0;

    for (const auto& map : rendered.Map) {
        const auto output = Range{CheckedSourceAnalysisSize(map.OutputBegin), CheckedSourceAnalysisSize(map.OutputEnd)};

        if (map.Source.Identity != layout::NoIdentity) {
            while (next < anchors.Tokens.size() && anchors.Tokens[next].Normalized < map.Source.Identity) {
                ++next;
            }

            if (next < anchors.Tokens.size() && anchors.Tokens[next].Normalized == map.Source.Identity) {
                anchors.Positions[anchors.Tokens[next].Position].Position = {output, true};
                ++next;
            }
        }

        auto protection = map;
        protection.Source.Identity = layout::NoIdentity;
        (void)RecordRenderAnchors(result, protection, output, safety, {});
    }

    MergeProtection(result.OutputProtection);
}

void ComposeNamespaceAnchors(
    FormatResult& result,
    const layout::Result& rendered,
    const SourceAnalysis& safety,
    std::span<const layout::Identity> identities)
{
    for (const auto& map : rendered.Map) {
        const auto output = Range{CheckedSourceAnalysisSize(map.OutputBegin), CheckedSourceAnalysisSize(map.OutputEnd)};
        (void)RecordRenderAnchors(result, map, output, safety, identities);
    }

    MergeProtection(result.OutputProtection);
}

void ComposeMaps(
    FormatResult& result,
    const layout::Result& rendered,
    const SourceAnalysis& safety,
    std::span<const layout::Identity> identities,
    std::span<const StructuralProvenance> provenance)
{
    result.Map.reserve(rendered.Map.size());

    for (const auto& map : rendered.Map) {
        const auto output = Range{CheckedSourceAnalysisSize(map.OutputBegin), CheckedSourceAnalysisSize(map.OutputEnd)};
        const auto original = RecordRenderAnchors(result, map, output, safety, identities);

        if (map.Source.Begin == layout::InvalidId) {
            result.Map.push_back({output, {}, original, NoStructuralAction, false, map.Exact, map.Protected, true});
            continue;
        }

        const auto source = Range{map.Source.Begin, map.Source.End};
        // A zero-width syntax insertion anchor belongs to the preceding
        // piece, including at a copied/generated boundary or after a moved
        // include.
        auto it = std::lower_bound(
            provenance.begin(),
            provenance.end(),
            source.Begin,
            [&] (const auto& piece, Offset begin) {
                return source.Empty() ? piece.Output.End < begin : piece.Output.End <= begin;
            });
        bool added = false;

        for (;
            it != provenance.end() &&
            (it->Output.Begin < source.End || (source.Empty() && it->Output.Begin <= source.Begin));
            ++it)
        {
            const auto begin = std::max(source.Begin, it->Output.Begin);
            const auto end = std::min(source.End, it->Output.End);
            auto mappedOutput = output;

            if (map.Exact) {
                mappedOutput = {output.Begin + begin - source.Begin, output.Begin + end - source.Begin};
            }

            const auto origin = it->Generated
                ? it->Original
                : Range{it->Original.Begin + begin - it->Output.Begin, it->Original.Begin + end - it->Output.Begin};
            result.Map.push_back({
                mappedOutput,
                origin,
                original,
                it->Operation,
                true,
                map.Exact && !it->Generated,
                map.Protected,
                original == layout::NoIdentity && (!map.Exact || it->Generated),
            });
            added = true;

            if (source.Empty()) {
                break;
            }
        }

        if (!added) {
            result.Map.push_back({output, {}, original, NoStructuralAction, false, false, map.Protected, true});
        }
    }

    MergeProtection(result.OutputProtection);
}

// Comment edits have output coordinates. Copy maps through the same pieces,
// dropping consumed identities and emitting honest generated comment
// mappings.
void ApplyComments(
    FormatResult& result,
    const StructuralPlan& comments,
    const parser::SyntaxTree& original,
    const NamespaceCommentPlan& namespacePlan,
    bool sourceMap,
    std::pmr::memory_resource& scratch)
{
    const auto actionBase = result.StructuralOperations.size();

    if (sourceMap) {
        result.StructuralOperations
            .insert(result.StructuralOperations.end(), comments.Operations.begin(), comments.Operations.end());
    }
    std::pmr::vector<NamespaceIdentity> namespaces(
        namespacePlan.Namespaces.begin(), namespacePlan.Namespaces.end(),
        &scratch);
    std::sort(
        namespaces.begin(),
        namespaces.end(),
        [] (const auto& a, const auto& b) {
            return a.Owner.Begin < b.Owner.Begin;
        });
    struct Copy
    {
        Range Before;
        Range After;
    };

    std::pmr::vector<Copy> copies(&scratch);
    std::pmr::vector<FormatMapping> generated(&scratch);
    copies.reserve(comments.Edits.size() + 1);

    if (sourceMap) {
        generated.reserve(comments.Edits.size());
    }

    Offset cursor = 0;
    Offset output = 0;

    for (const auto& edit : comments.Edits) {
        copies.push_back({{cursor, edit.Source.Begin}, {output, output + edit.Source.Begin - cursor}});
        output += edit.Source.Begin - cursor;
        const auto begin = output;

        for (auto p = edit.PieceBegin; p < edit.PieceEnd; ++p) {
            output += CheckedSourceAnalysisSize(comments.Pieces[p].Literal.size());
        }

        if (begin != output) {
            const auto owner = comments.Operations[edit.Operation].Owner;
            const auto identity = std::lower_bound(
                namespaces.begin(),
                namespaces.end(),
                owner.Begin,
                [] (const auto& ns, parser::TokenIndex token) {
                    return ns.Owner.Begin < token;
                });

            if (identity == namespaces.end() || identity->Owner.Begin != owner.Begin) {
                throw std::runtime_error("missing namespace action identity");
            }

            const auto anchor = original.Token(identity->Close).Text.End;

            if (sourceMap) {
                generated.push_back({
                    {begin, output},
                    {anchor, anchor},
                    layout::NoIdentity,
                    StructuralActionId(actionBase + edit.Operation),
                    true,
                    false,
                    false,
                    true,
                });
            }
        }

        cursor = edit.Source.End;
    }

    const auto size = CheckedSourceAnalysisSize(result.Text.size());
    copies.push_back({{cursor, size}, {output, output + size - cursor}});
    auto containing = [&] (Range range) -> const Copy* {
        auto it = std::lower_bound(
            copies.begin(),
            copies.end(),
            range.Begin,
            [] (const auto& copy, Offset begin) {
                return copy.Before.End <= begin;
            });

        if (it != copies.end() && it->Before.Begin <= range.Begin && range.End <= it->Before.End) {
            return &*it;
        }

        return nullptr;
    };

    for (auto& token : result.OriginalTokens) {
        if (!token.Present) {
            continue;
        }

        if (const auto* copy = containing(token.Output)) {
            token.Output = {
                copy->After.Begin + token.Output.Begin - copy->Before.Begin,
                copy->After.Begin + token.Output.End - copy->Before.Begin,
            };
        } else {
            token.Present = false;
        }
    }

    for (auto& range : result.OutputProtection) {
        const auto* copy = containing(range.Source);

        if (!copy) {
            throw std::runtime_error("namespace edit intersects output protection");
        }

        range.Source = {
            copy->After.Begin + range.Source.Begin - copy->Before.Begin,
            copy->After.Begin + range.Source.End - copy->Before.Begin,
        };
    }

    if (!sourceMap) {
        return;
    }
    std::vector<FormatMapping> maps;
    maps.reserve(result.Map.size() + generated.size());

    for (const auto& map : result.Map) {
        auto it = std::lower_bound(
            copies.begin(),
            copies.end(),
            map.Output.Begin,
            [] (const auto& copy, Offset begin) {
                return copy.Before.End <= begin;
            });

        for (; it != copies.end() && it->Before.Begin < map.Output.End; ++it) {
            const auto begin = std::max(map.Output.Begin, it->Before.Begin);
            const auto end = std::min(map.Output.End, it->Before.End);
            auto next = map;
            next.Output = {it->After.Begin + begin - it->Before.Begin, it->After.Begin + end - it->Before.Begin};

            if (map.Exact && map.HasOriginal) {
                next.Original =
                    {map.Original.Begin + begin - map.Output.Begin, map.Original.Begin + end - map.Output.Begin};
            }

            // A comment edit may retain token-owned CR while consuming
            // the rest of that token. The byte remains exact, not the ID.
            if (next.OriginalToken != layout::NoIdentity && !result.OriginalTokens[next.OriginalToken].Present) {
                next.OriginalToken = layout::NoIdentity;
            }

            maps.push_back(next);
        }
    }

    maps.insert(maps.end(), generated.begin(), generated.end());
    std::stable_sort(
        maps.begin(),
        maps.end(),
        [] (const auto& a, const auto& b) {
            return a.Output.Begin < b.Output.Begin;
        });
    result.Map = std::move(maps);
}

template <bool locations>
void WidthDiagnostics(FormatResult& result, const CompiledPolicies& policies, uint8_t tabWidth)
{
    if (policies.ColumnLimit == 0 || policies.WidthDiagnostics != WidthDiagnosticPolicy::Report) {
        return;
    }

    uint32_t line = 1;
    size_t width = 0;
    uint32_t codePointColumn = 1;
    std::optional<parser::SourceOffset> overflow;
    uint32_t caretColumn = 1;
    auto report = [&] {
        if (width > policies.ColumnLimit) {
            result.Diagnostics.push_back({
                {line, uint32_t(policies.ColumnLimit + 1)},
                std::format("emitted line width {} exceeds ColumnLimit {}", width, policies.ColumnLimit),
                LayoutDiagnosticKind::LineWidth,
                locations ? *overflow : 0,
                locations ? caretColumn : 1,
            });
        }
    };

    for (size_t i = 0; i < result.Text.size(); ++i) {
        const auto plain = util::PlainAsciiPrefix(std::string_view(result.Text).substr(i));

        if (plain) {
            if constexpr (locations) {
                if (!overflow && width + plain > policies.ColumnLimit) {
                    const auto remaining = policies.ColumnLimit > width ? policies.ColumnLimit - width : 0;
                    overflow = CheckedSourceAnalysisSize(i + remaining);
                    caretColumn = codePointColumn + uint32_t(remaining);
                }

                codePointColumn += uint32_t(plain);
            }

            width += plain;
            i += plain;

            if (i == result.Text.size()) {
                break;
            }
        }

        const auto c = result.Text[i];

        if (c == '\n' || c == '\r') {
            report();
            ++line;
            width = 0;

            if constexpr (locations) {
                codePointColumn = 1;
                overflow.reset();
            }

            if (c == '\r' && i + 1 < result.Text.size() && result.Text[i + 1] == '\n') {
                ++i;
            }
        } else {
            width += c == '\t' ? tabWidth - width % tabWidth : 1;

            if constexpr (locations) {
                if (!overflow && width > policies.ColumnLimit) {
                    overflow = CheckedSourceAnalysisSize(i);
                    caretColumn = codePointColumn;
                }

                ++codePointColumn;
            }

            // One column per code point; invalid UTF8 bytes each occupy one.
            if (static_cast<unsigned char>(c) >= 0x80) {
                i += unicode::utf8::GetFirstCodePointSize(std::string_view(result.Text).substr(i)).value_or(1) - 1;
            }
        }
    }

    report();
}

} // namespace

PreparedFormat::PreparedFormat(const Style& style)
    : Policies(PreparePolicies(style))
    , Annotations(CompileLibraryAnnotations(style))
    , IncludeGroups(style.IncludeGrouping != IncludeGrouping::Preserve
        ? std::span<const IncludeGroupRule>(style.IncludeGroups)
        : std::span<const IncludeGroupRule>{})
    , Structural{
        style.AddMissingControlBraces,
        style.EmptyControlBody == EmptyControlBody::EmptyBlock,
        style.RemoveNullStatements,
        style.RemoveNullDeclarations,
        style.IncludeOrder == IncludeOrder::Sort,
        style.LineEnding != LineEnding::Preserve,
        style.IndentPreprocessingDirectives,
        style.AlignMacroBackslashes,
        style.RulerLength,
        static_cast<std::uint8_t>(style.IndentWidth),
        style.IncludeCategoryOrder,
        style.IncludeDelimiters,
        style.IncludeMatchingHeader,
        style.IncludeGrouping,
        style.IncludeGroupOrder,
        style.IncludeDirectoryOrder,
        style.IncludeInGroupOrder,
        style.IncludePathBase,
        style.IncludeStandardHeaders,
        style.RulerPlacement,
        style.LineEnding,
    }
    , NamespaceComments(CommentOptions(style))
    , HonorClangFormatDirectives(style.HonorClangFormatDirectives)
    , WhitespaceSensitiveMacros(style.WhitespaceSensitiveMacros)
    , FunctionBodyMacroNames(style.FunctionBodyMacroNames)
    , ClassMacroNames(style.ClassMacroNames)
    , EnumMacroNames(style.EnumMacroNames)
{ }

namespace {

FormatResult FormatTree(
    const parser::SyntaxTree& original,
    const PreparedFormat& prepared,
    const FormatRequest& request,
    bool widthDiagnostics,
    parser::SyntaxTree* owned = nullptr)
{
    // Bound the initial input-local backing block. Consolidating the common
    // case avoids repeated heap growth/trim during arena teardown; larger
    // inputs still grow through the normal upstream resource.
    const auto capacity = std::clamp<std::size_t>(std::size_t(original.TokenCount()) * 512, 4096, 1048576);
    auto buffer = std::make_unique_for_overwrite<std::byte[]>(capacity);
    std::pmr::monotonic_buffer_resource file(buffer.get(), capacity);
    // Keep only this bounded initial slice across scratch resets. Overflow
    // blocks are released normally, and no backing memory survives the file.
    const auto scratchCapacity = std::clamp<std::size_t>(std::size_t(original.TokenCount()) * 128, 1024, 262144);
    auto* scratchBuffer = file.allocate(scratchCapacity);
    std::pmr::monotonic_buffer_resource scratch(scratchBuffer, scratchCapacity);
    std::pmr::vector<std::string_view> sensitive(&file);
    for (const auto& name : prepared.WhitespaceSensitiveMacros) {
        sensitive.push_back(name);
    }

    auto safety = BuildSourceAnalysis(
        original,
        {
            .HonorClangFormatDirectives = prepared.HonorClangFormatDirectives,
            .WhitespaceSensitiveMacros = sensitive,
            .EnumMacroNames = prepared.EnumMacroNames,
            .ImmutableRanges = request.ImmutableRanges,
            .EditScope = request.EditScope,
        },
        file,
        scratch);
    scratch.release();
    return detail::FormatWithAnalysis(
        original,
        prepared,
        safety,
        sensitive,
        request,
        file,
        scratch,
        widthDiagnostics,
        owned,
        owned ? &safety : nullptr);
}

} // namespace

FormatResult Format(const parser::SyntaxTree& original, const PreparedFormat& prepared, const FormatRequest& request)
{
    return FormatTree(original, prepared, request, true);
}

FormatResult detail::FormatWithAnalysis(
    const parser::SyntaxTree& original,
    const PreparedFormat& prepared,
    const SourceAnalysis& safety,
    std::span<const std::string_view> sensitive,
    const FormatRequest& request,
    std::pmr::memory_resource& file,
    std::pmr::monotonic_buffer_resource& scratch,
    bool widthDiagnostics,
    parser::SyntaxTree* owned,
    SourceAnalysis* ownedSafety)
{
    const auto structuralPlanner = owned ? detail::PlanStructuralEditsCertified : PlanStructuralEdits;
    auto plan = structuralPlanner(
        original,
        safety,
        prepared.Structural,
        file,
        scratch,
        {request.InputFilePath, &prepared.IncludeGroups});
    scratch.release();
    const auto namespacePlanner = owned ? detail::PlanNamespaceCommentsCertified : PlanNamespaceComments;
    auto namespacePlan = namespacePlanner(original, safety, prepared.NamespaceComments, file, scratch);
    scratch.release();
    FormatResult result;
    result.StructuralDiagnostics.assign(plan.Skipped.begin(), plan.Skipped.end());
    std::optional<StructuralMaterialization> normalized;
    std::pmr::vector<detail::CopiedTokenRun> copied(&file);
    bool copiedCertified = false;
    std::optional<parser::SyntaxTree> normalizedTree;
    std::optional<SourceAnalysis> normalizedSafety;
    const auto* tree = &original;
    const auto* currentSafety = &safety;
    std::pmr::vector<StructuralProvenance> unchanged(&file);
    std::span<const StructuralProvenance> provenance;
    if (!plan.Edits.empty()) {
        normalized.emplace(owned
            ? detail::MaterializeStructuralEditsCompact(original, plan, file, scratch, copied)
            : MaterializeStructuralEdits(original, plan, file, scratch));
        scratch.release();
        const auto size = normalized->Text.size();
        normalized->Text.append(util::SourcePaddingSize, '\0');
        const util::PaddedSourceView normalizedSource{normalized->Text.data(), static_cast<uint32_t>(size)};
        const bool includeOnly = std::ranges::all_of(
            plan.Operations,
            [] (const auto& operation) {
                return operation.Kind == StructuralAction::IncludeOrder ||
                    operation.Kind == StructuralAction::IncludeDelimiters;
            });

        const bool commentsAndIncludes = includeOnly ||
            std::ranges::all_of(
                plan.Operations,
                [] (const auto& operation) {
                    return operation.Kind == StructuralAction::IncludeOrder ||
                        operation.Kind == StructuralAction::IncludeDelimiters ||
                        operation.Kind == StructuralAction::RulerLength ||
                        operation.Kind == StructuralAction::RulerPlacement;
                });
        std::pmr::vector<parser::detail::NormalizedPiece> pieces(&scratch);
        std::pmr::vector<parser::TokenIndex> grammarMapping(&scratch);
        std::pmr::vector<parser::detail::NormalizedCommentInsertion> commentInsertions(&scratch);
        bool tokenView = false;
        const bool canConsumeAnalysis = owned && ownedSafety && !widthDiagnostics && !request.SourceMap &&
            !request.TokenMap && !request.Changes && !request.ChangeSummary && !request.DiagnosticLocations &&
            !request.EditScope && request.ImmutableRanges.empty();
        if (owned && commentsAndIncludes) {
            pieces.reserve(normalized->Provenance.size());

            for (const auto& piece : normalized->Provenance) {
                pieces.push_back({piece.Output, piece.Original, piece.Generated});
            }
            std::pmr::vector<Range> edits(&scratch);
            edits.reserve(plan.Edits.size());

            for (const auto& edit : plan.Edits) {
                edits.push_back(edit.Source);
            }

            if (CanReuseNormalizedSourceAnalysis(original, safety, edits, pieces, scratch)) {
                if (canConsumeAnalysis) {
                    const parser::detail::NormalizedRegionTopology topology{
                        safety.Regions().data(),
                        static_cast<uint32_t>(safety.Regions().size()),
                        [] (const void* rows, uint32_t id) {
                            return static_cast<const SourceRegion*>(rows)[id].FirstChild;
                        },
                        [] (const void* rows, uint32_t id) {
                            return static_cast<const SourceRegion*>(rows)[id].NextSibling;
                        },
                    };
                    normalizedTree = parser::detail::TryBuildNormalizedTokenView(
                        original,
                        normalizedSource,
                        pieces,
                        topology,
                        grammarMapping,
                        commentInsertions);
                    tokenView = normalizedTree.has_value();
                }

                if (!normalizedTree) {
                    normalizedTree =
                        parser::detail::TryConsumeNormalizedSyntax(*owned, normalizedSource, pieces, grammarMapping);
                }
            }
        }
        if (!normalizedTree) {
            if (includeOnly) {
                normalizedTree = parser::TryReparseIncludes(original, normalizedSource);
            } else if (commentsAndIncludes) {
                normalizedTree = parser::TryReparseCommentsAndIncludes(original, normalizedSource);
            }
        }

        if (!normalizedTree) {
            normalizedTree
                .emplace(parser::Parse(normalizedSource, [] (parser::ParseError) { }, prepared.ParsingOptions()));
        }

        tree = &*normalizedTree;
        copiedCertified = !grammarMapping.empty() && CertifyCopiedTokenRuns(*tree, original, copied);
        auto inherited = ProjectProtection(safety.Protection(), normalized->Provenance, scratch);
        std::optional<std::span<const Range>> currentScope;
        std::pmr::vector<Range> selected(&scratch);
        if (request.EditScope) {
            selected = ProjectEditScope(*request.EditScope, safety, plan, normalized->Provenance, scratch);
            currentScope = selected;
        }

        if (!grammarMapping.empty()) {
            const bool consumeIdentity = canConsumeAnalysis && tree->TokenCount() == original.TokenCount();
            auto reused = tokenView
                ? detail::ConsumeNormalizedTokenViewAnalysis(
                    original,
                    *tree,
                    std::move(*ownedSafety),
                    grammarMapping,
                    pieces,
                    commentInsertions,
                    inherited,
                    file,
                    scratch)
                : consumeIdentity
                    ? detail::ConsumeIdentityNormalizedSourceAnalysis(
                        original,
                        *tree,
                        std::move(*ownedSafety),
                        grammarMapping,
                        pieces,
                        inherited,
                        file,
                        scratch)
                    : ReuseNormalizedSourceAnalysis(
                        original,
                        *tree,
                        safety,
                        grammarMapping,
                        pieces,
                        inherited,
                        file,
                        scratch);

            if (reused) {
                normalizedSafety.emplace(std::move(*reused));
            }
        }

        if (!normalizedSafety) {
            if (tokenView) {
                // Analysis refusal may consume its arrays. The original AST is
                // still intact, so retain the established parser/fresh fallback.
                normalizedTree =
                    parser::detail::TryConsumeNormalizedSyntax(*owned, normalizedSource, pieces, grammarMapping);

                if (!normalizedTree) {
                    normalizedTree.emplace(
                        parser::Parse(normalizedSource, [] (parser::ParseError) { }, prepared.ParsingOptions()));
                }

                tree = &*normalizedTree;
                copiedCertified = !grammarMapping.empty() && CertifyCopiedTokenRuns(*tree, original, copied);
            }

            normalizedSafety.emplace(BuildSourceAnalysis(
                *tree,
                {
                    .HonorClangFormatDirectives = prepared.HonorClangFormatDirectives,
                    .WhitespaceSensitiveMacros = sensitive,
                    .EnumMacroNames = prepared.EnumMacroNames,
                    .InheritedProtection = inherited,
                    .EditScope = currentScope,
                },
                file,
                scratch));
        }

        currentSafety = &*normalizedSafety;
        provenance = normalized->Provenance;
    } else {
        unchanged.push_back({
            {0, CheckedSourceAnalysisSize(original.Source().size())},
            {0, CheckedSourceAnalysisSize(original.Source().size())},
            NoStructuralAction,
            false,
        });
        provenance = unchanged;
    }
    scratch.release();
    SyntaxLayoutOptions options;
    const bool flatSpans = owned && !request.EditScope && !widthDiagnostics && !request.SourceMap &&
        !request.TokenMap && !request.Changes && !request.ChangeSummary && !request.DiagnosticLocations;
    auto spacing = flatSpans
        ? detail::BuildFlatSyntaxSpacing(
            *tree,
            *currentSafety,
            prepared.Policies,
            prepared.Annotations,
            options.TabWidth,
            file,
            scratch)
        : BuildSyntaxSpacing(*tree, *currentSafety, prepared.Policies, file, scratch);
    scratch.release();

    if (prepared.Policies.LineEnding == LineEnding::CRLF) {
        options.LineEnding = layout::Newline::CRLF;
    } else if (prepared.Policies.LineEnding == LineEnding::Preserve) {
        const auto lf = original.Source().find('\n');

        if (lf != std::string_view::npos && lf && original.Source()[lf - 1] == '\r') {
            options.LineEnding = layout::Newline::CRLF;
        }
    }
    std::pmr::vector<GapId> reassembled(&scratch);
    auto gaps = GeneratedGaps(
        *currentSafety,
        normalized
        ? std::span<const GeneratedAdjacency>(normalized->GeneratedAdjacencies)
        : std::span<const GeneratedAdjacency>{},
        provenance,
        reassembled,
        scratch);
    options.GeneratedAdjacencies = gaps;
    options.ReassembledGaps = reassembled;
    detail::LayoutPlan layout(*tree, file, detail::FlatSpanAccess::Spans(spacing));
    const auto built = BuildSyntaxLayout(
        *tree,
        prepared.Policies,
        prepared.Annotations,
        *currentSafety,
        spacing,
        layout,
        scratch,
        options);

    if (!built) {
        throw std::runtime_error(std::format(
            "syntax lowering failed: code {} token {} core {}",
            int(built.Code),
            built.Token,
            int(built.Core.Code)));
    }

    options.GeneratedAdjacencies = {};
    options.ReassembledGaps = {};
    scratch.release();

    if (!widthDiagnostics && !request.SourceMap && !request.TokenMap && !request.Changes && !request.ChangeSummary &&
        !request.DiagnosticLocations)
    {
        PruneTextOnlyNamespaceNoOps(
            namespacePlan,
            original,
            *tree,
            normalized ? &*normalized : nullptr,
            layout,
            scratch,
            normalized && owned ? &copied : nullptr);
        scratch.release();
    }

    const bool needNamespaceAnchors = !namespacePlan.Namespaces.empty();
    const bool changeMetadata = request.Changes || request.ChangeSummary;
    const bool sourceMap = request.SourceMap || (changeMetadata && prepared.Structural.SortIncludes);
    const bool needMaps =
        sourceMap || request.TokenMap || request.Changes || request.DiagnosticLocations || needNamespaceAnchors;
    std::optional<SparseNamespaceAnchors> sparseNamespaces;
    if (!widthDiagnostics && !request.SourceMap && !request.TokenMap && !request.Changes && !request.ChangeSummary &&
        !request.DiagnosticLocations && needNamespaceAnchors)
    {
        if (normalized && owned && !copiedCertified) {
            detail::MaterializeOriginalTokenOffsets(original, *normalized, copied);
        }

        sparseNamespaces = PrepareSparseNamespaceAnchors(
            *tree,
            original,
            normalized ? &*normalized : nullptr,
            namespacePlan,
            layout,
            file,
            scratch,
            copiedCertified ? &copied : nullptr);
        scratch.release();
    }

    if (normalized && owned && needMaps && !sparseNamespaces) {
        detail::MaterializeOriginalTokenOffsets(original, *normalized, copied);
    }

    auto renderPolicy = SyntaxRenderPolicy(prepared.Policies, options);
    renderPolicy.SourceMap = sourceMap;
    auto rendered = [&] {
        std::pmr::vector<layout::Provenance> tracked(&scratch);
        renderPolicy.IndexedTokenMap = !sourceMap && needMaps && !sparseNamespaces;
        renderPolicy.TokenCount = tree->TokenCount();

        if (!sourceMap && (needNamespaceAnchors || request.Changes)) {
            tracked.reserve(currentSafety->Protection().size());

            for (const auto& range : currentSafety->Protection()) {
                tracked.push_back({range.Source.Begin, range.Source.End});
            }

            if (sparseNamespaces) {
                for (const auto& token : sparseNamespaces->Tokens) {
                    const auto range = tree->Token(token.Normalized).Text;
                    tracked.push_back({range.Begin, range.End});
                }
            }

            if (request.Changes) {
                for (const auto& piece : provenance) {
                    if (piece.Generated && !piece.Output.Empty()) {
                        tracked.push_back({piece.Output.Begin, piece.Output.End});
                    }
                }

            }

            if (request.Changes || sparseNamespaces) {
                std::sort(
                    tracked.begin(),
                    tracked.end(),
                    [] (const auto& a, const auto& b) {
                        return a.Begin < b.Begin;
                    });
                size_t count = 0;

                for (const auto range : tracked) {
                    if (count && range.Begin <= tracked[count - 1].End) {
                        tracked[count - 1].End = std::max(tracked[count - 1].End, range.End);
                    } else {
                        tracked[count++] = range;
                    }
                }

                tracked.resize(count);
            }

            renderPolicy.TrackSource = tracked;
        }

        auto output = layout.Render(renderPolicy, file);
        renderPolicy.TrackSource = {};
        return output;
    }();
    if (!rendered.State) {
        throw std::runtime_error(std::format(
            "layout rendering failed: code {} position {}",
            int(rendered.State.Code),
            rendered.State.Position));
    }

    result.Text = std::move(rendered.Text);

    if (prepared.NamespaceComments.Policy != NamespaceCommentPolicy::Preserve) {
        (void)CheckedSourceAnalysisSize(result.Text.size());
    }
    if (needMaps) {
        if (sourceMap || request.Changes) {
            result.StructuralOperations.assign(plan.Operations.begin(), plan.Operations.end());
        }

        if (sparseNamespaces) {
            ComposeSparseNamespaceAnchors(result, rendered, *currentSafety, *sparseNamespaces);
        } else {
            result.OriginalTokens.resize(original.TokenCount());
            auto identities = OriginalIdentities(*tree, original, normalized ? &*normalized : nullptr, scratch);

            if (sourceMap) {
                ComposeMaps(result, rendered, *currentSafety, identities, provenance);
            } else {
                for (parser::TokenIndex id = 0; id < rendered.Tokens.size(); ++id) {
                    const auto& token = rendered.Tokens[id];
                    const auto originalId = identities.empty() ? id : identities[id];

                    if (token.Present && originalId != layout::NoIdentity) {
                        result.OriginalTokens[originalId] = {{token.Begin, token.End}, true};
                    }
                }

                // Remaining mappings are precisely the tracked exact copies used
                // for protection validation, not a full per-token map vector.
                if (request.Changes) {
                    ComposeMaps(result, rendered, *currentSafety, identities, provenance);
                } else {
                    ComposeNamespaceAnchors(result, rendered, *currentSafety, identities);
                }
            }
        }

        scratch.release();
        std::pmr::vector<Range> outputProtection(&scratch);
        for (const auto& range : result.OutputProtection) {
            outputProtection.push_back(range.Source);
        }

        // Only original planner survivors may resolve. That planner checked
        // the original close-end insertion point (including scope End/EOF)
        // and the complete existing-comment replacement window. Validated
        // token identities transport those candidates, not map-anchor rights;
        // nonempty output protection alone cannot express an EOF point fence.
        auto comments = sparseNamespaces
            ? detail::ResolveNamespaceCommentsCertified(
                original,
                namespacePlan,
                result.Text,
                sparseNamespaces->Positions,
                outputProtection,
                file,
                scratch)
            : ResolveNamespaceComments(
                original,
                namespacePlan,
                result.Text,
                result.OriginalTokens,
                outputProtection,
                file,
                scratch);
        result.StructuralDiagnostics
            .insert(result.StructuralDiagnostics.end(), comments.Skipped.begin(), comments.Skipped.end());
        scratch.release();

        if (!comments.Edits.empty()) {
            auto text = MaterializeNamespaceComments(result.Text, comments, file);

            if (sourceMap || request.TokenMap || request.Changes || request.DiagnosticLocations ||
                !result.OutputProtection.empty())
            {
                // Validate protected copies even without output maps.
                ApplyComments(result, comments, original, namespacePlan, sourceMap || request.Changes, scratch);
                scratch.release();
            }

            result.Text.assign(text);
        }
    } else {
        result.StructuralDiagnostics
            .insert(result.StructuralDiagnostics.end(), namespacePlan.Skipped.begin(), namespacePlan.Skipped.end());
    }
    if (prepared.Policies.LineEnding != LineEnding::Preserve) {
        (void)CheckedSourceAnalysisSize(result.Text.size());
    }
    if (widthDiagnostics) {
        if (request.SourceMap || request.DiagnosticLocations) {
            WidthDiagnostics<true>(result, prepared.Policies, options.TabWidth);
        } else {
            WidthDiagnostics<false>(result, prepared.Policies, options.TabWidth);
        }
    }
    if (changeMetadata) {
        detail::BuildChangeMetadata(original, result, request.Changes, scratch);
        scratch.release();
    }

    if (request.DiagnosticLocations) {
        detail::MapWidthLocations(original, result, scratch);
        scratch.release();
    }

    if (!request.SourceMap) {
        result.Map = std::vector<FormatMapping>{};
        result.StructuralOperations = std::vector<StructuralOperation>{};

        if (!request.TokenMap) {
            result.OriginalTokens = std::vector<TokenOutputOffset>{};
        }

        result.OutputProtection = std::vector<ProtectedSourceRange>{};
    }

    return result;
}

FormatResult Format(util::PaddedSourceView source, const PreparedFormat& prepared, const FormatRequest& request)
{
    auto tree = parser::Parse(source, [] (parser::ParseError) { }, prepared.ParsingOptions());
    return Format(tree, prepared, request);
}

std::string Format(const parser::SyntaxTree& tree, const Style& style)
{
    return FormatTree(tree, PreparedFormat(style), {}, false).Text;
}

std::string Format(const parser::SyntaxTree& tree)
{
    return Format(tree, Style{});
}

std::string Format(util::PaddedSourceView source, const Style& style)
{
    const PreparedFormat prepared(style);
    auto tree = parser::Parse(source, [] (parser::ParseError) { }, prepared.ParsingOptions());
    return FormatTree(tree, prepared, {}, false, &tree).Text;
}

std::string Format(util::PaddedSourceView source)
{
    return Format(source, Style{});
}

} // namespace formatter
