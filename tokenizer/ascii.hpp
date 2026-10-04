#pragma once

#include <cstdint>

namespace tokenizer::detail {

enum class AsciiState : uint8_t
{
    Identifier,
    Whitespace,
    String,
    Character,
    LineComment,
    BlockComment,
    RawString,
    Directive,
    DirectiveLineComment,
};

[[nodiscard]] constexpr bool IsAsciiSpace(unsigned char byte) noexcept
{
    return byte == ' ' || byte == '\t' || byte == '\r' || byte == '\f' ||
        byte == '\v';
}

} // namespace tokenizer::detail
