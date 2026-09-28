#pragma once

#include "formatter.hpp"

#include <array>
#include <filesystem>
#include <optional>
#include <regex>
#include <string>
#include <string_view>
#include <vector>

namespace formatter {

inline constexpr std::string_view DefaultConfigFileName = ".hit-fmt";

// These preferences use Enforce* flags instead of a Preserve config value.
inline constexpr auto EnforcedPreferences =
    std::to_array<std::string_view>({
        "SpaceInsideFunctionParameters", "SpaceInsideCallArguments",
        "InitializerBracesSpacing", "EmptyBlockInteriorSpacing",
        "ControlBlockSpaceBeforeBrace", "ClassBraceSpacing",
        "StructBraceSpacing", "UnionBraceSpacing",
        "FunctionBraceSpacing", "LambdaBraceSpacing",
        "NamespaceBraceSpacing", "ClassBracePlacement",
        "StructBracePlacement", "UnionBracePlacement",
        "FunctionBracePlacement", "LambdaBracePlacement",
        "NamespaceBracePlacement", "FunctionParameterSplit",
        "CallArgumentSplit", "InitializerListSplit", "TernarySplit"});

[[nodiscard]] constexpr bool AllowsPreserveConfigValue(std::string_view key)
{
    for (auto preference : EnforcedPreferences) {
        if (preference == key) {
            return false;
        }
    }
    return true;
}

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

// Parses formatting settings and exclusion patterns from a config file.
// Unknown keys and malformed values are rejected with a descriptive error.
[[nodiscard]] Configuration LoadConfigFile(const std::filesystem::path& path);

// Finds the nearest fileName in startDirectory or one of its parents.
[[nodiscard]] std::optional<std::filesystem::path> FindStyleFile(
    std::filesystem::path startDirectory,
    std::string_view fileName = DefaultConfigFileName);

} // namespace formatter
