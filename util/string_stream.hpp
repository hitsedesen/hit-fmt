#pragma once

#include "input.hpp"

namespace util {

class StringInputStreamAdapter : public util::InputStream
{
public:
    explicit StringInputStreamAdapter(std::string& str);

    int Get() override;

    int Peek() override;

    bool Good() const override;

    util::Position GetPosition() const override;

private:
    std::string& String_;
    size_t Index_ = 0;
    util::Position Position_ = {.Line = 1, .Column = 1};
};

} // namespace util
