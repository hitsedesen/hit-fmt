#pragma once

#include "ascii_block.hpp"
#include "token.hpp"

#include <unicode/code_point.hpp>

#include <util/input.hpp>

#include <functional>
#include <iterator>
#include <limits>

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
        UnterminatedMultilineComment, "Unterminated multiline comment")        \
    TOKENIZATION_ERROR_KIND(                                                   \
        UnexpectedNullCharacter, "Unexpected NUL character")

enum class TokenizationErrorKind : uint8_t
{
#define TOKENIZATION_ERROR_KIND(value, description) value,
    TOKENIZATION_ERROR_KINDS
#undef TOKENIZATION_ERROR_KIND
};

[[nodiscard]] std::string_view GetTokenizationErrorDescription(
    TokenizationErrorKind kind);

struct TokenizationError
{
    TokenizationErrorKind Kind;
    util::Position TokenStart;
    util::Position ErrorPosition;
    util::Position TokenEnd;
};

namespace detail {

template <typename Owner>
class TokenizerCursor
{
protected:
    enum class NewlineHandling : uint8_t
    {
        Track,
        Ignore,
    };

    [[nodiscard]] unicode::CodePoint CurrentCodePoint();
    [[nodiscard]] unicode::CodePoint CurrentUnicodeCodePoint();
    void LoadCurrentUnicodeCodePoint();
    [[nodiscard]] bool AtEnd() const noexcept;
    [[nodiscard]] unsigned char ByteAt(
        util::SourceOffset offset) const noexcept;
    [[nodiscard]] unsigned char PeekByte(
        util::SourceOffset lookahead = 0) const noexcept;
    [[nodiscard]] const unsigned char* CurrentByteAddress() const noexcept;

    void ConsumeAsciiByte(
        unsigned char byte,
        NewlineHandling newlineHandling = NewlineHandling::Track);

    // Requires the non-ASCII character at the current offset to have been
    // decoded by LoadCurrentUnicodeCodePoint() before this call.
    void ConsumeUnicodeCodePoint();

    void ConsumeCodePoint(
        unicode::CodePoint value,
        NewlineHandling newlineHandling = NewlineHandling::Track);
    void ConsumeCodePoint(
        NewlineHandling newlineHandling = NewlineHandling::Track);

    static constexpr unicode::CodePoint InvalidCodePoint =
        static_cast<char32_t>(std::numeric_limits<uint32_t>::max());

    util::SourceOffset CurrentCodePointOffset_ = 0;
    // The cursor only advances, so ASCII never needs to invalidate this cache.
    util::SourceOffset CachedUnicodeOffset_ =
        std::numeric_limits<util::SourceOffset>::max();
    unicode::CodePoint CachedUnicodeValue_ = '\0';
    uint8_t CachedUnicodeSize_ = 0;
    uint32_t EncodingErrorCount_ = 0;
    util::Position CurrentCodePointPosition_ = {.Line = 1, .Column = 1};

private:
    [[nodiscard]] const unsigned char* ByteData() const noexcept;
    [[nodiscard]] Owner& GetOwner() noexcept;
    [[nodiscard]] const Owner& GetOwner() const noexcept;

    void DecodeCurrentUnicodeCodePoint();
};

} // namespace detail

// Tokenizes one padded source buffer. Tokens refer to the original input, which
// must outlive the tokenizer and all returned tokens.
class Tokenizer
    : private detail::TokenizerCursor<Tokenizer>
{
public:
    Tokenizer(
        util::PaddedSourceView input,
        std::function<void(TokenizationError)> errorHandler = {});

    // Returns the next token, or EndOfFile after the input is exhausted.
    [[nodiscard]] Token Get();

    // A single-pass view over this tokenizer's remaining tokens.
    class Iterator
    {
    public:
        using value_type = Token;
        using difference_type = std::ptrdiff_t;
        using iterator_concept = std::input_iterator_tag;
        using iterator_category = std::input_iterator_tag;

        Iterator() = default;

        [[nodiscard]] const Token& operator*() const noexcept
        {
            return Current_;
        }

        [[nodiscard]] const Token* operator->() const noexcept
        {
            return &Current_;
        }

        Iterator& operator++()
        {
            Current_ = Tokenizer_->Get();
            return *this;
        }

        Iterator operator++(int)
        {
            auto previous = *this;
            ++*this;
            return previous;
        }

        [[nodiscard]] bool operator==(std::default_sentinel_t) const noexcept
        {
            return Current_.Kind == TokenKind::EndOfFile;
        }

    private:
        friend class Tokenizer;

        Tokenizer* Tokenizer_ = nullptr;
        Token Current_{};
    };

    [[nodiscard]] Iterator begin()
    {
        Iterator iterator;
        iterator.Tokenizer_ = this;
        iterator.Current_ = Get();
        return iterator;
    }

    [[nodiscard]] static constexpr std::default_sentinel_t end() noexcept
    {
        return {};
    }

private:
    friend class detail::TokenizerCursor<Tokenizer>;

    [[nodiscard]] std::string_view CurrentTokenText() const noexcept;
    util::Position CurrentTokenPosition_ = {.Line = 1, .Column = 1};
    util::SourceOffset CurrentTokenOffset_ = 0;
    util::SourceOffset CurrentTokenSize_ = 0;
    [[nodiscard]] Token FinishToken(TokenKind kind) const noexcept;

    template <detail::AsciiRun Kind, bool TrackNonSpace = false>
    [[nodiscard]] const unsigned char* ScanAscii(
        const unsigned char* cursor,
        bool* nonSpace = nullptr) noexcept;
    [[nodiscard]] const unsigned char* ScanAsciiQuotedLiteral(
        const unsigned char* cursor,
        unsigned char quote) noexcept;

    void SkipWhitespace();
    bool TrySkipNewline();
    Token ParseAsciiOperator(unsigned char byte);
    Token ParseIdentifier();
    bool ConsumeEscapeSequence();
    void ConsumeLiteralSuffix();
    Token ParseCharacterLiteral();
    Token ParseStringLiteral();
    Token ParseRawStringLiteral();
    Token ParseNumber();
    Token DiagnoseInteger();
    Token DiagnoseFloatingPoint();
    Token ParseInlineComment();
    Token ParseMultilineComment();
    Token ParsePreprocessingDirective();

    void OnError(TokenizationError error);
    void OnError(TokenizationErrorKind error);

    util::PaddedSourceView Input_;

    detail::AsciiBlock Block_{};
    detail::AsciiBlockClassifier ClassifyBlock_ =
        detail::GetAsciiBlockClassifier();
    util::SourceOffset BlockOffset_ =
        std::numeric_limits<util::SourceOffset>::max();

    std::function<void(TokenizationError)> ErrorHandler_;

    [[nodiscard]] bool IsFileStart() const noexcept;
    bool IsNewLine_ = true;
    bool IsEndOfFile_ = false;
};

} // namespace tokenizer
