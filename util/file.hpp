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

// Append padding zero bytes so callers can safely perform bounded lookahead
// past the logical end of the file. The returned string size includes padding.
std::string
ReadEntireFile(const std::string& filePath, size_t maxSize, size_t padding = 0);

void WriteEntireFile(const std::string& filePath, std::string_view contents);

} // namespace util
