#include "run.hpp"

#include "config_help.hpp"
#include "config_template.hpp"
#include "file_check.hpp"
#include "file_dispatch.hpp"
#include "formatting_diagnostics.hpp"
#include "options.hpp"
#include "terminal.hpp"
#include "version.hpp"

#include <util/file.hpp>

#include <algorithm>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <memory>
#include <optional>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <syncstream>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace hit_fmt {

namespace {

constexpr size_t MaxSourceFileSize = 4 * 1024 * 1024;

bool IsCppSource(const std::filesystem::path& path)
{
    auto extension = path.extension().string();
    return extension == ".c" || extension == ".cc" || extension == ".cpp" || extension == ".cxx" || extension == ".h" ||
        extension == ".hh" || extension == ".hpp" || extension == ".hxx" || extension == ".inl" ||
        extension == ".ipp" || extension == ".tcc";
}

bool IsExcluded(const std::filesystem::path& relativePath, const std::vector<std::regex>& exclusions)
{
    if (exclusions.empty()) {
        return false;
    }
    auto name = relativePath.generic_string();
    return std::ranges::any_of(
        exclusions,
        [&] (const auto& pattern) {
            return std::regex_search(name, pattern);
        });
}

template <typename ConfigExcludes, typename Visit>
bool ForEachFile(
    const std::vector<std::filesystem::path>& paths,
    const std::vector<std::regex>& exclusions,
    ConfigExcludes configExcludes,
    Visit visit)
{
    std::unordered_set<std::filesystem::path> seen;
    auto emit = [&] (const std::filesystem::path& path) {
        return !seen.insert(path).second || visit(path);
    };
    for (const auto& path : paths) {
        if (std::filesystem::is_regular_file(path)) {
            if (!IsExcluded(path.filename(), exclusions) && !configExcludes(path, false) && !emit(path)) {
                return false;
            }
            continue;
        }
        if (!std::filesystem::is_directory(path)) {
            throw std::runtime_error(path.string() + ": path does not exist");
        }
        if (configExcludes(path, true)) {
            continue;
        }
        auto iterator = std::filesystem::recursive_directory_iterator(path);
        for (const auto end = std::filesystem::recursive_directory_iterator(); iterator != end; ++iterator) {
            const auto& entry = *iterator;
            bool directory = entry.is_directory();
            if (IsExcluded(entry.path().lexically_relative(path), exclusions) ||
                configExcludes(entry.path(), directory))
            {
                if (directory) {
                    iterator.disable_recursion_pending();
                }
                continue;
            }
            if (!entry.is_symlink() && entry.is_regular_file() && IsCppSource(entry.path()) && !emit(entry.path())) {
                return false;
            }
        }
    }
    return true;
}

class StyleResolver
{
public:
    explicit StyleResolver(const Options& options)
    {
        if (options.ConfigPath) {
            ExplicitConfig_ = ConfigEntry{
                .Directory = std::filesystem::absolute(*options.ConfigPath).parent_path().lexically_normal(),
                .Settings = LoadPreparedConfig(*options.ConfigPath),
            };
        }
    }

    const hit_fmt::PreparedFileConfiguration& Resolve(const std::filesystem::path& sourcePath)
    {
        return *FindConfig(sourcePath).Settings;
    }

    bool IsExcluded(const std::filesystem::path& path, bool directory)
    {
        // A directory is checked against its parent's configuration before
        // descending; a file uses its own nearest configuration.
        auto source = directory ? path.parent_path() / "unused.cpp" : path;
        const auto& config = FindConfig(source);
        if (config.Settings->Settings.Exclude.empty()) {
            return false;
        }
        auto relative = std::filesystem::absolute(path).lexically_normal().lexically_relative(config.Directory);
        return hit_fmt::IsExcluded(relative, config.Settings->Settings.Exclude);
    }

private:
    struct ConfigEntry
    {
        std::filesystem::path Directory;
        std::unique_ptr<const hit_fmt::PreparedFileConfiguration> Settings;
    };

    static std::unique_ptr<const hit_fmt::PreparedFileConfiguration> LoadPreparedConfig(
        const std::filesystem::path& path)
    {
        try {
            return std::make_unique<const hit_fmt::PreparedFileConfiguration>(formatter::LoadConfigFile(path));
        }
        catch (const std::exception& error) {
            throw std::runtime_error(path.string() + ": " + error.what());
        }
    }

    ConfigEntry& DefaultConfig()
    {
        if (!DefaultConfig_.Settings)
            DefaultConfig_.Settings =
                std::make_unique<const hit_fmt::PreparedFileConfiguration>(formatter::Configuration{});
        return DefaultConfig_;
    }

    const ConfigEntry& FindConfig(const std::filesystem::path& sourcePath)
    {
        if (ExplicitConfig_) {
            return *ExplicitConfig_;
        }
        auto directory = sourcePath.parent_path();
        std::error_code error;
        directory =
            directory.empty() ? std::filesystem::current_path(error) : std::filesystem::absolute(directory, error);
        if (error) {
            std::osyncstream(std::cerr) << "warning: " << sourcePath.string()
                << ": cannot determine configuration search directory: " << error.message()
                << "; using default configuration\n";
            return DefaultConfig();
        }
        auto directoryKey = directory.lexically_normal().string();
        auto [configIterator, configInserted] = ConfigPaths_.try_emplace(directoryKey);
        if (configInserted) {
            configIterator->second = formatter::FindStyleFile(
                directory,
                formatter::DefaultConfigFileName,
                [] (const std::filesystem::path& path, const std::error_code& error) {
                    std::osyncstream(std::cerr) << "warning: " << path.string() << ": configuration search: "
                        << error.message() << '\n';
                });
        }
        const auto& config = configIterator->second;
        if (!config) {
            return DefaultConfig();
        }
        auto key = config->lexically_normal().string();
        auto iterator = Configs_.try_emplace(key).first;
        if (!iterator->second.Settings) {
            iterator->second.Directory = config->parent_path().lexically_normal();
            iterator->second.Settings = LoadPreparedConfig(*config);
        }
        return iterator->second;
    }

    ConfigEntry DefaultConfig_;
    std::optional<ConfigEntry> ExplicitConfig_;
    std::unordered_map<std::string, std::optional<std::filesystem::path>>
        ConfigPaths_;
    std::unordered_map<std::string, ConfigEntry> Configs_;
};

void PrintHelp(std::ostream& output, std::string_view executable, const TerminalColors& colors)
{
    auto highlight = [&] (std::string_view text, std::string_view color) {
        output << color << text << colors.Reset;
    };
    auto option = [&] (std::string_view name, std::string_view description) {
        output << "  ";
        highlight(name, colors.BoldYellow);
        output << description << '\n';
    };
    highlight("Usage:", colors.BoldCyan);
    output << "\n  " << executable << " [options] <path>...\n" << "  " << executable
        << " --fix [options] <path>...\n\n";
    highlight("Options:", colors.BoldCyan);
    output << '\n';
    option("--fix", "            Fix what can be fixed; check the result.");
    option("--check", "          Check without editing (default; optional).");
    option("--no-spillover", "   Restrict changes to selected hunk lines.");
    option("--summary", "        Print only file status lines.");
    option("-j [n]", "         Use n threads, or all cores without n.");
    output << "    -j consumes the next non-flag argument as n; use -j -- <path> for all cores.\n";
    option("--threads <n>", "  Use n threads (default 1).");
    option("--config <file>", "  Use one configuration file for every input.");
    option("--no-config", "      Do not search parent directories for .hit-fmt.");
    option("--changed-hunks <file|->", "  Read changed file/line records (alias --changed-ranges).");
    option("--exclude <regex>", " Skip matching files or directories (repeatable).");
    option("-h, --help", "       Show this help.");
    option("-V, --version", "    Show the build version.");
    option("--help-config", "    Show configuration options and examples.");
    option("--generate-config [preset]", " Print a config to stdout.");
    output << "    Presets: ytsaurus, google, llvm; omit for a preserving config.\n"
        << "    Both --generate-config google and --generate-config=google work.\n";
    output << "\n"
        << "Without --config or --no-config, each source file uses the "
            "nearest .hit-fmt file in its directory or a parent directory.\n"
        << "Input paths may be files or directories.\n"
        << "Directories are searched recursively for C and C++ source " "files.\n"
        << "--exclude patterns search paths relative to each input directory; "
            ".hit-fmt Exclude patterns search paths relative to the " "config file.\n"
        << "Both use ECMAScript regex; matching directories are skipped.\n"
        << "Files larger than 4 MiB are skipped with a warning.\n"
        << "Hunk lines are one-based and inclusive in the original source.\n"
        << "Hunk paths are relative to the working directory. Safe enclosing "
            "lists and include groups may spill up to 200 lines.\n"
        << "Nonempty CallLikeMacroNames are currently unsupported.\n"
        << "Configure ClassMacroNames with Begin:End pairs for " "class-generating macros; see --help-config.\n"
        << "Output is colorized when written to a terminal; set " "NO_COLOR to disable it.\n\n";
    highlight("Examples:", colors.BoldCyan);
    output << '\n';
    for (auto example :
        {
            " source.cpp",
            " --fix source.cpp include/",
            " --summary src/ include/",
            " --fix --exclude '(^|/)generated(/|$)' src/",
            " --fix --changed-hunks - < hunks.txt",
            " --generate-config ytsaurus > .hit-fmt",
        })
    {
        output << "  ";
        highlight(executable, colors.BoldGreen);
        output << example << '\n';
    }
}

formatter::ChangedHunkFiles ReadChangedHunks(const std::string& path)
{
    if (path == "-") {
        return formatter::ParseChangedHunks(std::cin, std::filesystem::current_path());
    }
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("cannot open changed ranges: " + path);
    }
    return formatter::ParseChangedHunks(input, std::filesystem::current_path());
}

void PrintCheckResult(
    std::ostream& output,
    std::string_view path,
    std::string_view source,
    const CheckedFile& checked,
    const Options& options,
    const TerminalColors& colors)
{
    if (options.SummaryOnly) {
        if (options.Fix && checked.FormattingMismatch) {
            output << colors.BoldGreen << path << ": fixed formatting" << colors.Reset << '\n';
        }
        if (checked.HasFailures(options.Fix)) {
            output << colors.BoldRed << path << ": ";
            PrintFileSummaryIssues(output, checked, options.Fix);
            output << colors.Reset << '\n';
        }
        return;
    }
    const auto terminalWidth = TerminalWidth();
    if (checked.FormattingMismatch && options.Fix) {
        PrintFormattingDiagnostics(output, path, source, checked.Formatting.Text, colors, terminalWidth, true);
    } else if (checked.FormattingMismatch) {
        PrintFormattingDiagnostics(
            output,
            path,
            source,
            checked.Formatting.Text,
            colors,
            terminalWidth,
            false,
            checked.IncludeOrderChanges);
    }
    PrintFileIssues(output, path, source, checked, options.Fix, terminalWidth);
}

FileResult ProcessFile(
    const std::filesystem::path& path,
    StyleResolver& resolver,
    const Options& options,
    const formatter::ChangedHunkFiles& changed,
    const TerminalColors& colors)
{
    FileResult result;
    std::ostringstream output;
    const auto filePath = path.string();
    // File errors are recoverable: later inputs must still be processed.
    try {
        auto sourceStorage = util::ReadEntireFile(filePath, MaxSourceFileSize, util::SourcePaddingSize);
        const util::PaddedSourceView source{
            .Data = sourceStorage.data(),
            .Size = static_cast<uint32_t>(sourceStorage.size() - util::SourcePaddingSize),
        };
        const auto& config = resolver.Resolve(path);
        const auto inputFilePath = path.generic_string();
        std::optional<ChangedFileScope> scope;
        if (options.HunkInput) {
            const auto absolute = std::filesystem::absolute(path).lexically_normal();
            scope = ChangedFileScope{changed.Hunks.at(absolute.string()), options.Spillover};
        }
        const auto checked = CheckFile(source, config, inputFilePath, scope);
        if (options.Fix && checked.FormattingMismatch) {
            util::WriteEntireFile(filePath, checked.Formatting.Text);
        }
        PrintCheckResult(output, filePath, source.View(), checked, options, colors);
        result.Mismatches = checked.HasFailures(options.Fix);
    }
    catch (const util::FileTooLargeError& error) {
        result.Error = "warning: " + std::string(error.what()) + "; skipping\n";
    }
    catch (const std::exception& error) {
        result.Error = filePath + ": " + error.what() + '\n';
        result.Failed = true;
    }
    result.Output = output.str();
    return result;
}

void PrintSummary(size_t mismatchCount, bool changedHunks, const TerminalColors& colors)
{
    if (colors.Reset.empty()) {
        return;
    }
    if (mismatchCount == 0) {
        std::cout << colors.BoldGreen
            << (changedHunks ? "No hunk or spillover violations found." : "All checked files are formatted.")
            << colors.Reset << '\n';
    } else {
        std::cout << colors.BoldRed << mismatchCount << " file" << (mismatchCount == 1 ? "" : "s")
            << " require formatting, naming or parameter names." << colors.Reset << '\n';
    }
}

int RunFiles(const Options& options, const formatter::ChangedHunkFiles& changed, const TerminalColors& colors)
{
    StyleResolver styleResolver(options);
    const auto discover = [&] (auto visit) {
        return ForEachFile(
            options.Paths,
            options.Exclusions,
            [&] (const auto& path, bool directory) { return styleResolver.IsExcluded(path, directory); },
            visit);
    };
    const auto process = [&] (const auto& path, StyleResolver& resolver) {
        return ProcessFile(path, resolver, options, changed, colors);
    };
    size_t mismatchCount = 0;
    const auto print = [&] (const FileResult& result) {
        std::cout << result.Output;
        std::cout.flush();
        if (!result.Error.empty()) {
            std::osyncstream(std::cerr) << result.Error;
        }
        mismatchCount += result.Mismatches;
        return result.Failed;
    };
    const bool success = options.Threads == 1
        ? RunSerialFiles(discover, styleResolver, process, print)
        : RunParallelFiles(options.Threads, discover, [&] { return StyleResolver(options); }, process, print);
    if (!success) {
        return 1;
    }
    PrintSummary(mismatchCount, options.HunkInput.has_value(), colors);
    return mismatchCount != 0 ? 1 : 0;
}

} // namespace

int Run(std::span<const char* const> args)
{
    auto options = ParseOptions(args);
    const auto colors = GetTerminalColors();
    if (options.ShowHelp) {
        PrintHelp(std::cout, args[0], colors);
        return 0;
    }
    if (options.ShowVersion) {
        std::cout << "hit-fmt " << BuildVersion() << '\n';
        return 0;
    }
    if (options.ShowConfigHelp) {
        PrintConfigHelp(std::cout, colors);
        return 0;
    }
    if (options.GenerateConfig) {
        PrintConfig(std::cout, *options.GenerateConfig);
        return 0;
    }
    formatter::ChangedHunkFiles changed;
    if (options.HunkInput) {
        changed = ReadChangedHunks(*options.HunkInput);
        options.Paths = std::move(changed.Paths);
    } else if (options.Paths.empty()) {
        PrintHelp(std::cerr, args[0], GetTerminalColors(stderr));
        return 1;
    }
    return RunFiles(options, changed, colors);
}

} // namespace hit_fmt
