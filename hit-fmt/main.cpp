#include <formatter/config.hpp>
#include <formatter/formatter.hpp>
#include <formatter/naming.hpp>
#include <formatter/parameter_names.hpp>
#include <util/file.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <limits>
#include <optional>
#include <regex>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

namespace {

constexpr size_t MaxSourceFileSize = 4 * 1024 * 1024;
constexpr size_t DiffLookahead = 16;

constexpr std::string_view ResetColor = "\033[0m";
constexpr std::string_view BoldRed = "\033[1;31m";
constexpr std::string_view BoldGreen = "\033[1;32m";
constexpr std::string_view Red = "\033[31m";
constexpr std::string_view Green = "\033[32m";
constexpr std::string_view Cyan = "\033[36m";
constexpr std::string_view Faint = "\033[2m";

enum class Mode
{
    Format,
    InPlace,
    Check,
};

struct Line
{
    std::string_view Text;
    bool HasNewline;

    bool operator==(const Line&) const = default;
};

std::vector<Line> SplitLines(std::string_view text)
{
    std::vector<Line> lines;
    lines.reserve(std::count(text.begin(), text.end(), '\n') + 1);

    size_t begin = 0;
    while (begin < text.size()) {
        auto newline = text.find('\n', begin);
        bool hasNewline = newline != std::string_view::npos;
        auto end = hasNewline ? newline : text.size();
        lines.push_back(
            {.Text = text.substr(begin, end - begin),
             .HasNewline = hasNewline});
        begin = hasNewline ? end + 1 : text.size();
    }
    return lines;
}

void PrintParameterDiagnostics(
    std::string_view path,
    std::string_view source,
    const std::vector<Line>& lines,
    std::span<const formatter::ParameterNameDiagnostic> diagnostics)
{
    const auto& first = diagnostics.front();
    std::cout << "fail: unnamed parameter";
    if (diagnostics.size() != 1) {
        std::cout << 's';
    }
    const auto width = std::to_string(first.SignatureEndLine).size();
    std::cout << " in " << first.Function << '\n'
              << "  --> " << path << ':' << first.Position.Line << ':'
              << first.Position.Column << '\n'
              << std::string(width + 1, ' ') << "|\n";

    for (auto lineNumber = first.SignatureBeginLine;
        lineNumber <= first.SignatureEndLine; ++lineNumber)
    {
        auto text = lines[lineNumber - 1].Text;
        if (lineNumber == first.SignatureEndLine) {
            auto end = static_cast<size_t>(first.Signature.End) -
                static_cast<size_t>(text.data() - source.data());
            text = text.substr(0, std::min(end, text.size()));
        }
        if (text.ends_with('\r')) {
            text.remove_suffix(1);
        }
        const auto gutter = std::string(width + 1, ' ') + "| ";
        std::cout << std::string(width - std::to_string(lineNumber).size(), ' ')
                  << lineNumber << " | " << text << '\n';
        for (const auto& diagnostic : diagnostics) {
            if (diagnostic.Position.Line != lineNumber) {
                continue;
            }
            std::cout << gutter;
            // Positions count code points, not bytes. Retain source tabs
            // so the caret follows the same tab stops as the source line.
            size_t offset = 0;
            for (size_t column = 1;
                column < diagnostic.Position.Column && offset < text.size();
                ++column)
            {
                ++offset;
                while (offset < text.size() &&
                    (static_cast<unsigned char>(text[offset]) & 0xc0) == 0x80)
                {
                    ++offset;
                }
            }
            for (unsigned char c : text.substr(0, offset)) {
                if ((c & 0xc0) != 0x80) {
                    std::cout.put(c == '\t' ? '\t' : ' ');
                }
            }
            std::cout << "^ parameter #" << diagnostic.ParameterNumber
                      << ": expected a name";
            if (diagnostic.AllowCommentedName) {
                std::cout << " or /*name*/ comment";
            }
            std::cout << '\n';
        }
    }
}

bool UseTerminalColors()
{
    if (std::getenv("NO_COLOR") != nullptr) {
        return false;
    }
    if (auto* term = std::getenv("TERM");
        term && std::string_view(term) == "dumb")
    {
        return false;
    }
#ifdef _WIN32
    return _isatty(_fileno(stdout)) != 0;
#else
    return isatty(fileno(stdout)) != 0;
#endif
}

void PrintLine(char prefix, const Line& line, bool useColor)
{
    if (useColor) {
        std::cout << (prefix == '-' ? Red : Green);
    }
    std::cout.put(prefix);
    std::cout.write(line.Text.data(), line.Text.size());
    if (useColor) {
        std::cout << ResetColor;
    }
    std::cout.put('\n');
    if (!line.HasNewline) {
        if (useColor) {
            std::cout << Faint;
        }
        std::cout << "\\ No newline at end of file";
        if (useColor) {
            std::cout << ResetColor;
        }
        std::cout.put('\n');
    }
}

void PrintMismatch(
    std::string_view path,
    std::string_view source,
    std::string_view formatted,
    bool useColor)
{
    auto sourceLines = SplitLines(source);
    auto formattedLines = SplitLines(formatted);

    if (useColor) {
        std::cout << BoldRed;
    }
    std::cout << path << ": formatting mismatch";
    if (useColor) {
        std::cout << ResetColor;
    }
    std::cout.put('\n');
    size_t sourceIndex = 0;
    size_t formattedIndex = 0;
    while (sourceIndex < sourceLines.size() ||
        formattedIndex < formattedLines.size())
    {
        if (sourceIndex < sourceLines.size() &&
            formattedIndex < formattedLines.size() &&
            sourceLines[sourceIndex] == formattedLines[formattedIndex])
        {
            ++sourceIndex;
            ++formattedIndex;
            continue;
        }

        size_t sourceCount = sourceIndex < sourceLines.size() ? 1 : 0;
        size_t formattedCount = formattedIndex < formattedLines.size() ? 1 : 0;
        size_t bestDistance = std::numeric_limits<size_t>::max();
        auto sourceLimit =
            std::min(sourceLines.size(), sourceIndex + DiffLookahead + 1);
        auto formattedLimit =
            std::min(formattedLines.size(), formattedIndex + DiffLookahead + 1);
        for (auto nextSource = sourceIndex; nextSource < sourceLimit;
            ++nextSource)
        {
            for (auto nextFormatted = formattedIndex;
                nextFormatted < formattedLimit;
                ++nextFormatted)
            {
                if (nextSource == sourceIndex &&
                    nextFormatted == formattedIndex)
                {
                    continue;
                }
                if (sourceLines[nextSource] != formattedLines[nextFormatted]) {
                    continue;
                }
                auto distance =
                    nextSource - sourceIndex + nextFormatted - formattedIndex;
                if (distance < bestDistance) {
                    bestDistance = distance;
                    sourceCount = nextSource - sourceIndex;
                    formattedCount = nextFormatted - formattedIndex;
                }
            }
        }
        if (bestDistance == std::numeric_limits<size_t>::max()) {
            sourceCount = sourceLines.size() - sourceIndex;
            formattedCount = formattedLines.size() - formattedIndex;
        }

        if (useColor) {
            std::cout << Cyan;
        }
        std::cout << "@@ -" << sourceIndex + 1 << ',' << sourceCount << " +"
                  << formattedIndex + 1 << ',' << formattedCount << " @@";
        if (useColor) {
            std::cout << ResetColor;
        }
        std::cout.put('\n');
        for (size_t i = 0; i < sourceCount; ++i) {
            PrintLine('-', sourceLines[sourceIndex + i], useColor);
        }
        for (size_t i = 0; i < formattedCount; ++i) {
            PrintLine('+', formattedLines[formattedIndex + i], useColor);
        }
        sourceIndex += sourceCount;
        formattedIndex += formattedCount;
    }
}

bool IsCppSource(const std::filesystem::path& path)
{
    auto extension = path.extension().string();
    return extension == ".c" || extension == ".cc" || extension == ".cpp" ||
        extension == ".cxx" || extension == ".h" || extension == ".hh" ||
        extension == ".hpp" || extension == ".hxx" || extension == ".inl" ||
        extension == ".ipp" || extension == ".tcc";
}

bool IsExcluded(
    const std::filesystem::path& relativePath,
    const std::vector<std::regex>& exclusions)
{
    if (exclusions.empty()) {
        return false;
    }
    auto name = relativePath.generic_string();
    return std::ranges::any_of(exclusions, [&] (const auto& pattern) {
        return std::regex_search(name, pattern);
    });
}

template <typename ConfigExcludes>
std::vector<std::filesystem::path> CollectFiles(
    const std::vector<std::filesystem::path>& paths,
    const std::vector<std::regex>& exclusions,
    bool allowDirectories,
    ConfigExcludes configExcludes)
{
    std::vector<std::filesystem::path> files;
    for (const auto& path : paths) {
        if (std::filesystem::is_regular_file(path)) {
            if (!IsExcluded(path.filename(), exclusions) &&
                !configExcludes(path, false))
            {
                files.push_back(path);
            }
            continue;
        }
        if (!std::filesystem::is_directory(path)) {
            throw std::runtime_error(path.string() + ": path does not exist");
        }
        if (!allowDirectories) {
            throw std::runtime_error(
                path.string() + ": directory input requires -i or --check");
        }
        if (configExcludes(path, true)) {
            continue;
        }
        auto iterator = std::filesystem::recursive_directory_iterator(path);
        for (const auto end = std::filesystem::recursive_directory_iterator();
            iterator != end; ++iterator)
        {
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
            if (!entry.is_symlink() && entry.is_regular_file() &&
                IsCppSource(entry.path()))
            {
                files.push_back(entry.path());
            }
        }
    }
    std::ranges::sort(files);
    files.erase(std::ranges::unique(files).begin(), files.end());
    return files;
}

struct Options
{
    Mode FormattingMode = Mode::Format;
    bool SummaryOnly = false;
    bool DisableConfig = false;
    bool ShowHelp = false;
    std::optional<std::filesystem::path> ConfigPath;
    std::vector<std::regex> Exclusions;
    std::vector<std::filesystem::path> Paths;
};

Options ParseOptions(int argc, char* argv[])
{
    Options options;
    bool modeSelected = false;
    bool positionalOnly = false;
    for (int index = 1; index < argc; ++index) {
        std::string_view argument(argv[index]);
        if (positionalOnly || !argument.starts_with('-') || argument == "-") {
            options.Paths.emplace_back(argument);
        } else if (argument == "--") {
            positionalOnly = true;
        } else if (argument == "-h" || argument == "--help") {
            options.ShowHelp = true;
        } else if (argument == "-i" || argument == "--in-place") {
            if (modeSelected) {
                throw std::runtime_error("formatting mode specified more than once");
            }
            options.FormattingMode = Mode::InPlace;
            modeSelected = true;
        } else if (argument == "--check") {
            if (modeSelected) {
                throw std::runtime_error("formatting mode specified more than once");
            }
            options.FormattingMode = Mode::Check;
            modeSelected = true;
        } else if (argument == "--summary") {
            options.SummaryOnly = true;
        } else if (argument == "--no-config") {
            options.DisableConfig = true;
        } else if (argument == "--config") {
            if (++index == argc) {
                throw std::runtime_error("--config requires a file path");
            }
            options.ConfigPath = argv[index];
        } else if (argument.starts_with("--config=")) {
            options.ConfigPath = std::string(argument.substr(9));
        } else if (argument == "--exclude" ||
            argument.starts_with("--exclude="))
        {
            std::string pattern;
            if (argument == "--exclude") {
                if (++index == argc) {
                    throw std::runtime_error("--exclude requires a regex");
                }
                pattern = argv[index];
            } else {
                pattern = argument.substr(10);
            }
            if (pattern.empty()) {
                throw std::runtime_error("--exclude requires a regex");
            }
            try {
                options.Exclusions.emplace_back(pattern);
            } catch (const std::regex_error& error) {
                throw std::runtime_error(
                    "invalid --exclude regex: " + pattern + ": " + error.what());
            }
        } else {
            throw std::runtime_error("unknown option: " + std::string(argument));
        }
    }
    if (options.SummaryOnly && options.FormattingMode != Mode::Check) {
        throw std::runtime_error("--summary requires --check");
    }
    if (options.DisableConfig && options.ConfigPath) {
        throw std::runtime_error("--config and --no-config cannot be combined");
    }
    return options;
}

class StyleResolver
{
public:
    explicit StyleResolver(const Options& options)
        : Disabled_(options.DisableConfig)
    {
        if (options.ConfigPath) {
            ExplicitConfig_ = ConfigEntry{
                .Directory = std::filesystem::absolute(*options.ConfigPath)
                                 .parent_path().lexically_normal(),
                .Settings = formatter::LoadConfigFile(*options.ConfigPath),
            };
        }
    }

    const formatter::Configuration& Resolve(
        const std::filesystem::path& sourcePath)
    {
        return FindConfig(sourcePath).Settings;
    }

    bool IsExcluded(const std::filesystem::path& path, bool directory)
    {
        // A directory is checked against its parent's configuration before
        // descending; a file uses its own nearest configuration.
        auto source = directory ? path.parent_path() / "unused.cpp" : path;
        const auto& config = FindConfig(source);
        if (config.Settings.Exclusions.empty()) {
            return false;
        }
        auto relative = std::filesystem::absolute(path).lexically_normal()
                            .lexically_relative(config.Directory);
        return ::IsExcluded(relative, config.Settings.Exclusions);
    }

private:
    struct ConfigEntry
    {
        std::filesystem::path Directory;
        formatter::Configuration Settings;
    };

    const ConfigEntry& FindConfig(const std::filesystem::path& sourcePath)
    {
        if (ExplicitConfig_) {
            return *ExplicitConfig_;
        }
        if (Disabled_) {
            return DefaultConfig_;
        }
        auto directory = sourcePath.parent_path();
        std::error_code error;
        directory = directory.empty()
            ? std::filesystem::current_path(error)
            : std::filesystem::absolute(directory, error);
        if (error) {
            return DefaultConfig_;
        }
        auto directoryKey = directory.lexically_normal().string();
        auto [configIterator, configInserted] =
            ConfigPaths_.try_emplace(directoryKey);
        if (configInserted) {
            configIterator->second = formatter::FindStyleFile(directory);
        }
        const auto& config = configIterator->second;
        if (!config) {
            return DefaultConfig_;
        }
        auto key = config->lexically_normal().string();
        auto [iterator, inserted] = Configs_.try_emplace(key);
        if (inserted) {
            iterator->second.Directory =
                config->parent_path().lexically_normal();
            iterator->second.Settings = formatter::LoadConfigFile(*config);
        }
        return iterator->second;
    }

    bool Disabled_;
    ConfigEntry DefaultConfig_;
    std::optional<ConfigEntry> ExplicitConfig_;
    std::unordered_map<
        std::string,
        std::optional<std::filesystem::path>> ConfigPaths_;
    std::unordered_map<std::string, ConfigEntry> Configs_;
};

void PrintHelp(std::ostream& output, std::string_view executable)
{
    output
        << "Usage:\n"
        << "  " << executable << " [options] <file>\n"
        << "  " << executable << " -i [options] <path>...\n"
        << "  " << executable << " --check [options] <path>...\n"
        << "\n"
        << "Options:\n"
        << "  -i, --in-place   Format files in place.\n"
        << "  --check          Report mismatches without modifying files.\n"
        << "  --summary        With --check, print only file names.\n"
        << "  --config <file>  Use one configuration file for every input.\n"
        << "  --no-config      Do not search parent directories for .hit-fmt.\n"
        << "  --exclude <regex> Skip matching files or directories (repeatable).\n"
        << "  -h, --help       Show this help.\n"
        << "\n"
        << "Without --config or --no-config, each source file uses the "
           "nearest .hit-fmt file in its directory or a parent directory.\n"
        << "Paths passed to -i or --check may be files or directories.\n"
        << "Directories are searched recursively for C and C++ source "
           "files.\n"
        << "--exclude patterns search paths relative to each input directory; "
           ".hit-fmt Exclude patterns search paths relative to the config file.\n"
        << "Both use ECMAScript regex; matching directories are skipped.\n"
        << "Files larger than 4 MiB are skipped with a warning in -i and "
           "--check modes.\n"
        << "Check output is colorized when written to a terminal; set "
           "NO_COLOR to disable it.\n"
        << "\n"
        << "Examples:\n"
        << "  " << executable << " source.cpp\n"
        << "  " << executable << " -i source.cpp include/\n"
        << "  " << executable << " --check src/ include/header.h\n"
        << "  " << executable << " --check --summary src/ include/\n"
        << "  " << executable << " -i --exclude '(^|/)generated(/|$)' src/\n";
}

} // namespace

int main(int argc, char* argv[])
{
    Options options;
    std::vector<std::filesystem::path> files;
    std::optional<StyleResolver> styleResolver;
    try {
        options = ParseOptions(argc, argv);
        if (options.ShowHelp) {
            PrintHelp(std::cout, argv[0]);
            return 0;
        }
        bool standardOutputMode = options.FormattingMode == Mode::Format;
        if (options.Paths.empty() || (standardOutputMode && options.Paths.size() != 1)) {
            PrintHelp(std::cerr, argv[0]);
            return 1;
        }
        styleResolver.emplace(options);
        files = CollectFiles(
            options.Paths,
            options.Exclusions,
            !standardOutputMode,
            [&] (const auto& path, bool directory) {
                return styleResolver->IsExcluded(path, directory);
            });
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    auto mode = options.FormattingMode;
    bool summaryOnly = options.SummaryOnly;
    bool useColor = UseTerminalColors();
    size_t mismatchCount = 0;
    for (const auto& path : files) {
        auto filePath = path.string();
        try {
            auto sourceStorage = util::ReadEntireFile(
                filePath,
                MaxSourceFileSize,
                util::SourcePaddingSize);
            util::PaddedSourceView paddedSource{
                .Data = sourceStorage.data(),
                .Size = sourceStorage.size() - util::SourcePaddingSize,
            };
            auto source = paddedSource.View();
            const auto& config = styleResolver->Resolve(path);
            auto tree = parser::Parse(paddedSource, [] (parser::ParseError) { });
            auto formatted = formatter::Format(tree, config.FormattingStyle);
            switch (mode) {
            case Mode::Format:
                std::cout.write(formatted.data(), formatted.size());
                break;
            case Mode::InPlace:
                if (formatted != source) {
                    util::WriteEntireFile(filePath, formatted);
                }
                break;
            case Mode::Check: {
                auto names = formatter::CheckNames(tree, config.Naming);
                auto parameters = formatter::CheckParameterNames(tree, config);
                bool mismatch = formatted != source || !names.empty() ||
                    !parameters.empty();
                if (summaryOnly && mismatch) {
                    if (useColor) std::cout << BoldRed;
                    std::cout << filePath << ": ";
                    if (formatted != source || !names.empty()) {
                        std::cout << (formatted != source ? "formatting" :
                            "naming") << " mismatch";
                    } else {
                        std::cout << "fail: unnamed parameter";
                        if (parameters.size() != 1) {
                            std::cout << 's';
                        }
                    }
                    if (useColor) std::cout << ResetColor;
                    std::cout.put('\n');
                } else if (!summaryOnly) {
                    if (formatted != source) {
                        PrintMismatch(filePath, source, formatted, useColor);
                    }
                    for (const auto& diagnostic : names) {
                        std::cout << filePath << ':'
                                  << diagnostic.Position.Line << ':'
                                  << diagnostic.Position.Column << ": "
                                  << diagnostic.Message << '\n';
                    }
                    if (!parameters.empty()) {
                        auto sourceLines = SplitLines(source);
                        for (size_t begin = 0; begin < parameters.size();) {
                            auto end = begin + 1;
                            while (end < parameters.size() &&
                                parameters[end].Signature.Begin ==
                                    parameters[begin].Signature.Begin &&
                                parameters[end].Signature.End ==
                                    parameters[begin].Signature.End)
                            {
                                ++end;
                            }
                            PrintParameterDiagnostics(filePath, source,
                                sourceLines,
                                std::span(parameters).subspan(begin, end - begin));
                            begin = end;
                        }
                    }
                }
                mismatchCount += mismatch;
                break;
            }
            }
        } catch (const util::FileTooLargeError& error) {
            if (mode == Mode::Format) {
                std::cerr << error.what() << '\n';
                return 1;
            }
            std::cerr << "warning: " << error.what() << "; skipping\n";
        } catch (const std::exception& error) {
            std::cerr << filePath << ": " << error.what() << '\n';
            return 1;
        }
    }
    if (mode == Mode::Check && useColor) {
        if (mismatchCount == 0) {
            std::cout << BoldGreen << "All checked files are formatted."
                      << ResetColor << '\n';
        } else {
            std::cout << BoldRed << mismatchCount << " file"
                      << (mismatchCount == 1 ? "" : "s")
                      << " require formatting, naming or parameter names."
                      << ResetColor << '\n';
        }
    }
    return mismatchCount != 0 ? 1 : 0;
}
