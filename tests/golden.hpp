#pragma once

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace test::golden {
inline std::optional<std::string> ReadGoldenFile(
    const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return std::nullopt;
    }
    return std::string(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
}
inline std::string ReadFile(const std::filesystem::path& path)
{
    auto contents = ReadGoldenFile(path);
    if (!contents) {
        throw std::runtime_error("cannot read fixture: " + path.string());
    }
    return std::move(*contents);
}

inline void WriteGoldenFile(
    const std::filesystem::path& path,
    std::string_view contents)
{
    std::ofstream output(path, std::ios::binary);
    if (output) {
        output.write(contents.data(), contents.size());
        output.close();
    }
    if (!output) {
        ADD_FAILURE() << "cannot write golden file " << path;
    }
}
// Missing goldens are errors unless explicitly regenerated.
inline void ExpectGolden(
    const std::filesystem::path& path,
    std::string_view actual)
{
    if (std::getenv("UPDATE_GOLDEN")) {
        WriteGoldenFile(path, actual);
    } else if (auto expected = ReadGoldenFile(path)) {
        EXPECT_EQ(actual, *expected) << path;
    } else {
        ADD_FAILURE() << "missing golden file " << path;
    }
}
struct Fixture
{
    std::filesystem::path Directory;
    std::string Name;
    [[nodiscard]] std::filesystem::path Named(std::string_view suffix) const
    {
        return Directory / (Name + std::string(suffix));
    }
    [[nodiscard]] std::string Input(
        const std::filesystem::path& filename = "input.cpp") const
    {
        return ReadFile(Directory / filename);
    }
    [[nodiscard]] std::string InputNamed(std::string_view suffix) const
    {
        return ReadFile(Named(suffix));
    }
    void Expect(
        std::string_view actual,
        std::string_view suffix = ".output.cpp") const
    {
        ExpectGolden(Named(suffix), actual);
    }
};
} // namespace test::golden
