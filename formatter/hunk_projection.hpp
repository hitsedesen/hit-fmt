#pragma once

#include "diagnostic_locations.hpp"
#include "format_changes_internal.hpp"
#include "hunk_scope.hpp"

#include <unicode/utf8.hpp>

namespace formatter::detail {

// Projection consumes actual byte differences, never the renderer's (possibly
// overlapping or zero-width) source associations as independent edit rights.
struct HunkPatch
{
    parser::SourceRange Original;
    parser::SourceRange Canonical;
};

inline FormatResult ProjectHunkFormat(
    const parser::SyntaxTree& tree,
    const SourceAnalysis& safety,
    const FormatResult& canonical,
    const StructuralPlan& eligible,
    const PreparedFormat& prepared,
    std::span<const parser::SourceRange> scope,
    const FormatRequest& request,
    std::pmr::memory_resource& memory,
    std::span<const parser::SourceOffset> originalLines,
    bool originalBareCR)
{
    using Range = parser::SourceRange;
    using Offset = parser::SourceOffset;
    const auto source = tree.Source();
    const auto output = std::string_view(canonical.Text);
    const auto view = [] (Range range, std::string_view text) {
        return text.substr(range.Begin, range.End - range.Begin);
    };
    const auto originalSize = CheckedSourceAnalysisSize(source.size());
    const auto canonicalSize = CheckedSourceAnalysisSize(output.size());
    struct Dependency
    {
        Range Source;
        bool Allowed;
    };

    std::pmr::vector<Dependency> dependencies(&memory);
    struct OperationKey
    {
        StructuralAction Kind;
        parser::TokenRange Owner;
    };

    const auto operationLess = [] (const auto& a, const auto& b) {
        if (a.Kind != b.Kind) {
            return a.Kind < b.Kind;
        }

        return a.Owner.Begin != b.Owner.Begin ? a.Owner.Begin < b.Owner.Begin : a.Owner.End < b.Owner.End;
    };
    std::pmr::vector<OperationKey> eligibleOperations(&memory);

    for (const auto& operation : eligible.Operations) {
        eligibleOperations.push_back({operation.Kind, operation.Owner});
    }

    std::sort(eligibleOperations.begin(), eligibleOperations.end(), operationLess);
    struct CommaKey
    {
        bool HasOriginal;
        Range Original;
    };

    const auto commaLess = [] (const auto& a, const auto& b) {
        if (a.HasOriginal != b.HasOriginal) {
            return a.HasOriginal < b.HasOriginal;
        }

        return a.Original.Begin != b.Original.Begin
            ? a.Original.Begin < b.Original.Begin
            : a.Original.End < b.Original.End;
    };
    // Canonical output proves punctuation is required. Original parsed list
    // identities and scoped safety alone decide whether we may apply it.
    std::pmr::vector<CommaKey> eligibleCommas(&memory);
    std::pmr::vector<bool> removableCommas(tree.TokenCount(), false, &memory);
    const auto gapMutable = [&] (parser::TokenIndex index) {
        const auto& gap = safety.Gaps()[index];
        return !gap.Preserve && !gap.ContextSensitive &&
            !(prepared.Policies.LineEnding != LineEnding::Preserve && gap.HasBareCR);
    };

    for (const auto& region : safety.Regions()) {
        if (region.Kind != parser::NodeKind::InitializerList || region.Tokens.Empty()) {
            continue;
        }

        const auto open = region.Tokens.Begin;
        const auto close = tree.MatchingDelimiter(open);

        if (tree.Kind(open) != tokenizer::TokenKind::Operator_OpenBrace || close >= region.Tokens.End ||
            !gapMutable(close))
        {
            continue;
        }

        RegionId last = NoRegion;

        for (auto child = region.FirstChild; child != NoRegion; child = safety.Regions()[child].NextSibling) {
            if (safety.Regions()[child].Kind == parser::NodeKind::InitializerElement) {
                last = child;
            }
        }

        if (last == NoRegion || safety.Regions()[last].Tokens.Empty()) {
            continue;
        }

        const auto after = safety.Regions()[last].Tokens.End;
        auto comma = tree.TokenCount();
        bool valid = true;

        for (auto token = after; token < close; ++token) {
            if (tree.Kind(token) == tokenizer::TokenKind::Comment) {
                continue;
            }

            if (tree.Kind(token) == tokenizer::TokenKind::Operator_Comma && comma == tree.TokenCount()) {
                comma = token;
            } else {
                valid = false;
                break;
            }
        }

        const auto insertion = tree.Token(after - 1).Text.End;

        if (!valid ||
            (prepared.Policies.InitializersTrailingComma == TrailingCommaPolicy::WhenMultiline &&
                safety.IntersectsImmutable({insertion, insertion})))
        {
            continue;
        }

        if (comma != tree.TokenCount()) {
            const auto text = tree.Token(comma).Text;

            if (safety.IntersectsImmutable({text.Begin, text.End}) || !gapMutable(comma) ||
                !safety.CanReplaceGap(comma, "") ||
                (prepared.Policies.Initializers.Layout != LayoutMode::Canonical && safety.Gaps()[comma].HasLineFeed))
            {
                continue;
            }

            removableCommas[comma] = true;
        }

        eligibleCommas.push_back({true, {insertion, insertion}});
    }

    std::sort(eligibleCommas.begin(), eligibleCommas.end(), commaLess);
    std::pmr::vector<bool> allowedActions(&memory);
    allowedActions.reserve(canonical.StructuralOperations.size());

    for (const auto& operation : canonical.StructuralOperations) {
        const auto owner = operation.Owner;
        const bool allowed = std::binary_search(
            eligibleOperations.begin(),
            eligibleOperations.end(),
            OperationKey{operation.Kind, owner},
            operationLess);
        allowedActions.push_back(allowed);

        if (owner.Empty()) {
            continue;
        }

        Range envelope{tree.Token(owner.Begin).Text.Begin, tree.Token(owner.End - 1).Text.End};

        if (operation.Kind == StructuralAction::NamespaceComments) {
            // Header/interior eligibility comes from the scoped planner, but
            // only the closing comment is a byte replacement dependency.
            for (auto i = owner.Begin; i < owner.End; ++i) {
                if (tree.Kind(i) == tokenizer::TokenKind::Operator_OpenBrace) {
                    const auto close = tree.MatchingDelimiter(i);

                    if (close < owner.End) {
                        envelope.Begin = tree.Token(close).Text.End;
                    }

                    break;
                }
            }
        }

        dependencies.push_back({envelope, allowed});
    }

    // Namespace comments, moved includes and directive rewrites may own line
    // bytes beyond their syntax owner's token envelope.
    for (const auto& map : canonical.Map) {
        if (!map.HasOriginal || map.Action == NoStructuralAction || map.Action >= allowedActions.size()) {
            continue;
        }

        dependencies.push_back({map.Original, allowedActions[map.Action]});
    }

    // Optional trailing commas are layout atoms, not StructuralOperations.
    // Scoped original safety checks the closer gap and insertion/removal
    // points; no scoped layout or generated-text oracle is needed.
    for (const auto& map : canonical.Map) {
        if (map.Generated && map.Action == NoStructuralAction && map.Output.End - map.Output.Begin == 1 &&
            output[map.Output.Begin] == ',')
        {
            const bool allowed = std::binary_search(
                eligibleCommas.begin(),
                eligibleCommas.end(),
                CommaKey{map.HasOriginal, map.Original},
                commaLess);
            dependencies.push_back({map.Original, allowed});
        }
    }

    for (parser::TokenIndex i = 0; i < tree.TokenCount(); ++i) {
        if (tree.Kind(i) == tokenizer::TokenKind::Operator_Comma && !canonical.OriginalTokens[i].Present) {
            dependencies.push_back({{tree.Token(i).Text.Begin, tree.Token(i).Text.End}, removableCommas[i]});
        }

        // Reindented multiline comments remain indivisible original atoms.
        const auto& token = canonical.OriginalTokens[i];

        if (token.Present && tree.TokenText(i) != view(token.Output, output)) {
            dependencies.push_back({{tree.Token(i).Text.Begin, tree.Token(i).Text.End}, true});
        }
    }

    std::sort(
        dependencies.begin(),
        dependencies.end(),
        [] (const auto& a, const auto& b) {
            return a.Source.Begin != b.Source.Begin ? a.Source.Begin < b.Source.Begin : a.Source.End < b.Source.End;
        });
    size_t count = 0;

    for (const auto dependency : dependencies) {
        if (count && dependency.Source.Begin <= dependencies[count - 1].Source.End) {
            auto& previous = dependencies[count - 1];
            previous.Source.End = std::max(previous.Source.End, dependency.Source.End);
            previous.Allowed = previous.Allowed && dependency.Allowed;
        } else {
            dependencies[count++] = dependency;
        }
    }

    dependencies.resize(count);
    const auto overlaps = [] (Range a, Range b) {
        return a.Empty()
            ? (b.Empty() ? a.Begin == b.Begin : b.Begin <= a.Begin && a.Begin < b.End)
            : b.Empty() ? a.Begin <= b.Begin && b.Begin < a.End : a.Begin < b.End && b.Begin < a.End;
    };
    // Copied, monotonically ordered bytes are stable diff anchors. No anchor
    // may split a structural owner or a rewritten comment atom.
    std::pmr::vector<HunkPatch> anchors(&memory);

    for (const auto& map : canonical.Map) {
        if (!map.Exact || !map.HasOriginal || map.Output.Empty() ||
            map.Output.End - map.Output.Begin != map.Original.End - map.Original.Begin ||
            map.Action != NoStructuralAction || view(map.Output, output) != view(map.Original, source))
        {
            continue;
        }

        auto it = std::lower_bound(
            dependencies.begin(),
            dependencies.end(),
            map.Original.Begin,
            [] (const auto& d, auto begin) {
                return d.Source.End < begin;
            });
        bool blocked = false;

        for (; it != dependencies.end() && it->Source.Begin <= map.Original.End; ++it) {
            if (overlaps(map.Original, it->Source)) {
                blocked = true;
                break;
            }
        }

        if (!blocked) {
            anchors.push_back({map.Original, map.Output});
        }
    }

    std::sort(
        anchors.begin(),
        anchors.end(),
        [] (const auto& a, const auto& b) {
            return a.Canonical.Begin != b.Canonical.Begin
                ? a.Canonical.Begin < b.Canonical.Begin
                : a.Original.Begin < b.Original.Begin;
        });
    std::pmr::vector<HunkPatch> patches(&memory);
    const auto consider = [&] (Range before, Range after) {
        bool atomic = false;
        bool allowed = true;
        Offset atomicBegin = before.End;
        Offset atomicEnd = before.Begin;
        auto it = std::lower_bound(
            dependencies.begin(),
            dependencies.end(),
            before.Begin,
            [] (const auto& d, auto begin) {
                return d.Source.End < begin;
            });

        for (; it != dependencies.end() && it->Source.Begin <= before.End; ++it) {
            if (overlaps(before, it->Source)) {
                atomic = true;
                atomicBegin = std::min(atomicBegin, it->Source.Begin);
                atomicEnd = std::max(atomicEnd, it->Source.End);
                allowed = allowed && it->Allowed && HunkScopeContains(scope, it->Source);
            }
        }

        // Diff trimming must not transfer ownership to a different insertion
        // point. In particular a gap's following indentation belongs to the
        // following physical line, even if a common newline can be trimmed.
        allowed = allowed && HunkScopeContains(scope, before);
        {
            while (!before.Empty() && !after.Empty() && (!atomic || before.Begin < atomicBegin) &&
                source[before.Begin] == output[after.Begin])
            {
                ++before.Begin;
                ++after.Begin;
            }

            while (!before.Empty() && !after.Empty() && (!atomic || atomicEnd < before.End) &&
                source[before.End - 1] == output[after.End - 1])
            {
                --before.End;
                --after.End;
            }
        }

        if (view(before, source) != view(after, output) && allowed && HunkScopeContains(scope, before)) {
            patches.push_back({before, after});
        }
    };
    const auto considerGap = [&] (Range before, Range after) {
        const auto whitespace = [] (std::string_view text) {
            return text.find_first_not_of(" \t\r\n") == std::string_view::npos;
        };
        const auto original = view(before, source);
        const auto formatted = view(after, output);
        auto dependency = std::lower_bound(
            dependencies.begin(),
            dependencies.end(),
            before.Begin,
            [] (const auto& d, auto begin) {
                return d.Source.End < begin;
            });
        bool atomic = false;

        for (; dependency != dependencies.end() && dependency->Source.Begin <= before.End; ++dependency) {
            if (overlaps(before, dependency->Source)) {
                atomic = true;
                break;
            }
        }

        // Only corresponding physical newlines can divide gap authority.
        // Flattening, inserted breaks and structural atoms stay indivisible;
        // clipping those to a selected line would invent a different layout.
        if (atomic || !whitespace(original) || !whitespace(formatted) ||
            std::count(original.begin(), original.end(), '\n') != std::count(formatted.begin(), formatted.end(), '\n'))
        {
            consider(before, after);
            return;
        }

        auto originalBegin = before.Begin;
        auto canonicalBegin = after.Begin;

        for (auto i = before.Begin; i < before.End; ++i) {
            if (source[i] != '\n') {
                continue;
            }

            auto j = canonicalBegin;

            while (j < after.End && output[j] != '\n') {
                ++j;
            }

            consider({originalBegin, i + 1}, {canonicalBegin, j + 1});
            originalBegin = i + 1;
            canonicalBegin = j + 1;
        }

        consider({originalBegin, before.End}, {canonicalBegin, after.End});
    };
    Offset originalCursor = 0;
    Offset canonicalCursor = 0;

    for (const auto& anchor : anchors) {
        if (anchor.Original.Begin < originalCursor || anchor.Canonical.Begin < canonicalCursor) {
            continue;
        }

        considerGap({originalCursor, anchor.Original.Begin}, {canonicalCursor, anchor.Canonical.Begin});
        originalCursor = anchor.Original.End;
        canonicalCursor = anchor.Canonical.End;
    }

    considerGap({originalCursor, originalSize}, {canonicalCursor, canonicalSize});

    struct Piece
    {
        Range Input;
        Range Output;
        bool Canonical;
    };

    std::pmr::vector<Piece> pieces(&memory);
    FormatResult result;
    result.StructuralDiagnostics.assign(eligible.Skipped.begin(), eligible.Skipped.end());
    const auto append = [&] (Range input, bool formatted) {
        if (input.Empty()) {
            return;
        }

        const auto begin = CheckedSourceAnalysisSize(result.Text.size());
        result.Text.append(view(input, formatted ? output : source));
        pieces.push_back({input, {begin, CheckedSourceAnalysisSize(result.Text.size())}, formatted});
    };
    originalCursor = 0;

    for (const auto& patch : patches) {
        append({originalCursor, patch.Original.Begin}, false);
        append(patch.Canonical, true);
        originalCursor = patch.Original.End;
    }

    append({originalCursor, originalSize}, false);
    std::pmr::vector<StructuralActionId> actions(
        canonical.StructuralOperations.size(),
        NoStructuralAction,
        &memory);
    const auto action = [&] (StructuralActionId id) {
        if (id == NoStructuralAction) {
            return id;
        }

        if (actions[id] == NoStructuralAction) {
            actions[id] = StructuralActionId(result.StructuralOperations.size());
            result.StructuralOperations.push_back(canonical.StructuralOperations[id]);
        }

        return actions[id];
    };
    result.OriginalTokens.resize(tree.TokenCount());
    std::pmr::vector<Offset> mapEnds(&memory);
    Offset maximumEnd = 0;

    for (const auto& map : canonical.Map) {
        maximumEnd = std::max(maximumEnd, map.Output.End);
        mapEnds.push_back(maximumEnd);
    }

    // Each input stream is monotonic across pieces; binary search starts each
    // map/protection scan instead of rescanning the full file per patch.
    for (const auto& piece : pieces) {
        const auto shift = [&] (Range range) -> Range {
            return {
                piece.Output.Begin + range.Begin - piece.Input.Begin,
                piece.Output.Begin + range.End - piece.Input.Begin,
            };
        };

        if (piece.Canonical) {
            auto it = canonical.Map.begin() +
                (std::upper_bound(mapEnds.begin(), mapEnds.end(), piece.Input.Begin) - mapEnds.begin());

            for (; it != canonical.Map.end() && it->Output.Begin < piece.Input.End; ++it) {
                auto map = *it;
                const Range part{
                    std::max(map.Output.Begin, piece.Input.Begin),
                    std::min(map.Output.End, piece.Input.End),
                };

                if (part.Begin >= part.End) {
                    continue;
                }

                if (map.Exact && map.HasOriginal) {
                    map.Original = {
                        map.Original.Begin + part.Begin - map.Output.Begin,
                        map.Original.Begin + part.End - map.Output.Begin,
                    };
                }

                map.Output = shift(part);
                map.Action = action(map.Action);
                result.Map.push_back(map);
            }

            auto protection = std::lower_bound(
                canonical.OutputProtection.begin(),
                canonical.OutputProtection.end(),
                piece.Input.Begin,
                [] (const auto& range, auto begin) {
                    return range.Source.End <= begin;
                });

            for (;
                protection != canonical.OutputProtection.end() && protection->Source.Begin < piece.Input.End;
                ++protection)
            {
                const auto& protectedRange = *protection;
                const Range part{
                    std::max(protectedRange.Source.Begin, piece.Input.Begin),
                    std::min(protectedRange.Source.End, piece.Input.End),
                };

                if (part.Begin < part.End) {
                    result.OutputProtection.push_back({shift(part), protectedRange.Reasons});
                }
            }
        } else {
            result.Map.push_back({
                piece.Output,
                piece.Input,
                document::NoIdentity,
                NoStructuralAction,
                true,
                true,
                false,
                false,
            });
            const auto protections = safety.Protection();
            auto protection = std::lower_bound(
                protections.begin(),
                protections.end(),
                piece.Input.Begin,
                [] (const auto& range, auto begin) {
                    return range.Source.End <= begin;
                });

            for (; protection != protections.end() && protection->Source.Begin < piece.Input.End; ++protection) {
                const auto& protectedRange = *protection;
                const Range part{
                    std::max(protectedRange.Source.Begin, piece.Input.Begin),
                    std::min(protectedRange.Source.End, piece.Input.End),
                };

                if (part.Begin < part.End) {
                    result.OutputProtection.push_back({shift(part), protectedRange.Reasons});
                }
            }
        }
    }

    // Deletions can have no emitted map carrying their action ID.
    for (StructuralActionId id = 0; id < canonical.StructuralOperations.size(); ++id) {
        const auto& operation = canonical.StructuralOperations[id];

        if (!allowedActions[id] || operation.Owner.Empty()) {
            continue;
        }

        const Range owner{tree.Token(operation.Owner.Begin).Text.Begin, tree.Token(operation.Owner.End - 1).Text.End};
        const auto patch = std::lower_bound(
            patches.begin(),
            patches.end(),
            owner.Begin,
            [] (const auto& candidate, auto begin) {
                return candidate.Original.End <= begin;
            });

        if (patch != patches.end() && patch->Original.Begin <= owner.Begin && owner.End <= patch->Original.End) {
            (void)action(id);
        }
    }

    std::pmr::vector<Piece> originalPieces(&memory), canonicalPieces(&memory);

    for (const auto& piece : pieces) {
        (piece.Canonical ? canonicalPieces : originalPieces).push_back(piece);
    }

    // A token is present only when its entire atom survives one input piece.
    // This also restores identities deleted by rejected canonical operations.
    for (parser::TokenIndex i = 0; i < tree.TokenCount(); ++i) {
        for (const bool formatted : {false, true}) {
            if (formatted && !canonical.OriginalTokens[i].Present) {
                continue;
            }

            const auto range = formatted
                ? canonical.OriginalTokens[i].Output
                : Range{tree.Token(i).Text.Begin, tree.Token(i).Text.End};
            const auto& stream = formatted ? canonicalPieces : originalPieces;
            const auto it = std::lower_bound(
                stream.begin(),
                stream.end(),
                range.Begin,
                [] (const auto& piece, auto begin) {
                    return piece.Input.End <= begin;
                });

            if (it != stream.end() && it->Input.Begin <= range.Begin && range.End <= it->Input.End) {
                result.OriginalTokens[i] = {
                    {it->Output.Begin + range.Begin - it->Input.Begin, it->Output.Begin + range.End - it->Input.Begin},
                    true,
                };
            }
        }
    }

    // Transport canonical violations, never recheck widths of hybrid lines.
    // The overflow's exact surviving token is sufficient provenance even when
    // a rejected join or brace operation changes the surrounding indentation.
    std::pmr::vector<HunkPatch> diagnosticTokens(&memory);

    for (parser::TokenIndex i = 0; i < tree.TokenCount(); ++i) {
        const auto& before = canonical.OriginalTokens[i];
        const auto& after = result.OriginalTokens[i];

        if (before.Present && after.Present && view(before.Output, output) == view(after.Output, result.Text)) {
            diagnosticTokens.push_back({after.Output, before.Output});
        }
    }

    std::sort(
        diagnosticTokens.begin(),
        diagnosticTokens.end(),
        [] (const auto& a, const auto& b) {
            return a.Canonical.Begin < b.Canonical.Begin;
        });
    struct DiagnosticPoint
    {
        Offset Output;
        size_t Diagnostic;
    };

    std::pmr::vector<DiagnosticPoint> diagnosticPoints(&memory);
    // Public formatting uses the default eight-cell tab stops. Match its
    // code-point accounting, including standalone CR and CRLF.
    uint32_t codePointColumn = 1;
    const auto advance = [&] (std::string_view text, Offset& offset, util::Position& position) {
        const auto c = static_cast<unsigned char>(text[offset]);

        if (c == '\r' || c == '\n') {
            ++offset;

            if (c == '\r' && offset < text.size() && text[offset] == '\n') {
                ++offset;
            }

            ++position.Line;
            position.Column = 1;
            codePointColumn = 1;
        } else {
            offset += c < 0x80 ? 1 : unicode::utf8::GetFirstCodePointSize(text.substr(offset)).value_or(1);
            position.Column += c == '\t' ? 8 - (position.Column - 1) % 8 : 1;
            ++codePointColumn;
        }
    };

    for (size_t i = 0; i < canonical.Diagnostics.size(); ++i) {
        const auto offset = canonical.Diagnostics[i].Offset;

        if (offset >= canonicalSize) {
            continue;
        }

        auto token = std::upper_bound(
            diagnosticTokens.begin(),
            diagnosticTokens.end(),
            offset,
            [] (auto point, const auto& candidate) {
                return point < candidate.Canonical.Begin;
            });

        if (token != diagnosticTokens.begin()) {
            --token;

            if (offset < token->Canonical.End) {
                diagnosticPoints.push_back({token->Original.Begin + offset - token->Canonical.Begin, i});
                continue;
            }
        }

        // Generated overflow text needs its actual canonical piece to survive;
        // a nearby original token alone cannot justify a rejected operation.
        auto piece = std::upper_bound(
            canonicalPieces.begin(),
            canonicalPieces.end(),
            offset,
            [] (auto point, const auto& candidate) {
                return point < candidate.Input.Begin;
            });

        if (piece != canonicalPieces.begin()) {
            --piece;

            if (offset < piece->Input.End) {
                diagnosticPoints.push_back({piece->Output.Begin + offset - piece->Input.Begin, i});
            }
        }
    }

    std::sort(
        diagnosticPoints.begin(),
        diagnosticPoints.end(),
        [] (const auto& a, const auto& b) {
            return a.Output != b.Output ? a.Output < b.Output : a.Diagnostic < b.Diagnostic;
        });
    Offset offset = 0;
    util::Position position{1, 1};

    for (const auto point : diagnosticPoints) {
        while (offset < point.Output) {
            advance(result.Text, offset, position);
        }

        auto diagnostic = canonical.Diagnostics[point.Diagnostic];
        diagnostic.Position = position;
        diagnostic.Offset = point.Output;
        diagnostic.CodePointColumn = codePointColumn;

        // The measured width belongs to the canonical layout, not necessarily
        // the projected line whose token now carries the caret.
        if (diagnostic.Message.starts_with("emitted line width ")) {
            diagnostic.Message.replace(0, 7, "canonical");
        }

        if (!result.Diagnostics.empty() && result.Diagnostics.back().Position.Line == position.Line &&
            result.Diagnostics.back().Position.Column == position.Column &&
            result.Diagnostics.back().Kind == diagnostic.Kind)
        {
            continue;
        }

        result.Diagnostics.push_back(std::move(diagnostic));
    }

    if (request.Changes || request.ChangeSummary) {
        BuildChangeMetadata(tree, result, request.Changes, memory, originalLines, originalBareCR);
    }

    if (request.DiagnosticLocations) {
        MapWidthLocations(tree, result, memory);
    }

    if (!request.SourceMap) {
        decltype(result.Map){}.swap(result.Map);
        decltype(result.StructuralOperations){}.swap(result.StructuralOperations);
        decltype(result.OutputProtection){}.swap(result.OutputProtection);

        if (!request.TokenMap) {
            decltype(result.OriginalTokens){}.swap(result.OriginalTokens);
        }
    }

    return result;
}

} // namespace formatter::detail
