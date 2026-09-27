#include "config.hpp"

#include <array>
#include <charconv>
#include <cctype>
#include <fstream>
#include <stdexcept>
#include <type_traits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace formatter {

namespace {

std::string_view Trim(std::string_view text)
{
    while (!text.empty() &&
        std::isspace(static_cast<unsigned char>(text.front())))
    {
        text.remove_prefix(1);
    }
    while (!text.empty() &&
        std::isspace(static_cast<unsigned char>(text.back())))
    {
        text.remove_suffix(1);
    }
    return text;
}

std::string_view StripComment(std::string_view line)
{
    bool quoted = false;
    bool escaped = false;
    for (size_t index = 0; index < line.size(); ++index) {
        char character = line[index];
        if (escaped) {
            escaped = false;
        } else if (character == '\\' && quoted) {
            escaped = true;
        } else if (character == '"') {
            quoted = !quoted;
        } else if (character == '#' && !quoted) {
            return line.substr(0, index);
        }
    }
    return line;
}

[[noreturn]] void ThrowConfigError(
    const std::filesystem::path& path,
    size_t line,
    std::string_view message)
{
    throw std::runtime_error(
        path.string() + ":" + std::to_string(line) + ": "
        + std::string(message));
}

// A closing bracket inside a quoted item does not terminate an array.
// Quoted items themselves cannot span physical lines.
bool HasArrayEnd(std::string_view text,
    const std::filesystem::path& path, size_t line)
{
    bool quoted = false;
    bool escaped = false;
    for (char character : text) {
        if (escaped) {
            escaped = false;
        } else if (character == '\\' && quoted) {
            escaped = true;
        } else if (character == '"') {
            quoted = !quoted;
        } else if (character == ']' && !quoted) {
            return true;
        }
    }
    if (quoted) {
        ThrowConfigError(path, line, "unterminated quoted array item");
    }
    return false;
}

size_t ParsePositiveInteger(
    std::string_view value,
    const std::filesystem::path& path,
    size_t line)
{
    size_t result = 0;
    auto [end, error] =
        std::from_chars(value.data(), value.data() + value.size(), result);
    if (error != std::errc{} || end != value.data() + value.size() ||
        result == 0)
    {
        ThrowConfigError(path, line, "expected a positive integer");
    }
    return result;
}

template <typename T>
T ParseEnum(std::string_view value, const std::filesystem::path& path,
    size_t line)
{
    if (auto result = DeserializeConfigEnum<T>(value)) {
        return *result;
    }
    std::string error = "expected ";
    const auto& names = ConfigEnumNames<T>::Values;
    for (size_t index = 0; index < names.size(); ++index) {
        if (index != 0) {
            error += index + 1 == names.size() ? " or " : ", ";
        }
        error += names[index];
    }
    ThrowConfigError(path, line, error);
}

std::string ParseString(
    std::string_view value,
    const std::filesystem::path& path,
    size_t line)
{
    value = Trim(value);
    if (value.size() < 2 || value.front() != '"' || value.back() != '"') {
        ThrowConfigError(path, line, "expected a quoted string");
    }

    std::string result;
    result.reserve(value.size() - 2);
    for (size_t index = 1; index + 1 < value.size(); ++index) {
        char character = value[index];
        if (character != '\\') {
            result.push_back(character);
            continue;
        }
        if (++index + 1 >= value.size()) {
            ThrowConfigError(path, line, "unterminated string escape");
        }
        switch (value[index]) {
        case '\\':
        case '"':
            result.push_back(value[index]);
            break;
        case 'n':
            result.push_back('\n');
            break;
        case 't':
            result.push_back('\t');
            break;
        default:
            ThrowConfigError(path, line, "unsupported string escape");
        }
    }
    return result;
}

std::vector<std::string> ParseStringList(
    std::string_view value,
    const std::filesystem::path& path,
    size_t line)
{
    value = Trim(value);
    if (value.size() < 2 || value.front() != '[' || value.back() != ']') {
        ThrowConfigError(path, line, "expected an array of quoted strings");
    }
    value.remove_prefix(1);
    value.remove_suffix(1);

    std::vector<std::string> result;
    while (true) {
        value = Trim(value);
        if (value.empty()) {
            return result;
        }
        if (value.front() != '"') {
            ThrowConfigError(path, line, "expected a quoted array item");
        }
        bool escaped = false;
        size_t end = 1;
        for (; end < value.size(); ++end) {
            if (escaped) {
                escaped = false;
            } else if (value[end] == '\\') {
                escaped = true;
            } else if (value[end] == '"') {
                break;
            }
        }
        if (end == value.size()) {
            ThrowConfigError(path, line, "unterminated quoted array item");
        }
        result.push_back(ParseString(value.substr(0, end + 1), path, line));
        value.remove_prefix(end + 1);
        value = Trim(value);
        if (value.empty()) {
            return result;
        }
        if (value.front() != ',') {
            ThrowConfigError(path, line, "expected ',' between array items");
        }
        value.remove_prefix(1);
    }
}

bool ParseBoolean(std::string_view value,
    const std::filesystem::path& path, size_t line)
{
    if (value == "true") {
        return true;
    }
    if (value == "false") {
        return false;
    }
    ThrowConfigError(path, line, "expected true or false");
}

std::vector<std::string> ParseNamespaceExemptions(
    std::string_view value, const std::filesystem::path& path, size_t line)
{
    auto names = ParseStringList(value, path, line);
    for (const auto& name : names) {
        size_t start = 0;
        while (start < name.size()) {
            auto end = name.find("::", start);
            if (end == std::string::npos) end = name.size();
            auto part = std::string_view(name).substr(start, end - start);
            if (part.empty() ||
                !(std::isalpha(static_cast<unsigned char>(part.front())) ||
                    part.front() == '_') ||
                part.find_first_not_of(
                    "abcdefghijklmnopqrstuvwxyz"
                    "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") !=
                    std::string_view::npos)
            {
                ThrowConfigError(path, line, "expected qualified namespace names");
            }
            start = end == name.size() ? end : end + 2;
        }
        if (name.empty() || name.ends_with("::")) {
            ThrowConfigError(path, line, "expected qualified namespace names");
        }
    }
    return names;
}

std::vector<std::string> ParseCallNames(
    std::string_view value, const std::filesystem::path& path, size_t line)
{
    auto names = ParseStringList(value, path, line);
    for (const auto& name : names) {
        if (name.empty() ||
            !(std::isalpha(static_cast<unsigned char>(name.front())) ||
                name.front() == '_') ||
            name.find_first_not_of(
                "abcdefghijklmnopqrstuvwxyz"
                "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") !=
                std::string::npos)
        {
            ThrowConfigError(path, line, "expected call identifier names");
        }
    }
    return names;
}

std::vector<std::string> ParseNamePrefixes(
    std::string_view value, const std::filesystem::path& path, size_t line)
{
    auto prefixes = ParseStringList(value, path, line);
    if (prefixes.empty()) {
        ThrowConfigError(path, line, "expected at least one prefix");
    }
    for (const auto& prefix : prefixes) {
        if (prefix.find_first_not_of(
                "abcdefghijklmnopqrstuvwxyz"
                "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") != std::string::npos)
        {
            ThrowConfigError(path, line,
                "prefix must contain only identifier characters");
        }
    }
    return prefixes;
}

template <typename T>
void ParseField(T& field, std::string_view value,
    const std::filesystem::path& path, size_t line)
{
    if constexpr (std::is_enum_v<T>) {
        field = ParseEnum<T>(value, path, line);
    } else if constexpr (std::is_same_v<T, bool>) {
        field = ParseBoolean(value, path, line);
    } else if constexpr (std::is_same_v<T, size_t>) {
        field = ParsePositiveInteger(value, path, line);
    } else {
        static_assert(std::is_same_v<T, void>, "unsupported config field type");
    }
}

void ParseField(std::optional<std::vector<std::string>>& field,
    std::string_view value, const std::filesystem::path& path, size_t line)
{
    field = ParseNamePrefixes(value, path, line);
}

void ParseField(std::vector<std::string>& field, std::string_view value,
    const std::filesystem::path& path, size_t line)
{
    field = ParseNamespaceExemptions(value, path, line);
}

void ParseField(std::vector<std::regex>& field, std::string_view value,
    const std::filesystem::path& path, size_t line)
{
    field.clear();
    for (const auto& pattern : ParseStringList(value, path, line)) {
        if (pattern.empty()) {
            ThrowConfigError(path, line, "Exclude regex must not be empty");
        }
        try {
            field.emplace_back(pattern);
        } catch (const std::regex_error& error) {
            ThrowConfigError(path, line,
                "invalid Exclude regex '" + pattern + "': " + error.what());
        }
    }
}

void ValidateStyleLimits(const Style& style,
    const std::filesystem::path& path, size_t line)
{
    if (style.IndentWidth > 32) {
        ThrowConfigError(path, line, "IndentWidth must not exceed 32");
    }
    if (style.ColumnLimit > 10000) {
        ThrowConfigError(path, line, "ColumnLimit must not exceed 10000");
    }
    if (style.ShortNamespaceLines > 10000) {
        ThrowConfigError(path, line, "ShortNamespaceLines must not exceed 10000");
    }
    if (style.SeparatorLength != 0 &&
        (style.SeparatorLength < 8 || style.SeparatorLength > 10000))
    {
        ThrowConfigError(path, line,
            "SeparatorLength must be between 8 and 10000");
    }
    if (style.NamespaceCommentSpaces > 32) {
        ThrowConfigError(path, line, "NamespaceCommentSpaces must not exceed 32");
    }
    if (style.AccessModifierIndentWidth > 32 ||
        style.MemberIndentWidth > 32 || style.BlockBodyIndentWidth > 32 ||
        style.CaseLabelIndentWidth > 32 ||
        style.StatementAttributeIndentWidth > 32)
    {
        ThrowConfigError(path, line, "indent width must not exceed 32");
    }
    if (style.FunctionParameterNewLineIndent > 32) {
        ThrowConfigError(path, line,
            "FunctionParameterNewLineIndent must not exceed 32");
    }
    if (style.InheritanceListIndent > 32) {
        ThrowConfigError(path, line,
            "InheritanceListIndent must not exceed 32");
    }
    if (style.ConstructorInitializerListIndent > 32) {
        ThrowConfigError(path, line,
            "ConstructorInitializerListIndent must not exceed 32");
    }
}

template <auto Member>
void AssignStyle(Configuration& config, std::string_view value,
    const std::filesystem::path& path, size_t line)
{
    if constexpr (std::is_same_v<std::remove_cvref_t<
            decltype(config.FormattingStyle.*Member)>,
            std::vector<std::string>>)
    {
        if constexpr (Member == &Style::ChainedCallRootNames) {
            config.FormattingStyle.*Member =
                ParseNamespaceExemptions(value, path, line);
        } else {
            config.FormattingStyle.*Member = ParseCallNames(value, path, line);
        }
    } else {
        ParseField(config.FormattingStyle.*Member, value, path, line);
    }
}

template <auto Member>
void AssignConfig(Configuration& config, std::string_view value,
    const std::filesystem::path& path, size_t line)
{
    ParseField(config.*Member, value, path, line);
}

template <auto Member>
void AssignNamingCase(Configuration& config, std::string_view value,
    const std::filesystem::path& path, size_t line)
{
    ParseField((config.Naming.*Member).Case, value, path, line);
}

template <auto Member>
void AssignNamingPrefixes(Configuration& config, std::string_view value,
    const std::filesystem::path& path, size_t line)
{
    ParseField((config.Naming.*Member).Prefixes, value, path, line);
}

using OptionHandler = void (*)(Configuration&, std::string_view,
    const std::filesystem::path&, size_t);
struct OptionBinding
{
    std::string_view Name;
    OptionHandler Assign;
};

constexpr auto Options = std::to_array<OptionBinding>({
#define STYLE_BINDING(type, name, initial)                                      \
    {#name, &AssignStyle<&Style::name>},
    FORMATTER_STYLE_OPTIONS(STYLE_BINDING)
#undef STYLE_BINDING
#define NAMING_CATEGORY_BINDINGS(category)                                      \
    {#category "NameCase", &AssignNamingCase<&NamingStyle::category>},           \
    {#category "NamePrefixes", &AssignNamingPrefixes<&NamingStyle::category>},
    FORMATTER_NAMING_CATEGORIES(NAMING_CATEGORY_BINDINGS)
#undef NAMING_CATEGORY_BINDINGS
#define CONFIG_BINDING(type, name, initial)                                     \
    {#name, &AssignConfig<&Configuration::name>},
    FORMATTER_CONFIGURATION_OPTIONS(CONFIG_BINDING)
#undef CONFIG_BINDING
});

void ApplySetting(Configuration& config, std::string_view key,
    std::string_view value, const std::filesystem::path& path, size_t line)
{
    for (const auto& option : Options) {
        if (option.Name == key) {
            option.Assign(config, value, path, line);
            ValidateStyleLimits(config.FormattingStyle, path, line);
            return;
        }
    }
    ThrowConfigError(path, line, "unknown setting '" + std::string(key) + "'");
}

} // namespace

Configuration LoadConfigFile(const std::filesystem::path& path)
{
    std::ifstream input(path);
    if (!input.is_open()) {
        throw std::runtime_error("cannot open configuration file: " + path.string());
    }

    Configuration config;
    std::string lineText;
    size_t lineNumber = 0;
    while (std::getline(input, lineText)) {
        ++lineNumber;
        auto line = Trim(StripComment(lineText));
        if (line.empty()) {
            continue;
        }
        auto equals = line.find('=');
        if (equals == std::string_view::npos) {
            ThrowConfigError(path, lineNumber, "expected Key = Value");
        }
        auto key = Trim(line.substr(0, equals));
        auto value = Trim(line.substr(equals + 1));
        if (key.empty() || value.empty()) {
            ThrowConfigError(path, lineNumber, "expected Key = Value");
        }
        const size_t settingLine = lineNumber;
        std::string arrayValue;
        std::string arrayKey;
        if (value.front() == '[' && !HasArrayEnd(value, path, lineNumber)) {
            // key and value refer to lineText, which the next getline replaces.
            arrayKey = key;
            arrayValue = value;
            while (true) {
                if (!std::getline(input, lineText)) {
                    if (!input.eof()) {
                        throw std::runtime_error(
                            "failed to read configuration file: " + path.string());
                    }
                    ThrowConfigError(path, settingLine, "unterminated array");
                }
                ++lineNumber;
                auto itemLine = Trim(StripComment(lineText));
                arrayValue.push_back('\n');
                arrayValue.append(itemLine);
                if (HasArrayEnd(itemLine, path, lineNumber)) {
                    break;
                }
            }
            key = arrayKey;
            value = arrayValue;
        }
        ApplySetting(config, key, value, path, settingLine);
    }
    if (!input.eof()) {
        throw std::runtime_error("failed to read configuration file: " + path.string());
    }
    return config;
}

std::optional<std::filesystem::path> FindStyleFile(
    std::filesystem::path startDirectory,
    std::string_view fileName)
{
    std::error_code error;
    if (startDirectory.empty()) {
        startDirectory = std::filesystem::current_path(error);
    } else {
        startDirectory = std::filesystem::absolute(startDirectory, error);
    }
    if (error) {
        return std::nullopt;
    }

    while (!startDirectory.empty()) {
        auto candidate = startDirectory / fileName;
        if (std::filesystem::is_regular_file(candidate, error) && !error) {
            return candidate;
        }
        error.clear();
        auto parent = startDirectory.parent_path();
        if (parent == startDirectory) {
            break;
        }
        startDirectory = std::move(parent);
    }
    return std::nullopt;
}

} // namespace formatter
