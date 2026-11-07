#pragma once

#include <charconv>
#include <string_view>
#include <utility>

template <typename T>
std::pair<T, bool> TryParseInteger(std::string_view text)
{
    if (text.empty()) {
        return {};
    }

    T result{};
    auto end = text.data() + text.size();
    auto [next, error] = std::from_chars(text.data(), end, result);

    if (error != std::errc{} || next != end) {
        return {};
    }

    return {result, true};
}
