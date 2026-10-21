#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace tokenizer::detail {

enum class AsciiRun : uint8_t
{
    Whitespace,
    Identifier,
    String,
    Character,
    BlockComment,
    RawString,
    LineComment,
    Directive,
    DirectiveComment,
    Count,
};

struct AsciiBlock
{
    static constexpr uint32_t Size = 64;

    std::array<uint64_t, static_cast<size_t>(AsciiRun::Count)> Continuations;

    [[nodiscard]] uint64_t operator[](AsciiRun kind) const noexcept
    {
        return Continuations[static_cast<size_t>(kind)];
    }
};

// Each bit describes one source byte, independently of lexical context.
// The caller provides a readable block; NUL stops every run, including padding.
[[nodiscard]] AsciiBlock ClassifyAsciiBlock(
    const unsigned char* source) noexcept;

using AsciiBlockClassifier = AsciiBlock (*)(const unsigned char*) noexcept;

[[nodiscard]] AsciiBlockClassifier GetAsciiBlockClassifier() noexcept;

} // namespace tokenizer::detail
