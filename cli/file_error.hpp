#pragma once

#include "error.hpp"

#include <fs/path.hpp>

#include <string>
#include <system_error>
#include <variant>

namespace cli {

struct SystemFailure
{
    std::string_view Operation;
    std::error_code Code;
};

struct Warning
{
    std::string_view Message;
};

struct FileFailure
{
    using Payload = std::variant<Error, SystemFailure, Warning>;

    FileFailure() = default;

    FileFailure(Error error)
        : Detail(std::move(error))
    { }

    FileFailure(SystemFailure error)
        : Detail(error)
    { }

    FileFailure(Warning warning)
        : Detail(warning)
    { }

    Payload Detail;
    // Generated paths may outlive discovery and worker arenas.
    std::basic_string<fs::Char> Path;

    explicit operator bool() const noexcept
    {
        const auto* error = std::get_if<Error>(&Detail);
        return !error || static_cast<bool>(*error);
    }

    us StorageSize() const;
};

inline FileFailure FileError(fs::PathView path, FileFailure error)
{
    error.Path.assign(path);
    return error;
}

std::ostream& operator<<(std::ostream& output, const FileFailure& error);

} // namespace cli
