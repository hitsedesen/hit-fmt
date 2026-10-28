#include "run.hpp"

#include <exception>
#include <iostream>

int main(int argc, char* argv[])
{
    try {
        return hit_fmt::Run(argc, argv);
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
