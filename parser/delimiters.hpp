#pragma once

#include <util/memory.hpp>
#include <util/types.hpp>

#include <tokenizer/token.hpp>

namespace parser::detail {

class TokenPool;

enum class DelimiterInstructionSet
{
    Automatic,
    Scalar,
    Sse2,
    Avx,
    Avx2,
    Avx512,
};

inline constexpr u32 DelimiterBlockSize = 64;

using DelimiterBlockClassifier = u64 (*)(const tokenizer::TokenKind*, u32) noexcept;

// Classifiers read only count initialized kinds (at most DelimiterBlockSize).
// A requested instruction set returns nullptr if unavailable on this build/CPU.
[[nodiscard]] DelimiterBlockClassifier GetDelimiterBlockClassifier(
    DelimiterInstructionSet instructionSet = DelimiterInstructionSet::Automatic) noexcept;

[[nodiscard]] constexpr bool IsOpenDelimiter(tokenizer::TokenKind kind)
{
    using TK = tokenizer::TokenKind;
    return kind == TK::Operator_OpenParenthesis || kind == TK::Operator_OpenBracket || kind == TK::Operator_OpenBrace;
}

[[nodiscard]] constexpr tokenizer::TokenKind ClosingDelimiter(tokenizer::TokenKind kind)
{
    using TK = tokenizer::TokenKind;
    switch (kind) {
        case TK::Operator_OpenParenthesis:
            return TK::Operator_CloseParenthesis;
        case TK::Operator_OpenBracket:
            return TK::Operator_CloseBracket;
        case TK::Operator_OpenBrace:
            return TK::Operator_CloseBrace;
        default:
            return TK::Invalid;
    }
}

[[nodiscard]] constexpr bool IsDelimiter(tokenizer::TokenKind kind)
{
    using TK = tokenizer::TokenKind;
    static_assert(static_cast<u8>(TK::Operator_CloseBrace) == static_cast<u8>(TK::Operator_OpenBrace) + 1);
    static_assert(static_cast<u8>(TK::Operator_OpenBracket) == static_cast<u8>(TK::Operator_OpenBrace) + 2);
    static_assert(static_cast<u8>(TK::Operator_CloseBracket) == static_cast<u8>(TK::Operator_OpenBrace) + 3);
    static_assert(static_cast<u8>(TK::Operator_OpenParenthesis) == static_cast<u8>(TK::Operator_OpenBrace) + 4);
    static_assert(static_cast<u8>(TK::Operator_CloseParenthesis) == static_cast<u8>(TK::Operator_OpenBrace) + 5);
    return kind >= TK::Operator_OpenBrace && kind <= TK::Operator_CloseParenthesis;
}

// Existing links must be initialized to none. A mismatch does not pop the opener.
[[nodiscard]] bool MatchDelimiters(
    TokenPool& tokens,
    Arena& scratch,
    DelimiterBlockClassifier classify = GetDelimiterBlockClassifier());

} // namespace parser::detail
