#include <hit-fmt/file_batch.hpp>

#include <cstddef>
#include <filesystem>
#include <initializer_list>
#include <string>
#include <vector>

#include <gtest/gtest.h>

TEST(FileBatch, FlushesOnCountOrEstimatedBytes)
{
    FileBatch batch;
    size_t bytes = 0;
    size_t nextIndex = 0;
    bool first = true;
    std::vector<std::vector<size_t>> groups;
    auto flush = [&] {
        std::vector<size_t> indices;
        for (size_t offset = 0; offset < batch.Paths.size(); ++offset) {
            EXPECT_EQ(
                batch.Paths[offset],
                std::to_string(batch.FirstIndex + offset));
            indices.push_back(batch.FirstIndex + offset);
        }
        groups.push_back(std::move(indices));
        batch.Paths.clear();
        return true;
    };
    auto add = [&] (size_t estimatedBytes) {
        return AddFileToBatch(
            batch,
            bytes,
            nextIndex,
            first,
            std::filesystem::path(std::to_string(nextIndex)),
            estimatedBytes,
            4,
            10,
            flush);
    };
    for (auto size : {1, 3, 4, 4, 100, 2, 2, 2, 2, 6, 4}) {
        ASSERT_TRUE(add(size));
    }
    EXPECT_EQ(
        groups,
        (std::vector<std::vector<size_t>>{
            {0},
            {1, 2},
            {3},
            {4},
            {5, 6, 7, 8},
            {9, 10},
        }));
    EXPECT_TRUE(batch.Paths.empty());
}
