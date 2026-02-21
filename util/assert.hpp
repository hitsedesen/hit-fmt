#pragma once

#include <cstdlib>
#include <format>
#include <iostream>

#define ASSERT(condition)                                                      \
    if (!(condition)) [[unlikely]] {                                           \
        std::cerr << std::format(                                              \
            "Assertion failed: {}, file {}, line {}\n",                        \
            #condition,                                                        \
            __FILE__,                                                          \
            __LINE__);                                                         \
        std::abort();                                                          \
    }
