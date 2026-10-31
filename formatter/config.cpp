#include "config.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
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
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) {
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

[[noreturn]] void ThrowConfigError(const std::filesystem::path& path, size_t line, std::string_view message)
{
    throw std::runtime_error(path.string() + ":" + std::to_string(line) + ": " + std::string(message));
}

// Nested prefix arrays and quoted brackets do not terminate an outer array.
// Quoted items themselves cannot span physical lines.
size_t ArrayDepth(std::string_view text, size_t depth, const std::filesystem::path& path, size_t line)
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
        } else if (character == '[' && !quoted) {
            ++depth;
        } else if (character == ']' && !quoted) {
            if (depth == 0) {
                ThrowConfigError(path, line, "unexpected closing array bracket");
            }
            --depth;
        }
    }
    if (quoted) {
        ThrowConfigError(path, line, "unterminated quoted array item");
    }
    return depth;
}

size_t ParseNonnegativeInteger(std::string_view value, const std::filesystem::path& path, size_t line)
{
    size_t result = 0;
    auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), result);
    if (error != std::errc{} || end != value.data() + value.size()) {
        ThrowConfigError(path, line, "expected a nonnegative integer");
    }
    return result;
}

template <typename T>
T ParseEnum(std::string_view value, const std::filesystem::path& path, size_t line)
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

std::string ParseString(std::string_view value, const std::filesystem::path& path, size_t line)
{
    value = Trim(value);
    if (value.size() < 2 || value.front() != '"' || value.back() != '"') {
        ThrowConfigError(path, line, "expected a quoted string");
    }

    std::string result;
    result.reserve(value.size() - 2);
    for (size_t index = 1; index + 1 < value.size(); ++index) {
        char character = value[index];
        if (character == '"') {
            ThrowConfigError(path, line, "unescaped quote inside string");
        }
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

std::vector<std::string> ParseStringList(std::string_view value, const std::filesystem::path& path, size_t line)
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

std::vector<IncludeGroupRule> ParseIncludeGroups(std::string_view value, const std::filesystem::path& path, size_t line)
{
    auto expect = [&] (char character) {
        value = Trim(value);
        if (value.empty() || value.front() != character) {
            ThrowConfigError(path, line, "IncludeGroups expected '" + std::string(1, character) + "'");
        }
        value.remove_prefix(1);
    };
    auto fieldValue = [&] {
        bool quoted = false;
        bool escaped = false;
        size_t depth = 0;
        size_t end = 0;
        for (; end < value.size(); ++end) {
            const char character = value[end];
            if (escaped) {
                escaped = false;
            } else if (quoted && character == '\\') {
                escaped = true;
            } else if (character == '"') {
                quoted = !quoted;
            } else if (!quoted) {
                if (character == '[') {
                    ++depth;
                } else if (character == ']') {
                    if (!depth) {
                        break;
                    }
                    --depth;
                } else if (!depth && (character == ',' || character == '}')) {
                    break;
                }
            }
        }
        auto result = Trim(value.substr(0, end));
        value.remove_prefix(end);
        return result;
    };
    expect('[');
    std::vector<IncludeGroupRule> result;
    while (true) {
        value = Trim(value);
        if (!value.empty() && value.front() == ']') {
            value.remove_prefix(1);
            if (!Trim(value).empty()) {
                ThrowConfigError(path, line, "unexpected text after IncludeGroups array");
            }
            return result;
        }
        expect('{');
        IncludeGroupRule group;
        bool prefixes = false;
        bool standard = false;
        bool depth = false;
        bool priority = false;
        while (true) {
            value = Trim(value);
            if (!value.empty() && value.front() == '}') {
                value.remove_prefix(1);
                break;
            }
            const auto end = value.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_");
            const auto name = value.substr(0, end);
            value.remove_prefix(name.size());
            expect('=');
            const auto field = fieldValue();
            if (name == "Prefix" || name == "Prefixes") {
                if (prefixes) {
                    ThrowConfigError(path, line, "IncludeGroups requires at most one Prefix or Prefixes field");
                }
                prefixes = true;
                group.Prefixes = name == "Prefix"
                    ? std::vector<std::string>{ParseString(field, path, line)}
                    : ParseStringList(field, path, line);
            } else if (name == "StandardHeaders") {
                if (standard) {
                    ThrowConfigError(path, line, "IncludeGroups has duplicate StandardHeaders field");
                }
                standard = true;
                if (field != "true") {
                    ThrowConfigError(path, line, "IncludeGroups StandardHeaders must be true");
                }
                group.StandardHeaders = true;
            } else if (name == "Depth") {
                if (depth) {
                    ThrowConfigError(path, line, "IncludeGroups has duplicate Depth field");
                }
                depth = true;
                auto [end, error] = std::from_chars(field.data(), field.data() + field.size(), group.Depth);
                if (error != std::errc{} || end != field.data() + field.size() || group.Depth < 0) {
                    ThrowConfigError(path, line, "IncludeGroups Depth must be an integer " "from 0 to 127");
                }
            } else if (name == "Priority") {
                if (priority) {
                    ThrowConfigError(path, line, "IncludeGroups has duplicate Priority field");
                }
                priority = true;
                auto [end, error] = std::from_chars(field.data(), field.data() + field.size(), group.Priority);
                if (error != std::errc{} || end != field.data() + field.size()) {
                    ThrowConfigError(path, line, "IncludeGroups Priority must be an integer " "from -128 to 127");
                }
            } else {
                ThrowConfigError(path, line, "unknown IncludeGroups field '" + std::string(name) + "'");
            }
            value = Trim(value);
            if (!value.empty() && value.front() == '}') {
                continue;
            }
            expect(',');
        }
        if (!prefixes && !standard) {
            ThrowConfigError(path, line, "IncludeGroups requires Prefix, Prefixes or StandardHeaders = true");
        }
        result.push_back(std::move(group));
        value = Trim(value);
        if (!value.empty() && value.front() == ']') {
            continue;
        }
        expect(',');
    }
}

bool ParseBoolean(std::string_view value, const std::filesystem::path& path, size_t line)
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
            if (part.empty() || !(std::isalpha(static_cast<unsigned char>(part.front())) || part.front() == '_') ||
                part.find_first_not_of("abcdefghijklmnopqrstuvwxyz" "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") !=
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

std::vector<std::string> ParseCallNames(std::string_view value, const std::filesystem::path& path, size_t line)
{
    auto names = ParseStringList(value, path, line);
    for (const auto& name : names) {
        if (name.empty() || !(std::isalpha(static_cast<unsigned char>(name.front())) || name.front() == '_') ||
            name.find_first_not_of("abcdefghijklmnopqrstuvwxyz" "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") !=
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
        if (prefix.find_first_not_of("abcdefghijklmnopqrstuvwxyz" "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") !=
            std::string::npos)
        {
            ThrowConfigError(path, line, "prefix must contain only identifier characters");
        }
    }
    return prefixes;
}

template <typename T>
void ParseField(T& field, std::string_view value, const std::filesystem::path& path, size_t line)
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
void ParseField(std::optional<T>& field, std::string_view value, const std::filesystem::path& path, size_t line)
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

void ParseField(std::vector<std::string>& field, std::string_view value, const std::filesystem::path& path, size_t line)
{
    field = ParseNamespaceExemptions(value, path, line);
}

void ParseField(std::vector<std::regex>& field, std::string_view value, const std::filesystem::path& path, size_t line)
{
    field.clear();
    for (const auto& pattern : ParseStringList(value, path, line)) {
        if (pattern.empty()) {
            ThrowConfigError(path, line, "Exclude regex must not be empty");
        }
        try {
            field.emplace_back(pattern);
        } catch (const std::regex_error& error) {
            ThrowConfigError(path, line, "invalid Exclude regex '" + pattern + "': " + error.what());
        }
    }
}

void ParseField(
    std::vector<IncludeGroupRule>& field,
    std::string_view value,
    const std::filesystem::path& path,
    size_t line)
{
    field = ParseIncludeGroups(value, path, line);
}

template <auto Member>
void AssignStyle(Configuration& config, std::string_view value, const std::filesystem::path& path, size_t line)
{
    if constexpr (std::is_same_v<std::remove_cvref_t<
            decltype(config.FormattingStyle.*Member)>,
            std::vector<std::string>>)
    {
        if constexpr (Member == &Style::ChainedCallRootNames || Member == &Style::AlwaysBreakChainRootNames ||
            Member == &Style::IndentedCallScopePairs || Member == &Style::IndentedChainScopePairs ||
            Member == &Style::StickyChainCallPairs || Member == &Style::ClassMacroNames)
        {
            config.FormattingStyle.*Member = ParseStringList(value, path, line);
        } else {
            config.FormattingStyle.*Member = ParseCallNames(value, path, line);
        }
    } else if constexpr (std::is_same_v<std::remove_cvref_t<
            decltype(config.FormattingStyle.*Member)>, BodyIndentation>)
    {
        if constexpr (Member == &Style::IndentPreprocessingDirectives) {
            if (value == "true" || value == "false") {
                value = value == "true" ? "Indent" : "Preserve";
            }
        }
        ParseField(config.FormattingStyle.*Member, value, path, line);
    } else if constexpr (std::is_same_v<std::remove_cvref_t<
            decltype(config.FormattingStyle.*Member)>,
            std::optional<TrailingCommentSpaceSetting>>)
    {
        config.FormattingStyle.*Member =
            TrailingCommentSpaceSetting{
                value == "Preserve"
                    ? std::nullopt
                    : std::optional<std::size_t>{ParseNonnegativeInteger(value, path, line)},
            };
    } else if constexpr (std::is_same_v<std::remove_cvref_t<
            decltype(config.FormattingStyle.*Member)>, BlankLineSetting>)
    {
        if (value == "Preserve") {
            config.FormattingStyle.*Member = {};
        } else if (value == "Inherit") {
            if constexpr (Member == &Style::MemberDefinitionBlankLines || Member == &Style::MemberDeclarationBlankLines)
            {
                config.FormattingStyle.*Member = BlankLineSetting::Inherited();
            } else {
                ThrowConfigError(path, line, "Inherit is only valid for member blank-line settings");
            }
        } else {
            config.FormattingStyle.*Member = {BlankLineMode::Exact, ParseNonnegativeInteger(value, path, line)};
        }
    } else if constexpr (std::is_same_v<std::remove_cvref_t<
            decltype(config.FormattingStyle.*Member)>, std::optional<std::
                size_t>>)
    {
        if constexpr (Member == &Style::MaxEmptyLines) {
            config.FormattingStyle.*Member =
                value == "Preserve"
                ? std::nullopt
                : std::optional<std::size_t>{ParseNonnegativeInteger(value, path, line)};
        } else {
            ParseField(config.FormattingStyle.*Member, value, path, line);
        }
    } else {
        ParseField(config.FormattingStyle.*Member, value, path, line);
    }
}

template <auto Member>
void AssignConfig(Configuration& config, std::string_view value, const std::filesystem::path& path, size_t line)
{
    ParseField(config.*Member, value, path, line);
}

template <auto Member>
void AssignNamingCase(Configuration& config, std::string_view value, const std::filesystem::path& path, size_t line)
{
    ParseField((config.Naming.*Member).Case, value, path, line);
}

template <auto Member>
void AssignNamingPrefixes(Configuration& config, std::string_view value, const std::filesystem::path& path, size_t line)
{
    ParseField((config.Naming.*Member).Prefixes, value, path, line);
}

using OptionHandler = void (*)(Configuration&, std::string_view, const std::filesystem::path&, size_t);

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

struct SettingLocation
{
    std::filesystem::path Path;
    size_t Line = 0;
};

void ValidateStyleLimits(
    const Style& style,
    const std::filesystem::path& path,
    size_t line,
    std::span<const SettingLocation> settingLines)
{
    try {
        ValidatePolicyStyle(style);
    } catch (const std::invalid_argument& error) {
        const std::string_view reason = error.what();
        std::string message(reason);
        for (std::size_t index = 0; index < Options.size(); ++index) {
            if (settingLines[index].Line == 0) {
                continue;
            }
            const auto key = Options[index].Name;
            auto at = reason.find(key);
            while (at != std::string_view::npos) {
                const auto identifier = [] (char value) {
                    return std::isalnum(static_cast<unsigned char>(value)) || value == '_';
                };
                if ((at == 0 || !identifier(reason[at - 1])) &&
                    (at + key.size() == reason.size() || !identifier(reason[at + key.size()])))
                {
                    message += " [" + std::string(key) + " at " + settingLines[index].Path.string() + ":" +
                        std::to_string(settingLines[index].Line) + "]";
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
    std::span<SettingLocation> settingLines)
{
    for (std::size_t index = 0; index < Options.size(); ++index) {
        const auto& option = Options[index];
        if (option.Name == key) {
            option.Assign(config, value, path, line);
            settingLines[index] = {path, line};
            return;
        }
    }
    ThrowConfigError(path, line, "unknown setting '" + std::string(key) + "'; see --help-config.");
}

} // namespace

namespace {

bool AnnotationIdentifier(std::string_view name)
{
    constexpr std::string_view first = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_";
    constexpr std::string_view rest = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_0123456789";
    return !name.empty() && first.find(name.front()) != std::string_view::npos &&
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

void RejectDuplicateAnnotations(std::vector<std::string_view>& names, std::string_view key)
{
    std::sort(names.begin(), names.end());
    if (std::adjacent_find(names.begin(), names.end()) != names.end()) {
        throw std::invalid_argument("duplicate " + std::string(key) + " annotation name or scope endpoint");
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
        if (!(qualified ? AnnotationQualifiedName(name) : AnnotationIdentifier(name))) {
            throw std::invalid_argument("invalid " + std::string(key) + " annotation name");
        }
        scratch.push_back(name);
    }
    RejectDuplicateAnnotations(scratch, key);
}

std::pair<std::string_view, std::string_view> AnnotationScopePair(
    std::string_view pair,
    std::string_view key = "IndentedCallScopePairs",
    bool distinct = true)
{
    const auto colon = pair.find(':');
    if (colon == std::string_view::npos) {
        throw std::invalid_argument(std::string(key) + " expects First:Second identifier pairs");
    }
    const auto begin = pair.substr(0, colon);
    const auto end = pair.substr(colon + 1);
    if (!AnnotationIdentifier(begin) || !AnnotationIdentifier(end) || (distinct && begin == end)) {
        throw std::invalid_argument(std::string(key) + " expects valid identifier pairs");
    }
    return {begin, end};
}

std::vector<ChainCallPairAnnotation> CompileChainPairs(
    std::span<const std::string> input,
    std::string_view key,
    bool distinct)
{
    std::vector<ChainCallPairAnnotation> result;
    result.reserve(input.size());
    for (const auto& pair : input) {
        const auto [first, second] = AnnotationScopePair(pair, key, distinct);
        result.push_back({std::string(first), std::string(second)});
    }
    std::sort(
        result.begin(),
        result.end(),
        [] (const auto& a, const auto& b) {
            return a.First < b.First || (a.First == b.First && a.Second < b.Second);
        });
    return result;
}

} // namespace

void ValidateLibraryAnnotationStyle(const Style& style)
{
    std::vector<std::string_view> scratch;
    scratch
        .reserve(
            std::max({
                style.CallLineFirstArgumentNames.size(),
                style.CallLineFirstStringNames.size(),
                style.CallLineMessageNames.size(),
                style.ChainedCallRootNames.size(),
                style.AlwaysBreakChainRootNames.size(),
                style.AlwaysBreakChainCallNames.size(),
                style.IndentedCallScopePairs.size() * 2,
                style.IndentedChainScopePairs.size() * 2,
                style.StickyChainCallPairs.size(),
                style.ClassMacroNames.size() * 2,
            }));
    ValidateAnnotationNames(style.CallLineFirstArgumentNames, scratch, "CallLineFirstArgumentNames", false);
    ValidateAnnotationNames(style.CallLineFirstStringNames, scratch, "CallLineFirstStringNames", false);
    ValidateAnnotationNames(style.CallLineMessageNames, scratch, "CallLineMessageNames", false);
    ValidateAnnotationNames(style.ChainedCallRootNames, scratch, "ChainedCallRootNames", true);
    ValidateAnnotationNames(style.AlwaysBreakChainRootNames, scratch, "AlwaysBreakChainRootNames", true);
    ValidateAnnotationNames(style.AlwaysBreakChainCallNames, scratch, "AlwaysBreakChainCallNames", false);
    scratch.clear();
    for (const auto& pair : style.IndentedCallScopePairs) {
        const auto [begin, end] = AnnotationScopePair(pair);
        scratch.push_back(begin);
        scratch.push_back(end);
    }
    RejectDuplicateAnnotations(scratch, "IndentedCallScopePairs");
    scratch.clear();
    for (const auto& pair : style.IndentedChainScopePairs) {
        const auto [begin, end] = AnnotationScopePair(pair, "IndentedChainScopePairs");
        scratch.push_back(begin);
        scratch.push_back(end);
    }
    RejectDuplicateAnnotations(scratch, "IndentedChainScopePairs");
    scratch.clear();
    for (const auto& pair : style.StickyChainCallPairs) {
        (void)AnnotationScopePair(pair, "StickyChainCallPairs", false);
        scratch.push_back(pair);
    }
    RejectDuplicateAnnotations(scratch, "StickyChainCallPairs");
    scratch.clear();
    for (const auto& pair : style.ClassMacroNames) {
        const auto [begin, end] = AnnotationScopePair(pair, "ClassMacroNames");
        scratch.push_back(begin);
        scratch.push_back(end);
    }
    RejectDuplicateAnnotations(scratch, "ClassMacroNames");
}

LibraryAnnotations CompileLibraryAnnotations(const Style& style)
{
    ValidateLibraryAnnotationStyle(style);
    LibraryAnnotations result;
    result.MessagePins_.reserve(style.CallLineFirstArgumentNames.size() + style.CallLineFirstStringNames.size() +
        style.CallLineMessageNames.size());
    for (const auto& name : style.CallLineFirstArgumentNames) {
        result.MessagePins_
            .push_back({name, FirstArgumentAnnotation::PinAnyArgument, style.CallLineFirstArgumentOverflow});
    }
    for (const auto& name : style.CallLineFirstStringNames) {
        result.MessagePins_
            .push_back({name, FirstArgumentAnnotation::PinCompleteStringLiteral, style.CallLineFirstStringOverflow});
    }
    for (const auto& name : style.CallLineMessageNames) {
        result.MessagePins_.push_back({
            name,
            FirstArgumentAnnotation::PinCompleteStringLiteral,
            AttachedStringOverflow::KeepInline,
            true,
        });
    }
    std::sort(
        result.MessagePins_.begin(),
        result.MessagePins_.end(),
        [] (const auto& left, const auto& right) {
            if (left.Name != right.Name) {
                return left.Name < right.Name;
            }
            if (left.MessageLayout != right.MessageLayout) {
                return left.MessageLayout;
            }
            // Without message layout, the general argument rule wins overlaps.
            return left.FirstArgument == FirstArgumentAnnotation::PinAnyArgument &&
                right.FirstArgument != FirstArgumentAnnotation::PinAnyArgument;
        });
    result.MessagePins_.erase(
        std::unique(
            result.MessagePins_.begin(),
            result.MessagePins_.end(),
            [] (const auto& left, const auto& right) {
                return left.Name == right.Name;
            }),
        result.MessagePins_.end());
    result.ChainRoots_ = style.ChainedCallRootNames;
    std::sort(result.ChainRoots_.begin(), result.ChainRoots_.end());
    result.AlwaysBreakChainRoots_ = style.AlwaysBreakChainRootNames;
    std::sort(result.AlwaysBreakChainRoots_.begin(), result.AlwaysBreakChainRoots_.end());
    result.AlwaysBreakChainCalls_ = style.AlwaysBreakChainCallNames;
    std::sort(result.AlwaysBreakChainCalls_.begin(), result.AlwaysBreakChainCalls_.end());
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
            return result.VisualScopes_[left].End < result.VisualScopes_[right].End;
        });
    result.ChainScopes_ = CompileChainPairs(style.IndentedChainScopePairs, "IndentedChainScopePairs", true);
    result.StickyChainPairs_ = CompileChainPairs(style.StickyChainCallPairs, "StickyChainCallPairs", false);
    return result;
}

std::span<const ChainCallPairAnnotation> LibraryAnnotations::ChainScopes() const
{
    return ChainScopes_;
}

std::span<const ChainCallPairAnnotation> LibraryAnnotations::StickyChainPairs() const
{
    return StickyChainPairs_;
}

bool LibraryAnnotations::IsStickyChainPair(std::string_view first, std::string_view second) const noexcept
{
    const auto key = std::pair{first, second};
    const auto found = std::lower_bound(
        StickyChainPairs_.begin(),
        StickyChainPairs_.end(),
        key,
        [] (const auto& pair, const auto& value) {
            return std::pair<std::string_view, std::string_view>{pair.First, pair.Second} < value;
        });
    return found != StickyChainPairs_.end() && found->First == first && found->Second == second;
}

std::span<const MessagePinAnnotation> LibraryAnnotations::MessagePins() const
{
    return MessagePins_;
}

const MessagePinAnnotation* LibraryAnnotations::FindMessagePin(std::string_view name) const noexcept
{
    const auto found = std::lower_bound(
        MessagePins_.begin(),
        MessagePins_.end(),
        name,
        [] (const auto& value, auto key) {
            return value.Name < key;
        });
    return found != MessagePins_.end() && found->Name == name ? &*found : nullptr;
}

ChainParticipation LibraryAnnotations::Chains() const noexcept
{
    return ChainRoots_.empty() ? ChainParticipation::AllSafe : ChainParticipation::AnnotatedRoots;
}

bool LibraryAnnotations::IsAnnotatedChainRoot(std::string_view name) const noexcept
{
    return std::binary_search(ChainRoots_.begin(), ChainRoots_.end(), name);
}

std::span<const std::string> LibraryAnnotations::AlwaysBreakChainRoots() const
{
    return AlwaysBreakChainRoots_;
}

bool LibraryAnnotations::AlwaysBreakChainRoot(std::string_view name) const noexcept
{
    return std::binary_search(AlwaysBreakChainRoots_.begin(), AlwaysBreakChainRoots_.end(), name);
}

std::span<const std::string> LibraryAnnotations::AlwaysBreakChainCalls() const
{
    return AlwaysBreakChainCalls_;
}

bool LibraryAnnotations::AlwaysBreakChainCall(std::string_view name) const noexcept
{
    return std::binary_search(AlwaysBreakChainCalls_.begin(), AlwaysBreakChainCalls_.end(), name);
}

std::span<const std::string> LibraryAnnotations::ChainRoots() const
{
    return ChainRoots_;
}

std::span<const VisualScopeAnnotation> LibraryAnnotations::VisualScopes() const
{
    return VisualScopes_;
}

const VisualScopeAnnotation* LibraryAnnotations::FindScopeBegin(std::string_view name) const noexcept
{
    const auto found = std::lower_bound(
        VisualScopes_.begin(),
        VisualScopes_.end(),
        name,
        [] (const auto& value, auto key) {
            return value.Begin < key;
        });
    return found != VisualScopes_.end() && found->Begin == name ? &*found : nullptr;
}

const VisualScopeAnnotation* LibraryAnnotations::FindScopeEnd(std::string_view name) const noexcept
{
    const auto found = std::lower_bound(
        ScopeEndOrder_.begin(),
        ScopeEndOrder_.end(),
        name,
        [&] (auto index, auto key) {
            return VisualScopes_[index].End < key;
        });
    return found != ScopeEndOrder_.end() && VisualScopes_[*found].End == name ? &VisualScopes_[*found] : nullptr;
}

namespace {

std::filesystem::path ResolveConfigInclude(std::string_view name, const std::filesystem::path& path, size_t line)
{
    if (name.empty() || name.find('\0') != std::string_view::npos) {
        ThrowConfigError(path, line, "include expects a nonempty file path");
    }
    if (name.starts_with("@/")) {
        const std::filesystem::path relative(name.substr(2));
        if (relative.empty() || relative.has_root_path()) {
            ThrowConfigError(path, line, "expected a repository-relative include path");
        }
        auto directory = std::filesystem::canonical(std::filesystem::absolute(path).parent_path());
        while (true) {
            std::error_code error;
            for (const auto name : {".git", ".arc"}) {
                const auto marker = std::filesystem::status(directory / name, error);
                if (!error && (std::filesystem::is_directory(marker) || std::filesystem::is_regular_file(marker))) {
                    return directory / relative;
                }
                error.clear();
            }
            const auto parent = directory.parent_path();
            if (parent == directory) {
                ThrowConfigError(path, line, "cannot find repository root for include");
            }
            directory = parent;
        }
    }
    const std::filesystem::path included(name);
    return included.is_absolute() ? included : path.parent_path() / included;
}

size_t LoadConfigSettings(
    const std::filesystem::path& path,
    Configuration& config,
    std::span<SettingLocation> settingLines,
    std::vector<std::filesystem::path>& active)
{
    const auto canonical = std::filesystem::weakly_canonical(path);
    if (std::find(active.begin(), active.end(), canonical) != active.end()) {
        throw std::runtime_error("configuration include cycle: " + path.string());
    }
    if (active.size() >= 64) {
        throw std::runtime_error("configuration include depth exceeds 64: " + path.string());
    }
    std::ifstream input(path);
    if (!input.is_open()) {
        throw std::runtime_error("cannot open configuration file: " + path.string());
    }

    active.push_back(canonical);
    struct Setting
    {
        std::string Key;
        std::string Value;
        size_t Line;
    };
    std::vector<Setting> settings;
    std::optional<Setting> include;
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
        auto arrayDepth = value.front() == '[' ? ArrayDepth(value, 0, path, lineNumber) : 0;
        if (arrayDepth != 0) {
            // key and value refer to lineText, which the next getline replaces.
            arrayKey = key;
            arrayValue = value;
            while (true) {
                if (!std::getline(input, lineText)) {
                    if (!input.eof()) {
                        throw std::runtime_error("failed to read configuration file: " + path.string());
                    }
                    ThrowConfigError(path, settingLine, "unterminated array");
                }
                ++lineNumber;
                auto itemLine = Trim(StripComment(lineText));
                arrayValue.push_back('\n');
                arrayValue.append(itemLine);
                arrayDepth = ArrayDepth(itemLine, arrayDepth, path, lineNumber);
                if (arrayDepth == 0) {
                    break;
                }
            }
            key = arrayKey;
            value = arrayValue;
        }
        if (key == "include") {
            if (include) {
                ThrowConfigError(path, settingLine, "duplicate include setting");
            }
            include = Setting{std::string(key), ParseString(value, path, settingLine), settingLine};
        } else {
            settings.push_back({std::string(key), std::string(value), settingLine});
        }
    }
    if (!input.eof()) {
        throw std::runtime_error("failed to read configuration file: " + path.string());
    }
    input.close();
    if (include) {
        const auto included = ResolveConfigInclude(include->Value, path, include->Line);
        try {
            (void)LoadConfigSettings(included, config, settingLines, active);
        } catch (const std::runtime_error& error) {
            ThrowConfigError(path, include->Line, error.what());
        }
    }
    for (const auto& setting : settings) {
        ApplySetting(config, setting.Key, setting.Value, path, setting.Line, settingLines);
    }
    active.pop_back();
    return lineNumber;
}

} // namespace

Configuration LoadConfigFile(const std::filesystem::path& path)
{
    Configuration config;
    std::array<SettingLocation, Options.size()> settingLines{};
    std::vector<std::filesystem::path> active;
    const auto lineNumber = LoadConfigSettings(path, config, settingLines, active);
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
    std::string_view fileName,
    const std::function<void(const std::filesystem::path&, const std::error_code&)>& onError)
{
    std::error_code error;
    auto directory = startDirectory.empty()
        ? std::filesystem::current_path(error)
        : std::filesystem::absolute(startDirectory, error);
    if (error) {
        if (onError) {
            onError(startDirectory.empty() ? std::filesystem::path(".") : startDirectory, error);
        }
        return std::nullopt;
    }
    startDirectory = std::move(directory);

    while (!startDirectory.empty()) {
        auto candidate = startDirectory / fileName;
        if (std::filesystem::is_regular_file(candidate, error) && !error) {
            return candidate;
        }
        if (error && error != std::errc::no_such_file_or_directory && onError) {
            onError(candidate, error);
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
