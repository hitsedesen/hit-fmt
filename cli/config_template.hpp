#pragma once

#include <iosfwd>

namespace cli {

enum class ConfigPreset
{
    Default,
    YTsaurus,
    Google,
    LLVM
};

// Print the preserving default template or a built-in example configuration.
void PrintConfig(std::ostream& output, ConfigPreset preset = ConfigPreset::Default);

} // namespace cli
