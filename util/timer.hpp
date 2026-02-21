#pragma once

#include <chrono>

namespace util {

template<typename Clock = std::chrono::steady_clock>
class Timer
{
public:
    Timer()
        : Start_(Clock::now())
    { }

    Clock::duration Elapsed() const
    {
        return Clock::now() - Start_;
    }

private:
    Clock::time_point Start_;
};

} // namespace util
