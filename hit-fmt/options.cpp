#include "options.hpp"

#include <algorithm>
#include <charconv>
#include <string_view>
#include <thread>

namespace hit_fmt {

namespace {

struct FlagValue
{
    std::optional<std::string_view> Attached;
    std::span<const char* const>& Remaining;

    std::string_view Require(std::string_view error)
    {
        if (Attached) {
            return *Attached;
        }
        if (Remaining.empty()) {
            throw UsageError(std::string(error));
        }
        const std::string_view value(Remaining.front());
        Remaining = Remaining.subspan(1);
        return value;
    }

    template <typename Predicate>
    std::optional<std::string_view> TakeIf(Predicate accept)
    {
        if (Attached) {
            return Attached;
        }
        if (Remaining.empty() || !accept(std::string_view(Remaining.front()))) {
            return std::nullopt;
        }
        const std::string_view value(Remaining.front());
        Remaining = Remaining.subspan(1);
        return value;
    }
};

struct Flag
{
    std::string_view Name;
    bool TakesValue;
    void (*Apply)(Options&, FlagValue&);

    bool TryParse(std::string_view argument, std::span<const char* const>& remaining, Options& options) const
    {
        std::optional<std::string_view> attached;
        if (argument != Name) {
            if (!TakesValue || !argument.starts_with(Name)) {
                return false;
            }
            auto suffix = argument.substr(Name.size());
            if (Name.starts_with("--")) {
                if (!suffix.starts_with('=')) {
                    return false;
                }
                suffix.remove_prefix(1);
            }
            attached = suffix;
        }
        FlagValue value{attached, remaining};
        Apply(options, value);
        return true;
    }
};

template <bool Options::* Field, bool Value = true>
constexpr Flag BoolFlag(std::string_view name)
{
    return {
        name,
        false,
        [] (Options& options, FlagValue&) {
            options.*Field = Value;
        },
    };
}

constexpr Flag ValueFlag(std::string_view name, void (*apply)(Options&, FlagValue&))
{
    return {name, true, apply};
}

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

void ParseGenerateConfig(Options& options, FlagValue& value)
{
    const auto preset = value.TakeIf([] (std::string_view next) {
        return !next.starts_with('-');
    });
    options.GenerateConfig = preset ? ParseConfigPreset(*preset) : ConfigPreset::Default;
}

void ParseChangedHunks(Options& options, FlagValue& value)
{
    constexpr auto error = "--changed-hunks requires one input file or -";
    if (options.HunkInput) {
        throw UsageError(error);
    }
    options.HunkInput = value.Require(error);
    if (options.HunkInput->empty()) {
        throw UsageError(error);
    }
}

size_t ParseThreadCount(std::string_view value)
{
    size_t threads = 0;
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), threads);
    if (error != std::errc{} || end != value.data() + value.size() || threads == 0) {
        throw UsageError("--threads requires a positive integer");
    }
    return threads;
}

void ParseThreads(Options& options, FlagValue& value)
{
    options.Threads = ParseThreadCount(value.Require("--threads requires a positive integer"));
}

void ParseAutoThreads(Options& options, FlagValue& value)
{
    const auto count = value.TakeIf([] (std::string_view next) {
        // A nonnumeric argument after -j remains a path or another option.
        return !next.empty() && std::ranges::all_of(
            next,
            [] (char c) {
                return c >= '0' && c <= '9';
            });
    });
    options.Threads = count ? ParseThreadCount(*count) : std::max(1u, std::thread::hardware_concurrency());
}

void ParseConfigPath(Options& options, FlagValue& value)
{
    options.ConfigPath = value.Require("--config requires a file path");
}

void ParseExclusion(Options& options, FlagValue& value)
{
    const std::string pattern(value.Require("--exclude requires a regex"));
    if (pattern.empty()) {
        throw UsageError("--exclude requires a regex");
    }
    try {
        options.Exclusions.emplace_back(pattern);
    }
    catch (const std::regex_error& error) {
        throw UsageError("invalid --exclude regex: " + pattern + ": " + error.what());
    }
}

constexpr Flag Flags[] = {
    BoolFlag<&Options::ShowHelp>("-h"),
    BoolFlag<&Options::ShowHelp>("--help"),
    BoolFlag<&Options::ShowVersion>("-V"),
    BoolFlag<&Options::ShowVersion>("--version"),
    BoolFlag<&Options::ShowConfigHelp>("--help-config"),
    BoolFlag<&Options::Fix>("--fix"),
    // Check mode is the default, so this flag needs no state change.
    {"--check", false, [] (Options&, FlagValue&) {}},
    BoolFlag<&Options::Spillover, false>("--no-spillover"),
    BoolFlag<&Options::SummaryOnly>("--summary"),
    BoolFlag<&Options::DisableConfig>("--no-config"),
    ValueFlag("--generate-config", ParseGenerateConfig),
    ValueFlag("--changed-hunks", ParseChangedHunks),
    ValueFlag("--changed-ranges", ParseChangedHunks),
    ValueFlag("-j", ParseAutoThreads),
    ValueFlag("--threads", ParseThreads),
    ValueFlag("--config", ParseConfigPath),
    ValueFlag("--exclude", ParseExclusion),
};

void ValidateOptions(const Options& options)
{
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
}

} // namespace

Options ParseOptions(std::span<const char* const> args)
{
    Options options;
    if (!args.empty()) {
        args = args.subspan(1);
    }
    bool positionalOnly = false;
    while (!args.empty()) {
        const std::string_view argument(args.front());
        args = args.subspan(1);
        if (positionalOnly || !argument.starts_with('-') || argument == "-") {
            options.Paths.emplace_back(argument);
        } else if (argument == "--") {
            positionalOnly = true;
        } else if (!std::ranges::any_of(
            Flags,
            [&] (const Flag& flag) {
                return flag.TryParse(argument, args, options);
            }))
        {
            throw UsageError("unknown option: " + std::string(argument));
        }
    }
    ValidateOptions(options);
    return options;
}

} // namespace hit_fmt
