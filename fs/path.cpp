#include "fs.hpp"

#include <util/memory.hpp>
#include <util/types.hpp>

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <limits>
#ifndef _WIN32
#include <unistd.h>
#endif

namespace fs {

namespace {

bool Sep(Char ch)
{
    return ch == Separator
#ifdef _WIN32
        || ch == '/'
#endif
        ;
}
us RootName(PathView path)
{
#ifdef _WIN32
    if (path.size() >= 2 && path[1] == ':') {
        return 2;
    }

    if (path.size() > 2 && Sep(path[0]) && Sep(path[1]) && !Sep(path[2])) {
        us end = 2;

        while (end < path.size() && !Sep(path[end])) {
            ++end;
        }

        return end;
    }
#endif
    return 0;
}
us Root(PathView path)
{
    auto end = RootName(path);

    if (end < path.size() && Sep(path[end])) {
        ++end;
    }

    return end;
}
Vector<PathView> Parts(PathView text, us start, std::pmr::memory_resource* memory)
{
    Vector<PathView> result(memory);

    if (start < text.size()) {
        result.reserve(std::count_if(text.begin() + start, text.end(), Sep) + 1);
    }

    while (start < text.size()) {
        while (start < text.size() && Sep(text[start])) {
            ++start;
        }

        auto end = start;

        while (end < text.size() && !Sep(text[end])) {
            ++end;
        }

        if (end > start) {
            result.push_back(text.substr(start, end - start));
        }

        start = end;
    }

    return result;
}
bool Dot(PathView text)
{
    return text.size() == 1 && text[0] == '.';
}

bool DotDot(PathView text)
{
    return text.size() == 2 && text[0] == '.' && text[1] == '.';
}

} // namespace
NativeString FromUtf8(std::string_view text, std::pmr::memory_resource* memory)
{
#ifdef _WIN32
    auto size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    NativeString result(static_cast<us>(size), L'\0', memory);
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size);
    return result;
#else
    return NativeString(text, memory);
#endif
}
us Utf8Size(PathView text)
{
#ifdef _WIN32
    return static_cast<us>(WideCharToMultiByte(
        CP_UTF8,
        0,
        text.data(),
        static_cast<int>(text.size()),
        nullptr,
        0,
        nullptr,
        nullptr));
#else
    return text.size();
#endif
}
void EncodeUtf8(PathView text, char* output, us size)
{
#ifdef _WIN32
    WideCharToMultiByte(
        CP_UTF8,
        0,
        text.data(),
        static_cast<int>(text.size()),
        output,
        static_cast<int>(size),
        nullptr,
        nullptr);
#else
    (void)size;
    std::copy(text.begin(), text.end(), output);
#endif
}

Path Path::parent_path(std::pmr::memory_resource* memory) const
{
    PathView text = Text_;
    const auto root = Root(text);

    if (text.size() <= root) {
        return Path(text, memory);
    }

    us end = text.size();

    if (Sep(text.back())) {
        while (end > root && Sep(text[end - 1])) {
            --end;
        }

        return Path(text.substr(0, end), memory);
    }

    while (end > root && !Sep(text[end - 1])) {
        --end;
    }

    while (end > root && Sep(text[end - 1])) {
        --end;
    }

    return Path(text.substr(0, end), memory);
}

Path Path::filename(std::pmr::memory_resource* memory) const
{
    const auto root = Root(Text_);
    us start = size();

    while (start > root && !Sep(Text_[start - 1])) {
        --start;
    }

    return Path(PathView(Text_).substr(start), memory);
}

Path Path::extension() const
{
    auto name = filename();
    const auto dot = name.native().find_last_of('.');

    if (Dot(name) || DotDot(name) || dot == NativeString::npos || dot == 0) {
        return {};
    }

    return Path(PathView(name).substr(dot));
}

Path Path::stem() const
{
    auto name = filename();
    name.resize(name.size() - name.extension().size());
    return name;
}

Path& Path::replace_extension(const Path& suffix)
{
    Text_.resize(size() - extension().size());

    if (!suffix.empty() && suffix[0] != '.') {
        Text_.push_back('.');
    }

    Text_.append(suffix.native());
    return *this;
}

bool Path::has_root_path() const
{
    return Root(Text_) != 0;
}
bool Path::is_absolute() const
{
#ifdef _WIN32
    return RootName(Text_) != 0 && Root(Text_) > RootName(Text_);
#else
    return !empty() && Sep(Text_[0]);
#endif
}
Path& Path::make_preferred()
{
#ifdef _WIN32
    std::replace(Text_.begin(), Text_.end(), L'/', Separator);
#endif
    return *this;
}
Path operator/(const Path& base, const Path& suffix)
{
    if (suffix.is_absolute()) {
        return suffix;
    }
#ifdef _WIN32
    if (RootName(suffix.native()) != 0 &&
        PathView(suffix).substr(0, RootName(suffix)) != PathView(base).substr(0, RootName(base)))
    {
        return suffix;
    }

    if (Root(suffix) > RootName(suffix)) {
        Path result(PathView(base).substr(0, RootName(base)));
        result.append(PathView(suffix).substr(RootName(suffix)));
        return result;
    }
#endif
    Path result(base);

    if (!result.empty() && !Sep(result.back()) && result.size() != RootName(result)) {
        result.push_back(Separator);
    }

    result.append(PathView(suffix).substr(RootName(suffix)));
    return result;
}
Path Path::lexically_normal(std::pmr::memory_resource* memory) const
{
    if (empty()) {
        return Path(memory);
    }

    const us root = Root(Text_);
    const us rootName = RootName(Text_);
    auto parts = Parts(Text_, root, memory);
    const bool trailing = Sep(Text_.back()) || (!parts.empty() && (Dot(parts.back()) || DotDot(parts.back())));
    us kept = 0;

    for (auto part : parts) {
        if (Dot(part)) {
            continue;
        }

        if (DotDot(part)) {
            if (kept != 0 && !DotDot(parts[kept - 1])) {
                --kept;
            } else if (root == rootName) {
                parts[kept++] = part;
            }
        } else {
            parts[kept++] = part;
        }
    }

    Path result(PathView(Text_).substr(0, root), memory);
    result.Text_.reserve(Text_.size() + 1);
    result.make_preferred();

    for (us i = 0; i < kept; ++i) {
        if (!result.empty() && !Sep(result.back()) && result.size() != rootName) {
            result.push_back(Separator);
        }

        result.append(parts[i]);
    }

    if (result.empty()) {
        result.push_back('.');
    } else if (trailing && kept != 0 && !DotDot(parts[kept - 1]) && !Sep(result.back())) {
        result.push_back(Separator);
    }

    return result;
}

Path Path::lexically_relative(const Path& base, std::pmr::memory_resource* memory) const
{
    const auto aRoot = Root(Text_), bRoot = Root(base.native());

    if (PathView(Text_).substr(0, aRoot) != PathView(base).substr(0, bRoot)) {
        return Path(memory);
    }

    const auto a = Parts(Text_, aRoot, memory), b = Parts(base.native(), bRoot, memory);
    us common = 0;

    while (common < a.size() && common < b.size() && a[common] == b[common]) {
        ++common;
    }

    int parents = 0;

    for (us i = common; i < b.size(); ++i) {
        if (DotDot(b[i])) {
            --parents;
        } else if (!Dot(b[i])) {
            ++parents;
        }
    }

    if (parents < 0) {
        return Path(memory);
    }

    Path result(memory);
    result.Text_.reserve(static_cast<us>(parents) * 3 + Text_.size());

    for (int i = 0; i < parents; ++i) {
        if (!result.empty()) {
            result.push_back(Separator);
        }

        result.push_back('.');
        result.push_back('.');
    }

    for (us i = common; i < a.size(); ++i) {
        if (!result.empty()) {
            result.push_back(Separator);
        }

        result.append(a[i]);
    }

    if (result.empty()) {
        result.push_back('.');
    }

    return result;
}
Result<Path> CurrentPath(std::pmr::memory_resource& memory)
{
    Path path(&memory);
#ifdef _WIN32
    auto size = GetCurrentDirectoryW(0, nullptr);

    if (!size) {
        return {{}, {{static_cast<int>(GetLastError()), std::system_category()}, "current directory", {}}};
    }

    path.resize(size);
    auto used = GetCurrentDirectoryW(size, path.data());

    if (!used || used >= size) {
        return {{}, {{static_cast<int>(GetLastError()), std::system_category()}, "current directory", {}}};
    }

    path.resize(used);
#else
    us capacity = 256;

    for (;;) {
        path.resize(capacity);

        if (getcwd(path.data(), capacity)) {
            path.resize(std::strlen(path.c_str()));
            break;
        }

        if (errno != ERANGE) {
            return {{}, {{errno, std::generic_category()}, "current directory", {}}};
        }

        capacity *= 2;
    }
#endif
    return {std::move(path), {}};
}
Result<Path> Absolute(const Path& path, std::pmr::memory_resource& memory)
{
#ifdef _WIN32
    if (path.empty()) {
        return CurrentPath(memory);
    }

    const auto size = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);

    if (!size) {
        return {{}, {{static_cast<int>(GetLastError()), std::system_category()}, "absolute path", {}}};
    }

    Path result(&memory);
    result.resize(size);
    const auto used = GetFullPathNameW(path.c_str(), size, result.data(), nullptr);

    if (!used || used >= size) {
        return {{}, {{static_cast<int>(GetLastError()), std::system_category()}, "absolute path", {}}};
    }

    result.resize(used);
    return {std::move(result), {}};
#else
    if (path.is_absolute()) {
        return {Path(path, &memory), {}};
    }

    auto cwd = CurrentPath(memory);

    if (cwd.Error.Code) {
        return cwd;
    }

    return {Path(cwd.Value / path, &memory), {}};
#endif
}
Result<Path> Canonical(const Path& path, bool weak, std::pmr::memory_resource& memory)
{
    auto absolute = Absolute(path, memory);

    if (absolute.Error.Code) {
        return absolute;
    }

    auto current = std::move(absolute.Value);
    Path tail(&memory);
    for (;;) {
#ifdef _WIN32
        auto handle = CreateFileW(
            current.c_str(),
            0,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,
            OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS,
            nullptr);

        if (handle != INVALID_HANDLE_VALUE) {
            auto size = GetFinalPathNameByHandleW(handle, nullptr, 0, FILE_NAME_NORMALIZED);
            Path result(&memory);
            result.resize(size + 1);
            auto used = GetFinalPathNameByHandleW(
                handle,
                result.data(),
                static_cast<DWORD>(result.size()),
                FILE_NAME_NORMALIZED);
            const auto error = GetLastError();
            CloseHandle(handle);

            if (!used || used >= result.size()) {
                return {{}, {{static_cast<int>(error), std::system_category()}, "canonical path", {}}};
            }

            result.resize(used);

            if (PathView(result).starts_with(L"\\\\?\\UNC\\")) {
                result = Path(L"\\\\") / Path(PathView(result).substr(8));
            } else if (PathView(result).starts_with(L"\\\\?\\")) {
                result = Path(PathView(result).substr(4), &memory);
            }

            return {Path((tail.empty() ? result : result / tail).lexically_normal(), &memory), {}};
        }

        const std::error_code error(static_cast<int>(GetLastError()), std::system_category());
#else
        char* resolved = realpath(current.c_str(), nullptr);

        if (resolved) {
            Path result(resolved, &memory);
            std::free(resolved);
            return {Path((tail.empty() ? result : result / tail).lexically_normal(), &memory), {}};
        }

        const std::error_code error(errno, std::generic_category());
#endif
        if (!weak || (error != std::errc::no_such_file_or_directory && error != std::errc::not_a_directory)) {
            return {{}, {error, "canonical path", {}}};
        }

        auto parent = current.parent_path();

        if (parent == current || parent.empty()) {
            return {{}, {error, "canonical path", {}}};
        }

        tail = tail.empty() ? current.filename() : current.filename() / tail;
        current = std::move(parent);
    }
}

} // namespace fs
