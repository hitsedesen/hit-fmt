#include "file_check.hpp"

#include "source_excerpt.hpp"

#include <formatter/naming.hpp>
#include <formatter/parameter_names.hpp>

#include <unicode/utf8.hpp>

#include <algorithm>
#include <memory_resource>
#include <stdexcept>
#include <utility>

namespace hit_fmt {

namespace {

parser::SourceRange WholeLines(std::string_view text, parser::SourceRange range)
{
    const auto before = range.Begin ? text.rfind('\n', range.Begin - 1) : std::string_view::npos;
    range.Begin = before == std::string_view::npos ? 0 : before + 1;
    const auto after = range.End && text[range.End - 1] == '\n' ? range.End - 1 : text.find('\n', range.End);
    range.End = after == std::string_view::npos ? text.size() : after + 1;
    return range;
}

std::vector<IncludeOrderChange> IncludeChanges(
    const parser::SyntaxTree& tree,
    const formatter::FormatResult& formatting)
{
    const auto count = std::count_if(
        formatting.StructuralOperations.begin(),
        formatting.StructuralOperations.end(),
        [] (const auto& operation) {
            return operation.Kind == formatter::StructuralAction::IncludeOrder;
        });
    if (!count) {
        return {};
    }
    std::vector<IncludeOrderChange> changes;
    changes.reserve(count);
    std::vector<uint32_t> indices(formatting.StructuralOperations.size(),
        formatter::NoStructuralAction);
    for (std::size_t i = 0; i < indices.size(); ++i) {
        const auto& operation = formatting.StructuralOperations[i];
        if (operation.Kind != formatter::StructuralAction::IncludeOrder) {
            continue;
        }
        indices[i] = changes.size();
        auto position = tree.TokenPosition(operation.Owner.Begin);
        position.Column = 1;
        changes.push_back({
            {tree.Token(operation.Owner.Begin).Text.Begin, tree.Token(operation.Owner.End - 1).Text.End},
            {UINT32_MAX, 0},
            position,
        });
    }
    // Use admitted operation provenance, not textual include guesses. Restrict
    // associations to directive interiors, excluding neighboring layout gaps.
    for (const auto& map : formatting.Map) {
        if (map.Action == formatter::NoStructuralAction || !map.HasOriginal ||
            indices.at(map.Action) == formatter::NoStructuralAction)
        {
            continue;
        }
        auto& change = changes[indices[map.Action]];
        const auto original = change.Original;
        const bool intersects = map.Original.Empty()
            ? original.Begin <= map.Original.Begin && map.Original.Begin < original.End
            : map.Original.Begin < original.End && original.Begin < map.Original.End;
        if (intersects && !map.Output.Empty()) {
            change.Emitted.Begin = std::min(change.Emitted.Begin, map.Output.Begin);
            change.Emitted.End = std::max(change.Emitted.End, map.Output.End);
        }
    }
    for (auto& change : changes) {
        if (change.Emitted.Begin == UINT32_MAX) {
            throw std::runtime_error("include sorting has no output mapping");
        }
        change.Original = WholeLines(tree.Source(), change.Original);
        change.Emitted = WholeLines(formatting.Text, change.Emitted);
    }
    std::ranges::sort(
        changes,
        {},
        [] (const auto& change) {
            return change.Original.Begin;
        });
    return changes;
}

bool OtherFormattingChanged(
    std::string_view original,
    const formatter::FormatResult& formatting,
    std::span<const IncludeOrderChange> changes)
{
    std::size_t source = 0, emitted = 0;
    for (const auto& change : changes) {
        if (change.Original.Begin < source || change.Emitted.Begin < emitted) {
            throw std::runtime_error("overlapping include diagnostic blocks");
        }
        if (original.substr(source, change.Original.Begin - source) !=
            std::string_view(formatting.Text).substr(emitted, change.Emitted.Begin - emitted))
        {
            return true;
        }
        source = change.Original.End;
        emitted = change.Emitted.End;
    }
    return original.substr(source) != std::string_view(formatting.Text).substr(emitted);
}

// Convert code-point widths with tab stops to tokenizer columns.
size_t WidthCaretColumn(std::string_view text, size_t target)
{
    size_t width = 1, column = 1;
    for (size_t offset = 0; offset < text.size() && width < target; ++column) {
        const auto c = static_cast<unsigned char>(text[offset]);
        const auto size = c < 0x80 ? 1 : unicode::utf8::GetFirstCodePointSize(text.substr(offset)).value_or(1);
        const auto nextWidth = width + (c == '\t' ? 8 - (width - 1) % 8 : 1);
        if (nextWidth > target) {
            break;
        }
        width = nextWidth;
        offset += size;
    }
    return column;
}

bool Before(util::Position a, util::Position b)
{
    return a.Line < b.Line || (a.Line == b.Line && a.Column < b.Column);
}

// Sorted queries permit one byte/code-point sweep of each text, including
// multiple parameter-name points on a wide line. No per-diagnostic file scan.
class PositionSweep
{
public:
    explicit PositionSweep(std::string_view text)
        : Text_(text)
    { }

    size_t Offset(util::Position position)
    {
        while (Before(Current_, position) && Cursor_ < Text_.size()) {
            Advance();
        }
        if (Current_.Line != position.Line || Current_.Column != position.Column) {
            throw std::runtime_error("invalid original diagnostic position");
        }
        return Cursor_;
    }

    util::Position Position(size_t offset, bool containing = false)
    {
        if (offset < Cursor_ || offset > Text_.size()) {
            throw std::runtime_error("invalid emitted diagnostic offset");
        }
        while (Cursor_ < offset) {
            if (containing && NextOffset() > offset) {
                return Current_;
            }
            Advance();
        }
        if (Cursor_ != offset) {
            throw std::runtime_error("diagnostic offset splits a code point");
        }
        return Current_;
    }

private:
    size_t NextOffset() const
    {
        const auto c = static_cast<unsigned char>(Text_[Cursor_]);
        // Match tokenizer coordinates: each invalid UTF8 byte is one column.
        return Cursor_ + (c < 0x80 ? 1 : unicode::utf8::GetFirstCodePointSize(Text_.substr(Cursor_)).value_or(1));
    }

    void Advance()
    {
        const auto c = static_cast<unsigned char>(Text_[Cursor_]);
        Cursor_ = NextOffset();
        if (c == '\n') {
            ++Current_.Line;
            Current_.Column = 1;
        } else {
            ++Current_.Column;
        }
    }

    std::string_view Text_;
    size_t Cursor_ = 0;
    util::Position Current_{1, 1};
};

bool InLines(size_t line, std::span<const formatter::Hunk> hunks)
{
    const auto it =
        std::lower_bound(hunks.begin(), hunks.end(), line, [] (auto hunk, auto target) { return hunk.End < target; });
    return it != hunks.end() && it->Begin <= line;
}

std::string_view RequiredChangeMessage(formatter::StructuralAction action)
{
    using Action = formatter::StructuralAction;
    switch (action) {
        case Action::AddMissingControlBraces:
            return "control body requires braces";
        case Action::EmptyControlBody:
            return "empty control body must use an empty block";
        case Action::RemoveNullStatements:
            return "redundant null statement must be removed";
        case Action::RemoveNullDeclarations:
            return "redundant null declaration must be removed";
        case Action::IncludeOrder:
        case Action::NamespaceComments:
            break; // These skips are not proof of a required edit.
    }
    throw std::logic_error("unexpected required hunk edit");
}

constexpr std::string_view BlockedHunkFix = "; cannot automatically fix within selected hunks or allowed spillover";

void MapSemanticPositions(
    const parser::SyntaxTree& tree,
    const formatter::FormatResult& formatting,
    std::vector<SemanticDiagnostic>& diagnostics)
{
    std::sort(
        diagnostics.begin(),
        diagnostics.end(),
        [] (const auto& a, const auto& b) { return Before(a.Original, b.Original); });
    std::pmr::monotonic_buffer_resource scratch;
    struct Point
    {
        size_t Offset;
        size_t Diagnostic;
    };
    std::pmr::vector<Point> points(&scratch);
    points.reserve(diagnostics.size());
    PositionSweep original(tree.Source());
    for (size_t i = 0; i < diagnostics.size(); ++i) {
        const auto offset = original.Offset(diagnostics[i].Original);
        // Prefer the token beginning at a point; otherwise the containing or
        // preceding endpoint. Missing-name diagnostics can be insertion points.
        parser::TokenIndex low = 0, high = tree.TokenCount();
        while (low < high) {
            const auto middle = low + (high - low) / 2;
            if (tree.Token(middle).Text.Begin < offset) {
                low = middle + 1;
            } else {
                high = middle;
            }
        }
        auto token = low;
        if (token == tree.TokenCount() || tree.Token(token).Text.Begin != offset) {
            if (token == 0) {
                throw std::runtime_error("diagnostic has no original token anchor");
            }
            --token;
        }
        const auto range = tree.Token(token).Text;
        const auto& mapped = formatting.OriginalTokens.at(token);
        if (offset < range.Begin || offset > range.End || !mapped.Present ||
            mapped.Output.End - mapped.Output.Begin != range.End - range.Begin)
        {
            throw std::runtime_error("diagnostic token identity did not survive formatting");
        }
        points.push_back({mapped.Output.Begin + offset - range.Begin, i});
    }
    std::sort(points.begin(), points.end(), [] (const auto& a, const auto& b) { return a.Offset < b.Offset; });
    PositionSweep emitted(formatting.Text);
    for (const auto point : points) {
        diagnostics[point.Diagnostic].Emitted = emitted.Position(point.Offset);
    }
}

void MapWidthPositions(const parser::SyntaxTree& tree, CheckedFile& checked)
{
    if (checked.Formatting.Diagnostics.empty()) {
        return;
    }
    std::pmr::monotonic_buffer_resource scratch;
    std::pmr::vector<parser::TokenIndex> tokens(&scratch);
    tokens.reserve(tree.TokenCount());
    const auto& mapped = checked.Formatting.OriginalTokens;
    for (parser::TokenIndex token = 0; token < tree.TokenCount(); ++token) {
        if (mapped[token].Present) {
            tokens.push_back(token);
        }
    }
    std::sort(
        tokens.begin(),
        tokens.end(),
        [&] (auto a, auto b) {
            return mapped[a].Output.Begin < mapped[b].Output.Begin;
        });
    const auto& text = checked.Formatting.Text;
    size_t offset = 0, column = 1;
    uint32_t line = 1;
    struct Point
    {
        size_t Offset;
        size_t Diagnostic;
    };
    std::pmr::vector<Point> points(&scratch);
    points.reserve(checked.Formatting.Diagnostics.size());
    for (const auto& diagnostic : checked.Formatting.Diagnostics) {
        // Match the formatter's code-point/tab-stop coordinates, including
        // CRLF and standalone CR. Keep offsets at code-point boundaries;
        // generated text uses an original token anchor.
        while (offset < text.size() && (line < diagnostic.Position.Line || column < diagnostic.Position.Column)) {
            const auto c = static_cast<unsigned char>(text[offset]);
            if (c == '\r' || c == '\n') {
                ++offset;
                if (c == '\r' && offset < text.size() && text[offset] == '\n') {
                    ++offset;
                }
                ++line;
                column = 1;
            } else {
                const auto nextColumn = column + (c == '\t' ? 8 - (column - 1) % 8 : 1);
                if (line == diagnostic.Position.Line && nextColumn > diagnostic.Position.Column) {
                    break; // The overflow starts inside this tab's cells.
                }
                offset += c < 0x80
                    ? 1
                    : unicode::utf8::GetFirstCodePointSize(std::string_view(text).substr(offset)).value_or(1);
                column = nextColumn;
            }
        }
        auto token = std::upper_bound(
            tokens.begin(),
            tokens.end(),
            offset,
            [&] (size_t point, auto id) { return point < mapped[id].Output.Begin; });
        if (token != tokens.begin()) {
            --token;
        }
        // Generated text, such as a namespace comment, uses the preceding
        // original token as its source anchor.
        size_t original = 0;
        if (token != tokens.end()) {
            const auto source = tree.Token(*token).Text;
            const auto output = mapped[*token].Output;
            original = source.Begin;
            if (offset >= output.Begin && offset < output.End && output.End - output.Begin == source.End - source.Begin)
            {
                original += offset - output.Begin;
            }
        }
        points.push_back({original, points.size()});
    }
    std::sort(
        points.begin(),
        points.end(),
        [] (auto a, auto b) {
            return a.Offset < b.Offset;
        });
    checked.OriginalWidthPositions.resize(points.size());
    PositionSweep original(tree.Source());
    for (const auto point : points) {
        checked.OriginalWidthPositions[point.Diagnostic] = original.Position(point.Offset, true);
    }
}

} // namespace

PreparedFileConfiguration::PreparedFileConfiguration(formatter::Configuration configuration)
    : Settings(std::move(configuration))
    , Formatting(Settings.FormattingStyle)
{ }

bool CheckedFile::HasFailures(bool fix) const
{
    return (!fix && FormattingMismatch) || !Semantic.empty() || !RequiredChanges.empty() ||
        !Formatting.Diagnostics.empty();
}

CheckedFile CheckFile(
    util::PaddedSourceView source,
    const PreparedFileConfiguration& configuration,
    std::string_view inputFilePath,
    std::optional<ChangedFileScope> changed)
{
    auto tree = parser::Parse(source, [] (parser::ParseError) {}, configuration.Formatting.ParsingOptions());
    CheckedFile checked;
    checked.ColumnLimit = configuration.Formatting.Policies.ColumnLimit;
    for (auto& diagnostic : formatter::CheckNames(tree, configuration.Settings)) {
        checked.Semantic.push_back({diagnostic.Position, {}, diagnostic.Message + "; " + diagnostic.Expected});
    }
    for (const auto& diagnostic : formatter::CheckParameterNames(tree, configuration.Settings)) {
        auto message = "parameter #" + std::to_string(diagnostic.ParameterNumber) + " in " + diagnostic.Function +
            ": expected a name";
        if (diagnostic.AllowCommentedName) {
            message += " or /*name*/ comment";
        }
        checked.Semantic.push_back({diagnostic.Position, {}, std::move(message)});
    }
    // Hunk edit authority and include-order diagnostics require full source
    // associations. Other whole-file diagnostics need only token identities.
    const bool tokenMap = !checked.Semantic.empty() ||
        configuration.Formatting.Policies.WidthDiagnostics == formatter::WidthDiagnosticPolicy::Report;
    std::vector<formatter::Hunk> selected, permitted;
    const formatter::FormatRequest request{
        .SourceMap = changed.has_value() || configuration.Formatting.Structural.SortIncludes,
        .InputFilePath = inputFilePath,
        .TokenMap = tokenMap,
    };
    if (changed) {
        auto result =
            formatter::FormatChanged(tree, configuration.Formatting, changed->Hunks, changed->Spillover, request);
        selected = std::move(result.SelectedLines);
        permitted = std::move(result.PermittedLines);
        checked.Incomplete = std::move(result.Incomplete);
        for (const auto& skip : result.RequiredSkips) {
            checked.RequiredChanges.push_back(
                {tree.TokenPosition(skip.Tokens.Begin), {}, std::string(RequiredChangeMessage(skip.Action))});
        }
        checked.Formatting = std::move(result.Result);
        std::erase_if(
            checked.Semantic,
            [&] (const auto& diagnostic) {
                return !InLines(diagnostic.Original.Line, selected);
            });
    } else {
        checked.Formatting = formatter::Format(tree, configuration.Formatting, request);
    }
    checked.FormattingMismatch = checked.Formatting.Text != tree.Source();
    if (checked.FormattingMismatch) {
        checked.IncludeOrderChanges = IncludeChanges(tree, checked.Formatting);
        checked.OtherFormattingMismatch = checked.IncludeOrderChanges.empty() ||
            OtherFormattingChanged(tree.Source(), checked.Formatting, checked.IncludeOrderChanges);
    }
    MapSemanticPositions(tree, checked.Formatting, checked.Semantic);
    MapSemanticPositions(tree, checked.Formatting, checked.RequiredChanges);
    MapWidthPositions(tree, checked);
    if (changed) {
        size_t count = 0;
        for (size_t i = 0; i < checked.Formatting.Diagnostics.size(); ++i) {
            if (InLines(checked.OriginalWidthPositions[i].Line, permitted)) {
                if (count != i) {
                    checked.Formatting.Diagnostics[count] = std::move(checked.Formatting.Diagnostics[i]);
                }
                checked.OriginalWidthPositions[count++] = checked.OriginalWidthPositions[i];
            }
        }
        checked.Formatting.Diagnostics.resize(count);
        checked.OriginalWidthPositions.resize(count);
    }
    return checked;
}

void PrintFileIssues(
    std::ostream& output,
    std::string_view path,
    std::string_view originalSource,
    const CheckedFile& checked,
    bool fix,
    size_t terminalWidth)
{
    if (!fix && !checked.IncludeOrderChanges.empty()) {
        const auto position = checked.IncludeOrderChanges.front().Position;
        output << "fail: includes are not sorted\n  --> " << path << ':' << position.Line << ':' << position.Column
            << " [original source]\n";
    }
    for (const auto& diagnostic : checked.Semantic) {
        const auto position = fix ? diagnostic.Emitted : diagnostic.Original;
        output << "fail: " << diagnostic.Message << "\n  --> " << path << ':' << position.Line << ':' << position.Column
            << (fix ? " [written output]" : " [original source]") << '\n';
    }
    for (const auto& diagnostic : checked.RequiredChanges) {
        const auto position = fix ? diagnostic.Emitted : diagnostic.Original;
        output << "fail: " << diagnostic.Message;
        if (fix) {
            output << BlockedHunkFix;
        }
        output << "\n  --> " << path << ':' << position.Line << ':' << position.Column
            << (fix ? " [written output]" : " [original source]") << '\n';
    }
    if (checked.Formatting.Diagnostics.empty()) {
        return;
    }
    const std::string_view source = fix ? checked.Formatting.Text : originalSource;
    // One scan indexes borrowed line views, regardless of diagnostic order.
    std::vector<std::string_view> lines;
    lines.reserve(std::count(source.begin(), source.end(), '\n') + 1);
    for (size_t begin = 0; begin <= source.size();) {
        const auto newline = fix ? source.find_first_of("\r\n", begin) : source.find('\n', begin);
        const auto end = newline == source.npos ? source.size() : newline;
        auto text = source.substr(begin, end - begin);
        if (text.ends_with('\r')) {
            text.remove_suffix(1);
        }
        lines.push_back(text);
        if (newline == source.npos) {
            break;
        }
        begin = end + 1;
        if (fix && source[end] == '\r' && begin < source.size() && source[begin] == '\n') {
            ++begin;
        }
    }
    for (size_t i = 0; i < checked.Formatting.Diagnostics.size(); ++i) {
        const auto& diagnostic = checked.Formatting.Diagnostics[i];
        const auto position = fix ? diagnostic.Position : checked.OriginalWidthPositions.at(i);
        output << "fail: "
            << (fix ? "cannot format this line automatically within ColumnLimit " : "line exceeds ColumnLimit ")
            << checked.ColumnLimit << "\n  --> " << path << ':' << position.Line << ':' << position.Column
            << (fix ? " [written output]" : " [original source]") << '\n';
        const auto number = std::to_string(position.Line);
        const auto gutter = std::string(number.size() + 1, ' ') + "| ";
        const auto prefix = number + " | ";
        const auto text = lines.at(position.Line - 1);
        const auto column = fix ? WidthCaretColumn(text, position.Column) : position.Column;
        output << gutter.substr(0, gutter.size() - 1) << '\n' << prefix;
        const auto excerpt = PrintSourceExcerpt(output, text, column, prefix.size(), terminalWidth);
        if (excerpt.CaretVisible) {
            output << gutter << std::string(excerpt.CaretOffset, ' ') << "^\n";
        }
    }
}

void PrintFileSummaryIssues(std::ostream& output, const CheckedFile& checked, bool fix)
{
    bool separated = false;
    auto issue = [&] (std::string_view message, bool failure = false) {
        if (separated) {
            output << "; ";
        }
        if (failure) {
            output << "fail: ";
        }
        output << message;
        separated = true;
    };
    if (!fix && checked.FormattingMismatch && (checked.IncludeOrderChanges.empty() || checked.OtherFormattingMismatch))
    {
        issue("formatting mismatch");
    }
    if (!fix && !checked.IncludeOrderChanges.empty()) {
        issue("includes are not sorted", true);
    }
    if (!checked.Semantic.empty()) {
        issue("fail: naming or parameter-name violation");
    }
    for (const auto& diagnostic : checked.RequiredChanges) {
        issue(diagnostic.Message, true);
        if (fix) {
            output << BlockedHunkFix;
        }
    }
    if (!checked.Formatting.Diagnostics.empty()) {
        issue(fix ? "fail: cannot format automatically within column limit" : "fail: column-limit violation");
    }
}

} // namespace hit_fmt
