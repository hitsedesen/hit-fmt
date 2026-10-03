#include "hunks.hpp"
#include "hunk_scope.hpp"
#include "format_pipeline.hpp"
#include <algorithm>
#include <memory_resource>
#include <stdexcept>
namespace formatter {

namespace {

using Range = parser::SourceRange;

using Tokens = parser::TokenRange;

using Reason = HunkCoverageReason;

bool SpillList(parser::NodeKind kind)
{
    using NK = parser::NodeKind;
    return kind == NK::ArgumentList || kind == NK::ParameterList ||
        kind == NK::InitializerList || kind == NK::LambdaCaptureList ||
        kind == NK::ForHeader;
}

Range Source(const parser::SyntaxTree& tree, Tokens tokens)
{
    return {
        tree.Token(tokens.Begin).Text.Begin,
        tree.Token(tokens.End - 1).Text.End,
    };
}
std::pmr::vector<Range> ByteScope(
    const detail::OriginalHunkLines& lines,
    std::span<const Hunk> hunks,
    std::pmr::memory_resource& memory)
{
    std::pmr::vector<Range> result(&memory);
    result.reserve(hunks.size());
    for (const auto hunk : hunks) {
        result.push_back(lines.Bytes(hunk));
    }
    return result;
}
// Validate the caller's original authority BEFORE intersecting it: an invalid
// interval outside the requested lines must not disappear into a valid result.
std::pmr::vector<Range> NarrowScope(
    std::span<const Range> scope,
    std::optional<std::span<const Range>> caller,
    size_t size,
    std::pmr::memory_resource& memory)
{
    if (!caller)
        return {scope.begin(), scope.end(), &memory};
    parser::SourceOffset previous = 0;
    for (const auto range : *caller) {
        if (range.Begin >= range.End || range.End > size ||
            range.Begin < previous)
        {
            throw std::invalid_argument("invalid original hunk edit scope");
        }
        previous = range.End;
    }
    std::pmr::vector<Range> result(&memory);
    size_t a = 0, b = 0;
    while (a < scope.size() && b < caller->size()) {
        const auto begin = std::max(scope[a].Begin, (*caller)[b].Begin);
        const auto end = std::min(scope[a].End, (*caller)[b].End);
        if (begin < end) {
            if (!result.empty() && result.back().End == begin) {
                result.back().End = end;
            } else {
                result.push_back({begin, end});
            }
        }
        if (scope[a].End < (*caller)[b].End) {
            ++a;
        } else {
            ++b;
        }
    }
    return result;
}
bool SemanticProtection(const SourceAnalysis& safety, Range source)
{
    const auto ranges = safety.Protection();
    auto it = std::lower_bound(
        ranges.begin(),
        ranges.end(),
        source.Begin,
        [&] (const auto& range, auto begin) {
            return source.Empty()
                ? range.Source.End < begin
                : range.Source.End <= begin;
        });
    constexpr auto outside = uint16_t(ProtectionReason::OutsideEditScope);
    for (;
        it != ranges.end() &&
        (it->Source.Begin < source.End ||
                (source.Empty() && it->Source.Begin <= source.Begin));
        ++it)
    {
        if (uint16_t(it->Reasons) & ~outside) {
            return true;
        }
    }
    return false;
}

struct NamespaceFootprint
{
    parser::TokenIndex Begin;
    Range Close;
};
// Merge unit/gap coverage first, THEN intersect requested authority. Nested
// units therefore cannot cause a per-unit scan of every requested hunk.
std::vector<HunkCoverage> FinishCoverage(
    std::pmr::vector<HunkCoverage>& raw,
    std::span<const Range> selected)
{
    std::sort(
        raw.begin(),
        raw.end(),
        [] (const auto& a, const auto& b) {
            if (a.Reason != b.Reason) {
                return a.Reason < b.Reason;
            }
            return a.Original.Begin != b.Original.Begin
                ? a.Original.Begin < b.Original.Begin
                : a.Original.End < b.Original.End;
        });
    size_t count = 0;
    for (const auto record : raw) {
        if (count && raw[count - 1].Reason == record.Reason &&
            record.Original.Begin <= raw[count - 1].Original.End)
        {
            raw[count - 1].Original.End =
                std::max(raw[count - 1].Original.End, record.Original.End);
        } else {
            raw[count++] = record;
        }
    }
    raw.resize(count);
    std::vector<HunkCoverage> result;
    size_t next = 0;
    std::optional<Reason> reason;
    for (const auto record : raw) {
        if (reason != record.Reason) {
            next = 0;
            reason = record.Reason;
        }
        while (next < selected.size() &&
            selected[next].End <= record.Original.Begin)
        {
            ++next;
        }
        if (record.Original.Empty()) {
            if (detail::HunkScopeContains(selected, record.Original)) {
                result.push_back(record);
            }
            continue;
        }
        for (auto i = next;
            i < selected.size() && selected[i].Begin < record.Original.End;
            ++i)
        {
            result
                .push_back(
                    {
                        {
                            std::max(record.Original.Begin, selected[i].Begin),
                            std::min(record.Original.End, selected[i].End),
                        },
                        record.Reason,
                    });
        }
    }
    std::sort(
        result.begin(),
        result.end(),
        [] (const auto& a, const auto& b) {
            return a.Original.Begin != b.Original.Begin
                ? a.Original.Begin < b.Original.Begin
                : a.Reason < b.Reason;
        });
    return result;
}

} // namespace
ChangedFormatResult FormatChanged(
    const parser::SyntaxTree& tree,
    const PreparedFormat& prepared,
    std::span<const Hunk> hunks,
    bool spillover,
    const FormatRequest& request)
{
    std::pmr::monotonic_buffer_resource file;
    std::pmr::monotonic_buffer_resource scratch;
    const detail::OriginalHunkLines lines(tree.Source(), file);
    const auto selected = lines.Coalesce(hunks, file);
    const auto selectedBytes = ByteScope(lines, selected, file);
    std::pmr::vector<std::string_view> sensitive(&file);
    for (const auto& name : prepared.WhitespaceSensitiveMacros) {
        sensitive.push_back(name);
    }
    auto safety =
        BuildSourceAnalysis(
            tree,
            {
                .HonorClangFormatDirectives = prepared
                    .HonorClangFormatDirectives,
                .WhitespaceSensitiveMacros = sensitive,
                .ImmutableRanges = request.ImmutableRanges,
            },
            file,
            scratch);
    scratch.release();
    std::pmr::vector<Hunk> windows(selected.begin(), selected.end(), &file);
    std::pmr::vector<HunkCoverage> coverage(&file);
    std::pmr::vector<NamespaceFootprint> namespaces(&file);
    for (const auto& region : safety.Regions()) {
        if (region.Tokens.Empty()) {
            continue;
        }
        if (prepared.NamespaceComments.Policy !=
                NamespaceCommentPolicy::Preserve &&
            region.Kind == parser::NodeKind::NamespaceDeclaration)
        {
            // Matching delimiter metadata, not header/comment text guesses.
            for (auto i = region.Tokens.Begin; i < region.Tokens.End; ++i) {
                if (tree.Kind(i) == tokenizer::TokenKind::Operator_OpenBrace) {
                    const auto close = tree.MatchingDelimiter(i);
                    if (close < region.Tokens.End) {
                        namespaces
                            .push_back(
                                {
                                    region.Tokens.Begin,
                                    {
                                        tree.Token(close).Text.Begin,
                                        tree.Token(close).Text.End,
                                    },
                                });
                    }
                    break;
                }
            }
        }
        if (!SpillList(region.Kind) ||
            !detail::HunkScopeIntersects(selectedBytes, region.Source))
        {
            continue;
        }
        const auto unit = lines.Cover(region.Source);
        const bool safe =
            region.Status == RegionStatus::Complete &&
                !safety.IntersectsImmutable(region.Source);
        if (spillover && safe && unit.End - unit.Begin < 200) {
            windows.push_back(unit);
        } else if (!safe ||
            !detail::HunkScopeContains(selectedBytes, region.Source))
        {
            coverage.push_back({region.Source, Reason::LimitedLayout});
        }
    }
    // Discovery uses the original unrestricted semantic analysis, and the
    // eventual scoped planner independently checks atomic authority.
    if (!selected.empty()) {
        const auto groups = DiscoverIncludeGroups(tree, safety, file, scratch);
        for (const auto& group : groups) {
            if (!detail::HunkScopeIntersects(selectedBytes, group.Source)) {
                continue;
            }
            const auto unit = lines.Cover(group.Source);
            if (spillover && unit.End - unit.Begin < 200) {
                windows.push_back(unit);
            } else if (prepared.Structural.SortIncludes &&
                !detail::HunkScopeContains(selectedBytes, group.Source))
            {
                coverage
                    .push_back({group.Source, Reason::StructuralEligibility});
            }
        }
    }
    scratch.release();
    const auto permitted = lines.Coalesce(windows, file);
    const auto permittedBytes = ByteScope(lines, permitted, file);
    const auto scope =
        NarrowScope(
            permittedBytes,
            request.EditScope,
            tree.Source().size(),
            file);
    for (const auto& protection : safety.Protection()) {
        if (detail::HunkScopeIntersects(selectedBytes, protection.Source)) {
            coverage.push_back({protection.Source, Reason::SemanticProtection});
        }
    }
    safety.RestrictEditScope(scope);
    for (const auto& gap : safety.Gaps())
        if (gap.Preserve &&
            detail::HunkScopeIntersects(selectedBytes, gap.Source))
            coverage.push_back({gap.Source, Reason::LimitedLayout});
    // EOF is never an owned insertion point, even with the last line selected.
    // That limits layout-check coverage; it is not evidence of a missing LF.
    const auto size = CheckedSourceAnalysisSize(tree.Source().size());
    if (size && detail::HunkScopeContains(selectedBytes, {size - 1, size}))
        coverage.push_back({{size - 1, size}, Reason::LimitedLayout});
    ChangedFormatResult result;
    result.SelectedLines.assign(selected.begin(), selected.end());
    result.PermittedLines.assign(permitted.begin(), permitted.end());
    result.EditScope.assign(scope.begin(), scope.end());
    FormatRequest scoped = request;
    scoped.EditScope = scope;
    result.Result =
        detail::FormatWithAnalysis(
            tree,
            prepared,
            safety,
            sensitive,
            scoped,
            file,
            scratch);
    for (const auto& diagnostic : result.Result.StructuralDiagnostics) {
        if (diagnostic.Tokens.Empty())
            continue;
        // Proven policy exemptions are neither violations nor missing checks.
        if (diagnostic.Reason == StructuralSkip::ShortNamespace ||
            (diagnostic.Reason == StructuralSkip::RequiredBody &&
                    (diagnostic.Action ==
                                StructuralAction::RemoveNullStatements ||
                            diagnostic.Action ==
                                StructuralAction::RemoveNullDeclarations)))
        {
            continue;
        }
        auto footprint = Source(tree, diagnostic.Tokens);
        if (diagnostic.Action == StructuralAction::NamespaceComments) {
            const auto ns = std::lower_bound(
                namespaces.begin(),
                namespaces.end(),
                diagnostic.Tokens.Begin,
                [] (const auto& record, auto begin) {
                    return record.Begin < begin;
                });
            if (ns == namespaces.end() || ns->Begin != diagnostic.Tokens.Begin)
                continue; // No reliable requested comment footprint.
            footprint = ns->Close;
        }
        if (!detail::HunkScopeIntersects(selectedBytes, footprint)) {
            continue;
        }
        const bool requiredCandidate =
            diagnostic.Action != StructuralAction::IncludeOrder &&
                diagnostic.Action != StructuralAction::NamespaceComments &&
                diagnostic.Reason == StructuralSkip::Protected;
        const auto beginGap = diagnostic.Tokens.Begin;
        const auto endGap = diagnostic.Tokens.End;
        // These planner skips occur only AFTER action-specific requiredness,
        // completeness, attachment and semantic eligibility checks. Only a pure
        // selection fence can be promoted to a definite required-action skip.
        if (requiredCandidate && !SemanticProtection(safety, footprint) &&
            !SemanticProtection(safety, {footprint.End, footprint.End}) &&
            !safety.Gaps()[beginGap].IntrinsicPreserve &&
            !safety.Gaps()[endGap].IntrinsicPreserve &&
            !(uint16_t(safety.Gaps()[beginGap].Reasons) &
                    ~uint16_t(ProtectionReason::OutsideEditScope)) &&
            !(uint16_t(safety.Gaps()[endGap].Reasons) &
                    ~uint16_t(ProtectionReason::OutsideEditScope)))
        {
            result.RequiredSkips.push_back(diagnostic);
        } else {
            coverage.push_back({footprint, Reason::StructuralEligibility});
        }
    }
    result.Incomplete = FinishCoverage(coverage, selectedBytes);
    return result;
}
std::string FormatChanged(
    const parser::SyntaxTree& tree,
    const Style& style,
    std::span<const Hunk> hunks,
    bool spillover)
{
    return FormatChanged(tree, PreparedFormat(style), hunks, spillover).Result
        .Text;
}

} // namespace formatter
