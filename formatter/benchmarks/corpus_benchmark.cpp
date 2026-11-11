#include <formatter/config.hpp>
#include <formatter/formatter.hpp>
#include <util/types.hpp>
#include <util/memory.hpp>
#include <util/file.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <cstdint>
#include <exception>
#include <fs/fs.hpp>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr us MaxSourceFileSize = 4 * 1024 * 1024;

bool IsCppSource(const fs::Path& path)
{
    auto extension = path.extension().string();
    return extension == ".c" || extension == ".cc" || extension == ".cpp" ||
        extension == ".cxx" || extension == ".h" || extension == ".hh" ||
        extension == ".hpp" || extension == ".hxx" || extension == ".inl" ||
        extension == ".ipp" || extension == ".tcc";
}

// Stable FNV-1a fingerprints make output changes visible without retaining
// the corpus in memory. Hashing and file I/O are outside the timed region.
u64 Fingerprint(std::string_view text)
{
    u64 hash = 14695981039346656037ULL;
    for (unsigned char byte : text) {
        hash = (hash ^ byte) * 1099511628211ULL;
    }
    return hash;
}

} // namespace

int main(int argc, char* argv[])
{
    if (argc != 3) {
        std::cerr << "Usage: formatter-corpus-benchmark <corpus> <config>\n"
                  << "Per-file input/output fingerprints go to stdout; "
                     "timing goes to stderr.\n";
        return 1;
    }
    try {
        auto config = formatter::LoadConfigFile(argv[2]);
        const formatter::PreparedFormat prepared(config.FormattingStyle);
        auto absolute = fs::Absolute(fs::Path(argv[1]));
        if (absolute.Error.Code) { throw std::runtime_error(absolute.Error.Code.message()); }
        auto root = std::move(absolute.Value);
        std::vector<fs::Path> paths;
        Arena memory;
        auto walked = fs::Walk(root, [&] (fs::PathView name, const fs::Entry& entry) {
            fs::Path path(name);
            if (!entry.Symlink && entry.Regular && IsCppSource(path)) { paths.push_back(std::move(path)); }
            return fs::WalkAction::Continue;
        }, memory);
        if (walked.Error.Code) { throw std::runtime_error(walked.Error.Code.message()); }
        std::ranges::sort(paths);
        if (paths.empty()) {
            throw std::runtime_error("no C/C++ source files found");
        }

        const bool perFile = std::getenv("HIT_FMT_BENCH_PER_FILE") != nullptr;
        std::chrono::steady_clock::duration elapsed{};
        us files = 0;
        us skipped = 0;
        us inputBytes = 0;
        us outputBytes = 0;
        for (const auto& path : paths) {
            std::string storage;
            try {
                storage = util::ReadEntireFile(
                    path.string(),
                    MaxSourceFileSize,
                    util::SourcePaddingSize);
            } catch (const util::FileTooLargeError& error) {
                std::cerr << "warning: " << error.what() << "; skipping\n";
                ++skipped;
                continue;
            }
            util::PaddedSourceView source{
                .Data = storage.data(),
                .Size = static_cast<u32>(
                    storage.size() - util::SourcePaddingSize),
            };
            const auto inputFilePath = path.generic_string();
            auto start = std::chrono::steady_clock::now();
            auto output = formatter::Format(source, prepared, {.InputFilePath = inputFilePath}).Text;
            auto fileElapsed = std::chrono::steady_clock::now() - start;
            elapsed += fileElapsed;
            if (perFile) {
                std::cerr << "file_seconds="
                          << std::chrono::duration<double>(fileElapsed).count()
                          << '\t' << path.lexically_relative(root).generic_string()
                          << '\n';
            }

            ++files;
            inputBytes += source.Size;
            outputBytes += output.size();
            std::cout << path.lexically_relative(root).generic_string() << '\t'
                      << source.Size << '\t' << Fingerprint(source.View())
                      << '\t' << output.size() << '\t' << Fingerprint(output)
                      << '\n';
        }
        std::cerr << std::fixed << std::setprecision(6)
                  << "files=" << files << " skipped=" << skipped
                  << " input_bytes=" << inputBytes
                  << " output_bytes=" << outputBytes
                  << " seconds=" << std::chrono::duration<double>(elapsed).count()
                  << '\n';
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
