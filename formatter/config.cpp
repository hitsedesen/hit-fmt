#include "config.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace formatter {
namespace {

std::string_view Trim(std::string_view text)
{
    while (!text.empty()
           && std::isspace(static_cast<unsigned char>(text.front())))
    {
        text.remove_prefix(1);
    }
    while (!text.empty()
           && std::isspace(static_cast<unsigned char>(text.back())))
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
    if (error != std::errc{} || end != value.data() + value.size()
        || result == 0)
    {
        ThrowConfigError(path, line, "expected a positive integer");
    }
    return result;
}

bool ParseBoolean(
    std::string_view value,
    const std::filesystem::path& path,
    size_t line)
{
    if (value == "true") {
        return true;
    }
    if (value == "false") {
        return false;
    }
    ThrowConfigError(path, line, "expected true or false");
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

std::string NormalizeEnum(std::string value)
{
    std::string result;
    result.reserve(value.size());
    for (char character : value) {
        if (character == '_' || character == '-') {
            continue;
        }
        result.push_back(static_cast<char>(
            std::tolower(static_cast<unsigned char>(character))));
    }
    return result;
}

BracketAlignment ParseOpenBracketAlignment(
    std::string_view value,
    const std::filesystem::path& path,
    size_t line)
{
    auto normalized = NormalizeEnum(ParseString(value, path, line));
    if (normalized == "preserve") {
        return BracketAlignment::Preserve;
    }
    if (normalized == "continuation") {
        return BracketAlignment::Continuation;
    }
    if (normalized == "alignwithopening" || normalized == "opening") {
        return BracketAlignment::AlignWithOpening;
    }
    ThrowConfigError(
        path,
        line,
        "OpenBracketAlignment must be Preserve, Continuation, or AlignWithOpening");
}

BracePlacement ParseBracePlacement(
    std::string_view value,
    const std::filesystem::path& path,
    size_t line)
{
    auto normalized = NormalizeEnum(ParseString(value, path, line));
    if (normalized == "preserve") {
        return BracePlacement::Preserve;
    }
    if (normalized == "attach") {
        return BracePlacement::Attach;
    }
    if (normalized == "newline" || normalized == "break") {
        return BracePlacement::NewLine;
    }
    if (normalized == "attachunlessmultiline") {
        return BracePlacement::AttachUnlessMultiline;
    }
    ThrowConfigError(
        path,
        line,
        "brace placement must be Preserve, Attach, NewLine, or AttachUnlessMultiline");
}

ParenthesisSpacing ParseParenthesisSpacing(
    std::string_view value,
    const std::filesystem::path& path,
    size_t line)
{
    auto normalized = NormalizeEnum(ParseString(value, path, line));
    if (normalized == "never") {
        return ParenthesisSpacing::Never;
    }
    if (normalized == "controlstatements" || normalized == "control") {
        return ParenthesisSpacing::ControlStatements;
    }
    if (normalized == "always") {
        return ParenthesisSpacing::Always;
    }
    ThrowConfigError(
        path, line, "SpaceBeforeParentheses must be Never, ControlStatements, or Always");
}

DeclaratorAlignment ParseDeclaratorAlignment(
    std::string_view value,
    const std::filesystem::path& path,
    size_t line)
{
    auto normalized = NormalizeEnum(ParseString(value, path, line));
    if (normalized == "preserve") {
        return DeclaratorAlignment::Preserve;
    }
    if (normalized == "left") {
        return DeclaratorAlignment::Left;
    }
    if (normalized == "right") {
        return DeclaratorAlignment::Right;
    }
    if (normalized == "middle") {
        return DeclaratorAlignment::Middle;
    }
    ThrowConfigError(
        path, line, "alignment must be Preserve, Left, Right, or Middle");
}

CaseLabelIndentation ParseCaseLabelIndentation(
    std::string_view value,
    const std::filesystem::path& path,
    size_t line)
{
    auto normalized = NormalizeEnum(ParseString(value, path, line));
    if (normalized == "preserve") {
        return CaseLabelIndentation::Preserve;
    }
    if (normalized == "outdent") {
        return CaseLabelIndentation::Outdent;
    }
    if (normalized == "indent") {
        return CaseLabelIndentation::Indent;
    }
    ThrowConfigError(
        path, line, "IndentCaseLabels must be Preserve, Outdent, or Indent");
}

BracedInitializerSpacing ParseBracedInitializerSpacing(
    std::string_view value,
    const std::filesystem::path& path,
    size_t line)
{
    auto normalized = NormalizeEnum(ParseString(value, path, line));
    if (normalized == "preserve") {
        return BracedInitializerSpacing::Preserve;
    }
    if (normalized == "never") {
        return BracedInitializerSpacing::Never;
    }
    if (normalized == "always") {
        return BracedInitializerSpacing::Always;
    }
    ThrowConfigError(
        path,
        line,
        "SpaceInsideBracedInitializers must be Preserve, Never, or Always");
}

ClosingBracketAlignment ParseCloseBracketAlignment(
    std::string_view value,
    const std::filesystem::path& path,
    size_t line)
{
    auto normalized = NormalizeEnum(ParseString(value, path, line));
    if (normalized == "preserve") {
        return ClosingBracketAlignment::Preserve;
    }
    if (normalized == "alignwithopening" || normalized == "opening") {
        return ClosingBracketAlignment::AlignWithOpening;
    }
    ThrowConfigError(
        path,
        line,
        "CloseBracketAlignment must be Preserve or AlignWithOpening");
}

void ApplySetting(
    Style& style,
    std::string_view key,
    std::string_view value,
    const std::filesystem::path& path,
    size_t line)
{
    if (key == "IndentWidth") {
        style.IndentWidth = ParsePositiveInteger(value, path, line);
        if (style.IndentWidth > 32) {
            ThrowConfigError(path, line, "IndentWidth must not exceed 32");
        }
    } else if (key == "ColumnLimit") {
        style.ColumnLimit = ParsePositiveInteger(value, path, line);
        if (style.ColumnLimit > 10000) {
            ThrowConfigError(path, line, "ColumnLimit must not exceed 10000");
        }
    } else if (key == "Regroup") {
        style.Regroup = ParseBoolean(value, path, line);
    } else if (key == "InsertBraces") {
        style.InsertBraces = ParseBoolean(value, path, line);
    } else if (key == "BreakInlineRecordDeclarations") {
        style.BreakInlineRecordDeclarations =
            ParseBoolean(value, path, line);
    } else if (key == "SpaceBeforeTemplateParameterPack") {
        style.SpaceBeforeTemplateParameterPack =
            ParseBoolean(value, path, line);
    } else if (key == "FunctionBracePlacement") {
        style.FunctionBracePlacement =
            ParseBracePlacement(value, path, line);
    } else if (key == "RecordBracePlacement") {
        style.RecordBracePlacement = ParseBracePlacement(value, path, line);
    } else if (key == "ControlBracePlacement") {
        style.ControlBracePlacement = ParseBracePlacement(value, path, line);
    } else if (key == "LambdaBracePlacement") {
        style.LambdaBracePlacement = ParseBracePlacement(value, path, line);
    } else if (key == "NamespaceBracePlacement") {
        style.NamespaceBracePlacement = ParseBracePlacement(value, path, line);
    } else if (key == "SpaceBeforeParentheses") {
        style.SpaceBeforeParentheses =
            ParseParenthesisSpacing(value, path, line);
    } else if (key == "PointerAlignment") {
        style.PointerAlignment = ParseDeclaratorAlignment(value, path, line);
    } else if (key == "ReferenceAlignment") {
        style.ReferenceAlignment =
            ParseDeclaratorAlignment(value, path, line);
    } else if (key == "IndentCaseLabels") {
        style.IndentCaseLabels =
            ParseCaseLabelIndentation(value, path, line);
    } else if (key == "SpaceInsideParentheses") {
        style.SpaceInsideParentheses = ParseBoolean(value, path, line);
    } else if (key == "SpaceInsideSquareBrackets") {
        style.SpaceInsideSquareBrackets = ParseBoolean(value, path, line);
    } else if (key == "SpaceInsideBracedInitializers") {
        style.SpaceInsideBracedInitializers =
            ParseBracedInitializerSpacing(value, path, line);
    } else if (key == "IndentNamespaces") {
        style.IndentNamespaces = ParseBoolean(value, path, line);
    } else if (key == "KeepEmptyBlocksOnOneLine") {
        style.KeepEmptyBlocksOnOneLine = ParseBoolean(value, path, line);
    } else if (key == "AllowShortLambdasOnOneLine") {
        style.AllowShortLambdasOnOneLine = ParseBoolean(value, path, line);
    } else if (key == "OpenBracketAlignment") {
        style.OpenBracketAlignment =
            ParseOpenBracketAlignment(value, path, line);
    } else if (key == "CloseBracketAlignment") {
        style.CloseBracketAlignment =
            ParseCloseBracketAlignment(value, path, line);
    } else if (key == "ForbidBreakAfter") {
        style.ForbidBreakAfter = ParseStringList(value, path, line);
    } else if (key == "WhitespaceSensitiveMacros") {
        style.WhitespaceSensitiveMacros = ParseStringList(value, path, line);
    } else {
        ThrowConfigError(path, line, "unknown setting '" + std::string(key) + "'");
    }
}

} // namespace

Style LoadStyleFile(const std::filesystem::path& path)
{
    std::ifstream input(path);
    if (!input.is_open()) {
        throw std::runtime_error("cannot open configuration file: " + path.string());
    }

    Style style;
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
        ApplySetting(style, key, value, path, lineNumber);
    }
    if (!input.eof()) {
        throw std::runtime_error("failed to read configuration file: " + path.string());
    }
    return style;
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
