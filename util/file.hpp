#pragma once

#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>

namespace util {

class FileTooLargeError
    : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

// Reads at most maxSize bytes, then appends padding zero bytes. The returned
// size includes the padding.
[[nodiscard]] std::string ReadEntireFile(
    const std::string& filePath,
    size_t maxSize,
    size_t padding = 0);

// Replaces filePath with contents, preserving bytes exactly.
void WriteEntireFile(const std::string& filePath, std::string_view contents);

} // namespace util
