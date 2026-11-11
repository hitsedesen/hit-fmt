#pragma once

#include "error.hpp"

#include <util/types.hpp>

#include <cstdio>
#include <iosfwd>
#include <string_view>

namespace cli {

struct Terminal
{
    explicit Terminal(bool colors = false, us width = 80)
        : Width(width)
    {
        if (colors) {
            Reset = "\033[0m";
            BoldRed = "\033[1;31m";
            BoldGreen = "\033[1;32m";
            BoldCyan = "\033[1;36m";
            BoldYellow = "\033[1;33m";
            Green = "\033[32m";
            Cyan = "\033[36m";
            Dim = "\033[2m";
        }
    }

    us Width;
    std::string_view Reset;
    std::string_view BoldRed;
    std::string_view BoldGreen;
    std::string_view BoldCyan;
    std::string_view BoldYellow;
    std::string_view Green;
    std::string_view Cyan;
    std::string_view Dim;
};

Terminal GetTerminal(FILE* stream = stdout);

void PrintError(std::ostream& output, const Error& error, const Terminal& terminal);

} // namespace cli
