#include "options.hpp"

#include <algorithm>
#include <charconv>
#include <functional>
#include <string_view>
#include <thread>
#include <utility>

namespace hit_fmt {

namespace {

enum class ValueMode
{
    None,
    Required,
    Optional,
};

bool IsFlag(std::string_view argument)
{
    return argument.starts_with('-') && argument != "-";
}

struct Flag
{
    std::string_view Name;
    ValueMode Mode;
    std::string_view MissingValueError;
    std::function<void(std::optional<std::string_view>)> Apply;

    bool Matches(std::string_view argument) const
    {
        if (argument == Name) {
            return true;
        }
        if (Mode == ValueMode::None || !argument.starts_with(Name)) {
            return false;
        }
        return !Name.starts_with("--") || argument.substr(Name.size()).starts_with('=');
    }
};

class Flags
{
public:
    void Bool(std::string_view name, bool* target, bool value = true)
    {
        const auto apply = [target, value] (std::optional<std::string_view>) {
            if (target) {
                *target = value;
            }
        };
        Registered_.push_back({name, ValueMode::None, {}, apply});
    }

    template <typename T>
    void Value(
        std::string_view name,
        T* target,
        void (*parse)(T&, std::string_view),
        std::string_view missingValueError)
    {
        const auto apply = [target, parse] (std::optional<std::string_view> value) {
            parse(*target, *value);
        };
        Registered_.push_back({name, ValueMode::Required, missingValueError, apply});
    }

    template <typename T>
    void OptionalValue(std::string_view name, T* target, void (*parse)(T&, std::optional<std::string_view>))
    {
        const auto apply = [target, parse] (std::optional<std::string_view> value) {
            parse(*target, value);
        };
        Registered_.push_back({name, ValueMode::Optional, {}, apply});
    }

    void Positionals(std::function<void(std::string_view)> apply)
    {
        Positionals_ = std::move(apply);
    }

    void Parse(std::span<const char* const> args) const
    {
        if (!args.empty()) {
            args = args.subspan(1);
        }
        bool positionalOnly = false;
        while (!args.empty()) {
            const std::string_view argument(args.front());
            args = args.subspan(1);
            if (positionalOnly || !IsFlag(argument)) {
                if (!Positionals_) {
                    throw UsageError("unexpected argument: " + std::string(argument));
                }
                Positionals_(argument);
                continue;
            } else if (argument == "--") {
                positionalOnly = true;
                continue;
            }
            const auto flag = std::ranges::find_if(
                Registered_,
                [&] (const Flag& candidate) {
                    return candidate.Matches(argument);
                });
            if (flag == Registered_.end()) {
                throw UsageError("unknown option: " + std::string(argument));
            }
            std::optional<std::string_view> value;
            if (argument != flag->Name) {
                value = argument.substr(flag->Name.size() + (flag->Name.starts_with("--") ? 1 : 0));
            } else if (flag->Mode != ValueMode::None && !args.empty() &&
                (flag->Mode == ValueMode::Required || !IsFlag(args.front())))
            {
                value = args.front();
                args = args.subspan(1);
            } else if (flag->Mode == ValueMode::Required) {
                throw UsageError(std::string(flag->MissingValueError));
            }
            flag->Apply(value);
        }
    }

private:
    std::vector<Flag> Registered_;
    std::function<void(std::string_view)> Positionals_;
};

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
    flags.Bool("--no-config", &options.DisableConfig);
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
