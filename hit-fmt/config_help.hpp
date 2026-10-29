#pragma once

#include "terminal.hpp"

#include <iosfwd>

namespace hit_fmt {

void PrintConfigHelp(std::ostream& output, const Terminal& terminal);

} // namespace hit_fmt
