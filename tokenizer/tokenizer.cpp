#include "tokenizer.hpp"

#include "token.hpp"
#include "unicode/utf8.hpp"

#include <unicode/code_point.hpp>

#include <algorithm>
#include <array>
#include <unordered_map>
#include <utility>

namespace tokenizer {

bool IsPunctuation(unicode::CodePoint cp)
{
    return std::ispunct(cp);
}

bool IsWhitespace(unicode::CodePoint cp)
{
    return std::isspace(cp);
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
    return unicode::IsXIDStart(cp);
}

bool IsIdContinuation(unicode::CodePoint cp)
{
    if (IsNonDigit(cp) || IsDigit(cp)) {
        return true;
    }
    return unicode::IsXIDContinue(cp);
}

unicode::CodePoint Tokenizer::PeekCodePoint()
{
    if (Next_ != '\0') {
        return Next_;
    }
    return Stream_->Peek();
}

void Tokenizer::ConsumeCodePoint()
{
    Consumed_ += unicode::utf8::FromCodePoint(GetCodePoint());
}

Token Tokenizer::MakeToken(TokenKind kind)
{
    return Token{
        .Position = TokenStart_,
        .Kind = kind,
        .Text = std::exchange(Consumed_, ""),
    };
}

Token Tokenizer::Get()
{
    SkipWhitespace();

    TokenStart_ = Stream_->GetPosition();

    auto cur = PeekCodePoint();
    if (cur == '\0') [[unlikely]] {
        return MakeToken(TokenKind::EndOfFile);
    }

    bool preprocessing = IsNewLine_ || InsidePreprocessingDirective_;

    switch (cur) {
    case '#':
        {
            ConsumeCodePoint();
            cur = PeekCodePoint();
            if (cur == '#') {
                ConsumeCodePoint();
                if (!preprocessing) {
                    OnError(
                        "Operator ## can only appear inside a preprocessing "
                        "directive");
                    return MakeToken(TokenKind::Invalid);
                }
                InsidePreprocessingDirective_ = true;
                return MakeToken(TokenKind::HashHash);
            }
            if (!preprocessing) {
                OnError(
                    "Operator # can only appear inside a preprocessing "
                    "directive");
                return MakeToken(TokenKind::Invalid);
            }
            InsidePreprocessingDirective_ = true;
            return MakeToken(TokenKind::Hash);
        }
    case '{':
        ConsumeCodePoint();
        return MakeToken(TokenKind::Operator_OpenBrace);
    case '}':
        ConsumeCodePoint();
        return MakeToken(TokenKind::Operator_CloseBrace);
    case '[':
        ConsumeCodePoint();
        return MakeToken(TokenKind::Operator_OpenBracket);
    case ']':
        ConsumeCodePoint();
        return MakeToken(TokenKind::Operator_CloseBracket);
    case '(':
        ConsumeCodePoint();
        return MakeToken(TokenKind::Operator_OpenParenthesis);
    case ')':
        ConsumeCodePoint();
        return MakeToken(TokenKind::Operator_CloseParenthesis);
    case ';':
        ConsumeCodePoint();
        return MakeToken(TokenKind::Operator_Semicolon);
    case ':':
        {
            ConsumeCodePoint();
            cur = PeekCodePoint();
            switch (cur) {
            case ':':
                ConsumeCodePoint();
                return MakeToken(TokenKind::Operator_ScopeResolution);
            case '>':
                ConsumeCodePoint();
                return MakeToken(TokenKind::Operator_CloseBracket);
            default:
                return MakeToken(TokenKind::Operator_Colon);
            }
        }
    case '.':
        {
            ConsumeCodePoint();
            cur = PeekCodePoint();
            switch (cur) {
            case '.':
                {
                    ConsumeCodePoint();
                    cur = PeekCodePoint();
                    if (cur == '.') {
                        ConsumeCodePoint();
                        return MakeToken(TokenKind::Operator_Ellipsis);
                    }
                    OnError({
                        .Message = "Unfinished ellipsis",
                        .TokenStart = TokenStart_,
                        .ErrorPosition = Stream_->GetPosition(),
                        .TokenEnd = Stream_->GetPosition(),
                    });
                    return MakeToken(TokenKind::Invalid);
                }
            case '*':
                ConsumeCodePoint();
                return MakeToken(
                    TokenKind::Operator_PointerToMemberDereference);
            default:
                return MakeToken(TokenKind::Operator_ClassMemberAccess);
            }
        }
    case '-':
        {
            ConsumeCodePoint();
            cur = PeekCodePoint();
            switch (cur) {
            case '>':
                ConsumeCodePoint();
                cur = PeekCodePoint();
                if (cur == '*') {
                    ConsumeCodePoint();
                    return MakeToken(
                        TokenKind::Operator_IndirectPointerToMemberDereference);
                }
                return MakeToken(TokenKind::Operator_IndirectClassMemberAccess);
            case '=':
                ConsumeCodePoint();
                return MakeToken(TokenKind::Operator_MinusAssign);
            case '-':
                return MakeToken(TokenKind::Operator_Decrement);
            default:
                return MakeToken(TokenKind::Operator_Minus);
            }
        }
    case '?':
        ConsumeCodePoint();
        return MakeToken(TokenKind::Operator_Conditional);
    case '~':
        ConsumeCodePoint();
        return MakeToken(TokenKind::Operator_Complement);
    case '!':
        {
            ConsumeCodePoint();
            cur = PeekCodePoint();
            if (cur == '=') {
                ConsumeCodePoint();
                return MakeToken(TokenKind::Operator_NotEquals);
            }
            return MakeToken(TokenKind::Operator_Not);
        }
    case '+':
        {
            ConsumeCodePoint();
            cur = PeekCodePoint();
            if (cur == '=') {
                ConsumeCodePoint();
                return MakeToken(TokenKind::Operator_PlusAssign);
            }
            return MakeToken(TokenKind::Operator_Plus);
        }
    case '*':
        {
            ConsumeCodePoint();
            cur = PeekCodePoint();
            if (cur == '=') {
                ConsumeCodePoint();
                return MakeToken(TokenKind::Operator_MultiplyAssign);
            }
            return MakeToken(TokenKind::Operator_Star);
        }
    case '/':
        {
            ConsumeCodePoint();
            cur = PeekCodePoint();
            switch (cur) {
            case '=':
                ConsumeCodePoint();
                return MakeToken(TokenKind::Operator_DivideAssign);
            case '/':
                return ParseInlineComment();
            case '*':
                return ParseMultilineComment();
            default:
                return MakeToken(TokenKind::Operator_Divide);
            }
            if (cur == '=') {
            }
        }
    case '%':
        {
            ConsumeCodePoint();
            cur = PeekCodePoint();
            if (cur == '=') {
                ConsumeCodePoint();
                return MakeToken(TokenKind::Operator_RemainderAssign);
            }
            if (preprocessing) {
                if (cur == ':') {
                    ConsumeCodePoint();
                    cur = PeekCodePoint();
                    if (cur == '%') {
                        ConsumeCodePoint();
                        cur = PeekCodePoint();
                        if (cur == ':') {
                            ConsumeCodePoint();
                            return MakeToken(TokenKind::HashHash);
                        } else {
                            OnError("Unfinished %:%: operator");
                            return MakeToken(TokenKind::Invalid);
                        }
                    }
                    return MakeToken(TokenKind::Hash);
                }
            }
            return MakeToken(TokenKind::Operator_Remainder);
        }
    case '^':
        {
            ConsumeCodePoint();
            cur = PeekCodePoint();
            if (cur == '=') {
                ConsumeCodePoint();
                return MakeToken(TokenKind::Operator_XorAssign);
            }
            return MakeToken(TokenKind::Operator_Xor);
        }
    case '&':
        {
            ConsumeCodePoint();
            cur = PeekCodePoint();
            switch (cur) {
            case '=':
                ConsumeCodePoint();
                return MakeToken(TokenKind::Operator_BitAndAssign);
            case '&':
                ConsumeCodePoint();
                return MakeToken(TokenKind::Operator_And);
            default:
                return MakeToken(TokenKind::Operator_Ampersand);
            }
        }
    case '|':
        {
            ConsumeCodePoint();
            cur = PeekCodePoint();
            switch (cur) {
            case '=':
                ConsumeCodePoint();
                return MakeToken(TokenKind::Operator_BitOrAssign);
            case '|':
                ConsumeCodePoint();
                return MakeToken(TokenKind::Operator_Or);
            default:
                return MakeToken(TokenKind::Operator_BitOr);
            }
        }
    case '=':
        {
            ConsumeCodePoint();
            cur = PeekCodePoint();
            if (cur == '=') {
                ConsumeCodePoint();
                return MakeToken(TokenKind::Operator_Equals);
            }
            return MakeToken(TokenKind::Operator_Assign);
        }
    case '<':
        {
            ConsumeCodePoint();
            cur = PeekCodePoint();
            switch (cur) {
            case ':':
                ConsumeCodePoint();
                return MakeToken(TokenKind::Operator_OpenBracket);
            case '%':
                ConsumeCodePoint();
                return MakeToken(TokenKind::Operator_OpenBrace);
            case '=':
                {
                    ConsumeCodePoint();
                    cur = PeekCodePoint();
                    if (cur == '>') {
                        ConsumeCodePoint();
                        return MakeToken(TokenKind::Operator_Spaceship);
                    }
                    return MakeToken(TokenKind::Operator_LessEquals);
                }
            case '<':
                {
                    ConsumeCodePoint();
                    cur = PeekCodePoint();
                    if (cur == '=') {
                        ConsumeCodePoint();
                        return MakeToken(TokenKind::Operator_LeftShiftAssign);
                    }
                    return MakeToken(TokenKind::Operator_LeftShift);
                }
            default:
                return MakeToken(TokenKind::Operator_Less);
            }
            break;
        }
    case '>':
        {
            ConsumeCodePoint();
            cur = PeekCodePoint();
            switch (cur) {
            case '=':
                ConsumeCodePoint();
                return MakeToken(TokenKind::Operator_GreaterEquals);
            case '>':
                {
                    ConsumeCodePoint();
                    cur = PeekCodePoint();
                    if (cur == '=') {
                        ConsumeCodePoint();
                        return MakeToken(TokenKind::Operator_RightShiftAssign);
                    }
                    return MakeToken(TokenKind::Operator_RightShift);
                }
            default:
                return MakeToken(TokenKind::Operator_Greater);
            }
        }
    case ',':
        ConsumeCodePoint();
        return MakeToken(TokenKind::Operator_Comma);
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
                        "Unexpected character {}",
                        unicode::utf8::FromCodePoint(cur)));
                return MakeToken(TokenKind::Invalid);
            }
        }
    }
}

void Tokenizer::SkipWhitespace()
{
start_skip_whitespace:
    while (IsWhitespace(Stream_->Peek())
           || (IsFileStart_ && Stream_->Peek() == unicode::utf8::ByteOrderMark))
    {
        GetCodePoint();
    }
    if (!InsideRawString_ && Stream_->Peek() == '\\') {
        Next_ = Stream_->Get();
        while (IsWhitespace(Stream_->Peek()) && Stream_->Peek() != '\n') {
            Stream_->Get();
        }
        if (Stream_->Peek() == '\n') {
            Stream_->Get();
            Next_ = '\0';
            goto start_skip_whitespace;
        }
    }
}

unicode::CodePoint Tokenizer::GetCodePoint()
{
    IsFileStart_ = false;
    auto res = Next_ == '\0' ? Stream_->Get() : Next_;
    Next_ = '\0';

    if (!InsideRawString_) {
        if (res == '\\') {
            auto next = Stream_->Peek();
            while (IsWhitespace(next) && next != '\n') {
                Stream_->Get();
                next = Stream_->Peek();
            }
            if (next == '\n') {
                Stream_->Get();
                res = Stream_->Get();
            }
        }
    }

    if (res == '\n' || res == '\r') {
        IsNewLine_ = true;
        InsidePreprocessingDirective_ = false;
    } else if (!IsWhitespace(res)) {
        IsNewLine_ = false;
    }
    return res;
}

namespace {

const std::unordered_map<std::string_view, TokenKind> IdentifierMapping{
    // Alternative representations.
    {"and", TokenKind::Operator_And},
    {"bitor", TokenKind::Operator_BitOr},
    {"or", TokenKind::Operator_Or},
    {"xor", TokenKind::Operator_Xor},
    {"compl", TokenKind::Operator_Complement},
    {"bitand", TokenKind::Operator_Ampersand},
    {"and_eq", TokenKind::Operator_BitAndAssign},
    {"or_eq", TokenKind::Operator_BitOrAssign},
    {"xor_eq", TokenKind::Operator_XorAssign},
    {"not", TokenKind::Operator_Not},
    {"not_eq", TokenKind::Operator_NotEquals},

    // Keywords.
    {"alignas", TokenKind::Keyword_alignas},
    {"alignof", TokenKind::Keyword_alignof},
    {"asm", TokenKind::Keyword_asm},
    {"auto", TokenKind::Keyword_auto},
    {"bool", TokenKind::Keyword_bool},
    {"break", TokenKind::Keyword_break},
    {"case", TokenKind::Keyword_case},
    {"catch", TokenKind::Keyword_catch},
    {"char", TokenKind::Keyword_char},
    {"char8_t", TokenKind::Keyword_char8_t},
    {"char16_t", TokenKind::Keyword_char16_t},
    {"char32_t", TokenKind::Keyword_char32_t},
    {"class", TokenKind::Keyword_class},
    {"concept", TokenKind::Keyword_concept},
    {"const", TokenKind::Keyword_const},
    {"consteval", TokenKind::Keyword_consteval},
    {"constexpr", TokenKind::Keyword_constexpr},
    {"constinit", TokenKind::Keyword_constinit},
    {"const_cast", TokenKind::Keyword_const_cast},
    {"continue", TokenKind::Keyword_continue},
    {"co_await", TokenKind::Keyword_co_await},
    {"co_return", TokenKind::Keyword_co_return},
    {"co_yield", TokenKind::Keyword_co_yield},
    {"decltype", TokenKind::Keyword_decltype},
    {"default", TokenKind::Keyword_default},
    {"delete", TokenKind::Keyword_delete},
    {"do", TokenKind::Keyword_do},
    {"double", TokenKind::Keyword_double},
    {"dynamic_cast", TokenKind::Keyword_dynamic_cast},
    {"else", TokenKind::Keyword_else},
    {"enum", TokenKind::Keyword_enum},
    {"explicit", TokenKind::Keyword_explicit},
    {"export", TokenKind::Keyword_export},
    {"extern", TokenKind::Keyword_extern},
    {"false", TokenKind::Keyword_false},
    {"float", TokenKind::Keyword_float},
    {"for", TokenKind::Keyword_for},
    {"friend", TokenKind::Keyword_friend},
    {"goto", TokenKind::Keyword_goto},
    {"if", TokenKind::Keyword_if},
    {"inline", TokenKind::Keyword_inline},
    {"int", TokenKind::Keyword_int},
    {"long", TokenKind::Keyword_long},
    {"mutable", TokenKind::Keyword_mutable},
    {"namespace", TokenKind::Keyword_namespace},
    {"new", TokenKind::Keyword_new},
    {"noexcept", TokenKind::Keyword_noexcept},
    {"nullptr", TokenKind::Keyword_nullptr},
    {"operator", TokenKind::Keyword_operator},
    {"private", TokenKind::Keyword_private},
    {"protected", TokenKind::Keyword_protected},
    {"public", TokenKind::Keyword_public},
    {"register", TokenKind::Keyword_register},
    {"reinterpret_cast", TokenKind::Keyword_reinterpret_cast},
    {"requires", TokenKind::Keyword_requires},
    {"return", TokenKind::Keyword_return},
    {"short", TokenKind::Keyword_short},
    {"signed", TokenKind::Keyword_signed},
    {"sizeof", TokenKind::Keyword_sizeof},
    {"static", TokenKind::Keyword_static},
    {"static_assert", TokenKind::Keyword_static_assert},
    {"static_cast", TokenKind::Keyword_static_cast},
    {"struct", TokenKind::Keyword_struct},
    {"switch", TokenKind::Keyword_switch},
    {"template", TokenKind::Keyword_template},
    {"this", TokenKind::Keyword_this},
    {"thread_local", TokenKind::Keyword_thread_local},
    {"throw", TokenKind::Keyword_throw},
    {"true", TokenKind::Keyword_true},
    {"try", TokenKind::Keyword_try},
    {"typedef", TokenKind::Keyword_typedef},
    {"typeid", TokenKind::Keyword_typeid},
    {"typename", TokenKind::Keyword_typename},
    {"union", TokenKind::Keyword_union},
    {"unsigned", TokenKind::Keyword_unsigned},
    {"using", TokenKind::Keyword_using},
    {"virtual", TokenKind::Keyword_virtual},
    {"void", TokenKind::Keyword_void},
    {"volatile", TokenKind::Keyword_volatile},
    {"wchar_t", TokenKind::Keyword_wchar_t},
    {"while", TokenKind::Keyword_while},
};

}

Token Tokenizer::ParseIdentifier()
{
    auto cur = PeekCodePoint();
    ASSERT(IsIdStart(cur))
    auto next = [&] {
        ConsumeCodePoint();
        cur = PeekCodePoint();
    };
    next();
    while (IsIdContinuation(cur)) {
        next();
    }

    constexpr auto encodingPrefixes = std::to_array({"u8", "u", "U", "L"});
    constexpr auto rawEncodingPrefixes =
        std::to_array({"R", "u8R", "uR", "UR", "LR"});

    if (cur == '\'') {
        if (std::ranges::contains(encodingPrefixes, Consumed_)) {
            return ParseCharacterLiteral();
        }
    } else if (cur == '"') {
        if (std::ranges::contains(encodingPrefixes, Consumed_)) {
            return ParseStringLiteral();
        }
        if (std::ranges::contains(rawEncodingPrefixes, Consumed_)) {
            return ParseRawStringLiteral();
        }
    }

    auto mappingIt = IdentifierMapping.find(Consumed_);
    if (mappingIt != IdentifierMapping.end()) {
        return MakeToken(mappingIt->second);
    }
    return MakeToken(TokenKind::Identifier);
}

Token Tokenizer::ParseCharacterLiteral()
{
    auto cur = PeekCodePoint();
    ASSERT(cur == '\'');
    auto next = [&] {
        ConsumeCodePoint();
        cur = PeekCodePoint();
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
        return MakeToken(TokenKind::Invalid);
    }
    next();
    return MakeToken(TokenKind::Literal_Character);
}

Token Tokenizer::ParseStringLiteral()
{
    auto cur = PeekCodePoint();
    ASSERT(cur == '"');
    auto next = [&] {
        ConsumeCodePoint();
        cur = PeekCodePoint();
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
        return MakeToken(TokenKind::Invalid);
    }
    next();
    return MakeToken(TokenKind::Literal_String);
}

Token Tokenizer::ParseRawStringLiteral()
{
    auto cur = PeekCodePoint();
    ASSERT(cur == '"');
    auto next = [&] {
        ConsumeCodePoint();
        cur = PeekCodePoint();
    };
    next();

    std::vector<unicode::CodePoint> delimiter;

    while (cur != '(' && cur != '\n' && cur != '\0') {
        delimiter.push_back(cur);
        next();
    }

    if (cur != '(') {
        OnError("Unterminated raw string literal");
        return MakeToken(TokenKind::Invalid);
    }

    delimiter.push_back('"');

    ssize_t delimiterPos = -1;

    InsideRawString_ = true;

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

    InsideRawString_ = false;

    if (cur == '\0') {
        OnError("Unterminated raw string literal");
        return MakeToken(TokenKind::Invalid);
    }
    next();
    return MakeToken(TokenKind::Literal_String);
}

Token Tokenizer::ParseNumber()
{
    TokenKind kind = TokenKind::Invalid;
    auto cur = PeekCodePoint();
    ASSERT(IsDigit(cur));
    ConsumeCodePoint();
    int parsedDigits = 1;
    auto prev = cur;
    cur = PeekCodePoint();
    auto next = [&] {
        ConsumeCodePoint();
        prev = cur;
        cur = PeekCodePoint();
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
            cur = PeekCodePoint();
        }
        if (prev == '\'') {
            OnError(
                "Digit separator cannot appear at the end of "
                "decimal integer literal");
            return MakeToken(TokenKind::Invalid);
        }
    }

    constexpr static auto validSuffixes = std::to_array(
        {"", "u", "ul", "ull", "uz", "l", "lu", "ll", "llu", "z", "zu"});

    auto suffixStart = Consumed_.size();

    if (IsIdStart(cur)) {
        next();
        while (IsIdContinuation(cur)) {
            next();
        }
    }

    if (kind == TokenKind::Invalid) {
        return MakeToken(kind);
    }

    auto suffix = std::string(Consumed_.begin() + suffixStart, Consumed_.end());
    for (char& c : suffix) {
        c = std::tolower(c);
    }

    if (!std::ranges::contains(validSuffixes, suffix)) {
        return MakeToken(TokenKind::Literal_UserDefinedInteger);
    }
    return MakeToken(kind);
}

Token Tokenizer::ParseInlineComment()
{
    auto cur = PeekCodePoint();
    ASSERT(cur == '/');
    ConsumeCodePoint();
    cur = PeekCodePoint();
    while (cur != '\n' && cur != '\0') {
        ConsumeCodePoint();
        cur = PeekCodePoint();
    }
    return MakeToken(TokenKind::Comment);
}

Token Tokenizer::ParseMultilineComment()
{
    auto cur = PeekCodePoint();
    ASSERT(cur == '*');
    ConsumeCodePoint();
    cur = PeekCodePoint();
    while (cur != '\0') {
        if (cur == '*') {
            ConsumeCodePoint();
            cur = PeekCodePoint();
            if (cur == '/') {
                ConsumeCodePoint();
                break;
            }
        } else {
            ConsumeCodePoint();
            cur = PeekCodePoint();
        }
    }

    if (cur == '\0') {
        OnError("Unterminated multiline comment");
        return MakeToken(TokenKind::Invalid);
    }
    return MakeToken(TokenKind::Comment);
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
        .TokenStart = TokenStart_,
        .ErrorPosition = Stream_->GetPosition(),
        .TokenEnd = Stream_->GetPosition(),
    });
}

} // namespace tokenizer
