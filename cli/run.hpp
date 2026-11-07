#pragma once

#include <span>

namespace cli {

int Run(std::span<const char* const> argv);

} // namespace cli
