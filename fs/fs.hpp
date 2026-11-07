#pragma once

#include "path.hpp"

#include <util/memory.hpp>
#include <util/types.hpp>

#include <cstdint>
#include <memory_resource>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dirent.h>
#endif

namespace fs {

struct Error
{
    std::error_code Code;
    const char* Operation = nullptr;
    // Traversal errors attach generated paths using the caller's arena.
    Path Context;
};

template <typename T>
struct Result
{
    T Value{};
    fs::Error Error{};
};

[[nodiscard]] Result<std::pmr::string> ReadFile(
    const Char* path,
    us maxSize = 0,
    us padding = 0,
    std::pmr::memory_resource& memory = *std::pmr::get_default_resource());

[[nodiscard]] inline Result<std::pmr::string> ReadFile(
    const Path& path,
    us maxSize = 0,
    us padding = 0,
    std::pmr::memory_resource& memory = *std::pmr::get_default_resource())
{
    return ReadFile(path.c_str(), maxSize, padding, memory);
}

[[nodiscard]] Result<us> WriteFile(const Char* path, std::string_view contents);

[[nodiscard]] inline Result<us> WriteFile(const Path& path, std::string_view contents)
{
    return WriteFile(path.c_str(), contents);
}

[[nodiscard]] Result<Path> CurrentPath(std::pmr::memory_resource& memory = *std::pmr::get_default_resource());
[[nodiscard]] Result<Path> Absolute(
    const Path& path,
    std::pmr::memory_resource& memory = *std::pmr::get_default_resource());
[[nodiscard]] Result<Path> Canonical(
    const Path& path,
    bool weak = false,
    std::pmr::memory_resource& memory = *std::pmr::get_default_resource());
[[nodiscard]] Result<std::uintmax_t> FileSize(const Char* path);

[[nodiscard]] inline Result<std::uintmax_t> FileSize(const Path& path)
{
    return FileSize(path.c_str());
}

struct Entry
{
    PathView Name;
    bool Directory = false;
    bool Regular = false;
    bool Symlink = false;
};

[[nodiscard]] Result<Entry> Status(const Char* path);

[[nodiscard]] inline Result<Entry> Status(const Path& path)
{
    return Status(path.c_str());
}

class Directory
{
public:
    Directory() = default;
    [[nodiscard]] static Result<Directory> Open(
        const Path& path,
        std::pmr::memory_resource& scratch = *std::pmr::get_default_resource());
    ~Directory();
    Directory(const Directory& other) = delete;
    Directory& operator=(const Directory& other) = delete;
    Directory(Directory&& other) noexcept;
    Directory& operator=(Directory&& other) = delete;

    // The returned name is borrowed until the next call or destruction.
    [[nodiscard]] Result<bool> Next(Entry& entry);

private:
#ifdef _WIN32
    HANDLE Handle_ = INVALID_HANDLE_VALUE;
    WIN32_FIND_DATAW Data_{};
    bool First_ = false;
#else
    DIR* Handle_ = nullptr;
#endif
};

enum class WalkAction
{
    Continue,
    SkipDirectory,
    Stop,
};

// Path and entry names are borrowed only for the duration of the visitor.
template <typename Visitor>
Result<bool> Walk(PathView root, Visitor&& visit, std::pmr::memory_resource& memory = *std::pmr::get_default_resource())
{
    Path current(root, &memory);
    Arena scratch;
    struct Frame
    {
        Directory DirectoryHandle;
        us PathLength;
    };

    Vector<Frame> stack(&memory);
    auto opened = Directory::Open(current, scratch);

    if (opened.Error.Code) {
        return {false, {opened.Error.Code, opened.Error.Operation, Path(current, &memory)}};
    }

    stack.push_back({std::move(opened.Value), current.size()});
    scratch.release();
    while (!stack.empty()) {
        current.resize(stack.back().PathLength);
        Entry entry;
        auto next = stack.back().DirectoryHandle.Next(entry);

        if (next.Error.Code) {
            return {false, {next.Error.Code, next.Error.Operation, Path(current, &memory)}};
        }

        if (!next.Value) {
            stack.pop_back();
            continue;
        }
        if (!current.empty() && current.back() != Separator
#ifdef _WIN32
            && current.back() != '/' && !(current.size() == 2 && current[1] == ':')
#endif
        )
        {
            current.push_back(Separator);
        }
        current.append(entry.Name);
        const auto action = visit(PathView(current), entry);

        if (action == WalkAction::Stop) {
            return {false, {}};
        }

        if (entry.Directory && !entry.Symlink && action != WalkAction::SkipDirectory) {
            auto child = Directory::Open(current, scratch);

            if (child.Error.Code) {
                return {false, {child.Error.Code, child.Error.Operation, Path(current, &memory)}};
            }

            stack.push_back({std::move(child.Value), current.size()});
            scratch.release();
        }
    }
    return {true, {}};
}

} // namespace fs
