#pragma once

#include <fs/fs.hpp>

#include <algorithm>
#include <string_view>
#include <utility>

namespace cli {

inline fs::Path FindRepositoryRoot(const fs::Path& startDirectory, std::pmr::memory_resource& scratch)
{
    auto absolute = fs::Absolute(startDirectory, scratch);

    if (absolute.Error.Code) {
        return fs::Path(&scratch);
    }

    auto canonical = fs::Canonical(absolute.Value, false, scratch);

    if (canonical.Error.Code) {
        return fs::Path(&scratch);
    }

    auto directory = std::move(canonical.Value);

    for (;;) {
        for (const auto name : {".git", ".arc"}) {
            auto marker = directory / fs::Path(name, &scratch);
            auto status = fs::Status(marker);

            if (!status.Error.Code && (status.Value.Directory || status.Value.Regular)) {
                return directory;
            }
        }

        auto parent = directory.parent_path(&scratch);

        if (parent == directory) {
            return fs::Path(&scratch);
        }

        directory = std::move(parent);
    }
}

inline bool IsCppSource(const fs::Path& path)
{
    const auto& name = path.native();
    auto separator = name.find_last_of(fs::Path::preferred_separator);
    auto start = separator == fs::Path::string_type::npos ? 0 : separator + 1;
#ifdef _WIN32
    if (start == 0 && name.size() >= 2 && name[1] == ':') {
        start = 2;
    }

    auto slash = name.find_last_of('/');

    if (slash != fs::Path::string_type::npos && slash + 1 > start) {
        start = slash + 1;
    }
#endif
    auto dot = name.find_last_of('.');

    if (dot == fs::Path::string_type::npos || dot <= start ||
        (dot == start + 1 && name[start] == '.' && dot + 1 == name.size()))
    {
        return false;
    }

    std::basic_string_view<fs::Path::value_type> extension(name.data() + dot, name.size() - dot);

    for (std::string_view candidate :
        {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".inl", ".ipp", ".tcc"})
    {
        if (extension.size() == candidate.size() && std::equal(extension.begin(), extension.end(), candidate.begin())) {
            return true;
        }
    }

    return false;
}

} // namespace cli
