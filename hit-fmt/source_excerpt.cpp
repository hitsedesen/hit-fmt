#include "source_excerpt.hpp"

#include <unicode/utf8.hpp>

#include <algorithm>
#include <ostream>

namespace hit_fmt {

size_t RenderedLine::Caret(size_t column) const
{
    if (!column) {
        return 0;
    }
    if (column > CodePoints) {
        return Cells.size();
    }
    auto adjustment = std::upper_bound(
        Adjustments.begin(),
        Adjustments.end(),
        column,
        [] (auto point, const auto& item) { return point < item.Column; });
    if (adjustment == Adjustments.begin()) {
        return column - 1;
    }
    --adjustment;
    return adjustment->Cell + column - adjustment->Column;
}

RenderedLine RenderLine(std::string_view text, size_t prefixWidth)
{
    RenderedLine rendered;
    rendered.Cells.reserve(std::min(text.size(), size_t{1024}));
    size_t codePoint = 1;
    for (size_t i = 0; i < text.size(); ++codePoint) {
        const auto start = i;
        const auto c = static_cast<unsigned char>(text[i]);
        i += c < 0x80 ? 1 : unicode::utf8::GetFirstCodePointSize(text.substr(i)).value_or(1);
        if (text[start] == '\t') {
            auto spaces = 8 - (prefixWidth + rendered.Cells.size()) % 8;
            while (spaces--) {
                rendered.Cells.push_back(" ");
            }
            rendered.Adjustments.push_back({codePoint + 1, rendered.Cells.size()});
        } else if (text[start] == '\r') {
            // CRLF lines are trimmed before reaching this function.
            rendered.Adjustments.push_back({codePoint + 1, rendered.Cells.size()});
        } else {
            rendered.Cells.push_back(text.substr(start, i - start));
        }
    }
    rendered.CodePoints = codePoint - 1;
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
    const RenderedLine& rendered,
    size_t column,
    size_t prefixWidth,
    size_t terminalWidth,
    bool fromStart)
{
    const auto caret = rendered.Caret(column);
    const auto& cells = rendered.Cells;
    auto available = terminalWidth > prefixWidth + 1 ? terminalWidth - prefixWidth - 1 : size_t{16};
    auto window = available > 6 ? available - 6 : size_t{1};
    size_t begin = 0;
    if (cells.size() > available && !fromStart) {
        begin = std::min(cells.size() - window, caret > window / 2 ? caret - window / 2 : 0);
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
        .CaretOffset = (begin ? 3 : 0) + std::min(caret, end) - begin,
        .CaretVisible = caret < end || (caret == end && end == cells.size()),
    };
}

} // namespace hit_fmt
