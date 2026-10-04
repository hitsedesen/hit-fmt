#pragma once

#include <util/assert.hpp>

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

    [[nodiscard]] constexpr operator uint32_t() const noexcept
    {
        return Value_;
    }

private:
    uint32_t Value_;
};

constexpr std::string
operator+(const std::string& lhs, const CodePoint& rhs) = delete;
constexpr std::string&
operator+=(const std::string& lhs, const CodePoint& rhs) = delete;

constexpr bool operator==(CodePoint lhs, CodePoint rhs)
{
    return static_cast<uint32_t>(lhs) == static_cast<uint32_t>(rhs);
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
