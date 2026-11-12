#pragma once

#include "error.hpp"
#include "file_batch.hpp"

#include <util/types.hpp>

#include <fs/fs.hpp>

#include <algorithm>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <exception>
#include <mutex>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace cli {

// A file failure affects the aggregate status, not discovery of later files.
template <typename Discover, typename Resolver, typename Process, typename Print>
Result<bool, FileFailure> RunSerialFiles(
    Discover&& discover,
    Resolver& resolver,
    Process&& process,
    Print&& print,
    std::pmr::memory_resource* memory = nullptr)
{
    memory = memory ? memory : std::pmr::get_default_resource();
    BatchMemory batchMemory(memory);
    bool failed = false;
    try {
        auto discovered = discover([&] (const auto& path, const auto&... context) {
            {
                auto result = process(path, resolver, context..., &batchMemory.Resource);
                failed |= print(result);
            }
            batchMemory.Resource.release();
            return true;
        });
        return {discovered.Value && !failed, std::move(discovered.Error)};
    } catch (const std::exception&) {
        return {false, ExceptionError()};
    }
}

// Each slot transfers exclusively from discovery to worker to ordered printing.
// Context must outlive dispatch.
template <typename Context = std::nullptr_t, typename Discover, typename MakeResolver, typename Process, typename Print>
Result<bool, FileFailure> RunParallelFiles(
    us threadCount,
    Discover&& discover,
    MakeResolver&& makeResolver,
    Process&& process,
    Print&& print,
    std::pmr::memory_resource* memory = nullptr)
{
    constexpr us BufferedOutputLimit = 16 * 1024 * 1024;
    constexpr us BatchByteLimit = 256 * 1024;
    using Batch = BasicFileBatch<Context>;
    enum class State
    {
        Free,
        Filling,
        Pending,
        Processing,
        Ready
    };

    struct Slot
        : Batch
    {
        explicit Slot(std::pmr::memory_resource* memory)
            : Batch(memory)
        { }

        State Status = State::Free;
        us OutputBytes = 0;
    };

    memory = memory ? memory : std::pmr::get_default_resource();
    auto slotCount = std::max(threadCount, us{32});
    std::pmr::deque<Slot> slots(memory);

    for (us i = 0; i < slotCount; ++i) {
        slots.emplace_back(memory);
    }

    std::mutex mutex;
    std::condition_variable ready;
    us produced = 0;
    us claimed = 0;
    us printed = 0;
    us processing = 0;
    us bufferedBytes = 0;
    bool discoveryDone = false;
    bool discoveryCompleted = false;
    bool stop = false;
    bool failed = false;
    FileFailure discoveryError;
    FileFailure dispatchError;
    FileFailure workerError;
    {
        std::vector<std::jthread> workers;
        auto launchWorker = [&] {
            workers.emplace_back([&] {
                try {
                    auto resolver = [&] {
                        std::lock_guard lock(mutex);
                        return makeResolver();
                    }();

                    for (;;) {
                        Slot* slot = nullptr;
                        {
                            std::unique_lock lock(mutex);
                            ready.wait(
                                lock,
                                [&] {
                                    return stop || (claimed != produced && bufferedBytes < BufferedOutputLimit) ||
                                        (discoveryDone && claimed == produced);
                                });

                            if (stop || (discoveryDone && claimed == produced)) {
                                return;
                            }

                            slot = &slots[claimed++ % slotCount];
                            slot->Status = State::Processing;
                            ++processing;
                        }

                        us bytes = 0;

                        for (us i = 0; i < slot->Count; ++i) {
                            auto& entry = *slot->Entries[i];

                            if constexpr (std::is_same_v<Context, std::nullptr_t>) {
                                entry.Result = process(entry.Path, resolver, &slot->Memory.Resource);
                            } else {
                                entry.Result = process(
                                    entry.Path,
                                    resolver,
                                    entry.ContextValue,
                                    &slot->Memory.Resource);
                            }

                            bytes += entry.Result.Output.size() + entry.Result.Error.StorageSize();
                        }

                        {
                            std::lock_guard lock(mutex);
                            slot->OutputBytes = bytes;
                            bufferedBytes += bytes;
                            slot->Status = State::Ready;
                            --processing;
                        }

                        ready.notify_all();
                    }
                } catch (const std::exception&) {
                    {
                        std::lock_guard lock(mutex);

                        if (!workerError) {
                            workerError = ExceptionError();
                        }

                        stop = true;
                    }

                    ready.notify_all();
                }
            });
        };
        std::jthread discovery;
        try {
            discovery = std::jthread([&] {
                try {
                    Slot* filling = nullptr;
                    auto getBatch = [&] () -> Batch* {
                        if (filling) {
                            return filling;
                        }

                        std::unique_lock lock(mutex);
                        ready.wait(
                            lock,
                            [&] {
                                return stop || slots[produced % slotCount].Status == State::Free;
                            });

                        if (stop) {
                            return nullptr;
                        }

                        filling = &slots[produced % slotCount];
                        filling->Status = State::Filling;
                        return filling;
                    };
                    auto flush = [&] {
                        if (!filling || filling->Count == 0) {
                            return true;
                        }

                        {
                            std::lock_guard lock(mutex);

                            if (stop) {
                                return false;
                            }

                            filling->Status = State::Pending;
                            ++produced;
                            filling = nullptr;
                        }

                        ready.notify_all();
                        return true;
                    };
                    us batchBytes = 0;
                    us nextIndex = 0;
                    bool first = true;
                    auto discovered = discover([&] (const auto& path, const auto&... context) {
                        auto size = fs::FileSize(path);
                        auto bytes = size.Error.Code
                            ? BatchByteLimit
                            : static_cast<us>(std::min<std::uintmax_t>(size.Value, BatchByteLimit));
                        return AddFileToBatch<Context>(
                            getBatch,
                            batchBytes,
                            nextIndex,
                            first,
                            path,
                            bytes,
                            Batch::Capacity,
                            BatchByteLimit,
                            flush,
                            context...);
                    });

                    if (discovered.Value && !discovered.Error) {
                        flush();
                    }

                    std::lock_guard lock(mutex);
                    discoveryCompleted = discovered.Value;
                    discoveryError = std::move(discovered.Error);
                } catch (const std::exception&) {
                    std::lock_guard lock(mutex);
                    discoveryError = ExceptionError();
                }
                {
                    std::lock_guard lock(mutex);
                    discoveryDone = true;
                }

                ready.notify_all();
            });
            auto canLaunch = [&] {
                return claimed != produced && bufferedBytes < BufferedOutputLimit && workers.size() < threadCount &&
                    workers.size() < produced - claimed + processing;
            };

            for (;;) {
                Slot* slot = nullptr;
                {
                    std::unique_lock lock(mutex);
                    ready.wait(
                        lock,
                        [&] {
                            return stop || (printed != produced && slots[printed % slotCount].Status == State::Ready) ||
                                (discoveryDone && printed == produced) || canLaunch();
                        });

                    if (stop) {
                        break;
                    }

                    if (canLaunch()) {
                        lock.unlock();
                        launchWorker();
                        continue;
                    }

                    if (printed == produced) {
                        break;
                    }

                    slot = &slots[printed % slotCount];
                }

                for (us i = 0; i < slot->Count; ++i) {
                    failed |= print(slot->Entries[i]->Result);
                }

                slot->Reset();
                {
                    std::lock_guard lock(mutex);
                    bufferedBytes -= slot->OutputBytes;
                    slot->OutputBytes = 0;
                    slot->Status = State::Free;
                    ++printed;
                }

                ready.notify_all();
            }
        } catch (const std::exception&) {
            {
                std::lock_guard lock(mutex);
                stop = true;
            }

            ready.notify_all();
            dispatchError = ExceptionError();
        }
    }
    if (dispatchError) {
        return {false, std::move(dispatchError)};
    }

    if (workerError) {
        return {false, std::move(workerError)};
    }

    return {discoveryCompleted && !failed, std::move(discoveryError)};
}

} // namespace cli
