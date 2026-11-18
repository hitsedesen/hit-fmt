#pragma once

#include "types.hpp"

#include <cstdint>
#include <cstring>
#include <string_view>

namespace util {

inline us PlainAsciiPrefix(std::string_view text)
{
    constexpr u64 low = 0x2020202020202020;
    constexpr u64 high = 0x8080808080808080;
    us size = 0;

    while (text.size() - size >= sizeof(u64)) {
        u64 word;
        std::memcpy(&word, text.data() + size, sizeof(word));

        // If every byte is in [0x20, 0x7f], subtraction cannot borrow
        // between bytes. Otherwise a high bit rejects the entire word.
        if (((word | (word - low)) & high) != 0) {
            break;
        }

        size += sizeof(word);
    }

    while (size < text.size()) {
        const auto byte = static_cast<unsigned char>(text[size]);

        if (byte < 0x20 || byte >= 0x80) {
            break;
        }

        ++size;
    }

    return size;
}

} // namespace util
