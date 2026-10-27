#pragma once

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <format>
#include <limits>
#include <string_view>

namespace util {

using SourceOffset = uint32_t;

inline constexpr size_t SourcePaddingSize = 64;

inline constexpr size_t MaxSourceSize =
    static_cast<size_t>(std::numeric_limits<SourceOffset>::max()) - SourcePaddingSize;

// A non-owning source buffer with safe lookahead. Data points to Size source
// bytes followed by SourcePaddingSize zero bytes.
struct PaddedSourceView
{
    const char* Data;
    uint32_t Size;

    [[nodiscard]] constexpr std::string_view View() const
    {
        return {Data, Size};
    }

    [[nodiscard]] constexpr std::string_view substr(size_t pos = 0, size_t count = std::string_view::npos) const
    {
        return View().substr(pos, count);
    }
};

template <size_t Size>
[[nodiscard]] constexpr auto PadSource(const char (&source)[Size])
{
    std::array<char, Size - 1 + SourcePaddingSize> result{};
    for (size_t i = 0; i < Size - 1; ++i) {
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
    uint32_t Line;
    uint32_t Column;
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
