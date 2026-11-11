#pragma once

#include <fs/fs.hpp>

#include <algorithm>
#include <string_view>

namespace cli {

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
