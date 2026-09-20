#pragma once

#include "token.hpp"

#include <unicode/code_point.hpp>
#include <unicode/utf8.hpp>
#include <util/input.hpp>

#include <functional>
#include <limits>
#include <utility>

namespace tokenizer {

#define TOKENIZATION_ERROR_KINDS                                               \
    TOKENIZATION_ERROR_KIND(                                                   \
        InputTooLarge, "input exceeds the maximum supported size")             \
    TOKENIZATION_ERROR_KIND(InvalidUtf8CodePoint, "Invalid UTF8 code point")   \
    TOKENIZATION_ERROR_KIND(UnexpectedToken, "Unexpected token")               \
    TOKENIZATION_ERROR_KIND(UnfinishedEllipsis, "Unfinished ellipsis")         \
    TOKENIZATION_ERROR_KIND(InvalidEscapeSequence, "Invalid escape sequence")  \
    TOKENIZATION_ERROR_KIND(                                                   \
        InvalidUniversalCharacter, "Invalid universal character")              \
    TOKENIZATION_ERROR_KIND(                                                   \
        EmptyCharacterLiteral, "Character literal cannot be empty")            \
    TOKENIZATION_ERROR_KIND(                                                   \
        TooManyCharactersInCharacterLiteral,                                   \
        "Encoded character literal must contain one character")                \
    TOKENIZATION_ERROR_KIND(                                                   \
        UnterminatedCharacterLiteral, "Unterminated character literal")        \
    TOKENIZATION_ERROR_KIND(                                                   \
        UnterminatedStringLiteral, "Unterminated string literal")              \
    TOKENIZATION_ERROR_KIND(                                                   \
        UnterminatedRawStringLiteral, "Unterminated raw string literal")       \
    TOKENIZATION_ERROR_KIND(                                                   \
        InvalidRawStringDelimiter, "Invalid raw string delimiter")             \
    TOKENIZATION_ERROR_KIND(                                                   \
        RawStringDelimiterTooLong,                                             \
        "Raw string delimiter exceeds 16 characters")                          \
    TOKENIZATION_ERROR_KIND(                                                   \
        NoDigitsBetweenDigitSeparators, "No digits between digit separators")  \
    TOKENIZATION_ERROR_KIND(InvalidDigitSeparator, "Invalid digit separator")  \
    TOKENIZATION_ERROR_KIND(                                                   \
        NoDigitsInFloatingPointExponent,                                       \
        "No digits in floating-point exponent")                                \
    TOKENIZATION_ERROR_KIND(                                                   \
        HexadecimalFloatingPointRequiresExponent,                              \
        "Hexadecimal floating-point literal requires an exponent")             \
    TOKENIZATION_ERROR_KIND(                                                   \
        DigitSeparatorAtStartOfBinaryInteger,                                  \
        "Digit separator at the start of a binary integer")                    \
    TOKENIZATION_ERROR_KIND(                                                   \
        NoDigitsInBinaryInteger, "No digits in binary integer")                \
    TOKENIZATION_ERROR_KIND(                                                   \
        InvalidDigitInBinaryInteger, "Invalid digit in binary integer")        \
    TOKENIZATION_ERROR_KIND(                                                   \
        DigitSeparatorAtEndOfBinaryInteger,                                    \
        "Digit separator at the end of a binary integer")                      \
    TOKENIZATION_ERROR_KIND(                                                   \
        DigitSeparatorAtStartOfHexadecimalInteger,                             \
        "Digit separator at the start of a hexadecimal integer")               \
    TOKENIZATION_ERROR_KIND(                                                   \
        NoDigitsInHexadecimalInteger, "No digits in hexadecimal integer")      \
    TOKENIZATION_ERROR_KIND(                                                   \
        DigitSeparatorAtEndOfHexadecimalInteger,                               \
        "Digit separator at the end of a hexadecimal integer")                 \
    TOKENIZATION_ERROR_KIND(                                                   \
        InvalidDigitInOctalInteger, "Invalid digit in octal integer")          \
    TOKENIZATION_ERROR_KIND(                                                   \
        DigitSeparatorAtEndOfOctalInteger,                                     \
        "Digit separator at the end of an octal integer")                      \
    TOKENIZATION_ERROR_KIND(                                                   \
        DigitSeparatorAtEndOfDecimalInteger,                                   \
        "Digit separator cannot appear at the end of decimal integer literal") \
    TOKENIZATION_ERROR_KIND(                                                   \
        UnterminatedMultilineComment, "Unterminated multiline comment")

enum class TokenizationErrorKind : uint8_t
{
#define TOKENIZATION_ERROR_KIND(value, description) value,
    TOKENIZATION_ERROR_KINDS
#undef TOKENIZATION_ERROR_KIND
};

constexpr std::string_view
GetTokenizationErrorDescription(TokenizationErrorKind kind)
{
    constexpr auto Descriptions = std::to_array<std::string_view>({
#define TOKENIZATION_ERROR_KIND(value, description) {description},
        TOKENIZATION_ERROR_KINDS
#undef TOKENIZATION_ERROR_KIND
    });

    return Descriptions[std::to_underlying(kind)];
}

struct TokenizationError
{
    TokenizationErrorKind Kind;
    util::Position TokenStart;
    util::Position ErrorPosition;
    util::Position TokenEnd;
};

class Tokenizer
{
public:
    Tokenizer(
        util::PaddedSourceView input,
        std::move_only_function<void(TokenizationError)> errorHandler = {});

    Token Get();

private:
    unicode::CodePoint CurrentCodePoint()
    {
        if (CurrentCodePointSize_ != UnloadedCodePoint) [[likely]] {
            return CurrentCodePointValue_;
        }
        return LoadCurrentCodePoint();
    }

    unicode::CodePoint LoadCurrentCodePoint();

    [[nodiscard]] bool AtEnd() const
    {
        return CurrentCodePointSize_ == 0;
    }

    static constexpr size_t UnloadedCodePoint =
        std::numeric_limits<size_t>::max();
    static constexpr unicode::CodePoint InvalidCodePoint =
        static_cast<char32_t>(std::numeric_limits<uint32_t>::max());

    [[nodiscard]] bool CurrentCodePointValid() const
    {
        return CurrentCodePointValue_ != InvalidCodePoint;
    }

    size_t CurrentCodePointOffset_ = 0;
    size_t CurrentCodePointSize_ = UnloadedCodePoint;
    unicode::CodePoint CurrentCodePointValue_ = '\0';
    size_t EncodingErrorCount_ = 0;
    util::Position CurrentCodePointPosition_ = {.Line = 1, .Column = 1};

    void ConsumeCodePoint(bool skippingNewLine = false)
    {
        auto value = CurrentCodePoint();
        auto size = std::exchange(CurrentCodePointSize_, UnloadedCodePoint);

        CurrentTokenSize_ += size;
        CurrentCodePointOffset_ += size;
        if (value == '\n') {
            ++CurrentCodePointPosition_.Line;
            CurrentCodePointPosition_.Column = 1;
            if (!skippingNewLine) {
                IsNewLine_ = true;
            }
        } else {
            ++CurrentCodePointPosition_.Column;
            if (value != ' ' && value != '\t' && value != '\r' && value != '\f'
                && value != '\v')
            {
                IsNewLine_ = false;
            }
        }
    }

    std::string_view CurrentTokenText() const;
    util::Position CurrentTokenPosition_;
    size_t CurrentTokenOffset_ = 0;
    size_t CurrentTokenSize_ = 0;
    Token FinishToken(TokenKind kind);

    void SkipWhitespace();
    bool TrySkipNewline();
    Token ParseIdentifier();
    bool ConsumeEscapeSequence();
    Token ParseCharacterLiteral();
    Token ParseStringLiteral();
    Token ParseRawStringLiteral();
    Token ParseNumber();
    Token ParseInteger();
    Token ParseFloatingPoint();
    Token ParseInlineComment();
    Token ParseMultilineComment();
    Token ParsePreprocessingDirective();

    void OnError(TokenizationError error);
    void OnError(TokenizationErrorKind error);

    std::string_view Input_;

    std::move_only_function<void(TokenizationError)> ErrorHandler_;

    bool IsFileStart() const;
    bool IsNewLine_ = true;
};

} // namespace tokenizer
