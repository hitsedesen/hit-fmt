#pragma once

#include "code_point.hpp"

#include <util/input.hpp>

#include <cstddef>
#include <format>
#include <optional>
#include <string_view>

namespace unicode::utf8 {

constexpr char32_t ValueFromUtf8(std::string_view cp);

constexpr std::optional<short> GetFirstCodePointSize(std::string_view str);

struct CodePointWithSize
{
    CodePoint Value;
    short Size;
};

[[nodiscard]] constexpr std::optional<CodePointWithSize>
GetFirstCodePoint(std::string_view str)
{
    auto bytes = GetFirstCodePointSize(str);
    if (!bytes) {
        return std::nullopt;
    }
    return CodePointWithSize{
        .Value = ValueFromUtf8(str.substr(0, *bytes)),
        .Size = *bytes,
    };
}

// Make a UTF8 byte sequence from code point.
[[nodiscard]] constexpr std::string FromCodePoint(CodePoint cp)
{
    auto value = static_cast<uint32_t>(cp);
    std::string bytes;
    if (value <= U'\x7f') {
        bytes.push_back(static_cast<char8_t>(value));
    } else if (value <= U'\u07ff') {
        bytes.push_back(static_cast<char8_t>(0xc0 | ((value >> 6) & 0x1f)));
        bytes.push_back(static_cast<char8_t>(0x80 | (value & 0x3f)));
    } else if (value <= U'\uffff') {
        bytes.push_back(static_cast<char8_t>(0xe0 | ((value >> 12) & 0x0f)));
        bytes.push_back(static_cast<char8_t>(0x80 | ((value >> 6) & 0x3f)));
        bytes.push_back(static_cast<char8_t>(0x80 | (value & 0x3F)));
    } else {
        bytes.push_back(static_cast<char8_t>(0xf0 | ((value >> 18) & 0x07)));
        bytes.push_back(static_cast<char8_t>(0x80 | ((value >> 12) & 0x3f)));
        bytes.push_back(static_cast<char8_t>(0x80 | ((value >> 6) & 0x3f)));
        bytes.push_back(static_cast<char8_t>(0x80 | (value & 0x3F)));
    }
    return bytes;
}

constexpr auto ByteOrderMark = unicode::CodePoint(U'\uFEFF');

constexpr std::optional<short> GetFirstCodePointSize(std::string_view str)
{
    if (str.size() == 0) {
        return 0;
    }
    unsigned char byte = str[0];
    if ((byte >> 7) == 0) {
        return 1;
    } else if ((byte >> 5) == 0b110) {
        return 2;
    } else if ((byte >> 4) == 0b1110) {
        return 3;
    } else if ((byte >> 3) == 0b11110) {
        return 4;
    } else {
        return std::nullopt;
    }
}

constexpr char32_t ValueFromUtf8(std::string_view cp)
{
    switch (cp.size()) {
    case 0:
        return '\0';
    case 1:
        return cp[0] & 0x7f;
    case 2:
        return ((cp[0] & 0x1f) << 6) | (cp[1] & 0x3f);
    case 3:
        return ((cp[0] & 0x0f) << 12) | ((cp[1] & 0x3f) << 6) | (cp[2] & 0x3f);
    case 4:
        return ((cp[0] & 0x07) << 18)
            | ((cp[1] & 0x3f) << 12) + ((cp[2] & 0x3f) << 6) + (cp[3] & 0x3f);
    default:
        ASSERT(false);
    }
}

} // namespace unicode::utf8
