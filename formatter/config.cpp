#include "config.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <fstream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace formatter {
namespace {
std::string_view Trim(std::string_view text)
{
    while (
        !text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
    {
        text.remove_prefix(1);
    }
    while (
        !text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
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
        path.string() + ":" + std::to_string(line) + ": " +
            std::string(message));
}
// A closing bracket inside a quoted item does not terminate an array.
// Quoted items themselves cannot span physical lines.
bool HasArrayEnd(
    std::string_view text,
    const std::filesystem::path& path,
    size_t line)
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

size_t ParseNonnegativeInteger(
    std::string_view value,
    const std::filesystem::path& path,
    size_t line)
{
    size_t result = 0;
    auto [end, error] =
        std::from_chars(value.data(), value.data() + value.size(), result);
    if (error != std::errc{} || end != value.data() + value.size()) {
        ThrowConfigError(path, line, "expected a nonnegative integer");
    }
    return result;
}

template <typename T>
T ParseEnum(
    std::string_view value,
    const std::filesystem::path& path,
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

std::vector<std::string> ParseNamespaceExemptions(
    std::string_view value,
    const std::filesystem::path& path,
    size_t line)
{
    auto names = ParseStringList(value, path, line);
    for (const auto& name : names) {
        size_t start = 0;
        while (start < name.size()) {
            auto end = name.find("::", start);
            if (end == std::string::npos) {
                end = name.size();
            }
            auto part = std::string_view(name).substr(start, end - start);
            if (
                part.empty() ||
                    !(std::isalpha(static_cast<unsigned char>(part.front())) ||
                            part.front() == '_') ||
                    part
                        .find_first_not_of(
                            "abcdefghijklmnopqrstuvwxyz"
                                "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") !=
                        std::string_view::npos)
            {
                ThrowConfigError(
                    path,
                    line,
                    "expected qualified namespace names");
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
    std::string_view value,
    const std::filesystem::path& path,
    size_t line)
{
    auto names = ParseStringList(value, path, line);
    for (const auto& name : names) {
        if (
            name.empty() ||
                !(std::isalpha(static_cast<unsigned char>(name.front())) ||
                        name.front() == '_') ||
                name
                    .find_first_not_of(
                        "abcdefghijklmnopqrstuvwxyz"
                            "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") !=
                    std::string::npos)
        {
            ThrowConfigError(path, line, "expected call identifier names");
        }
    }
    return names;
}
std::optional<std::vector<std::string>> ParseNamePrefixes(
    std::string_view value,
    const std::filesystem::path& path,
    size_t line)
{
    auto prefixes = ParseStringList(value, path, line);
    if (prefixes.empty()) {
        return std::nullopt;
    }
    for (const auto& prefix : prefixes) {
        if (
            prefix
                .find_first_not_of(
                    "abcdefghijklmnopqrstuvwxyz"
                        "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") !=
                std::string::npos)
        {
            ThrowConfigError(
                path,
                line,
                "prefix must contain only identifier characters");
        }
    }
    return prefixes;
}

template <typename T>
void ParseField(
    T& field,
    std::string_view value,
    const std::filesystem::path& path,
    size_t line)
{
    if constexpr (std::is_enum_v<T>) {
        field = ParseEnum<T>(value, path, line);
    } else if constexpr (std::is_same_v<T, bool>) {
        field = ParseBoolean(value, path, line);
    } else if constexpr (std::is_same_v<T, size_t>) {
        field = ParseNonnegativeInteger(value, path, line);
    } else {
        static_assert(std::is_same_v<T, void>, "unsupported config field type");
    }
}
template <typename T>
void ParseField(
    std::optional<T>& field,
    std::string_view value,
    const std::filesystem::path& path,
    size_t line)
{
    if (value == "Inherit") {
        field.reset();
        return;
    }
    T parsed{};
    ParseField(parsed, value, path, line);
    field = parsed;
}
void ParseField(
    std::optional<std::vector<std::string>>& field,
    std::string_view value,
    const std::filesystem::path& path,
    size_t line)
{
    field = ParseNamePrefixes(value, path, line);
}
void ParseField(
    std::vector<std::string>& field,
    std::string_view value,
    const std::filesystem::path& path,
    size_t line)
{
    field = ParseNamespaceExemptions(value, path, line);
}
void ParseField(
    std::vector<std::regex>& field,
    std::string_view value,
    const std::filesystem::path& path,
    size_t line)
{
    field.clear();
    for (const auto& pattern : ParseStringList(value, path, line)) {
        if (pattern.empty()) {
            ThrowConfigError(path, line, "Exclude regex must not be empty");
        }
        try {
            field.emplace_back(pattern);
        } catch (const std::regex_error& error) {
            ThrowConfigError(
                path,
                line,
                "invalid Exclude regex '" + pattern + "': " + error.what());
        }
    }
}

template <auto Member>
void AssignStyle(
    Configuration& config,
    std::string_view value,
    const std::filesystem::path& path,
    size_t line)
{
    if constexpr (std::is_same_v<std::remove_cvref_t<
            decltype(config.FormattingStyle.*Member)>,
            std::vector<std::string>>)
    {
        if constexpr (
            Member == &Style::ChainedCallRootNames ||
                Member == &Style::IndentedCallScopePairs ||
                Member == &Style::IncludeGroupPrefixes)
        {
            config.FormattingStyle.*Member = ParseStringList(value, path, line);
        } else {
            config.FormattingStyle.*Member = ParseCallNames(value, path, line);
        }
    } else if constexpr (std::is_same_v<std::remove_cvref_t<
            decltype(config.FormattingStyle.*Member)>,
            std::optional<TrailingCommentSpaceSetting>>)
    {
        config.FormattingStyle.*Member = TrailingCommentSpaceSetting{
            value == "Preserve" ? std::nullopt : std::optional<std::size_t>{
                ParseNonnegativeInteger(value, path, line)}};
    } else if constexpr (std::is_same_v<std::remove_cvref_t<
            decltype(config.FormattingStyle.*Member)>, BlankLineSetting>)
    {
        if (value == "Preserve") {
            config.FormattingStyle.*Member = {};
        } else if (value == "Inherit") {
            if constexpr (Member == &Style::MemberDefinitionBlankLines) {
                config.FormattingStyle.*Member = BlankLineSetting::Inherited();
            } else {
                ThrowConfigError(
                    path,
                    line,
                    "Inherit is only valid for MemberDefinitionBlankLines");
            }
        } else {
            config.FormattingStyle.*Member = {BlankLineMode::Exact,
                ParseNonnegativeInteger(value, path, line)};
        }
    } else if constexpr (std::is_same_v<std::remove_cvref_t<
            decltype(config.FormattingStyle.*Member)>, std::optional<std::
                size_t>>)
    {
        if constexpr (Member == &Style::MaxEmptyLines) {
            config.FormattingStyle.*Member = value == "Preserve"
                ? std::nullopt : std::optional<std::size_t>{
                    ParseNonnegativeInteger(value, path, line)};
        } else {
            ParseField(config.FormattingStyle.*Member, value, path, line);
        }
    } else {
        ParseField(config.FormattingStyle.*Member, value, path, line);
    }
}

template <auto Member>
void AssignConfig(
    Configuration& config,
    std::string_view value,
    const std::filesystem::path& path,
    size_t line)
{
    ParseField(config.*Member, value, path, line);
}
template <auto Member>
void AssignNamingCase(
    Configuration& config,
    std::string_view value,
    const std::filesystem::path& path,
    size_t line)
{
    ParseField((config.Naming.*Member).Case, value, path, line);
}
template <auto Member>
void AssignNamingPrefixes(
    Configuration& config,
    std::string_view value,
    const std::filesystem::path& path,
    size_t line)
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
#define STYLE_BINDING(type, name, initial)                                     \
    {#name, &AssignStyle<&Style::name>},
    FORMATTER_STYLE_OPTIONS(STYLE_BINDING)
#undef STYLE_BINDING
#define NAMING_CATEGORY_BINDINGS(category)                                     \
    {#category "NameCase", &AssignNamingCase<&NamingStyle::category>},         \
    {#category "NamePrefixes", &AssignNamingPrefixes<&NamingStyle::category>},
    FORMATTER_NAMING_CATEGORIES(NAMING_CATEGORY_BINDINGS)
#undef NAMING_CATEGORY_BINDINGS
#define CONFIG_BINDING(type, name, initial)                                    \
    {#name, &AssignConfig<&Configuration::name>},
    FORMATTER_CONFIGURATION_OPTIONS(CONFIG_BINDING)
#undef CONFIG_BINDING
});

void ValidateStyleLimits(
    const Style& style,
    const std::filesystem::path& path,
    size_t line,
    std::span<const std::size_t> settingLines)
{
    try {
        ValidatePolicyStyle(style);
    } catch (const std::invalid_argument& error) {
        const std::string_view reason = error.what();
        std::string message(reason);
        for (std::size_t index = 0; index < Options.size(); ++index) {
            if (settingLines[index] == 0) {
                continue;
            }
            const auto key = Options[index].Name;
            auto at = reason.find(key);
            while (at != std::string_view::npos) {
                const auto identifier = [] (char value) {
                    return std::isalnum(static_cast<unsigned char>(value)) ||
                        value == '_';
                };
                if (
                    (at == 0 || !identifier(reason[at - 1])) &&
                        (at + key.size() == reason.size() ||
                                !identifier(reason[at + key.size()])))
                {
                    message +=
                        " [" + std::string(key) + " at " + path.string() + ":" +
                            std::to_string(settingLines[index]) + "]";
                    break;
                }
                at = reason.find(key, at + key.size());
            }
        }
        ThrowConfigError(path, line, message);
    }
}

void ApplySetting(
    Configuration& config,
    std::string_view key,
    std::string_view value,
    const std::filesystem::path& path,
    size_t line,
    std::span<std::size_t> settingLines)
{
    for (std::size_t index = 0; index < Options.size(); ++index) {
        const auto& option = Options[index];
        if (option.Name == key) {
            option.Assign(config, value, path, line);
            settingLines[index] = line;
            return;
        }
    }
    ThrowConfigError(
        path,
        line,
        "unknown setting '" + std::string(key) + "'; see --help-config.");
}
} // namespace
namespace {
bool AnnotationIdentifier(std::string_view name)
{
    constexpr std::string_view first =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_";
    constexpr std::string_view rest =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_0123456789";
    return !name.empty() &&
        first.find(name.front()) != std::string_view::npos &&
        name.find_first_not_of(rest) == std::string_view::npos;
}
bool AnnotationQualifiedName(std::string_view name)
{
    while (true) {
        const auto separator = name.find("::");
        if (!AnnotationIdentifier(name.substr(0, separator))) {
            return false;
        }
        if (separator == std::string_view::npos) {
            return true;
        }
        name.remove_prefix(separator + 2);
    }
}
void RejectDuplicateAnnotations(
    std::vector<std::string_view>& names,
    std::string_view key)
{
    std::sort(names.begin(), names.end());
    if (std::adjacent_find(names.begin(), names.end()) != names.end()) {
        throw std::invalid_argument(
            "duplicate " + std::string(key) +
                " annotation name or scope endpoint");
    }
}
void ValidateAnnotationNames(
    std::span<const std::string> input,
    std::vector<std::string_view>& scratch,
    std::string_view key,
    bool qualified)
{
    scratch.clear();
    for (const auto& name : input) {
        if (
            !(qualified
                    ? AnnotationQualifiedName(name)
                    : AnnotationIdentifier(name)))
        {
            throw std::invalid_argument(
                "invalid " + std::string(key) + " annotation name");
        }
        scratch.push_back(name);
    }
    RejectDuplicateAnnotations(scratch, key);
}
std::pair<std::string_view, std::string_view> AnnotationScopePair(
    std::string_view pair)
{
    const auto colon = pair.find(':');
    if (colon == std::string_view::npos) {
        throw std::invalid_argument(
            "IndentedCallScopePairs expects Begin:End identifier pairs");
    }
    const auto begin = pair.substr(0, colon);
    const auto end = pair.substr(colon + 1);
    if (
        !AnnotationIdentifier(begin) || !AnnotationIdentifier(end) ||
            begin == end)
    {
        throw std::invalid_argument(
            "IndentedCallScopePairs expects distinct Begin:End identifiers");
    }
    return {begin, end};
}
} // namespace

void ValidateLibraryAnnotationStyle(const Style& style)
{
    std::vector<std::string_view> scratch;
    scratch
        .reserve(
            std::max(
                {
                    style.CallLineFirstStringNames.size(),
                    style.ChainedCallRootNames.size(),
                    style.IndentedCallScopePairs.size() * 2,
                }));
    ValidateAnnotationNames(
        style.CallLineFirstStringNames,
        scratch,
        "CallLineFirstStringNames",
        false);
    ValidateAnnotationNames(
        style.ChainedCallRootNames,
        scratch,
        "ChainedCallRootNames",
        true);
    scratch.clear();
    for (const auto& pair : style.IndentedCallScopePairs) {
        const auto [begin, end] = AnnotationScopePair(pair);
        scratch.push_back(begin);
        scratch.push_back(end);
    }
    RejectDuplicateAnnotations(scratch, "IndentedCallScopePairs");
}

LibraryAnnotations CompileLibraryAnnotations(const Style& style)
{
    ValidateLibraryAnnotationStyle(style);
    LibraryAnnotations result;
    result.MessagePins_.reserve(style.CallLineFirstStringNames.size());
    for (const auto& name : style.CallLineFirstStringNames) {
        result.MessagePins_.push_back({name});
    }
    std::sort(
        result.MessagePins_.begin(),
        result.MessagePins_.end(),
        [] (const auto& left, const auto& right) {
            return left.Name < right.Name;
        });
    result.ChainRoots_ = style.ChainedCallRootNames;
    std::sort(result.ChainRoots_.begin(), result.ChainRoots_.end());
    result.VisualScopes_.reserve(style.IndentedCallScopePairs.size());
    for (const auto& pair : style.IndentedCallScopePairs) {
        const auto [begin, end] = AnnotationScopePair(pair);
        result.VisualScopes_.push_back({std::string(begin), std::string(end)});
    }
    std::sort(
        result.VisualScopes_.begin(),
        result.VisualScopes_.end(),
        [] (const auto& left, const auto& right) {
            return left.Begin < right.Begin;
        });
    result.ScopeEndOrder_.reserve(result.VisualScopes_.size());
    for (std::size_t index = 0; index < result.VisualScopes_.size(); ++index) {
        result.ScopeEndOrder_.push_back(index);
    }
    std::sort(
        result.ScopeEndOrder_.begin(),
        result.ScopeEndOrder_.end(),
        [&] (auto left, auto right) {
            return result.VisualScopes_[left].End <
                result.VisualScopes_[right].End;
        });
    return result;
}
std::span<const MessagePinAnnotation>LibraryAnnotations::MessagePins() const
{
    return MessagePins_;
}
const MessagePinAnnotation* LibraryAnnotations::FindMessagePin(
    std::string_view name) const noexcept
{
    const auto found =
        std::lower_bound(
            MessagePins_.begin(),
            MessagePins_.end(),
            name,
            [] (const auto& value, auto key) { return value.Name < key; });
    return found != MessagePins_.end() && found->Name == name
        ? &*found
        : nullptr;
}
ChainParticipation LibraryAnnotations::Chains() const noexcept
{
    return ChainRoots_.empty()
        ? ChainParticipation::AllSafe
        : ChainParticipation::AnnotatedRoots;
}
bool LibraryAnnotations::IsAnnotatedChainRoot(
    std::string_view name) const noexcept
{
    return std::binary_search(ChainRoots_.begin(), ChainRoots_.end(), name);
}
std::span<const std::string>LibraryAnnotations::ChainRoots() const
{
    return ChainRoots_;
}
std::span<const VisualScopeAnnotation>LibraryAnnotations::VisualScopes() const
{
    return VisualScopes_;
}
const VisualScopeAnnotation* LibraryAnnotations::FindScopeBegin(
    std::string_view name) const noexcept
{
    const auto found =
        std::lower_bound(
            VisualScopes_.begin(),
            VisualScopes_.end(),
            name,
            [] (const auto& value, auto key) { return value.Begin < key; });
    return found != VisualScopes_.end() && found->Begin == name
        ? &*found
        : nullptr;
}
const VisualScopeAnnotation* LibraryAnnotations::FindScopeEnd(
    std::string_view name) const noexcept
{
    const auto found =
        std::lower_bound(
            ScopeEndOrder_.begin(),
            ScopeEndOrder_.end(),
            name,
            [
                &] (
                auto index,
                auto key) { return VisualScopes_[index].End < key; });
    return found != ScopeEndOrder_.end() && VisualScopes_[*found].End == name
        ? &VisualScopes_[*found]
        : nullptr;
}

Configuration LoadConfigFile(const std::filesystem::path& path)
{
    std::ifstream input(path);
    if (!input.is_open()) {
        throw std::runtime_error(
            "cannot open configuration file: " + path.string());
    }

    Configuration config;
    std::array<std::size_t, Options.size()> settingLines{};
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
                            "failed to read configuration file: " +
                                path.string());
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
        ApplySetting(config, key, value, path, settingLine, settingLines);
    }
    if (!input.eof()) {
        throw std::runtime_error(
            "failed to read configuration file: " + path.string());
    }
    ValidateStyleLimits(config.FormattingStyle, path, lineNumber, settingLines);
    try {
        ValidateLibraryAnnotationStyle(config.FormattingStyle);
    } catch (const std::invalid_argument& error) {
        ThrowConfigError(path, lineNumber, error.what());
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
