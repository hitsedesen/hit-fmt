#pragma once

#include "config_annotations.hpp"
#include "formatter.hpp"
#include "policy.hpp"

#include <filesystem>
#include <functional>
#include <optional>
#include <regex>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace formatter {

inline constexpr std::string_view DefaultConfigFileName = ".hit-fmt";

struct NameRule
{
    // Unspecified roles fall back; explicit Unchecked selects a role.
    std::optional<NameCase> Case;
    // No prefix rule by default; an explicit [""] permits no prefix.
    std::optional<std::vector<std::string>> Prefixes;
    std::optional<std::vector<std::string>> Suffixes;
    std::vector<std::string> Exemptions;
};

struct NamingStyle
{
    #define FORMATTER_NAMING_FIELD(category) NameRule category;
    FORMATTER_NAMING_CATEGORIES(FORMATTER_NAMING_FIELD)
#undef FORMATTER_NAMING_FIELD
};

struct Configuration
{
    Style FormattingStyle;
    NamingStyle Naming;
    // Qualified namespace paths; each entry also excludes its descendants.
#define FORMATTER_CONFIG_FIELD(type, name, initial) type name = initial;
    FORMATTER_CONFIGURATION_OPTIONS(FORMATTER_CONFIG_FIELD)
#undef FORMATTER_CONFIG_FIELD
};

// Loads an optional include, then overrides it with this file's settings.
// Relative paths start at this file; @/ starts at a .git/.arc root.
// Unknown keys and malformed values are rejected with a descriptive error.
[[nodiscard]] Configuration LoadConfigFile(const std::filesystem::path& path);

// Finds the nearest fileName in startDirectory or one of its parents.
// Reports filesystem errors through onError; missing candidates are ignored.
[[nodiscard]] std::optional<std::filesystem::path> FindStyleFile(
    std::filesystem::path startDirectory,
    std::string_view fileName = DefaultConfigFileName,
    const std::function<void(const std::filesystem::path&, const std::error_code&)>& onError = {});

} // namespace formatter
