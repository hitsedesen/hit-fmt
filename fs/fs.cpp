#include "fs.hpp"

#include <util/types.hpp>

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <limits>
#include <system_error>
#include <utility>

#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>

#include <sys/stat.h>
#endif

namespace fs {

namespace {

// A thin wrapper for OS-specific behaviour.
class File
{
public:
    std::error_code Open(const Char* path, bool writing)
    {
#ifdef _WIN32
        Handle_ = CreateFileW(
            path,
            writing ? GENERIC_WRITE : GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,
            writing ? CREATE_ALWAYS : OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);

        if (Handle_ == INVALID_HANDLE_VALUE) {
            const auto error = GetLastError();
            return {static_cast<int>(error), std::system_category()};
        }
#else
        Handle_ = open(path, writing ? O_WRONLY | O_CREAT | O_TRUNC : O_RDONLY, 0666);

        if (Handle_ < 0) {
            const auto error = errno;
            return {error, std::generic_category()};
        }
#endif
        return {};
    }

    File() = default;
    ~File()
    {
#ifdef _WIN32
        if (Handle_ != INVALID_HANDLE_VALUE) {
            CloseHandle(Handle_);
        }
#else
        if (Handle_ >= 0) {
            close(Handle_);
        }
#endif
    }

    File(const File& other) = delete;
    File& operator=(const File& other) = delete;

    [[nodiscard]] Result<std::uintmax_t> Size() const
    {
#ifdef _WIN32
        LARGE_INTEGER info{};

        if (!GetFileSizeEx(Handle_, &info)) {
            const auto error = GetLastError();
            return {{}, {{static_cast<int>(error), std::system_category()}, "file size", {}}};
        }

        const auto end = info.QuadPart;
#else
        struct stat info{};

        if (fstat(Handle_, &info) != 0) {
            const auto error = errno;
            return {{}, {{error, std::generic_category()}, "file size", {}}};
        }

        const auto end = info.st_size;
#endif
        if (end < 0) {
            return {{}, {std::make_error_code(std::errc::io_error), "file size", {}}};
        }

        return {static_cast<std::uintmax_t>(end), {}};
    }

    [[nodiscard]] Result<us> Read(char* data, us size) const
    {
#ifdef _WIN32
        DWORD count = 0;
        const auto chunk = static_cast<DWORD>(std::min<us>(size, MAXDWORD));

        if (!::ReadFile(Handle_, data, chunk, &count, nullptr)) {
            const auto error = GetLastError();
            return {{}, {{static_cast<int>(error), std::system_category()}, "read file", {}}};
        }
#else
        const auto chunk = std::min<us>(size, std::numeric_limits<ssize_t>::max());
        ssize_t count;
        do {
            count = read(Handle_, data, chunk);
        } while (count < 0 && errno == EINTR);

        if (count < 0) {
            const auto error = errno;
            return {{}, {{error, std::generic_category()}, "read file", {}}};
        }
#endif
        return {static_cast<us>(count), {}};
    }

    [[nodiscard]] Result<us> Write(const char* data, us size) const
    {
#ifdef _WIN32
        DWORD count = 0;
        const auto chunk = static_cast<DWORD>(std::min<us>(size, MAXDWORD));

        if (!::WriteFile(Handle_, data, chunk, &count, nullptr)) {
            const auto error = GetLastError();
            return {{}, {{static_cast<int>(error), std::system_category()}, "write file", {}}};
        }
#else
        const auto chunk = std::min<us>(size, std::numeric_limits<ssize_t>::max());
        ssize_t count;
        do {
            count = write(Handle_, data, chunk);
        } while (count < 0 && errno == EINTR);

        if (count < 0) {
            const auto error = errno;
            return {{}, {{error, std::generic_category()}, "write file", {}}};
        }
#endif
        return {static_cast<us>(count), {}};
    }

    std::error_code Close()
    {
#ifdef _WIN32
        const auto handle = std::exchange(Handle_, INVALID_HANDLE_VALUE);

        if (!CloseHandle(handle)) {
            const auto error = GetLastError();
            return {static_cast<int>(error), std::system_category()};
        }
#else
        const auto handle = std::exchange(Handle_, -1);

        if (close(handle) != 0) {
            const auto error = errno;
            return {error, std::generic_category()};
        }
#endif
        return {};
    }

private:
#ifdef _WIN32
    HANDLE Handle_ = INVALID_HANDLE_VALUE;
#else
    int Handle_ = -1;
#endif
};

} // namespace

Result<std::uintmax_t> FileSize(const Char* path)
{
    File file;

    if (auto error = file.Open(path, false)) {
        return {{}, {error, "open file", {}}};
    }

    return file.Size();
}

Result<Entry> Status(const Char* path)
{
#ifdef _WIN32
    const auto attributes = GetFileAttributesW(path);

    if (attributes == INVALID_FILE_ATTRIBUTES) {
        return {{}, {{static_cast<int>(GetLastError()), std::system_category()}, "file status", {}}};
    }

    return {
        {{}, (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0, (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0, false},
        {},
    };
#else
    struct stat info{};

    if (stat(path, &info) != 0) {
        return {{}, {{errno, std::generic_category()}, "file status", {}}};
    }

    return {{{}, S_ISDIR(info.st_mode), S_ISREG(info.st_mode), false}, {}};
#endif
}

Result<std::pmr::string> ReadFile(const Char* path, us maxSize, us padding, std::pmr::memory_resource& memory)
{
    File file;

    if (auto error = file.Open(path, false)) {
        return {{}, {error, "open file", {}}};
    }

    const auto sizeResult = file.Size();

    if (sizeResult.Error.Code) {
        return {{}, {sizeResult.Error.Code, sizeResult.Error.Operation, {}}};
    }

    const auto size = sizeResult.Value;

    if (maxSize != 0 && size > maxSize) {
        return {{}, {std::make_error_code(std::errc::file_too_large), "file size", {}}};
    }

    if (size > std::numeric_limits<us>::max() || padding > std::numeric_limits<us>::max() - size) {
        return {{}, {std::make_error_code(std::errc::value_too_large), "file size and padding", {}}};
    }

    std::pmr::string buffer(static_cast<us>(size) + padding, '\0', &memory);

    us offset = 0;

    while (offset < size) {
        const auto read = file.Read(buffer.data() + offset, static_cast<us>(size - offset));

        if (read.Error.Code) {
            return {{}, read.Error};
        }

        if (read.Value == 0) {
            return {{}, {std::make_error_code(std::errc::io_error), "unexpected end of file", {}}};
        }

        offset += read.Value;
    }

    return {std::move(buffer), {}};
}

Result<us> WriteFile(const Char* path, std::string_view contents)
{
    File file;

    if (auto error = file.Open(path, true)) {
        return {0, {error, "open file", {}}};
    }

    us offset = 0;

    while (offset < contents.size()) {
        const auto written = file.Write(contents.data() + offset, contents.size() - offset);

        if (written.Error.Code) {
            return {offset, written.Error};
        }

        if (written.Value == 0) {
            return {offset, {std::make_error_code(std::errc::io_error), "incomplete file write", {}}};
        }

        offset += written.Value;
    }

    if (auto error = file.Close()) {
        return {offset, {error, "close file", {}}};
    }

    return {offset, {}};
}

Result<Directory> Directory::Open(const Path& path, std::pmr::memory_resource& scratch)
{
    Directory directory;
#ifdef _WIN32
    Path pattern(path, &scratch);

    if (!pattern.empty() && pattern.back() != L'\\' && pattern.back() != L'/' &&
        !(pattern.size() == 2 && pattern[1] == L':'))
    {
        pattern.push_back(L'\\');
    }

    pattern.push_back(L'*');
    directory.Handle_ = FindFirstFileW(pattern.c_str(), &directory.Data_);

    if (directory.Handle_ == INVALID_HANDLE_VALUE) {
        const auto error = GetLastError();

        if (error != ERROR_FILE_NOT_FOUND) {
            return {{}, {{static_cast<int>(error), std::system_category()}, "open directory", {}}};
        }
    } else {
        directory.First_ = true;
    }
#else
    (void)scratch;
    directory.Handle_ = opendir(path.c_str());

    if (!directory.Handle_) {
        const auto error = errno;
        return {{}, {{error, std::generic_category()}, "open directory", {}}};
    }
#endif
    return {std::move(directory), {}};
}

Directory::~Directory()
{
#ifdef _WIN32
    if (Handle_ != INVALID_HANDLE_VALUE) {
        FindClose(Handle_);
    }
#else
    if (Handle_) {
        closedir(Handle_);
    }
#endif
}

Directory::Directory(Directory&& other) noexcept
#ifdef _WIN32
    : Handle_(std::exchange(other.Handle_, INVALID_HANDLE_VALUE))
    , Data_(other.Data_)
    , First_(other.First_)
#else
    : Handle_(std::exchange(other.Handle_, nullptr))
#endif
{ }

Result<bool> Directory::Next(Entry& entry)
{
    for (;;) {
#ifdef _WIN32
        if (Handle_ == INVALID_HANDLE_VALUE) {
            return {false, {}};
        }

        if (First_) {
            First_ = false;
        } else if (!FindNextFileW(Handle_, &Data_)) {
            const auto error = GetLastError();

            if (error == ERROR_NO_MORE_FILES) {
                return {false, {}};
            }

            return {{}, {{static_cast<int>(error), std::system_category()}, "read directory", {}}};
        }

        const PathView name(Data_.cFileName);

        if (name == L"." || name == L"..") {
            continue;
        }

        const bool symlink = (Data_.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 &&
            (Data_.dwReserved0 == IO_REPARSE_TAG_SYMLINK || Data_.dwReserved0 == IO_REPARSE_TAG_MOUNT_POINT);
        entry = {
            name,
            (Data_.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0,
            (Data_.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0,
            symlink,
        };
#else
        if (!Handle_) {
            return {false, {}};
        }

        errno = 0;
        auto* current = readdir(Handle_);

        if (!current) {
            if (errno != 0) {
                return {{}, {{errno, std::generic_category()}, "read directory", {}}};
            }

            return {false, {}};
        }

        const PathView name(current->d_name);

        if (name == "." || name == "..") {
            continue;
        }
#if defined(DT_DIR) && (defined(_DIRENT_HAVE_D_TYPE) || defined(__APPLE__) || defined(__FreeBSD__))
        if (current->d_type == DT_DIR || current->d_type == DT_REG) {
            entry = {name, current->d_type == DT_DIR, current->d_type == DT_REG, false};
            return {true, {}};
        }

        if (current->d_type != DT_LNK && current->d_type != DT_UNKNOWN) {
            entry = {name, false, false, false};
            return {true, {}};
        }
#endif
        struct stat info{};

        if (fstatat(dirfd(Handle_), current->d_name, &info, AT_SYMLINK_NOFOLLOW) != 0) {
            if (errno == ENOENT || errno == ENOTDIR) {
                entry = {name, false, false, false};
                return {true, {}};
            }

            return {{}, {{errno, std::generic_category()}, "stat directory entry", {}}};
        }

        const bool symlink = S_ISLNK(info.st_mode);

        if (symlink && fstatat(dirfd(Handle_), current->d_name, &info, 0) != 0) {
            if (errno != ENOENT && errno != ENOTDIR) {
                return {{}, {{errno, std::generic_category()}, "stat symbolic link", {}}};
            }

            entry = {name, false, false, true};
        } else {
            entry = {name, S_ISDIR(info.st_mode), S_ISREG(info.st_mode), symlink};
        }
#endif
        return {true, {}};
    }
}

} // namespace fs
