#pragma once

#include <util/types.hpp>

#include <array>
#include <cstddef>
#include <cstdint>

namespace tokenizer::detail {

enum class AsciiRun : u8
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
    static constexpr u32 Size = 64;

    u64 Whitespace;
    u64 Identifiers;
    u64 ClosingParentheses;
    u64 RawPrefixes;
    u64 Digits;
    u64 Newlines;
    u64 NonAscii;
    u64 Nulls;
    u64 Quotes;
    u64 Apostrophes;
    u64 Backslashes;
    u64 Slashes;
    u64 Stars;
    u64 Punctuation;
    u64 Singles;
    u64 Pairs;
    u64 Triples;

    [[nodiscard]] u64 operator[](AsciiRun kind) const noexcept
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
                return ordinary & ~(Backslashes | Slashes | Quotes | Apostrophes | RawPrefixes);
            case AsciiRun::DirectiveComment:
                return ordinary & ~Backslashes;
            default:
                return 0;
        }
    }
};

// Each bit describes one source byte, independently of lexical context.
// The caller provides a readable block; NUL stops every run, including padding.
void ClassifyAsciiBlock(const unsigned char* source, AsciiBlock& result) noexcept;

using AsciiBlockClassifier =
    void (*)(const unsigned char*, AsciiBlock&) noexcept;

[[nodiscard]] AsciiBlockClassifier GetAsciiBlockClassifier() noexcept;

} // namespace tokenizer::detail
