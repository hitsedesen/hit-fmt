#pragma once

#include "input.hpp"

#include <formatter/formatter.hpp>

namespace test {

inline formatter::FormatResult FormatInput(
    std::string_view source,
    const formatter::PreparedFormat& prepared,
    formatter::FormatRequest request = {})
{
    const Input input(source);
    return formatter::Format(input.View(), prepared, request);
}

inline std::string FormatWithStyle(
    std::string_view source,
    const formatter::Style& style,
    std::string_view inputFilePath = {})
{
    return FormatInput(source, formatter::PreparedFormat(style), {.InputFilePath = inputFilePath}).Text;
}

} // namespace test
