#pragma once

#include <cstddef>
#include <string>

namespace util {

// Append padding zero bytes so callers can safely perform bounded lookahead
// past the logical end of the file. The returned string size includes padding.
std::string
ReadEntireFile(const std::string& filePath, size_t maxSize, size_t padding = 0);

} // namespace util
