#include <hit-fmt/config_help.hpp>

#include <gtest/gtest.h>

#include <tests/golden.hpp>

#include <sstream>
#include <string>

TEST(ConfigHelp, Golden)
{
    std::ostringstream output;
    hit_fmt::PrintConfigHelp(output, hit_fmt::TerminalColors());
    test::golden::ExpectGolden(std::filesystem::path(HIT_FMT_TESTDATA_DIR) / "config_help.out", output.str());
}

TEST(ConfigHelp, ReadableLinesAndColorParity)
{
    std::ostringstream plain;
    hit_fmt::PrintConfigHelp(plain, hit_fmt::TerminalColors());
    std::istringstream lines(plain.str());
    std::string line;
    while (std::getline(lines, line)) {
        EXPECT_LE(line.size(), 80u) << line;
    }

    std::ostringstream colored;
    hit_fmt::PrintConfigHelp(colored, hit_fmt::TerminalColors(true));
    const auto text = colored.str();
    EXPECT_NE(text.find("\033["), std::string::npos);
    std::string stripped;
    for (std::size_t index = 0; index < text.size(); ++index) {
        if (text[index] == '\033') {
            const auto end = text.find('m', index);
            ASSERT_NE(end, std::string::npos);
            index = end;
        } else {
            stripped += text[index];
        }
    }
    EXPECT_EQ(stripped, plain.str());
}
