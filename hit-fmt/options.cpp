#include "options.hpp"

#include <algorithm>
#include <charconv>
#include <string_view>
#include <thread>

namespace hit_fmt {

namespace {

ConfigPreset ParseConfigPreset(std::string_view name)
{
    if (name == "ytsaurus") {
        return ConfigPreset::YTsaurus;
    }

    if (name == "google") {
        return ConfigPreset::Google;
    }

    if (name == "llvm") {
        return ConfigPreset::LLVM;
    }

    throw UsageError("unknown config preset: " + std::string(name) + " (expected ytsaurus, google or llvm)");
}

constexpr auto ChangedHunkError = "--changed-hunks requires one input file or -";
constexpr auto ThreadsError = "--threads requires a positive integer";
constexpr auto ExclusionError = "--exclude requires a regex";

void ParseGenerateConfig(std::optional<ConfigPreset>& target, std::optional<std::string_view> preset)
{
    target = preset ? ParseConfigPreset(*preset) : ConfigPreset::Default;
}

void ParseChangedHunks(std::optional<std::string>& hunkInput, std::string_view value)
{
    if (hunkInput || value.empty()) {
        throw UsageError(ChangedHunkError);
    }

    hunkInput = value;
}

size_t ParseThreadCount(std::string_view value)
{
    size_t threads = 0;
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), threads);

    if (error != std::errc{} || end != value.data() + value.size() || threads == 0) {
        throw UsageError(ThreadsError);
    }

    return threads;
}

void ParseThreads(size_t& threads, std::string_view value)
{
    threads = ParseThreadCount(value);
}

void ParseAutoThreads(size_t& threads, std::optional<std::string_view> count)
{
    threads = count ? ParseThreadCount(*count) : std::max(1u, std::thread::hardware_concurrency());
}

void ParseConfigPath(std::optional<std::filesystem::path>& path, std::string_view value)
{
    path = value;
}

void ParseExclusion(std::vector<std::regex>& exclusions, std::string_view value)
{
    const std::string pattern(value);

    if (pattern.empty()) {
        throw UsageError(ExclusionError);
    }

    try {
        exclusions.emplace_back(pattern);
    }
    catch (const std::regex_error& error) {
        throw UsageError("invalid --exclude regex: " + pattern + ": " + error.what());
    }
}

void ValidateOptions(const Options& options)
{
    if (options.HunkInput && !options.Paths.empty()) {
        throw UsageError("--changed-hunks cannot be combined with input paths");
    }

    if (!options.Spillover && !options.HunkInput) {
        throw UsageError("--no-spillover requires --changed-hunks");
    }

    if (options.GenerateConfig && !options.Paths.empty()) {
        throw UsageError("--generate-config cannot be combined with input paths");
    }
}

} // namespace

Options ParseOptions(std::span<const char* const> args)
{
    Options options;
    Flags flags;
    flags.Bool("-h", &options.ShowHelp);
    flags.Bool("--help", &options.ShowHelp);
    flags.Bool("-V", &options.ShowVersion);
    flags.Bool("--version", &options.ShowVersion);
    flags.Bool("--help-config", &options.ShowConfigHelp);
    flags.Bool("--fix", &options.Fix);
    // Check mode is the default, so this flag needs no state change.
    flags.Bool("--check", nullptr);
    flags.Bool("--no-spillover", &options.Spillover, false);
    flags.Bool("--summary", &options.SummaryOnly);
    flags.OptionalValue("--generate-config", &options.GenerateConfig, ParseGenerateConfig);
    flags.Value("--changed-hunks", &options.HunkInput, ParseChangedHunks, ChangedHunkError);
    flags.Value("--changed-ranges", &options.HunkInput, ParseChangedHunks, ChangedHunkError);
    flags.OptionalValue("-j", &options.Threads, ParseAutoThreads);
    flags.Value("--threads", &options.Threads, ParseThreads, ThreadsError);
    flags.Value("--config", &options.ConfigPath, ParseConfigPath, "--config requires a file path");
    flags.Value("--exclude", &options.Exclusions, ParseExclusion, ExclusionError);
    flags.Positionals([&options] (std::string_view argument) {
        options.Paths.emplace_back(argument);
    });
    flags.Parse(args);
    ValidateOptions(options);
    return options;
}

} // namespace hit_fmt
