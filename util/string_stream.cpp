#include "string_stream.hpp"

namespace util {

StringInputStreamAdapter::StringInputStreamAdapter(std::string& str)
    : String_(str)
{ }

int StringInputStreamAdapter::Get()
{
    if (Index_ >= String_.size()) {
        return EOF;
    } else {
        unsigned char c = String_[Index_++];
        if (c == '\n') {
            ++Position_.Line;
            Position_.Column = 1;
        } else {
            ++Position_.Column;
        }
        return c;
    }
}

int StringInputStreamAdapter::Peek()
{
    if (Index_ >= String_.size()) {
        return EOF;
    }

    return String_[Index_];
}

bool StringInputStreamAdapter::Good() const
{
    return Index_ < String_.size();
}

util::Position StringInputStreamAdapter::GetPosition() const
{
    return Position_;
}

} // namespace util
