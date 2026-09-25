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

BracePlacement ParseBracePlacement(
    std::string_view value,
    const std::filesystem::path& path,
    size_t line)
{
    if (value == "Preserve") {
        return BracePlacement::Preserve;
    }
    if (value == "Attach") {
        return BracePlacement::Attach;
    }
    if (value == "NewLine") {
        return BracePlacement::NewLine;
    }
    ThrowConfigError(path, line, "expected Preserve, Attach or NewLine");
}

SameLineBraceSpacing ParseSameLineBraceSpacing(
    std::string_view value,
    const std::filesystem::path& path,
    size_t line)
{
    if (value == "Preserve") {
        return SameLineBraceSpacing::Preserve;
    }
    if (value == "SingleSpace") {
        return SameLineBraceSpacing::SingleSpace;
    }
    ThrowConfigError(path, line, "expected Preserve or SingleSpace");
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

std::string_view ParseOption(std::string_view value,
    const std::filesystem::path& path, size_t line)
{
    if (value.empty() || value.find_first_not_of(
            "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ") !=
            std::string_view::npos)
    {
        ThrowConfigError(path, line, "expected an option name");
    }
    return value;
}

NameCase ParseNameCase(std::string_view value,
    const std::filesystem::path& path, size_t line)
{
    if (value == "camelCase") {
        return NameCase::CamelCase;
    }
    if (value == "PascalCase") {
        return NameCase::PascalCase;
    }
    if (value == "snake_case") {
        return NameCase::SnakeCase;
    }
    if (value == "SCREAMING_SNAKE_CASE") {
        return NameCase::ScreamingSnakeCase;
    }
    ThrowConfigError(path, line,
        "expected camelCase, PascalCase, snake_case or SCREAMING_SNAKE_CASE");
}

void ApplySetting(
    Configuration& config,
    std::string_view key,
    std::string_view value,
    const std::filesystem::path& path,
    size_t line)
{
    if (key == "NamingNamespaceExemptions") {
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
                    ThrowConfigError(path, line,
                        "expected qualified namespace names");
                }
                start = end == name.size() ? end : end + 2;
            }
            if (name.empty() || name.ends_with("::")) {
                ThrowConfigError(path, line,
                    "expected qualified namespace names");
            }
        }
        config.Naming.ExemptNamespaces = std::move(names);
        return;
    }
    for (auto [category, rule] : {
        std::pair<std::string_view, NameRule*>{"Class", &config.Naming.Class},
        {"Enum", &config.Naming.Enum}, {"Union", &config.Naming.Union},
        {"Struct", &config.Naming.Struct},
        {"Function", &config.Naming.Function},
        {"Concept", &config.Naming.Concept},
        {"Variable", &config.Naming.Variable},
        {"Namespace", &config.Naming.Namespace},
    }) {
        if (key == std::string(category) + "NameCase") {
            rule->Case = ParseNameCase(value, path, line);
            return;
        }
        if (key == std::string(category) + "NamePrefixes") {
            auto prefixes = ParseStringList(value, path, line);
            if (prefixes.empty()) {
                ThrowConfigError(path, line, "expected at least one prefix");
            }
            for (const auto& prefix : prefixes) {
                if (prefix.find_first_not_of(
                        "abcdefghijklmnopqrstuvwxyz"
                        "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_")
                    != std::string::npos)
                {
                    ThrowConfigError(path, line, "prefix must contain only identifier characters");
                }
            }
            rule->Prefixes = std::move(prefixes);
            return;
        }
    }
    if (key == "IndentWidth") {
        config.FormattingStyle.IndentWidth =
            ParsePositiveInteger(value, path, line);
        if (config.FormattingStyle.IndentWidth > 32) {
            ThrowConfigError(path, line, "IndentWidth must not exceed 32");
        }
    } else if (key == "ColumnLimit") {
        config.FormattingStyle.ColumnLimit =
            ParsePositiveInteger(value, path, line);
        if (config.FormattingStyle.ColumnLimit > 10000) {
            ThrowConfigError(path, line, "ColumnLimit must not exceed 10000");
        }
    } else if (key == "ClassBracePlacement") {
        config.FormattingStyle.ClassBracePlacement =
            ParseBracePlacement(value, path, line);
    } else if (key == "StructBracePlacement") {
        config.FormattingStyle.StructBracePlacement =
            ParseBracePlacement(value, path, line);
    } else if (key == "UnionBracePlacement") {
        config.FormattingStyle.UnionBracePlacement =
            ParseBracePlacement(value, path, line);
    } else if (key == "FunctionBracePlacement") {
        config.FormattingStyle.FunctionBracePlacement =
            ParseBracePlacement(value, path, line);
    } else if (key == "LambdaBracePlacement") {
        config.FormattingStyle.LambdaBracePlacement =
            ParseBracePlacement(value, path, line);
    } else if (key == "NamespaceBracePlacement") {
        config.FormattingStyle.NamespaceBracePlacement =
            ParseBracePlacement(value, path, line);
    } else if (key == "NamespaceComments") {
        value = ParseOption(value, path, line);
        if (value == "Preserve") {
            config.FormattingStyle.NamespaceComments = NamespaceComments::Preserve;
        } else if (value == "Remove") {
            config.FormattingStyle.NamespaceComments = NamespaceComments::Remove;
        } else if (value == "Required") {
            config.FormattingStyle.NamespaceComments = NamespaceComments::Required;
        } else {
            ThrowConfigError(path, line, "invalid NamespaceComments");
        }
    } else if (key == "FunctionParameterSplit") {
        value = ParseOption(value, path, line);
        if (value == "Preserve") {
            config.FormattingStyle.FunctionParameterSplit =
                FunctionParameterSplit::Preserve;
        } else if (value == "Pack") {
            config.FormattingStyle.FunctionParameterSplit =
                FunctionParameterSplit::Pack;
        } else if (value == "OnePerLine") {
            config.FormattingStyle.FunctionParameterSplit =
                FunctionParameterSplit::OnePerLine;
        } else {
            ThrowConfigError(path, line, "invalid FunctionParameterSplit");
        }
    } else if (key == "FirstFunctionParameterPosition") {
        value = ParseOption(value, path, line);
        if (value == "NameLine") {
            config.FormattingStyle.FirstFunctionParameterPosition =
                FirstFunctionParameterPosition::NameLine;
        } else if (value == "NewLine") {
            config.FormattingStyle.FirstFunctionParameterPosition =
                FirstFunctionParameterPosition::NewLine;
        } else {
            ThrowConfigError(path, line,
                "invalid FirstFunctionParameterPosition");
        }
    } else if (key == "EnforceFirstFunctionParameterPosition") {
        config.FormattingStyle.EnforceFirstFunctionParameterPosition =
            ParseBoolean(value, path, line);
    } else if (key == "FunctionParameterIndent") {
        value = ParseOption(value, path, line);
        if (value == "IndentWidth") {
            config.FormattingStyle.FunctionParameterIndent =
                FunctionParameterIndent::IndentWidth;
        } else if (value == "Continuation") {
            config.FormattingStyle.FunctionParameterIndent =
                FunctionParameterIndent::Continuation;
        } else {
            ThrowConfigError(path, line, "invalid FunctionParameterIndent");
        }
    } else if (key == "MergeFunctionParameters") {
        config.FormattingStyle.MergeFunctionParameters =
            ParseBoolean(value, path, line);
    } else if (key == "ClassBraceSpacing") {
        config.FormattingStyle.ClassBraceSpacing =
            ParseSameLineBraceSpacing(value, path, line);
    } else if (key == "LambdaBraceSpacing") {
        config.FormattingStyle.LambdaBraceSpacing =
            ParseSameLineBraceSpacing(value, path, line);
    } else if (key == "LambdaParameterSpacing") {
        value = ParseOption(value, path, line);
        if (value == "Preserve") {
            config.FormattingStyle.LambdaParameterSpacing =
                LambdaParameterSpacing::Preserve;
        } else if (value == "Space") {
            config.FormattingStyle.LambdaParameterSpacing =
                LambdaParameterSpacing::Space;
        } else if (value == "NoSpace") {
            config.FormattingStyle.LambdaParameterSpacing =
                LambdaParameterSpacing::NoSpace;
        } else {
            ThrowConfigError(path, line,
                "expected Preserve, Space or NoSpace");
        }
    } else if (key == "StructBraceSpacing") {
        config.FormattingStyle.StructBraceSpacing =
            ParseSameLineBraceSpacing(value, path, line);
    } else if (key == "EnumBraceSpacing") {
        config.FormattingStyle.EnumBraceSpacing =
            ParseSameLineBraceSpacing(value, path, line);
    } else if (key == "UnionBraceSpacing") {
        config.FormattingStyle.UnionBraceSpacing =
            ParseSameLineBraceSpacing(value, path, line);
    } else if (key == "FunctionBraceSpacing") {
        config.FormattingStyle.FunctionBraceSpacing =
            ParseSameLineBraceSpacing(value, path, line);
    } else if (key == "NamespaceBraceSpacing") {
        config.FormattingStyle.NamespaceBraceSpacing =
            ParseSameLineBraceSpacing(value, path, line);
    } else if (key == "FunctionDeclarationParameterNames") {
        value = ParseOption(value, path, line);
        if (value == "Unchecked") {
            config.DeclarationParameterNames =
                FunctionDeclarationParameterNames::Unchecked;
        } else if (value == "Required") {
            config.DeclarationParameterNames =
                FunctionDeclarationParameterNames::Required;
        } else {
            ThrowConfigError(path, line,
                "invalid FunctionDeclarationParameterNames");
        }
    } else if (key == "FunctionDefinitionParameterNames") {
        value = ParseOption(value, path, line);
        if (value == "Unchecked") {
            config.DefinitionParameterNames =
                FunctionDefinitionParameterNames::Unchecked;
        } else if (value == "NameRequired") {
            config.DefinitionParameterNames =
                FunctionDefinitionParameterNames::NameRequired;
        } else if (value == "NameOrCommentedNameRequired") {
            config.DefinitionParameterNames =
                FunctionDefinitionParameterNames::NameOrCommentedNameRequired;
        } else {
            ThrowConfigError(path, line,
                "invalid FunctionDefinitionParameterNames");
        }
    } else if (key == "Exclude") {
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
    } else {
        ThrowConfigError(path, line, "unknown setting '" + std::string(key) + "'");
    }
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
        ApplySetting(config, key, value, path, lineNumber);
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
