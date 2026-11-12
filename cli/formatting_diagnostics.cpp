#include "formatting_diagnostics.hpp"

#include "source_excerpt.hpp"

#include <util/memory.hpp>
#include <util/types.hpp>

#include <unicode/utf8.hpp>

#include <algorithm>
#include <cctype>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

namespace cli {

namespace {

void PrintWrappedMessage(std::ostream& output, std::string_view message, std::string_view prefix, us terminalWidth)
{
    auto available = terminalWidth > prefix.size() + 2 ? terminalWidth - prefix.size() - 2 : us{20};

    while (!message.empty()) {
        auto length = std::min(available, message.size());

        if (length < message.size()) {
            auto space = message.rfind(' ', length);

            if (space != std::string_view::npos && space > 0) {
                length = space;
            }
        }

        while (length && length < message.size() && (static_cast<u8>(message[length]) & 0xc0) == 0x80) {
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

void PrintSuggestedLine(std::ostream& output, std::string_view text, us gutterWidth, const Terminal& terminal)
{
    Arena scratch;
    auto cells = RenderLine(text, gutterWidth, &scratch).Cells;
    auto available = terminal.Width > gutterWidth + 1 ? terminal.Width - gutterWidth - 1 : us{16};

    for (us begin = 0;;) {
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
    auto append = [&] (us count, std::string_view name) {
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
        [] (u8 c) {
            return c != ' ' && c != '\t' && c != '\r';
        });
    append(other, "other whitespace character");
    return description;
}

std::string TokenAt(std::string_view text, us offset)
{
    if (offset == text.size()) {
        return "end of line";
    }

    auto c = static_cast<u8>(text[offset]);

    if (!std::isalnum(c) && c != '_' && c < 0x80) {
        return std::string(1, text[offset]);
    }

    us end = offset;

    while (end < text.size() && end - offset < 24) {
        auto next = static_cast<u8>(text[end]);

        if (!std::isalnum(next) && next != '_' && next < 0x80) {
            break;
        }

        ++end;

        while (end < text.size() && (static_cast<u8>(text[end]) & 0xc0) == 0x80) {
            ++end;
        }
    }

    return std::string(text.substr(offset, end - offset));
}

std::string QuoteSnippet(std::string_view text)
{
    std::string quoted = "\"";
    us i = 0;

    while (i < text.size() && i < 48) {
        auto start = i++;

        while (i < text.size() && (static_cast<u8>(text[i]) & 0xc0) == 0x80) {
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
    us Begin;
    us End;
    us FirstChange;
    us LastChange;
    std::vector<Violation> Violations;
};

std::string ChangeMessage(
    std::string_view source,
    const formatter::FormatResult& result,
    const formatter::FormatChange& change)
{
    const auto& lines = result.Changes->OriginalLines;
    auto original = source.substr(change.Original.Begin, change.Original.End - change.Original.Begin);
    auto formatted = std::string_view(result.Text).substr(change.Output.Begin, change.Output.End - change.Output.Begin);

    if (change.Kind == formatter::FormatChangeKind::Content) {
        if (original.empty()) {
            return "insert " + QuoteSnippet(formatted) + " here";
        }

        if (formatted.empty()) {
            return "remove " + QuoteSnippet(original) + " here";
        }

        return "replace " + QuoteSnippet(original) + " with " + QuoteSnippet(formatted);
    }

    auto line = lines.LineAt(change.Original.End);
    auto text = lines.Text(source, line);
    auto range = lines.Range(line);
    auto offset = std::min<us>(change.Original.End - range.Begin, text.size());
    auto token = TokenAt(text, offset);
    auto oldBreaks = std::count(original.begin(), original.end(), '\n');
    auto newBreaks = std::count(formatted.begin(), formatted.end(), '\n');

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
        auto indent = formatted.substr(formatted.rfind('\n') + 1);

        if (!indent.empty()) {
            message += " and indent by " + DescribeWhitespace(indent);
        }

        return message;
    }

    if (oldBreaks && !newBreaks) {
        return "join lines before '" + token + "' with " + DescribeWhitespace(formatted);
    }

    if (oldBreaks != newBreaks) {
        auto difference = oldBreaks > newBreaks ? oldBreaks - newBreaks : newBreaks - oldBreaks;
        return (oldBreaks > newBreaks ? "remove " : "insert ") + std::to_string(difference) +
            (difference == 1 ? " blank line" : " blank lines") + " before '" + token + "'";
    }

    auto wanted = DescribeWhitespace(formatted);
    auto found = DescribeWhitespace(original);

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
    us positionLine = 0;
    us cursor = 0;
    u32 column = 1;
    // Change locations are monotonic. A wide line's prefix is decoded once,
    // even when it contains many separate spacing violations.
    auto positionAt = [&] (parser::SourceOffset offset) {
        auto line = changes.OriginalLines.LineAt(offset);
        auto range = changes.OriginalLines.Range(line);

        if (line != positionLine) {
            positionLine = line;
            cursor = range.Begin;
            column = 1;
        }

        auto end = std::min<us>(offset, range.Begin + changes.OriginalLines.Text(source, line).size());

        while (cursor < end) {
            cursor += unicode::utf8::GetFirstCodePointSize(source.substr(cursor, end - cursor)).value_or(1);
            ++column;
        }

        return util::Position{static_cast<u32>(line), column};
    };
    for (us i = 0; i < changes.Edits.size(); ++i) {
        const auto& change = changes.Edits[i];
        auto offset = change.Kind == formatter::FormatChangeKind::Whitespace
            ? change.Original.End
            : change.Original.Begin;
        auto position = positionAt(offset);
        auto begin = changes.OriginalLines.LineAt(change.Original.Begin);
        auto end = std::max<us>(
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

Error PrintFormattingDiagnostics(
    std::ostream& output,
    std::string_view path,
    std::string_view source,
    const formatter::FormatResult& result,
    const Terminal& terminal,
    bool fixed)
{
    if (!result.Changes) {
        return MakeError("formatting diagnostics require FormatRequest::Changes");
    }

    const auto& changes = *result.Changes;
    auto chunks = DiagnosticChunks(source, result);

    for (auto& chunk : chunks) {
        if (chunk.Violations.empty()) {
            continue;
        }

        auto first = chunk.Violations.front().Position;
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

        auto width = std::to_string(chunk.End - 1).size();
        auto gutter = std::string(width + 1, ' ') + "| ";
        output << gutter.substr(0, gutter.size() - 1) << '\n';
        us violationIndex = 0;

        for (auto lineNumber = chunk.Begin; lineNumber < chunk.End; ++lineNumber) {
            auto text = changes.OriginalLines.Text(source, lineNumber);

            if (text.ends_with('\r')) {
                text.remove_suffix(1);
            }

            auto prefix = std::string(width - std::to_string(lineNumber).size(), ' ') + std::to_string(lineNumber) +
                " | ";
            Arena scratch;
            auto rendered = RenderLine(text, prefix.size(), &scratch);
            auto printLine = [&] (us column) {
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
                auto excerpt = printLine(violation.Position.Column);
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
        auto prefix = firstChange.Original.Begin - changes.OriginalLines.Range(chunk.Begin).Begin;
        auto suffix = changes.OriginalLines.Range(chunk.End - 1).End - lastChange.Original.End;

        if (prefix > firstChange.Output.Begin || suffix > result.Text.size() - lastChange.Output.End) {
            return MakeError("invalid formatting diagnostic context");
        }

        auto begin = firstChange.Output.Begin - prefix;
        auto end = lastChange.Output.End + suffix;
        output << (fixed ? "  applied formatting:\n" : "  suggested formatting:\n");

        if (begin == end) {
            output << "  (remove this chunk)\n";
            continue;
        }

        auto firstLine = changes.OutputLines.LineAt(begin);
        auto lastLine = changes.OutputLines.LineAt(end - 1);

        for (auto line = firstLine; line <= lastLine; ++line) {
            PrintSuggestedLine(output, changes.OutputLines.Text(result.Text, line), gutter.size(), terminal);
            auto range = changes.OutputLines.Range(line);

            if (range.End && result.Text[range.End - 1] != '\n') {
                output << std::string(gutter.size(), ' ') << "\\ No newline at end of file\n";
            }
        }
    }

    return {};
}

} // namespace cli
