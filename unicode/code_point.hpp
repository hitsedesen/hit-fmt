#pragma once

#include <util/assert.hpp>
#include <util/types.hpp>

#include <cstdint>
#include <string>

namespace unicode {

class CodePoint
{
public:
    constexpr CodePoint(unsigned char cp)
        : Value_(cp)
    { }

    constexpr CodePoint(char cp)
        : Value_(cp)
    { }

    constexpr CodePoint(char8_t cp)
        : Value_(cp)
    { }

    constexpr CodePoint(char16_t cp)
        : Value_(cp)
    { }

    constexpr CodePoint(char32_t cp)
        : Value_(cp)
    { }

    [[nodiscard]] constexpr operator u32() const noexcept
    {
        return Value_;
    }

private:
    u32 Value_;
};

constexpr std::string
operator+(const std::string& lhs, const CodePoint& rhs) = delete;
constexpr std::string&
operator+=(const std::string& lhs, const CodePoint& rhs) = delete;

constexpr bool operator==(CodePoint lhs, CodePoint rhs)
{
    return static_cast<u32>(lhs) == static_cast<u32>(rhs);
}

constexpr bool operator==(CodePoint lhs, char rhs)
{
    return lhs == CodePoint(rhs);
}

constexpr bool operator==(CodePoint lhs, char8_t rhs)
{
    return lhs == CodePoint(rhs);
}

constexpr bool operator==(CodePoint lhs, char16_t rhs)
{
    return lhs == CodePoint(rhs);
}

constexpr bool operator==(CodePoint lhs, char32_t rhs)
{
    return lhs == CodePoint(rhs);
}

// Test the Unicode properties used to form C++ identifiers.
[[nodiscard]] bool IsXIDStart(CodePoint cp);

[[nodiscard]] bool IsXIDContinue(CodePoint cp);

} // namespace unicode
