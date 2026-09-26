#include "config.hpp"

#include <charconv>
#include <cctype>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace formatter {

// A single registry supplies the accepted option names and their handlers.
// Ordinary option names are the same as the Style members they configure.
#define STYLE_ENUM_OPTIONS(X) \
    X(IncludeOrder, IncludeOrder) \
    X(ClassBracePlacement, BracePlacement) \
    X(StructBracePlacement, BracePlacement) \
    X(UnionBracePlacement, BracePlacement) \
    X(FunctionBracePlacement, BracePlacement) \
    X(LambdaBracePlacement, BracePlacement) \
    X(NamespaceBracePlacement, BracePlacement) \
    X(NamespaceComments, NamespaceComments) \
    X(FunctionParameterSplit, FunctionParameterSplit) \
    X(FirstFunctionParameterPosition, FirstFunctionParameterPosition) \
    X(FunctionParameterIndent, FunctionParameterIndent) \
    X(CallArgumentSplit, CallArgumentSplit) \
    X(FirstCallArgumentPosition, FirstCallArgumentPosition) \
    X(CallArgumentIndent, CallArgumentIndent) \
    X(ExpressionLayoutCondition, ExpressionLayoutCondition) \
    X(ExpressionLayoutAlgorithm, ExpressionLayoutAlgorithm) \
    X(BinaryOperatorPlacement, BinaryOperatorPlacement) \
    X(TernaryOperatorPlacement, TernaryOperatorPlacement) \
    X(TernarySplit, TernarySplit) \
    X(ControlBlockBracePlacement, ControlBlockBracePlacement) \
    X(ControlBlockKeywordSpacing, ControlBlockKeywordSpacing) \
    X(ClassBraceSpacing, SameLineBraceSpacing) \
    X(LambdaBraceSpacing, SameLineBraceSpacing) \
    X(FunctionCallSpaceBeforeParameters, FunctionCallSpaceBeforeParameters) \
    X(SpaceInsideCallArguments, SpaceInsideCallArguments) \
    X(SpaceInsideLambdaParameters, SpaceInsideLambdaParameters) \
    X(SpaceInsideFunctionParameters, SpaceInsideFunctionParameters) \
    X(FunctionSpaceBeforeParameters, FunctionSpaceBeforeParameters) \
    X(IndentAccessModifiers, BodyIndentation) \
    X(IndentMembers, BodyIndentation) \
    X(InheritanceList, InheritanceListMode) \
    X(InheritanceListColonPlacement, InheritanceListColonPlacement) \
    X(InheritanceListColonSpaceBefore, InheritanceListSpacing) \
    X(InheritanceListColonSpaceAfter, InheritanceListSpacing) \
    X(InheritanceListCommaPlacement, InheritanceListCommaPlacement) \
    X(InheritanceListCommaSpaceBefore, InheritanceListSpacing) \
    X(InheritanceListCommaSpaceAfter, InheritanceListSpacing) \
    X(InheritanceListStart, InheritanceListStart) \
    X(InheritanceListSplit, InheritanceListSplit) \
    X(SpaceAroundCaptureList, LambdaCaptureSpacing) \
    X(SpaceAroundCaptureAssignment, LambdaCaptureSpacing) \
    X(LambdaSpaceBeforeParameters, LambdaSpaceBeforeParameters) \
    X(StructBraceSpacing, SameLineBraceSpacing) \
    X(EnumBraceSpacing, SameLineBraceSpacing) \
    X(UnionBraceSpacing, SameLineBraceSpacing) \
    X(FunctionBraceSpacing, SameLineBraceSpacing) \
    X(NamespaceBraceSpacing, SameLineBraceSpacing)

#define STYLE_BOOLEAN_OPTIONS(X)                                                \
    X(EnforceFirstFunctionParameterPosition)                                   \
    X(EnforceFirstCallArgumentPosition)                                        \
    X(MergeFunctionParameters)

#define STYLE_INTEGER_OPTIONS(X)                                                \
    X(IndentWidth, 32) X(ColumnLimit, 10000) X(InheritanceListIndent, 32)

#define PARAMETER_NAME_OPTIONS(X)                                               \
    X(FunctionDeclarationParameterNames, DeclarationParameterNames)           \
    X(FunctionDefinitionParameterNames, DefinitionParameterNames)

#define NAMING_CATEGORIES(X)                                                    \
    X(Class) X(Enum) X(Union) X(Struct) X(Function) X(Concept) X(Variable)    \
    X(Namespace)

#define NAMING_OPTION_VALUE(category) category##NameCase, category##NamePrefixes,
#define OPTION_VALUE(name, ...) name,
enum class ConfigOption
{
    STYLE_ENUM_OPTIONS(OPTION_VALUE)
    STYLE_BOOLEAN_OPTIONS(OPTION_VALUE)
    STYLE_INTEGER_OPTIONS(OPTION_VALUE)
    PARAMETER_NAME_OPTIONS(OPTION_VALUE)
    NAMING_CATEGORIES(NAMING_OPTION_VALUE)
    NamingNamespaceExemptions,
    Exclude,
};
#undef OPTION_VALUE
#undef NAMING_OPTION_VALUE

#define NAMING_OPTION_NAME(category) #category "NameCase", #category "NamePrefixes",
#define OPTION_NAME(name, ...) #name,
template <> struct ConfigEnumNames<ConfigOption>
{
    static constexpr auto Values = std::to_array<std::string_view>({
        STYLE_ENUM_OPTIONS(OPTION_NAME)
        STYLE_BOOLEAN_OPTIONS(OPTION_NAME)
        STYLE_INTEGER_OPTIONS(OPTION_NAME)
        PARAMETER_NAME_OPTIONS(OPTION_NAME)
        NAMING_CATEGORIES(NAMING_OPTION_NAME)
        "NamingNamespaceExemptions",
        "Exclude",
    });
};
#undef OPTION_NAME
#undef NAMING_OPTION_NAME

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

void ApplySetting(
    Configuration& config,
    std::string_view key,
    std::string_view value,
    const std::filesystem::path& path,
    size_t line)
{
    auto option = DeserializeConfigEnum<ConfigOption>(key);
    if (!option) {
        ThrowConfigError(path, line, "unknown setting '" + std::string(key) + "'");
    }

    switch (*option) {
#define APPLY_ENUM_OPTION(name, type)                                           \
    case ConfigOption::name:                                                    \
        config.FormattingStyle.name = ParseEnum<type>(value, path, line);      \
        return;
        STYLE_ENUM_OPTIONS(APPLY_ENUM_OPTION)
#undef APPLY_ENUM_OPTION

#define APPLY_BOOLEAN_OPTION(name)                                              \
    case ConfigOption::name:                                                    \
        config.FormattingStyle.name = ParseBoolean(value, path, line);         \
        return;
        STYLE_BOOLEAN_OPTIONS(APPLY_BOOLEAN_OPTION)
#undef APPLY_BOOLEAN_OPTION

#define APPLY_INTEGER_OPTION(name, maximum)                                     \
    case ConfigOption::name:                                                    \
        config.FormattingStyle.name = ParsePositiveInteger(value, path, line); \
        if (config.FormattingStyle.name > maximum) {                           \
            ThrowConfigError(path, line, #name " must not exceed " #maximum);  \
        }                                                                       \
        return;
        STYLE_INTEGER_OPTIONS(APPLY_INTEGER_OPTION)
#undef APPLY_INTEGER_OPTION

#define APPLY_PARAMETER_OPTION(name, member)                                    \
    case ConfigOption::name:                                                    \
        config.member = ParseEnum<name>(value, path, line);                     \
        return;
        PARAMETER_NAME_OPTIONS(APPLY_PARAMETER_OPTION)
#undef APPLY_PARAMETER_OPTION

#define APPLY_NAMING_CATEGORY(category)                                         \
    case ConfigOption::category##NameCase:                                      \
        config.Naming.category.Case = ParseEnum<NameCase>(value, path, line);   \
        return;                                                                 \
    case ConfigOption::category##NamePrefixes:                                  \
        config.Naming.category.Prefixes = ParseNamePrefixes(value, path, line); \
        return;
        NAMING_CATEGORIES(APPLY_NAMING_CATEGORY)
#undef APPLY_NAMING_CATEGORY

    case ConfigOption::NamingNamespaceExemptions:
        config.Naming.ExemptNamespaces = ParseNamespaceExemptions(
            value, path, line);
        return;

    case ConfigOption::Exclude:
        config.Exclusions.clear();
        for (const auto& pattern : ParseStringList(value, path, line)) {
            if (pattern.empty()) {
                ThrowConfigError(path, line, "Exclude regex must not be empty");
            }
            try {
                config.Exclusions.emplace_back(pattern);
            } catch (const std::regex_error& error) {
                ThrowConfigError(path, line,
                    "invalid Exclude regex '" + pattern + "': " + error.what());
            }
        }
        return;
    }
}

#undef STYLE_ENUM_OPTIONS
#undef STYLE_BOOLEAN_OPTIONS
#undef STYLE_INTEGER_OPTIONS
#undef PARAMETER_NAME_OPTIONS
#undef NAMING_CATEGORIES

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
