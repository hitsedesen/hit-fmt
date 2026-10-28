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

} // namespace

Options ParseOptions(int argc, const char* const argv[])
{
    Options options;
    bool positionalOnly = false;
    for (int index = 1; index < argc; ++index) {
        std::string_view argument(argv[index]);
        if (positionalOnly || !argument.starts_with('-') || argument == "-") {
            options.Paths.emplace_back(argument);
        } else if (argument == "--") {
            positionalOnly = true;
        } else if (argument == "-h" || argument == "--help") {
            options.ShowHelp = true;
        } else if (argument == "-V" || argument == "--version") {
            options.ShowVersion = true;
        } else if (argument == "--help-config") {
            options.ShowConfigHelp = true;
        } else if (argument == "--generate-config" || argument.starts_with("--generate-config=")) {
            options.GenerateConfig = ConfigPreset::Default;
            if (argument != "--generate-config") {
                options.GenerateConfig = ParseConfigPreset(argument.substr(18));
            } else if (index + 1 < argc && !std::string_view(argv[index + 1]).starts_with('-')) {
                options.GenerateConfig = ParseConfigPreset(argv[++index]);
            }
        } else if (argument == "--fix") {
            options.Fix = true;
        } else if (argument == "--check") {
            // Check mode is the default, so this flag needs no state change.
        } else if (argument == "--no-spillover") {
            options.Spillover = false;
        } else if (argument == "--summary") {
            options.SummaryOnly = true;
        } else if (argument == "--no-config") {
            options.DisableConfig = true;
        } else if (argument == "--changed-hunks" || argument.starts_with("--changed-hunks=") ||
            argument == "--changed-ranges" || argument.starts_with("--changed-ranges="))
        {
            if (options.HunkInput) {
                throw UsageError("--changed-hunks requires one input file or -");
            }
            const auto equal = argument.find('=');
            if (equal == argument.npos) {
                if (++index == argc) {
                    throw UsageError("--changed-hunks requires one input file or -");
                }
                options.HunkInput = argv[index];
            } else {
                options.HunkInput = argument.substr(equal + 1);
            }
            if (options.HunkInput->empty()) {
                throw UsageError("--changed-hunks requires one input file or -");
            }
        } else if (argument == "-j" || argument == "--threads" || argument.starts_with("--threads=") ||
            (argument.starts_with("-j") && argument.size() > 2))
        {
            std::string_view value;
            if (argument == "-j") {
                // Only consume the next token if it is a thread count;
                // otherwise it remains an input path or another option.
                if (index + 1 < argc) {
                    std::string_view next(argv[index + 1]);
                    if (!next.empty() && std::ranges::all_of(
                        next,
                        [] (char c) {
                            return c >= '0' && c <= '9';
                        }))
                    {
                        value = argv[++index];
                    }
                }
                if (value.empty()) {
                    options.Threads = std::max(1u, std::thread::hardware_concurrency());
                    continue;
                }
            } else if (argument == "--threads") {
                if (++index == argc) {
                    throw UsageError("--threads requires a positive integer");
                }
                value = argv[index];
            } else {
                value = argument.substr(argument.starts_with("-j") ? 2 : 10);
            }
            size_t threads = 0;
            auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), threads);
            if (error != std::errc{} || end != value.data() + value.size() || threads == 0) {
                throw UsageError("--threads requires a positive integer");
            }
            options.Threads = threads;
        } else if (argument == "--config") {
            if (++index == argc) {
                throw UsageError("--config requires a file path");
            }
            options.ConfigPath = argv[index];
        } else if (argument.starts_with("--config=")) {
            options.ConfigPath = std::string(argument.substr(9));
        } else if (argument == "--exclude" || argument.starts_with("--exclude=")) {
            std::string pattern;
            if (argument == "--exclude") {
                if (++index == argc) {
                    throw UsageError("--exclude requires a regex");
                }
                pattern = argv[index];
            } else {
                pattern = argument.substr(10);
            }
            if (pattern.empty()) {
                throw UsageError("--exclude requires a regex");
            }
            try {
                options.Exclusions.emplace_back(pattern);
            } catch (const std::regex_error& error) {
                throw UsageError("invalid --exclude regex: " + pattern + ": " + error.what());
            }
        } else {
            throw UsageError("unknown option: " + std::string(argument));
        }
    }
    if (options.DisableConfig && options.ConfigPath) {
        throw UsageError("--config and --no-config cannot be combined");
    }
    if (options.HunkInput && !options.Paths.empty()) {
        throw UsageError("--changed-hunks cannot be combined with input paths");
    }
    if (!options.Spillover && !options.HunkInput) {
        throw UsageError("--no-spillover requires --changed-hunks");
    }
    if (options.GenerateConfig && !options.Paths.empty()) {
        throw UsageError("--generate-config cannot be combined with input paths");
    }
    return options;
}

} // namespace hit_fmt
