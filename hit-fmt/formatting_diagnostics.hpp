#pragma once

#include "terminal.hpp"

#include <formatter/formatter.hpp>

#include <cstddef>
#include <iosfwd>
#include <string_view>

namespace hit_fmt {

// Requires final change metadata collected with FormatRequest::Changes.
// Borrows the original source only for the duration of printing.
void PrintFormattingDiagnostics(
    std::ostream& output,
    std::string_view path,
    std::string_view source,
    const formatter::FormatResult& result,
    const Terminal& terminal,
    bool fixed = false);

} // namespace hit_fmt
