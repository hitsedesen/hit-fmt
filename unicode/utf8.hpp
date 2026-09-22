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

// Decodes the first code point without bounds checks. At least four bytes must
// be readable, including any zero padding after the logical input.
[[nodiscard]] constexpr std::optional<CodePointWithSize>
GetFirstCodePointFromPaddedInput(const char* str)
{
    auto byte = [&] (size_t offset) {
        return static_cast<unsigned char>(str[offset]);
    };
    auto isContinuation = [&] (size_t offset) {
        return (byte(offset) & 0xc0) == 0x80;
    };

    auto first = byte(0);
    short size;
    if (first <= 0x7f) {
        size = 1;
    } else if (first >= 0xc2 && first <= 0xdf && isContinuation(1)) {
        size = 2;
    } else if (first >= 0xe0 && first <= 0xef && isContinuation(1) && isContinuation(2) &&
        !(first == 0xe0 && byte(1) < 0xa0) &&
        !(first == 0xed && byte(1) > 0x9f))
    {
        size = 3;
    } else if (first >= 0xf0 && first <= 0xf4 && isContinuation(1) && isContinuation(2) &&
        isContinuation(3) && !(first == 0xf0 && byte(1) < 0x90) &&
        !(first == 0xf4 && byte(1) > 0x8f))
    {
        size = 4;
    } else {
        return std::nullopt;
    }

    return CodePointWithSize{
        .Value = ValueFromUtf8(std::string_view(str, size)),
        .Size = size,
    };
}

// Encodes one code point as UTF-8.
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
    if (str.empty()) {
        return 0;
    }

    auto byte = [&] (size_t offset) {
        return static_cast<unsigned char>(str[offset]);
    };
    auto isContinuation = [&] (size_t offset) {
        return offset < str.size() && (byte(offset) & 0xc0) == 0x80;
    };

    auto first = byte(0);
    if (first <= 0x7f) {
        return 1;
    }
    if (first >= 0xc2 && first <= 0xdf) {
        return isContinuation(1) ? std::optional<short>(2) : std::nullopt;
    }
    if (first >= 0xe0 && first <= 0xef) {
        if (!isContinuation(1) || !isContinuation(2)) {
            return std::nullopt;
        }
        if ((first == 0xe0 && byte(1) < 0xa0) || (first == 0xed && byte(1) > 0x9f)) {
            return std::nullopt;
        }
        return 3;
    }
    if (first >= 0xf0 && first <= 0xf4) {
        if (!isContinuation(1) || !isContinuation(2) || !isContinuation(3)) {
            return std::nullopt;
        }
        if ((first == 0xf0 && byte(1) < 0x90) || (first == 0xf4 && byte(1) > 0x8f)) {
            return std::nullopt;
        }
        return 4;
    }
    return std::nullopt;
}

constexpr char32_t ValueFromUtf8(std::string_view cp)
{
    auto byte = [&] (size_t offset) {
        return static_cast<unsigned char>(cp[offset]);
    };
    switch (cp.size()) {
    case 0:
        return '\0';
    case 1:
        return byte(0);
    case 2:
        return ((byte(0) & 0x1f) << 6) | (byte(1) & 0x3f);
    case 3:
        return ((byte(0) & 0x0f) << 12) | ((byte(1) & 0x3f) << 6)
            | (byte(2) & 0x3f);
    case 4:
        return ((byte(0) & 0x07) << 18) | ((byte(1) & 0x3f) << 12)
            | ((byte(2) & 0x3f) << 6) | (byte(3) & 0x3f);
    default:
        ASSERT(false);
    }
}

} // namespace unicode::utf8
