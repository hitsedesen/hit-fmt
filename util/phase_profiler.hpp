#pragma once

// Compile-time opt-in diagnostics for corpus profiling.
// Build with -DHIT_FMT_PROFILE_PHASES=1 and set HIT_FMT_PROFILE_PHASES=1
// to print inclusive and exclusive phase times at process exit.
#ifdef HIT_FMT_PROFILE_PHASES

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <map>
#include <mutex>
#include <source_location>
#include <string_view>
#include <vector>

namespace util::profile {
using Clock = std::chrono::steady_clock;
inline bool Enabled()
{
    static const bool enabled =
        std::getenv("HIT_FMT_PROFILE_PHASES") != nullptr;
    return enabled;
}
struct Summary
{
    uint64_t Calls = 0;
    std::chrono::nanoseconds Inclusive{};
    std::chrono::nanoseconds Exclusive{};
};
using Key = std::pair<std::string_view, uint32_t>;

class Results
{
public:
    ~Results()
    {
        if (Phases_.empty()) {
            return;
        }
        std::vector<std::pair<Key, Summary>> phases(
            Phases_.begin(), Phases_.end());
        std::ranges::sort(
            phases,
            [] (const auto& left, const auto& right) {
                return left.second.Exclusive > right.second.Exclusive;
            });
        std::cerr << std::fixed << std::setprecision(6);
        for (const auto& [key, value] : phases) {
            std::cerr << "phase=" << key.first;
            if (key.second) {
                std::cerr << ':' << key.second;
            }
            std::cerr << " calls=" << value.Calls << " inclusive="
                      << std::chrono::duration<double>(value.Inclusive).count()
                      << " exclusive="
                      << std::chrono::duration<double>(value.Exclusive).count()
                      << '\n';
        }
    }

    void Merge(const std::map<Key, Summary>& phases)
    {
        std::lock_guard lock(Mutex_);
        for (const auto& [key, source] : phases) {
            auto& target = Phases_[key];
            target.Calls += source.Calls;
            target.Inclusive += source.Inclusive;
            target.Exclusive += source.Exclusive;
        }
    }
private:
    std::mutex Mutex_;
    std::map<Key, Summary> Phases_;
};
inline Results& GetResults()
{
    static Results results;
    return results;
}
class LocalResults
{
public:
    LocalResults()
    {
        // Construct the process summary first, so the main thread merges
        // its local data before the process summary is destroyed.
        (void)GetResults();
    }
    ~LocalResults()
    {
        if (!Phases_.empty()) {
            GetResults().Merge(Phases_);
        }
    }
    void Count(std::string_view name, uint32_t line = 0)
    {
        ++Phases_[{name, line}].Calls;
    }
    void Record(
        std::string_view name,
        uint32_t line,
        std::chrono::nanoseconds inclusive,
        std::chrono::nanoseconds exclusive)
    {
        auto& value = Phases_[{name, line}];
        ++value.Calls;
        value.Inclusive += inclusive;
        value.Exclusive += exclusive;
    }
private:
    std::map<Key, Summary> Phases_;
};
inline LocalResults& GetLocalResults()
{
    thread_local LocalResults local;
    return local;
}

class Scope
{
public:
    explicit Scope(
        std::string_view name,
        uint32_t line = 0): Name_(name), Line_(line)
    {
        if (!Enabled()) {
            return;
        }
        Parent_ = Current_;
        Current_ = this;
        Start_ = Clock::now();
        Active_ = true;
    }

    ~Scope()
    {
        if (!Active_) {
            return;
        }
        auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
            Clock::now() - Start_);
        Current_ = Parent_;
        if (Parent_) {
            Parent_->Children_ += elapsed;
        }
        GetLocalResults().Record(Name_, Line_, elapsed, elapsed - Children_);
    }

    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
private:
    inline static thread_local Scope* Current_ = nullptr;
    std::string_view Name_;
    uint32_t Line_ = 0;
    Scope* Parent_ = nullptr;
    Clock::time_point Start_{};
    std::chrono::nanoseconds Children_{};
    bool Active_ = false;
};
} // namespace util::profile

#define HIT_FMT_PROFILE_JOIN_(a, b) a##b
#define HIT_FMT_PROFILE_JOIN(a, b) HIT_FMT_PROFILE_JOIN_(a, b)
#define HIT_FMT_PROFILE_PHASE(name) \
    ::util::profile::Scope HIT_FMT_PROFILE_JOIN(phaseScope_, __LINE__){name}
#define HIT_FMT_PROFILE_PASS_ARG \
    , std::source_location phaseLocation = std::source_location::current()
#define HIT_FMT_PROFILE_PASS() \
    ::util::profile::Scope phasePassScope{"formatter.pass", \
        phaseLocation.line()}
#define HIT_FMT_PROFILE_EVENT(name) \
    do { \
        if (::util::profile::Enabled()) \
            ::util::profile::GetLocalResults().Count(name); \
    } while (false)
#define HIT_FMT_PROFILE_EDIT(lastLine) \
    do { \
        lastLine = phaseLocation.line(); \
        if (::util::profile::Enabled()) \
            ::util::profile::GetLocalResults().Count("formatter.changed", \
                phaseLocation.line()); \
    } while (false)

#else

#define HIT_FMT_PROFILE_PHASE(name) ((void)0)
#define HIT_FMT_PROFILE_PASS_ARG
#define HIT_FMT_PROFILE_PASS() ((void)0)
#define HIT_FMT_PROFILE_EVENT(name) ((void)0)
#define HIT_FMT_PROFILE_EDIT(lastLine) ((void)0)

#endif
