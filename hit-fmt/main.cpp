#include <formatter/config.hpp>
#include <formatter/formatter.hpp>
#include <formatter/naming.hpp>
#include <formatter/parameter_names.hpp>
#include <util/file.hpp>

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <limits>
#include <optional>
#include <regex>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#include <io.h>
#else
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace {

constexpr size_t MaxSourceFileSize = 4 * 1024 * 1024;
constexpr size_t DiffLookahead = 16;

constexpr std::string_view ResetColor = "\033[0m";
constexpr std::string_view BoldRed = "\033[1;31m";
constexpr std::string_view BoldGreen = "\033[1;32m";
constexpr std::string_view Green = "\033[32m";

enum class Mode
{
    Check,
    Fix,
};

struct Line
{
    std::string_view Text;
    bool HasNewline;

    bool operator==(const Line&) const = default;
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
        lines.push_back(
            {.Text = text.substr(begin, end - begin),
             .HasNewline = hasNewline});
        begin = hasNewline ? end + 1 : text.size();
    }
    return lines;
}

size_t TerminalWidth()
{
#ifndef _WIN32
    if (winsize size{}; isatty(fileno(stdout)) &&
        ioctl(fileno(stdout), TIOCGWINSZ, &size) == 0 && size.ws_col > 0)
    {
        return std::max<size_t>(24, size.ws_col);
    }
#endif
    if (const char* value = std::getenv("COLUMNS")) {
        size_t width = 0;
        auto end = value + std::char_traits<char>::length(value);
        auto [next, error] = std::from_chars(value, end, width);
        if (error == std::errc{} && next == end && width >= 24 &&
            width <= 1000) return width;
    }
    return 80;
}

// Tokenizer columns count Unicode code points. Render a display cell per
// code point, expanding tabs so both the excerpt and caret use cell offsets.
struct RenderedLine
{
    std::vector<std::string_view> Cells;
    size_t Caret = 0;
};

RenderedLine RenderLine(
    std::string_view text, size_t column, size_t prefixWidth)
{
    RenderedLine rendered;
    size_t codePoint = 1;
    for (size_t i = 0; i < text.size(); ++codePoint) {
        if (codePoint == column) rendered.Caret = rendered.Cells.size();
        auto start = i++;
        while (i < text.size() &&
            (static_cast<unsigned char>(text[i]) & 0xc0) == 0x80) ++i;
        if (text[start] == '\t') {
            auto spaces = 8 - (prefixWidth + rendered.Cells.size()) % 8;
            while (spaces--) rendered.Cells.push_back(" ");
        } else if (text[start] == '\r') {
            // CRLF lines are trimmed before reaching this function.
            continue;
        } else {
            rendered.Cells.push_back(text.substr(start, i - start));
        }
    }
    if (column >= codePoint) rendered.Caret = rendered.Cells.size();
    return rendered;
}

void PrintCells(const std::vector<std::string_view>& cells,
    size_t begin, size_t end)
{
    for (auto i = begin; i < end; ++i) std::cout << cells[i];
}

struct Excerpt
{
    size_t CaretOffset;
    bool CaretVisible;
};

Excerpt PrintSourceExcerpt(std::string_view text, size_t column,
    size_t prefixWidth, size_t terminalWidth, bool fromStart = false)
{
    auto rendered = RenderLine(text, column, prefixWidth);
    const auto& cells = rendered.Cells;
    auto available = terminalWidth > prefixWidth + 1
        ? terminalWidth - prefixWidth - 1 : size_t{16};
    auto window = available > 6 ? available - 6 : size_t{1};
    size_t begin = 0;
    if (cells.size() > available && !fromStart) {
        begin = std::min(cells.size() - window,
            rendered.Caret > window / 2 ? rendered.Caret - window / 2 : 0);
    }
    auto count = cells.size() > available
        ? (fromStart ? available - 3 : window) : available;
    auto end = std::min(cells.size(), begin + count);
    if (begin) std::cout << "...";
    PrintCells(cells, begin, end);
    if (end < cells.size()) std::cout << "...";
    std::cout << '\n';
    return {.CaretOffset = (begin ? 3 : 0) +
        std::min(rendered.Caret, end) - begin,
        .CaretVisible = rendered.Caret < end ||
            (rendered.Caret == end && end == cells.size())};
}

void PrintWrappedMessage(std::string_view message,
    std::string_view prefix, size_t terminalWidth)
{
    auto available = terminalWidth > prefix.size() + 2
        ? terminalWidth - prefix.size() - 2 : size_t{20};
    while (!message.empty()) {
        auto length = std::min(available, message.size());
        if (length < message.size()) {
            auto space = message.rfind(' ', length);
            if (space != std::string_view::npos && space > 0) length = space;
        }
        while (length && length < message.size() &&
            (static_cast<unsigned char>(message[length]) & 0xc0) == 0x80)
        {
            --length;
        }
        std::cout << "  " << message.substr(0, length) << '\n';
        message.remove_prefix(length);
        while (message.starts_with(' ')) message.remove_prefix(1);
        if (!message.empty()) std::cout << prefix;
    }
}

void PrintSuggestedLine(std::string_view text, size_t terminalWidth,
    size_t gutterWidth, bool useColor)
{
    auto cells = RenderLine(text, 0, gutterWidth).Cells;
    auto available = terminalWidth > gutterWidth + 1
        ? terminalWidth - gutterWidth - 1 : size_t{16};
    for (size_t begin = 0;;) {
        if (useColor) std::cout << Green;
        if (begin) {
            std::cout << std::string(gutterWidth, ' ');
        } else {
            std::cout << std::string(gutterWidth - 3, ' ') << " + ";
        }
        auto end = std::min(cells.size(), begin + available);
        PrintCells(cells, begin, end);
        if (useColor) std::cout << ResetColor;
        std::cout << '\n';
        if (end == cells.size()) break;
        begin = end;
    }
}

bool UseTerminalColors()
{
    if (std::getenv("NO_COLOR") != nullptr) {
        return false;
    }
    if (auto* term = std::getenv("TERM");
        term && std::string_view(term) == "dumb")
    {
        return false;
    }
#ifdef _WIN32
    return _isatty(_fileno(stdout)) != 0;
#else
    return isatty(fileno(stdout)) != 0;
#endif
}

enum class ViolationKind { Other, Reflow, LineWidth };

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
    std::vector<Line> LayoutFix;
    size_t FormattedBegin = 0;
    size_t FormattedEnd = 0;
    size_t LayoutFormattedBegin = 0;
    size_t LayoutFormattedEnd = 0;
    bool HasFix = false;
    bool HasLayoutFix = false;
    bool HasLayoutIssue = false;
    bool HasWidthIssue = false;
};

// The first differing code point is normally the token at which formatting
// diverges. For removed whitespace, point to the following token instead.
size_t ColumnAt(std::string_view text, size_t byte)
{
    return 1 + std::count_if(text.begin(), text.begin() + byte,
        [] (unsigned char c) { return (c & 0xc0) != 0x80; });
}

size_t DifferenceColumn(std::string_view actual, std::string_view expected)
{
    size_t byte = 0;
    while (byte < actual.size() && byte < expected.size() &&
        actual[byte] == expected[byte]) ++byte;
    while (byte < actual.size() &&
        std::isspace(static_cast<unsigned char>(actual[byte]))) ++byte;
    return ColumnAt(actual, byte);
}

std::string DescribeWhitespace(std::string_view gap)
{
    if (gap.empty()) return "no whitespace";
    std::string description;
    auto append = [&] (size_t count, std::string_view name) {
        if (!count) return;
        if (!description.empty()) description += " and ";
        description += count == 1 ? "one " : std::to_string(count) + " ";
        description += name;
        if (count != 1) description += 's';
    };
    append(std::count(gap.begin(), gap.end(), ' '), "space");
    append(std::count(gap.begin(), gap.end(), '\t'), "tab");
    // CR can appear at the end of a line split on LF.
    append(std::count(gap.begin(), gap.end(), '\r'), "carriage return");
    auto other = std::count_if(gap.begin(), gap.end(), [] (unsigned char c) {
        return c != ' ' && c != '\t' && c != '\r';
    });
    append(other, "other whitespace character");
    return description;
}

std::string TokenAt(std::string_view text, size_t offset)
{
    if (offset == text.size()) return "end of line";
    auto c = static_cast<unsigned char>(text[offset]);
    if (!std::isalnum(c) && c != '_' && c < 0x80) {
        return std::string(1, text[offset]);
    }
    size_t end = offset;
    while (end < text.size() && end - offset < 24) {
        auto next = static_cast<unsigned char>(text[end]);
        if (!std::isalnum(next) && next != '_' && next < 0x80) break;
        ++end;
        while (end < text.size() &&
            (static_cast<unsigned char>(text[end]) & 0xc0) == 0x80) ++end;
    }
    return std::string(text.substr(offset, end - offset));
}

std::string SpacingMessage(std::string_view actual, std::string_view expected,
    size_t begin, size_t end, size_t expectedBegin, size_t expectedEnd)
{
    auto found = DescribeWhitespace(actual.substr(begin, end - begin));
    auto wanted = DescribeWhitespace(
        expected.substr(expectedBegin, expectedEnd - expectedBegin));
    if (begin == 0) {
        return "indentation: expected " + wanted + ", found " + found;
    }
    if (end == actual.size()) {
        return "trailing whitespace: expected " + wanted + ", found " + found;
    }
    return "expected " + wanted + " before '" + TokenAt(actual, end) +
        "', found " + found;
}

std::vector<Violation> SpacingViolations(
    size_t line, std::string_view actual, std::string_view expected)
{
    std::vector<Violation> violations;
    size_t a = 0, b = 0;
    while (a < actual.size() || b < expected.size()) {
        auto startA = a, startB = b;
        while (a < actual.size() &&
            std::isspace(static_cast<unsigned char>(actual[a]))) ++a;
        while (b < expected.size() &&
            std::isspace(static_cast<unsigned char>(expected[b]))) ++b;
        if (actual.substr(startA, a - startA) !=
            expected.substr(startB, b - startB))
        {
            violations.push_back({line, ColumnAt(actual, a),
                SpacingMessage(actual, expected, startA, a, startB, b)});
        }
        if (a == actual.size() || b == expected.size()) break;
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
        while (i < text.size() &&
            (static_cast<unsigned char>(text[i]) & 0xc0) == 0x80) ++i;
        if (i - start != 1) {
            quoted += text.substr(start, i - start);
            continue;
        }
        switch (text[start]) {
        case '\t': quoted += "\\t"; break;
        case '\r': quoted += "\\r"; break;
        case '\"': quoted += "\\\""; break;
        case '\\': quoted += "\\\\"; break;
        default: quoted += text[start]; break;
        }
    }
    if (text.size() > i) quoted += "...";
    return quoted + '\"';
}

std::string ContentChange(std::string_view actual, std::string_view expected)
{
    size_t prefix = 0;
    while (prefix < actual.size() && prefix < expected.size() &&
        actual[prefix] == expected[prefix]) ++prefix;
    while (prefix && ((prefix < actual.size() &&
        (static_cast<unsigned char>(actual[prefix]) & 0xc0) == 0x80) ||
        (prefix < expected.size() &&
            (static_cast<unsigned char>(expected[prefix]) & 0xc0) == 0x80)))
    {
        --prefix;
    }
    size_t suffix = 0;
    while (suffix < actual.size() - prefix &&
        suffix < expected.size() - prefix &&
        actual[actual.size() - suffix - 1] ==
            expected[expected.size() - suffix - 1]) ++suffix;
    while (suffix && ((static_cast<unsigned char>(
        actual[actual.size() - suffix]) & 0xc0) == 0x80 ||
        (static_cast<unsigned char>(expected[expected.size() - suffix]) &
            0xc0) == 0x80))
    {
        --suffix;
    }
    auto removed = actual.substr(prefix, actual.size() - prefix - suffix);
    auto added = expected.substr(prefix, expected.size() - prefix - suffix);
    if (removed.empty()) return "insert " + QuoteSnippet(added) + " here";
    if (added.empty()) return "remove " + QuoteSnippet(removed) + " here";
    return "replace " + QuoteSnippet(removed) + " with " +
        QuoteSnippet(added);
}

bool SameNonWhitespace(std::string_view a, std::string_view b)
{
    auto skip = [] (std::string_view text, size_t& i) {
        while (i < text.size() &&
            std::isspace(static_cast<unsigned char>(text[i]))) ++i;
    };
    size_t i = 0, j = 0;
    for (;;) {
        skip(a, i);
        skip(b, j);
        if (i == a.size() || j == b.size()) {
            return i == a.size() && j == b.size();
        }
        if (a[i++] != b[j++]) return false;
    }
}

// Align the source and formatted token streams across changed line breaks.
// Only whitespace can be compared this way: for content edits, the line diff
// below describes the changed text instead.
std::optional<std::vector<Violation>> ReflowViolations(
    std::span<const Line> source, std::span<const Line> formatted,
    size_t firstLine)
{
    std::string original, replacement;
    std::vector<size_t> lineStarts;
    for (const auto& line : source) {
        lineStarts.push_back(original.size());
        original += line.Text;
        if (line.HasNewline) original += '\n';
    }
    for (const auto& line : formatted) {
        replacement += line.Text;
        if (line.HasNewline) replacement += '\n';
    }
    if (!SameNonWhitespace(original, replacement)) return std::nullopt;

    std::vector<Violation> violations;
    size_t a = 0, b = 0;
    while (a < original.size() || b < replacement.size()) {
        auto startA = a, startB = b;
        while (a < original.size() &&
            std::isspace(static_cast<unsigned char>(original[a]))) ++a;
        while (b < replacement.size() &&
            std::isspace(static_cast<unsigned char>(replacement[b]))) ++b;
        auto gapA = std::string_view(original).substr(startA, a - startA);
        auto gapB = std::string_view(replacement).substr(startB, b - startB);
        if (gapA != gapB) {
            auto lineIndex = static_cast<size_t>(
                std::upper_bound(lineStarts.begin(), lineStarts.end(), a) -
                lineStarts.begin() - 1);
            auto byte = std::min(a - lineStarts[lineIndex],
                source[lineIndex].Text.size());
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
                message = "join lines before '" + token + "' with " +
                    DescribeWhitespace(gapB);
            } else if (oldBreaks != newBreaks) {
                auto difference = oldBreaks > newBreaks
                    ? oldBreaks - newBreaks : newBreaks - oldBreaks;
                message = (oldBreaks > newBreaks ? "remove " : "insert ") +
                    std::to_string(difference) +
                    (difference == 1 ? " blank line" : " blank lines") +
                    " before '" + token + "'";
            } else if (oldBreaks) {
                auto oldIndent = gapA.substr(gapA.rfind('\n') + 1);
                auto newIndent = gapB.substr(gapB.rfind('\n') + 1);
                message = "indent '" + token + "' with " +
                    DescribeWhitespace(newIndent) + " instead of " +
                    DescribeWhitespace(oldIndent);
            } else {
                message = SpacingMessage(original, replacement,
                    startA, a, startB, b);
            }
            violations.push_back({firstLine + lineIndex,
                ColumnAt(source[lineIndex].Text, byte), std::move(message),
                ViolationKind::Reflow});
        }
        if (a == original.size() || b == replacement.size()) break;
        ++a;
        ++b;
    }
    return violations;
}

bool IsIncludeLine(const Line& line)
{
    auto begin = line.Text.find_first_not_of(" \t");
    if (begin == std::string_view::npos) return false;
    auto text = line.Text.substr(begin);
    return text.starts_with("#include") && text.size() > 8 &&
        (std::isspace(static_cast<unsigned char>(text[8])) ||
            text[8] == '<' || text[8] == '"');
}

std::vector<Chunk> FormattingChunks(
    const std::vector<Line>& source, const std::vector<Line>& formatted)
{
    std::vector<Chunk> chunks;
    size_t sourceIndex = 0, formattedIndex = 0;
    while (sourceIndex < source.size() || formattedIndex < formatted.size()) {
        if (sourceIndex < source.size() && formattedIndex < formatted.size() &&
            source[sourceIndex] == formatted[formattedIndex])
        {
            ++sourceIndex;
            ++formattedIndex;
            continue;
        }
        size_t sourceCount = sourceIndex < source.size() ? 1 : 0;
        size_t formattedCount = formattedIndex < formatted.size() ? 1 : 0;
        // Sorting a run of includes must not look like a series of insertions
        // and deletions just because one moved line happens to match early.
        if (sourceCount && formattedCount &&
            IsIncludeLine(source[sourceIndex]) &&
            IsIncludeLine(formatted[formattedIndex]))
        {
            while (sourceIndex + sourceCount < source.size() &&
                IsIncludeLine(source[sourceIndex + sourceCount])) ++sourceCount;
            while (formattedIndex + formattedCount < formatted.size() &&
                IsIncludeLine(formatted[formattedIndex + formattedCount]))
            {
                ++formattedCount;
            }
        } else {
            size_t bestDistance = std::numeric_limits<size_t>::max();
            auto sourceLimit =
                std::min(source.size(), sourceIndex + DiffLookahead + 1);
            auto formattedLimit =
                std::min(formatted.size(), formattedIndex + DiffLookahead + 1);
            for (auto nextSource = sourceIndex; nextSource < sourceLimit;
                ++nextSource)
            {
                for (auto nextFormatted = formattedIndex;
                    nextFormatted < formattedLimit; ++nextFormatted)
                {
                    if (nextSource == sourceIndex &&
                        nextFormatted == formattedIndex) continue;
                    if (source[nextSource] != formatted[nextFormatted]) continue;
                    auto distance = nextSource - sourceIndex +
                        nextFormatted - formattedIndex;
                    if (distance < bestDistance) {
                        bestDistance = distance;
                        sourceCount = nextSource - sourceIndex;
                        formattedCount = nextFormatted - formattedIndex;
                    }
                }
            }
            if (bestDistance == std::numeric_limits<size_t>::max()) {
                sourceCount = source.size() - sourceIndex;
                formattedCount = formatted.size() - formattedIndex;
            }
        }

        auto begin = std::min(sourceIndex + 1, std::max(size_t{1}, source.size()));
        Chunk chunk{.Begin = begin,
            .End = std::max(begin + 1, sourceIndex + sourceCount + 1),
            .FormattedBegin = formattedIndex,
            .FormattedEnd = formattedIndex + formattedCount,
            .HasFix = true};
        bool alignedReflow = false;
        if (sourceCount && formattedCount &&
            (sourceCount != formattedCount || sourceCount > 1))
        {
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
            auto replacement = i < formattedCount
                ? formatted[formattedIndex + i].Text : std::string_view{};
            if (i < formattedCount && line == formatted[formattedIndex + i]) {
                continue;
            }
            std::string message;
            auto kind = ViolationKind::Other;
            if (line.Text == replacement && !line.HasNewline &&
                i < formattedCount && formatted[formattedIndex + i].HasNewline)
            {
                message = "missing final newline";
            } else if (line.Text == replacement && line.HasNewline &&
                i < formattedCount && !formatted[formattedIndex + i].HasNewline)
            {
                message = "unexpected final newline";
            } else if (sourceCount == formattedCount &&
                i < formattedCount && IsIncludeLine(line) &&
                IsIncludeLine(formatted[formattedIndex + i]))
            {
                message = "include order: expected " +
                    QuoteSnippet(replacement) + " on this line";
            } else if (sourceCount != formattedCount) {
                if (formattedCount == 0) {
                    message = "remove this line";
                } else if (i >= formattedCount) {
                    message = "merge this line into the preceding formatted line";
                } else if (formattedCount > sourceCount) {
                    message = "split line: expected " + QuoteSnippet(replacement) +
                        " on this line";
                    kind = ViolationKind::Reflow;
                } else {
                    message = "join lines: expected " + QuoteSnippet(replacement) +
                        " on this line";
                    kind = ViolationKind::Reflow;
                }
            } else if (SameNonWhitespace(line.Text, replacement)) {
                auto spacing = SpacingViolations(
                    sourceIndex + i + 1, line.Text, replacement);
                chunk.Violations.insert(chunk.Violations.end(),
                    spacing.begin(), spacing.end());
                continue;
            } else {
                message = ContentChange(line.Text, replacement);
            }
            chunk.Violations.push_back({sourceIndex + i + 1,
                DifferenceColumn(line.Text, replacement), std::move(message),
                kind});
        }
        if (sourceCount == 0) {
            auto column = sourceIndex < source.size() || source.empty()
                ? size_t{1}
                : ColumnAt(source.back().Text, source.back().Text.size());
            auto location = sourceIndex == source.size()
                ? " at end of file" : " before this line";
            auto message = formattedCount == 1
                ? "insert " + QuoteSnippet(formatted[formattedIndex].Text) +
                    location
                : "insert " + std::to_string(formattedCount) +
                    " lines" + location + ", starting with " +
                    QuoteSnippet(formatted[formattedIndex].Text);
            chunk.Violations.push_back({begin, column, std::move(message)});
        }
        if (chunk.Violations.empty()) {
            // A matched prefix can be followed by inserted output lines.
            auto number = sourceIndex + sourceCount;
            auto column = ColumnAt(source[number - 1].Text,
                source[number - 1].Text.size());
            auto inserted = formattedCount > sourceCount
                ? formattedCount - sourceCount : size_t{0};
            chunk.Violations.push_back({number, column,
                inserted ? "insert " + std::to_string(inserted) +
                    " formatted lines after this line" :
                    "replace lines following this line with formatted output"});
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

// Adjacent ordinary C++ string literals concatenate without changing their
// value. Offer a conservative check-only repair when the formatter cannot
// reflow a line dominated by one long literal (for example a log message).
std::vector<std::string> SuggestStringSplit(const parser::SyntaxTree& tree,
    std::string_view line, size_t lineNumber, size_t limit,
    size_t indentWidth)
{
    if (line.ends_with('\r') || line.contains('\\') || limit < 20) return {};
    auto sourceOffset = static_cast<size_t>(line.data() - tree.Source().data());
    auto leading = line.find_first_not_of(' ');
    if (leading == std::string_view::npos) return {};
    auto continuation = std::string(leading + indentWidth, ' ');
    size_t index = 0, upper = tree.TokenCount();
    while (index < upper) {
        auto middle = index + (upper - index) / 2;
        if (tree.TokenPosition(middle).Line < lineNumber) {
            index = middle + 1;
        } else {
            upper = middle;
        }
    }
    for (; index < tree.TokenCount() &&
        tree.TokenPosition(index).Line == lineNumber; ++index)
    {
        if (tree.Kind(index) != tokenizer::TokenKind::Literal_String) continue;
        auto literal = tree.TokenText(index);
        if (literal.size() < 4 || literal.front() != '"' ||
            literal.back() != '"' || literal.contains('\\') ||
            literal.contains('\n')) continue;
        auto begin = static_cast<size_t>(tree.Token(index).Text.Begin) -
            sourceOffset;
        if (begin > line.size() || literal.size() > line.size() - begin) {
            continue;
        }
        auto prefix = line.substr(0, begin);
        auto suffix = line.substr(begin + literal.size());
        auto content = literal.substr(1, literal.size() - 2);
        if (prefix.size() + 2 >= limit ||
            continuation.size() + 2 + suffix.size() >= limit)
        {
            continue;
        }
        std::vector<std::string> suggestion;
        auto remaining = content;
        auto head = prefix;
        while (head.size() + remaining.size() + 2 + suffix.size() > limit) {
            auto capacity = limit - head.size() - 2;
            auto space = remaining.rfind(' ', capacity - 1);
            if (space == std::string_view::npos || !space ||
                suggestion.size() >= 12) break;
            auto split = space + 1; // Keep the original space in the literal.
            suggestion.push_back(std::string(head) + '"' +
                std::string(remaining.substr(0, split)) + '"');
            remaining.remove_prefix(split);
            head = continuation;
        }
        if (!suggestion.empty() &&
            head.size() + remaining.size() + 2 + suffix.size() <= limit)
        {
            suggestion.push_back(std::string(head) + '"' +
                std::string(remaining) + '"' + std::string(suffix));
            return suggestion;
        }
    }
    return {};
}

void PrintCheckDiagnostics(
    const parser::SyntaxTree& tree,
    const formatter::Style& style,
    std::string_view path, std::string_view source, std::string_view formatted,
    std::string_view layoutFormatted,
    std::span<const formatter::NameDiagnostic> names,
    std::span<const formatter::ParameterNameDiagnostic> parameters,
    std::span<const formatter::LayoutDiagnostic> layout,
    bool useColor)
{
    auto sourceLines = SplitLines(source);
    auto formattedLines = SplitLines(formatted);
    auto chunks = FormattingChunks(sourceLines, formattedLines);
    // Source layout diagnostics own their line. A formatter-emitted break on
    // that line is a possible repair, not another source violation. Preserve
    // unrelated formatting, naming and parameter-name diagnostics.
    for (auto& chunk : chunks) {
        std::erase_if(chunk.Violations, [&] (const Violation& violation) {
            return violation.Kind == ViolationKind::Reflow &&
                std::ranges::any_of(layout, [&] (const auto& issue) {
                    return issue.Position.Line == violation.Line;
                });
        });
    }
    const auto terminalWidth = TerminalWidth();
    for (const auto& name : names) {
        chunks.push_back({.Begin = name.Position.Line,
            .End = name.Position.Line + 1,
            .Violations = {{name.Position.Line, name.Position.Column,
                name.Message + "; " + name.Expected}}});
    }
    for (const auto& parameter : parameters) {
        auto message = "parameter #" + std::to_string(parameter.ParameterNumber) +
            " in " + parameter.Function + ": expected a name";
        if (parameter.AllowCommentedName) message += " or /*name*/ comment";
        chunks.push_back({.Begin = parameter.SignatureBeginLine,
            .End = parameter.SignatureEndLine + 1,
            .Violations = {{parameter.Position.Line, parameter.Position.Column,
                std::move(message)}}});
    }
    for (const auto& issue : layout) {
        chunks.push_back({.Begin = issue.Position.Line,
            .End = issue.Position.Line + 1,
            .Violations = {{issue.Position.Line, issue.Position.Column,
                issue.Message, issue.Kind ==
                    formatter::LayoutDiagnosticKind::LineWidth
                    ? ViolationKind::LineWidth : ViolationKind::Other}},
            .HasLayoutIssue = true,
            .HasWidthIssue = issue.Kind ==
                formatter::LayoutDiagnosticKind::LineWidth});
    }
    // Check mode does not reflow expressions to decide whether a file fails.
    // Use normal formatter output only as an optional suggestion for layout
    // diagnostics whose source lines actually change under reflow.
    auto layoutLines = SplitLines(layoutFormatted);
    if (!layout.empty()) {
        for (auto& candidate : FormattingChunks(sourceLines, layoutLines)) {
            if (std::ranges::none_of(layout, [&] (const auto& issue) {
                return issue.Position.Line >= candidate.Begin &&
                    issue.Position.Line < candidate.End;
            })) continue;
            Chunk suggestion{.Begin = candidate.Begin, .End = candidate.End,
                .LayoutFix = std::move(candidate.Fix),
                .LayoutFormattedBegin = candidate.FormattedBegin,
                .LayoutFormattedEnd = candidate.FormattedEnd,
                .HasLayoutFix = true};
            chunks.push_back(std::move(suggestion));
        }
    }
    // Overlapping diagnostics share the source chunk and its proposed fix.
    std::ranges::sort(chunks, [] (const Chunk& a, const Chunk& b) {
        return a.Begin < b.Begin || (a.Begin == b.Begin && a.End < b.End);
    });
    std::vector<Chunk> merged;
    for (auto& chunk : chunks) {
        if (merged.empty() || chunk.Begin >= merged.back().End) {
            merged.push_back(std::move(chunk));
        } else {
            auto& previous = merged.back();
            previous.End = std::max(previous.End, chunk.End);
            previous.Violations.insert(previous.Violations.end(),
                std::make_move_iterator(chunk.Violations.begin()),
                std::make_move_iterator(chunk.Violations.end()));
            if (chunk.HasFix) {
                if (previous.HasFix) {
                    previous.Fix.insert(previous.Fix.end(),
                        formattedLines.begin() + previous.FormattedEnd,
                        formattedLines.begin() + chunk.FormattedBegin);
                } else {
                    previous.FormattedBegin = chunk.FormattedBegin;
                }
                previous.HasFix = true;
                previous.FormattedEnd = chunk.FormattedEnd;
                previous.Fix.insert(previous.Fix.end(), chunk.Fix.begin(),
                    chunk.Fix.end());
            }
            if (chunk.HasLayoutFix) {
                if (previous.HasLayoutFix) {
                    previous.LayoutFix.insert(previous.LayoutFix.end(),
                        layoutLines.begin() + previous.LayoutFormattedEnd,
                        layoutLines.begin() + chunk.LayoutFormattedBegin);
                } else {
                    previous.LayoutFormattedBegin = chunk.LayoutFormattedBegin;
                }
                previous.HasLayoutFix = true;
                previous.LayoutFormattedEnd = chunk.LayoutFormattedEnd;
                previous.LayoutFix.insert(previous.LayoutFix.end(),
                    chunk.LayoutFix.begin(), chunk.LayoutFix.end());
            }
            previous.HasLayoutIssue |= chunk.HasLayoutIssue;
            previous.HasWidthIssue |= chunk.HasWidthIssue;
        }
    }
    for (auto& chunk : merged) {
        std::ranges::stable_sort(chunk.Violations,
            [] (const Violation& a, const Violation& b) {
                return a.Line < b.Line ||
                    (a.Line == b.Line && a.Column < b.Column);
            });
        const auto& first = chunk.Violations.front();
        if (useColor) std::cout << BoldRed;
        std::cout << "fail: " << chunk.Violations.size() << " violation"
                  << (chunk.Violations.size() == 1 ? "" : "s");
        if (useColor) std::cout << ResetColor;
        auto location = std::string(path) + ':' + std::to_string(first.Line) +
            ':' + std::to_string(first.Column);
        if (location.size() + 6 <= terminalWidth) {
            std::cout << "\n  --> " << location << '\n';
        } else {
            std::cout << "\n  -->\n    ";
            PrintWrappedMessage(location, "    ", terminalWidth);
        }
        const auto width = std::to_string(chunk.End - 1).size();
        const auto gutter = std::string(width + 1, ' ') + "| ";
        std::cout << gutter << '\n';
        size_t violationIndex = 0;
        for (auto lineNumber = chunk.Begin; lineNumber < chunk.End;
            ++lineNumber)
        {
            auto text = lineNumber <= sourceLines.size()
                ? sourceLines[lineNumber - 1].Text : std::string_view{};
            if (text.ends_with('\r')) text.remove_suffix(1);
            const auto prefix = std::string(
                width - std::to_string(lineNumber).size(), ' ') +
                std::to_string(lineNumber) + " | ";
            auto printLine = [&] (size_t column, bool fromStart = false) {
                std::cout << prefix;
                return PrintSourceExcerpt(text, column, prefix.size(),
                    terminalWidth, fromStart);
            };
            if (violationIndex == chunk.Violations.size() ||
                chunk.Violations[violationIndex].Line != lineNumber)
            {
                printLine(0);
            }
            while (violationIndex < chunk.Violations.size() &&
                chunk.Violations[violationIndex].Line == lineNumber)
            {
                const auto& violation = chunk.Violations[violationIndex++];
                auto excerpt = printLine(violation.Column,
                    violation.Kind == ViolationKind::LineWidth);
                std::cout << gutter;
                if (!excerpt.CaretVisible) {
                    // Do not imply that the cropped end of a long line is
                    // the exact token at ColumnLimit + 1.
                    PrintWrappedMessage(violation.Message, gutter,
                        terminalWidth);
                    continue;
                }
                std::cout << std::string(excerpt.CaretOffset, ' ') << '^';
                if (gutter.size() + excerpt.CaretOffset + 2 +
                    violation.Message.size() < terminalWidth)
                {
                    std::cout << ' ' << violation.Message << '\n';
                } else {
                    std::cout << '\n' << gutter;
                    PrintWrappedMessage(violation.Message, gutter,
                        terminalWidth);
                }
            }
        }
        bool hasManualFix = false;
        if (!chunk.HasFix && !chunk.HasLayoutFix && chunk.HasWidthIssue) {
            for (const auto& violation : chunk.Violations) {
                if (violation.Kind != ViolationKind::LineWidth ||
                    violation.Line > sourceLines.size()) continue;
                auto suggestion = SuggestStringSplit(tree,
                    sourceLines[violation.Line - 1].Text, violation.Line,
                    style.ColumnLimit, style.IndentWidth);
                if (suggestion.empty()) continue;
                std::cout << "  suggested fix for line "
                          << violation.Line << " (manual):\n";
                for (const auto& line : suggestion) {
                    PrintSuggestedLine(line, terminalWidth,
                        gutter.size(), useColor);
                }
                if (!sourceLines[violation.Line - 1].HasNewline) {
                    std::cout << std::string(gutter.size(), ' ')
                              << "\\ No newline at end of file\n";
                }
                hasManualFix = true;
            }
        }
        if (chunk.HasFix || chunk.HasLayoutFix) {
            std::cout << "  suggested formatting:\n";
            const auto& fix = chunk.HasLayoutFix ? chunk.LayoutFix : chunk.Fix;
            for (const auto& line : fix) {
                PrintSuggestedLine(line.Text, terminalWidth,
                    gutter.size(), useColor);
                if (!line.HasNewline) {
                    std::cout << std::string(gutter.size(), ' ')
                              << "\\ No newline at end of file\n";
                }
            }
            if (fix.empty()) std::cout << "  (remove this chunk)\n";
        } else if (!chunk.HasLayoutIssue) {
            std::cout << "  ";
            PrintWrappedMessage("hint: names are not changed by the formatter.",
                "  ", terminalWidth);
        }
        if (chunk.HasLayoutIssue && !chunk.HasLayoutFix && !hasManualFix) {
            std::cout << "  ";
            PrintWrappedMessage(chunk.HasWidthIssue
                ? "hint: split the line manually or increase ColumnLimit."
                : "hint: adjust the operator or line break manually.",
                "  ", terminalWidth);
        }
    }
}

bool IsCppSource(const std::filesystem::path& path)
{
    auto extension = path.extension().string();
    return extension == ".c" || extension == ".cc" || extension == ".cpp" ||
        extension == ".cxx" || extension == ".h" || extension == ".hh" ||
        extension == ".hpp" || extension == ".hxx" || extension == ".inl" ||
        extension == ".ipp" || extension == ".tcc";
}

bool IsExcluded(
    const std::filesystem::path& relativePath,
    const std::vector<std::regex>& exclusions)
{
    if (exclusions.empty()) {
        return false;
    }
    auto name = relativePath.generic_string();
    return std::ranges::any_of(exclusions, [&] (const auto& pattern) {
        return std::regex_search(name, pattern);
    });
}

template <typename ConfigExcludes>
std::vector<std::filesystem::path> CollectFiles(
    const std::vector<std::filesystem::path>& paths,
    const std::vector<std::regex>& exclusions,
    ConfigExcludes configExcludes)
{
    std::vector<std::filesystem::path> files;
    for (const auto& path : paths) {
        if (std::filesystem::is_regular_file(path)) {
            if (!IsExcluded(path.filename(), exclusions) &&
                !configExcludes(path, false))
            {
                files.push_back(path);
            }
            continue;
        }
        if (!std::filesystem::is_directory(path)) {
            throw std::runtime_error(path.string() + ": path does not exist");
        }
        if (configExcludes(path, true)) {
            continue;
        }
        auto iterator = std::filesystem::recursive_directory_iterator(path);
        for (const auto end = std::filesystem::recursive_directory_iterator();
            iterator != end; ++iterator)
        {
            const auto& entry = *iterator;
            bool directory = entry.is_directory();
            if (IsExcluded(entry.path().lexically_relative(path), exclusions) ||
                configExcludes(entry.path(), directory))
            {
                if (directory) {
                    iterator.disable_recursion_pending();
                }
                continue;
            }
            if (!entry.is_symlink() && entry.is_regular_file() &&
                IsCppSource(entry.path()))
            {
                files.push_back(entry.path());
            }
        }
    }
    std::ranges::sort(files);
    files.erase(std::ranges::unique(files).begin(), files.end());
    return files;
}

struct Options
{
    Mode FormattingMode = Mode::Check;
    bool SummaryOnly = false;
    bool DisableConfig = false;
    bool ShowHelp = false;
    std::optional<std::filesystem::path> ConfigPath;
    std::vector<std::regex> Exclusions;
    std::vector<std::filesystem::path> Paths;
};

Options ParseOptions(int argc, char* argv[])
{
    Options options;
    bool modeSelected = false;
    bool positionalOnly = false;
    for (int index = 1; index < argc; ++index) {
        std::string_view argument(argv[index]);
        if (positionalOnly || !argument.starts_with('-') || argument == "-") {
            options.Paths.emplace_back(argument);
        } else if (argument == "--") {
            positionalOnly = true;
        } else if (argument == "-h" || argument == "--help") {
            options.ShowHelp = true;
        } else if (argument == "--fix" || argument == "--check") {
            if (modeSelected) {
                throw std::runtime_error("mode specified more than once");
            }
            options.FormattingMode = argument == "--fix" ? Mode::Fix :
                Mode::Check;
            modeSelected = true;
        } else if (argument == "--summary") {
            options.SummaryOnly = true;
        } else if (argument == "--no-config") {
            options.DisableConfig = true;
        } else if (argument == "--config") {
            if (++index == argc) {
                throw std::runtime_error("--config requires a file path");
            }
            options.ConfigPath = argv[index];
        } else if (argument.starts_with("--config=")) {
            options.ConfigPath = std::string(argument.substr(9));
        } else if (argument == "--exclude" ||
            argument.starts_with("--exclude="))
        {
            std::string pattern;
            if (argument == "--exclude") {
                if (++index == argc) {
                    throw std::runtime_error("--exclude requires a regex");
                }
                pattern = argv[index];
            } else {
                pattern = argument.substr(10);
            }
            if (pattern.empty()) {
                throw std::runtime_error("--exclude requires a regex");
            }
            try {
                options.Exclusions.emplace_back(pattern);
            } catch (const std::regex_error& error) {
                throw std::runtime_error(
                    "invalid --exclude regex: " + pattern + ": " + error.what());
            }
        } else {
            throw std::runtime_error("unknown option: " + std::string(argument));
        }
    }
    if (options.SummaryOnly && options.FormattingMode != Mode::Check) {
        throw std::runtime_error("--summary requires check mode");
    }
    if (options.DisableConfig && options.ConfigPath) {
        throw std::runtime_error("--config and --no-config cannot be combined");
    }
    return options;
}

class StyleResolver
{
public:
    explicit StyleResolver(const Options& options)
        : Disabled_(options.DisableConfig)
    {
        if (options.ConfigPath) {
            ExplicitConfig_ = ConfigEntry{
                .Directory = std::filesystem::absolute(*options.ConfigPath)
                                 .parent_path().lexically_normal(),
                .Settings = formatter::LoadConfigFile(*options.ConfigPath),
            };
        }
    }

    const formatter::Configuration& Resolve(
        const std::filesystem::path& sourcePath)
    {
        return FindConfig(sourcePath).Settings;
    }

    bool IsExcluded(const std::filesystem::path& path, bool directory)
    {
        // A directory is checked against its parent's configuration before
        // descending; a file uses its own nearest configuration.
        auto source = directory ? path.parent_path() / "unused.cpp" : path;
        const auto& config = FindConfig(source);
        if (config.Settings.Exclusions.empty()) {
            return false;
        }
        auto relative = std::filesystem::absolute(path).lexically_normal()
                            .lexically_relative(config.Directory);
        return ::IsExcluded(relative, config.Settings.Exclusions);
    }

private:
    struct ConfigEntry
    {
        std::filesystem::path Directory;
        formatter::Configuration Settings;
    };

    const ConfigEntry& FindConfig(const std::filesystem::path& sourcePath)
    {
        if (ExplicitConfig_) {
            return *ExplicitConfig_;
        }
        if (Disabled_) {
            return DefaultConfig_;
        }
        auto directory = sourcePath.parent_path();
        std::error_code error;
        directory = directory.empty()
            ? std::filesystem::current_path(error)
            : std::filesystem::absolute(directory, error);
        if (error) {
            return DefaultConfig_;
        }
        auto directoryKey = directory.lexically_normal().string();
        auto [configIterator, configInserted] =
            ConfigPaths_.try_emplace(directoryKey);
        if (configInserted) {
            configIterator->second = formatter::FindStyleFile(directory);
        }
        const auto& config = configIterator->second;
        if (!config) {
            return DefaultConfig_;
        }
        auto key = config->lexically_normal().string();
        auto [iterator, inserted] = Configs_.try_emplace(key);
        if (inserted) {
            iterator->second.Directory =
                config->parent_path().lexically_normal();
            iterator->second.Settings = formatter::LoadConfigFile(*config);
        }
        return iterator->second;
    }

    bool Disabled_;
    ConfigEntry DefaultConfig_;
    std::optional<ConfigEntry> ExplicitConfig_;
    std::unordered_map<
        std::string,
        std::optional<std::filesystem::path>> ConfigPaths_;
    std::unordered_map<std::string, ConfigEntry> Configs_;
};

void PrintHelp(std::ostream& output, std::string_view executable)
{
    output
        << "Usage:\n"
        << "  " << executable << " [options] <path>...\n"
        << "  " << executable << " --fix [options] <path>...\n"
        << "\n"
        << "Options:\n"
        << "  --fix            Format files in place.\n"
        << "  --check          Check without editing (default).\n"
        << "  --summary        In check mode, print only file names.\n"
        << "  --config <file>  Use one configuration file for every input.\n"
        << "  --no-config      Do not search parent directories for .hit-fmt.\n"
        << "  --exclude <regex> Skip matching files or directories (repeatable).\n"
        << "  -h, --help       Show this help.\n"
        << "\n"
        << "Without --config or --no-config, each source file uses the "
           "nearest .hit-fmt file in its directory or a parent directory.\n"
        << "Input paths may be files or directories.\n"
        << "Directories are searched recursively for C and C++ source "
           "files.\n"
        << "--exclude patterns search paths relative to each input directory; "
           ".hit-fmt Exclude patterns search paths relative to the config file.\n"
        << "Both use ECMAScript regex; matching directories are skipped.\n"
        << "Files larger than 4 MiB are skipped with a warning.\n"
        << "Check output is colorized when written to a terminal; set "
           "NO_COLOR to disable it.\n"
        << "\n"
        << "Examples:\n"
        << "  " << executable << " source.cpp\n"
        << "  " << executable << " --fix source.cpp include/\n"
        << "  " << executable << " --summary src/ include/\n"
        << "  " << executable << " --fix --exclude '(^|/)generated(/|$)' src/\n";
}

} // namespace

int main(int argc, char* argv[])
{
    Options options;
    std::vector<std::filesystem::path> files;
    std::optional<StyleResolver> styleResolver;
    try {
        options = ParseOptions(argc, argv);
        if (options.ShowHelp) {
            PrintHelp(std::cout, argv[0]);
            return 0;
        }
        if (options.Paths.empty()) {
            PrintHelp(std::cerr, argv[0]);
            return 1;
        }
        styleResolver.emplace(options);
        files = CollectFiles(
            options.Paths,
            options.Exclusions,
            [&] (const auto& path, bool directory) {
                return styleResolver->IsExcluded(path, directory);
            });
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    auto mode = options.FormattingMode;
    bool summaryOnly = options.SummaryOnly;
    bool useColor = UseTerminalColors();
    size_t mismatchCount = 0;
    for (const auto& path : files) {
        auto filePath = path.string();
        try {
            auto sourceStorage = util::ReadEntireFile(
                filePath,
                MaxSourceFileSize,
                util::SourcePaddingSize);
            util::PaddedSourceView paddedSource{
                .Data = sourceStorage.data(),
                .Size = sourceStorage.size() - util::SourcePaddingSize,
            };
            auto source = paddedSource.View();
            const auto& config = styleResolver->Resolve(path);
            auto tree = parser::Parse(paddedSource, [] (parser::ParseError) { });
            auto checkStyle = config.FormattingStyle;
            if (mode == Mode::Check) {
                // Check layout on the original syntax tree, not a speculative
                // reflow. Keep checking unrelated formatting rules as before.
                checkStyle.ExpressionLayoutCondition =
                    formatter::ExpressionLayoutCondition::Preserve;
            }
            auto formatted = formatter::Format(tree, checkStyle);
            switch (mode) {
            case Mode::Fix:
                if (formatted != source) {
                    util::WriteEntireFile(filePath, formatted);
                }
                break;
            case Mode::Check: {
                auto names = formatter::CheckNames(tree, config.Naming);
                auto parameters = formatter::CheckParameterNames(tree, config);
                auto layout = formatter::CheckExpressionLayout(tree,
                    config.FormattingStyle);
                bool mismatch = formatted != source || !names.empty() ||
                    !parameters.empty() || !layout.empty();
                if (summaryOnly && mismatch) {
                    if (useColor) std::cout << BoldRed;
                    std::cout << filePath << ": ";
                    if (formatted != source) {
                        std::cout << "formatting mismatch";
                    } else if (!layout.empty()) {
                        std::cout << "fail: layout violation";
                        if (layout.size() != 1) std::cout << 's';
                    } else if (!names.empty()) {
                        std::cout << "fail: invalid name";
                        if (names.size() != 1) std::cout << 's';
                    } else {
                        std::cout << "fail: unnamed parameter";
                        if (parameters.size() != 1) std::cout << 's';
                    }
                    if (useColor) std::cout << ResetColor;
                    std::cout.put('\n');
                } else if (!summaryOnly && mismatch) {
                    auto layoutFormatted = layout.empty() ? std::string{} :
                        formatter::Format(tree, config.FormattingStyle);
                    PrintCheckDiagnostics(tree, config.FormattingStyle,
                        filePath, source, formatted, layoutFormatted, names,
                        parameters, layout, useColor);
                }
                mismatchCount += mismatch;
                break;
            }
            }
        } catch (const util::FileTooLargeError& error) {
            std::cerr << "warning: " << error.what() << "; skipping\n";
        } catch (const std::exception& error) {
            std::cerr << filePath << ": " << error.what() << '\n';
            return 1;
        }
    }
    if (mode == Mode::Check && useColor) {
        if (mismatchCount == 0) {
            std::cout << BoldGreen << "All checked files are formatted."
                      << ResetColor << '\n';
        } else {
            std::cout << BoldRed << mismatchCount << " file"
                      << (mismatchCount == 1 ? "" : "s")
                      << " require formatting, naming or parameter names."
                      << ResetColor << '\n';
        }
    }
    return mismatchCount != 0 ? 1 : 0;
}
