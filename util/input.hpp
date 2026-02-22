#pragma once

#include <cstddef>
#include <format>

namespace util {

struct Position
{
    size_t Line;
    size_t Column;
};

} // namespace util

template<>
struct std::formatter<util::Position>
{
    constexpr auto parse(std::format_parse_context& ctx) const
    {
        return ctx.begin();
    }

    auto format(const util::Position& position, std::format_context& ctx) const
    {
        return std::format_to(
            ctx.out(), "Ln {}, Col {}", position.Line, position.Column);
    }
};
