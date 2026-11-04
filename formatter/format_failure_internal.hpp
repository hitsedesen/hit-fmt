#pragma once

#include <parser/syntax_tree.hpp>

#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace formatter::detail {

class FormatFailure
    : public std::runtime_error
{
public:
    explicit FormatFailure(std::string message, std::optional<parser::SourceRange> source = std::nullopt)
        : std::runtime_error(std::move(message))
        , Source(source)
    { }

    // Original-input coordinates only. Absent means mapping is untrustworthy.
    const std::optional<parser::SourceRange> Source;
};

} // namespace formatter::detail
