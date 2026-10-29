#include "format_changes_internal.hpp"

#include <unicode/utf8.hpp>

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace formatter {

SourceLines::SourceLines(std::string_view text, bool splitBareCR)
{
    const auto size = CheckedSourceAnalysisSize(text.size());
    Boundaries_.reserve(std::min<size_t>(size / 40 + 2, 1024));
    Boundaries_.push_back(0);
    for (parser::SourceOffset i = 0; i < size; ++i) {
        const bool bareCR = text[i] == '\r' && (i + 1 == size || text[i + 1] != '\n');
        BareCR_ |= bareCR;
        if (text[i] == '\n' || (splitBareCR && bareCR)) {
            Boundaries_.push_back(i + 1);
        }
    }
    if (Boundaries_.back() != size) {
        Boundaries_.push_back(size);
    }
}

SourceLines::SourceLines(std::span<const parser::SourceOffset> boundaries, bool containsBareCR)
    : Boundaries_(boundaries.begin(), boundaries.end())
    , BareCR_(containsBareCR)
{ }

size_t SourceLines::Count() const
{
    return Boundaries_.empty() ? 0 : Boundaries_.size() - 1;
}

size_t SourceLines::LineAt(parser::SourceOffset offset) const
{
    if (Boundaries_.empty() || offset > Boundaries_.back()) {
        throw std::out_of_range("invalid source line offset");
    }
    return std::max<size_t>(
        1,
        std::min<size_t>(
            Count(),
            std::upper_bound(Boundaries_.begin(), Boundaries_.end(), offset) - Boundaries_.begin()));
}

parser::SourceRange SourceLines::Range(size_t line) const
{
    if (line == 1 && Count() == 0) {
        return {};
    }
    if (!line || line > Count()) {
        throw std::out_of_range("invalid physical source line");
    }
    return {Boundaries_[line - 1], Boundaries_[line]};
}

parser::SourceRange SourceLines::Cover(parser::SourceRange range) const
{
    const auto first = LineAt(range.Begin);
    const auto last = LineAt(range.Empty() ? range.End : range.End - 1);
    return {Range(first).Begin, Range(last).End};
}

std::string_view SourceLines::Text(std::string_view text, size_t line) const
{
    const auto range = Range(line);
    auto result = text.substr(range.Begin, range.End - range.Begin);
    if (result.ends_with('\n')) {
        result.remove_suffix(1);
    }
    return result;
}

util::Position SourceLines::Position(std::string_view text, parser::SourceOffset offset) const
{
    const auto line = LineAt(offset);
    const auto range = Range(line);
    const auto end = std::min<size_t>(offset, range.Begin + Text(text, line).size());
    uint32_t column = 1;
    for (size_t i = range.Begin; i < end; ++column) {
        i += unicode::utf8::GetFirstCodePointSize(text.substr(i, end - i)).value_or(1);
    }
    return {static_cast<uint32_t>(line), column};
}

std::span<const parser::SourceOffset> SourceLines::Boundaries() const
{
    return Boundaries_;
}

bool SourceLines::ContainsBareCR() const
{
    return BareCR_;
}

namespace {

using Range = parser::SourceRange;
using Offset = parser::SourceOffset;

std::string_view View(std::string_view text, Range range)
{
    return text.substr(range.Begin, range.End - range.Begin);
}

bool Whitespace(std::string_view text)
{
    return text.find_first_not_of(" \t\r\n\v\f") == std::string_view::npos;
}

class ChangeBuilder
{
public:
    ChangeBuilder(
        std::string_view source,
        std::string_view output,
        std::vector<FormatChange>& edits,
        bool& otherChanges)
        : Source_(source)
        , Output_(output)
        , Edits_(edits)
        , OtherChanges_(otherChanges)
    { }

    void Gap(Range before, Range after)
    {
        const auto original = View(Source_, before);
        const auto formatted = View(Output_, after);
        if (original == formatted) {
            return;
        }
        if (!Whitespace(original) || !Whitespace(formatted)) {
            Content(before, after);
            return;
        }
        // Matching physical newlines separate indentation and trailing gaps.
        // Reflow with a changed newline count remains one complete gap.
        if (std::count(original.begin(), original.end(), '\n') == std::count(formatted.begin(), formatted.end(), '\n'))
        {
            auto begin = before.Begin;
            auto emitted = after.Begin;
            for (auto i = before.Begin; i < before.End; ++i) {
                if (Source_[i] != '\n') {
                    continue;
                }
                const auto end = static_cast<Offset>(Output_.find('\n', emitted));
                Add({begin, i}, {emitted, end}, FormatChangeKind::Whitespace);
                begin = i + 1;
                emitted = end + 1;
            }
            Add({begin, before.End}, {emitted, after.End}, FormatChangeKind::Whitespace);
        } else {
            Add(before, after, FormatChangeKind::Whitespace);
        }
    }

    void Add(Range before, Range after, FormatChangeKind kind)
    {
        if (View(Source_, before) != View(Output_, after)) {
            OtherChanges_ |= kind != FormatChangeKind::IncludeOrder;
            Edits_.push_back({
                before,
                after,
                kind,
                kind == FormatChangeKind::IncludeOrder ? std::optional{StructuralAction::IncludeOrder} : std::nullopt,
            });
        }
    }

private:
    void Content(Range before, Range after)
    {
        const auto original = View(Source_, before);
        const auto formatted = View(Output_, after);
        size_t prefix = 0;
        while (prefix < original.size() && prefix < formatted.size() && original[prefix] == formatted[prefix]) {
            ++prefix;
        }
        const auto continuation = [] (std::string_view text, size_t offset) {
            return offset < text.size() && (static_cast<unsigned char>(text[offset]) & 0xc0) == 0x80;
        };
        while (prefix && (continuation(original, prefix) || continuation(formatted, prefix))) {
            --prefix;
        }
        size_t suffix = 0;
        while (suffix < original.size() - prefix && suffix < formatted.size() - prefix &&
            original[original.size() - suffix - 1] == formatted[formatted.size() - suffix - 1])
        {
            ++suffix;
        }
        while (suffix &&
            (continuation(original, original.size() - suffix) || continuation(formatted, formatted.size() - suffix)))
        {
            --suffix;
        }
        Add(
            {before.Begin + static_cast<Offset>(prefix), before.End - static_cast<Offset>(suffix)},
            {after.Begin + static_cast<Offset>(prefix), after.End - static_cast<Offset>(suffix)},
            FormatChangeKind::Content);
    }

    std::string_view Source_;
    std::string_view Output_;
    std::vector<FormatChange>& Edits_;
    bool& OtherChanges_;
};

std::pmr::vector<FormatChange> IncludeBlocks(
    const parser::SyntaxTree& tree,
    const FormatResult& result,
    const SourceLines& originalLines,
    const SourceLines& outputLines,
    std::pmr::memory_resource& scratch)
{
    std::pmr::vector<FormatChange> blocks(&scratch);
    if (result.StructuralOperations.empty()) {
        return blocks;
    }
    constexpr auto missing = std::numeric_limits<uint32_t>::max();
    std::pmr::vector<uint32_t> indices(result.StructuralOperations.size(), missing, &scratch);
    for (size_t i = 0; i < indices.size(); ++i) {
        const auto& operation = result.StructuralOperations[i];
        if (operation.Kind == StructuralAction::IncludeOrder && !operation.Owner.Empty()) {
            indices[i] = blocks.size();
            blocks.push_back({
                {tree.Token(operation.Owner.Begin).Text.Begin, tree.Token(operation.Owner.End - 1).Text.End},
                {missing, 0},
                FormatChangeKind::IncludeOrder,
            });
        }
    }
    if (blocks.empty()) {
        return blocks;
    }
    for (const auto& map : result.Map) {
        if (map.Action == NoStructuralAction || !map.HasOriginal || indices.at(map.Action) == missing) {
            continue;
        }
        auto& block = blocks[indices[map.Action]];
        const bool intersects = map.Original.Empty()
            ? block.Original.Begin <= map.Original.Begin && map.Original.Begin < block.Original.End
            : map.Original.Begin < block.Original.End && block.Original.Begin < map.Original.End;
        if (intersects && !map.Output.Empty()) {
            block.Output.Begin = std::min(block.Output.Begin, map.Output.Begin);
            block.Output.End = std::max(block.Output.End, map.Output.End);
        }
    }
    for (auto& block : blocks) {
        if (block.Output.Begin == missing) {
            throw std::runtime_error("include sorting has no final output mapping");
        }
        block.Original = originalLines.Cover(block.Original);
        block.Output = outputLines.Cover(block.Output);
    }
    std::sort(
        blocks.begin(),
        blocks.end(),
        [] (const auto& a, const auto& b) {
            return a.Original.Begin < b.Original.Begin;
        });
    for (size_t i = 1; i < blocks.size(); ++i) {
        if (blocks[i].Original.Begin < blocks[i - 1].Original.End || blocks[i].Output.Begin < blocks[i - 1].Output.End)
        {
            throw std::runtime_error("overlapping include diagnostic blocks");
        }
    }
    return blocks;
}

void BuildDetailedChanges(
    const parser::SyntaxTree& tree,
    FormatResult& result,
    std::span<const FormatChange> blocks,
    FormatChanges& changes,
    bool& otherChanges)
{
    const auto source = tree.Source();
    const auto output = std::string_view(result.Text);
    if (result.OriginalTokens.size() != tree.TokenCount()) {
        throw std::runtime_error("formatting changes require final token identities");
    }
    // Verbatim atoms can copy many original tokens without per-token renderer
    // records. Recover those identities from the few tracked exact copies.
    for (const auto& map : result.Map) {
        if (!map.Exact || !map.HasOriginal || map.OriginalToken != document::NoIdentity || map.Original.Empty() ||
            map.Output.End - map.Output.Begin != map.Original.End - map.Original.Begin)
        {
            continue;
        }
        parser::TokenIndex begin = 0, end = tree.TokenCount();
        while (begin < end) {
            const auto middle = begin + (end - begin) / 2;
            if (tree.Token(middle).Text.Begin < map.Original.Begin) {
                begin = middle + 1;
            } else {
                end = middle;
            }
        }
        for (auto id = begin; id < tree.TokenCount() && tree.Token(id).Text.End <= map.Original.End; ++id) {
            if (!result.OriginalTokens[id].Present) {
                const auto range = tree.Token(id).Text;
                result.OriginalTokens[id] = {
                    {
                        map.Output.Begin + range.Begin - map.Original.Begin,
                        map.Output.Begin + range.End - map.Original.Begin,
                    },
                    true,
                };
            }
        }
    }
    ChangeBuilder builder(source, output, changes.Edits, otherChanges);
    Offset original = 0, emitted = 0;
    const auto anchor = [&] (parser::TokenIndex token) {
        const auto& mapped = result.OriginalTokens[token];
        const auto range = tree.Token(token).Text;
        if (!mapped.Present || range.Empty() || range.Begin < original || mapped.Output.Begin < emitted ||
            View(source, {range.Begin, range.End}) != View(output, mapped.Output))
        {
            return;
        }
        builder.Gap({original, range.Begin}, {emitted, mapped.Output.Begin});
        original = range.End;
        emitted = mapped.Output.End;
    };
    parser::TokenIndex token = 0;
    for (const auto& block : blocks) {
        while (token < tree.TokenCount() && tree.Token(token).Text.End <= block.Original.Begin) {
            anchor(token++);
        }
        if (block.Original.Begin < original || block.Output.Begin < emitted) {
            throw std::runtime_error("overlapping formatting change blocks");
        }
        builder.Gap({original, block.Original.Begin}, {emitted, block.Output.Begin});
        builder.Add(block.Original, block.Output, FormatChangeKind::IncludeOrder);
        original = block.Original.End;
        emitted = block.Output.End;
        while (token < tree.TokenCount() && tree.Token(token).Text.Begin < original) {
            ++token;
        }
    }
    while (token < tree.TokenCount()) {
        anchor(token++);
    }
    builder
        .Gap({original, CheckedSourceAnalysisSize(source.size())}, {emitted, CheckedSourceAnalysisSize(output.size())});
    for (const auto& map : result.Map) {
        if (!map.Generated || map.Action == NoStructuralAction || map.Output.Empty()) {
            continue;
        }
        auto edit = std::lower_bound(
            changes.Edits.begin(),
            changes.Edits.end(),
            map.Output.Begin,
            [] (const auto& change, Offset offset) {
                return change.Output.End <= offset;
            });
        for (; edit != changes.Edits.end() && edit->Output.Begin < map.Output.End; ++edit) {
            if (edit->Kind == FormatChangeKind::Content && !edit->Action) {
                edit->Action = result.StructuralOperations.at(map.Action).Kind;
            }
        }
    }
}

} // namespace

void detail::BuildChangeMetadata(
    const parser::SyntaxTree& tree,
    FormatResult& result,
    bool detailed,
    std::pmr::memory_resource& scratch,
    std::span<const parser::SourceOffset> originalLines,
    bool originalBareCR)
{
    auto& summary = result.Summary.emplace();
    const auto source = tree.Source();
    const auto output = std::string_view(result.Text);
    const bool unchanged = source == output;
    auto* changes = detailed ? &result.Changes.emplace() : nullptr;
    if (changes) {
        changes->OriginalLines =
            originalLines.empty() ? SourceLines(source) : SourceLines(originalLines, originalBareCR);
        changes->OutputLines = unchanged ? changes->OriginalLines : SourceLines(output);
    }
    if (unchanged) {
        return;
    }
    const bool includes = std::ranges::any_of(
        result.StructuralOperations,
        [] (const auto& operation) {
            return operation.Kind == StructuralAction::IncludeOrder;
        });
    if (!changes && !includes) {
        summary.OtherChanges = true;
        return;
    }
    SourceLines sourceLines, outputLines;
    if (includes && !changes) {
        sourceLines = originalLines.empty() ? SourceLines(source) : SourceLines(originalLines, originalBareCR);
        outputLines = SourceLines(output);
    }
    std::pmr::vector<FormatChange> blocks(&scratch);
    if (includes) {
        const auto& original = changes ? changes->OriginalLines : sourceLines;
        blocks = IncludeBlocks(tree, result, original, changes ? changes->OutputLines : outputLines, scratch);
        summary.IncludeOrderChanges.reserve(blocks.size());
        for (const auto& block : blocks) {
            if (View(source, block.Original) != View(output, block.Output)) {
                summary.IncludeOrderChanges.push_back(
                    {block.Original, block.Output, {static_cast<uint32_t>(original.LineAt(block.Original.Begin)), 1}});
            }
        }
    }
    if (!changes) {
        Offset original = 0, emitted = 0;
        for (const auto& block : blocks) {
            if (View(source, {original, block.Original.Begin}) != View(output, {emitted, block.Output.Begin})) {
                summary.OtherChanges = true;
                return;
            }
            original = block.Original.End;
            emitted = block.Output.End;
        }
        summary.OtherChanges = source.substr(original) != output.substr(emitted);
        return;
    }
    BuildDetailedChanges(tree, result, blocks, *changes, summary.OtherChanges);
}

} // namespace formatter
