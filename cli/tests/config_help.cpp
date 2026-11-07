#include <util/types.hpp>

#include <cli/config_help.hpp>

#include <gtest/gtest.h>

#include <tests/golden.hpp>

#include <sstream>
#include <string>

TEST(ConfigHelp, Golden)
{
    std::ostringstream output;
    cli::PrintConfigHelp(output, cli::Terminal());
    test::golden::ExpectGolden(std::filesystem::path(HIT_FMT_TESTDATA_DIR) / "config_help.out", output.str());
}

TEST(ConfigHelp, ReadableLinesAndColorParity)
{
    std::ostringstream plain;
    cli::PrintConfigHelp(plain, cli::Terminal());
    std::istringstream lines(plain.str());
    std::string line;

    while (std::getline(lines, line)) {
        EXPECT_LE(line.size(), 80u) << line;
    }

    std::ostringstream colored;
    cli::PrintConfigHelp(colored, cli::Terminal(true));
    auto text = colored.str();
    EXPECT_NE(text.find("\033["), std::string::npos);
    std::string stripped;

    for (us index = 0; index < text.size(); ++index) {
        if (text[index] == '\033') {
            auto end = text.find('m', index);
            ASSERT_NE(end, std::string::npos);
            index = end;
        } else {
            stripped += text[index];
        }
    }

    EXPECT_EQ(stripped, plain.str());
}
