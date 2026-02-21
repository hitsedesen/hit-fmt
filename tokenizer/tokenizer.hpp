#pragma once

#include "token.hpp"

#include <unicode/code_point.hpp>
#include <unicode/utf8.hpp>
#include <util/input.hpp>

#include <functional>
#include <utility>

namespace tokenizer {

struct TokenizationError
{
    std::string Message;
    util::Position TokenStart;
    util::Position ErrorPosition;
    util::Position TokenEnd;
};

class Tokenizer
{
public:
    explicit inline Tokenizer(
        unicode::utf8::Stream* stream,
        std::move_only_function<void(TokenizationError)> errorHandler = {})
        : Stream_(stream)
        , ErrorHandler_(std::move(errorHandler))
    { }

    Token Get();

private:
    unicode::CodePoint PeekCodePoint();
    unicode::CodePoint Next_ = '\0';

    unicode::CodePoint LastConsumed();
    void ConsumeCodePoint();
    Token MakeToken(TokenKind kind);

    util::Position TokenStart_;
    std::string Consumed_;

    void SkipWhitespace();
    unicode::CodePoint GetCodePoint();
    Token ParseIdentifier();
    Token ParseCharacterLiteral();
    Token ParseStringLiteral();
    Token ParseRawStringLiteral();
    Token ParseNumber();
    Token ParseInlineComment();
    Token ParseMultilineComment();

    void OnError(TokenizationError error);
    void OnError(std::string error);

    unicode::utf8::Stream* Stream_;
    std::move_only_function<void(TokenizationError)> ErrorHandler_;

    bool IsFileStart_ = true;
    bool IsNewLine_ = true;
    bool InsidePreprocessingDirective_ = false;
    bool InsideRawString_ = false;
};

} // namespace tokenizer
