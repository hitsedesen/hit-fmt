#include "formatting_diagnostics.hpp"

#include "source_excerpt.hpp"

#include <algorithm>
#include <cctype>
#include <limits>
#include <optional>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

namespace hit_fmt {

namespace {

constexpr size_t DiffLookahead = 16;

struct Line
{
    std::string_view Text;
    bool HasNewline;
    size_t Offset;

    bool operator==(const Line& other) const
    {
        return Text == other.Text && HasNewline == other.HasNewline;
    }
};

std::vector<Line> SplitLines(std::string_view text)
{
    std::vector<Line> lines;
    lines.reserve(std::count(text.begin(), text.end(), '\n') + 1);

    size_t begin = 0;
    while (begin < text.size()) {
        auto newline = text.find('\n', begin);
        bool hasNewline = newline != std::string_view::npos;
        auto end = hasNewline ? newline : text.size();
        lines.push_back({.Text = text.substr(begin, end - begin), .HasNewline = hasNewline, .Offset = begin});
        begin = hasNewline ? end + 1 : text.size();
    }
    return lines;
}

void PrintWrappedMessage(std::ostream& output, std::string_view message, std::string_view prefix, size_t terminalWidth)
{
    auto available = terminalWidth > prefix.size() + 2 ? terminalWidth - prefix.size() - 2 : size_t{20};
    while (!message.empty()) {
        auto length = std::min(available, message.size());
        if (length < message.size()) {
            auto space = message.rfind(' ', length);
            if (space != std::string_view::npos && space > 0) {
                length = space;
            }
        }
        while (length && length < message.size() && (static_cast<unsigned char>(message[length]) & 0xc0) == 0x80) {
            --length;
        }
        output << "  " << message.substr(0, length) << '\n';
        message.remove_prefix(length);
        while (message.starts_with(' ')) {
            message.remove_prefix(1);
        }
        if (!message.empty()) {
            output << prefix;
        }
    }
}

void PrintSuggestedLine(std::ostream& output, std::string_view text, size_t gutterWidth, const Terminal& terminal)
{
    auto cells = RenderLine(text, 0, gutterWidth).Cells;
    auto available = terminal.Width > gutterWidth + 1 ? terminal.Width - gutterWidth - 1 : size_t{16};
    for (size_t begin = 0;;) {
        output << terminal.Green;
        if (begin) {
            output << std::string(gutterWidth, ' ');
        } else {
            output << std::string(gutterWidth - 3, ' ') << " + ";
        }
        auto end = std::min(cells.size(), begin + available);
        PrintCells(output, cells, begin, end);
        output << terminal.Reset;
        output << '\n';
        if (end == cells.size()) {
            break;
        }
        begin = end;
    }
}

enum class ViolationKind
{
    Other,
    Reflow
};

struct Violation
{
    size_t Line;
    size_t Column;
    std::string Message;
    ViolationKind Kind = ViolationKind::Other;
};

struct Chunk
{
    // Source line interval [Begin, End), one-based. Empty input uses [1, 2).
    size_t Begin;
    size_t End;
    std::vector<Violation> Violations;
    std::vector<Line> Fix;
    size_t SourceBegin = 0;
    size_t SourceEnd = 0;
    size_t FormattedBegin = 0;
    size_t FormattedEnd = 0;
    bool HasFix = false;
};

// The first differing code point is normally the token at which formatting
// diverges. For removed whitespace, point to the following token instead.
size_t ColumnAt(std::string_view text, size_t byte)
{
    return 1 + std::count_if(text.begin(), text.begin() + byte, [] (unsigned char c) { return (c & 0xc0) != 0x80; });
}

size_t DifferenceColumn(std::string_view actual, std::string_view expected)
{
    size_t byte = 0;
    while (byte < actual.size() && byte < expected.size() && actual[byte] == expected[byte]) {
        ++byte;
    }
    while (byte < actual.size() && std::isspace(static_cast<unsigned char>(actual[byte]))) {
        ++byte;
    }
    return ColumnAt(actual, byte);
}

std::string DescribeWhitespace(std::string_view gap)
{
    if (gap.empty()) {
        return "no whitespace";
    }
    std::string description;
    auto append = [&] (size_t count, std::string_view name) {
        if (!count) {
            return;
        }
        if (!description.empty()) {
            description += " and ";
        }
        description += count == 1 ? "one " : std::to_string(count) + " ";
        description += name;
        if (count != 1) {
            description += 's';
        }
    };
    append(std::count(gap.begin(), gap.end(), ' '), "space");
    append(std::count(gap.begin(), gap.end(), '\t'), "tab");
    // CR can appear at the end of a line split on LF.
    append(std::count(gap.begin(), gap.end(), '\r'), "carriage return");
    auto other = std::count_if(
        gap.begin(),
        gap.end(),
        [] (unsigned char c) {
            return c != ' ' && c != '\t' && c != '\r';
        });
    append(other, "other whitespace character");
    return description;
}

std::string TokenAt(std::string_view text, size_t offset)
{
    if (offset == text.size()) {
        return "end of line";
    }
    auto c = static_cast<unsigned char>(text[offset]);
    if (!std::isalnum(c) && c != '_' && c < 0x80) {
        return std::string(1, text[offset]);
    }
    size_t end = offset;
    while (end < text.size() && end - offset < 24) {
        auto next = static_cast<unsigned char>(text[end]);
        if (!std::isalnum(next) && next != '_' && next < 0x80) {
            break;
        }
        ++end;
        while (end < text.size() && (static_cast<unsigned char>(text[end]) & 0xc0) == 0x80) {
            ++end;
        }
    }
    return std::string(text.substr(offset, end - offset));
}

std::string SpacingMessage(
    std::string_view actual,
    std::string_view expected,
    size_t begin,
    size_t end,
    size_t expectedBegin,
    size_t expectedEnd)
{
    auto found = DescribeWhitespace(actual.substr(begin, end - begin));
    auto wanted = DescribeWhitespace(expected.substr(expectedBegin, expectedEnd - expectedBegin));
    if (begin == 0) {
        return "indentation: expected " + wanted + ", found " + found;
    }
    if (end == actual.size()) {
        return "trailing whitespace: expected " + wanted + ", found " + found;
    }
    return "expected " + wanted + " before '" + TokenAt(actual, end) + "', found " + found;
}

std::vector<Violation> SpacingViolations(size_t line, std::string_view actual, std::string_view expected)
{
    std::vector<Violation> violations;
    size_t a = 0, b = 0;
    while (a < actual.size() || b < expected.size()) {
        auto startA = a, startB = b;
        while (a < actual.size() && std::isspace(static_cast<unsigned char>(actual[a]))) {
            ++a;
        }
        while (b < expected.size() && std::isspace(static_cast<unsigned char>(expected[b]))) {
            ++b;
        }
        if (actual.substr(startA, a - startA) != expected.substr(startB, b - startB)) {
            violations.push_back({line, ColumnAt(actual, a), SpacingMessage(actual, expected, startA, a, startB, b)});
        }
        if (a == actual.size() || b == expected.size()) {
            break;
        }
        // Compare gaps after each byte (including punctuation), not just
        // after whole words: "f( int)" and "f(int)" differ inside a run.
        ++a;
        ++b;
    }
    return violations;
}

std::string QuoteSnippet(std::string_view text)
{
    std::string quoted = "\"";
    size_t i = 0;
    while (i < text.size() && i < 48) {
        auto start = i++;
        while (i < text.size() && (static_cast<unsigned char>(text[i]) & 0xc0) == 0x80) {
            ++i;
        }
        if (i - start != 1) {
            quoted += text.substr(start, i - start);
            continue;
        }
        switch (text[start]) {
            case '\t':
                quoted += "\\t";
                break;
            case '\r':
                quoted += "\\r";
                break;
            case '\"':
                quoted += "\\\"";
                break;
            case '\\':
                quoted += "\\\\";
                break;
            default:
                quoted += text[start];
                break;
        }
    }
    if (text.size() > i) {
        quoted += "...";
    }
    return quoted + '\"';
}

std::string ContentChange(std::string_view actual, std::string_view expected)
{
    size_t prefix = 0;
    while (prefix < actual.size() && prefix < expected.size() && actual[prefix] == expected[prefix]) {
        ++prefix;
    }
    while (prefix &&
        ((prefix < actual.size() && (static_cast<unsigned char>(actual[prefix]) & 0xc0) == 0x80) ||
            (prefix < expected.size() && (static_cast<unsigned char>(expected[prefix]) & 0xc0) == 0x80)))
    {
        --prefix;
    }
    size_t suffix = 0;
    while (suffix < actual.size() - prefix && suffix < expected.size() - prefix &&
        actual[actual.size() - suffix - 1] == expected[expected.size() - suffix - 1])
    {
        ++suffix;
    }
    while (suffix &&
        ((static_cast<unsigned char>(actual[actual.size() - suffix]) & 0xc0) == 0x80 ||
            (static_cast<unsigned char>(expected[expected.size() - suffix]) & 0xc0) == 0x80))
    {
        --suffix;
    }
    auto removed = actual.substr(prefix, actual.size() - prefix - suffix);
    auto added = expected.substr(prefix, expected.size() - prefix - suffix);
    if (removed.empty()) {
        return "insert " + QuoteSnippet(added) + " here";
    }
    if (added.empty()) {
        return "remove " + QuoteSnippet(removed) + " here";
    }
    return "replace " + QuoteSnippet(removed) + " with " + QuoteSnippet(added);
}

bool SameNonWhitespace(std::string_view a, std::string_view b)
{
    auto skip = [] (std::string_view text, size_t& i) {
        while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) {
            ++i;
        }
    };
    size_t i = 0, j = 0;
    for (;;) {
        skip(a, i);
        skip(b, j);
        if (i == a.size() || j == b.size()) {
            return i == a.size() && j == b.size();
        }
        if (a[i++] != b[j++]) {
            return false;
        }
    }
}

// Align the source and formatted token streams across changed line breaks.
// Only whitespace can be compared this way: for content edits, the line diff
// below describes the changed text instead.
std::optional<std::vector<Violation>> ReflowViolations(
    std::span<const Line> source,
    std::span<const Line> formatted,
    size_t firstLine)
{
    std::string original, replacement;
    std::vector<size_t> lineStarts;
    for (const auto& line : source) {
        lineStarts.push_back(original.size());
        original += line.Text;
        if (line.HasNewline) {
            original += '\n';
        }
    }
    for (const auto& line : formatted) {
        replacement += line.Text;
        if (line.HasNewline) {
            replacement += '\n';
        }
    }
    if (!SameNonWhitespace(original, replacement)) {
        return std::nullopt;
    }

    std::vector<Violation> violations;
    size_t a = 0, b = 0;
    while (a < original.size() || b < replacement.size()) {
        auto startA = a, startB = b;
        while (a < original.size() && std::isspace(static_cast<unsigned char>(original[a]))) {
            ++a;
        }
        while (b < replacement.size() && std::isspace(static_cast<unsigned char>(replacement[b]))) {
            ++b;
        }
        auto gapA = std::string_view(original).substr(startA, a - startA);
        auto gapB = std::string_view(replacement).substr(startB, b - startB);
        if (gapA != gapB) {
            auto lineIndex =
                static_cast<size_t>(std::upper_bound(lineStarts.begin(), lineStarts.end(), a) - lineStarts.begin() - 1);
            auto byte = std::min(a - lineStarts[lineIndex], source[lineIndex].Text.size());
            auto token = TokenAt(original, a);
            std::string message;
            auto oldBreaks = std::count(gapA.begin(), gapA.end(), '\n');
            auto newBreaks = std::count(gapB.begin(), gapB.end(), '\n');
            if (!oldBreaks && newBreaks) {
                message = "insert a line break before '" + token + "'";
                auto indent = gapB.substr(gapB.rfind('\n') + 1);
                if (!indent.empty()) {
                    message += " and indent by " + DescribeWhitespace(indent);
                }
            } else if (oldBreaks && !newBreaks) {
                message = "join lines before '" + token + "' with " + DescribeWhitespace(gapB);
            } else if (oldBreaks != newBreaks) {
                auto difference = oldBreaks > newBreaks ? oldBreaks - newBreaks : newBreaks - oldBreaks;
                message = (oldBreaks > newBreaks ? "remove " : "insert ") + std::to_string(difference) +
                    (difference == 1 ? " blank line" : " blank lines") + " before '" + token + "'";
            } else if (oldBreaks) {
                auto oldIndent = gapA.substr(gapA.rfind('\n') + 1);
                auto newIndent = gapB.substr(gapB.rfind('\n') + 1);
                message = "indent '" + token + "' with " + DescribeWhitespace(newIndent) + " instead of " +
                    DescribeWhitespace(oldIndent);
            } else {
                message = SpacingMessage(original, replacement, startA, a, startB, b);
            }
            violations.push_back({
                firstLine + lineIndex,
                ColumnAt(source[lineIndex].Text, byte),
                std::move(message),
                ViolationKind::Reflow,
            });
        }
        if (a == original.size() || b == replacement.size()) {
            break;
        }
        ++a;
        ++b;
    }
    return violations;
}

bool SameNonWhitespaceLines(std::span<const Line> source, std::span<const Line> formatted)
{
    std::string a, b;
    for (const auto& line : source) {
        a += line.Text;
    }
    for (const auto& line : formatted) {
        b += line.Text;
    }
    return SameNonWhitespace(a, b);
}

bool IsIncludeLine(const Line& line)
{
    auto begin = line.Text.find_first_not_of(" \t");
    if (begin == std::string_view::npos) {
        return false;
    }
    auto text = line.Text.substr(begin);
    return text.starts_with("#include") && text.size() > 8 &&
        (std::isspace(static_cast<unsigned char>(text[8])) || text[8] == '<' || text[8] == '"');
}

std::vector<Chunk> FormattingChunks(
    const std::vector<Line>& source,
    const std::vector<Line>& formatted,
    std::span<const hit_fmt::IncludeOrderChange> includeChanges)
{
    std::vector<Chunk> chunks;
    size_t sourceIndex = 0, formattedIndex = 0, includeIndex = 0;
    size_t sourceBoundary = source.size(), formattedBoundary = formatted.size();
    size_t sourceIncludeEnd = 0, formattedIncludeEnd = 0;
    auto lineAt = [] (const auto& lines, size_t offset) {
        return size_t(std::lower_bound(
            lines.begin(),
            lines.end(),
            offset,
            [] (const Line& line, size_t value) { return line.Offset < value; }) - lines.begin());
    };
    auto nextBoundary = [&] {
        if (includeIndex < includeChanges.size()) {
            const auto& change = includeChanges[includeIndex];
            sourceBoundary = lineAt(source, change.Original.Begin);
            sourceIncludeEnd = lineAt(source, change.Original.End);
            formattedBoundary = lineAt(formatted, change.Emitted.Begin);
            formattedIncludeEnd = lineAt(formatted, change.Emitted.End);
        } else {
            sourceBoundary = source.size();
            formattedBoundary = formatted.size();
        }
    };
    nextBoundary();
    while (sourceIndex < source.size() || formattedIndex < formatted.size()) {
        if (includeIndex < includeChanges.size() && sourceIndex == sourceBoundary &&
            formattedIndex == formattedBoundary)
        {
            // PrintFileIssues reports include sorting once. Line-diff anchors
            // must not split admitted permutations into per-header errors.
            sourceIndex = sourceIncludeEnd;
            formattedIndex = formattedIncludeEnd;
            ++includeIndex;
            nextBoundary();
            continue;
        }
        if (sourceIndex < sourceBoundary && formattedIndex < formattedBoundary &&
            source[sourceIndex] == formatted[formattedIndex])
        {
            ++sourceIndex;
            ++formattedIndex;
            continue;
        }
        size_t sourceCount = sourceIndex < sourceBoundary ? 1 : 0;
        size_t formattedCount = formattedIndex < formattedBoundary ? 1 : 0;
        // Sorting a run of includes must not look like a series of insertions
        // and deletions just because one moved line happens to match early.
        if (sourceCount && formattedCount && IsIncludeLine(source[sourceIndex]) &&
            IsIncludeLine(formatted[formattedIndex]))
        {
            while (sourceIndex + sourceCount < sourceBoundary && IsIncludeLine(source[sourceIndex + sourceCount])) {
                ++sourceCount;
            }
            while (formattedIndex + formattedCount < formattedBoundary &&
                IsIncludeLine(formatted[formattedIndex + formattedCount]))
            {
                ++formattedCount;
            }
        } else {
            size_t bestDistance = std::numeric_limits<size_t>::max();
            size_t bestSkew = std::numeric_limits<size_t>::max();
            bool bestPreservesTokens = false;
            auto sourceLimit = std::min(sourceBoundary, sourceIndex + DiffLookahead + 1);
            auto formattedLimit = std::min(formattedBoundary, formattedIndex + DiffLookahead + 1);
            for (auto nextSource = sourceIndex; nextSource < sourceLimit; ++nextSource) {
                for (auto nextFormatted = formattedIndex; nextFormatted < formattedLimit; ++nextFormatted) {
                    if (nextSource == sourceIndex && nextFormatted == formattedIndex) {
                        continue;
                    }
                    if (source[nextSource] != formatted[nextFormatted]) {
                        continue;
                    }
                    auto sourceSkip = nextSource - sourceIndex;
                    auto formattedSkip = nextFormatted - formattedIndex;
                    auto distance = sourceSkip + formattedSkip;
                    auto skew = sourceSkip > formattedSkip ? sourceSkip - formattedSkip : formattedSkip - sourceSkip;
                    // An unchanged line inside a repeated block can look
                    // closer than the actual end of a whitespace-only edit.
                    // Prefer an anchor that keeps all intervening tokens.
                    bool preservesTokens = sourceSkip && formattedSkip &&
                        SameNonWhitespaceLines(
                            std::span(source).subspan(sourceIndex, sourceSkip),
                            std::span(formatted).subspan(formattedIndex, formattedSkip));
                    if ((preservesTokens && !bestPreservesTokens) ||
                        (preservesTokens == bestPreservesTokens &&
                            (distance < bestDistance || (distance == bestDistance && skew < bestSkew))))
                    {
                        bestPreservesTokens = preservesTokens;
                        bestDistance = distance;
                        bestSkew = skew;
                        sourceCount = sourceSkip;
                        formattedCount = formattedSkip;
                    }
                }
            }
            if (bestDistance == std::numeric_limits<size_t>::max()) {
                sourceCount = sourceBoundary - sourceIndex;
                formattedCount = formattedBoundary - formattedIndex;
            }
        }

        auto begin = std::min(sourceIndex + 1, std::max(size_t{1}, source.size()));
        Chunk chunk{
            .Begin = begin,
            .End = std::max(begin + 1, sourceIndex + sourceCount + 1),
            .SourceBegin = sourceIndex,
            .SourceEnd = sourceIndex + sourceCount,
            .FormattedBegin = formattedIndex,
            .FormattedEnd = formattedIndex + formattedCount,
            .HasFix = true,
        };
        bool alignedReflow = false;
        if (sourceCount && formattedCount && (sourceCount != formattedCount || sourceCount > 1)) {
            auto reflow = ReflowViolations(
                std::span(source).subspan(sourceIndex, sourceCount),
                std::span(formatted).subspan(formattedIndex, formattedCount),
                sourceIndex + 1);
            if (reflow && !reflow->empty()) {
                chunk.Violations = std::move(*reflow);
                alignedReflow = true;
            }
        }
        for (size_t i = 0; i < sourceCount && !alignedReflow; ++i) {
            const auto& line = source[sourceIndex + i];
            auto replacement = i < formattedCount ? formatted[formattedIndex + i].Text : std::string_view{};
            if (i < formattedCount && line == formatted[formattedIndex + i]) {
                continue;
            }
            std::string message;
            auto kind = ViolationKind::Other;
            auto column = DifferenceColumn(line.Text, replacement);
            if (line.Text == replacement && !line.HasNewline && i < formattedCount &&
                formatted[formattedIndex + i].HasNewline)
            {
                message = "missing final newline";
            } else if (line.Text == replacement && line.HasNewline && i < formattedCount &&
                !formatted[formattedIndex + i].HasNewline)
            {
                message = "unexpected final newline";
            } else if (sourceCount == formattedCount && i < formattedCount && IsIncludeLine(line) &&
                IsIncludeLine(formatted[formattedIndex + i]))
            {
                message = "include order: expected " + QuoteSnippet(replacement) + " on this line";
            } else if (sourceCount != formattedCount) {
                if (formattedCount == 0) {
                    message = "remove this line";
                } else if (i >= formattedCount) {
                    message = "merge this line into the preceding formatted line";
                } else if (formattedCount > sourceCount) {
                    message = "split line: expected " + QuoteSnippet(replacement) + " on this line";
                    kind = ViolationKind::Reflow;
                } else {
                    message = "join lines: expected " + QuoteSnippet(replacement) + " on this line";
                    kind = ViolationKind::Reflow;
                }
            } else if (SameNonWhitespace(line.Text, replacement)) {
                auto spacing = SpacingViolations(sourceIndex + i + 1, line.Text, replacement);
                chunk.Violations.insert(chunk.Violations.end(), spacing.begin(), spacing.end());
                continue;
            } else {
                message = ContentChange(line.Text, replacement);
            }
            chunk.Violations.push_back({sourceIndex + i + 1, column, std::move(message), kind});
        }
        if (sourceCount == 0) {
            auto column = sourceIndex < source.size() || source.empty()
                ? size_t{1}
                : ColumnAt(source.back().Text, source.back().Text.size());
            auto location = sourceIndex == source.size() ? " at end of file" : " before this line";
            auto message = formattedCount == 1
                ? "insert " + QuoteSnippet(formatted[formattedIndex].Text) + location
                : "insert " + std::to_string(formattedCount) + " lines" + location + ", starting with " +
                    QuoteSnippet(formatted[formattedIndex].Text);
            chunk.Violations.push_back({begin, column, std::move(message)});
        }
        if (chunk.Violations.empty()) {
            // A matched prefix can be followed by inserted output lines.
            auto number = sourceIndex + sourceCount;
            auto column = ColumnAt(source[number - 1].Text, source[number - 1].Text.size());
            auto inserted = formattedCount > sourceCount ? formattedCount - sourceCount : size_t{0};
            chunk.Violations.push_back({
                number,
                column,
                inserted
                    ? "insert " + std::to_string(inserted) + " formatted lines after this line"
                    : "replace lines following this line with " "formatted output",
            });
        }
        for (size_t i = 0; i < formattedCount; ++i) {
            chunk.Fix.push_back(formatted[formattedIndex + i]);
        }
        chunks.push_back(std::move(chunk));
        sourceIndex += sourceCount;
        formattedIndex += formattedCount;
    }
    return chunks;
}

} // namespace

void PrintFormattingDiagnostics(
    std::ostream& output,
    std::string_view path,
    std::string_view source,
    std::string_view formatted,
    const Terminal& terminal,
    bool fixed,
    std::span<const hit_fmt::IncludeOrderChange> includeChanges)
{
    const auto sourceLines = SplitLines(source);
    auto chunks = FormattingChunks(sourceLines, SplitLines(formatted), includeChanges);
    for (auto& chunk : chunks) {
        std::ranges::stable_sort(
            chunk.Violations,
            [] (const Violation& a, const Violation& b) {
                return a.Line < b.Line || (a.Line == b.Line && a.Column < b.Column);
            });
        const auto& first = chunk.Violations.front();
        output << (fixed ? terminal.BoldGreen : terminal.BoldRed);
        output << (fixed ? "fixed: " : "fail: ") << chunk.Violations.size() << " violation"
            << (chunk.Violations.size() == 1 ? "" : "s");
        output << terminal.Reset;
        auto location = std::string(path) + ':' + std::to_string(first.Line) + ':' + std::to_string(first.Column);
        if (fixed) {
            location += " [original source; before formatting]";
        }
        if (location.size() + 6 <= terminal.Width) {
            output << "\n  --> " << location << '\n';
        } else {
            output << "\n  -->\n    ";
            PrintWrappedMessage(output, location, "    ", terminal.Width);
        }
        const auto width = std::to_string(chunk.End - 1).size();
        const auto gutter = std::string(width + 1, ' ') + "| ";
        output << gutter.substr(0, gutter.size() - 1) << '\n';
        size_t violationIndex = 0;
        for (auto lineNumber = chunk.Begin; lineNumber < chunk.End; ++lineNumber) {
            auto text = lineNumber <= sourceLines.size() ? sourceLines[lineNumber - 1].Text : std::string_view{};
            if (text.ends_with('\r')) {
                text.remove_suffix(1);
            }
            const auto prefix =
                std::string(width - std::to_string(lineNumber).size(), ' ') + std::to_string(lineNumber) + " | ";
            auto printLine = [&] (size_t column, bool fromStart = false) {
                output << prefix;
                return PrintSourceExcerpt(output, text, column, prefix.size(), terminal.Width, fromStart);
            };
            if (violationIndex == chunk.Violations.size() || chunk.Violations[violationIndex].Line != lineNumber) {
                printLine(0);
            }
            while (violationIndex < chunk.Violations.size() && chunk.Violations[violationIndex].Line == lineNumber) {
                const auto& violation = chunk.Violations[violationIndex++];
                auto excerpt = printLine(violation.Column);
                output << gutter;
                if (!excerpt.CaretVisible) {
                    // Do not imply that the cropped end of a long line is
                    // the exact token at ColumnLimit + 1.
                    PrintWrappedMessage(output, violation.Message, gutter, terminal.Width);
                    continue;
                }
                output << std::string(excerpt.CaretOffset, ' ') << '^';
                if (gutter.size() + excerpt.CaretOffset + 2 + violation.Message.size() < terminal.Width) {
                    output << ' ' << violation.Message << '\n';
                } else {
                    output << '\n' << gutter;
                    PrintWrappedMessage(output, violation.Message, gutter, terminal.Width);
                }
            }
        }
        if (chunk.HasFix) {
            output << (fixed ? "  applied formatting:\n" : "  suggested formatting:\n");
            const auto& fix = chunk.Fix;
            for (const auto& line : fix) {
                PrintSuggestedLine(output, line.Text, gutter.size(), terminal);
                if (!line.HasNewline) {
                    output << std::string(gutter.size(), ' ') << "\\ No newline at end of file\n";
                }
            }
            if (fix.empty()) {
                output << "  (remove this chunk)\n";
            }
        }
    }
}

} // namespace hit_fmt
