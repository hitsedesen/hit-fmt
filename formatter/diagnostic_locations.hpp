#pragma once

#include "diagnostics.hpp"
#include "formatter.hpp"

#include <memory_resource>

namespace formatter::detail {

util::Position OriginalPosition(const parser::SyntaxTree& tree, TokenAnchor anchor);
void MapWidthLocations(const parser::SyntaxTree& tree, FormatResult& result, std::pmr::memory_resource& scratch);

} // namespace formatter::detail
