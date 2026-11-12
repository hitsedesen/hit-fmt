#pragma once

#include "error.hpp"
#include "terminal.hpp"

#include <formatter/formatter.hpp>

#include <cstddef>
#include <iosfwd>
#include <string_view>

namespace cli {

Error PrintFormattingLocations(
    std::ostream& output,
    std::string_view path,
    std::string_view source,
    const formatter::FormatResult& result);

// Requires final change metadata collected with FormatRequest::Changes.
// Borrows the original source only for the duration of printing.
Error PrintFormattingDiagnostics(
    std::ostream& output,
    std::string_view path,
    std::string_view source,
    const formatter::FormatResult& result,
    const Terminal& terminal,
    bool fixed = false);

} // namespace cli
