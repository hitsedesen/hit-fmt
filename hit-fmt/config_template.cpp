#include "config_template.hpp"

#include <formatter/config.hpp>

#include <cstddef>
#include <optional>
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
    if (name == "MaxEmptyLines") {
        return "0..10000";
    }
    if (name == "ShortNamespaceLines") {
        return "0 (no exemption) or 1..10000";
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

template <typename T> struct OptionalValue
{
    static constexpr bool IsOptional = false;
};

template <typename T> struct OptionalValue<std::optional<T>>
{
    static constexpr bool IsOptional = true;
    using Type = T;
};

template <typename T>
void PrintOption(std::ostream& output, std::string_view name, const T& value)
{
    if constexpr (std::is_same_v<T,
        std::optional<formatter::TrailingCommentSpaceSetting>>)
    {
        output
            << "# Values: Preserve | 0..10000 (omitted: TrailingCommentSpacing)\n";
        if (!value) {
            output << "# " << name << " = Preserve\n\n";
        } else {
            output << name << " = ";
            if (value->Count) {
                output << *value->Count;
            } else {
                output << "Preserve";
            }
            output << "\n\n";
        }
        return;
    }
    output << "# Values: ";
    if constexpr (std::is_enum_v<T>) {
        bool first = true;
        for (auto spelling : formatter::ConfigEnumNames<T>::Values) {
            if (!first) {
                output << " | ";
            }
            output << spelling;
            first = false;
        }
        output << "\n" << name << " = "
            << formatter::SerializeConfigEnum(value);
    } else if constexpr (std::is_same_v<T, bool>) {
        output << "false | true\n" << name << " = "
            << (value ? "true" : "false");
    } else if constexpr (std::is_same_v<T, formatter::BlankLineSetting>) {
        if (
            name == "MemberDefinitionBlankLines" ||
            name == "MemberDeclarationBlankLines")
        {
            output << "Inherit | ";
        }
        output << "Preserve | 0..10000\n" << name << " = ";
        if (value.Mode == formatter::BlankLineMode::Exact) {
            output << value.EmptyLines;
        } else {
            output
                << (value.Mode == formatter::BlankLineMode::Inherit
                        ? "Inherit"
                        : "Preserve");
        }
    } else if constexpr (OptionalValue<T>::IsOptional &&
        !std::is_same_v<T, std::optional<formatter::TrailingCommentSpaceSetting>>)
    {
        using Item = typename OptionalValue<T>::Type;
        output << (name == "MaxEmptyLines" ? "Preserve | " : "Inherit | ");
        if constexpr (std::is_enum_v<Item>) {
            bool first = true;
            for (auto spelling : formatter::ConfigEnumNames<Item>::Values) {
                if (!first) {
                    output << " | ";
                }
                output << spelling;
                first = false;
            }
            output << '\n' << name << " = ";
            output
                << (value ? formatter::SerializeConfigEnum(*value) : "Inherit");
        } else {
            output << IntegerValues(name) << '\n' << name << " = ";
            if (value) {
                output << *value;
            } else {
                output << (name == "MaxEmptyLines" ? "Preserve" : "Inherit");
            }
        }
    } else if constexpr (std::is_same_v<T, std::size_t>) {
        output << IntegerValues(name) << '\n' << name << " = " << value;
    } else if constexpr (std::is_same_v<T, std::vector<std::string>>) {
        if (
            name == "NamingNamespaceExemptions" ||
            name == "ChainedCallRootNames")
        {
            output << "[] or [\"qualified::name\", ...]";
        } else if (name == "IncludeGroupPrefixes") {
            output << "[] or [\"directory/\", ...] (ordered literal prefixes)";
        } else if (name == "IndentedCallScopePairs") {
            output << "[] or [\"Begin:End\", ...]";
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
    output
        << "# Preserve source break topology by "
            "default. Set Layout = Canonical\n"
            "# to normalize groups. Category Inherit values resolve once.\n"
            "# Generated* applies to properties rebuilt "
            "by their owning canonical\n"
            "# combinator (also inserted gaps), not unrelated nested edits.\n"
            "# Break = Always requires a legal owned "
            "break; Preserve/protection win.\n"
            "# EmptyBodyShape is independent of nonempty BodyShape.\n"
            "# Blank counts measure empty lines; zero "
            "retains boundary topology.\n" "# Exact counts above an explicit "
            "MaxEmptyLines cap are invalid.\n"
            "# Public preparation rejects template breaks, Operand alignment,\n"
            "# matching-header selection, regrouping and constructor overrides.\n"
            "# Inactive include prefixes validate but do not activate regrouping.\n"
            "# Regroup conflicts with MaxEmptyLines = 0.\n"
            "# LineEnding Preserve uses the first newline for generated endings\n"
            "# and retains copied gap bytes, not uniform per-edge preservation.\n"
            "# LF/CRLF spell only authorized whole whitespace newline atoms;\n"
            "# no final newline insertion, token/directive conversion or cleanup.\n"
            "# Directives and protected regions remain byte-exact.\n"
            "# Option guidance: --help-config\n\n" "# Formatting options\n\n";
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
