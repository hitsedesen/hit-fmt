#pragma once

// Build with -DHIT_FMT_PROFILE=1 to print phase measurements at process exit.
#if defined(HIT_FMT_PROFILE) && HIT_FMT_PROFILE

#include "types.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <format>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#if defined(_M_X64) || defined(_M_IX86)
#include <intrin.h>
#elif defined(__x86_64__) || defined(__i386__)
#include <x86intrin.h>
#endif

namespace util::profile {

class Clock
{
public:
    static u64 Now()
    {
#if defined(_M_X64) || defined(_M_IX86) || defined(__x86_64__) || defined(__i386__)
        unsigned int auxiliary;
        return __rdtscp(&auxiliary);
#else
        return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count();
#endif
    }

    static double Seconds(u64 ticks)
    {
        return static_cast<double>(ticks) / Frequency();
    }

private:
    static double Frequency()
    {
#if defined(_M_X64) || defined(_M_IX86) || defined(__x86_64__) || defined(__i386__)
        static const double frequency = [] {
            auto sample = [] {
                std::pair<std::chrono::steady_clock::time_point, u64> best;
                auto bestSpan = std::numeric_limits<u64>::max();

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

struct Measurement
{
    u64 Hits = 0;
    u64 TotalTicks = 0;
    u64 OwnTicks = 0;
    u64 BytesProcessed = 0;
};

inline constexpr us MaxSlots = 256;
using Measurements = std::array<Measurement, MaxSlots>;

struct ThreadResult
{
    Measurements Phases{};
    ThreadResult* Next = nullptr;
};

// Slot zero accumulates the outermost scopes across all threads.
class Profiler
{
public:
    us Acquire(std::string_view name)
    {
        const auto slot = Next_.fetch_add(1, std::memory_order_relaxed);

        if (slot >= MaxSlots) {
            std::abort();
        }

        Phases_[slot].Name = name;
        return slot;
    }

    void Publish(ThreadResult* result)
    {
        result->Next = Completed_.load(std::memory_order_relaxed);

        while (!Completed_.compare_exchange_weak(
            result->Next,
            result,
            std::memory_order_release,
            std::memory_order_relaxed))
        { }
    }

    ~Profiler()
    {
        // The exit report owns completed snapshots after the workers have joined.
        const auto end = Next_.load(std::memory_order_relaxed);

        for (auto* result = Completed_.load(std::memory_order_acquire); result;) {
            for (us i = 0; i < end; ++i) {
                const auto& source = result->Phases[i];
                auto& target = Phases_[i].Stats;
                target.Hits += source.Hits;
                target.TotalTicks += source.TotalTicks;
                target.OwnTicks += source.OwnTicks;
                target.BytesProcessed += source.BytesProcessed;
            }

            auto* next = result->Next;
            delete result;
            result = next;
        }

        const auto total = Phases_[0].Stats.TotalTicks;

        if (!Phases_[0].Stats.Hits) {
            return;
        }

        std::sort(
            Phases_.begin() + 1,
            Phases_.begin() + end,
            [] (const Phase& left, const Phase& right) {
                if (left.Stats.OwnTicks != right.Stats.OwnTicks) {
                    return left.Stats.OwnTicks > right.Stats.OwnTicks;
                }

                return left.Name < right.Name;
            });

        const auto formatDuration = [] (double seconds) {
            if (seconds >= 1) {
                return std::format("{:.2f} s", seconds);
            }

            if (seconds >= 0.001) {
                return std::format("{:.2f} ms", seconds * 1000);
            }

            return std::format("{:.2f} µs", seconds * 1000000);
        };
        const auto formatTime = [&] (u64 ticks) {
            auto duration = formatDuration(Clock::Seconds(ticks));
            return total ? std::format("{} ({:.1f}%)", duration, 100.0 * ticks / total) : duration;
        };
        const auto formatSize = [] (double bytes, std::string_view suffix = {}) {
            constexpr std::array<std::string_view, 3> units{"KiB", "MiB", "GiB"};
            auto value = bytes / 1024;
            us unit = 0;

            while (value >= 1024 && unit + 1 < units.size()) {
                value /= 1024;
                ++unit;
            }

            return std::format("{:.{}f} {}{}", value, value < 1 ? 3 : 2, units[unit], suffix);
        };

        us phaseWidth = std::string_view("Phase").size();

        for (us i = 1; i < end; ++i) {
            if (Phases_[i].Stats.Hits) {
                phaseWidth = std::max(phaseWidth, Phases_[i].Name.size());
            }
        }

        constexpr auto row = "{:<{}}  {:>8}  {:>20}  {:>20}  {:>14}  {:>16}\n";
        std::cerr << "Profiled thread time: " << formatDuration(Clock::Seconds(total)) << "\n\n"
            << std::format(row, "Phase", phaseWidth, "Calls", "Self", "Total", "Input", "Throughput");

        for (us i = 1; i < end; ++i) {
            const auto& [name, phase] = Phases_[i];

            if (!phase.Hits) {
                continue;
            }

            const auto seconds = Clock::Seconds(phase.TotalTicks);
            const auto bytes = static_cast<double>(phase.BytesProcessed);
            std::cerr << std::format(
                row,
                name,
                phaseWidth,
                phase.Hits,
                formatTime(phase.OwnTicks),
                formatTime(phase.TotalTicks),
                bytes > 0 ? formatSize(bytes) : std::string("—"),
                bytes > 0 && seconds > 0 ? formatSize(bytes / seconds, "/s") : std::string("—"));
        }
    }

private:
    struct Phase
    {
        std::string_view Name;
        Measurement Stats;
    };

    std::array<Phase, MaxSlots> Phases_{};
    std::atomic<us> Next_{1};
    std::atomic<ThreadResult*> Completed_{nullptr};
};

inline constinit Profiler ProcessProfiler;

template <class Name>
inline const us PhaseSlot = [] {
    constexpr std::string_view name = Name{}();
    return ProcessProfiler.Acquire(name);
}();

struct ThreadMeasurements
{
    ThreadResult* Result;

    ~ThreadMeasurements()
    {
        ProcessProfiler.Publish(Result);
    }
};

class Timer
{
public:
    explicit Timer(us slot, u64 bytesProcessed = 0)
    {
        auto* local = Local_;

        if (!local) {
            // Register the exit callback only on this thread's first use.
            static thread_local const ThreadMeasurements owner{new ThreadResult};
            local = Local_ = owner.Result;
        }

        Phase_ = &local->Phases[slot];
        PreviousTotalTicks_ = Phase_->TotalTicks;
        Phase_->BytesProcessed += bytesProcessed;
        Parent_ = Active_;
        Active_ = this;
        StartTicks_ = Clock::Now();
    }

    Timer(const Timer& other) = delete;
    Timer& operator=(const Timer& other) = delete;

    ~Timer()
    {
        const auto elapsed = Clock::Now() - StartTicks_;
        ++Phase_->Hits;
        Phase_->OwnTicks += elapsed - ChildrenTicks_;
        Phase_->TotalTicks = PreviousTotalTicks_ + elapsed;
        Active_ = Parent_;

        if (Parent_) {
            Parent_->ChildrenTicks_ += elapsed;
        } else {
            auto& total = Local_->Phases[0];
            ++total.Hits;
            total.TotalTicks += elapsed;
        }
    }

private:
    inline static thread_local Timer* Active_ = nullptr;
    inline static constinit thread_local ThreadResult* Local_ = nullptr;
    Measurement* Phase_;
    Timer* Parent_;
    u64 PreviousTotalTicks_;
    u64 StartTicks_;
    u64 ChildrenTicks_ = 0;
};

} // namespace util::profile

#define HIT_FMT_PROFILE_JOIN_(a, b) a##b
#define HIT_FMT_PROFILE_JOIN(a, b) HIT_FMT_PROFILE_JOIN_(a, b)
#define HIT_FMT_PROFILE_PHASE_BANDWIDTH(name, bytesProcessed) \
    ::util::profile::Timer HIT_FMT_PROFILE_JOIN(phaseTimer_, __LINE__){ \
        ::util::profile::PhaseSlot<decltype([] { return name; })>, (bytesProcessed)}
#define HIT_FMT_PROFILE_PHASE(name) HIT_FMT_PROFILE_PHASE_BANDWIDTH(name, 0)

#else

#define HIT_FMT_PROFILE_PHASE(name) ((void)0)
#define HIT_FMT_PROFILE_PHASE_BANDWIDTH(name, bytesProcessed) ((void)0)

#endif
