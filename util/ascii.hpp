#pragma once

#include <cstdint>
#include <cstring>
#include <string_view>

namespace util {

// Conservatively count an ASCII text prefix without controls. Tabs,
// line boundaries, other controls and Unicode are left to the caller's scalar
// path. Reads stay within the view and require neither alignment nor padding.
inline std::size_t PlainAsciiPrefix(std::string_view text)
{
    constexpr std::uint64_t low = 0x2020202020202020;
    constexpr std::uint64_t high = 0x8080808080808080;
    std::size_t size = 0;
    while (text.size() - size >= sizeof(std::uint64_t)) {
        std::uint64_t word;
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
