#pragma once

#include "config_template.hpp"
#include "flags.hpp"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <regex>
#include <span>
#include <string>
#include <vector>

namespace hit_fmt {

struct Options
{
    bool Fix = false;
    bool Spillover = true;
    bool SummaryOnly = false;
    bool Debug = false;
    bool ShowHelp = false;
    bool ShowVersion = false;
    bool ShowConfigHelp = false;
    std::optional<ConfigPreset> GenerateConfig;
    size_t Threads = 1;
    std::optional<std::filesystem::path> ConfigPath;
    std::optional<std::string> HunkInput;
    std::vector<std::regex> Exclusions;
    std::vector<std::filesystem::path> Paths;
};

[[nodiscard]] Options ParseOptions(std::span<const char* const> args);

} // namespace hit_fmt
