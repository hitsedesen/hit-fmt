#pragma once

#include <util/input.hpp>

#include <string>
#include <string_view>

namespace test {

struct Input
{
    std::string Storage;
    size_t Size;

    explicit Input(std::string_view text)
        : Storage(text)
        , Size(text.size())
    {
        Storage.append(util::SourcePaddingSize, '\0');
    }

    util::PaddedSourceView View() const
    {
        return {Storage.data(), static_cast<uint32_t>(Size)};
    }
};

} // namespace test
