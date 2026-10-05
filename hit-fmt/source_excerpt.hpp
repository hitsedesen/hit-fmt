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
    size_t Caret = 0;
};

[[nodiscard]] RenderedLine RenderLine(
    std::string_view text,
    size_t column,
    size_t prefixWidth);

void PrintCells(
    std::ostream& output,
    std::span<const std::string_view> cells,
    size_t begin,
    size_t end);

struct Excerpt
{
    size_t CaretOffset;
    bool CaretVisible;
};

// Columns count Unicode code points, as in original-source diagnostics.
[[nodiscard]] Excerpt PrintSourceExcerpt(
    std::ostream& output,
    std::string_view text,
    size_t column,
    size_t prefixWidth,
    size_t terminalWidth,
    bool fromStart = false);

} // namespace hit_fmt
