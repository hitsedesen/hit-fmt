#pragma once

#include "file_check.hpp"
#include "terminal.hpp"

#include <cstddef>
#include <iosfwd>
#include <span>
#include <string_view>

namespace hit_fmt {

void PrintFormattingDiagnostics(
    std::ostream& output,
    std::string_view path,
    std::string_view source,
    std::string_view formatted,
    const Terminal& terminal,
    bool fixed = false,
    std::span<const IncludeOrderChange> includeChanges = {});

} // namespace hit_fmt
