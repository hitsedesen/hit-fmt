#pragma once

#include "formatter.hpp"

#include <memory_resource>

namespace formatter::detail {
// Internal operation-only entry: analysis describes THIS original tree and
// prepared semantic options, with request.EditScope already applied exactly
// once. Sensitive views name that same configuration. Source/configuration and
// file/scratch arenas remain live throughout the call; analysis and persistent
// views live in file, never scratch (which this pipeline repeatedly releases).
// No additional original analysis/solve. Returned FormatResult owns storage.
[[nodiscard]] FormatResult FormatWithAnalysis(
    const parser::SyntaxTree& original,
    const PreparedFormat& prepared,
    const SourceAnalysis& safety,
    std::span<const std::string_view> sensitive,
    const FormatRequest& request,
    std::pmr::memory_resource& file,
    std::pmr::monotonic_buffer_resource& scratch);
} // namespace formatter::detail
