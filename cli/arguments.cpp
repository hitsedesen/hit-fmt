#include "arguments.hpp"

#include "flags.hpp"

#include <util/memory.hpp>
#include <util/parse.hpp>
#include <util/types.hpp>

#include <algorithm>
#include <ostream>
#include <string_view>
#include <thread>

namespace cli {

namespace {

constexpr auto ChangedHunkError = "--changed-hunks requires one input file or -";
constexpr auto ThreadsError = "--threads requires a positive integer";
constexpr auto ExclusionError = "--exclude requires a regex";

ArgumentError ParseGenerateConfig(std::optional<ConfigPreset>& target, std::optional<std::string_view> preset)
{
    if (!preset) {
        target = ConfigPreset::Default;
    } else if (*preset == "ytsaurus") {
        target = ConfigPreset::YTsaurus;
    } else if (*preset == "google") {
        target = ConfigPreset::Google;
    } else if (*preset == "llvm") {
        target = ConfigPreset::LLVM;
    } else {
        return {.Code = ErrorCode::UnknownConfigPreset, .Argument = *preset};
    }

    return {};
}

ArgumentError ParseChangedHunks(std::optional<std::string_view>& hunkInput, std::string_view value)
{
    if (hunkInput || value.empty()) {
        return {.Code = ErrorCode::Message, .Argument = ChangedHunkError};
    }

    hunkInput = value;
    return {};
}

ArgumentError ParseThreads(int& threads, std::string_view value)
{
    auto [count, success] = TryParseInteger<int>(value);

    if (!success || count <= 0) {
        return {.Code = ErrorCode::Message, .Argument = ThreadsError};
    }

    threads = count;
    return {};
}

ArgumentError ParseAutoThreads(int& threads, std::optional<std::string_view> count)
{
    if (count) {
        return ParseThreads(threads, *count);
    }

    threads = static_cast<int>(std::max(1u, std::thread::hardware_concurrency()));
    return {};
}

ArgumentError ParseConfigPath(std::optional<std::string_view>& path, std::string_view value)
{
    path = value;
    return {};
}

ArgumentError ParseExclusion(Vector<std::string_view>& exclusions, std::string_view value)
{
    if (value.empty()) {
        return {.Code = ErrorCode::Message, .Argument = ExclusionError};
    }

    exclusions.emplace_back(value);
    return {};
}

ArgumentError ValidateArguments(const Arguments& arguments)
{
    if (arguments.HunkInput && !arguments.Paths.empty()) {
        return {.Code = ErrorCode::Message, .Argument = "--changed-hunks cannot be combined with input paths"};
    }

    if (!arguments.Spillover && !arguments.HunkInput) {
        return {.Code = ErrorCode::Message, .Argument = "--no-spillover requires --changed-hunks"};
    }

    if (arguments.GenerateConfig && !arguments.Paths.empty()) {
        return {.Code = ErrorCode::Message, .Argument = "--generate-config cannot be combined with input paths"};
    }

    return {};
}

} // namespace

std::ostream& operator<<(std::ostream& output, const ArgumentError& error)
{
    switch (error.Code) {
        case ErrorCode::None:
            break;
        case ErrorCode::Message:
            output << error.Argument;
            break;
        case ErrorCode::UnexpectedArgument:
            output << "unexpected argument: " << error.Argument;
            break;
        case ErrorCode::UnknownOption:
            output << "unknown option: " << error.Argument;
            break;
        case ErrorCode::UnknownConfigPreset:
            output << "unknown config preset: " << error.Argument << " (expected ytsaurus, google or llvm)";
            break;
    }

    return output;
}

Result<Arguments> ParseArguments(
    std::span<const char* const> argv,
    std::pmr::memory_resource* memory,
    bool outputIsTerminal)
{
    Arena scratch;
    Arguments arguments{.SummaryOnly = !outputIsTerminal};
    Flags flags;
    Vector<std::string_view> exclusions(&scratch);
    std::optional<std::string_view> hunkInput;
    std::optional<std::string_view> configPath;

    flags.Bool("--fix", &arguments.Fix);
    flags.Bool("--check", nullptr);

    flags.Bool("--summary", &arguments.SummaryOnly);
    flags.Bool("--details", &arguments.SummaryOnly, false);

    flags.Bool("-h", &arguments.ShowHelp);
    flags.Bool("--help", &arguments.ShowHelp);

    flags.Bool("-V", &arguments.ShowVersion);
    flags.Bool("--version", &arguments.ShowVersion);

    flags.Bool("--help-config", &arguments.ShowConfigHelp);

    flags.Bool("--no-spillover", &arguments.Spillover, false);
    flags.Bool("--debug", &arguments.Debug);

    flags.Optional("--generate-config", &arguments.GenerateConfig, ParseGenerateConfig);

    flags.Required("--changed-hunks", &hunkInput, ParseChangedHunks, ChangedHunkError);
    flags.Required("--changed-ranges", &hunkInput, ParseChangedHunks, ChangedHunkError);

    flags.Optional("-j", &arguments.Threads, ParseAutoThreads);
    flags.Required("--threads", &arguments.Threads, ParseThreads, ThreadsError);
    flags.Required("--config", &configPath, ParseConfigPath, "--config requires a file path");
    flags.Required("--exclude", &exclusions, ParseExclusion, ExclusionError);
    flags.Positionals([&arguments] (std::string_view argument) {
        arguments.Paths.emplace_back(argument);
    });

    if (auto error = flags.Parse(argv); error) {
        return {{}, std::move(error)};
    }

    if (configPath) {
        arguments.ConfigPath.emplace(*configPath, memory ? memory : std::pmr::get_default_resource());
    }

    if (hunkInput) {
        arguments.HunkInput.emplace(*hunkInput, memory ? memory : std::pmr::get_default_resource());
    }

    if (auto [pathRules, error] = CompilePathRules(exclusions, &scratch); error) {
        return {
            {},
            InvalidExclusionError(std::move(error), exclusions.size() == 1 ? exclusions.front() : std::string_view{}),
        };
    } else {
        arguments.Exclusions = std::move(pathRules);
    }

    if (auto error = ValidateArguments(arguments); error) {
        return {{}, std::move(error)};
    }

    return {std::move(arguments), {}};
}

} // namespace cli
