#pragma once

#include "formatter.hpp"

#include <filesystem>
#include <optional>
#include <regex>
#include <string>
#include <string_view>
#include <vector>

namespace formatter {

inline constexpr std::string_view DefaultConfigFileName = ".hit-fmt";

struct NameRule
{
    NameCase Case = NameCase::Unchecked;
    // No prefix rule by default; an explicit [""] permits no prefix.
    std::optional<std::vector<std::string>> Prefixes;
};

struct NamingStyle
{
#define FORMATTER_NAMING_FIELD(category) NameRule category;
    FORMATTER_NAMING_CATEGORIES(FORMATTER_NAMING_FIELD)
#undef FORMATTER_NAMING_FIELD
    // Qualified namespace paths; each entry also excludes its descendants.
#define FORMATTER_NAMING_OPTION_FIELD(type, name, initial, key) type name = initial;
    FORMATTER_NAMING_OPTIONS(FORMATTER_NAMING_OPTION_FIELD)
#undef FORMATTER_NAMING_OPTION_FIELD
};

struct Configuration
{
    Style FormattingStyle;
    NamingStyle Naming;
#define FORMATTER_CONFIG_FIELD(type, name, initial, key) type name = initial;
    FORMATTER_CONFIGURATION_OPTIONS(FORMATTER_CONFIG_FIELD)
#undef FORMATTER_CONFIG_FIELD
};

// Parses formatting settings and exclusion patterns from a config file.
// Unknown keys and malformed values are rejected with a descriptive error.
[[nodiscard]] Configuration LoadConfigFile(const std::filesystem::path& path);

// Finds the nearest fileName in startDirectory or one of its parents.
[[nodiscard]] std::optional<std::filesystem::path> FindStyleFile(
    std::filesystem::path startDirectory,
    std::string_view fileName = DefaultConfigFileName);

} // namespace formatter
