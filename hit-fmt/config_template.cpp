#include "config_template.hpp"

#include <cstddef>
#include <optional>
#include <ostream>
#include <regex>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include <formatter/config.hpp>

namespace hit_fmt {

namespace {

std::string_view IntegerValues(std::string_view name)
{
    if (name == "IndentWidth") {
        return "1..32";
    }
    if (name == "ColumnLimit") {
        return "0 (unlimited) or 1..10000";
    }
    if (name == "RulerLength") {
        return "0 (preserve) or 3..10000";
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
    if constexpr (std::is_same_v<
        T,
        std::optional<formatter::TrailingCommentSpaceSetting>>)
    {
        output
            << "# Values: Preserve | 0..10000 "
                "(omitted: TrailingCommentSpacing)\n";
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
    if (name == "IndentPreprocessingDirectives") {
        output
            << "# Indent the whole directive "
                "(including # or %:) by nesting\n"
                "# depth using IndentWidth spaces; "
                "remove spaces after the marker.\n"
                "# Example with IndentWidth = 4: "
                "a nested #if becomes \"    #if\".\n"
                "# else/elif/endif align with their if.\n";
    } else if (name == "IndentedCallScopePairs") {
        output
            << "# Uncertain scopes retain existing line-start regions.\n"
                "# Inline endpoints do not prevent body expansion.\n";
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
        if (name == "MemberDefinitionBlankLines" ||
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
        !std::is_same_v<
            T,
            std::optional<formatter::TrailingCommentSpaceSetting>>)
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
        if (name == "NamingNamespaceExemptions" ||
            name == "ChainedCallRootNames" ||
            name == "AlwaysBreakChainRootNames")
        {
            output << "[] or [\"qualified::name\", ...]";
        } else if (name == "IncludeGroupPrefixes") {
            output << "[] or [\"directory/\", ...] (ordered literal prefixes)";
        } else if (name == "IndentedCallScopePairs" ||
            name == "IndentedChainScopePairs")
        {
            output << "[] or [\"Begin:End\", ...]";
        } else if (name == "StickyChainCallPairs") {
            output << "[] or [\"First:Second\", ...]";
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
            "# IncludeOrder = Sort enables the following include policies.\n"
            "# IncludeMatchingHeader = First selects quoted same-stem headers\n"
            "# (.h/.hh/.hpp/.hxx/.h++) in the input file's directory.\n"
            "# Pairing is disabled for header inputs or missing filenames.\n"
            "# IncludeGrouping = Preserve retains existing blank boundaries;\n"
            "# Regroup uses ordered literal IncludeGroupPrefixes; Directory\n"
            "# groups normalized directories and sorts filenames in each.\n"
            "# Regroup/Directory span blank lines and insert one empty line\n"
            "# between groups; both conflict with MaxEmptyLines = 0.\n"
            "# IncludeDirectoryOrder = NearestFirst uses parent + child steps\n"
            "# from the input directory; ties sort by directory spelling.\n"
            "# Alphabetical omits distance. Quoted groups precede angled "
            "ones.\n"
            "# IncludePathBase = SourceDirectory resolves source-relative "
            "paths.\n"
            "# ProjectRoot requires input and include paths with the same "
            "root.\n" "# Bare quoted names and quoted ./ or ../ paths stay "
            "source-relative.\n"
            "# Paths normalize lexically only; no file lookup or spelling "
            "edit.\n"
            "# Prefixes are validated even when inactive; earliest match "
            "wins.\n"
            "# Comments, macros, conditionals, protection and mixed line "
            "endings\n"
            "# stop blocks. Hunk edits require authority over the entire "
            "block.\n" "# LineEnding Preserve uses the first newline "
            "for generated endings\n"
            "# and retains copied gap bytes, not uniform "
            "per-edge preservation.\n"
            "# LF/CRLF spell only authorized whole whitespace newline atoms;\n"
            "# no final newline insertion, token/directive "
            "conversion or cleanup.\n" "# Protected regions remain byte-exact. "
            "Directive whitespace is opt-in.\n"
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
