#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <regex>
#include <string>
#include <vector>

namespace hit_fmt {

struct Options
{
    bool Fix = false;
    bool Spillover = true;
    bool SummaryOnly = false;
    bool DisableConfig = false;
    bool ShowHelp = false;
    bool ShowVersion = false;
    bool ShowConfigHelp = false;
    bool GenerateConfig = false;
    size_t Threads = 1;
    std::optional<std::filesystem::path> ConfigPath;
    std::optional<std::string> HunkInput;
    std::vector<std::regex> Exclusions;
    std::vector<std::filesystem::path> Paths;
};

// Parse arguments only; do not discover files, read stdin, or run the CLI.
[[nodiscard]] Options ParseOptions(int argc, const char* const argv[]);

} // namespace hit_fmt
