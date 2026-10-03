#pragma once

#include <iosfwd>

namespace hit_fmt {
// Print a complete, parseable configuration with formatting rules disabled.
void PrintPreserveConfig(std::ostream& output);
} // namespace hit_fmt
