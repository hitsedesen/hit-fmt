#pragma once

#include "file_batch.hpp"

#include <algorithm>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <exception>
#include <filesystem>
#include <mutex>
#include <optional>
#include <ostream>
#include <string>
#include <system_error>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace hit_fmt {

struct FileResult
{
    std::string Output;
    std::string Error;
    size_t Mismatches = 0;
    bool Failed = false;
};

// Discovery yields file paths through visit(path). Each worker owns a
// resolver; only the caller prints results, in discovery order.
template <typename Discover, typename MakeResolver, typename Process,
    typename Print>
bool RunParallelFiles(
    size_t threadCount,
    Discover&& discover,
    MakeResolver&& makeResolver,
    Process&& process,
    Print&& print,
    std::ostream& errors)
{
    constexpr size_t BufferedOutputLimit = 16 * 1024 * 1024;
    constexpr size_t BatchFileLimit = 4;
    constexpr size_t BatchByteLimit = 256 * 1024;
    const auto pendingFileLimit = std::max(threadCount, size_t{128});

    using Resolver = std::invoke_result_t<MakeResolver&>;
    // Deque keeps resolver addresses stable as workers join.
    std::deque<Resolver> resolvers;
    std::mutex mutex;
    std::condition_variable ready;
    std::deque<FileBatch> pending;
    std::deque<std::optional<FileResult>> results;
    // Bound queued paths and completed diagnostics so a slow earlier file
    // cannot let later workers run arbitrarily far ahead of ordered output.
    size_t bufferedBytes = 0;
    size_t processing = 0;
    size_t printed = 0;
    bool discoveryDone = false;
    bool stop = false;
    std::optional<std::string> discoveryError;
    {
        std::vector<std::jthread> workers;
        auto launchWorker = [&] {
            resolvers.emplace_back(makeResolver());
            auto* resolver = &resolvers.back();
            workers.emplace_back([&, resolver] {
                std::vector<FileResult> completed;
                completed.reserve(BatchFileLimit);
                for (;;) {
                    FileBatch batch;
                    {
                        std::unique_lock lock(mutex);
                        ready.wait(
                                lock,
                                [&] {
                                    bool canClaim =
                                        !pending.empty() &&
                                            bufferedBytes < BufferedOutputLimit;
                                    return stop || canClaim ||
                                        (discoveryDone && pending.empty());
                                });
                        if (stop || (discoveryDone && pending.empty())) {
                            return;
                        }
                        batch = std::move(pending.front());
                        pending.pop_front();
                        ++processing;
                    }
                    completed.clear();
                    for (const auto& path : batch.Paths) {
                        completed.push_back(process(path, *resolver));
                    }
                    {
                        std::lock_guard lock(mutex);
                        for (size_t index = 0;
                            index < completed.size();
                            ++index)
                        {
                            auto& result = completed[index];
                            bufferedBytes +=
                                result.Output.size() + result.Error.size();
                            // Batch indices are global; printed results have
                            // already left this deque.
                            results[batch.FirstIndex + index - printed] =
                                std::move(result);
                        }
                        --processing;
                    }
                    ready.notify_all();
                }
            });
        };
        std::jthread discovery;
        try {
            discovery = std::jthread([&] {
                try {
                    FileBatch batch;
                    batch.Paths.reserve(BatchFileLimit);
                    size_t batchBytes = 0;
                    size_t nextIndex = 0;
                    bool first = true;
                    auto flush = [&] {
                        if (batch.Paths.empty()) {
                            return true;
                        }
                        std::unique_lock lock(mutex);
                        ready.wait(
                                lock,
                                [&] {
                                    return stop ||
                                        results.size() + batch.Paths.size() <=
                                            pendingFileLimit;
                                });
                        if (stop) {
                            return false;
                        }
                        for (size_t i = 0; i < batch.Paths.size(); ++i) {
                            results.emplace_back();
                        }
                        pending.push_back(std::move(batch));
                        lock.unlock();
                        ready.notify_all();
                        return true;
                    };
                    if (discover([&] (const auto& path) {
                        std::error_code error;
                        auto size = std::filesystem::file_size(path, error);
                        size_t estimatedBytes = error
                                ? BatchByteLimit
                                : static_cast<size_t>(
                                    std::min<std::uintmax_t>(
                                        size, BatchByteLimit));
                        return AddFileToBatch(
                            batch,
                            batchBytes,
                            nextIndex,
                            first,
                            path,
                            estimatedBytes,
                            BatchFileLimit,
                            BatchByteLimit,
                            flush);
                    }))
                    {
                        flush();
                    }
                } catch (const std::exception& error) {
                    std::lock_guard lock(mutex);
                    discoveryError = error.what();
                    stop = true;
                }
                {
                    std::lock_guard lock(mutex);
                    discoveryDone = true;
                }
                ready.notify_all();
            });
        } catch (const std::exception& error) {
            stop = true; // No worker has been launched yet.
            errors << error.what() << '\n';
            return false;
        }
        auto canLaunch = [&] {
            return !pending.empty() && bufferedBytes < BufferedOutputLimit &&
                workers.size() < threadCount &&
                workers.size() < pending.size() + processing;
        };
        for (;;) {
            FileResult result;
            {
                std::unique_lock lock(mutex);
                ready.wait(
                        lock,
                        [&] {
                            return discoveryError ||
                                (!results.empty() &&
                                        results.front().has_value()) ||
                                (discoveryDone && results.empty()) ||
                                canLaunch();
                        });
                if (discoveryError) {
                    errors << *discoveryError << '\n';
                    return false;
                }
                if (canLaunch()) {
                    lock.unlock();
                    try {
                        launchWorker();
                    } catch (const std::exception& error) {
                        {
                            std::lock_guard guard(mutex);
                            stop = true;
                        }
                        ready.notify_all();
                        errors << error.what() << '\n';
                        return false;
                    }
                    continue;
                }
                if (results.empty()) {
                    break;
                }
                bufferedBytes -=
                    results.front()->Output.size() +
                        results.front()->Error.size();
                result = std::move(*results.front());
                results.pop_front();
                ++printed;
            }
            ready.notify_all();
            if (print(result)) {
                {
                    std::lock_guard lock(mutex);
                    stop = true;
                }
                ready.notify_all();
                return false;
            }
        }
    } // Join discovery and workers before returning.
    return true;
}

} // namespace hit_fmt
