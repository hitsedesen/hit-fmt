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

template <typename T>
T ParseEnum(std::string_view value, const std::filesystem::path& path,
    size_t line, std::string_view error)
{
    if (auto result = DeserializeConfigEnum<T>(value)) {
        return *result;
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
            rule->Case = ParseEnum<NameCase>(value, path, line,
                "expected camelCase, PascalCase, snake_case or "
                "SCREAMING_SNAKE_CASE");
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
    if (key == "IncludeOrder") {
        config.FormattingStyle.IncludeOrder = ParseEnum<IncludeOrder>(
            value, path, line, "expected Preserve or Sort");
        return;
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
            ParseEnum<BracePlacement>(value, path, line,
                "expected Preserve, Attach or NewLine");
    } else if (key == "StructBracePlacement") {
        config.FormattingStyle.StructBracePlacement =
            ParseEnum<BracePlacement>(value, path, line,
                "expected Preserve, Attach or NewLine");
    } else if (key == "UnionBracePlacement") {
        config.FormattingStyle.UnionBracePlacement =
            ParseEnum<BracePlacement>(value, path, line,
                "expected Preserve, Attach or NewLine");
    } else if (key == "FunctionBracePlacement") {
        config.FormattingStyle.FunctionBracePlacement =
            ParseEnum<BracePlacement>(value, path, line,
                "expected Preserve, Attach or NewLine");
    } else if (key == "LambdaBracePlacement") {
        config.FormattingStyle.LambdaBracePlacement =
            ParseEnum<BracePlacement>(value, path, line,
                "expected Preserve, Attach or NewLine");
    } else if (key == "NamespaceBracePlacement") {
        config.FormattingStyle.NamespaceBracePlacement =
            ParseEnum<BracePlacement>(value, path, line,
                "expected Preserve, Attach or NewLine");
    } else if (key == "NamespaceComments") {
        config.FormattingStyle.NamespaceComments =
            ParseEnum<NamespaceComments>(value, path, line,
                "invalid NamespaceComments");
    } else if (key == "FunctionParameterSplit") {
        config.FormattingStyle.FunctionParameterSplit =
            ParseEnum<FunctionParameterSplit>(value, path, line,
                "invalid FunctionParameterSplit");
    } else if (key == "FirstFunctionParameterPosition") {
        config.FormattingStyle.FirstFunctionParameterPosition =
            ParseEnum<FirstFunctionParameterPosition>(value, path, line,
                "invalid FirstFunctionParameterPosition");
    } else if (key == "EnforceFirstFunctionParameterPosition") {
        config.FormattingStyle.EnforceFirstFunctionParameterPosition =
            ParseBoolean(value, path, line);
    } else if (key == "FunctionParameterIndent") {
        config.FormattingStyle.FunctionParameterIndent =
            ParseEnum<FunctionParameterIndent>(value, path, line,
                "invalid FunctionParameterIndent");
    } else if (key == "CallArgumentSplit") {
        config.FormattingStyle.CallArgumentSplit =
            ParseEnum<CallArgumentSplit>(value, path, line,
                "invalid CallArgumentSplit");
    } else if (key == "FirstCallArgumentPosition") {
        config.FormattingStyle.FirstCallArgumentPosition =
            ParseEnum<FirstCallArgumentPosition>(value, path, line,
                "invalid FirstCallArgumentPosition");
    } else if (key == "EnforceFirstCallArgumentPosition") {
        config.FormattingStyle.EnforceFirstCallArgumentPosition =
            ParseBoolean(value, path, line);
    } else if (key == "CallArgumentIndent") {
        config.FormattingStyle.CallArgumentIndent =
            ParseEnum<CallArgumentIndent>(value, path, line,
                "invalid CallArgumentIndent");
    } else if (key == "ExpressionLayoutCondition") {
        config.FormattingStyle.ExpressionLayoutCondition =
            ParseEnum<ExpressionLayoutCondition>(value, path, line,
                "expected Preserve, Overflow, FitOrOverflow or Always for "
                "ExpressionLayoutCondition");
    } else if (key == "ExpressionLayoutAlgorithm") {
        config.FormattingStyle.ExpressionLayoutAlgorithm =
            ParseEnum<ExpressionLayoutAlgorithm>(value, path, line,
                "expected Greedy or DynamicProgramming for ExpressionLayoutAlgorithm");
    } else if (key == "BinaryOperatorPlacement") {
        config.FormattingStyle.BinaryOperatorPlacement =
            ParseEnum<BinaryOperatorPlacement>(value, path, line,
                "expected Leading or Trailing for BinaryOperatorPlacement");
    } else if (key == "TernaryOperatorPlacement") {
        config.FormattingStyle.TernaryOperatorPlacement =
            ParseEnum<TernaryOperatorPlacement>(value, path, line,
                "expected Preserve, Leading or Trailing for TernaryOperatorPlacement");
    } else if (key == "TernarySplit") {
        config.FormattingStyle.TernarySplit =
            ParseEnum<TernarySplit>(value, path, line,
                "expected Preserve, AsNeeded or ThreeLines for TernarySplit");
    } else if (key == "MergeFunctionParameters") {
        config.FormattingStyle.MergeFunctionParameters =
            ParseBoolean(value, path, line);
    } else if (key == "ControlBlockBracePlacement") {
        config.FormattingStyle.ControlBlockBracePlacement =
            ParseEnum<ControlBlockBracePlacement>(value, path, line,
                "expected Preserve, SameLine, NewLine or AttachUnlessMultiline");
    } else if (key == "ControlBlockKeywordSpacing") {
        config.FormattingStyle.ControlBlockKeywordSpacing =
            ParseEnum<ControlBlockKeywordSpacing>(value, path, line,
                "expected Preserve, Space or NoSpace");
    } else if (key == "ClassBraceSpacing") {
        config.FormattingStyle.ClassBraceSpacing =
            ParseEnum<SameLineBraceSpacing>(value, path, line,
                "expected Preserve or SingleSpace");
    } else if (key == "LambdaBraceSpacing") {
        config.FormattingStyle.LambdaBraceSpacing =
            ParseEnum<SameLineBraceSpacing>(value, path, line,
                "expected Preserve or SingleSpace");
    } else if (key == "FunctionCallSpaceBeforeParameters") {
        config.FormattingStyle.FunctionCallSpaceBeforeParameters =
            ParseEnum<FunctionCallSpaceBeforeParameters>(value, path, line,
                "expected Preserve, Space or NoSpace");
    } else if (key == "SpaceInsideCallArguments") {
        config.FormattingStyle.SpaceInsideCallArguments =
            ParseEnum<SpaceInsideCallArguments>(value, path, line,
                "expected Preserve, Space or NoSpace");
    } else if (key == "SpaceInsideLambdaParameters") {
        config.FormattingStyle.SpaceInsideLambdaParameters =
            ParseEnum<SpaceInsideLambdaParameters>(value, path, line,
                "expected Preserve, Space or NoSpace");
    } else if (key == "SpaceInsideFunctionParameters") {
        config.FormattingStyle.SpaceInsideFunctionParameters =
            ParseEnum<SpaceInsideFunctionParameters>(value, path, line,
                "expected Preserve, Space or NoSpace");
    } else if (key == "FunctionSpaceBeforeParameters") {
        config.FormattingStyle.FunctionSpaceBeforeParameters =
            ParseEnum<FunctionSpaceBeforeParameters>(value, path, line,
                "expected Preserve, Space or NoSpace");
    } else if (key == "IndentAccessModifiers") {
        config.FormattingStyle.IndentAccessModifiers =
            ParseEnum<BodyIndentation>(value, path, line,
                "expected Preserve, Indent or NoIndent");
    } else if (key == "IndentMembers") {
        config.FormattingStyle.IndentMembers =
            ParseEnum<BodyIndentation>(value, path, line,
                "expected Preserve, Indent or NoIndent");
    } else if (key == "InheritanceList") {
        config.FormattingStyle.InheritanceList =
            ParseEnum<InheritanceListMode>(value, path, line,
                "expected Preserve or Format");
    } else if (key == "InheritanceListColonPlacement") {
        config.FormattingStyle.InheritanceListColonPlacement =
            ParseEnum<InheritanceListColonPlacement>(value, path, line,
                "expected AttachToBase or AttachToClass");
    } else if (key == "InheritanceListColonSpaceBefore") {
        config.FormattingStyle.InheritanceListColonSpaceBefore =
            ParseEnum<InheritanceListSpacing>(value, path, line,
                "expected Space or NoSpace");
    } else if (key == "InheritanceListColonSpaceAfter") {
        config.FormattingStyle.InheritanceListColonSpaceAfter =
            ParseEnum<InheritanceListSpacing>(value, path, line,
                "expected Space or NoSpace");
    } else if (key == "InheritanceListCommaPlacement") {
        config.FormattingStyle.InheritanceListCommaPlacement =
            ParseEnum<InheritanceListCommaPlacement>(value, path, line,
                "expected AttachToNext or AttachToPrevious");
    } else if (key == "InheritanceListCommaSpaceBefore") {
        config.FormattingStyle.InheritanceListCommaSpaceBefore =
            ParseEnum<InheritanceListSpacing>(value, path, line,
                "expected Space or NoSpace");
    } else if (key == "InheritanceListCommaSpaceAfter") {
        config.FormattingStyle.InheritanceListCommaSpaceAfter =
            ParseEnum<InheritanceListSpacing>(value, path, line,
                "expected Space or NoSpace");
    } else if (key == "InheritanceListStart") {
        config.FormattingStyle.InheritanceListStart =
            ParseEnum<InheritanceListStart>(value, path, line,
                "expected NewLine, ClassLine or NewLineOnOverflow");
    } else if (key == "InheritanceListSplit") {
        config.FormattingStyle.InheritanceListSplit =
            ParseEnum<InheritanceListSplit>(value, path, line,
                "expected OnePerLine, Pack or OnePerLineOnOverflow");
    } else if (key == "InheritanceListIndent") {
        config.FormattingStyle.InheritanceListIndent =
            ParsePositiveInteger(value, path, line);
        if (config.FormattingStyle.InheritanceListIndent > 32) {
            ThrowConfigError(path, line,
                "InheritanceListIndent must not exceed 32");
        }
    } else if (key == "SpaceAroundCaptureList") {
        config.FormattingStyle.SpaceAroundCaptureList =
            ParseEnum<LambdaCaptureSpacing>(value, path, line,
                "expected Preserve, Space or NoSpace");
    } else if (key == "SpaceAroundCaptureAssignment") {
        config.FormattingStyle.SpaceAroundCaptureAssignment =
            ParseEnum<LambdaCaptureSpacing>(value, path, line,
                "expected Preserve, Space or NoSpace");
    } else if (key == "LambdaSpaceBeforeParameters") {
        config.FormattingStyle.LambdaSpaceBeforeParameters =
            ParseEnum<LambdaSpaceBeforeParameters>(value, path, line,
                "expected Preserve, Space or NoSpace");
    } else if (key == "StructBraceSpacing") {
        config.FormattingStyle.StructBraceSpacing =
            ParseEnum<SameLineBraceSpacing>(value, path, line,
                "expected Preserve or SingleSpace");
    } else if (key == "EnumBraceSpacing") {
        config.FormattingStyle.EnumBraceSpacing =
            ParseEnum<SameLineBraceSpacing>(value, path, line,
                "expected Preserve or SingleSpace");
    } else if (key == "UnionBraceSpacing") {
        config.FormattingStyle.UnionBraceSpacing =
            ParseEnum<SameLineBraceSpacing>(value, path, line,
                "expected Preserve or SingleSpace");
    } else if (key == "FunctionBraceSpacing") {
        config.FormattingStyle.FunctionBraceSpacing =
            ParseEnum<SameLineBraceSpacing>(value, path, line,
                "expected Preserve or SingleSpace");
    } else if (key == "NamespaceBraceSpacing") {
        config.FormattingStyle.NamespaceBraceSpacing =
            ParseEnum<SameLineBraceSpacing>(value, path, line,
                "expected Preserve or SingleSpace");
    } else if (key == "FunctionDeclarationParameterNames") {
        config.DeclarationParameterNames =
            ParseEnum<FunctionDeclarationParameterNames>(value, path, line,
                "invalid FunctionDeclarationParameterNames");
    } else if (key == "FunctionDefinitionParameterNames") {
        config.DefinitionParameterNames =
            ParseEnum<FunctionDefinitionParameterNames>(value, path, line,
                "invalid FunctionDefinitionParameterNames");
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
