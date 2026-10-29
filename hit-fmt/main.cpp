#include "flags.hpp"
#include "run.hpp"
#include "terminal.hpp"

#include <exception>
#include <iostream>

int main(int argc, char* argv[])
{
    try {
        return hit_fmt::Run({argv, static_cast<size_t>(argc)});
    }
    catch (const hit_fmt::UsageError& error) {
        hit_fmt::PrintError(std::cerr, error.what(), hit_fmt::GetTerminal(stderr));
        return 1;
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
