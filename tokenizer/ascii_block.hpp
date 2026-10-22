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

    uint64_t Whitespace;
    uint64_t Identifiers;
    uint64_t ClosingParentheses;
    uint64_t RawPrefixes;
    uint64_t Digits;
    uint64_t Newlines;
    uint64_t NonAscii;
    uint64_t Nulls;
    uint64_t Quotes;
    uint64_t Apostrophes;
    uint64_t Backslashes;
    uint64_t Slashes;
    uint64_t Stars;
    uint64_t Punctuation;
    uint64_t Singles;
    uint64_t Pairs;
    uint64_t Triples;

    [[nodiscard]] uint64_t operator[](AsciiRun kind) const noexcept
    {
        const auto ordinary = ~(NonAscii | Nulls | Newlines);
        switch (kind) {
            case AsciiRun::Whitespace:
                return Whitespace;
            case AsciiRun::Identifier:
                return Identifiers;
            case AsciiRun::String:
                return ordinary & ~(Quotes | Backslashes);
            case AsciiRun::Character:
                return ordinary & ~(Apostrophes | Backslashes);
            case AsciiRun::BlockComment:
                return ordinary & ~Stars;
            case AsciiRun::RawString:
                return ordinary & ~ClosingParentheses;
            case AsciiRun::LineComment:
                return ordinary;
            case AsciiRun::Directive:
                return ordinary &
                    ~(Backslashes | Slashes | Quotes | Apostrophes |
                        RawPrefixes);
            case AsciiRun::DirectiveComment:
                return ordinary & ~Backslashes;
            default:
                return 0;
        }
    }
};

// Each bit describes one source byte, independently of lexical context.
// The caller provides a readable block; NUL stops every run, including padding.
void ClassifyAsciiBlock(
    const unsigned char* source,
    AsciiBlock& result) noexcept;

using AsciiBlockClassifier =
    void (*)(const unsigned char*, AsciiBlock&) noexcept;

[[nodiscard]] AsciiBlockClassifier GetAsciiBlockClassifier() noexcept;

} // namespace tokenizer::detail
