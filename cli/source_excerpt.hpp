#pragma once

#include <util/memory.hpp>
#include <util/types.hpp>

#include <iosfwd>
#include <span>
#include <string_view>

namespace cli {

// Cells borrow the supplied line; tabs expand to terminal display columns.
// The supplied memory resource must outlive the rendered line.
struct RenderedLine
{
    explicit RenderedLine(std::pmr::memory_resource* memory)
        : Cells(memory)
        , Adjustments(memory)
    { }

    Vector<std::string_view> Cells;

    struct Adjustment
    {
        us Column;
        us Cell;
    };

    Vector<Adjustment> Adjustments;
    us CodePoints = 0;

    us Caret(us column) const;
};

RenderedLine RenderLine(std::string_view text, us prefixWidth, std::pmr::memory_resource* memory);

void PrintCells(std::ostream& output, std::span<const std::string_view> cells, us begin, us end);

struct Excerpt
{
    us CaretOffset;
    bool CaretVisible;
};

// CodePoints count Unicode code points, as in original-source diagnostics.
Excerpt PrintSourceExcerpt(
    std::ostream& output,
    const RenderedLine& rendered,
    us column,
    us prefixWidth,
    us terminalWidth,
    bool fromStart = false);

} // namespace cli
