#include <util/types.hpp>

#include <cli/file_batch.hpp>

#include <gtest/gtest.h>

#include <filesystem>
#include <initializer_list>
#include <string>
#include <vector>

TEST(FileBatch, FlushesOnCountOrEstimatedBytes)
{
    FileBatch batch;
    us bytes = 0;
    us nextIndex = 0;
    bool first = true;
    std::vector<std::vector<us>> groups;
    auto flush = [&] {
        std::vector<us> indices;

        for (us offset = 0; offset < batch.Count; ++offset) {
            EXPECT_EQ(batch.Entries[offset]->Path.string(), std::to_string(batch.FirstIndex + offset));
            indices.push_back(batch.FirstIndex + offset);
        }

        groups.push_back(std::move(indices));
        batch.Reset();
        return true;
    };
    auto add = [&] (us estimatedBytes) {
        return AddFileToBatch(
            [&] {
                return &batch;
            },
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

    EXPECT_EQ(groups, (std::vector<std::vector<us>>{{0}, {1, 2}, {3}, {4}, {5, 6, 7, 8}, {9, 10}}));
    EXPECT_EQ(batch.Count, 0u);
}
