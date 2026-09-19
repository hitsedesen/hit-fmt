#include "tokenizer.hpp"

#include "identifier_kind.hpp"
#include "token.hpp"

#include <unicode/code_point.hpp>
#include <unicode/utf8.hpp>

#include <algorithm>
#include <array>
#include <utility>

namespace tokenizer {

bool IsWhitespace(unicode::CodePoint cp)
{
    return cp == ' ' || cp == '\t' || cp == '\n' || cp == '\r' || cp == '\f'
        || cp == '\v';
}

bool IsOctalDigit(unicode::CodePoint cp)
{
    return cp >= '0' && cp <= '7';
}

bool IsDigit(unicode::CodePoint cp)
{
    return cp >= '0' && cp <= '9';
}

bool IsHexDigit(unicode::CodePoint cp)
{
    return IsDigit(cp) || (cp >= 'a' && cp <= 'f') || (cp >= 'A' && cp <= 'F');
}

bool IsNonDigit(unicode::CodePoint cp)
{
    return (cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z') || cp == '_';
}

bool IsIdStart(unicode::CodePoint cp)
{
    if (IsNonDigit(cp)) {
        return true;
    }
    if (cp < 128) {
        return false;
    }
    return unicode::IsXIDStart(cp);
}

bool IsIdContinuation(unicode::CodePoint cp)
{
    if (IsNonDigit(cp) || IsDigit(cp)) {
        return true;
    }
    if (cp < 128) {
        return false;
    }
    return unicode::IsXIDContinue(cp);
}

Tokenizer::Tokenizer(
    std::string_view input,
    std::move_only_function<void(TokenizationError)> errorHandler)
    : Input_(input)
    , ErrorHandler_(std::move(errorHandler))
{ }

Token Tokenizer::FinishToken(TokenKind kind)
{
    return Token{
        .Position = CurrentTokenPosition_,
        .Kind = kind,
        .Text = Input_.substr(CurrentTokenOffset_, CurrentTokenSize_),
    };
}

Token Tokenizer::Get()
{
    SkipWhitespace();

    CurrentTokenPosition_ = CurrentCodePointPosition_;
    CurrentTokenOffset_ = CurrentCodePointOffset_;
    CurrentTokenSize_ = 0;

    auto cur = CurrentCodePoint();
    if (cur == '\0') [[unlikely]] {
        return FinishToken(TokenKind::EndOfFile);
    }

    switch (cur) {
    case '#':
        {
            if (!IsNewLine_) {
                ConsumeCodePoint();
                OnError(
                    std::format(
                        "Unexpected token {}",
                        unicode::utf8::FromCodePoint(cur)));
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
                    OnError("Unfinished ellipsis");
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
                    return FinishToken(
                        TokenKind::Operator_IndirectPointerToMemberDereference);
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
            if (cur == '=') {
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
                if (Input_.substr(CurrentCodePointOffset_).starts_with("::")
                    && (CurrentCodePointOffset_ + 2 >= Input_.size()
                        || (Input_[CurrentCodePointOffset_ + 2] != ':'
                            && Input_[CurrentCodePointOffset_ + 2] != '>')))
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
                OnError(
                    std::format(
                        "Unexpected token {}",
                        unicode::utf8::FromCodePoint(cur)));
                return FinishToken(TokenKind::Invalid);
            }
        }
    }
}

bool Tokenizer::IsFileStart() const
{
    return CurrentCodePointOffset_ == 0;
}

bool Tokenizer::TrySkipNewline()
{
    while (IsWhitespace(CurrentCodePoint()) && CurrentCodePoint() != '\n') {
        ConsumeCodePoint();
    }
    if (CurrentCodePoint() == '\n') {
        ConsumeCodePoint(/*skippingNewLine*/ true);
        return true;
    }
    return false;
}

void Tokenizer::SkipWhitespace()
{
    bool skippedNewline;
    do {
        skippedNewline = false;
        while (CurrentCodePointOffset_ < Input_.size()) {
            auto byte =
                static_cast<unsigned char>(Input_[CurrentCodePointOffset_]);
            if (byte == '\n') {
                ++CurrentCodePointOffset_;
                ++CurrentCodePointPosition_.Line;
                CurrentCodePointPosition_.Column = 1;
                CurrentCodePointSize_ = UnloadedCodePoint;
                IsNewLine_ = true;
            } else if (
                byte == ' ' || byte == '\t' || byte == '\r' || byte == '\f'
                || byte == '\v')
            {
                ++CurrentCodePointOffset_;
                ++CurrentCodePointPosition_.Column;
                CurrentCodePointSize_ = UnloadedCodePoint;
            } else {
                break;
            }
        }
        if (IsFileStart() && CurrentCodePoint() == unicode::utf8::ByteOrderMark)
        {
            ConsumeCodePoint();
            skippedNewline = true;
            continue;
        }
        if (CurrentCodePoint() == '\\') {
            ConsumeCodePoint();
            skippedNewline = TrySkipNewline();
        }
    }
    while (skippedNewline);
}

std::string_view Tokenizer::CurrentTokenText() const
{
    return Input_.substr(CurrentTokenOffset_, CurrentTokenSize_);
}

Token Tokenizer::ParseIdentifier()
{
    ASSERT(IsIdStart(CurrentCodePoint()))
    while (CurrentCodePointOffset_ < Input_.size()) {
        auto byte = static_cast<unsigned char>(Input_[CurrentCodePointOffset_]);
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

    constexpr auto encodingPrefixes = std::to_array({"u8", "u", "U", "L"});
    constexpr auto rawEncodingPrefixes =
        std::to_array({"R", "u8R", "uR", "UR", "LR"});

    if (cur == '\'') {
        if (std::ranges::contains(encodingPrefixes, CurrentTokenText())) {
            return ParseCharacterLiteral();
        }
    } else if (cur == '"') {
        if (std::ranges::contains(encodingPrefixes, CurrentTokenText())) {
            return ParseStringLiteral();
        }
        if (std::ranges::contains(rawEncodingPrefixes, CurrentTokenText())) {
            return ParseRawStringLiteral();
        }
    }

    return FinishToken(detail::ClassifyIdentifier(CurrentTokenText()));
}

Token Tokenizer::ParseCharacterLiteral()
{
    auto cur = CurrentCodePoint();
    ASSERT(cur == '\'');
    auto next = [&] {
        ConsumeCodePoint();
        cur = CurrentCodePoint();
    };
    next();
    while (cur != '\'' && cur != '\n' && cur != '\0') {
        // TODO(hitsedesen): Tokenizer should validate escape sequences.
        if (cur == '\\') {
            next();
        }
        next();
    }
    if (cur != '\'') {
        OnError("Unterminated character literal");
        return FinishToken(TokenKind::Invalid);
    }
    next();
    return FinishToken(TokenKind::Literal_Character);
}

Token Tokenizer::ParseStringLiteral()
{
    auto cur = CurrentCodePoint();
    ASSERT(cur == '"');
    auto next = [&] {
        ConsumeCodePoint();
        cur = CurrentCodePoint();
    };
    next();
    while (cur != '"' && cur != '\n' && cur != '\0') {
        // TODO(hitsedesen): Tokenizer should validate escape sequences.
        if (cur == '\\') {
            next();
        }
        next();
    }
    if (cur != '"') {
        OnError("Unterminated string literal");
        return FinishToken(TokenKind::Invalid);
    }
    next();
    return FinishToken(TokenKind::Literal_String);
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

    std::vector<unicode::CodePoint> delimiter;

    while (cur != '(' && cur != '\n' && cur != '\0') {
        delimiter.push_back(cur);
        next();
    }

    if (cur != '(') {
        OnError("Unterminated raw string literal");
        return FinishToken(TokenKind::Invalid);
    }

    delimiter.push_back('"');

    long long delimiterPos = -1;

    next();

    while (cur != '\0') {
        if (delimiterPos != -1) {
            if (cur == delimiter[delimiterPos]) {
                delimiterPos++;
                if (delimiterPos == delimiter.size()) {
                    next();
                    break;
                }
            } else {
                delimiterPos = -1;
            }
        }
        if (delimiterPos == -1) {
            if (cur == ')') {
                delimiterPos = 0;
            }
        }
        next();
    }

    if (cur == '\0') {
        OnError("Unterminated raw string literal");
        return FinishToken(TokenKind::Invalid);
    }
    return FinishToken(TokenKind::Literal_String);
}

Token Tokenizer::ParseNumber()
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
                            OnError("No digits between digit separators");
                            kind = TokenKind::Invalid;
                        } else if (parsedDigits == 0) {
                            OnError(
                                "Digit separator at the start of a binary "
                                "integer");
                            kind = TokenKind::Invalid;
                        }
                    } else {
                        parsedDigits++;
                    }
                    next();
                }
                if (parsedDigits == 0) {
                    OnError("No digits in binary integer");
                    kind = TokenKind::Invalid;
                } else if (prev == '\'') {
                    OnError("Digit separator at the on of a binary integer");
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
                            OnError("No digits between digit separators");
                            kind = TokenKind::Invalid;
                        } else if (parsedDigits == 0) {
                            OnError(
                                "Digit separator at the start of a hexadecimal "
                                "integer");
                            kind = TokenKind::Invalid;
                        }
                    } else {
                        parsedDigits++;
                    }
                    next();
                }
                if (parsedDigits == 0) {
                    OnError("No digits in hexadecimal integer");
                    kind = TokenKind::Invalid;
                } else if (prev == '\'') {
                    OnError(
                        "Digit separator at the on of a hexadecimal integer");
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
                            OnError("No digits between digit separators");
                            kind = TokenKind::Invalid;
                        }
                    } else {
                        parsedDigits++;
                    }
                    next();
                }
                if (prev == '\'') {
                    OnError("Digit separator at the on of an octal integer");
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
                    OnError("No digits between digit separators");
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
            OnError(
                "Digit separator cannot appear at the end of "
                "decimal integer literal");
            return FinishToken(TokenKind::Invalid);
        }
    }

    constexpr static auto validSuffixes = std::to_array(
        {"", "u", "ul", "ull", "uz", "l", "lu", "ll", "llu", "z", "zu"});

    size_t suffixStart = CurrentTokenText().size();

    if (IsIdStart(cur)) {
        next();
        while (IsIdContinuation(cur)) {
            next();
        }
    }

    if (kind == TokenKind::Invalid) {
        return FinishToken(kind);
    }

    auto suffix = std::string(CurrentTokenText().substr(suffixStart));
    for (char& c : suffix) {
        c = std::tolower(c);
    }

    if (!std::ranges::contains(validSuffixes, suffix)) {
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
        OnError("Unterminated multiline comment");
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
        if (cur == 'R'
            && Input_.substr(CurrentCodePointOffset_).starts_with("R\""))
        {
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
                delimiterStart, CurrentCodePointOffset_ - delimiterStart);
            ConsumeCodePoint();
            cur = CurrentCodePoint();
            while (cur != '\0') {
                if (cur == ')') {
                    auto suffix = Input_.substr(CurrentCodePointOffset_ + 1);
                    if (suffix.starts_with(delimiter)
                        && suffix.size() > delimiter.size()
                        && suffix[delimiter.size()] == '"')
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
        bool digitSeparator = cur == '\'' && CurrentCodePointOffset_ != 0
            && CurrentCodePointOffset_ + 1 < Input_.size()
            && std::isalnum(static_cast<unsigned char>(
                Input_[CurrentCodePointOffset_ - 1]))
            && std::isalnum(static_cast<unsigned char>(
                Input_[CurrentCodePointOffset_ + 1]));
        if ((cur == '\'' && !digitSeparator) || cur == '"') {
            auto quote = cur;
            do {
                ConsumeCodePoint();
                cur = CurrentCodePoint();
                if (cur == '\\') {
                    ConsumeCodePoint();
                    cur = CurrentCodePoint();
                    if (cur != '\0') {
                        ConsumeCodePoint();
                        cur = CurrentCodePoint();
                    }
                }
            }
            while (cur != quote && cur != '\n' && cur != '\0');
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
        std::cerr << std::format(
            "Tokenization error at {}: {}\n",
            error.ErrorPosition,
            error.Message);
    }
}

void Tokenizer::OnError(std::string error)
{
    OnError({
        .Message = std::move(error),
        .TokenStart = CurrentTokenPosition_,
        .ErrorPosition = CurrentCodePointPosition_,
        .TokenEnd = CurrentCodePointPosition_,
    });
}

} // namespace tokenizer
