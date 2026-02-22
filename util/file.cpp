#include "file.hpp"

#include <format>
#include <fstream>
#include <iostream>

namespace util {

std::string ReadEntireFile(const std::string& filePath, size_t maxSize)
{
    std::ifstream ifs{filePath, std::ios::in | std::ios::binary};
    if (!ifs.good()) {
        throw std::runtime_error(
            std::format("Failed to open file {}", filePath));
    }
    ifs.seekg(0, std::ios::end);
    size_t size = ifs.tellg();
    if (size > maxSize) {
        std::cerr << std::format(
            "Skipping file {} as it is too large: {} > {}",
            filePath,
            size,
            maxSize);
    }
    std::string buffer(size, '\0');
    ifs.seekg(0);
    ifs.read(buffer.data(), size);
    return buffer;
}

} // namespace util
