#pragma once

#include <cstddef>
#include <iosfwd>
#include <span>
#include <string_view>
#include <vector>

namespace hit_fmt {

// Cells borrow the supplied line; tabs expand to terminal display columns.
struct RenderedLine
{
    std::vector<std::string_view> Cells;
    struct Adjustment
    {
        size_t Column;
        size_t Cell;
    };
    std::vector<Adjustment> Adjustments;
    size_t CodePoints = 0;

    [[nodiscard]] size_t Caret(size_t column) const;
};

[[nodiscard]] RenderedLine RenderLine(std::string_view text, size_t prefixWidth);

void PrintCells(std::ostream& output, std::span<const std::string_view> cells, size_t begin, size_t end);

struct Excerpt
{
    size_t CaretOffset;
    bool CaretVisible;
};

// CodePoints count Unicode code points, as in original-source diagnostics.
[[nodiscard]] Excerpt PrintSourceExcerpt(
    std::ostream& output,
    const RenderedLine& rendered,
    size_t column,
    size_t prefixWidth,
    size_t terminalWidth,
    bool fromStart = false);

} // namespace hit_fmt
