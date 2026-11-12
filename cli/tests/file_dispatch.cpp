#include <util/file.hpp>
#include <util/memory.hpp>
#include <util/types.hpp>

#include <cli/file_discovery.hpp>
#include <cli/file_dispatch.hpp>
#include <cli/worker_memory.hpp>

#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <future>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace {

struct CountingMemory
    : std::pmr::memory_resource
{
    std::atomic<us> Allocations{0};
    std::atomic<us> BytesInUse{0};

    void* do_allocate(us bytes, us alignment) override
    {
        auto* result = std::pmr::new_delete_resource()->allocate(bytes, alignment);
        ++Allocations;
        BytesInUse += bytes;
        return result;
    }

    void do_deallocate(void* pointer, us bytes, us alignment) override
    {
        std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
        BytesInUse -= bytes;
    }

    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override
    {
        return this == &other;
    }
};

struct FileInputs
{
    std::filesystem::path Directory = std::filesystem::temp_directory_path() /
        ("hit-fmt-dispatch-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));

    explicit FileInputs(us count)
    {
        std::filesystem::create_directory(Directory);

        for (us index = 0; index < count; ++index) {
            util::WriteEntireFile(Path(index).string(), "int value;\n");
        }
    }

    ~FileInputs()
    {
        std::error_code error;
        std::filesystem::remove_all(Directory, error);
    }

    std::filesystem::path Path(us index) const
    {
        return Directory / (std::to_string(index) + ".cpp");
    }
};

} // namespace

TEST(FileDispatch, FileFailuresDoNotHideLaterResults)
{
    for (us threads : {1u, 2u}) {
        SCOPED_TRACE(threads);
        // Exceed the queue limit, with failures inside multi-file batches.
        constexpr us count = 140;
        const FileInputs files(count);
        std::pmr::synchronized_pool_resource batchMemory;
        std::atomic<us> processed{0};
        us printed = 0;
        std::array<std::pmr::memory_resource*, count> resultAllocators{};
        const std::string spill(64 * 1024, 'e');
        auto discover = [&] (auto visit) -> cli::Result<bool> {
            for (us index = 0; index < count; ++index) {
                if (!visit(files.Path(index))) {
                    return {false, {}};
                }
            }

            return {true, {}};
        };
        auto process = [&] (const auto& path, int&, std::pmr::memory_resource* resultMemory) {
            ++processed;
            auto name = path.stem().string();
            bool failed = name == "0" || name == "3";
            resultAllocators[std::stoul(name)] = resultMemory;

            if (!failed) {
                util::WriteEntireFile(path.string(), "int value = 0;\n");
            }

            cli::FileResult result{.Output = std::pmr::string(name, resultMemory), .Failed = failed};

            if (failed) {
                try {
                    const auto message = name + ": file processing failed before arena release" +
                        (name == "0" ? spill : "");
                    throw std::runtime_error(message);
                } catch (const std::exception&) {
                    result.Error = cli::ExceptionError();
                }
            }

            return result;
        };
        auto print = [&] (const auto& result) {
            auto index = printed++;
            EXPECT_EQ(std::string_view(result.Output), std::to_string(index));
            EXPECT_EQ(result.Output.get_allocator().resource(), resultAllocators[index]);

            if (result.Failed) {
                std::ostringstream message;
                message << result.Error;
                EXPECT_EQ(
                    message.str(),
                    std::string(result.Output) + ": file processing failed before arena release" +
                    (index == 0 ? spill : ""));
            }

            return result.Failed;
        };
        int resolver = 0;
        auto success = threads == 1
            ? cli::RunSerialFiles(discover, resolver, process, print, &batchMemory)
            : cli::RunParallelFiles(
                threads,
                discover,
                [] {
                    return 0;
                },
                process,
                print,
                &batchMemory);
        EXPECT_FALSE(success.Value);
        EXPECT_FALSE(success.Error);
        EXPECT_EQ(processed.load(), count);
        EXPECT_EQ(printed, count);
    }
}

TEST(FileDispatch, DiscoveryErrorsDrainDispatchedFiles)
{
    for (const std::string message : {"", "discovery failed"}) {
        for (us threads : {1u, 2u}) {
            SCOPED_TRACE(threads);
            std::pmr::synchronized_pool_resource batchMemory;
            std::promise<void> firstStarted;
            auto started = firstStarted.get_future();
            std::vector<std::string> printed;
            auto discover = [&] (auto visit) -> cli::Result<bool> {
                visit(std::filesystem::path("first.cpp"));

                if (started.wait_for(std::chrono::seconds(5)) != std::future_status::ready) {
                    throw std::runtime_error("worker did not start");
                }

                return {false, cli::MakeError(message)};
            };
            auto process = [&] (const auto& path, int&, std::pmr::memory_resource* resultMemory) {
                firstStarted.set_value();
                return cli::FileResult{.Output = std::pmr::string(path.string(), resultMemory)};
            };
            auto print = [&] (const auto& result) {
                printed.emplace_back(result.Output);
                return result.Failed;
            };
            auto run = [&] {
                int resolver = 0;
                return threads == 1
                    ? cli::RunSerialFiles(discover, resolver, process, print, &batchMemory)
                    : cli::RunParallelFiles(
                        threads,
                        discover,
                        [] {
                            return 0;
                        },
                        process,
                        print,
                        &batchMemory);
            };
            auto result = run();
            EXPECT_FALSE(result.Value);
            std::ostringstream output;
            output << result.Error;
            EXPECT_EQ(output.str(), message);

            EXPECT_EQ(printed, (std::vector<std::string>{"first.cpp"}));
        }
    }
}

TEST(FileDispatch, DispatchFailuresStopWorkersAndDiscovery)
{
    for (bool failResolver : {true, false}) {
        SCOPED_TRACE(failResolver ? "resolver failure" : "output failure");
        std::pmr::synchronized_pool_resource memory;
        auto run = [&] {
            return cli::RunParallelFiles(
                2,
                [] (auto visit) -> cli::Result<bool> {
                    for (us index = 0; index < 140; ++index) {
                        if (!visit(std::filesystem::path("input.cpp"))) {
                            return {false, {}};
                        }
                    }

                    return {true, {}};
                },
                [&] {
                    if (failResolver) {
                        throw std::runtime_error("resolver failed");
                    }

                    return 0;
                },
                [] (const auto&, int&, std::pmr::memory_resource*) {
                    return cli::FileResult{};
                },
                [] (const auto&) -> bool {
                    throw std::runtime_error("output failed");
                },
                &memory);
        };
        auto result = run();
        EXPECT_FALSE(result.Value);
        std::ostringstream output;
        output << result.Error;
        EXPECT_EQ(output.str(), failResolver ? "resolver failed" : "output failed");
    }
}

TEST(FileDispatch, ProcessesDuringDiscoveryAndPrintsInOrder)
{
    std::promise<void> firstStarted;
    auto started = firstStarted.get_future();
    std::atomic<bool> secondProcessed{false};
    auto discover = [&] (auto visit) -> cli::Result<bool> {
        if (!visit(std::filesystem::path("first.cpp"))) {
            return {false, {}};
        }

        if (started.wait_for(std::chrono::seconds(5)) != std::future_status::ready) {
            throw std::runtime_error("discovery waited for formatting");
        }

        return {visit(std::filesystem::path("second.cpp")), {}};
    };
    auto process = [&] (const auto& path, int&, std::pmr::memory_resource* resultMemory) {
        if (path == "first.cpp") {
            firstStarted.set_value();
            auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);

            while (!secondProcessed.load() && std::chrono::steady_clock::now() < deadline) {
                std::this_thread::yield();
            }
        } else {
            secondProcessed = true;
        }

        return cli::FileResult{.Output = std::pmr::string(path.string(), resultMemory)};
    };
    std::vector<std::string> printed;
    auto completed = cli::RunParallelFiles(
        2,
        discover,
        [] {
            return 0;
        },
        process,
        [&] (const auto& result) {
            printed.emplace_back(result.Output);
            return false;
        });
    EXPECT_TRUE(completed.Value);
    EXPECT_FALSE(completed.Error);
    EXPECT_TRUE(secondProcessed.load());
    EXPECT_EQ(printed, (std::vector<std::string>{"first.cpp", "second.cpp"}));
}

TEST(FileDispatch, PreservesDiscoveredContextAcrossBatches)
{
    const FileInputs files(140);
    const int configurations[] = {17, 23};

    for (us threads : {1u, 2u}) {
        us printed = 0;
        CountingMemory upstream;
        std::atomic<us> workers{0};
        std::mutex allocationMutex;
        std::unordered_map<cli::WorkerMemory*, void*> firstAllocations;
        auto discover = [&] (auto visit) -> cli::Result<bool> {
            for (us i = 0; i < 140; ++i) {
                if (!visit(files.Path(i), &configurations[i % 2])) {
                    return {false, {}};
                }
            }

            return {true, {}};
        };
        auto process = [&] (
            const auto& path,
            cli::WorkerMemory& memory,
            const int* config,
            std::pmr::memory_resource* resultMemory) {
            auto index = std::stoul(path.stem().string());
            auto* allocation = memory.File.allocate(64);
            {
                std::lock_guard lock(allocationMutex);
                auto first = firstAllocations.try_emplace(&memory, allocation).first;
                EXPECT_EQ(allocation, first->second);
            }

            if (index == 0) {
                (void)memory.File.allocate(cli::WorkerMemory::FileCapacity + 1);
            }

            const std::pmr::string output(std::to_string(*config), &memory.Scratch);
            cli::FileResult result{
                .Output = std::pmr::string(output, resultMemory),
                .Failed = *config != (index % 2 ? 23 : 17),
            };
            memory.Reset();
            return result;
        };
        auto print = [&] (const auto& result) {
            EXPECT_EQ(result.Output, printed++ % 2 ? "23" : "17");
            return result.Failed;
        };
        cli::Result<bool, cli::FileFailure> success;

        if (threads == 1) {
            cli::WorkerMemory memory(&upstream);
            ++workers;
            success = cli::RunSerialFiles(discover, memory, process, print);
        } else {
            success = cli::RunParallelFiles<const int*>(
                threads,
                discover,
                [&] {
                    ++workers;
                    return cli::WorkerMemory(&upstream);
                },
                process,
                print);
        }

        EXPECT_LE(workers.load(), threads);
        EXPECT_GE(workers.load(), 1u);
        EXPECT_EQ(upstream.Allocations.load(), workers.load() + 1);
        EXPECT_EQ(upstream.BytesInUse.load(), 0u);
        EXPECT_TRUE(success.Value);
        EXPECT_FALSE(success.Error);
        EXPECT_EQ(printed, 140u);
    }
}

TEST(FileDispatch, RecognizesSourceExtensionsWithoutCopyingPathText)
{
    for (const auto* path : {"src/file.cpp", "src/.hidden.hpp", "src/a.c", "src/a.tcc"}) {
        EXPECT_TRUE(cli::IsCppSource(path)) << path;
    }

    for (const auto* path : {"src/.cpp", "src/file.cpp.bak", "src/file.CPP", "src.dir/file", "src/.."}) {
        EXPECT_FALSE(cli::IsCppSource(path)) << path;
    }
}

TEST(FileDiscovery, FindsNearestRepositoryRoot)
{
    for (const auto marker : {".git", ".arc"}) {
        for (bool directory : {false, true}) {
            const FileInputs files(0);
            const auto root = files.Directory / "repository";
            std::filesystem::create_directories(root / "nested/source");
            util::WriteEntireFile((files.Directory / ".git").string(), "gitdir: outer\n");

            if (directory) {
                std::filesystem::create_directory(root / marker);
            } else {
                util::WriteEntireFile((root / marker).string(), "marker\n");
            }

            Arena scratch;
            EXPECT_EQ(cli::FindRepositoryRoot(root / "nested/source/..", scratch), root);
        }
    }
}

TEST(FileBatch, RecyclesStorageAndReleasesSpillAfterResultsAreConsumed)
{
    CountingMemory upstream;
    FileBatch batch(std::pmr::get_default_resource(), &upstream);
    const std::string source = "source/with/a/long/path/whose/characters/must/be/copied.cpp";
    us bytes = 0;
    us index = 0;
    bool first = false;
    auto getBatch = [&] {
        return &batch;
    };
    auto flush = [] {
        return true;
    };
    {
        Arena scratch;
        const fs::Path path(std::string_view(source), &scratch);
        ASSERT_TRUE(AddFileToBatch(getBatch, bytes, index, first, path, 1, 4, 10, flush));
    }

    ASSERT_EQ(batch.Count, 1u);
    auto& entry = *batch.Entries[0];
    EXPECT_EQ(entry.Path.string(), source);
    EXPECT_EQ(entry.Path.native().get_allocator().resource(), &batch.Memory.Resource);
    EXPECT_EQ(entry.Result.Output.get_allocator().resource(), &batch.Memory.Resource);
    const auto* initial = entry.Path.data();
    entry.Result.Output.assign(cli::BatchMemory::Capacity * 2, 'x');
    EXPECT_GT(upstream.BytesInUse.load(), 0u);
    EXPECT_EQ(entry.Result.Output.size(), cli::BatchMemory::Capacity * 2);
    batch.Reset();
    EXPECT_EQ(upstream.BytesInUse.load(), 0u);
    ASSERT_TRUE(AddFileToBatch(getBatch, bytes, index, first, fs::Path(source), 1, 4, 10, flush));
    EXPECT_EQ(batch.Entries[0]->Path.data(), initial);
    EXPECT_EQ(batch.Entries[0]->Path.string(), source);
}

TEST(FileDispatch, BoundsSlotsAndDrainsAfterOversizedHead)
{
    constexpr us count = 140;
    constexpr us queuedLimit = 1 + 31 * 4;
    constexpr us outputSize = 17 * 1024 * 1024;
    const FileInputs files(count);
    std::atomic<us> discovered{0};
    std::promise<void> filled;
    auto filledFuture = filled.get_future();
    auto discover = [&] (auto visit) -> cli::Result<bool> {
        for (us index = 0; index < count; ++index) {
            if (!visit(files.Path(index))) {
                return {false, {}};
            }

            if (++discovered == queuedLimit) {
                filled.set_value();
            }
        }

        return {true, {}};
    };
    auto process = [&] (const auto& path, int&, std::pmr::memory_resource* memory) {
        auto index = std::stoul(path.stem().string());
        cli::FileResult result{.Output = std::pmr::string(memory)};

        if (index == 0) {
            if (filledFuture.wait_for(std::chrono::seconds(5)) != std::future_status::ready) {
                throw std::runtime_error("discovery did not fill the available slots");
            }

            EXPECT_EQ(discovered.load(), queuedLimit);
            result.Output.assign(outputSize, 'x');
        } else {
            result.Output = std::to_string(index);
        }

        return result;
    };
    us printed = 0;
    auto completed = cli::RunParallelFiles(
        2,
        discover,
        [] {
            return 0;
        },
        process,
        [&] (const auto& result) {
            if (printed == 0) {
                EXPECT_EQ(result.Output.size(), outputSize);
                EXPECT_EQ(result.Output.front(), 'x');
                EXPECT_EQ(result.Output.back(), 'x');
            } else {
                EXPECT_EQ(std::string_view(result.Output), std::to_string(printed));
            }

            ++printed;
            return false;
        });
    EXPECT_TRUE(completed.Value) << completed.Error;
    EXPECT_EQ(printed, count);
}
