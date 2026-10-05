#pragma once

#include "formatter.hpp"

#include <memory_resource>

namespace formatter::detail {

// Safety describes the original tree and prepared settings, with EditScope
// already applied. Keep source, configuration and arenas alive for this call.
// Store persistent views in file, not scratch: scratch is reset during use.
// The result owns its storage.
[[nodiscard]] FormatResult FormatWithAnalysis(
    const parser::SyntaxTree& original,
    const PreparedFormat& prepared,
    const SourceAnalysis& safety,
    std::span<const std::string_view> sensitive,
    const FormatRequest& request,
    std::pmr::memory_resource& file,
    std::pmr::monotonic_buffer_resource& scratch);

} // namespace formatter::detail
