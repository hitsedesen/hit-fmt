#include "formatting_diagnostics.hpp"

#include "source_excerpt.hpp"

#include <unicode/utf8.hpp>

#include <algorithm>
#include <cctype>
#include <ostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace hit_fmt {

namespace {

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
    auto cells = RenderLine(text, gutterWidth).Cells;
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
            case '\n':
                quoted += "\\n";
                break;
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

struct Violation
{
    util::Position Position;
    std::string Message;
};

struct Chunk
{
    size_t Begin;
    size_t End;
    size_t FirstChange;
    size_t LastChange;
    std::vector<Violation> Violations;
};

std::string ChangeMessage(
    std::string_view source,
    const formatter::FormatResult& result,
    const formatter::FormatChange& change)
{
    const auto& lines = result.Changes->OriginalLines;
    const auto original = source.substr(change.Original.Begin, change.Original.End - change.Original.Begin);
    const auto formatted =
        std::string_view(result.Text).substr(change.Output.Begin, change.Output.End - change.Output.Begin);

    if (change.Kind == formatter::FormatChangeKind::Content) {
        if (original.empty()) {
            return "insert " + QuoteSnippet(formatted) + " here";
        }

        if (formatted.empty()) {
            return "remove " + QuoteSnippet(original) + " here";
        }

        return "replace " + QuoteSnippet(original) + " with " + QuoteSnippet(formatted);
    }

    const auto line = lines.LineAt(change.Original.End);
    const auto text = lines.Text(source, line);
    const auto range = lines.Range(line);
    const auto offset = std::min<size_t>(change.Original.End - range.Begin, text.size());
    const auto token = TokenAt(text, offset);
    const auto oldBreaks = std::count(original.begin(), original.end(), '\n');
    const auto newBreaks = std::count(formatted.begin(), formatted.end(), '\n');

    if (change.Original.End == source.size()) {
        if (!oldBreaks && newBreaks == 1 && formatted.ends_with('\n')) {
            return "missing final newline";
        }

        if (oldBreaks == 1 && !newBreaks && formatted.empty()) {
            return "unexpected final newline";
        }
    }

    if (!oldBreaks && newBreaks) {
        auto message = newBreaks == 1
            ? std::string("insert a line break")
            : "insert " + std::to_string(newBreaks) + " line breaks";
        message += " before '" + token + "'";
        const auto indent = formatted.substr(formatted.rfind('\n') + 1);

        if (!indent.empty()) {
            message += " and indent by " + DescribeWhitespace(indent);
        }

        return message;
    }

    if (oldBreaks && !newBreaks) {
        return "join lines before '" + token + "' with " + DescribeWhitespace(formatted);
    }

    if (oldBreaks != newBreaks) {
        const auto difference = oldBreaks > newBreaks ? oldBreaks - newBreaks : newBreaks - oldBreaks;
        return (oldBreaks > newBreaks ? "remove " : "insert ") + std::to_string(difference) +
            (difference == 1 ? " blank line" : " blank lines") + " before '" + token + "'";
    }

    const auto wanted = DescribeWhitespace(formatted);
    const auto found = DescribeWhitespace(original);

    if (change.Original.Begin == range.Begin) {
        return "indentation: expected " + wanted + ", found " + found;
    }

    if (offset == text.size()) {
        return "trailing whitespace: expected " + wanted + ", found " + found;
    }

    return "expected " + wanted + " before '" + token + "', found " + found;
}

std::vector<Chunk> DiagnosticChunks(std::string_view source, const formatter::FormatResult& result)
{
    const auto& changes = *result.Changes;
    std::vector<Chunk> chunks;
    size_t positionLine = 0;
    size_t cursor = 0;
    uint32_t column = 1;
    // Change locations are monotonic. A wide line's prefix is decoded once,
    // even when it contains many separate spacing violations.
    const auto positionAt = [&] (parser::SourceOffset offset) {
        const auto line = changes.OriginalLines.LineAt(offset);
        const auto range = changes.OriginalLines.Range(line);

        if (line != positionLine) {
            positionLine = line;
            cursor = range.Begin;
            column = 1;
        }
        const auto end = std::min<size_t>(offset, range.Begin + changes.OriginalLines.Text(source, line).size());
        while (cursor < end) {
            cursor += unicode::utf8::GetFirstCodePointSize(source.substr(cursor, end - cursor)).value_or(1);
            ++column;
        }

        return util::Position{static_cast<uint32_t>(line), column};
    };
    for (size_t i = 0; i < changes.Edits.size(); ++i) {
        const auto& change = changes.Edits[i];
        const auto offset = change.Kind == formatter::FormatChangeKind::Whitespace
            ? change.Original.End
            : change.Original.Begin;
        const auto position = positionAt(offset);
        const auto begin = changes.OriginalLines.LineAt(change.Original.Begin);
        const auto end = std::max<size_t>(
            position.Line,
            changes.OriginalLines.LineAt(change.Original.Empty() ? change.Original.End : change.Original.End - 1)) + 1;
        if (chunks.empty() || begin > chunks.back().End) {
            chunks.push_back({begin, end, i, i, {}});
        } else {
            chunks.back().End = std::max(chunks.back().End, end);
            chunks.back().LastChange = i;
        }

        if (change.Kind != formatter::FormatChangeKind::IncludeOrder) {
            chunks.back().Violations.push_back({position, ChangeMessage(source, result, change)});
        }
    }
    return chunks;
}

} // namespace

void PrintFormattingDiagnostics(
    std::ostream& output,
    std::string_view path,
    std::string_view source,
    const formatter::FormatResult& result,
    const Terminal& terminal,
    bool fixed)
{
    if (!result.Changes) {
        throw std::invalid_argument("formatting diagnostics require FormatRequest::Changes");
    }

    const auto& changes = *result.Changes;
    auto chunks = DiagnosticChunks(source, result);

    for (auto& chunk : chunks) {
        if (chunk.Violations.empty()) {
            continue;
        }

        const auto first = chunk.Violations.front().Position;
        output << (fixed ? terminal.BoldGreen : terminal.BoldRed);
        output << (fixed ? "fixed: " : "fail: ") << chunk.Violations.size() << " violation"
            << (chunk.Violations.size() == 1 ? "" : "s") << terminal.Reset;
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
            auto text = changes.OriginalLines.Text(source, lineNumber);

            if (text.ends_with('\r')) {
                text.remove_suffix(1);
            }

            const auto prefix =
                std::string(width - std::to_string(lineNumber).size(), ' ') + std::to_string(lineNumber) + " | ";
            const auto rendered = RenderLine(text, prefix.size());
            const auto printLine = [&] (size_t column) {
                output << prefix;
                return PrintSourceExcerpt(output, rendered, column, prefix.size(), terminal.Width);
            };

            if (violationIndex == chunk.Violations.size() ||
                chunk.Violations[violationIndex].Position.Line != lineNumber)
            {
                printLine(0);
            }

            while (violationIndex < chunk.Violations.size() &&
                chunk.Violations[violationIndex].Position.Line == lineNumber)
            {
                const auto& violation = chunk.Violations[violationIndex++];
                const auto excerpt = printLine(violation.Position.Column);
                output << gutter;

                if (!excerpt.CaretVisible) {
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

        // Full-line context is transported through unchanged prefix/suffix
        // bytes around the first/last replacement, without another text diff.
        const auto& firstChange = changes.Edits[chunk.FirstChange];
        const auto& lastChange = changes.Edits[chunk.LastChange];
        const auto prefix = firstChange.Original.Begin - changes.OriginalLines.Range(chunk.Begin).Begin;
        const auto suffix = changes.OriginalLines.Range(chunk.End - 1).End - lastChange.Original.End;

        if (prefix > firstChange.Output.Begin || suffix > result.Text.size() - lastChange.Output.End) {
            throw std::runtime_error("invalid formatting diagnostic context");
        }

        const auto begin = firstChange.Output.Begin - prefix;
        const auto end = lastChange.Output.End + suffix;
        output << (fixed ? "  applied formatting:\n" : "  suggested formatting:\n");

        if (begin == end) {
            output << "  (remove this chunk)\n";
            continue;
        }

        const auto firstLine = changes.OutputLines.LineAt(begin);
        const auto lastLine = changes.OutputLines.LineAt(end - 1);

        for (auto line = firstLine; line <= lastLine; ++line) {
            PrintSuggestedLine(output, changes.OutputLines.Text(result.Text, line), gutter.size(), terminal);
            const auto range = changes.OutputLines.Range(line);

            if (range.End && result.Text[range.End - 1] != '\n') {
                output << std::string(gutter.size(), ' ') << "\\ No newline at end of file\n";
            }
        }
    }
}

} // namespace hit_fmt
