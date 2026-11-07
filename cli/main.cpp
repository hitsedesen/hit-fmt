#include "run.hpp"
#include "terminal.hpp"

#include <util/assert.hpp>
#include <util/types.hpp>

#include <exception>
#include <iostream>

int main(int argc, char* argv[])
{
    ASSERT(argc > 0);
    ASSERT(argv != nullptr);

    try {
        return cli::Run({argv, argc});
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
