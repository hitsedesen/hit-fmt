#include "formatter.hpp"

#include "diagnostic_locations.hpp"
#include "format_changes_internal.hpp"
#include "format_pipeline.hpp"
#include "syntax_layout.hpp"
#include "text_policies.hpp"

#include <unicode/utf8.hpp>

#include <algorithm>
#include <format>
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

struct CurrentIdentity
{
    Range Source;
    document::Identity Original = document::NoIdentity;
};

std::pmr::vector<document::Identity> OriginalIdentities(
    const parser::SyntaxTree& tree,
    const StructuralMaterialization* normalized,
    std::pmr::memory_resource& scratch)
{
    std::pmr::vector<document::Identity> identities(
        tree.TokenCount(), document::NoIdentity, &scratch);
    if (!normalized) {
        for (parser::TokenIndex i = 0; i < tree.TokenCount(); ++i) {
            identities[i] = i;
        }

        return identities;
    }

    std::pmr::vector<CurrentIdentity> surviving(&scratch);
    surviving.reserve(normalized->OriginalTokens.size());

    for (parser::TokenIndex i = 0; i < normalized->OriginalTokens.size(); ++i) {
        const auto& token = normalized->OriginalTokens[i];

        if (token.Present) {
            surviving.push_back({token.Output, i});
        }
    }

    std::sort(
        surviving.begin(),
        surviving.end(),
        [] (const auto& a, const auto& b) {
            return a.Source.Begin < b.Source.Begin;
        });
    size_t next = 0;

    for (parser::TokenIndex i = 0; i < tree.TokenCount() && next < surviving.size(); ++i) {
        const auto range = tree.Token(i).Text;

        if (range.Begin == surviving[next].Source.Begin && range.End == surviving[next].Source.End) {
            identities[i] = surviving[next++].Original;
        } else if (range.End > surviving[next].Source.Begin) {
            throw std::runtime_error("normalization changed an original token identity");
        }
    }

    if (next != surviving.size()) {
        throw std::runtime_error("normalization lost an original token identity");
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

document::Identity RecordRenderAnchors(
    FormatResult& result,
    const document::Mapping& map,
    Range output,
    const SourceAnalysis& safety,
    std::span<const document::Identity> identities)
{
    document::Identity original = document::NoIdentity;

    if (map.Source.Identity != document::NoIdentity) {
        if (map.Source.Identity >= identities.size()) {
            throw std::runtime_error("invalid rendered token identity");
        }

        original = identities[map.Source.Identity];

        if (original != document::NoIdentity) {
            result.OriginalTokens[original] = {output, true};
        }
    }

    if (map.Exact && map.Source.Begin != document::InvalidId) {
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

void ComposeNamespaceAnchors(
    FormatResult& result,
    const document::Result& rendered,
    const SourceAnalysis& safety,
    std::span<const document::Identity> identities)
{
    for (const auto& map : rendered.Map) {
        const auto output = Range{CheckedSourceAnalysisSize(map.OutputBegin), CheckedSourceAnalysisSize(map.OutputEnd)};
        (void)RecordRenderAnchors(result, map, output, safety, identities);
    }

    MergeProtection(result.OutputProtection);
}

void ComposeMaps(
    FormatResult& result,
    const document::Result& rendered,
    const SourceAnalysis& safety,
    std::span<const document::Identity> identities,
    std::span<const StructuralProvenance> provenance)
{
    result.Map.reserve(rendered.Map.size());

    for (const auto& map : rendered.Map) {
        const auto output = Range{CheckedSourceAnalysisSize(map.OutputBegin), CheckedSourceAnalysisSize(map.OutputEnd)};
        const auto original = RecordRenderAnchors(result, map, output, safety, identities);

        if (map.Source.Begin == document::InvalidId) {
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
                original == document::NoIdentity && (!map.Exact || it->Generated),
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
                    document::NoIdentity,
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
            if (next.OriginalToken != document::NoIdentity && !result.OriginalTokens[next.OriginalToken].Present) {
                next.OriginalToken = document::NoIdentity;
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
    }
    , NamespaceComments(CommentOptions(style))
    , HonorClangFormatDirectives(style.HonorClangFormatDirectives)
    , WhitespaceSensitiveMacros(style.WhitespaceSensitiveMacros)
    , FunctionBodyMacroNames(style.FunctionBodyMacroNames)
    , ClassMacroNames(style.ClassMacroNames)
    , EnumMacroNames(style.EnumMacroNames)
{ }

FormatResult Format(const parser::SyntaxTree& original, const PreparedFormat& prepared, const FormatRequest& request)
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
    return detail::FormatWithAnalysis(original, prepared, safety, sensitive, request, file, scratch);
}

FormatResult detail::FormatWithAnalysis(
    const parser::SyntaxTree& original,
    const PreparedFormat& prepared,
    const SourceAnalysis& safety,
    std::span<const std::string_view> sensitive,
    const FormatRequest& request,
    std::pmr::memory_resource& file,
    std::pmr::monotonic_buffer_resource& scratch)
{
    auto plan = PlanStructuralEdits(
        original,
        safety,
        prepared.Structural,
        file,
        scratch,
        {request.InputFilePath, &prepared.IncludeGroups});
    scratch.release();
    auto namespacePlan = PlanNamespaceComments(original, safety, prepared.NamespaceComments, file, scratch);
    scratch.release();
    FormatResult result;
    result.StructuralDiagnostics.assign(plan.Skipped.begin(), plan.Skipped.end());
    std::optional<StructuralMaterialization> normalized;
    std::optional<parser::SyntaxTree> normalizedTree;
    std::optional<SourceAnalysis> normalizedSafety;
    const auto* tree = &original;
    const auto* currentSafety = &safety;
    std::pmr::vector<StructuralProvenance> unchanged(&file);
    std::span<const StructuralProvenance> provenance;
    if (!plan.Edits.empty()) {
        normalized.emplace(MaterializeStructuralEdits(original, plan, file, scratch));
        scratch.release();
        const auto size = normalized->Text.size();
        normalized->Text.append(util::SourcePaddingSize, '\0');
        normalizedTree.emplace(parser::Parse(
            {normalized->Text.data(), static_cast<uint32_t>(size)},
            [] (parser::ParseError) { },
            prepared.ParsingOptions()));
        tree = &*normalizedTree;
        auto inherited = ProjectProtection(safety.Protection(), normalized->Provenance, scratch);
        std::optional<std::span<const Range>> currentScope;
        std::pmr::vector<Range> selected(&scratch);
        if (request.EditScope) {
            selected = ProjectEditScope(*request.EditScope, safety, plan, normalized->Provenance, scratch);
            currentScope = selected;
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
    auto spacing = BuildSyntaxSpacing(*tree, *currentSafety, prepared.Policies, file, scratch);
    scratch.release();
    SyntaxLayoutOptions options;

    if (prepared.Policies.LineEnding == LineEnding::CRLF) {
        options.LineEnding = document::Newline::CRLF;
    } else if (prepared.Policies.LineEnding == LineEnding::Preserve) {
        const auto lf = original.Source().find('\n');

        if (lf != std::string_view::npos && lf && original.Source()[lf - 1] == '\r') {
            options.LineEnding = document::Newline::CRLF;
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
    document::Document document(tree->Source(), &file);
    const auto built = BuildSyntaxDocument(
        *tree,
        prepared.Policies,
        prepared.Annotations,
        *currentSafety,
        spacing,
        document,
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
    const bool needNamespaceAnchors = !namespacePlan.Namespaces.empty();
    const bool changeMetadata = request.Changes || request.ChangeSummary;
    const bool sourceMap = request.SourceMap || (changeMetadata && prepared.Structural.SortIncludes);
    const bool needMaps =
        sourceMap || request.TokenMap || request.Changes || request.DiagnosticLocations || needNamespaceAnchors;
    auto renderPolicy = SyntaxRenderPolicy(prepared.Policies, options);
    renderPolicy.SourceMap = sourceMap;
    auto rendered = [&] {
        std::pmr::vector<document::Provenance> tracked(&scratch);
        renderPolicy.IndexedTokenMap = !sourceMap && needMaps;
        renderPolicy.TokenCount = tree->TokenCount();

        if (!sourceMap && (needNamespaceAnchors || request.Changes)) {
            tracked.reserve(currentSafety->Protection().size());

            for (const auto& range : currentSafety->Protection()) {
                tracked.push_back({range.Source.Begin, range.Source.End});
            }

            if (request.Changes) {
                for (const auto& piece : provenance) {
                    if (piece.Generated && !piece.Output.Empty()) {
                        tracked.push_back({piece.Output.Begin, piece.Output.End});
                    }
                }

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

        auto output = document.Render(renderPolicy, &file);
        renderPolicy.TrackSource = {};
        return output;
    }();
    if (!rendered.State) {
        throw std::runtime_error(
            std::format("layout rendering failed: code {} node {}", int(rendered.State.Code), rendered.State.Node));
    }

    result.Text.assign(rendered.Text);

    if (prepared.NamespaceComments.Policy != NamespaceCommentPolicy::Preserve) {
        (void)CheckedSourceAnalysisSize(result.Text.size());
    }
    if (needMaps) {
        if (sourceMap || request.Changes) {
            result.StructuralOperations.assign(plan.Operations.begin(), plan.Operations.end());
        }

        result.OriginalTokens.resize(original.TokenCount());
        auto identities = OriginalIdentities(*tree, normalized ? &*normalized : nullptr, scratch);

        if (sourceMap) {
            ComposeMaps(result, rendered, *currentSafety, identities, provenance);
        } else {
            for (parser::TokenIndex id = 0; id < rendered.Tokens.size(); ++id) {
                const auto& token = rendered.Tokens[id];
                const auto originalId = identities[id];

                if (token.Present && originalId != document::NoIdentity) {
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
        auto comments = ResolveNamespaceComments(
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
    if (request.SourceMap || request.DiagnosticLocations) {
        WidthDiagnostics<true>(result, prepared.Policies, options.TabWidth);
    } else {
        WidthDiagnostics<false>(result, prepared.Policies, options.TabWidth);
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
    return Format(tree, PreparedFormat(style)).Text;
}

std::string Format(const parser::SyntaxTree& tree)
{
    return Format(tree, Style{});
}

std::string Format(util::PaddedSourceView source, const Style& style)
{
    return Format(source, PreparedFormat(style)).Text;
}

std::string Format(util::PaddedSourceView source)
{
    return Format(source, Style{});
}

} // namespace formatter
