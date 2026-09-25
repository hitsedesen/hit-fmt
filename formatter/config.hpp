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

enum class NameCase
{
    Unchecked,
    CamelCase,
    PascalCase,
    SnakeCase,
    ScreamingSnakeCase,
};

struct NameRule
{
    NameCase Case = NameCase::Unchecked;
    // No prefix rule by default; an explicit [""] permits no prefix.
    std::optional<std::vector<std::string>> Prefixes;
};

struct NamingStyle
{
    NameRule Class;
    NameRule Enum;
    NameRule Union;
    NameRule Struct;
    NameRule Function;
    NameRule Concept;
    NameRule Variable;
    NameRule Namespace;
};

struct Configuration
{
    Style FormattingStyle;
    NamingStyle Naming;
    std::vector<std::regex> Exclusions;
};

// Parses formatting settings and exclusion patterns from a config file.
// Unknown keys and malformed values are rejected with a descriptive error.
[[nodiscard]] Configuration LoadConfigFile(const std::filesystem::path& path);

// Finds the nearest fileName in startDirectory or one of its parents.
[[nodiscard]] std::optional<std::filesystem::path> FindStyleFile(
    std::filesystem::path startDirectory,
    std::string_view fileName = DefaultConfigFileName);

} // namespace formatter
