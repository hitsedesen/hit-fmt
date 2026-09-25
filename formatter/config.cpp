#include "config.hpp"

#include <charconv>
#include <cctype>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
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

void ApplySetting(
    Configuration& config,
    std::string_view key,
    std::string_view value,
    const std::filesystem::path& path,
    size_t line)
{
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
