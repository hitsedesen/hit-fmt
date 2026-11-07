#pragma once

#include "types.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <format>
#include <limits>
#include <string_view>

namespace util {

using SourceOffset = u32;

inline constexpr us SourcePaddingSize = 64;

inline constexpr us MaxSourceSize = static_cast<us>(std::numeric_limits<SourceOffset>::max()) - SourcePaddingSize;

// A non-owning source buffer with safe lookahead. Data points to Size source
// bytes followed by SourcePaddingSize zero bytes.
struct PaddedSourceView
{
    const char* Data;
    u32 Size;

    [[nodiscard]] constexpr std::string_view View() const
    {
        return {Data, Size};
    }

    [[nodiscard]] constexpr std::string_view substr(us pos = 0, us count = std::string_view::npos) const
    {
        return View().substr(pos, count);
    }
};

template <us Size>
[[nodiscard]] constexpr auto PadSource(const char (&source)[Size])
{
    std::array<char, Size - 1 + SourcePaddingSize> result{};

    for (us i = 0; i < Size - 1; ++i) {
        result[i] = source[i];
    }

    return result;
}

struct SourceView
{
    SourceOffset Begin = 0;
    SourceOffset End = 0;

    constexpr bool operator==(const SourceView& other) const = default;

    [[nodiscard]] constexpr bool Empty() const
    {
        return Begin == End;
    }

    [[nodiscard]] constexpr SourceOffset Size() const
    {
        return End - Begin;
    }

    [[nodiscard]] std::string_view View(std::string_view source) const
    {
        assert(Begin <= End);
        assert(End <= source.size());
        return {source.data() + Begin, Size()};
    }
};

struct Position
{
    u32 Line;
    u32 Column;
};

} // namespace util

template <>
struct std::formatter<util::Position>
{
    constexpr auto parse(std::format_parse_context& ctx) const
    {
        return ctx.begin();
    }

    auto format(const util::Position& position, std::format_context& ctx) const
    {
        return std::format_to(ctx.out(), "Ln {}, Col {}", position.Line, position.Column);
    }
};
