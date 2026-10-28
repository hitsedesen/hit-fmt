#include "options.hpp"

#include <algorithm>
#include <charconv>
#include <functional>
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
    std::function<void(FlagValue&)> Apply;

    bool TryParse(std::string_view argument, std::span<const char* const>& remaining) const
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
        Apply(value);
        return true;
    }
};

class Flags
{
public:
    void Bool(std::string_view name, bool* target, bool value = true)
    {
        const auto apply = [target, value] (FlagValue&) {
            if (target) {
                *target = value;
            }
        };
        Registered_.push_back({name, false, apply});
    }

    template <typename T>
    void Value(std::string_view name, T* target, void (*parse)(T&, FlagValue&))
    {
        const auto apply = [target, parse] (FlagValue& value) {
            parse(*target, value);
        };
        Registered_.push_back({name, true, apply});
    }

    void Positionals(std::vector<std::filesystem::path>* target)
    {
        Positionals_ = target;
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
            if (positionalOnly || !argument.starts_with('-') || argument == "-") {
                if (!Positionals_) {
                    throw UsageError("unexpected argument: " + std::string(argument));
                }
                Positionals_->emplace_back(argument);
            } else if (argument == "--") {
                positionalOnly = true;
            } else if (!std::ranges::any_of(
                Registered_,
                [&] (const Flag& flag) {
                    return flag.TryParse(argument, args);
                }))
            {
                throw UsageError("unknown option: " + std::string(argument));
            }
        }
    }

private:
    std::vector<Flag> Registered_;
    std::vector<std::filesystem::path>* Positionals_ = nullptr;
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

void ParseGenerateConfig(std::optional<ConfigPreset>& target, FlagValue& value)
{
    const auto preset = value.TakeIf([] (std::string_view next) {
        return !next.starts_with('-');
    });
    target = preset ? ParseConfigPreset(*preset) : ConfigPreset::Default;
}

void ParseChangedHunks(std::optional<std::string>& hunkInput, FlagValue& value)
{
    constexpr auto error = "--changed-hunks requires one input file or -";
    if (hunkInput) {
        throw UsageError(error);
    }
    hunkInput = value.Require(error);
    if (hunkInput->empty()) {
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

void ParseThreads(size_t& threads, FlagValue& value)
{
    threads = ParseThreadCount(value.Require("--threads requires a positive integer"));
}

void ParseAutoThreads(size_t& threads, FlagValue& value)
{
    const auto count = value.TakeIf([] (std::string_view next) {
        // A nonnumeric argument after -j remains a path or another option.
        return !next.empty() && std::ranges::all_of(
            next,
            [] (char c) {
                return c >= '0' && c <= '9';
            });
    });
    threads = count ? ParseThreadCount(*count) : std::max(1u, std::thread::hardware_concurrency());
}

void ParseConfigPath(std::optional<std::filesystem::path>& path, FlagValue& value)
{
    path = value.Require("--config requires a file path");
}

void ParseExclusion(std::vector<std::regex>& exclusions, FlagValue& value)
{
    const std::string pattern(value.Require("--exclude requires a regex"));
    if (pattern.empty()) {
        throw UsageError("--exclude requires a regex");
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
    flags.Value("--generate-config", &options.GenerateConfig, ParseGenerateConfig);
    flags.Value("--changed-hunks", &options.HunkInput, ParseChangedHunks);
    flags.Value("--changed-ranges", &options.HunkInput, ParseChangedHunks);
    flags.Value("-j", &options.Threads, ParseAutoThreads);
    flags.Value("--threads", &options.Threads, ParseThreads);
    flags.Value("--config", &options.ConfigPath, ParseConfigPath);
    flags.Value("--exclude", &options.Exclusions, ParseExclusion);
    flags.Positionals(&options.Paths);
    flags.Parse(args);
    ValidateOptions(options);
    return options;
}

} // namespace hit_fmt
