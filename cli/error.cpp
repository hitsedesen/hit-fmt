#include "error.hpp"

#include "arguments.hpp"
#include "file_error.hpp"

#include <ostream>
#include <type_traits>

namespace cli {

namespace {

void PrintException(std::ostream& output, const std::exception_ptr& exception)
{
    try {
        std::rethrow_exception(exception);
    } catch (const std::exception& error) {
        output << error.what();
    }
}

us ExceptionSize(const std::exception_ptr& exception)
{
    try {
        std::rethrow_exception(exception);
    } catch (const std::exception& error) {
        return std::string_view(error.what()).size();
    }
}

} // namespace

Error::Error(ArgumentError error)
    : Detail(error ? Payload(ArgumentDetail{error.Code, error.Argument}) : Payload{})
{ }

Error InvalidExclusionError(Error error, std::string_view argument)
{
    if (auto* code = std::get_if<std::regex_constants::error_type>(&error.Detail)) {
        return Error{InvalidExclusion{*code, argument}};
    }
    return Error{InvalidExclusion{std::get<std::exception_ptr>(error.Detail), argument}};
}

us Error::StorageSize() const
{
    if (const auto* exception = std::get_if<std::exception_ptr>(&Detail)) {
        return ExceptionSize(*exception);
    }
    if (const auto* exclusion = std::get_if<InvalidExclusion>(&Detail)) {
        if (const auto* exception = std::get_if<std::exception_ptr>(&exclusion->Cause)) {
            return ExceptionSize(*exception);
        }
    }
    return 0;
}

std::ostream& operator<<(std::ostream& output, const Error& error)
{
    std::visit(
        [&] (const auto& detail) {
            using T = std::decay_t<decltype(detail)>;

            if constexpr (std::is_same_v<T, std::exception_ptr>) {
                PrintException(output, detail);
            } else if constexpr (std::is_same_v<T, std::regex_constants::error_type>) {
                output << std::regex_error(detail).what();
            } else if constexpr (std::is_same_v<T, InvalidExclusion>) {
                output << "invalid --exclude regex: ";

                if (!detail.Argument.empty()) {
                    output << detail.Argument << ": ";
                }

                std::visit(
                    [&] (const auto& cause) {
                        output << Error{cause};
                    },
                    detail.Cause);
            } else if constexpr (std::is_same_v<T, Error::ArgumentDetail>) {
                output << ArgumentError{detail.first, detail.second};
            } else if constexpr (!std::is_same_v<T, std::monostate>) {
                output << detail;
            }
        },
        error.Detail);
    return output;
}

us FileFailure::StorageSize() const
{
    const auto* error = std::get_if<Error>(&Detail);
    return Path.size() * sizeof(fs::Char) + (error ? error->StorageSize() : 0);
}

std::ostream& operator<<(std::ostream& output, const FileFailure& error)
{
    if (std::holds_alternative<Warning>(error.Detail)) {
        output << "warning: ";
    }
    if (!error.Path.empty()) {
        output << fs::Path(error.Path).string() << ": ";
    }

    std::visit(
        [&] (const auto& detail) {
            using T = std::decay_t<decltype(detail)>;

            if constexpr (std::is_same_v<T, SystemFailure>) {
                output << detail.Operation << ": " << detail.Code.message();
            } else if constexpr (std::is_same_v<T, Warning>) {
                output << detail.Message;
            } else {
                output << detail;
            }
        },
        error.Detail);
    return output;
}

} // namespace cli
