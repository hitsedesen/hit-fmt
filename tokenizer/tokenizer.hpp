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
    Tokenizer(
        std::string_view input,
        std::move_only_function<void(TokenizationError)> errorHandler = {});

    Token Get();

private:
    unicode::CodePoint CurrentCodePoint();
    size_t CurrentCodePointOffset_ = 0;
    util::Position CurrentCodePointPosition_ = {.Line = 1, .Column = 1};
    std::optional<unicode::utf8::CodePointWithSize> Current_;

    void ConsumeCodePoint(bool skippingNewLine = false);

    std::string_view CurrentTokenText() const;
    util::Position CurrentTokenPosition_;
    size_t CurrentTokenOffset_ = 0;
    size_t CurrentTokenSize_ = 0;
    Token FinishToken(TokenKind kind);

    void SkipWhitespace();
    bool TrySkipNewline();
    Token ParseIdentifier();
    Token ParseCharacterLiteral();
    Token ParseStringLiteral();
    Token ParseRawStringLiteral();
    Token ParseNumber();
    Token ParseInlineComment();
    Token ParseMultilineComment();
    Token ParsePreprocessingDirective();

    void OnError(TokenizationError error);
    void OnError(std::string error);

    std::string_view Input_;

    std::move_only_function<void(TokenizationError)> ErrorHandler_;

    bool IsFileStart() const;
    bool IsNewLine_ = true;
};

} // namespace tokenizer
