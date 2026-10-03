#include "config_help.hpp"
#include "config_template.hpp"
#include "file_dispatch.hpp"
#include "file_check.hpp"
#include "version.hpp"

#include <formatter/config.hpp>
#include <formatter/formatter.hpp>
#include <util/file.hpp>

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <regex>
#include <sstream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
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

constexpr std::string_view BoldCyan = "\033[1;36m";

constexpr std::string_view BoldYellow = "\033[1;33m";

constexpr std::string_view Green = "\033[32m";

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
        lines
            .push_back(
                {
                    .Text = text.substr(begin, end - begin),
                    .HasNewline = hasNewline,
                });
        begin = hasNewline ? end + 1 : text.size();
    }
    return lines;
}

size_t TerminalWidth()
{
    #ifndef _WIN32
    if (winsize size{};
        isatty(fileno(stdout)) &&
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
            width <= 1000)
        {
            return width;
        }
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
    std::string_view text,
    size_t column,
    size_t prefixWidth)
{
    RenderedLine rendered;
    size_t codePoint = 1;
    for (size_t i = 0; i < text.size(); ++codePoint) {
        if (codePoint == column) {
            rendered.Caret = rendered.Cells.size();
        }
        auto start = i++;
        while (i < text.size() &&
               (static_cast<unsigned char>(text[i]) & 0xc0) == 0x80)
        {
            ++i;
        }
        if (text[start] == '\t') {
            auto spaces = 8 - (prefixWidth + rendered.Cells.size()) % 8;
            while (spaces--) {
                rendered.Cells.push_back(" ");
            }
        } else if (text[start] == '\r') {
            // CRLF lines are trimmed before reaching this function.
            continue;
        } else {
            rendered.Cells.push_back(text.substr(start, i - start));
        }
    }
    if (column >= codePoint) {
        rendered.Caret = rendered.Cells.size();
    }
    return rendered;
}

void PrintCells(
    std::ostream& output,
    const std::vector<std::string_view>& cells,
    size_t begin,
    size_t end)
{
    for (auto i = begin; i < end; ++i) {
        output << cells[i];
    }
}

struct Excerpt
{
    size_t CaretOffset;
    bool CaretVisible;
};

Excerpt PrintSourceExcerpt(
    std::ostream& output,
    std::string_view text,
    size_t column,
    size_t prefixWidth,
    size_t terminalWidth,
    bool fromStart = false)
{
    auto rendered = RenderLine(text, column, prefixWidth);
    const auto& cells = rendered.Cells;
    auto available = terminalWidth > prefixWidth + 1
        ? terminalWidth - prefixWidth - 1
        : size_t{16};
    auto window = available > 6 ? available - 6 : size_t{1};
    size_t begin = 0;
    if (cells.size() > available && !fromStart) {
        begin =
            std::min(
                cells.size() - window,
                rendered.Caret > window / 2 ? rendered.Caret - window / 2 : 0);
    }
    auto count =
        cells.size() > available
            ? (fromStart ? available - 3 : window)
            : available;
    auto end = std::min(cells.size(), begin + count);
    if (begin) {
        output << "...";
    }
    PrintCells(output, cells, begin, end);
    if (end < cells.size()) {
        output << "...";
    }
    output << '\n';
    return {
        .CaretOffset = (begin ? 3 : 0) + std::min(rendered.Caret, end) - begin,
        .CaretVisible = rendered.Caret < end ||
            (rendered.Caret == end && end == cells.size()),
    };
}

void PrintWrappedMessage(
    std::ostream& output,
    std::string_view message,
    std::string_view prefix,
    size_t terminalWidth)
{
    auto available = terminalWidth > prefix.size() + 2
        ? terminalWidth - prefix.size() - 2
        : size_t{20};
    while (!message.empty()) {
        auto length = std::min(available, message.size());
        if (length < message.size()) {
            auto space = message.rfind(' ', length);
            if (space != std::string_view::npos && space > 0) {
                length = space;
            }
        }
        while (length && length < message.size() &&
               (static_cast<unsigned char>(message[length]) & 0xc0) == 0x80)
        {
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

void PrintSuggestedLine(
    std::ostream& output,
    std::string_view text,
    size_t terminalWidth,
    size_t gutterWidth,
    bool useColor)
{
    auto cells = RenderLine(text, 0, gutterWidth).Cells;
    auto available = terminalWidth > gutterWidth + 1
        ? terminalWidth - gutterWidth - 1
        : size_t{16};
    for (size_t begin = 0;;) {
        if (useColor) {
            output << Green;
        }
        if (begin) {
            output << std::string(gutterWidth, ' ');
        } else {
            output << std::string(gutterWidth - 3, ' ') << " + ";
        }
        auto end = std::min(cells.size(), begin + available);
        PrintCells(output, cells, begin, end);
        if (useColor) {
            output << ResetColor;
        }
        output << '\n';
        if (end == cells.size()) {
            break;
        }
        begin = end;
    }
}

bool UseTerminalColors(FILE* stream = stdout)
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
    return _isatty(_fileno(stream)) != 0;
#else
    return isatty(fileno(stream)) != 0;
#endif
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
    return 1 +
        std::count_if(
            text.begin(),
            text.begin() + byte,
            [] (unsigned char c) { return (c & 0xc0) != 0x80; });
}

size_t DifferenceColumn(std::string_view actual, std::string_view expected)
{
    size_t byte = 0;
    while (byte < actual.size() && byte < expected.size() &&
           actual[byte] == expected[byte])
    {
        ++byte;
    }
    while (byte < actual.size() &&
           std::isspace(static_cast<unsigned char>(actual[byte])))
    {
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
        while (end < text.size() &&
               (static_cast<unsigned char>(text[end]) & 0xc0) == 0x80)
        {
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
    auto wanted =
        DescribeWhitespace(
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
    size_t line,
    std::string_view actual,
    std::string_view expected)
{
    std::vector<Violation> violations;
    size_t a = 0, b = 0;
    while (a < actual.size() || b < expected.size()) {
        auto startA = a, startB = b;
        while (a < actual.size() &&
               std::isspace(static_cast<unsigned char>(actual[a])))
        {
            ++a;
        }
        while (b < expected.size() &&
               std::isspace(static_cast<unsigned char>(expected[b])))
        {
            ++b;
        }
        if (actual.substr(startA, a - startA) !=
            expected.substr(startB, b - startB))
        {
            violations
                .push_back(
                    {
                        line,
                        ColumnAt(actual, a),
                        SpacingMessage(actual, expected, startA, a, startB, b),
                    });
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
        while (i < text.size() &&
               (static_cast<unsigned char>(text[i]) & 0xc0) == 0x80)
        {
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
    while (prefix < actual.size() && prefix < expected.size() &&
           actual[prefix] == expected[prefix])
    {
        ++prefix;
    }
    while (prefix &&
           ((prefix < actual.size() &&
                        (static_cast<unsigned char>(actual[prefix]) & 0xc0) ==
                            0x80) ||
                (prefix < expected.size() &&
                        (static_cast<unsigned char>(expected[prefix]) & 0xc0) ==
                            0x80)))
    {
        --prefix;
    }
    size_t suffix = 0;
    while (suffix < actual.size() - prefix &&
           suffix < expected.size() - prefix &&
           actual[actual.size() - suffix - 1] ==
            expected[expected.size() - suffix - 1])
    {
        ++suffix;
    }
    while (suffix &&
           ((static_cast<unsigned char>(actual[actual.size() - suffix]) &
                            0xc0) == 0x80 ||
                (static_cast<unsigned char>(expected[expected.size() -
                                        suffix]) & 0xc0) == 0x80))
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
        while (i < text.size() &&
               std::isspace(static_cast<unsigned char>(text[i])))
        {
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
        while (a < original.size() &&
               std::isspace(static_cast<unsigned char>(original[a])))
        {
            ++a;
        }
        while (b < replacement.size() &&
               std::isspace(static_cast<unsigned char>(replacement[b])))
        {
            ++b;
        }
        auto gapA = std::string_view(original).substr(startA, a - startA);
        auto gapB = std::string_view(replacement).substr(startB, b - startB);
        if (gapA != gapB) {
            auto lineIndex =
                static_cast<size_t>(std::upper_bound(
                    lineStarts.begin(),
                    lineStarts.end(),
                    a) - lineStarts.begin() - 1);
            auto byte =
                std::min(
                    a - lineStarts[lineIndex],
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
                message =
                    "join lines before '" + token + "' with " +
                        DescribeWhitespace(gapB);
            } else if (oldBreaks != newBreaks) {
                auto difference =
                    oldBreaks > newBreaks
                        ? oldBreaks - newBreaks
                        : newBreaks - oldBreaks;
                message =
                    (oldBreaks > newBreaks ? "remove " : "insert ") +
                        std::to_string(difference) +
                        (difference == 1 ? " blank line" : " blank lines") +
                        " before '" + token + "'";
            } else if (oldBreaks) {
                auto oldIndent = gapA.substr(gapA.rfind('\n') + 1);
                auto newIndent = gapB.substr(gapB.rfind('\n') + 1);
                message =
                    "indent '" + token + "' with " +
                        DescribeWhitespace(newIndent) + " instead of " +
                        DescribeWhitespace(oldIndent);
            } else {
                message =
                    SpacingMessage(original, replacement, startA, a, startB, b);
            }
            violations
                .push_back(
                    {
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

bool SameNonWhitespaceLines(
    std::span<const Line> source,
    std::span<const Line> formatted)
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
        (std::isspace(static_cast<unsigned char>(text[8])) || text[8] == '<' ||
                text[8] == '"');
}

std::vector<Chunk> FormattingChunks(
    const std::vector<Line>& source,
    const std::vector<Line>& formatted)
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
                   IsIncludeLine(source[sourceIndex + sourceCount]))
            {
                ++sourceCount;
            }
            while (formattedIndex + formattedCount < formatted.size() &&
                   IsIncludeLine(formatted[formattedIndex + formattedCount]))
            {
                ++formattedCount;
            }
        } else {
            size_t bestDistance = std::numeric_limits<size_t>::max();
            size_t bestSkew = std::numeric_limits<size_t>::max();
            bool bestPreservesTokens = false;
            auto sourceLimit =
                std::min(source.size(), sourceIndex + DiffLookahead + 1);
            auto formattedLimit =
                std::min(formatted.size(), formattedIndex + DiffLookahead + 1);
            for (auto nextSource = sourceIndex;
                 nextSource < sourceLimit;
                 ++nextSource)
            {
                for (auto nextFormatted = formattedIndex;
                     nextFormatted < formattedLimit;
                     ++nextFormatted)
                {
                    if (nextSource == sourceIndex &&
                        nextFormatted == formattedIndex)
                    {
                        continue;
                    }
                    if (source[nextSource] != formatted[nextFormatted]) {
                        continue;
                    }
                    auto sourceSkip = nextSource - sourceIndex;
                    auto formattedSkip = nextFormatted - formattedIndex;
                    auto distance = sourceSkip + formattedSkip;
                    auto skew =
                        sourceSkip > formattedSkip
                            ? sourceSkip - formattedSkip
                            : formattedSkip - sourceSkip;
                    // An unchanged line inside a repeated block can look
                    // closer than the actual end of a whitespace-only edit.
                    // Prefer an anchor that keeps all intervening tokens.
                    bool preservesTokens =
                        sourceSkip && formattedSkip &&
                            SameNonWhitespaceLines(
                                std::span(source)
                                    .subspan(sourceIndex, sourceSkip),
                                std::span(formatted)
                                    .subspan(formattedIndex, formattedSkip));
                    if ((preservesTokens && !bestPreservesTokens) ||
                        (preservesTokens == bestPreservesTokens &&
                                (distance < bestDistance ||
                                        (distance == bestDistance &&
                                                skew < bestSkew))))
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
                sourceCount = source.size() - sourceIndex;
                formattedCount = formatted.size() - formattedIndex;
            }
        }

        auto begin = std::min(
            sourceIndex + 1,
            std::max(size_t{1}, source.size()));
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
        if (sourceCount && formattedCount &&
            (sourceCount != formattedCount || sourceCount > 1))
        {
            auto reflow =
                ReflowViolations(
                    std::span(source).subspan(sourceIndex, sourceCount),
                    std::span(formatted)
                        .subspan(formattedIndex, formattedCount),
                    sourceIndex + 1);
            if (reflow && !reflow->empty()) {
                chunk.Violations = std::move(*reflow);
                alignedReflow = true;
            }
        }
        for (size_t i = 0; i < sourceCount && !alignedReflow; ++i) {
            const auto& line = source[sourceIndex + i];
            auto replacement = i < formattedCount
                ? formatted[formattedIndex + i].Text
                : std::string_view{};
            if (i < formattedCount && line == formatted[formattedIndex + i]) {
                continue;
            }
            std::string message;
            auto kind = ViolationKind::Other;
            auto column = DifferenceColumn(line.Text, replacement);
            if (line.Text == replacement && !line.HasNewline &&
                i < formattedCount && formatted[formattedIndex + i].HasNewline)
            {
                message = "missing final newline";
            } else if (line.Text == replacement && line.HasNewline &&
                       i < formattedCount &&
                       !formatted[formattedIndex + i].HasNewline)
            {
                message = "unexpected final newline";
            } else if (sourceCount == formattedCount && i < formattedCount &&
                       IsIncludeLine(line) &&
                       IsIncludeLine(formatted[formattedIndex + i]))
            {
                message =
                    "include order: expected " + QuoteSnippet(replacement) +
                        " on this line";
            } else if (sourceCount != formattedCount) {
                if (formattedCount == 0) {
                    message = "remove this line";
                } else if (i >= formattedCount) {
                    message =
                        "merge this line into the preceding formatted line";
                } else if (formattedCount > sourceCount) {
                    message =
                        "split line: expected " + QuoteSnippet(replacement) +
                            " on this line";
                    kind = ViolationKind::Reflow;
                } else {
                    message =
                        "join lines: expected " + QuoteSnippet(replacement) +
                            " on this line";
                    kind = ViolationKind::Reflow;
                }
            } else if (SameNonWhitespace(line.Text, replacement)) {
                auto spacing =
                    SpacingViolations(
                        sourceIndex + i + 1,
                        line.Text,
                        replacement);
                chunk.Violations
                    .insert(
                        chunk.Violations.end(),
                        spacing.begin(),
                        spacing.end());
                continue;
            } else {
                message = ContentChange(line.Text, replacement);
            }
            chunk.Violations
                .push_back(
                    {sourceIndex + i + 1, column, std::move(message), kind});
        }
        if (sourceCount == 0) {
            auto column = sourceIndex < source.size() || source.empty()
                ? size_t{1}
                : ColumnAt(source.back().Text, source.back().Text.size());
            auto location =
                sourceIndex == source.size()
                    ? " at end of file"
                    : " before this line";
            auto message =
                formattedCount == 1
                    ? "insert " + QuoteSnippet(formatted[formattedIndex].Text) +
                        location
                    : "insert " + std::to_string(formattedCount) + " lines" +
                        location + ", starting with " +
                        QuoteSnippet(formatted[formattedIndex].Text);
            chunk.Violations.push_back({begin, column, std::move(message)});
        }
        if (chunk.Violations.empty()) {
            // A matched prefix can be followed by inserted output lines.
            auto number = sourceIndex + sourceCount;
            auto column =
                ColumnAt(
                    source[number - 1].Text,
                    source[number - 1].Text.size());
            auto inserted = formattedCount > sourceCount
                ? formattedCount - sourceCount
                : size_t{0};
            chunk.Violations
                .push_back(
                    {
                        number,
                        column,
                        inserted
                            ? "insert " + std::to_string(inserted) +
                                " formatted lines after this line"
                            : "replace lines following this line with "
                                "formatted output",
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

// Display the one pipeline's formatting difference, without a second model.
void PrintFormattingDiagnostics(
    std::ostream& output,
    std::string_view path,
    std::string_view source,
    std::string_view formatted,
    bool useColor,
    bool fixed = false)
{
    const auto sourceLines = SplitLines(source);
    auto chunks = FormattingChunks(sourceLines, SplitLines(formatted));
    const auto terminalWidth = TerminalWidth();
    for (auto& chunk : chunks) {
        std::ranges::stable_sort(
            chunk.Violations,
            [] (const Violation& a, const Violation& b) {
                return a.Line < b.Line ||
                    (a.Line == b.Line && a.Column < b.Column);
            });
        const auto& first = chunk.Violations.front();
        if (useColor) {
            output << (fixed ? BoldGreen : BoldRed);
        }
        output << (fixed ? "fixed: " : "fail: ") << chunk.Violations.size()
            << " violation" << (chunk.Violations.size() == 1 ? "" : "s");
        if (useColor) {
            output << ResetColor;
        }
        auto location =
            std::string(path) + ':' + std::to_string(first.Line) + ':' +
                std::to_string(first.Column);
        if (fixed) {
            location += " [original source; before formatting]";
        }
        if (location.size() + 6 <= terminalWidth) {
            output << "\n  --> " << location << '\n';
        } else {
            output << "\n  -->\n    ";
            PrintWrappedMessage(output, location, "    ", terminalWidth);
        }
        const auto width = std::to_string(chunk.End - 1).size();
        const auto gutter = std::string(width + 1, ' ') + "| ";
        output << gutter.substr(0, gutter.size() - 1) << '\n';
        size_t violationIndex = 0;
        for (auto lineNumber = chunk.Begin;
             lineNumber < chunk.End;
             ++lineNumber)
        {
            auto text = lineNumber <= sourceLines.size()
                ? sourceLines[lineNumber - 1].Text
                : std::string_view{};
            if (text.ends_with('\r')) {
                text.remove_suffix(1);
            }
            const auto prefix =
                std::string(width - std::to_string(lineNumber).size(), ' ') +
                    std::to_string(lineNumber) + " | ";
            auto printLine = [&] (size_t column, bool fromStart = false) {
                output << prefix;
                return PrintSourceExcerpt(
                    output,
                    text,
                    column,
                    prefix.size(),
                    terminalWidth,
                    fromStart);
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
                auto excerpt = printLine(violation.Column);
                output << gutter;
                if (!excerpt.CaretVisible) {
                    // Do not imply that the cropped end of a long line is
                    // the exact token at ColumnLimit + 1.
                    PrintWrappedMessage(
                        output,
                        violation.Message,
                        gutter,
                        terminalWidth);
                    continue;
                }
                output << std::string(excerpt.CaretOffset, ' ') << '^';
                if (gutter.size() + excerpt.CaretOffset + 2 +
                        violation.Message.size() < terminalWidth)
                {
                    output << ' ' << violation.Message << '\n';
                } else {
                    output << '\n' << gutter;
                    PrintWrappedMessage(
                        output,
                        violation.Message,
                        gutter,
                        terminalWidth);
                }
            }
        }
        if (chunk.HasFix) {
            output
                << (fixed
                        ? "  applied formatting:\n"
                        : "  suggested formatting:\n");
            const auto& fix = chunk.Fix;
            for (const auto& line : fix) {
                PrintSuggestedLine(
                    output,
                    line.Text,
                    terminalWidth,
                    gutter.size(),
                    useColor);
                if (!line.HasNewline) {
                    output << std::string(gutter.size(), ' ')
                        << "\\ No newline at end of file\n";
                }
            }
            if (fix.empty()) {
                output << "  (remove this chunk)\n";
            }
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
    return std::ranges::any_of(
        exclusions,
        [&] (const auto& pattern) {
            return std::regex_search(name, pattern);
        });
}

template <typename ConfigExcludes, typename Visit>
bool ForEachFile(
    const std::vector<std::filesystem::path>& paths,
    const std::vector<std::regex>& exclusions,
    ConfigExcludes configExcludes,
    Visit visit)
{
    std::unordered_set<std::filesystem::path> seen;
    auto emit = [&] (const std::filesystem::path& path) {
        return !seen.insert(path).second || visit(path);
    };
    for (const auto& path : paths) {
        if (std::filesystem::is_regular_file(path)) {
            if (!IsExcluded(path.filename(), exclusions) &&
                !configExcludes(path, false) && !emit(path))
            {
                return false;
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
             iterator != end;
             ++iterator)
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
                IsCppSource(entry.path()) && !emit(entry.path()))
            {
                return false;
            }
        }
    }
    return true;
}

struct Options
{
    bool Fix = false;
    bool SummaryOnly = false;
    bool DisableConfig = false;
    bool ShowHelp = false;
    bool ShowVersion = false;
    bool ShowConfigHelp = false;
    bool GenerateConfig = false;
    size_t Threads = 1;
    std::optional<std::filesystem::path> ConfigPath;
    std::vector<std::regex> Exclusions;
    std::vector<std::filesystem::path> Paths;
};

Options ParseOptions(int argc, char* argv[])
{
    Options options;
    bool positionalOnly = false;
    for (int index = 1; index < argc; ++index) {
        std::string_view argument(argv[index]);
        if (!positionalOnly) {
            hit_fmt::ValidatePreviewOption(argument);
        }
        if (positionalOnly || !argument.starts_with('-') || argument == "-") {
            options.Paths.emplace_back(argument);
        } else if (argument == "--") {
            positionalOnly = true;
        } else if (argument == "-h" || argument == "--help") {
            options.ShowHelp = true;
        } else if (argument == "-V" || argument == "--version") {
            options.ShowVersion = true;
        } else if (argument == "--help-config") {
            options.ShowConfigHelp = true;
        } else if (argument == "--generate-config") {
            options.GenerateConfig = true;
        } else if (argument == "--fix") {
            options.Fix = true;
        } else if (argument == "--check") {
            // Check mode is the default, so this flag needs no state change.
        } else if (argument == "--summary") {
            options.SummaryOnly = true;
        } else if (argument == "--no-config") {
            options.DisableConfig = true;
        } else if (argument == "-j" || argument == "--threads" ||
                   argument.starts_with("--threads=") ||
                   (argument.starts_with("-j") && argument.size() > 2))
        {
            std::string_view value;
            if (argument == "-j") {
                // Only consume the next token if it is a thread count;
                // otherwise it remains an input path or another option.
                if (index + 1 < argc) {
                    std::string_view next(argv[index + 1]);
                    if (!next.empty() &&
                        std::ranges::all_of(
                            next,
                            [] (char c) { return c >= '0' && c <= '9'; }))
                    {
                        value = argv[++index];
                    }
                }
                if (value.empty()) {
                    options.Threads =
                        std::max(1u, std::thread::hardware_concurrency());
                    continue;
                }
            } else if (argument == "--threads") {
                if (++index == argc) {
                    throw std::runtime_error(
                        "--threads requires a positive integer");
                }
                value = argv[index];
            } else {
                value = argument.substr(argument.starts_with("-j") ? 2 : 10);
            }
            size_t threads = 0;
            auto [end, error] =
                std::from_chars(
                    value.data(),
                    value.data() + value.size(),
                    threads);
            if (error != std::errc{} || end != value.data() + value.size() ||
                threads == 0)
            {
                throw std::runtime_error(
                    "--threads requires a positive integer");
            }
            options.Threads = threads;
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
                    "invalid --exclude regex: " + pattern + ": " +
                        error.what());
            }
        } else {
            throw std::runtime_error(
                "unknown option: " + std::string(argument));
        }
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
                                 .parent_path()
                                 .lexically_normal(),
                .Settings = LoadPreparedConfig(*options.ConfigPath),
            };
        }
    }

    const hit_fmt::PreparedFileConfiguration& Resolve(
        const std::filesystem::path& sourcePath)
    {
        return *FindConfig(sourcePath).Settings;
    }

    bool IsExcluded(const std::filesystem::path& path, bool directory)
    {
        // A directory is checked against its parent's configuration before
        // descending; a file uses its own nearest configuration.
        auto source = directory ? path.parent_path() / "unused.cpp" : path;
        const auto& config = FindConfig(source);
        if (config.Settings->Settings.Exclude.empty()) {
            return false;
        }
        auto relative =
            std::filesystem::absolute(path).lexically_normal()
                .lexically_relative(config.Directory);
        return ::IsExcluded(relative, config.Settings->Settings.Exclude);
    }

private:
    struct ConfigEntry
    {
        std::filesystem::path Directory;
        std::unique_ptr<const hit_fmt::PreparedFileConfiguration> Settings;
    };

    static std::unique_ptr<const hit_fmt::PreparedFileConfiguration>
    LoadPreparedConfig(const std::filesystem::path& path)
    {
        try {
            return std::make_unique<const hit_fmt::PreparedFileConfiguration>(
                formatter::LoadConfigFile(path));
        }
        catch (const std::exception &error) {
            throw std::runtime_error(path.string() + ": " + error.what());
        }
    }

    ConfigEntry& DefaultConfig()
    {
        if (!DefaultConfig_.Settings)
            DefaultConfig_.Settings =
                std::make_unique<const hit_fmt::PreparedFileConfiguration>(
                    formatter::Configuration{});
        return DefaultConfig_;
    }

    const ConfigEntry& FindConfig(const std::filesystem::path& sourcePath)
    {
        if (ExplicitConfig_) {
            return *ExplicitConfig_;
        }
        if (Disabled_) {
            return DefaultConfig();
        }
        auto directory = sourcePath.parent_path();
        std::error_code error;
        directory =
            directory.empty()
                ? std::filesystem::current_path(error)
                : std::filesystem::absolute(directory, error);
        if (error) {
            return DefaultConfig();
        }
        auto directoryKey = directory.lexically_normal().string();
        auto [configIterator, configInserted] =
            ConfigPaths_.try_emplace(directoryKey);
        if (configInserted) {
            configIterator->second = formatter::FindStyleFile(directory);
        }
        const auto& config = configIterator->second;
        if (!config) {
            return DefaultConfig();
        }
        auto key = config->lexically_normal().string();
        auto iterator = Configs_.try_emplace(key).first;
        if (!iterator->second.Settings) {
            iterator->second.Directory =
                config->parent_path().lexically_normal();
            iterator->second.Settings = LoadPreparedConfig(*config);
        }
        return iterator->second;
    }

    bool Disabled_;
    ConfigEntry DefaultConfig_;
    std::optional<ConfigEntry> ExplicitConfig_;
    std::unordered_map<std::string, std::optional<std::filesystem::path>>
        ConfigPaths_;
    std::unordered_map<std::string, ConfigEntry> Configs_;
};

void PrintHelp(std::ostream& output, std::string_view executable, bool useColor)
{
    auto highlight = [&] (std::string_view text, std::string_view color) {
        if (useColor) {
            output << color;
        }
        output << text;
        if (useColor) {
            output << ResetColor;
        }
    };
    auto option = [&] (std::string_view name, std::string_view description) {
        output << "  ";
        highlight(name, BoldYellow);
        output << description << '\n';
    };
    highlight("Usage:", BoldCyan);
    output << "\n  " << executable << " [options] <path>...\n" << "  "
        << executable << " --fix [options] <path>...\n\n";
    highlight("Options:", BoldCyan);
    output << '\n';
    option("--fix", "            Fix what can be fixed; check the result.");
    option("--check", "          Check without editing (default; optional).");
    option("--no-spillover", "   Unsupported in this whole-file preview.");
    option("--summary", "        Print only file status lines.");
    option("-j [n]", "         Use n threads, or all cores without n.");
    option("--threads <n>", "  Use n threads (default 1).");
    option("--config <file>", "  Use one configuration file for every input.");
    option(
        "--no-config",
        "      Do not search parent directories for .hit-fmt.");
    option(
        "--changed-hunks <file|->",
        "  Unsupported in this whole-file preview.");
    option(
        "--exclude <regex>",
        " Skip matching files or directories (repeatable).");
    option("-h, --help", "       Show this help.");
    option("-V, --version", "    Show the build version.");
    option("--help-config", "    Show configuration options and examples.");
    option("--generate-config", " Print a preserving config to stdout.");
    output << "\n"
        << "Without --config or --no-config, each source file uses the "
            "nearest .hit-fmt file in its directory or a parent directory.\n"
        << "Input paths may be files or directories.\n"
        << "Directories are searched recursively for C and C++ source "
            "files.\n"
        << "--exclude patterns search paths relative to each input directory; "
            ".hit-fmt Exclude patterns search paths relative to the "
            "config file.\n"
        << "Both use ECMAScript regex; matching directories are skipped.\n"
        << "Files larger than 4 MiB are skipped with a warning.\n"
        << "Experimental whole-file pipeline: changed-hunks/no-spillover and "
            "nonempty CallLikeMacroNames are currently unsupported.\n"
        << "Output is colorized when written to a terminal; set "
            "NO_COLOR to disable it.\n\n";
    highlight("Examples:", BoldCyan);
    output << '\n';
    for (auto example :
         {
            " source.cpp",
            " --fix source.cpp include/",
            " --summary src/ include/",
            " --fix --exclude '(^|/)generated(/|$)' src/",
        })
    {
        output << "  ";
        highlight(executable, BoldGreen);
        output << example << '\n';
    }
}

} // namespace

int main(int argc, char* argv[])
{
    Options options;
    std::optional<StyleResolver> styleResolver;
    try {
        options = ParseOptions(argc, argv);
        if (options.ShowHelp) {
            PrintHelp(std::cout, argv[0], UseTerminalColors());
            return 0;
        }
        if (options.ShowVersion) {
            std::cout << "hit-fmt " << hit_fmt::BuildVersion() << '\n';
            return 0;
        }
        if (options.ShowConfigHelp) {
            hit_fmt::PrintConfigHelp(std::cout, UseTerminalColors());
            return 0;
        }
        if (options.GenerateConfig) {
            hit_fmt::PrintPreserveConfig(std::cout);
            return 0;
        }
        if (options.Paths.empty()) {
            PrintHelp(std::cerr, argv[0], UseTerminalColors(stderr));
            return 1;
        }
        styleResolver.emplace(options);
    }
    catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    bool summaryOnly = options.SummaryOnly;
    bool useColor = UseTerminalColors();
    using hit_fmt::FileResult;
    auto processFile =
        [&] (const std::filesystem::path& path, StyleResolver& resolver) {
        FileResult result;
        std::ostringstream output;
        std::ostringstream errors;
        auto filePath = path.string();
        try {
            auto sourceStorage =
                util::ReadEntireFile(
                    filePath,
                    MaxSourceFileSize,
                    util::SourcePaddingSize);
            util::PaddedSourceView paddedSource{
                .Data = sourceStorage.data(),
                .Size = sourceStorage.size() - util::SourcePaddingSize,
            };
            auto source = paddedSource.View();
            const auto& config = resolver.Resolve(path);
            const auto inputFilePath = path.generic_string();
            const auto checked =
                hit_fmt::CheckFile(paddedSource, config, inputFilePath);
            if (options.Fix && checked.FormattingMismatch) {
                util::WriteEntireFile(filePath, checked.Formatting.Text);
                if (summaryOnly) {
                    if (useColor) {
                        output << BoldGreen;
                    }
                    output << filePath << ": fixed formatting";
                    if (useColor) {
                        output << ResetColor;
                    }
                    output << '\n';
                } else {
                    PrintFormattingDiagnostics(
                        output,
                        filePath,
                        source,
                        checked.Formatting.Text,
                        useColor,
                        true);
                }
            }
            const auto failed = checked.HasFailures(options.Fix);
            if (summaryOnly && failed) {
                if (useColor) {
                    output << BoldRed;
                }
                output << filePath << ": ";
                hit_fmt::PrintFileSummaryIssues(output, checked, options.Fix);
                if (useColor) {
                    output << ResetColor;
                }
                output << '\n';
            } else if (!summaryOnly) {
                if (!options.Fix && checked.FormattingMismatch) {
                    PrintFormattingDiagnostics(
                        output,
                        filePath,
                        source,
                        checked.Formatting.Text,
                        useColor);
                }
                hit_fmt::PrintFileIssues(
                    output,
                    filePath,
                    checked,
                    options.Fix);
            }
            result.Mismatches += failed;
        }
        catch (const util::FileTooLargeError &error) {
            errors << "warning: " << error.what() << "; skipping\n";
        }
        catch (const std::exception &error) {
            errors << filePath << ": " << error.what() << '\n';
            result.Failed = true;
        }
        result.Output = output.str();
        result.Error = errors.str();
        return result;
    };

    size_t mismatchCount = 0;
    auto printResult = [&] (const FileResult& result) {
        std::cout << result.Output;
        std::cout.flush();
        std::cerr << result.Error;
        mismatchCount += result.Mismatches;
        return result.Failed;
    };
    if (options.Threads == 1) {
        try {
            if (!ForEachFile(
                    options.Paths,
                    options.Exclusions,
                    [
                        &] (
                        const auto& path,
                        bool directory) { return styleResolver
                            ->IsExcluded(path, directory); },
                    [&] (const auto& path) {
                        bool failed =
                            printResult(processFile(path, *styleResolver));
                        return !failed;
                    }))
            {
                return 1;
            }
        }
        catch (const std::exception &error) {
            std::cerr << error.what() << '\n';
            return 1;
        }
    } else {
        if (!hit_fmt::RunParallelFiles(
                options.Threads,
                [&] (auto visit) {
                    return ForEachFile(
                        options.Paths,
                        options.Exclusions,
                        [
                            &] (
                            const auto& path,
                            bool directory) { return styleResolver
                                ->IsExcluded(path, directory); },
                        visit);
                },
                [&] { return StyleResolver(options); },
                processFile,
                printResult,
                std::cerr))
        {
            return 1;
        }
    }
    if (useColor) {
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
