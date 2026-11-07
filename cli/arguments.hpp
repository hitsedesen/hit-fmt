#pragma once

#include "config_template.hpp"
#include "path_rules.hpp"

#include <util/types.hpp>

#include <fs/fs.hpp>

#include <iosfwd>
#include <memory_resource>
#include <optional>
#include <regex>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace cli {

enum class ErrorCode
{
    None,
    Message,
    UnexpectedArgument,
    UnknownOption,
    UnknownConfigPreset,
};

struct ArgumentError
{
    ErrorCode Code = ErrorCode::None;
    // Borrows argument text or a message literal until reporting.
    std::string_view Argument;

    explicit operator bool() const noexcept
    {
        return Code != ErrorCode::None;
    }
};

std::ostream& operator<<(std::ostream& output, const ArgumentError& error);

struct Arguments
{
    bool Fix = false;

    bool SummaryOnly = false;

    bool ShowHelp = false;
    bool ShowVersion = false;
    bool ShowConfigHelp = false;
    std::optional<ConfigPreset> GenerateConfig;

    int Threads = 1;

    bool Spillover = true;
    std::optional<std::pmr::string> HunkInput;

    std::optional<fs::Path> ConfigPath;
    std::optional<cli::PathRegex> Exclusions;
    std::vector<fs::Path> Paths;

    bool Debug = false;
};

Result<Arguments> ParseArguments(
    std::span<const char* const> argv,
    std::pmr::memory_resource* memory = std::pmr::get_default_resource());

} // namespace cli
