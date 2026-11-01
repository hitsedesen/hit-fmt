#pragma once

// Compile-time opt-in diagnostics for corpus profiling.
// Build with -DHIT_FMT_PROFILE_PHASES=1 and set HIT_FMT_PROFILE_PHASES=1
// to print inclusive and exclusive phase times at process exit.
#ifdef HIT_FMT_PROFILE_PHASES

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <mutex>
#include <source_location>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#if defined(_M_X64) || defined(_M_IX86)
#include <intrin.h>
#elif defined(__x86_64__) || defined(__i386__)
#include <x86intrin.h>
#endif

namespace util::profile {

class Clock
{
public:
    static uint64_t Now()
    {
#if defined(_M_X64) || defined(_M_IX86) || defined(__x86_64__) || defined(__i386__)
        // Requires RDTSCP and a synchronized, invariant TSC.
        unsigned int auxiliary;
        return __rdtscp(&auxiliary);
#else
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
#endif
    }

    static double Seconds(uint64_t ticks)
    {
        return static_cast<double>(ticks) / Frequency();
    }

private:
    static double Frequency()
    {
#if defined(_M_X64) || defined(_M_IX86) || defined(__x86_64__) || defined(__i386__)
        // Calibrate only when elapsed seconds are requested, outside profiled work.
        static const double frequency = [] {
            auto sample = [] {
                std::pair<std::chrono::steady_clock::time_point, uint64_t> best;
                auto bestSpan = std::numeric_limits<uint64_t>::max();

                // Use the tightest bracket to limit error from preemption.
                for (int attempt = 0; attempt < 5; ++attempt) {
                    auto first = Now();
                    auto time = std::chrono::steady_clock::now();
                    auto last = Now();
                    auto span = last - first;

                    if (span < bestSpan) {
                        best = {time, first + span / 2};
                        bestSpan = span;
                    }
                }

                return best;
            };
            auto [start, first] = sample();
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            auto [end, last] = sample();
            return static_cast<double>(last - first) / std::chrono::duration<double>(end - start).count();
        }();
        return frequency;
#else
        return 1e9;
#endif
    }
};

inline bool Enabled()
{
    static const bool enabled = std::getenv("HIT_FMT_PROFILE_PHASES") != nullptr;
    return enabled;
}

struct Summary
{
    uint64_t Calls = 0;
    uint64_t InclusiveTicks = 0;
    uint64_t ExclusiveTicks = 0;
};

using Key = std::pair<std::string_view, uint32_t>;
using PhaseId = size_t;

inline constexpr PhaseId InvalidPhaseId = std::numeric_limits<PhaseId>::max();

struct PhaseKeyLess
{
    using is_transparent = void;

    bool operator()(const auto& left, const auto& right) const
    {
        auto order = std::string_view(left.first).compare(std::string_view(right.first));
        return order < 0 || (order == 0 && left.second < right.second);
    }
};

class Results
{
public:
    ~Results()
    {
        std::vector<std::pair<Key, Summary>> phases;
        phases.reserve(Phases_.size());

        for (const auto& [key, phase] : PhaseIds_) {
            const auto& value = Phases_[phase];

            if (value.Calls) {
                phases.push_back({{key.first, key.second}, value});
            }
        }

        if (phases.empty()) {
            return;
        }

        std::ranges::sort(
            phases,
            [] (const auto& left, const auto& right) {
                if (left.second.ExclusiveTicks != right.second.ExclusiveTicks) {
                    return left.second.ExclusiveTicks > right.second.ExclusiveTicks;
                }

                return left.first < right.first;
            });
        std::cerr << std::fixed << std::setprecision(6);

        for (const auto& [key, value] : phases) {
            std::cerr << "phase=" << key.first;

            if (key.second) {
                std::cerr << ':' << key.second;
            }

            std::cerr << " calls=" << value.Calls << " inclusive=" << Clock::Seconds(value.InclusiveTicks)
                << " exclusive=" << Clock::Seconds(value.ExclusiveTicks) << '\n';
        }
    }

    PhaseId Register(std::string_view name, uint32_t line)
    {
        std::lock_guard lock(Mutex_);
        auto found = PhaseIds_.find(Key{name, line});

        if (found != PhaseIds_.end()) {
            return found->second;
        }

        // Reserve before inserting the owning key so publishing the ID cannot
        // leave a registry entry without a corresponding summary on allocation failure.
        if (Phases_.size() == Phases_.capacity()) {
            Phases_.reserve(std::max<size_t>(8, Phases_.size() * 2));
        }

        auto phase = Phases_.size();
        PhaseIds_.emplace(std::pair{std::string(name), line}, phase);
        Phases_.emplace_back();
        return phase;
    }

    void Merge(std::span<const Summary> phases)
    {
        std::lock_guard lock(Mutex_);

        for (size_t phase = 0; phase < phases.size(); ++phase) {
            const auto& source = phases[phase];
            auto& target = Phases_[phase];
            target.Calls += source.Calls;
            target.InclusiveTicks += source.InclusiveTicks;
            target.ExclusiveTicks += source.ExclusiveTicks;
        }
    }

private:
    std::mutex Mutex_;
    // Names are owned here and only consulted during registration and reporting.
    std::map<std::pair<std::string, uint32_t>, PhaseId, PhaseKeyLess> PhaseIds_;
    std::vector<Summary> Phases_;
};

inline Results& GetResults()
{
    static Results results;
    return results;
}

inline PhaseId RegisterPhase(std::string_view name, uint32_t line = 0)
{
    return Enabled() ? GetResults().Register(name, line) : InvalidPhaseId;
}

struct PassSite
{
    PhaseId Pass = InvalidPhaseId;
    PhaseId Changed = InvalidPhaseId;
    uint32_t Line = 0;
};

inline PassSite RegisterPassSite(std::source_location location = std::source_location::current())
{
    auto line = location.line();
    return {RegisterPhase("formatter.pass", line), RegisterPhase("formatter.changed", line), line};
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

    LocalResults(const LocalResults& other) = delete;
    LocalResults& operator=(const LocalResults& other) = delete;

    void Count(PhaseId phase)
    {
        if (phase != InvalidPhaseId) {
            ++GetSummary(phase).Calls;
        }
    }

    void Record(PhaseId phase, uint64_t inclusive, uint64_t exclusive)
    {
        if (phase == InvalidPhaseId) {
            return;
        }

        auto& value = GetSummary(phase);
        ++value.Calls;
        value.InclusiveTicks += inclusive;
        value.ExclusiveTicks += exclusive;
    }

private:
    Summary& GetSummary(PhaseId phase)
    {
        if (phase >= Phases_.size()) {
            Grow(phase);
        }

        return Phases_[phase];
    }

    void Grow(PhaseId phase)
    {
        if (phase >= Phases_.capacity()) {
            Phases_.reserve(std::max({size_t{8}, phase + 1, Phases_.capacity() * 2}));
        }

        Phases_.resize(phase + 1);
    }

    std::vector<Summary> Phases_;
};

inline LocalResults& GetLocalResults()
{
    thread_local LocalResults local;
    return local;
}

class Scope
{
public:
    explicit Scope(PhaseId phase)
        : Phase_(phase)
    {
        if (Phase_ == InvalidPhaseId) {
            return;
        }

        Parent_ = Current_;
        Current_ = this;
        Start_ = Clock::Now();
    }

    ~Scope()
    {
        if (Phase_ == InvalidPhaseId) {
            return;
        }

        auto elapsed = Clock::Now() - Start_;
        Current_ = Parent_;

        if (Parent_) {
            Parent_->Children_ += elapsed;
        }

        GetLocalResults().Record(Phase_, elapsed, elapsed - Children_);
    }

    Scope(const Scope& other) = delete;
    Scope& operator=(const Scope& other) = delete;

private:
    inline static thread_local Scope* Current_ = nullptr;
    PhaseId Phase_;
    Scope* Parent_ = nullptr;
    uint64_t Start_ = 0;
    uint64_t Children_ = 0;
};

} // namespace util::profile

#define HIT_FMT_PROFILE_JOIN_(a, b) a##b
#define HIT_FMT_PROFILE_JOIN(a, b) HIT_FMT_PROFILE_JOIN_(a, b)
// Named macros retain one fixed phase per call site. Dynamic phases register
// once with RegisterPhase and pass the retained ID directly to Scope or Count.
#define HIT_FMT_PROFILE_ID_(name) \
    [] (::std::string_view phaseName) { \
        static const auto phase = ::util::profile::RegisterPhase(phaseName); \
        return phase; \
    }(name)
#define HIT_FMT_PROFILE_PHASE(name) \
    ::util::profile::Scope HIT_FMT_PROFILE_JOIN(phaseScope_, __LINE__){HIT_FMT_PROFILE_ID_(name)}
// Callers append HIT_FMT_PROFILE_PASS_SITE() to retain caller-specific IDs.
#define HIT_FMT_PROFILE_PASS_ARG \
    , const ::util::profile::PassSite& phaseLocation
#define HIT_FMT_PROFILE_PASS_SITE() \
    , [] () -> const ::util::profile::PassSite& { \
        static const auto site = ::util::profile::RegisterPassSite(); \
        return site; \
    }()
#define HIT_FMT_PROFILE_PASS() \
    ::util::profile::Scope phasePassScope{phaseLocation.Pass}
#define HIT_FMT_PROFILE_EVENT(name) \
    do { \
        if (::util::profile::Enabled()) { \
            ::util::profile::GetLocalResults().Count(HIT_FMT_PROFILE_ID_(name)); \
        } \
    } while (false)
#define HIT_FMT_PROFILE_EDIT(lastLine) \
    do { \
        lastLine = phaseLocation.Line; \
        if (::util::profile::Enabled()) \
            ::util::profile::GetLocalResults().Count(phaseLocation.Changed); \
    } while (false)

#else

#define HIT_FMT_PROFILE_PHASE(name) ((void)0)
#define HIT_FMT_PROFILE_PASS_ARG
#define HIT_FMT_PROFILE_PASS_SITE()
#define HIT_FMT_PROFILE_PASS() ((void)0)
#define HIT_FMT_PROFILE_EVENT(name) ((void)0)
#define HIT_FMT_PROFILE_EDIT(lastLine) ((void)0)

#endif
