#pragma once

#include "code_point.hpp"

#include <util/input.hpp>

#include <string_view>
#include <format>
#include <cstddef>
#include <optional>

namespace unicode::utf8 {

constexpr char32_t ValueFromUtf8(std::string_view cp);

// Transform correct UTF8 byte sequence to code point.
[[nodiscard]] constexpr CodePoint ToCodePoint(std::string_view cp)
{
    return ValueFromUtf8(cp);
}

// Make a UTF8 byte sequence from code point.
[[nodiscard]] constexpr std::string FromCodePoint(CodePoint cp)
{
    auto value = static_cast<uint32_t>(cp);
    std::string bytes;
    if (value <= U'\x7f') {
        bytes.push_back(static_cast<char8_t>(value));
    } else if (value <= U'\u07ff') {
        bytes.push_back(static_cast<char8_t>(0xc0 | ((value >> 6) & 0x1f)));
        bytes.push_back(static_cast<char8_t>(0x80 | (value & 0x3f)));
    } else if (value <= U'\uffff') {
        bytes.push_back(static_cast<char8_t>(0xe0 | ((value >> 12) & 0x0f)));
        bytes.push_back(static_cast<char8_t>(0x80 | ((value >> 6) & 0x3f)));
        bytes.push_back(static_cast<char8_t>(0x80 | (value & 0x3F)));
    } else {
        bytes.push_back(static_cast<char8_t>(0xf0 | ((value >> 18) & 0x07)));
        bytes.push_back(static_cast<char8_t>(0x80 | ((value >> 12) & 0x3f)));
        bytes.push_back(static_cast<char8_t>(0x80 | ((value >> 6) & 0x3f)));
        bytes.push_back(static_cast<char8_t>(0x80 | (value & 0x3F)));
    }
    return bytes;
}

constexpr auto ByteOrderMark = unicode::CodePoint(U'\uFEFF');

class InvalidUtf8Sequence
{
public:
    InvalidUtf8Sequence(std::string sequence)
        : Value_(std::move(sequence))
    {
        ASSERT(Value_.size() > 0 && Value_.size() <= 4);
    }

private:
    std::string Value_;

    friend struct std::formatter<InvalidUtf8Sequence>;
};

} // namespace unicode::utf8

template<>
struct std::formatter<unicode::utf8::InvalidUtf8Sequence>
{
    constexpr auto parse(std::format_parse_context& ctx) const
    {
        return ctx.begin();
    }

    auto format(
        const unicode::utf8::InvalidUtf8Sequence& sequence,
        std::format_context& ctx) const
    {
        auto it = ctx.out();
        const auto& value = sequence.Value_;
        auto size = value.size();
        it = std::format_to(it, "[");
        for (size_t i = 0; i < size; ++i) {
            it = std::format_to(it, "{:#02x}", value[i]);
            if ((i + 1) < size) {
                it = std::format_to(it, " ");
            }
        }
        it = std::format_to(it, "]");
        return it;
    }
};

namespace unicode::utf8 {

class InvalidUtf8SequenceError : public std::exception
{
public:
    InvalidUtf8SequenceError(
        InvalidUtf8Sequence sequence, util::Position position)
        : Message_(
              std::format(
                  "Invalid UTF8 byte sequence {} at {}", sequence, position))
    { }

    const char* what() const noexcept override
    {
        return Message_.c_str();
    }

private:
    std::string Message_;
};

class Stream
{
public:
    Stream(util::InputStream* input)
        : Underlying_(input)
    { }

    unicode::CodePoint Peek()
    {
        if (Peeked_) {
            return *Peeked_;
        }
        Peeked_ = GetImpl();
        return *Peeked_;
    }

    unicode::CodePoint Get()
    {
        auto cp = GetImpl();
        if (cp == '\0' || cp == ByteOrderMark) {
        } else if (cp == U'\n') {
            ++Position_.Line;
            Position_.Column = 1;
        } else {
            ++Position_.Column;
        }
        return cp;
    }

    util::Position GetPosition()
    {
        return Position_;
    }

private:
    unicode::CodePoint GetImpl()
    {
        if (Peeked_) {
            auto value = std::move(*Peeked_);
            Peeked_.reset();
            return value;
        }

        int b1 = Underlying_->Get();
        if (b1 == EOF) {
            // TODO(hitsedesen): Is it ok to use null as EOF symbol?
            return U'\0';
        }

        std::string result(1, b1);
        int size = 1;
        if ((b1 >> 7) == 0) {
            return ToCodePoint(result);
        } else if ((b1 >> 5) == 0b110) {
            size = 2;
        } else if ((b1 >> 4) == 0b1110) {
            size = 3;
        } else if ((b1 >> 3) == 0b11110) {
            size = 4;
        } else {
            throw InvalidUtf8SequenceError(result, Position_);
        }

        int b2 = Underlying_->Get();
        result.push_back(b2);
        if (b2 == EOF || (b2 >> 6) != 0b10) {
            throw InvalidUtf8SequenceError(result, Position_);
        }
        if (size == 2) {
            return ToCodePoint(result);
        }

        int b3 = Underlying_->Get();
        result.push_back(b3);
        if (b3 == EOF || (b3 >> 6) != 0b10) {
            throw InvalidUtf8SequenceError(result, Position_);
        }
        if (size == 3) {
            return ToCodePoint(result);
        }

        int b4 = Underlying_->Get();
        result.push_back(b4);
        if (b4 == EOF || (b4 >> 6) != 0b10) {
            throw InvalidUtf8SequenceError(result, Position_);
        }
        if (size == 4) {
            return ToCodePoint(result);
        }

        throw InvalidUtf8SequenceError(result, Position_);
    }

    util::InputStream* Underlying_;

    util::Position Position_ = {.Line = 1, .Column = 1};
    std::optional<unicode::CodePoint> Peeked_;
};

constexpr char32_t ValueFromUtf8(std::string_view cp)
{
    switch (cp.size()) {
    case 1:
        return cp[0] & 0x7f;
    case 2:
        return ((cp[0] & 0x1f) << 6) | (cp[1] & 0x3f);
    case 3:
        return ((cp[0] & 0x0f) << 12) | ((cp[1] & 0x3f) << 6) | (cp[2] & 0x3f);
    case 4:
        return ((cp[0] & 0x07) << 18)
            | ((cp[1] & 0x3f) << 12) + ((cp[2] & 0x3f) << 6) + (cp[3] & 0x3f);
    default:
        ASSERT(false);
    }
}

} // namespace unicode::utf8
