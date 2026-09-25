#include <formatter/config.hpp>
#include <formatter/formatter.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <regex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

std::string ReadFile(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot read fixture: " + path.string());
    }
    return {std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()};
}

void ExpectUnchanged(std::string_view source, const formatter::Style& style)
{
    std::string storage(source);
    storage.append(util::SourcePaddingSize, '\0');
    util::PaddedSourceView padded{.Data = storage.data(),
        .Size = source.size()};
    auto tree = parser::Parse(padded, [] (parser::ParseError) { });
    EXPECT_EQ(formatter::Format(tree, style), source);
    const auto first = formatter::Format(padded, style);
    EXPECT_EQ(first, source);
    storage.assign(first);
    storage.append(util::SourcePaddingSize, '\0');
    EXPECT_EQ(formatter::Format({.Data = storage.data(),
        .Size = first.size()}, style), first);
}

std::vector<std::filesystem::path> DiscoverCases()
{
    std::vector<std::filesystem::path> cases;
    for (const auto& entry :
        std::filesystem::directory_iterator(FORMATTER_TESTDATA_DIR))
    {
        auto filename = entry.path().filename().string();
        if (filename.ends_with(".input.cpp") ||
            filename.ends_with(".output.cpp"))
        {
            cases.push_back(entry.path());
        }
    }
    std::ranges::sort(cases);
    return cases;
}

class PreserveGoldenTest : public testing::TestWithParam<std::filesystem::path>
{ };

TEST_P(PreserveGoldenTest, KeepsEverySourceByteUnderAllWidths)
{
    const auto input = ReadFile(GetParam());
    for (auto [indent, column] : {
        std::pair<size_t, size_t>{4, 120},
        {1, 1},
        {32, 10000},
    }) {
        formatter::Style style;
        style.IndentWidth = indent;
        style.ColumnLimit = column;
        ExpectUnchanged(input, style);
    }
}

INSTANTIATE_TEST_SUITE_P(Preserve, PreserveGoldenTest,
    testing::ValuesIn(DiscoverCases()),
    [] (const auto& info) {
        auto name = info.param.filename().string();
        std::ranges::replace(name, '.', '_');
        std::ranges::replace(name, '-', '_');
        return name;
    });

TEST(PreserveFormatterTest, KeepsEmptyRecoveryAndBinarySource)
{
    for (const std::string source : {
        std::string{},
        std::string{" \t\r\n\r\n"},
        std::string{"#define STR(x) #x\r\nSTR(a+b);\r\n"},
        std::string{"int f(\n  /* unfinished"},
        std::string{"int x;\0int y;\n", 14},
    }) {
        ExpectUnchanged(source, formatter::Style{});
    }
}

TEST(PreserveConfigTest, AcceptsOnlyExclusionsAndPassiveWidths)
{
    const auto path = std::filesystem::temp_directory_path() /
        "hit-fmt-preserve-config-test.hit-fmt";
    auto save = [&] (std::string_view contents) {
        std::ofstream output(path);
        ASSERT_TRUE(output.is_open());
        output << contents;
    };
    save("IndentWidth = 2\nColumnLimit = 40\n"
         "Exclude = [\"(^|/)generated(/|$)\"]\n");
    const auto config = formatter::LoadConfigFile(path);
    EXPECT_EQ(config.FormattingStyle.IndentWidth, 2u);
    EXPECT_EQ(config.FormattingStyle.ColumnLimit, 40u);
    ASSERT_EQ(config.Exclusions.size(), 1u);
    EXPECT_TRUE(std::regex_search("generated/source.cpp", config.Exclusions[0]));
    for (auto setting : {
        "Regroup = true\n",
        "InsertBraces = false\n",
        "WhitespaceSensitiveMacros = []\n",
        "ColumnLimit = 0\n",
        "IndentWidth = 0\n",
        "Exclude = [\"[\"]\n",
    }) {
        save(setting);
        EXPECT_THROW(formatter::LoadConfigFile(path), std::runtime_error);
    }
    std::filesystem::remove(path);
}

} // namespace
