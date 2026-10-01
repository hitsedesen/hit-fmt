#include <hit-fmt/file_dispatch.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <future>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

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
            auto deadline = std::chrono::steady_clock::now() +
                std::chrono::seconds(5);
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
