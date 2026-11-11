#pragma once

#include "terminal.hpp"

#include <iosfwd>

namespace cli {

void PrintConfigHelp(std::ostream& output, const Terminal& terminal);

} // namespace cli
