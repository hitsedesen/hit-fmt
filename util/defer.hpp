#pragma once

#include <utility>

namespace util {

template <typename Function>
class ScopeExit
{
public:
    explicit ScopeExit(Function function)
        : Function_(std::move(function))
    { }

    ScopeExit(const ScopeExit& other) = delete;
    ScopeExit& operator=(const ScopeExit& other) = delete;

    ~ScopeExit()
    {
        Function_();
    }

private:
    Function Function_;
};

} // namespace util

#define UTIL_DEFER_JOIN_(a, b) a##b
#define UTIL_DEFER_JOIN(a, b) UTIL_DEFER_JOIN_(a, b)
#define DEFER(...) const ::util::ScopeExit UTIL_DEFER_JOIN(defer_, __COUNTER__){[&] { __VA_ARGS__; }}
