#pragma once

#include <util/types.hpp>

#include <exception>
#include <iosfwd>
#include <regex>
#include <string_view>
#include <utility>
#include <variant>

namespace cli {

enum class ErrorCode;
struct ArgumentError;

struct InvalidExclusion
{
    std::variant<std::regex_constants::error_type, std::exception_ptr> Cause;
    std::string_view Argument;
};

struct Error
{
    using ArgumentDetail = std::pair<ErrorCode, std::string_view>;
    using Payload = std::variant<
        std::monostate,
        std::string_view,
        ArgumentDetail,
        std::regex_constants::error_type,
        std::exception_ptr,
        InvalidExclusion>;

    Error() = default;

    Error(Payload detail)
        : Detail(std::move(detail))
    { }

    Error(ArgumentError error);

    Payload Detail;

    explicit operator bool() const noexcept
    {
        return Detail.index() != 0;
    }

    us StorageSize() const;
};

inline Error MakeError(std::string_view message)
{
    return message.empty() ? Error{} : Error{message};
}

inline Error ExceptionError()
{
    return Error{std::current_exception()};
}

inline Error RegexError(std::regex_constants::error_type code)
{
    return Error{code};
}

Error InvalidExclusionError(Error error, std::string_view argument);
std::ostream& operator<<(std::ostream& output, const Error& error);

template <typename T, typename E = cli::Error>
struct Result
{
    T Value{};
    E Error{};
};

} // namespace cli
