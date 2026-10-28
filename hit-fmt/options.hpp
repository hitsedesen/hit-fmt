#pragma once

#include "config_template.hpp"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <regex>
#include <stdexcept>
#include <string>
#include <vector>

namespace hit_fmt {

class UsageError
    : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

struct Options
{
    bool Fix = false;
    bool Spillover = true;
    bool SummaryOnly = false;
    bool DisableConfig = false;
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

[[nodiscard]] Options ParseOptions(int argc, const char* const argv[]);

} // namespace hit_fmt
