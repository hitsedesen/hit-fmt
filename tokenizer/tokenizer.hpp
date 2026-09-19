#pragma once

#include "token.hpp"

#include <unicode/code_point.hpp>
#include <unicode/utf8.hpp>
#include <util/input.hpp>

#include <functional>
#include <limits>
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
    unicode::CodePoint CurrentCodePoint()
    {
        if (CurrentCodePointSize_ == UnloadedCodePoint) {
            if (CurrentCodePointOffset_ >= Input_.size()) {
                CurrentCodePointValue_ = '\0';
                CurrentCodePointSize_ = 0;
            } else {
                auto firstByte =
                    static_cast<unsigned char>(Input_[CurrentCodePointOffset_]);
                if (firstByte < 0x80) [[likely]] {
                    CurrentCodePointValue_ = static_cast<char32_t>(firstByte);
                    CurrentCodePointSize_ = 1;
                } else {
                    auto first = unicode::utf8::GetFirstCodePoint(
                        Input_.substr(CurrentCodePointOffset_));
                    if (!first) {
                        OnError("Invalid UTF8 code point");
                        CurrentCodePointValue_ =
                            static_cast<char32_t>(firstByte);
                        CurrentCodePointSize_ = 1;
                    } else {
                        CurrentCodePointValue_ = first->Value;
                        CurrentCodePointSize_ = first->Size;
                    }
                }
            }
        }
        return CurrentCodePointValue_;
    }

    static constexpr size_t UnloadedCodePoint =
        std::numeric_limits<size_t>::max();
    size_t CurrentCodePointOffset_ = 0;
    size_t CurrentCodePointSize_ = UnloadedCodePoint;
    unicode::CodePoint CurrentCodePointValue_ = '\0';
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
