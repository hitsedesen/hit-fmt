#include "source_excerpt.hpp"

#include <unicode/utf8.hpp>

#include <algorithm>
#include <ostream>

namespace hit_fmt {

RenderedLine RenderLine(std::string_view text, size_t column, size_t prefixWidth)
{
    RenderedLine rendered;
    size_t codePoint = 1;
    for (size_t i = 0; i < text.size(); ++codePoint) {
        if (codePoint == column) {
            rendered.Caret = rendered.Cells.size();
        }
        const auto start = i;
        const auto c = static_cast<unsigned char>(text[i]);
        i += c < 0x80 ? 1 : unicode::utf8::GetFirstCodePointSize(text.substr(i)).value_or(1);
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

void PrintCells(std::ostream& output, std::span<const std::string_view> cells, size_t begin, size_t end)
{
    for (auto i = begin; i < end; ++i) {
        output << cells[i];
    }
}

Excerpt PrintSourceExcerpt(
    std::ostream& output,
    std::string_view text,
    size_t column,
    size_t prefixWidth,
    size_t terminalWidth,
    bool fromStart)
{
    auto rendered = RenderLine(text, column, prefixWidth);
    const auto& cells = rendered.Cells;
    auto available = terminalWidth > prefixWidth + 1 ? terminalWidth - prefixWidth - 1 : size_t{16};
    auto window = available > 6 ? available - 6 : size_t{1};
    size_t begin = 0;
    if (cells.size() > available && !fromStart) {
        begin = std::min(cells.size() - window, rendered.Caret > window / 2 ? rendered.Caret - window / 2 : 0);
    }
    auto count = cells.size() > available ? (fromStart ? available - 3 : window) : available;
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
        .CaretVisible = rendered.Caret < end || (rendered.Caret == end && end == cells.size()),
    };
}

} // namespace hit_fmt
