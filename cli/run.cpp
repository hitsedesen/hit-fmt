#include "run.hpp"

#include "arguments.hpp"
#include "config_help.hpp"
#include "config_template.hpp"
#include "file_check.hpp"
#include "file_discovery.hpp"
#include "file_dispatch.hpp"
#include "terminal.hpp"
#include "version.hpp"
#include "worker_memory.hpp"

#include <util/defer.hpp>
#include <util/file.hpp>
#include <util/memory.hpp>
#include <util/types.hpp>

#include <fs/fs.hpp>

#include <algorithm>
#include <exception>
#include <iostream>
#include <list>
#include <memory_resource>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>
#include <syncstream>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace cli {

namespace {

FileFailure SystemError(const fs::Error& error)
{
    return SystemFailure{error.Operation, error.Code};
}

constexpr us MaxSourceFileSize = 4 * 1024 * 1024;

Result<bool, FileFailure> IsExcluded(fs::PathView relativePath, const std::optional<cli::PathRegex>& exclusions)
{
    try {
        return {exclusions && std::regex_search(relativePath.begin(), relativePath.end(), *exclusions), {}};
    } catch (const std::regex_error&) {
        return {false, FileError(relativePath, ExceptionError())};
    }
}

class StyleResolver
{
public:
    FileFailure Initialize(const Arguments& arguments)
    {
        Scratch_.release();

        if (!arguments.ConfigPath) {
            return {};
        }

        auto absolute = fs::Absolute(*arguments.ConfigPath, Scratch_);

        if (absolute.Error.Code) {
            return FileError(*arguments.ConfigPath, SystemError(absolute.Error));
        }

        auto settings = LoadPreparedConfig(*arguments.ConfigPath);

        if (settings.Error) {
            return std::move(settings.Error);
        }

        ExplicitConfig_ = ConfigEntry{
            .Directory = absolute.Value.parent_path(&Scratch_).lexically_normal(&ConfigArena_),
            .Settings = settings.Value,
        };
        return {};
    }

    Result<bool, FileFailure> IsExcluded(const fs::Path& path, const cli::PreparedConfiguration*& settings)
    {
        Scratch_.release();
        auto config = FindConfig(path);

        if (config.Error) {
            return {false, std::move(config.Error)};
        }

        settings = config.Value->Settings;

        if (!settings->Exclusions) {
            return {false, {}};
        }

        auto absolute = fs::Absolute(path, Scratch_);

        if (absolute.Error.Code) {
            return {false, FileError(path, SystemError(absolute.Error))};
        }

        auto relative = absolute.Value
            .lexically_normal(&Scratch_)
            .lexically_relative(config.Value->Directory, &Scratch_);
        return cli::IsExcluded(relative, settings->Exclusions);
    }

private:
    struct ConfigEntry
    {
        fs::Path Directory;
        const cli::PreparedConfiguration* Settings = nullptr;
    };

    Result<const cli::PreparedConfiguration*, FileFailure> LoadPreparedConfig(const fs::Path& path)
    {
        formatter::Configuration settings;
        try {
            settings = formatter::LoadConfigFile(path);
        } catch (const std::exception&) {
            return {nullptr, FileError(path, ExceptionError())};
        }

        auto prepared = PrepareConfiguration(std::move(settings));

        if (prepared.Error) {
            return {nullptr, FileError(path, std::move(prepared.Error))};
        }

        PreparedConfigs_.push_back(std::move(prepared.Value));
        return {&PreparedConfigs_.back(), {}};
    }

    const ConfigEntry& DefaultConfig()
    {
        if (!DefaultConfig_.Settings) {
            PreparedConfigs_.emplace_back();
            DefaultConfig_.Settings = &PreparedConfigs_.back();
        }

        return DefaultConfig_;
    }

    Result<const ConfigEntry*, FileFailure> FindConfig(const fs::Path& sourcePath)
    {
        if (ExplicitConfig_) {
            return {&*ExplicitConfig_, {}};
        }

        auto directory = sourcePath.parent_path(&Scratch_);
        auto resolved = directory.empty() ? fs::CurrentPath(Scratch_) : fs::Absolute(directory, Scratch_);

        if (resolved.Error.Code) {
            auto error = resolved.Error.Code;
            std::osyncstream(std::cerr) << "warning: " << sourcePath.string()
                << ": cannot determine configuration search directory: " << error.message()
                << "; using default configuration\n";
            return {&DefaultConfig(), {}};
        }

        directory = std::move(resolved.Value);
        auto directoryKey = directory.lexically_normal(&Scratch_);

        if (auto cached = ConfigPaths_.find(directoryKey); cached != ConfigPaths_.end()) {
            return {cached->second, {}};
        }

        auto config = formatter::FindStyleFile(
            directory,
            formatter::DefaultConfigFileName,
            [] (const fs::Path& path, const std::error_code& error) {
                std::osyncstream(std::cerr) << "warning: " << path.string() << ": configuration search: "
                    << error.message() << '\n';
            });

        const ConfigEntry* entry = nullptr;

        if (!config) {
            entry = &DefaultConfig();
        } else {
            auto key = config->lexically_normal(&Scratch_);
            auto iterator = Configs_.find(key);

            if (iterator == Configs_.end()) {
                auto settings = LoadPreparedConfig(*config);

                if (settings.Error) {
                    return {nullptr, std::move(settings.Error)};
                }

                iterator = Configs_.emplace(
                    fs::Path(key, &ConfigArena_),
                    ConfigEntry{
                        .Directory = config->parent_path(&Scratch_).lexically_normal(&ConfigArena_),
                        .Settings = settings.Value,
                    }).first;
            }

            entry = &iterator->second;
        }

        ConfigPaths_.emplace(fs::Path(directoryKey, &ConfigArena_), entry);
        return {entry, {}};
    }

    std::vector<u8> ScratchStorage_ = std::vector<u8>(16 * 1024);
    Arena Scratch_{ScratchStorage_.data(), ScratchStorage_.size()};
    Arena ConfigArena_;
    std::pmr::list<cli::PreparedConfiguration> PreparedConfigs_{&ConfigArena_};
    ConfigEntry DefaultConfig_;
    std::optional<ConfigEntry> ExplicitConfig_;
    std::unordered_map<fs::Path, const ConfigEntry*> ConfigPaths_;
    std::unordered_map<fs::Path, ConfigEntry> Configs_;
};

template <typename Visit>
Result<bool, FileFailure> DiscoverFiles(const Arguments& arguments, StyleResolver& resolver, Visit visit)
{
    std::vector<u8> entryStorage(16 * 1024);
    Arena entryScratch(entryStorage.data(), entryStorage.size());
    Arena seenMemory;
    std::pmr::unordered_set<fs::Path> seen(&seenMemory);
    auto emit = [&] (const fs::Path& path, const cli::PreparedConfiguration* config) {
        if (seen.contains(path)) {
            return true;
        }

        seen.emplace(path, &seenMemory);
        return visit(path, config);
    };

    for (const auto& path : arguments.Paths) {
        entryScratch.release();
        auto status = fs::Status(path);

        if (status.Error.Code) {
            return {false, FileError(path, SystemError(status.Error))};
        }

        if (!status.Value.Regular && !status.Value.Directory) {
            return {false, FileError(path, MakeError("path does not exist"))};
        }

        if (status.Value.Regular) {
            auto excluded = IsExcluded(path.filename(&entryScratch), arguments.Exclusions);

            if (excluded.Error) {
                return excluded;
            }

            if (excluded.Value) {
                continue;
            }
        }

        const cli::PreparedConfiguration* config = nullptr;
        auto excluded = resolver.IsExcluded(path, config);

        if (excluded.Error) {
            return excluded;
        }

        if (excluded.Value) {
            continue;
        }

        if (status.Value.Regular) {
            if (!emit(path, config)) {
                return {false, {}};
            }

            continue;
        }

        Arena traversalMemory;
        FileFailure traversalError;
        auto completed = fs::Walk(
            path,
            [&] (fs::PathView name, const fs::Entry& entry) {
                entryScratch.release();

                if (arguments.Exclusions) {
                    auto relativePath = name.substr(path.size());

                    if (!relativePath.empty() && relativePath.front() == fs::Separator) {
                        relativePath.remove_prefix(1);
                    }

                    auto excluded = IsExcluded(relativePath, arguments.Exclusions);

                    if (excluded.Error) {
                        traversalError = std::move(excluded.Error);
                        return fs::WalkAction::Stop;
                    }

                    if (excluded.Value) {
                        return fs::WalkAction::SkipDirectory;
                    }
                }

                const fs::Path entryPath(name, &entryScratch);
                const cli::PreparedConfiguration* settings = nullptr;
                auto excluded = resolver.IsExcluded(entryPath, settings);

                if (excluded.Error) {
                    traversalError = std::move(excluded.Error);
                    return fs::WalkAction::Stop;
                }

                if (excluded.Value) {
                    return fs::WalkAction::SkipDirectory;
                }

                if (!entry.Directory && !entry.Symlink && entry.Regular && IsCppSource(entryPath) &&
                    !emit(entryPath, settings))
                {
                    return fs::WalkAction::Stop;
                }

                return fs::WalkAction::Continue;
            },
            traversalMemory);

        if (completed.Error.Code) {
            return {false, FileError(completed.Error.Context, SystemError(completed.Error))};
        }

        if (!completed.Value) {
            return {false, std::move(traversalError)};
        }
    }

    return {true, {}};
}

void PrintHelp(std::ostream& output, std::string_view executable, const Terminal& terminal)
{
    auto highlight = [&] (std::string_view text, std::string_view color) {
        output << color << text << terminal.Reset;
    };
    auto option = [&] (std::string_view name, std::string_view description) {
        output << "  ";
        highlight(name, terminal.BoldYellow);
        constexpr std::string_view padding = "                                ";
        output << padding.substr(std::min(name.size(), padding.size() - 1)) << description << '\n';
    };
    highlight("Usage:", terminal.BoldCyan);
    output << "\n  " << executable << " [options] <path>...\n" << "  " << executable
        << " --fix [options] <path>...\n\n";
    option("--check / --fix", "Check (default) / apply fixes.");
    option("--quick / --summary / --details", "Files / file:line:column errors / carets.");
    option("-j [n], --threads <n>", "Threads (default 1; bare -j uses all cores).");
    option("--config <file>", "Config (default: nearest .hit-fmt).");
    option("--exclude <regex>", "Skip paths (repeatable POSIX ERE).");
    option("--changed-hunks <file|->", "Original line ranges (--changed-ranges alias).");
    option("--no-spillover", "Restrict fixes to selected hunk lines.");
    option("--generate-config [preset]", "Print config: ytsaurus, google, llvm; default preserving.");
    option("--help-config", "Configuration help.");
    option("-h, --help / -V, --version", "Help / version.");
}

Result<formatter::ChangedHunkFiles, FileFailure> ReadChangedHunks(std::string_view path)
{
    Arena scratch;
    std::pmr::string buffer(&scratch);

    if (path != "-") {
        const fs::Path inputPath(path, &scratch);
        auto input = fs::ReadFile(inputPath, 0, 0, scratch);

        if (input.Error.Code) {
            return {{}, FileError(inputPath, SystemError(input.Error))};
        }

        buffer = std::move(input.Value);
    } else {
        char chunk[8192];

        while (std::cin.read(chunk, sizeof(chunk)) || std::cin.gcount() != 0) {
            buffer.append(chunk, static_cast<us>(std::cin.gcount()));
        }

        if (!std::cin.eof() || std::cin.bad()) {
            return {{}, MakeError("failed to read changed ranges")};
        }
    }

    auto directory = fs::CurrentPath(scratch);

    if (directory.Error.Code) {
        return {{}, SystemError(directory.Error)};
    }

    try {
        return {formatter::ParseChangedHunks(buffer, directory.Value), {}};
    } catch (const std::exception&) {
        return {{}, ExceptionError()};
    }
}

FileResult ProcessFile(
    const fs::Path& path,
    const cli::PreparedConfiguration& config,
    const Arguments& arguments,
    const formatter::ChangedHunkFiles& changed,
    const Terminal& terminal,
    WorkerMemory& worker,
    std::pmr::memory_resource* resultMemory)
{
    DEFER(worker.Reset());

    auto& memory = worker.File;
    auto& scratch = worker.Scratch;
    FileResult result{.Output = std::pmr::string(resultMemory)};
    std::basic_ostringstream<char, std::char_traits<char>, std::pmr::polymorphic_allocator<char>> output{
        std::pmr::string{&scratch},
    };
    auto filePath = path.string<char, std::char_traits<char>, std::pmr::polymorphic_allocator<char>>(&scratch);
    // File errors are recoverable: later inputs must still be processed.
    try {
        auto read = fs::ReadFile(path, MaxSourceFileSize, util::SourcePaddingSize, memory);

        if (read.Error.Code) {
            if (read.Error.Code == std::errc::file_too_large) {
                result.Error = FileError(path, Warning{"file is too large; skipping"});
            } else {
                result.Error = FileError(path, SystemError(read.Error));
                result.Failed = true;
            }

            return result;
        }

        const auto& sourceStorage = read.Value;
        util::PaddedSourceView source{
            .Data = sourceStorage.data(),
            .Size = static_cast<u32>(sourceStorage.size() - util::SourcePaddingSize),
        };
        auto inputFilePath = path.generic_string<char, std::char_traits<char>, std::pmr::polymorphic_allocator<char>>(
            &scratch);
        std::optional<ChangedFileScope> scope;

        if (arguments.HunkInput) {
            auto absolute = fs::Absolute(path, scratch);

            if (absolute.Error.Code) {
                result.Error = FileError(path, SystemError(absolute.Error));
                result.Failed = true;
                return result;
            }

            auto hunks = changed.Hunks.find(absolute.Value.lexically_normal(&scratch).string());

            if (hunks == changed.Hunks.end()) {
                result.Error = FileError(path, MakeError("missing changed ranges for input file"));
                result.Failed = true;
                return result;
            }

            scope = ChangedFileScope{hunks->second, arguments.Spillover};
        }

        auto checked = FormatAndCheckFile(
            source,
            config,
            inputFilePath,
            scope,
            arguments.Diagnostics,
            arguments.Debug,
            &memory,
            arguments.Fix);

        if (checked.Error) {
            result.Error = FileError(path, std::move(checked.Error));
            result.Failed = true;
            return result;
        }

        if (arguments.Fix && checked.Value.FormattingMismatch) {
            auto written = fs::WriteFile(path, checked.Value.Formatting.Text);

            if (written.Error.Code) {
                const auto& error = written.Error;
                result.Error = FileError(path, SystemError(error));
                result.Failed = true;
                return result;
            }
        }

        if (auto error = PrintCheckResult(
            output,
            filePath,
            source.View(),
            checked.Value,
            arguments.Diagnostics,
            arguments.Fix,
            terminal,
            arguments.Debug);
            error)
        {
            result.Error = FileError(path, std::move(error));
            result.Failed = true;
            return result;
        }

        result.Mismatches = checked.Value.HasFailures(arguments.Fix);
    }
    catch (const std::exception&) {
        result.Error = FileError(path, ExceptionError());
        result.Failed = true;
    }
    result.Output = output.view();
    return result;
}

template <typename Print>
Result<bool, FileFailure> DispatchFiles(
    const Arguments& arguments,
    const formatter::ChangedHunkFiles& changed,
    const Terminal& terminal,
    Print&& print,
    std::pmr::memory_resource* memory)
{
    StyleResolver discoveryResolver;

    if (auto error = discoveryResolver.Initialize(arguments); error) {
        return {false, std::move(error)};
    }

    auto discover = [&] (auto visit) {
        return DiscoverFiles(arguments, discoveryResolver, visit);
    };
    auto process = [&] (
        const auto& path,
        WorkerMemory& worker,
        const cli::PreparedConfiguration* config,
        std::pmr::memory_resource* batchMemory) {
        return ProcessFile(path, *config, arguments, changed, terminal, worker, batchMemory);
    };
    auto makeWorkerResolver = [] {
        return WorkerMemory{};
    };

    if (arguments.Threads == 1) {
        WorkerMemory worker;
        return RunSerialFiles(discover, worker, process, print, memory);
    }

    return RunParallelFiles<const cli::PreparedConfiguration*>(
        arguments.Threads,
        discover,
        makeWorkerResolver,
        process,
        print,
        memory);
}

bool PrintFileResult(const FileResult& result, us& mismatchCount)
{
    std::cout << result.Output;
    std::cout.flush();

    if (result.Error) {
        std::osyncstream(std::cerr) << result.Error << '\n';
    }

    mismatchCount += result.Mismatches;
    return result.Failed;
}

void PrintSummary(us mismatchCount, bool changedHunks, const Terminal& terminal)
{
    if (terminal.Reset.empty()) {
        return;
    }

    if (mismatchCount == 0) {
        std::cout << terminal.BoldGreen
            << (changedHunks ? "No hunk or spillover violations found." : "No style violations found.")
            << terminal.Reset << '\n';
    } else {
        std::cout << terminal.BoldRed << mismatchCount << " file" << (mismatchCount == 1 ? "" : "s")
            << " require formatting, naming or parameter names." << terminal.Reset << '\n';
    }
}

int RunFiles(
    const Arguments& arguments,
    const formatter::ChangedHunkFiles& changed,
    const Terminal& terminal,
    std::pmr::memory_resource* memory)
{
    us mismatchCount = 0;
    auto print = [&mismatchCount] (const FileResult& result) {
        return PrintFileResult(result, mismatchCount);
    };

    auto dispatched = DispatchFiles(arguments, changed, terminal, print, memory);

    if (mismatchCount != 0 && arguments.Diagnostics == DiagnosticDetail::Quick) {
        PrintQuickHint(std::cout, arguments.Fix);
    }

    if (dispatched.Error) {
        std::cerr << dispatched.Error << '\n';
        return 1;
    }

    if (!dispatched.Value) {
        return 1;
    }

    PrintSummary(mismatchCount, arguments.HunkInput.has_value(), terminal);
    return mismatchCount != 0 ? 1 : 0;
}

} // namespace

int Run(std::span<const char* const> argv)
{
    Arena cliMemory(16 * 1024);
    auto [arguments, error] = ParseArguments(argv, &cliMemory, IsTerminal());

    if (error) {
        PrintError(std::cerr, error, GetTerminal(stderr));
        return 1;
    }

    auto terminal = GetTerminal();

    if (arguments.ShowHelp) {
        PrintHelp(std::cout, argv[0], terminal);
        return 0;
    }

    if (arguments.ShowVersion) {
        std::cout << "hit-fmt " << BuildVersion() << '\n';
        return 0;
    }

    if (arguments.ShowConfigHelp) {
        PrintConfigHelp(std::cout, terminal);
        return 0;
    }

    if (arguments.GenerateConfig) {
        PrintConfig(std::cout, *arguments.GenerateConfig);
        return 0;
    }

    formatter::ChangedHunkFiles changed;

    if (arguments.HunkInput) {
        auto input = ReadChangedHunks(*arguments.HunkInput);

        if (input.Error) {
            std::cerr << input.Error << '\n';
            return 1;
        }

        changed = std::move(input.Value);
        arguments.Paths = std::move(changed.Paths);
    } else if (arguments.Paths.empty()) {
        PrintHelp(std::cerr, argv[0], GetTerminal(stderr));
        return 1;
    }

    return RunFiles(arguments, changed, terminal, &cliMemory);
}

} // namespace cli
