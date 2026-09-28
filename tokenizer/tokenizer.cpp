#include "tokenizer.hpp"

#include "identifier_kind.hpp"
#include "token.hpp"

#include <unicode/code_point.hpp>
#include <unicode/utf8.hpp>

#include <algorithm>
#include <array>
#include <utility>

namespace tokenizer {
namespace {

[[nodiscard]] constexpr bool IsWhitespace(unicode::CodePoint cp) noexcept
{
    return cp == ' ' || cp == '\t' || cp == '\n' || cp == '\r' || cp == '\f' ||
        cp == '\v';
}

[[nodiscard]] constexpr bool IsOctalDigit(unicode::CodePoint cp) noexcept
{
    return cp >= '0' && cp <= '7';
}

[[nodiscard]] constexpr bool IsDigit(unicode::CodePoint cp) noexcept
{
    return cp >= '0' && cp <= '9';
}

[[nodiscard]] constexpr bool IsHexDigit(unicode::CodePoint cp) noexcept
{
    return IsDigit(cp) || (cp >= 'a' && cp <= 'f') || (cp >= 'A' && cp <= 'F');
}

[[nodiscard]] constexpr bool IsNonDigit(unicode::CodePoint cp) noexcept
{
    return (cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z') || cp == '_';
}

[[nodiscard]] constexpr bool IsAsciiDigit(unsigned char value) noexcept
{
    return value >= '0' && value <= '9';
}

[[nodiscard]] constexpr bool IsAsciiAlphaNumeric(unsigned char value) noexcept
{
    return IsAsciiDigit(value) || (value >= 'a' && value <= 'z') ||
        (value >= 'A' && value <= 'Z') || value == '_';
}

[[nodiscard]] bool IsIdStart(unicode::CodePoint cp)
{
    if (IsNonDigit(cp)) {
        return true;
    }
    if (cp < 128) {
        return false;
    }
    return unicode::IsXIDStart(cp);
}

[[nodiscard]] bool IsIdContinuation(unicode::CodePoint cp)
{
    if (IsNonDigit(cp) || IsDigit(cp)) {
        return true;
    }
    if (cp < 128) {
        return false;
    }
    return unicode::IsXIDContinue(cp);
}

constexpr std::array<std::string_view, 4> EncodingPrefixes{"u8", "u", "U", "L"};
constexpr std::array<std::string_view, 5> RawEncodingPrefixes{
    "R", "u8R", "uR", "UR", "LR"};
constexpr std::array<std::string_view, 8> FloatingPointSuffixes{
    "", "f", "l", "f16", "f32", "f64", "f128", "bf16"};
constexpr std::array<std::string_view, 11> IntegerSuffixes{
    "", "u", "ul", "ull", "uz", "l", "lu", "ll", "llu", "z", "zu"};

[[nodiscard]] constexpr bool
EqualsIgnoringAsciiCase(std::string_view lhs, std::string_view rhs) noexcept
{
    if (lhs.size() != rhs.size()) {
        return false;
    }

    auto lower = [] (char value) {
        if (value >= 'A' && value <= 'Z') {
            return static_cast<char>(value + ('a' - 'A'));
        }
        return value;
    };
    for (size_t i = 0; i < lhs.size(); ++i) {
        if (lower(lhs[i]) != lower(rhs[i])) {
            return false;
        }
    }
    return true;
}

} // namespace

Tokenizer::Tokenizer(
    util::PaddedSourceView input,
    std::function<void(TokenizationError)> errorHandler)
    : Input_(input.View())
    , ErrorHandler_(std::move(errorHandler))
{
    if (Input_.size() > util::MaxSourceSize) {
        Input_ = std::string_view("\0\0\0", 0);
        OnError(
            TokenizationError{
                .Kind = TokenizationErrorKind::InputTooLarge,
                .TokenStart = {.Line = 1, .Column = 1},
                .ErrorPosition = {.Line = 1, .Column = 1},
                .TokenEnd = {.Line = 1, .Column = 1},
            });
    }
}

template <>
unicode::CodePoint detail::TokenizerCursor<Tokenizer>::LoadCurrentCodePoint()
{
    auto firstByte = static_cast<unsigned char>(PeekByte());
    const auto& input = GetOwner().Input_;
    if (firstByte == 0 && CurrentCodePointOffset_ ==
        input.size()) [[unlikely]]
    {
        CurrentCodePointValue_ = '\0';
        CurrentCodePointSize_ = 0;
        return CurrentCodePointValue_;
    }

    if (firstByte < 0x80) [[likely]] {
        CurrentCodePointValue_ = static_cast<char32_t>(firstByte);
        CurrentCodePointSize_ = 1;
        return CurrentCodePointValue_;
    }

    auto first =
        unicode::utf8::GetFirstCodePointFromPaddedInput(CurrentByteAddress());
    if (!first) {
        GetOwner().OnError(TokenizationErrorKind::InvalidUtf8CodePoint);
        ++EncodingErrorCount_;
        CurrentCodePointValue_ = InvalidCodePoint;
        CurrentCodePointSize_ = 1;
    } else {
        CurrentCodePointValue_ = first->Value;
        CurrentCodePointSize_ = first->Size;
    }
    return CurrentCodePointValue_;
}

Token Tokenizer::FinishToken(TokenKind kind) const noexcept
{
    return Token{
        .Kind = kind,
        .Position = CurrentTokenPosition_,
        .Text = {
            .Begin = static_cast<util::SourceOffset>(CurrentTokenOffset_),
            .End = static_cast<util::SourceOffset>(
                CurrentTokenOffset_ + CurrentTokenSize_),
        },
    };
}

Token Tokenizer::Get()
{
    SkipWhitespace();

    CurrentTokenPosition_ = CurrentCodePointPosition_;
    CurrentTokenOffset_ = CurrentCodePointOffset_;
    CurrentTokenSize_ = 0;

    auto cur = CurrentCodePoint();
    if (AtEnd()) [[unlikely]] {
        return FinishToken(TokenKind::EndOfFile);
    }
    switch (cur) {
        case '#':
        {
            if (!IsNewLine_) {
                ConsumeCodePoint();
                OnError(TokenizationErrorKind::UnexpectedToken);
                return FinishToken(TokenKind::Invalid);
            }
            return ParsePreprocessingDirective();
        }
        case '{':
            ConsumeCodePoint();
            return FinishToken(TokenKind::Operator_OpenBrace);
        case '}':
            ConsumeCodePoint();
            return FinishToken(TokenKind::Operator_CloseBrace);
        case '[':
            ConsumeCodePoint();
            return FinishToken(TokenKind::Operator_OpenBracket);
        case ']':
            ConsumeCodePoint();
            return FinishToken(TokenKind::Operator_CloseBracket);
        case '(':
            ConsumeCodePoint();
            return FinishToken(TokenKind::Operator_OpenParenthesis);
        case ')':
            ConsumeCodePoint();
            return FinishToken(TokenKind::Operator_CloseParenthesis);
        case ';':
            ConsumeCodePoint();
            return FinishToken(TokenKind::Operator_Semicolon);
        case ':':
        {
            ConsumeCodePoint();
            cur = CurrentCodePoint();
            switch (cur) {
                case ':':
                    ConsumeCodePoint();
                    return FinishToken(TokenKind::Operator_ScopeResolution);
                case '>':
                    ConsumeCodePoint();
                    return FinishToken(TokenKind::Operator_CloseBracket);
                default:
                    return FinishToken(TokenKind::Operator_Colon);
            }
        }
        case '.':
        {
            if (IsDigit(PeekByte(1))) [[unlikely]] {
                return ParseNumber();
            }
            ConsumeCodePoint();
            cur = CurrentCodePoint();
            switch (cur) {
                case '.':
                {
                    ConsumeCodePoint();
                    cur = CurrentCodePoint();
                    if (cur == '.') {
                        ConsumeCodePoint();
                        return FinishToken(TokenKind::Operator_Ellipsis);
                    }
                    OnError(TokenizationErrorKind::UnfinishedEllipsis);
                    return FinishToken(TokenKind::Invalid);
                }
                case '*':
                    ConsumeCodePoint();
                    return FinishToken(
                        TokenKind::Operator_PointerToMemberDereference);
                default:
                    return FinishToken(TokenKind::Operator_ClassMemberAccess);
            }
        }
        case '-':
        {
            ConsumeCodePoint();
            cur = CurrentCodePoint();
            switch (cur) {
                case '>':
                    ConsumeCodePoint();
                    cur = CurrentCodePoint();
                    if (cur == '*') {
                        ConsumeCodePoint();
                        using Kind = TokenKind;
                        auto kind =
                            Kind::Operator_IndirectPointerToMemberDereference;
                        return FinishToken(kind);
                    }
                    return FinishToken(
                        TokenKind::Operator_IndirectClassMemberAccess);
                case '=':
                    ConsumeCodePoint();
                    return FinishToken(TokenKind::Operator_MinusAssign);
                case '-':
                    ConsumeCodePoint();
                    return FinishToken(TokenKind::Operator_Decrement);
                default:
                    return FinishToken(TokenKind::Operator_Minus);
            }
        }
        case '?':
            ConsumeCodePoint();
            return FinishToken(TokenKind::Operator_Conditional);
        case '~':
            ConsumeCodePoint();
            return FinishToken(TokenKind::Operator_Complement);
        case '!':
        {
            ConsumeCodePoint();
            cur = CurrentCodePoint();
            if (cur == '=') {
                ConsumeCodePoint();
                return FinishToken(TokenKind::Operator_NotEquals);
            }
            return FinishToken(TokenKind::Operator_Not);
        }
        case '+':
        {
            ConsumeCodePoint();
            cur = CurrentCodePoint();
            if (cur == '=') {
                ConsumeCodePoint();
                return FinishToken(TokenKind::Operator_PlusAssign);
            }
            if (cur == '+') {
                ConsumeCodePoint();
                return FinishToken(TokenKind::Operator_Increment);
            }
            return FinishToken(TokenKind::Operator_Plus);
        }
        case '*':
        {
            ConsumeCodePoint();
            cur = CurrentCodePoint();
            if (cur == '=') {
                ConsumeCodePoint();
                return FinishToken(TokenKind::Operator_MultiplyAssign);
            }
            return FinishToken(TokenKind::Operator_Star);
        }
        case '/':
        {
            ConsumeCodePoint();
            cur = CurrentCodePoint();
            switch (cur) {
                case '=':
                    ConsumeCodePoint();
                    return FinishToken(TokenKind::Operator_DivideAssign);
                case '/':
                    return ParseInlineComment();
                case '*':
                    return ParseMultilineComment();
                default:
                    return FinishToken(TokenKind::Operator_Divide);
            }
        }
        case '%':
        {
            const bool startsLine = IsNewLine_;
            ConsumeCodePoint();
            cur = CurrentCodePoint();
            if (cur == '=') {
                ConsumeCodePoint();
                return FinishToken(TokenKind::Operator_RemainderAssign);
            }
            if (startsLine) {
                if (cur == ':') {
                    ConsumeCodePoint();
                    return ParsePreprocessingDirective();
                }
            }
            return FinishToken(TokenKind::Operator_Remainder);
        }
        case '^':
        {
            ConsumeCodePoint();
            cur = CurrentCodePoint();
            if (cur == '=') {
                ConsumeCodePoint();
                return FinishToken(TokenKind::Operator_XorAssign);
            }
            return FinishToken(TokenKind::Operator_Xor);
        }
        case '&':
        {
            ConsumeCodePoint();
            cur = CurrentCodePoint();
            switch (cur) {
                case '=':
                    ConsumeCodePoint();
                    return FinishToken(TokenKind::Operator_BitAndAssign);
                case '&':
                    ConsumeCodePoint();
                    return FinishToken(TokenKind::Operator_And);
                default:
                    return FinishToken(TokenKind::Operator_Ampersand);
            }
        }
        case '|':
        {
            ConsumeCodePoint();
            cur = CurrentCodePoint();
            switch (cur) {
                case '=':
                    ConsumeCodePoint();
                    return FinishToken(TokenKind::Operator_BitOrAssign);
                case '|':
                    ConsumeCodePoint();
                    return FinishToken(TokenKind::Operator_Or);
                default:
                    return FinishToken(TokenKind::Operator_BitOr);
            }
        }
        case '=':
        {
            ConsumeCodePoint();
            cur = CurrentCodePoint();
            if (cur == '=') {
                ConsumeCodePoint();
                return FinishToken(TokenKind::Operator_Equals);
            }
            return FinishToken(TokenKind::Operator_Assign);
        }
        case '<':
        {
            ConsumeCodePoint();
            cur = CurrentCodePoint();
            switch (cur) {
                case ':':
                    // [lex.pptoken] has a special maximal-munch exception for
                    // <:: when the following character is neither ':' nor '>'.
                    // In that case this is '<' followed by '::', not the '<:'
                    // digraph followed by ':'.
                    if (PeekByte() == ':' && PeekByte(1) == ':' &&
                    PeekByte(2) != ':' && PeekByte(2) != '>')
                {
                        return FinishToken(TokenKind::Operator_Less);
                    }
                    ConsumeCodePoint();
                    return FinishToken(TokenKind::Operator_OpenBracket);
                case '%':
                    ConsumeCodePoint();
                    return FinishToken(TokenKind::Operator_OpenBrace);
                case '=':
                {
                    ConsumeCodePoint();
                    cur = CurrentCodePoint();
                    if (cur == '>') {
                        ConsumeCodePoint();
                        return FinishToken(TokenKind::Operator_Spaceship);
                    }
                    return FinishToken(TokenKind::Operator_LessEquals);
                }
                case '<':
                {
                    ConsumeCodePoint();
                    cur = CurrentCodePoint();
                    if (cur == '=') {
                        ConsumeCodePoint();
                        return FinishToken(TokenKind::Operator_LeftShiftAssign);
                    }
                    return FinishToken(TokenKind::Operator_LeftShift);
                }
                default:
                    return FinishToken(TokenKind::Operator_Less);
            }
            break;
        }
        case '>':
        {
            ConsumeCodePoint();
            cur = CurrentCodePoint();
            switch (cur) {
                case '=':
                    ConsumeCodePoint();
                    return FinishToken(TokenKind::Operator_GreaterEquals);
                case '>':
                {
                    ConsumeCodePoint();
                    cur = CurrentCodePoint();
                    if (cur == '=') {
                        ConsumeCodePoint();
                        return FinishToken(
                            TokenKind::Operator_RightShiftAssign);
                    }
                    return FinishToken(TokenKind::Operator_RightShift);
                }
                default:
                    return FinishToken(TokenKind::Operator_Greater);
            }
        }
        case ',':
            ConsumeCodePoint();
            return FinishToken(TokenKind::Operator_Comma);
        case '\'':
            return ParseCharacterLiteral();
        case '"':
            return ParseStringLiteral();
        default:
        {
            if (IsIdStart(cur)) {
                return ParseIdentifier();
            }
            if (IsDigit(cur)) {
                return ParseNumber();
            }
            {
                ConsumeCodePoint();
                if (CurrentCodePointValid()) {
                    OnError(TokenizationErrorKind::UnexpectedToken);
                }
                return FinishToken(TokenKind::Invalid);
            }
        }
    }
}

bool Tokenizer::IsFileStart() const noexcept
{
    return CurrentCodePointOffset_ == 0;
}

bool Tokenizer::TrySkipNewline()
{
    while (IsWhitespace(CurrentCodePoint()) && CurrentCodePoint() != '\n') {
        ConsumeCodePoint();
    }
    if (CurrentCodePoint() == '\n') {
        ConsumeCodePoint(NewlineHandling::Ignore);
        return true;
    }
    return false;
}

void Tokenizer::SkipWhitespace()
{
    bool skippedNewline;
    do {
        skippedNewline = false;
        while (true) {
            auto byte = static_cast<unsigned char>(PeekByte());
            if (byte == '\n') {
                ++CurrentCodePointOffset_;
                ++CurrentCodePointPosition_.Line;
                CurrentCodePointPosition_.Column = 1;
                CurrentCodePointSize_ = UnloadedCodePoint;
                IsNewLine_ = true;
            } else if (byte ==
                ' ' || byte == '\t' || byte == '\r' || byte == '\f' ||
                byte == '\v')
            {
                ++CurrentCodePointOffset_;
                ++CurrentCodePointPosition_.Column;
                CurrentCodePointSize_ = UnloadedCodePoint;
            } else {
                break;
            }
        }
        if (IsFileStart() && CurrentCodePoint() ==
            unicode::utf8::ByteOrderMark)
        {
            ConsumeCodePoint();
            skippedNewline = true;
            continue;
        }
        if (CurrentCodePoint() == '\\') {
            ConsumeCodePoint();
            skippedNewline = TrySkipNewline();
        }
    } while (skippedNewline);
}

std::string_view Tokenizer::CurrentTokenText() const noexcept
{
    return Input_.substr(CurrentTokenOffset_, CurrentTokenSize_);
}

Token Tokenizer::ParseIdentifier()
{
    ASSERT(IsIdStart(CurrentCodePoint()))
    while (true) {
        auto byte = static_cast<unsigned char>(PeekByte());
        if (byte < 0x80) [[likely]] {
            if (!((byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z')
                  || byte == '_' || (byte >= '0' && byte <= '9')))
            {
                break;
            }
            ++CurrentCodePointOffset_;
            ++CurrentTokenSize_;
            ++CurrentCodePointPosition_.Column;
            CurrentCodePointSize_ = UnloadedCodePoint;
            IsNewLine_ = false;
            continue;
        }
        if (!IsIdContinuation(CurrentCodePoint())) {
            break;
        }
        ConsumeCodePoint();
    }
    auto cur = CurrentCodePoint();

    if (cur == '\'') {
        if (std::ranges::find(EncodingPrefixes, CurrentTokenText()) !=
            EncodingPrefixes.end())
        {
            return ParseCharacterLiteral();
        }
    } else if (cur == '"') {
        if (std::ranges::find(EncodingPrefixes, CurrentTokenText()) !=
            EncodingPrefixes.end())
        {
            return ParseStringLiteral();
        }
        if (std::ranges::find(RawEncodingPrefixes, CurrentTokenText()) !=
            RawEncodingPrefixes.end())
        {
            return ParseRawStringLiteral();
        }
    }

    return FinishToken(detail::ClassifyIdentifier(CurrentTokenText()));
}

bool Tokenizer::ConsumeEscapeSequence()
{
    ASSERT(CurrentCodePoint() == '\\');
    ConsumeCodePoint();
    auto cur = CurrentCodePoint();

    switch (cur) {
        case '\'':
        case '"':
        case '?':
        case '\\':
        case 'a':
        case 'b':
        case 'f':
        case 'n':
        case 'r':
        case 't':
        case 'v':
            ConsumeCodePoint();
            return true;
        case '\n':
            ConsumeCodePoint(NewlineHandling::Ignore);
            return true;
        case '\r':
            if (PeekByte() == '\r' && PeekByte(1) == '\n') {
                ConsumeCodePoint();
                ConsumeCodePoint(NewlineHandling::Ignore);
                return true;
            }
            break;
        default:
            break;
    }

    if (IsOctalDigit(cur)) {
        for (int count = 0;
            count < 3 && IsOctalDigit(CurrentCodePoint());
            ++count)
        {
            ConsumeCodePoint();
        }
        return true;
    }

    auto consumeBracedDigits = [&] (auto isDigit) {
        ASSERT(CurrentCodePoint() == '{');
        ConsumeCodePoint();
        bool hasDigits = false;
        while (isDigit(CurrentCodePoint())) {
            ConsumeCodePoint();
            hasDigits = true;
        }
        if (CurrentCodePoint() != '}') {
            return false;
        }
        ConsumeCodePoint();
        return hasDigits;
    };

    if (cur == 'x') {
        ConsumeCodePoint();
        if (CurrentCodePoint() == '{') {
            if (consumeBracedDigits(IsHexDigit)) {
                return true;
            }
        } else if (IsHexDigit(CurrentCodePoint())) {
            while (IsHexDigit(CurrentCodePoint())) {
                ConsumeCodePoint();
            }
            return true;
        }
        OnError(TokenizationErrorKind::InvalidEscapeSequence);
        return false;
    }

    if (cur == 'o') {
        ConsumeCodePoint();
        if (CurrentCodePoint() == '{' && consumeBracedDigits(IsOctalDigit)) {
            return true;
        }
        OnError(TokenizationErrorKind::InvalidEscapeSequence);
        return false;
    }

    if (cur == 'u' || cur == 'U') {
        auto marker = cur;
        ConsumeCodePoint();
        uint32_t value = 0;
        bool valid = true;
        if (marker == 'u' && CurrentCodePoint() == '{') {
            ConsumeCodePoint();
            int digits = 0;
            while (IsHexDigit(CurrentCodePoint())) {
                auto digit = CurrentCodePoint();
                value = value > 0x10ffff / 16
                    ? 0x110000
                    : value * 16 +
                        (IsDigit(digit)
                               ? static_cast<uint32_t>(digit) - '0'
                               : (static_cast<uint32_t>(digit) | 0x20) - 'a' +
                                   10);
                ConsumeCodePoint();
                ++digits;
            }
            valid = digits != 0 && CurrentCodePoint() == '}';
            if (CurrentCodePoint() == '}') {
                ConsumeCodePoint();
            }
        } else {
            int expectedDigits = marker == 'u' ? 4 : 8;
            for (int i = 0; i < expectedDigits; ++i) {
                auto digit = CurrentCodePoint();
                if (!IsHexDigit(digit)) {
                    valid = false;
                    break;
                }
                value = value * 16 +
                    (IsDigit(digit)
                           ? static_cast<uint32_t>(digit) - '0'
                           : (static_cast<uint32_t>(digit) | 0x20) - 'a' + 10);
                ConsumeCodePoint();
            }
        }
        if (!valid || value > 0x10ffff || (value >=
            0xd800 && value <= 0xdfff))
        {
            OnError(TokenizationErrorKind::InvalidUniversalCharacter);
            return false;
        }
        return true;
    }

    if (cur == 'N') {
        ConsumeCodePoint();
        if (CurrentCodePoint() == '{') {
            ConsumeCodePoint();
            bool hasName = false;
            while (CurrentCodePoint() != '}' && CurrentCodePoint() != '\n' &&
                !AtEnd())
            {
                ConsumeCodePoint();
                hasName = true;
            }
            if (hasName && CurrentCodePoint() == '}') {
                ConsumeCodePoint();
                return true;
            }
        }
        OnError(TokenizationErrorKind::InvalidUniversalCharacter);
        return false;
    }

    if (!AtEnd() && cur != '\n') {
        ConsumeCodePoint();
    }
    OnError(TokenizationErrorKind::InvalidEscapeSequence);
    return false;
}

Token Tokenizer::ParseCharacterLiteral()
{
    ASSERT(CurrentCodePoint() == '\'');
    auto prefix = CurrentTokenText();
    auto encodingErrorCount = EncodingErrorCount_;
    ConsumeCodePoint();
    bool valid = true;
    size_t characterCount = 0;
    while (CurrentCodePoint() != '\'' && CurrentCodePoint() != '\n' &&
        CurrentCodePoint() != '\0')
    {
        if (CurrentCodePoint() == '\\') {
            bool lineSplice = PeekByte(
                1) == '\n' || (PeekByte(
                1) == '\r' && PeekByte(
                2) == '\n');
            valid &= ConsumeEscapeSequence();
            characterCount += !lineSplice;
        } else {
            ConsumeCodePoint();
            ++characterCount;
        }
    }
    valid &= encodingErrorCount == EncodingErrorCount_;
    if (CurrentCodePoint() != '\'') {
        OnError(TokenizationErrorKind::UnterminatedCharacterLiteral);
        return FinishToken(TokenKind::Invalid);
    }
    ConsumeCodePoint();
    if (characterCount == 0) {
        OnError(TokenizationErrorKind::EmptyCharacterLiteral);
        valid = false;
    }
    if ((prefix == "u8" || prefix == "u" || prefix == "U") &&
        characterCount > 1)
    {
        OnError(TokenizationErrorKind::TooManyCharactersInCharacterLiteral);
        valid = false;
    }
    return FinishToken(
        valid ? TokenKind::Literal_Character : TokenKind::Invalid);
}

Token Tokenizer::ParseStringLiteral()
{
    ASSERT(CurrentCodePoint() == '"');
    auto encodingErrorCount = EncodingErrorCount_;
    ConsumeCodePoint();
    bool valid = true;
    while (CurrentCodePoint() != '"' && CurrentCodePoint() != '\n' &&
        CurrentCodePoint() != '\0')
    {
        if (CurrentCodePoint() == '\\') {
            valid &= ConsumeEscapeSequence();
        } else {
            ConsumeCodePoint();
        }
    }
    valid &= encodingErrorCount == EncodingErrorCount_;
    if (CurrentCodePoint() != '"') {
        OnError(TokenizationErrorKind::UnterminatedStringLiteral);
        return FinishToken(TokenKind::Invalid);
    }
    ConsumeCodePoint();
    return FinishToken(valid ? TokenKind::Literal_String : TokenKind::Invalid);
}

Token Tokenizer::ParseRawStringLiteral()
{
    auto cur = CurrentCodePoint();
    ASSERT(cur == '"');
    auto next = [&] {
        ConsumeCodePoint();
        cur = CurrentCodePoint();
    };
    next();

    auto delimiterStart = CurrentCodePointOffset_;
    auto encodingErrorCount = EncodingErrorCount_;
    size_t delimiterLength = 0;
    bool valid = true;
    bool reportedInvalidDelimiter = false;
    bool reportedLongDelimiter = false;
    while (!AtEnd() && cur != '(' && cur != '\n') {
        auto value = static_cast<uint32_t>(cur);
        if ((value < 0x21 || value > 0x7e || cur == ')' || cur == '\\') &&
            !reportedInvalidDelimiter)
        {
            OnError(TokenizationErrorKind::InvalidRawStringDelimiter);
            reportedInvalidDelimiter = true;
            valid = false;
        }
        ++delimiterLength;
        if (delimiterLength > 16 && !reportedLongDelimiter) {
            OnError(TokenizationErrorKind::RawStringDelimiterTooLong);
            reportedLongDelimiter = true;
            valid = false;
        }
        next();
    }

    if (AtEnd() || cur != '(') {
        OnError(TokenizationErrorKind::UnterminatedRawStringLiteral);
        return FinishToken(TokenKind::Invalid);
    }

    auto delimiter =
        Input_.substr(delimiterStart, CurrentCodePointOffset_ - delimiterStart);
    next();

    while (!AtEnd()) {
        if (cur == ')') {
            auto suffix = Input_.substr(CurrentCodePointOffset_ + 1);
            if (suffix.starts_with(delimiter) &&
                suffix.size() > delimiter.size() &&
                suffix[delimiter.size()] == '"')
            {
                auto closingDelimiterEnd =
                    CurrentCodePointOffset_ + 2 + delimiter.size();
                do {
                    ConsumeCodePoint();
                } while (CurrentCodePointOffset_ < closingDelimiterEnd);
                valid &= encodingErrorCount == EncodingErrorCount_;
                return FinishToken(
                    valid ? TokenKind::Literal_String : TokenKind::Invalid);
            }
        }
        next();
    }

    OnError(TokenizationErrorKind::UnterminatedRawStringLiteral);
    return FinishToken(TokenKind::Invalid);
}

Token Tokenizer::ParseNumber()
{
    size_t offset = CurrentCodePointOffset_;
    auto byte = [&] (size_t position) {
        return ByteAt(position);
    };

    if (byte(offset) == '.') {
        return ParseFloatingPoint();
    }
    if (byte(offset) == '0' &&
        (byte(offset + 1) == 'x' || byte(offset + 1) == 'X'))
    {
        offset += 2;
        while (IsHexDigit(byte(offset)) || byte(offset) == '\'') {
            ++offset;
        }
        if (byte(offset) == '.') {
            return ParseFloatingPoint();
        }
        if (byte(offset) == 'p' || byte(offset) == 'P') {
            return ParseFloatingPoint();
        }
        return ParseInteger();
    }

    while (IsDigit(byte(offset)) || byte(offset) == '\'') {
        ++offset;
    }
    if (byte(offset) == '.' || byte(offset) == 'e' || byte(offset) == 'E') {
        return ParseFloatingPoint();
    }
    return ParseInteger();
}

Token Tokenizer::ParseFloatingPoint()
{
    auto cur = CurrentCodePoint();
    ASSERT(cur == '.' || IsDigit(cur));
    bool valid = true;
    auto next = [&] {
        ConsumeCodePoint();
        cur = CurrentCodePoint();
    };
    auto parseDigits = [&] (auto isDigit) {
        size_t count = 0;
        bool previousSeparator = false;
        bool lastSeparatorReported = false;
        while (isDigit(cur) || cur == '\'') {
            if (cur == '\'') {
                bool invalidSeparator = count == 0 || previousSeparator;
                if (invalidSeparator) {
                    OnError(TokenizationErrorKind::InvalidDigitSeparator);
                    valid = false;
                }
                previousSeparator = true;
                lastSeparatorReported = invalidSeparator;
            } else {
                ++count;
                previousSeparator = false;
                lastSeparatorReported = false;
            }
            next();
        }
        if (previousSeparator && !lastSeparatorReported) {
            OnError(TokenizationErrorKind::InvalidDigitSeparator);
            valid = false;
        }
        return count;
    };
    auto parseExponent = [&] {
        next();
        if (cur == '+' || cur == '-') {
            next();
        }
        if (parseDigits(IsDigit) == 0) {
            OnError(TokenizationErrorKind::NoDigitsInFloatingPointExponent);
            valid = false;
        }
    };

    TokenKind kind;
    if (cur == '0' && (PeekByte(1) == 'x' || PeekByte(1) == 'X')) {
        kind = TokenKind::Literal_HexadecimalFloatingPoint;
        next();
        next();
        auto digitCount = parseDigits(IsHexDigit);
        if (cur == '.') {
            next();
            digitCount += parseDigits(IsHexDigit);
        }
        if (digitCount == 0) {
            OnError(TokenizationErrorKind::NoDigitsInHexadecimalInteger);
            valid = false;
        }
        if (cur == 'p' || cur == 'P') {
            parseExponent();
        } else {
            OnError(
                TokenizationErrorKind::
                    HexadecimalFloatingPointRequiresExponent);
            valid = false;
        }
    } else {
        kind = TokenKind::Literal_DecimalFloatingPoint;
        if (cur == '.') {
            next();
            parseDigits(IsDigit);
        } else {
            parseDigits(IsDigit);
            if (cur == '.') {
                next();
                parseDigits(IsDigit);
            }
        }
        if (cur == 'e' || cur == 'E') {
            parseExponent();
        }
    }

    size_t suffixStart = CurrentTokenText().size();
    if (IsIdStart(cur)) {
        do {
            next();
        } while (IsIdContinuation(cur));
    }
    if (!valid) {
        return FinishToken(TokenKind::Invalid);
    }

    auto suffix = CurrentTokenText().substr(suffixStart);
    if (!std::ranges::any_of(
        FloatingPointSuffixes,
        [&] (auto candidate) {
            return EqualsIgnoringAsciiCase(suffix, candidate);
        }))
    {
        return FinishToken(TokenKind::Literal_UserDefinedFloatingPoint);
    }
    return FinishToken(kind);
}

Token Tokenizer::ParseInteger()
{
    TokenKind kind = TokenKind::Invalid;
    auto cur = CurrentCodePoint();
    ASSERT(IsDigit(cur));
    ConsumeCodePoint();
    int parsedDigits = 1;
    auto prev = cur;
    cur = CurrentCodePoint();
    auto next = [&] {
        ConsumeCodePoint();
        prev = cur;
        cur = CurrentCodePoint();
    };
    if (prev == '0') {
        switch (cur) {
            case 'b':
            case 'B':
            {
                kind = TokenKind::Literal_BinaryInteger;
                next();
                parsedDigits = 0;
                while (cur == '0' || cur == '1' || cur == '\'') {
                    if (cur == '\'') {
                        if (prev == '\'') {
                            OnError(
                                TokenizationErrorKind::
                                    NoDigitsBetweenDigitSeparators);
                            kind = TokenKind::Invalid;
                        } else if (parsedDigits == 0) {
                            OnError(
                                TokenizationErrorKind::
                                    DigitSeparatorAtStartOfBinaryInteger);
                            kind = TokenKind::Invalid;
                        }
                    } else {
                        parsedDigits++;
                    }
                    next();
                }
                if (IsDigit(cur)) {
                    OnError(TokenizationErrorKind::InvalidDigitInBinaryInteger);
                    kind = TokenKind::Invalid;
                    while (IsDigit(cur) || cur == '\'') {
                        next();
                    }
                }
                if (parsedDigits == 0) {
                    OnError(TokenizationErrorKind::NoDigitsInBinaryInteger);
                    kind = TokenKind::Invalid;
                } else if (prev == '\'') {
                    OnError(
                        TokenizationErrorKind::
                            DigitSeparatorAtEndOfBinaryInteger);
                    kind = TokenKind::Invalid;
                }
                break;
            }
            case 'x':
            case 'X':
            {
                kind = TokenKind::Literal_HexadecimalInteger;
                parsedDigits = 0;
                next();
                while (IsHexDigit(cur) || cur == '\'') {
                    if (cur == '\'') {
                        if (prev == '\'') {
                            OnError(
                                TokenizationErrorKind::
                                    NoDigitsBetweenDigitSeparators);
                            kind = TokenKind::Invalid;
                        } else if (parsedDigits == 0) {
                            OnError(
                                TokenizationErrorKind::
                                    DigitSeparatorAtStartOfHexadecimalInteger);
                            kind = TokenKind::Invalid;
                        }
                    } else {
                        parsedDigits++;
                    }
                    next();
                }
                if (parsedDigits == 0) {
                    OnError(
                        TokenizationErrorKind::NoDigitsInHexadecimalInteger);
                    kind = TokenKind::Invalid;
                } else if (prev == '\'') {
                    OnError(
                        TokenizationErrorKind::
                            DigitSeparatorAtEndOfHexadecimalInteger);
                    kind = TokenKind::Invalid;
                }
                break;
            }
            default:
            {
                kind = TokenKind::Literal_OctalInteger;
                while (IsOctalDigit(cur) || cur == '\'') {
                    if (cur == '\'') {
                        if (prev == '\'') {
                            OnError(
                                TokenizationErrorKind::
                                    NoDigitsBetweenDigitSeparators);
                            kind = TokenKind::Invalid;
                        }
                    } else {
                        parsedDigits++;
                    }
                    next();
                }
                if (IsDigit(cur)) {
                    OnError(TokenizationErrorKind::InvalidDigitInOctalInteger);
                    kind = TokenKind::Invalid;
                    while (IsDigit(cur) || cur == '\'') {
                        next();
                    }
                }
                if (prev == '\'') {
                    OnError(
                        TokenizationErrorKind::
                            DigitSeparatorAtEndOfOctalInteger);
                    kind = TokenKind::Invalid;
                }
                break;
            }
        }
    } else {
        kind = TokenKind::Literal_DecimalInteger;
        while (IsDigit(cur) || cur == '\'') {
            if (cur == '\'') {
                if (prev == '\'') {
                    OnError(
                        TokenizationErrorKind::NoDigitsBetweenDigitSeparators);
                    kind = TokenKind::Invalid;
                }
            } else {
                parsedDigits++;
            }
            ConsumeCodePoint();
            prev = cur;
            cur = CurrentCodePoint();
        }
        if (prev == '\'') {
            OnError(TokenizationErrorKind::DigitSeparatorAtEndOfDecimalInteger);
            return FinishToken(TokenKind::Invalid);
        }
    }

    size_t suffixStart = CurrentTokenText().size();

    if (IsIdStart(cur)) {
        auto isDigitSeparator = [&] {
            return cur == '\'' && CurrentCodePointOffset_ != 0 &&
                IsAsciiDigit(
                    static_cast<unsigned char>(
                        ByteAt(CurrentCodePointOffset_ - 1))) &&
                IsAsciiDigit(static_cast<unsigned char>(PeekByte(1)));
        };
        do {
            next();
        } while (IsIdContinuation(cur) || isDigitSeparator());
    }

    if (kind == TokenKind::Invalid) {
        return FinishToken(kind);
    }

    auto suffix = CurrentTokenText().substr(suffixStart);
    if (!std::ranges::any_of(
        IntegerSuffixes,
        [&] (auto candidate) {
            return EqualsIgnoringAsciiCase(suffix, candidate);
        }))
    {
        return FinishToken(TokenKind::Literal_UserDefinedInteger);
    }
    return FinishToken(kind);
}

Token Tokenizer::ParseInlineComment()
{
    auto cur = CurrentCodePoint();
    ASSERT(cur == '/');
    ConsumeCodePoint();
    cur = CurrentCodePoint();
    while (cur != '\n' && cur != '\0') {
        ConsumeCodePoint();
        cur = CurrentCodePoint();
    }
    return FinishToken(TokenKind::Comment);
}

Token Tokenizer::ParseMultilineComment()
{
    auto cur = CurrentCodePoint();
    ASSERT(cur == '*');
    ConsumeCodePoint();
    cur = CurrentCodePoint();
    while (cur != '\0') {
        if (cur == '*') {
            ConsumeCodePoint();
            cur = CurrentCodePoint();
            if (cur == '/') {
                ConsumeCodePoint();
                break;
            }
        } else {
            ConsumeCodePoint();
            cur = CurrentCodePoint();
        }
    }

    if (cur == '\0') {
        OnError(TokenizationErrorKind::UnterminatedMultilineComment);
        return FinishToken(TokenKind::Invalid);
    }
    return FinishToken(TokenKind::Comment);
}

Token Tokenizer::ParsePreprocessingDirective()
{
    auto cur = CurrentCodePoint();
    ASSERT(cur == '#' || CurrentTokenText() == "%:");

    bool inBlockComment = false;
    while (cur != '\0') {
        if (inBlockComment) {
            if (cur == '*') {
                ConsumeCodePoint();
                cur = CurrentCodePoint();
                if (cur == '/') {
                    ConsumeCodePoint();
                    cur = CurrentCodePoint();
                    inBlockComment = false;
                }
            } else {
                ConsumeCodePoint();
                cur = CurrentCodePoint();
            }
            continue;
        }

        if (cur == '\n') {
            break;
        }
        if (cur == '\\') {
            ConsumeCodePoint();
            TrySkipNewline();
            cur = CurrentCodePoint();
            continue;
        }
        if (cur == '/') {
            ConsumeCodePoint();
            cur = CurrentCodePoint();
            if (cur == '*') {
                ConsumeCodePoint();
                cur = CurrentCodePoint();
                inBlockComment = true;
            } else if (cur == '/') {
                while (cur != '\n' && cur != '\0') {
                    ConsumeCodePoint();
                    cur = CurrentCodePoint();
                    if (cur == '\\') {
                        ConsumeCodePoint();
                        TrySkipNewline();
                        cur = CurrentCodePoint();
                    }
                }
            }
            continue;
        }
        if (cur == 'R' && PeekByte(1) == '"') {
            ConsumeCodePoint(); // R
            ConsumeCodePoint(); // "
            auto delimiterStart = CurrentCodePointOffset_;
            cur = CurrentCodePoint();
            while (cur != '(' && cur != '\n' && cur != '\0') {
                ConsumeCodePoint();
                cur = CurrentCodePoint();
            }
            if (cur != '(') {
                continue;
            }
            auto delimiter = Input_.substr(
                delimiterStart,
                CurrentCodePointOffset_ - delimiterStart);
            ConsumeCodePoint();
            cur = CurrentCodePoint();
            while (cur != '\0') {
                if (cur == ')') {
                    auto suffix = Input_.substr(CurrentCodePointOffset_ + 1);
                    if (suffix.starts_with(delimiter) &&
                        suffix.size() > delimiter.size() &&
                        suffix[delimiter.size()] == '"')
                    {
                        ConsumeCodePoint();
                        for (size_t i = 0; i <= delimiter.size(); ++i) {
                            ConsumeCodePoint();
                        }
                        cur = CurrentCodePoint();
                        break;
                    }
                }
                ConsumeCodePoint();
                cur = CurrentCodePoint();
            }
            continue;
        }
        bool digitSeparator =
            cur == '\'' && CurrentCodePointOffset_ != 0 &&
            IsAsciiAlphaNumeric(
                static_cast<unsigned char>(ByteAt(CurrentCodePointOffset_ -
                    1))) &&
            IsAsciiAlphaNumeric(static_cast<unsigned char>(PeekByte(1)));
        if ((cur == '\'' && !digitSeparator) || cur == '"') {
            auto quote = cur;
            ConsumeCodePoint();
            cur = CurrentCodePoint();
            while (!AtEnd() && cur != quote && cur != '\n') {
                if (cur == '\\') {
                    ConsumeCodePoint();
                    cur = CurrentCodePoint();
                    if (AtEnd()) {
                        break;
                    }
                }
                ConsumeCodePoint();
                cur = CurrentCodePoint();
            }
            if (cur == quote) {
                ConsumeCodePoint();
                cur = CurrentCodePoint();
            }
            continue;
        }

        ConsumeCodePoint();
        cur = CurrentCodePoint();
    }
    return FinishToken(TokenKind::PreprocessingDirective);
}

void Tokenizer::OnError(TokenizationError error)
{
    if (ErrorHandler_) {
        ErrorHandler_(std::move(error));
    } else {
        std::cerr << "Tokenization error at Ln " << error.ErrorPosition.Line
                  << ", Col " << error.ErrorPosition.Column << ": "
                  << GetTokenizationErrorDescription(error.Kind) << '\n';
    }
}

void Tokenizer::OnError(TokenizationErrorKind error)
{
    OnError({
        .Kind = error,
        .TokenStart = CurrentTokenPosition_,
        .ErrorPosition = CurrentCodePointPosition_,
        .TokenEnd = CurrentCodePointPosition_,
    });
}

} // namespace tokenizer
