#pragma once

#include <cstddef>
#include <format>

namespace util {

struct Position
{
    size_t Line;
    size_t Column;
};

} // namespace util

template<>
struct std::formatter<util::Position>
{
    constexpr auto parse(std::format_parse_context& ctx) const
    {
        return ctx.begin();
    }

    auto format(const util::Position& position, std::format_context& ctx) const
    {
        return std::format_to(
            ctx.out(), "Ln {}, Col {}", position.Line, position.Column);
    }
};

namespace util {

class InputStream
{
public:
    virtual int Get() = 0;
    virtual int Peek() = 0;
    virtual bool Good() const = 0;
    virtual Position GetPosition() const = 0;

    virtual ~InputStream() = default;
};

template<typename UnderlyingStream>
class StdInputStreamAdapter final : public InputStream
{
public:
    StdInputStreamAdapter(UnderlyingStream underlyingStream)
        : UnderlyingStream_(std::move(underlyingStream))
    { }

    int Get() override
    {
        int c = UnderlyingStream_.get();
        if (c == EOF) {
        } else if (c == '\n') {
            ++Position_.Line;
            Position_.Column = 1;
        } else {
            ++Position_.Column;
        }

        return c;
    }

    int Peek() override
    {
        return UnderlyingStream_.peek();
    }

    bool Good() const override
    {
        return UnderlyingStream_.good();
    }

    Position GetPosition() const override
    {
        return Position_;
    }

private:
    UnderlyingStream UnderlyingStream_;
    Position Position_ = {.Line = 1, .Column = 1};
};

} // namespace util
