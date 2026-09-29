#include "config_help.hpp"
#include "config_template.hpp"
#include "version.hpp"

#include <formatter/config.hpp>
#include <formatter/formatter.hpp>
#include <formatter/hunks.hpp>
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
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <optional>
#include <regex>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
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
    const std::vector<std::string_view>& cells,
    size_t begin,
    size_t end)
{
    for (auto i = begin; i < end; ++i) {
        std::cout << cells[i];
    }
}

struct Excerpt
{
    size_t CaretOffset;
    bool CaretVisible;
};

Excerpt PrintSourceExcerpt(
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
        begin = std::min(
            cells.size() - window,
            rendered.Caret > window / 2 ? rendered.Caret - window / 2 : 0);
    }
    auto count = cells.size() > available
        ? (fromStart ? available - 3 : window)
        : available;
    auto end = std::min(cells.size(), begin + count);
    if (begin) {
        std::cout << "...";
    }
    PrintCells(cells, begin, end);
    if (end < cells.size()) {
        std::cout << "...";
    }
    std::cout << '\n';
    return {.CaretOffset = (begin ? 3 : 0) +
        std::min(rendered.Caret, end) - begin,
        .CaretVisible = rendered.Caret < end ||
            (rendered.Caret == end && end == cells.size())};
}

void PrintWrappedMessage(
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
        std::cout << "  " << message.substr(0, length) << '\n';
        message.remove_prefix(length);
        while (message.starts_with(' ')) {
            message.remove_prefix(1);
        }
        if (!message.empty()) {
            std::cout << prefix;
        }
    }
}

void PrintSuggestedLine(
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
            std::cout << Green;
        }
        if (begin) {
            std::cout << std::string(gutterWidth, ' ');
        } else {
            std::cout << std::string(gutterWidth - 3, ' ') << " + ";
        }
        auto end = std::min(cells.size(), begin + available);
        PrintCells(cells, begin, end);
        if (useColor) {
            std::cout << ResetColor;
        }
        std::cout << '\n';
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
    size_t SourceBegin = 0;
    size_t SourceEnd = 0;
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
    return 1 + std::count_if(
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
    auto other = std::count_if(gap.begin(), gap.end(), [] (unsigned char c) {
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
            violations.push_back({line,
                ColumnAt(actual, a),
                SpacingMessage(actual, expected, startA, a, startB, b)});
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
            expected[expected.size() - suffix - 1])
    {
        ++suffix;
    }
    while (suffix && ((static_cast<unsigned char>(
        actual[actual.size() - suffix]) & 0xc0) == 0x80 ||
        (static_cast<unsigned char>(expected[expected.size() - suffix]) &
            0xc0) == 0x80))
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

std::optional<size_t> SlashRulerLength(
    std::string_view actual,
    std::string_view expected,
    size_t length)
{
    if (length == 0) {
        return std::nullopt;
    }
    const auto indent = actual.find_first_not_of(" \t");
    if (indent == std::string_view::npos) {
        return std::nullopt;
    }
    auto ruler = actual.substr(indent);
    if (ruler.ends_with('\r') && expected.ends_with('\r')) {
        ruler.remove_suffix(1);
        expected.remove_suffix(1);
    }
    if (ruler.size() < 8 || expected.size() != length ||
        ruler.find_first_not_of('/') != std::string_view::npos ||
        expected.find_first_not_of('/') != std::string_view::npos)
    {
        return std::nullopt;
    }
    return ruler.size();
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
            auto lineIndex = static_cast<size_t>(
                std::upper_bound(lineStarts.begin(), lineStarts.end(), a) -
                lineStarts.begin() - 1);
            auto byte = std::min(
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
                message = "join lines before '" + token + "' with " +
                    DescribeWhitespace(gapB);
            } else if (oldBreaks != newBreaks) {
                auto difference = oldBreaks > newBreaks
                    ? oldBreaks - newBreaks
                    : newBreaks - oldBreaks;
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
                message = SpacingMessage(
                    original,
                    replacement,
                    startA,
                    a,
                    startB,
                    b);
            }
            violations.push_back({firstLine + lineIndex,
                ColumnAt(source[lineIndex].Text, byte),
                std::move(message),
                ViolationKind::Reflow});
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
        (std::isspace(static_cast<unsigned char>(text[8])) ||
            text[8] == '<' || text[8] == '"');
}

std::vector<Chunk> FormattingChunks(
    const std::vector<Line>& source,
    const std::vector<Line>& formatted,
    size_t separatorLength = 0)
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
                    auto skew = sourceSkip > formattedSkip
                        ? sourceSkip - formattedSkip
                        : formattedSkip - sourceSkip;
                    // An unchanged line inside a repeated block can look
                    // closer than the actual end of a whitespace-only edit.
                    // Prefer an anchor that keeps all intervening tokens.
                    bool preservesTokens = sourceSkip && formattedSkip &&
                        SameNonWhitespaceLines(
                            std::span(source).subspan(sourceIndex, sourceSkip),
                            std::span(formatted).subspan(
                                formattedIndex,
                                formattedSkip));
                    if ((preservesTokens && !bestPreservesTokens) ||
                        (preservesTokens == bestPreservesTokens &&
                            (distance < bestDistance ||
                                (distance == bestDistance && skew < bestSkew))))
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
        Chunk chunk{.Begin = begin,
            .End = std::max(begin + 1, sourceIndex + sourceCount + 1),
            .SourceBegin = sourceIndex,
            .SourceEnd = sourceIndex + sourceCount,
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
                ? formatted[formattedIndex + i].Text
                : std::string_view{};
            if (i < formattedCount && line == formatted[formattedIndex + i]) {
                continue;
            }
            std::string message;
            auto kind = ViolationKind::Other;
            auto column = DifferenceColumn(line.Text, replacement);
            auto rulerLength = i < formattedCount
                ? SlashRulerLength(line.Text, replacement, separatorLength)
                : std::nullopt;
            if (rulerLength) {
                const auto indent = line.Text.find_first_not_of(" \t");
                if (indent != 0) {
                    message = "separator should start at column 1";
                }
                if (*rulerLength != separatorLength) {
                    message += message.empty()
                        ? "separator should be "
                        : " and be ";
                    message += std::to_string(separatorLength) +
                        " characters long (found " +
                        std::to_string(*rulerLength) + ")";
                }
                column = indent + 1;
            } else if (line.Text == replacement && !line.HasNewline &&
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
                    message =
                        "merge this line into the preceding formatted line";
                } else if (formattedCount > sourceCount) {
                    message = "split line: expected " +
                        QuoteSnippet(replacement) +
                        " on this line";
                    kind = ViolationKind::Reflow;
                } else {
                    message = "join lines: expected " +
                        QuoteSnippet(replacement) +
                        " on this line";
                    kind = ViolationKind::Reflow;
                }
            } else if (SameNonWhitespace(line.Text, replacement)) {
                auto spacing = SpacingViolations(
                    sourceIndex + i + 1,
                    line.Text,
                    replacement);
                chunk.Violations.insert(
                    chunk.Violations.end(),
                    spacing.begin(),
                    spacing.end());
                continue;
            } else {
                message = ContentChange(line.Text, replacement);
            }
            chunk.Violations.push_back({sourceIndex + i + 1,
                column,
                std::move(message),
                kind});
        }
        if (sourceCount == 0) {
            auto column = sourceIndex < source.size() || source.empty()
                ? size_t{1}
                : ColumnAt(source.back().Text, source.back().Text.size());
            auto location = sourceIndex == source.size()
                ? " at end of file"
                : " before this line";
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
            auto column = ColumnAt(
                source[number - 1].Text,
                source[number - 1].Text.size());
            auto inserted = formattedCount > sourceCount
                ? formattedCount - sourceCount
                : size_t{0};
            chunk.Violations.push_back({number, column,
                inserted
                    ? "insert " + std::to_string(inserted) +
                    " formatted lines after this line"
                    : "replace lines following this line with "
                        "formatted output"});
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

void PrintCheckDiagnostics(
    std::string_view path,
    std::string_view source,
    std::string_view formatted,
    std::string_view layoutFormatted,
    std::span<const formatter::NameDiagnostic> names,
    std::span<const formatter::ParameterNameDiagnostic> parameters,
    std::span<const formatter::LayoutDiagnostic> layout,
    bool useColor,
    size_t separatorLength,
    bool fixed = false)
{
    auto sourceLines = SplitLines(source);
    auto formattedLines = SplitLines(formatted);
    auto chunks = FormattingChunks(
        sourceLines,
        formattedLines,
        separatorLength);
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
            .Violations = {{name.Position.Line,
                name.Position.Column,
                name.Message + "; " + name.Expected}}});
    }
    for (const auto& parameter : parameters) {
        auto message = "parameter #" +
            std::to_string(parameter.ParameterNumber) +
            " in " + parameter.Function + ": expected a name";
        if (parameter.AllowCommentedName) {
            message += " or /*name*/ comment";
        }
        chunks.push_back({.Begin = parameter.SignatureBeginLine,
            .End = parameter.SignatureEndLine + 1,
            .Violations = {{parameter.Position.Line,
                parameter.Position.Column,
                std::move(message)}}});
    }
    for (const auto& issue : layout) {
        chunks.push_back({.Begin = issue.Position.Line,
            .End = issue.Position.Line + 1,
            .Violations = {{issue.Position.Line, issue.Position.Column,
                issue.Message, issue.Kind ==
                    formatter::LayoutDiagnosticKind::LineWidth
                    ? ViolationKind::LineWidth
                    : ViolationKind::Other}},
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
            }))
            {
                continue;
            }
            Chunk suggestion{.Begin = candidate.Begin,
                .End = candidate.End,
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
            previous.Violations.insert(
                previous.Violations.end(),
                std::make_move_iterator(chunk.Violations.begin()),
                std::make_move_iterator(chunk.Violations.end()));
            if (chunk.HasFix) {
                if (previous.HasFix) {
                    previous.Fix.insert(
                        previous.Fix.end(),
                        formattedLines.begin() + previous.FormattedEnd,
                        formattedLines.begin() + chunk.FormattedBegin);
                } else {
                    previous.FormattedBegin = chunk.FormattedBegin;
                }
                previous.HasFix = true;
                previous.FormattedEnd = chunk.FormattedEnd;
                previous.Fix.insert(
                    previous.Fix.end(),
                    chunk.Fix.begin(),
                    chunk.Fix.end());
            }
            if (chunk.HasLayoutFix) {
                if (previous.HasLayoutFix) {
                    previous.LayoutFix.insert(
                        previous.LayoutFix.end(),
                        layoutLines.begin() + previous.LayoutFormattedEnd,
                        layoutLines.begin() + chunk.LayoutFormattedBegin);
                } else {
                    previous.LayoutFormattedBegin = chunk.LayoutFormattedBegin;
                }
                previous.HasLayoutFix = true;
                previous.LayoutFormattedEnd = chunk.LayoutFormattedEnd;
                previous.LayoutFix.insert(
                    previous.LayoutFix.end(),
                    chunk.LayoutFix.begin(),
                    chunk.LayoutFix.end());
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
        if (useColor) {
            std::cout << (fixed ? BoldGreen : BoldRed);
        }
        std::cout << (fixed ? "fixed: " : "fail: ")
            << chunk.Violations.size() << " violation"
                  << (chunk.Violations.size() == 1 ? "" : "s");
        if (useColor) {
            std::cout << ResetColor;
        }
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
        std::cout << gutter.substr(0, gutter.size() - 1) << '\n';
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
            const auto prefix = std::string(
                width - std::to_string(lineNumber).size(),
                ' ') + std::to_string(lineNumber) + " | ";
            auto printLine = [&] (size_t column, bool fromStart = false) {
                std::cout << prefix;
                return PrintSourceExcerpt(
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
                auto excerpt = printLine(
                    violation.Column,
                    violation.Kind == ViolationKind::LineWidth);
                std::cout << gutter;
                if (!excerpt.CaretVisible) {
                    // Do not imply that the cropped end of a long line is
                    // the exact token at ColumnLimit + 1.
                    PrintWrappedMessage(
                        violation.Message,
                        gutter,
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
                    PrintWrappedMessage(
                        violation.Message,
                        gutter,
                        terminalWidth);
                }
            }
        }
        if (chunk.HasFix || chunk.HasLayoutFix) {
            std::cout << (fixed
                ? "  applied formatting:\n"
                : "  suggested formatting:\n");
            const auto& fix = chunk.HasLayoutFix ? chunk.LayoutFix : chunk.Fix;
            for (const auto& line : fix) {
                PrintSuggestedLine(
                    line.Text,
                    terminalWidth,
                    gutter.size(),
                    useColor);
                if (!line.HasNewline) {
                    std::cout << std::string(gutter.size(), ' ')
                              << "\\ No newline at end of file\n";
                }
            }
            if (fix.empty()) {
                std::cout << "  (remove this chunk)\n";
            }
        } else if (!chunk.HasLayoutIssue) {
            std::cout << "  ";
            PrintWrappedMessage(
                "hint: names are not changed by the formatter.",
                "  ",
                terminalWidth);
        }
        if (chunk.HasLayoutIssue && !chunk.HasLayoutFix) {
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

using formatter::Hunk;
using formatter::FormatHunks;
using formatter::HunkWindows;
using formatter::InHunks;

struct Options
{
    bool Fix = false;
    bool NoSpillover = false;
    bool SummaryOnly = false;
    bool DisableConfig = false;
    bool ShowHelp = false;
    bool ShowVersion = false;
    bool ShowConfigHelp = false;
    bool GenerateConfig = false;
    std::optional<std::filesystem::path> ConfigPath;
    std::optional<std::string> HunkInput;
    std::unordered_map<std::string, std::vector<Hunk>> Hunks;
    std::vector<std::regex> Exclusions;
    std::vector<std::filesystem::path> Paths;
};

Options ParseOptions(int argc, char* argv[])
{
    Options options;
    bool positionalOnly = false;
    for (int index = 1; index < argc; ++index) {
        std::string_view argument(argv[index]);
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
            // Retained as an alias for the default check behavior.
        } else if (argument == "--no-spillover") {
            options.NoSpillover = true;
        } else if (argument == "--summary") {
            options.SummaryOnly = true;
        } else if (argument == "--no-config") {
            options.DisableConfig = true;
        } else if (argument == "--changed-hunks" ||
            argument == "--changed-ranges")
        {
            if (++index == argc || options.HunkInput) {
                throw std::runtime_error(
                    "--changed-hunks requires one input file or -");
            }
            options.HunkInput = argv[index];
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
            throw std::runtime_error("unknown option: " +
                std::string(argument));
        }
    }
    if (options.DisableConfig && options.ConfigPath) {
        throw std::runtime_error("--config and --no-config cannot be combined");
    }
    if (options.HunkInput && !options.Paths.empty()) {
        throw std::runtime_error(
            "--changed-hunks cannot be combined with input paths");
    }
    if (options.NoSpillover && !options.HunkInput) {
        throw std::runtime_error("--no-spillover requires --changed-hunks");
    }
    return options;
}

std::vector<Hunk> RemapHunks(
    const parser::SyntaxTree& beforeTree,
    const parser::SyntaxTree& afterTree,
    std::span<const Hunk> hunks)
{
    bool sameTokens = beforeTree.TokenCount() == afterTree.TokenCount();
    for (parser::TokenIndex i = 0;
        sameTokens && i < beforeTree.TokenCount();
        ++i)
    {
        sameTokens = beforeTree.Kind(i) == afterTree.Kind(i) &&
            beforeTree.TokenText(i) == afterTree.TokenText(i);
    }
    auto sourceLines = SplitLines(beforeTree.Source());
    auto formattedLines = SplitLines(afterTree.Source());
    auto chunks = FormattingChunks(sourceLines, formattedLines);
    auto mapLine = [&] (size_t line) {
        if (sameTokens && beforeTree.TokenCount() != 0) {
            for (parser::TokenIndex i = 0; i < beforeTree.TokenCount(); ++i) {
                auto original = beforeTree.TokenPosition(i).Line;
                if (original >= line) {
                    auto updated = afterTree.TokenPosition(i).Line;
                    return original == line ? size_t{updated} :
                        std::max(size_t{1}, size_t{updated} -
                            (original - line));
                }
            }
            auto last = beforeTree.TokenCount() - 1;
            return size_t{afterTree.TokenPosition(last).Line} +
                line - beforeTree.TokenPosition(last).Line;
        }
        size_t sourceCursor = 0;
        size_t formattedCursor = 0;
        size_t index = line - 1;
        for (const auto& chunk : chunks) {
            if (index < chunk.SourceBegin) {
                return formattedCursor + (index - sourceCursor) + 1;
            }
            if (index < chunk.SourceEnd) {
                auto count = chunk.FormattedEnd - chunk.FormattedBegin;
                if (count == 0) {
                    return std::max(size_t{1}, chunk.FormattedBegin);
                }
                return chunk.FormattedBegin +
                    std::min(index - chunk.SourceBegin, count - 1) + 1;
            }
            sourceCursor = chunk.SourceEnd;
            formattedCursor = chunk.FormattedEnd;
        }
        return formattedCursor + (index - sourceCursor) + 1;
    };
    std::vector<Hunk> mapped;
    mapped.reserve(hunks.size());
    auto limit = std::max(size_t{1}, formattedLines.size());
    for (const auto& hunk : hunks) {
        auto begin = std::min(mapLine(hunk.Begin), limit);
        auto end = std::min(mapLine(hunk.End), limit);
        mapped.push_back({begin, std::max(begin, end)});
    }
    return mapped;
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
        if (config.Settings.Exclude.empty()) {
            return false;
        }
        auto relative = std::filesystem::absolute(path).lexically_normal()
                            .lexically_relative(config.Directory);
        return ::IsExcluded(relative, config.Settings.Exclude);
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

void PrintHelp(
    std::ostream& output,
    std::string_view executable,
    bool useColor)
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
    output << "\n  " << executable << " [options] <path>...\n"
           << "  " << executable << " --fix [options] <path>...\n\n";
    highlight("Options:", BoldCyan);
    output << '\n';
    option("--fix", "            Fix what can be fixed; check the result.");
    option("--check", "          Check without editing (default; optional).");
    option("--no-spillover", "   Restrict fixes to the selected hunk lines.");
    option("--summary", "        Print only file status lines.");
    option("--config <file>", "  Use one configuration file for every input.");
    option(
        "--no-config",
        "      Do not search parent directories for .hit-fmt.");
    option("--changed-hunks <file|->", "  Check hunks from a file or stdin.");
    option(
        "--exclude <regex>",
        " Skip matching files or directories (repeatable).");
    option("-h, --help", "       Show this help.");
    option("-V, --version", "    Show the build version.");
    option("--help-config", "    Show configuration options and examples.");
    option("--generate-config", " Print a preserving config to stdout.");
    output
        << "\n"
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
        << "Changed ranges use 'file <path>' followed by one or more\n"
           "'hunk <first>:<last>' lines (inclusive, current-file lines).\n"
        << "Paths are relative to the current working directory.\n"
        << "Output is colorized when written to a terminal; set "
           "NO_COLOR to disable it.\n\n";
    highlight("Examples:", BoldCyan);
    output << '\n';
    for (auto example : {
        " source.cpp",
        " --fix source.cpp include/",
        " --summary src/ include/",
        " --fix --exclude '(^|/)generated(/|$)' src/",
        " --fix --changed-hunks - < hunks.txt"})
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
        if (options.HunkInput) {
            formatter::ChangedHunkFiles changed;
            if (*options.HunkInput == "-") {
                changed = formatter::ParseChangedHunks(
                    std::cin,
                    std::filesystem::current_path());
            } else {
                std::ifstream input(*options.HunkInput);
                if (!input) {
                    throw std::runtime_error("cannot open changed ranges: " +
                        *options.HunkInput);
                }
                changed = formatter::ParseChangedHunks(
                    input,
                    std::filesystem::current_path());
            }
            options.Paths = std::move(changed.Paths);
            options.Hunks = std::move(changed.Hunks);
        }
        if (options.Paths.empty() && !options.HunkInput) {
            PrintHelp(std::cerr, argv[0], UseTerminalColors(stderr));
            return 1;
        }
        styleResolver.emplace(options);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    bool summaryOnly = options.SummaryOnly;
    bool useColor = UseTerminalColors();
    size_t mismatchCount = 0;
    auto checkFile = [&] (const std::filesystem::path& path) {
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
            std::span<const Hunk> hunks;
            if (options.HunkInput) {
                auto absolute =
                    std::filesystem::absolute(path).lexically_normal();
                hunks = options.Hunks.at(absolute.string());
                auto lastLine = std::max(size_t{1}, SplitLines(source).size());
                for (const auto& hunk : hunks) {
                    if (hunk.End > lastLine) {
                        throw std::runtime_error("hunk exceeds file length");
                    }
                }
            }
            const auto& config = styleResolver->Resolve(path);
            auto tree = parser::Parse(
                paddedSource,
                [] (parser::ParseError) { });
            // Check the actual text, never a speculative expression reflow.
            // Fixes may reflow, but must be checked again after writing.
            auto inspect = [&] (const parser::SyntaxTree& current,
                std::span<const Hunk> selected) {
                    auto checkStyle = config.FormattingStyle;
                    checkStyle.ExpressionLayoutCondition =
                    formatter::ExpressionLayoutCondition::Preserve;
                    auto windows = options.HunkInput
                        ? HunkWindows(current, selected, true)
                        : std::vector<Hunk>{};
                    auto formatted = formatter::Format(current, checkStyle);
                    bool unsupported = false;
                    if (options.HunkInput) {
                        formatted = FormatHunks(
                            current,
                            formatted,
                            windows,
                            &unsupported,
                            true);
                    }
                    auto names = formatter::CheckNames(current, config);
                    auto parameters = formatter::CheckParameterNames(
                        current,
                        config);
                    auto layout = formatter::CheckExpressionLayout(
                        current,
                        config.FormattingStyle);
                    if (options.HunkInput) {
                        std::erase_if(names, [&] (const auto& issue) {
                            return !InHunks(
                                issue.Position.Line,
                                issue.Position.Line,
                                selected);
                        });
                        std::erase_if(parameters, [&] (const auto& issue) {
                            return !InHunks(
                                issue.Position.Line,
                                issue.Position.Line,
                                selected);
                        });
                        std::erase_if(layout, [&] (const auto& issue) {
                            return !InHunks(
                                issue.Position.Line,
                                issue.Position.Line,
                                windows);
                        });
                    }
                    bool mismatch = formatted !=
                        current.Source() || unsupported ||
                    !names.empty() || !parameters.empty() || !layout.empty();
                    if (summaryOnly && mismatch) {
                        if (useColor) {
                            std::cout << BoldRed;
                        }
                        std::cout << filePath << ": ";
                        if (formatted != current.Source()) {
                            std::cout << "formatting mismatch";
                        } else if (unsupported) {
                            std::cout << "fail: unsafe structural edit";
                        } else if (!layout.empty()) {
                            std::cout << "fail: layout violation";
                            if (layout.size() != 1) {
                                std::cout << 's';
                            }
                        } else if (!names.empty()) {
                            std::cout << "fail: invalid name";
                        } else {
                            std::cout << "fail: unnamed parameter";
                        }
                        if (useColor) {
                            std::cout << ResetColor;
                        }
                        std::cout.put('\n');
                    } else if (!summaryOnly && mismatch) {
                        auto layoutFormatted = layout.empty() ? std::string{} :
                        formatter::Format(current, config.FormattingStyle);
                        if (options.HunkInput && !layout.empty()) {
                            layoutFormatted = FormatHunks(
                                current,
                                layoutFormatted,
                                windows,
                                nullptr,
                                true);
                        }
                        if (formatted != current.Source() || !names.empty() ||
                        !parameters.empty() || !layout.empty())
                    {
                            PrintCheckDiagnostics(
                                filePath,
                                current.Source(),
                                formatted,
                                layoutFormatted,
                                names,
                                parameters,
                                layout,
                                useColor,
                                checkStyle.SeparatorLength);
                        }
                        if (unsupported) {
                            std::cout << "fail: unsafe structural edit\n"
                                "  --> " << filePath << ':'
                                << selected.front().Begin << ":1\n";
                        }
                    }
                    return mismatch;
                };

            if (!options.Fix) {
                mismatchCount += inspect(tree, hunks);
                std::cout.flush();
                return true;
            }

            auto applied = options.HunkInput
                ? formatter::FormatChanged(
                    tree,
                    config.FormattingStyle,
                    hunks,
                    !options.NoSpillover)
                : formatter::Format(tree, config.FormattingStyle);
            if (applied != source) {
                util::WriteEntireFile(filePath, applied);
                if (summaryOnly) {
                    if (useColor) {
                        std::cout << BoldGreen;
                    }
                    std::cout << filePath << ": fixed formatting";
                    if (useColor) {
                        std::cout << ResetColor;
                    }
                    std::cout.put('\n');
                } else {
                    PrintCheckDiagnostics(
                        filePath,
                        source,
                        applied,
                        {},
                        {},
                        {},
                        {},
                        useColor,
                        config.FormattingStyle.SeparatorLength,
                        true);
                }
            }
            if (applied == source) {
                mismatchCount += inspect(tree, hunks);
            } else {
                applied.append(util::SourcePaddingSize, '\0');
                auto updated = parser::Parse(
                    {.Data = applied.data(),
                        .Size = applied.size() - util::SourcePaddingSize},
                    [] (parser::ParseError) { });
                auto mapped = options.HunkInput
                    ? RemapHunks(tree, updated, hunks)
                    : std::vector<Hunk>{};
                mismatchCount += inspect(updated, mapped);
            }
        } catch (const util::FileTooLargeError& error) {
            std::cerr << "warning: " << error.what() << "; skipping\n";
        } catch (const std::exception& error) {
            std::cerr << filePath << ": " << error.what() << '\n';
            return false;
        }
        std::cout.flush();
        return true;
    };
    try {
        if (!ForEachFile(
                options.Paths,
                options.Exclusions,
                [&] (const auto& path, bool directory) {
                    return styleResolver->IsExcluded(path, directory);
                },
                checkFile))
        {
            return 1;
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
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
