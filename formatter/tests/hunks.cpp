#include <formatter/hunks.hpp>

#include <gtest/gtest.h>

#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

TEST(HunkParser, GroupsFilesAndResolvesPathsAgainstGivenDirectory)
{
    std::istringstream input(
        "file src/a.cpp\r\nhunk 2:4\r\nhunk 8:8\n"
        "file src/file with spaces.cpp\nhunk 1:1\n"
        "file src/../src/a.cpp\nhunk 9:10\n");
    const auto base = std::filesystem::current_path() / "hunk-test";
    const auto parsed = formatter::ParseChangedHunks(input, base);
    ASSERT_EQ(parsed.Paths.size(), 3);
    EXPECT_EQ(parsed.Paths[0], base / "src/a.cpp");
    EXPECT_EQ(parsed.Paths[1], base / "src/file with spaces.cpp");
    EXPECT_EQ(parsed.Paths[2], parsed.Paths[0]);
    const auto& hunks = parsed.Hunks.at((base / "src/a.cpp").string());
    ASSERT_EQ(hunks.size(), 3);
    EXPECT_EQ(hunks[0], (formatter::Hunk{2, 4}));
    EXPECT_EQ(hunks[1], (formatter::Hunk{8, 8}));
    EXPECT_EQ(hunks[2], (formatter::Hunk{9, 10}));
    EXPECT_EQ(
        parsed.Hunks.at((base / "src/file with spaces.cpp").string())[0],
        (formatter::Hunk{1, 1}));
}

TEST(HunkParser, EmptyInputSelectsNoFiles)
{
    std::istringstream input;
    const auto parsed = formatter::ParseChangedHunks(
        input,
        std::filesystem::current_path());
    EXPECT_TRUE(parsed.Paths.empty());
    EXPECT_TRUE(parsed.Hunks.empty());
}

TEST(HunkParser, RejectsMalformedRecordsWithLineNumbers)
{
    for (auto [text, line] : {
        std::pair<std::string_view, size_t>{"hunk 1:2\n", 1},
        {"file a.cpp\nfile b.cpp\nhunk 1:1\n", 2},
        {"file a.cpp\nhunk 0:1\n", 2},
        {"file a.cpp\nhunk 4:2\n", 2},
        {"file a.cpp\nhunk 2:x\n", 2},
        {"file a.cpp\nhunk 1:2:3\n", 2},
        {"file a.cpp\nline 1:2\n", 2},
    })
    {
        std::istringstream input{std::string(text)};
        try {
            (void)formatter::ParseChangedHunks(
                input,
                std::filesystem::current_path());
            FAIL() << "Accepted: " << text;
        } catch (const std::runtime_error& error) {
            EXPECT_NE(std::string(error.what()).find(
                "changed ranges line " + std::to_string(line)),
                std::string::npos) << text;
        }
    }
}

TEST(HunkParser, RejectsFileWithoutHunksAtEndOfInput)
{
    std::istringstream input("file a.cpp\n");
    EXPECT_THROW(formatter::ParseChangedHunks(
        input,
        std::filesystem::current_path()), std::runtime_error);
}

} // namespace
