#pragma once

#include <cstdio>
#include <cstdlib>

#define ASSERT(condition)                                                      \
    if (!(condition)) [[unlikely]] {                                           \
        std::fprintf(                                                         \
            stderr, "Assertion failed: %s, file %s, line %d\n",                 \
            #condition,                                                        \
            __FILE__,                                                          \
            __LINE__);                                                         \
        std::abort();                                                          \
    }
