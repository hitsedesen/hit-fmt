#include <formatter/config.hpp>
#include <formatter/formatter.hpp>
#include <util/file.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr size_t MaxSourceFileSize = 4 * 1024 * 1024;

bool IsCppSource(const std::filesystem::path& path)
{
    auto extension = path.extension().string();
    return extension == ".c" || extension == ".cc" || extension == ".cpp" ||
        extension == ".cxx" || extension == ".h" || extension == ".hh" ||
        extension == ".hpp" || extension == ".hxx" || extension == ".inl" ||
        extension == ".ipp" || extension == ".tcc";
}

// Stable FNV-1a fingerprints make output changes visible without retaining
// the corpus in memory. Hashing and file I/O are outside the timed region.
uint64_t Fingerprint(std::string_view text)
{
    uint64_t hash = 14695981039346656037ULL;
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
        std::filesystem::path root(argv[1]);
        std::vector<std::filesystem::path> paths;
        for (const auto& entry :
             std::filesystem::recursive_directory_iterator(root))
        {
            if (!entry.is_symlink() && entry.is_regular_file() &&
                IsCppSource(entry.path()))
            {
                paths.push_back(entry.path());
            }
        }
        std::ranges::sort(paths);
        if (paths.empty()) {
            throw std::runtime_error("no C/C++ source files found");
        }

        const bool perFile = std::getenv("HIT_FMT_BENCH_PER_FILE") != nullptr;
        std::chrono::steady_clock::duration elapsed{};
        size_t files = 0;
        size_t skipped = 0;
        size_t inputBytes = 0;
        size_t outputBytes = 0;
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
                .Size = storage.size() - util::SourcePaddingSize,
            };
            auto start = std::chrono::steady_clock::now();
            auto output = formatter::Format(source, config.FormattingStyle);
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
