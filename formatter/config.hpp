#pragma once

#include "formatter.hpp"

#include <filesystem>
#include <optional>

namespace formatter {

inline constexpr std::string_view DefaultConfigFileName = ".hit-fmt";

// Parses a configuration file by applying its values over the default Style.
// Unknown keys and malformed values are rejected with a descriptive error.
[[nodiscard]] Style LoadStyleFile(const std::filesystem::path& path);

// Searches startDirectory and each parent directory for fileName.
[[nodiscard]] std::optional<std::filesystem::path> FindStyleFile(
    std::filesystem::path startDirectory,
    std::string_view fileName = DefaultConfigFileName);

} // namespace formatter
