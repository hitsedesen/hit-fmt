#pragma once

#include "error.hpp"

#include <util/types.hpp>

#include <fs/fs.hpp>

#include <memory_resource>
#include <optional>
#include <regex>
#include <string>
#include <string_view>

namespace cli {

using PathRegex = std::basic_regex<fs::Path::value_type>;

template <typename Patterns>
Result<std::optional<PathRegex>> CompilePathRules(const Patterns& patterns, std::pmr::memory_resource* scratch)
{
    if (patterns.empty()) {
        return {};
    }

    fs::Path::string_type combined(scratch);

    for (const auto& pattern : patterns) {
        bool inClass = false;
        us classStart = 0;
        us depth = 0;

        for (us i = 0; i < pattern.size(); ++i) {
            if (pattern[i] == '\\') {
                if (++i == pattern.size()) {
                    return {{}, RegexError(std::regex_constants::error_escape)};
                }

                if (!inClass && pattern[i] >= '1' && pattern[i] <= '9') {
                    return {{}, RegexError(std::regex_constants::error_backref)};
                }
            } else if (!inClass && pattern[i] == '[') {
                inClass = true;
                classStart = i + 1;

                if (classStart < pattern.size() && pattern[classStart] == '^') {
                    ++classStart;
                }
            } else if (inClass && pattern[i] == '[' && i + 1 < pattern.size() &&
                (pattern[i + 1] == ':' || pattern[i + 1] == '.' || pattern[i + 1] == '='))
            {
                auto end = pattern.find(pattern[i + 1], i + 2);

                if (end != std::string::npos && end + 1 < pattern.size() && pattern[end + 1] == ']') {
                    i = end + 1;
                }
            } else if (inClass && pattern[i] == ']' && i > classStart) {
                inClass = false;
            } else if (!inClass && pattern[i] == '(') {
                ++depth;
            } else if (!inClass && pattern[i] == ')') {
                if (depth == 0) {
                    return {{}, RegexError(std::regex_constants::error_paren)};
                }

                --depth;
            }
        }

        if (inClass) {
            return {{}, RegexError(std::regex_constants::error_brack)};
        }

        if (depth != 0) {
            return {{}, RegexError(std::regex_constants::error_paren)};
        }

        fs::Path path(std::string_view(pattern), scratch);
        const auto& native = path.native();
        fs::Path::string_type normalized(scratch);

        for (us i = 0; i < native.size(); ++i) {
            auto ch = native[i];

            if constexpr (fs::Path::preferred_separator == '\\') {
                if (ch == '\\' && i + 1 < native.size() && native[i + 1] == '/') {
                    ++i;
                    normalized += {'\\', '\\'};
                    continue;
                }

                if (ch == '/') {
                    normalized += {'\\', '\\'};
                    continue;
                }

                if (ch == '\\' && i + 1 < native.size()) {
                    normalized += ch;
                    normalized += native[++i];
                    continue;
                }
            }

            normalized += ch;
        }

        if (!combined.empty()) {
            combined += '|';
        }

        combined += '(';
        combined += normalized;
        combined += ')';
    }

    try {
        return {
            PathRegex(
                combined,
                std::regex_constants::extended | std::regex_constants::nosubs | std::regex_constants::optimize),
            {},
        };
    } catch (const std::regex_error&) {
        return {{}, ExceptionError()};
    }
}

} // namespace cli
