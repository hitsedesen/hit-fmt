#pragma once

#include <cstddef>
#include <string>

namespace util {

std::string ReadEntireFile(const std::string& filePath, size_t maxSize);

} // namespace util
