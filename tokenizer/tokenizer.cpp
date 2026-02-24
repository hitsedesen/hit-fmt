#include "tokenizer.hpp"

#include "token.hpp"

#include <unicode/code_point.hpp>
#include <unicode/utf8.hpp>

#include <algorithm>
#include <array>
#include <unordered_map>
#include <utility>

namespace tokenizer {

bool IsWhitespace(unicode::CodePoint cp)
{
    if (cp > 255) {
        return false;
    }
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

Tokenizer::Tokenizer(
    std::string_view input,
    std::move_only_function<void(TokenizationError)> errorHandler)
    : Input_(input)
    , ErrorHandler_(std::move(errorHandler))
{ }

unicode::CodePoint Tokenizer::CurrentCodePoint()
{
    if (!Current_) {
        if (CurrentCodePointOffset_ >= Input_.size()) {
            Current_ =
                unicode::utf8::CodePointWithSize{.Value = '\0', .Size = 0};
        } else {
            auto first = unicode::utf8::GetFirstCodePoint(
                Input_.substr(CurrentCodePointOffset_));

            if (!first) {
                OnError("Invalid UTF8 code point");
                Current_ =
                    unicode::utf8::CodePointWithSize{.Value = '\0', .Size = 0};
            } else {
                Current_ = first;
            }
        }
    }

    return Current_->Value;
}

void Tokenizer::ConsumeCodePoint(bool skippingNewLine)
{
    CurrentCodePoint();
    auto first = *std::exchange(Current_, std::nullopt);

    CurrentTokenSize_ += first.Size;
    CurrentCodePointOffset_ += first.Size;
    if (first.Value == '\n') {
        CurrentCodePointPosition_.Line++;
        CurrentCodePointPosition_.Column = 1;
        if (!skippingNewLine) {
            IsNewLine_ = true;
        }
    } else {
        CurrentCodePointPosition_.Column++;
        if (!IsWhitespace(first.Value)) {
            IsNewLine_ = false;
        }
    }
}

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
            ConsumeCodePoint();
            cur = CurrentCodePoint();
            if (cur == '=') {
                ConsumeCodePoint();
                return FinishToken(TokenKind::Operator_RemainderAssign);
            }
            if (IsNewLine_) {
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
    bool skippedNewline = false;
    do {
        while (IsWhitespace(CurrentCodePoint())
               || (IsFileStart()
                   && CurrentCodePoint() == unicode::utf8::ByteOrderMark))
        {
            ConsumeCodePoint();
        }
        if (CurrentCodePoint() == '\\') {
            ConsumeCodePoint();
            skippedNewline = TrySkipNewline();
        }
    }
    while (skippedNewline);
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

std::string_view Tokenizer::CurrentTokenText() const
{
    return Input_.substr(CurrentTokenOffset_, CurrentTokenSize_);
}

Token Tokenizer::ParseIdentifier()
{
    auto cur = CurrentCodePoint();
    ASSERT(IsIdStart(cur))
    auto next = [&] {
        ConsumeCodePoint();
        cur = CurrentCodePoint();
    };
    next();
    while (IsIdContinuation(cur)) {
        next();
    }

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

    auto mappingIt = IdentifierMapping.find(CurrentTokenText());
    if (mappingIt != IdentifierMapping.end()) {
        return FinishToken(mappingIt->second);
    }
    return FinishToken(TokenKind::Identifier);
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
    next();
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
    while (cur != '\0' && cur != '\n') {
        ConsumeCodePoint();
        cur = CurrentCodePoint();
        if (cur == '\\') {
            ConsumeCodePoint();
            TrySkipNewline();
        }
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
