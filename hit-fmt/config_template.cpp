#include "config_template.hpp"

#include <formatter/config.hpp>

#include <cstddef>
#include <ostream>
#include <regex>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace hit_fmt {
namespace {

std::string_view IntegerValues(std::string_view name)
{
    if (name == "IndentWidth") {
        return "1..32";
    }
    if (name == "ColumnLimit") {
        return "1..10000";
    }
    if (name == "NamespaceCommentSpaces") {
        return "1..32";
    }
    if (name == "ShortNamespaceLines") {
        return "0 (no exemption) or 1..10000";
    }
    if (name == "SeparatorLength") {
        return "0 (disabled) or 8..10000";
    }
    if (name == "CallArgumentSplitColumnLimit") {
        return "0 (use ColumnLimit) or a positive integer";
    }
    if (name == "FunctionParameterNewLineIndent") {
        return "0 (normal indent) or 1..32";
    }
    return "0 (use IndentWidth) or 1..32";
}

void PrintStringList(
    std::ostream& output,
    const std::vector<std::string>& values)
{
    output << '[';
    for (size_t i = 0; i < values.size(); ++i) {
        if (i != 0) {
            output << ", ";
        }
        output << '"';
        for (char character : values[i]) {
            if (character == '"' || character == '\\') {
                output << '\\';
            }
            output << character;
        }
        output << '"';
    }
    output << ']';
}

template <typename T>
void PrintOption(std::ostream& output, std::string_view name, const T& value)
{
    output << "# Values: ";
    if constexpr (std::is_enum_v<T>) {
        bool first = true;
        for (auto spelling : formatter::ConfigEnumNames<T>::Values) {
            if (spelling == "Preserve" &&
                !formatter::AllowsPreserveConfigValue(name))
            {
                continue;
            }
            if (!first) {
                output << " | ";
            }
            output << spelling;
            first = false;
        }
        auto preserved = formatter::DeserializeConfigEnum<T>("Preserve");
        auto setting = preserved && formatter::AllowsPreserveConfigValue(name)
            ? *preserved
            : value;
        output << "\n" << name << " = "
               << formatter::SerializeConfigEnum(setting);
    } else if constexpr (std::is_same_v<T, bool>) {
        output << "false | true\n" << name << " = "
               << (value ? "true" : "false");
    } else if constexpr (std::is_same_v<T, std::size_t>) {
        output << IntegerValues(name) << '\n' << name << " = " << value;
    } else if constexpr (std::is_same_v<T, std::vector<std::string>>) {
        if (name == "IndentedCallScopePairs") {
            output << "[] or [\"Begin:End\", ...]";
        } else if (name == "NamingNamespaceExemptions") {
            output << "[] or [\"namespace::path\", ...]";
        } else {
            output << "[] or [\"name\", ...]";
        }
        output << '\n' << name << " = ";
        PrintStringList(output, value);
    } else if constexpr (std::is_same_v<T, std::vector<std::regex>>) {
        output << "[] or [\"path regex\", ...]\n" << name << " = []";
    }
    output << "\n\n";
}

} // namespace

void PrintPreserveConfig(std::ostream& output)
{
    output << "# Preserve source layout. Options without Preserve use\n"
              "# inactive defaults; Enforce options stay false.\n\n"
              "# Formatting options\n\n";
    const formatter::Style style;
#define PRINT_STYLE_OPTION(type, name, initial) \
    PrintOption(output, #name, style.name);
    FORMATTER_STYLE_OPTIONS(PRINT_STYLE_OPTION)
#undef PRINT_STYLE_OPTION

    output << "# Naming checks\n\n";
#define PRINT_NAMING_OPTION(category)                                     \
    PrintOption(output, #category "NameCase", formatter::NameCase::Unchecked); \
    output << "# Values: [] (disabled) or [\"prefix\", ...]\n"         \
              "# [\"\"] explicitly allows names without a prefix.\n"     \
           << #category "NamePrefixes = []\n\n";
    FORMATTER_NAMING_CATEGORIES(PRINT_NAMING_OPTION)
#undef PRINT_NAMING_OPTION

    output << "# Other configuration options\n\n";
    const formatter::Configuration config;
#define PRINT_CONFIG_OPTION(type, name, initial) \
    PrintOption(output, #name, config.name);
    FORMATTER_CONFIGURATION_OPTIONS(PRINT_CONFIG_OPTION)
#undef PRINT_CONFIG_OPTION
}

} // namespace hit_fmt
