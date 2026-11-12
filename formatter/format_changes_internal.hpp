#pragma once

#include "formatter.hpp"

#include <memory_resource>

namespace formatter::detail {

void BuildChangeMetadata(
    const parser::SyntaxTree& tree,
    FormatResult& result,
    bool detailed,
    std::pmr::memory_resource& scratch,
    std::span<const parser::SourceOffset> originalLines = {},
    bool originalBareCR = false,
    bool collectOutputLines = true);

} // namespace formatter::detail
