#include <hit-fmt/file_dispatch.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <future>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include <util/file.hpp>

namespace {

struct FileInputs
{
    std::filesystem::path Directory = std::filesystem::temp_directory_path() /
        ("hit-fmt-dispatch-" + std::to_string(std::chrono::steady_clock::now()
            .time_since_epoch()
            .count()));

    explicit FileInputs(size_t count)
    {
        std::filesystem::create_directory(Directory);
        for (size_t index = 0; index < count; ++index) {
            util::WriteEntireFile(Path(index).string(), "int value;\n");
        }
    }

    ~FileInputs()
    {
        std::error_code error;
        std::filesystem::remove_all(Directory, error);
    }

    std::filesystem::path Path(size_t index) const
    {
        return Directory / (std::to_string(index) + ".cpp");
    }
};

} // namespace

TEST(FileDispatch, FileFailuresDoNotHideLaterResults)
{
    for (const size_t threads : {1u, 2u}) {
        SCOPED_TRACE(threads);
        // Exceed the queue limit, with failures inside multi-file batches.
        constexpr size_t count = 140;
        const FileInputs files(count);
        std::atomic<size_t> processed{0};
        size_t printed = 0;
        auto discover = [&] (auto visit) {
            for (size_t index = 0; index < count; ++index) {
                if (!visit(files.Path(index))) {
                    return false;
                }
            }
            return true;
        };
        auto process = [&] (const std::filesystem::path& path, int&) {
            ++processed;
            const auto name = path
                .stem()
                .string();
            const bool failed = name == "0" || name == "3";
            if (!failed) {
                util::WriteEntireFile(path.string(), "int value = 0;\n");
            }
            return hit_fmt::FileResult{.Output = name, .Failed = failed};
        };
        auto print = [&] (const auto& result) {
            EXPECT_EQ(result.Output, std::to_string(printed++));
            return result.Failed;
        };
        std::ostringstream errors;
        int resolver = 0;
        const bool success = threads == 1
            ? hit_fmt::RunSerialFiles(
                discover,
                resolver,
                process,
                print,
                errors)
            : hit_fmt::RunParallelFiles(
                threads,
                discover,
                [] { return 0; },
                process,
                print,
                errors);
        EXPECT_FALSE(success);
        EXPECT_EQ(processed.load(), count);
        EXPECT_EQ(printed, count);
        EXPECT_TRUE(errors
            .str()
            .empty());
    }
}

TEST(FileDispatch, DiscoveryErrorsDrainDispatchedFiles)
{
    std::promise<void> firstStarted;
    auto started = firstStarted.get_future();
    std::vector<std::string> printed;
    std::ostringstream errors;
    const bool success = hit_fmt::RunParallelFiles(
        2,
        [&] (auto visit) -> bool {
            visit(std::filesystem::path("first.cpp"));
            if (started.wait_for(std::chrono::seconds(5)) !=
                std::future_status::ready)
            {
                throw std::runtime_error("worker did not start");
            }
            throw std::runtime_error("discovery failed");
        },
        [] { return 0; },
        [&] (const auto& path, int&) {
            firstStarted.set_value();
            return hit_fmt::FileResult{.Output = path.string()};
        },
        [&] (const auto& result) {
            printed.push_back(result.Output);
            return result.Failed;
        },
        errors);
    EXPECT_FALSE(success);
    EXPECT_EQ(printed, (std::vector<std::string>{"first.cpp"}));
    EXPECT_EQ(errors.str(), "discovery failed\n");
}

TEST(FileDispatch, ProcessesDuringDiscoveryAndPrintsInOrder)
{
    std::promise<void> firstStarted;
    auto started = firstStarted.get_future();
    std::atomic<bool> secondProcessed{false};
    auto discover = [&] (auto visit) {
        if (!visit(std::filesystem::path("first.cpp"))) {
            return false;
        }
        if (started.wait_for(std::chrono::seconds(5)) !=
            std::future_status::ready)
        {
            throw std::runtime_error("discovery waited for formatting");
        }
        return visit(std::filesystem::path("second.cpp"));
    };
    auto process = [&] (const std::filesystem::path& path, int&) {
        if (path == "first.cpp") {
            firstStarted.set_value();
            auto deadline =
                std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (!secondProcessed.load() &&
                std::chrono::steady_clock::now() < deadline)
            {
                std::this_thread::yield();
            }
        } else {
            secondProcessed = true;
        }
        return hit_fmt::FileResult{.Output = path.string()};
    };
    std::vector<std::string> printed;
    std::ostringstream errors;
    bool completed = hit_fmt::RunParallelFiles(
        2,
        discover,
        [] { return 0; },
        process,
        [&] (const auto& result) {
            printed.push_back(result.Output);
            return false;
        },
        errors);
    EXPECT_TRUE(completed) << errors.str();
    EXPECT_TRUE(secondProcessed.load());
    EXPECT_EQ(printed, (std::vector<std::string>{"first.cpp", "second.cpp"}));
}
