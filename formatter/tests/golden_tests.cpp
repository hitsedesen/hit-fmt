#include <formatter/config.hpp>
#include <formatter/formatter.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
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

class BracePlacementGoldenTest : public testing::TestWithParam<std::string>
{ };

TEST_P(BracePlacementGoldenTest, FormatsOnlySelectedDeclarationBraces)
{
    const auto directory = std::filesystem::path(BRACE_TESTDATA_DIR);
    const auto input = ReadFile(directory / "input.cpp");
    const auto expected = ReadFile(directory / (GetParam() + ".output.cpp"));
    const auto style = formatter::LoadConfigFile(
        directory / (GetParam() + ".hit-fmt")).FormattingStyle;

    auto format = [&style] (std::string_view source) {
        std::string storage(source);
        storage.append(util::SourcePaddingSize, '\0');
        util::PaddedSourceView padded{.Data = storage.data(),
            .Size = source.size()};
        auto tree = parser::Parse(padded, [] (parser::ParseError) { });
        EXPECT_EQ(formatter::Format(tree, style),
            formatter::Format(padded, style));
        return formatter::Format(tree, style);
    };
    EXPECT_EQ(format(input), expected);
    EXPECT_EQ(format(expected), expected);
}

INSTANTIATE_TEST_SUITE_P(Configurations, BracePlacementGoldenTest,
    testing::Values("preserve", "attach", "newline", "mixed"));

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

} // namespace
