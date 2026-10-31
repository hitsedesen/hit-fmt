#include "file.hpp"

#include <format>
#include <fstream>

namespace util {

std::string ReadEntireFile(const std::string& filePath, size_t maxSize, size_t padding)
{
    std::ifstream ifs{filePath, std::ios::in | std::ios::binary};

    if (!ifs.good()) {
        throw std::runtime_error(std::format("Failed to open file {}", filePath));
    }

    ifs.seekg(0, std::ios::end);
    size_t size = ifs.tellg();

    if (size > maxSize) {
        throw FileTooLargeError(std::format("File {} is too large: {} > {}", filePath, size, maxSize));
    }

    std::string buffer(size + padding, '\0');
    ifs.seekg(0);
    ifs.read(buffer.data(), size);
    return buffer;
}

void WriteEntireFile(const std::string& filePath, std::string_view contents)
{
    std::ofstream ofs{filePath, std::ios::out | std::ios::binary};

    if (!ofs.good()) {
        throw std::runtime_error(std::format("Failed to open file {} for writing", filePath));
    }

    ofs.write(contents.data(), static_cast<std::streamsize>(contents.size()));

    if (!ofs.good()) {
        throw std::runtime_error(std::format("Failed to write file {}", filePath));
    }
}

} // namespace util
