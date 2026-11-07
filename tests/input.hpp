#pragma once

#include <util/input.hpp>
#include <util/types.hpp>

#include <string>
#include <string_view>

namespace test {

struct Input
{
    std::string Storage;
    us Size;

    explicit Input(std::string_view text)
        : Storage(text)
        , Size(text.size())
    {
        Storage.append(util::SourcePaddingSize, '\0');
    }

    util::PaddedSourceView View() const
    {
        return {Storage.data(), static_cast<u32>(Size)};
    }
};

} // namespace test
