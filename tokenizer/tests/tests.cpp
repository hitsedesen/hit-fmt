#include <tokenizer/tokenizer.hpp>
#include <unicode/utf8.hpp>
#include <util/file.hpp>
#include <util/input.hpp>

#include <gtest/gtest.h>

#include <array>
#include <format>
#include <fstream>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <vector>

using namespace tokenizer;

static_assert(std::ranges::input_range<Tokenizer>);
static_assert(!std::ranges::forward_range<Tokenizer>);

struct GoldenCase
{
    std::string Name;
};

std::ostream& operator<<(std::ostream& os, const GoldenCase& testCase)
{
    return os << testCase.Name;
}

std::string ReadInputFile(const std::string& path)
{
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error(
            std::format("Failed to open input file {}", path));
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

std::optional<std::string> ReadGoldenFile(const std::string& path)
{
    std::ifstream file(path);
    if (!file.is_open()) {
        return std::nullopt;
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

std::pair<std::string, std::vector<std::string>>
TokenizeAndSerialize(const std::string& input)
{
    std::vector<std::string> errors;
    auto paddedInput = input;
    paddedInput.append(util::SourcePaddingSize, '\0');
    Tokenizer tokenizer(
        {.Data = paddedInput.data(), .Size = input.size()},
        [&](TokenizationError error) {
            errors.push_back(
                std::format(
                    "{} error: {}",
                    error.ErrorPosition,
                    GetTokenizationErrorDescription(error.Kind)));
        });
    std::string result;
    for (const auto& token : tokenizer) {
        result += std::format(
            "{}: {} {}\n", token.Position, token.Kind, token.Text.View(input));
    }
    return {std::move(result), std::move(errors)};
}

std::vector<Token> Tokenize(std::string_view input)
{
    std::string paddedInput(input);
    paddedInput.append(util::SourcePaddingSize, '\0');
    Tokenizer tokenizer({.Data = paddedInput.data(), .Size = input.size()});
    std::vector<Token> tokens;
    for (const auto& token : tokenizer) {
        tokens.push_back(token);
    }
    return tokens;
}

std::vector<TokenizationErrorKind> TokenizeErrors(std::string_view input)
{
    std::vector<TokenizationErrorKind> errors;
    std::string paddedInput(input);
    paddedInput.append(util::SourcePaddingSize, '\0');
    Tokenizer tokenizer(
        {.Data = paddedInput.data(), .Size = input.size()},
        [&](TokenizationError error) {
            errors.push_back(error.Kind);
        });
    for ([[maybe_unused]] const auto& token : tokenizer) {
    }
    return errors;
}

void SaveToFile(const std::string& path, const std::string& content)
{
    std::ofstream out(path);
    ASSERT(out.is_open());
    out << content;
}

class GoldenGoodTest : public testing::TestWithParam<GoldenCase>
{ };

TEST_P(GoldenGoodTest, Match)
{
    auto goldenPath = std::format("./testdata/{}.out", GetParam().Name);
    auto input =
        ReadInputFile(std::format("./testdata/{}.in", GetParam().Name));
    auto expected = ReadGoldenFile(goldenPath);

    auto [actual, errors] = TokenizeAndSerialize(input);
    EXPECT_TRUE(errors.empty());
    if (std::getenv("UPDATE_GOLDEN")) {
        SaveToFile(goldenPath, actual);
    } else if (expected) {
        ASSERT_EQ(actual, *expected);
    } else {
        std::cerr << std::format(
            "No golden file for {}, saving first result\n", GetParam().Name);
        SaveToFile(goldenPath, actual);
    }
}

class GoldenBadTest : public testing::TestWithParam<GoldenCase>
{ };

TEST_P(GoldenBadTest, CanonizeErrors)
{
    auto goldenPath = std::format("./testdata/{}.out", GetParam().Name);
    auto goldenErrorsPath = std::format("./testdata/{}.err", GetParam().Name);
    auto input =
        ReadInputFile(std::format("./testdata/{}.in", GetParam().Name));
    auto expected = ReadGoldenFile(goldenPath);
    auto expectedErrors = ReadGoldenFile(goldenErrorsPath);

    auto [actual, errors] = TokenizeAndSerialize(input);
    EXPECT_FALSE(errors.empty()) << "should output errors";
    std::string actualErrors;
    for (auto& error : errors) {
        actualErrors += error;
        actualErrors += "\n";
    }

    if (std::getenv("UPDATE_GOLDEN")) {
        SaveToFile(goldenPath, actual);
        SaveToFile(goldenErrorsPath, actualErrors);
    } else if (expected && expectedErrors) {
        EXPECT_EQ(actual, *expected);
        EXPECT_EQ(actualErrors, expectedErrors);
    } else {
        std::cerr << std::format(
            "No golden file for {}, saving first result\n", GetParam().Name);
        SaveToFile(goldenPath, actual);
        SaveToFile(goldenErrorsPath, actualErrors);
    }
}

INSTANTIATE_TEST_SUITE_P(
    Tokenizer,
    GoldenGoodTest,
    testing::Values(
        GoldenCase{"multiline_preprocessing"},
        GoldenCase{"program_exists"},
        GoldenCase{"binary_literals"},
        GoldenCase{"hexadecimal_float_literals"},
        GoldenCase{"pointer_literal"},
        GoldenCase{"boolean_literals"},
        GoldenCase{"octal_literals"},
        GoldenCase{"raw_string_literals"},
        GoldenCase{"hexadecimal_literals"},
        GoldenCase{"character_literals"},
        GoldenCase{"decimal_float_literals"},
        GoldenCase{"user_defined_literals"},
        GoldenCase{"decimal_literals"},
        GoldenCase{"string_literals"},
        GoldenCase{"operators"},
        GoldenCase{"positions"}),
    [](const auto& info) {
        return info.param.Name;
    });

INSTANTIATE_TEST_SUITE_P(
    Tokenizer,
    GoldenBadTest,
    testing::Values(
        GoldenCase{"bad_tokens"},
        GoldenCase{"unterminated_raw_string"},
        GoldenCase{"validation_errors"}),
    [](const auto& info) {
        return info.param.Name;
    });

TEST(Tokenizer, KeepsEmbeddedNullInRawStringLiteral)
{
    std::string source = "R\"(before";
    source.push_back('\0');
    source += "after)\" identifier";
    auto [serialized, errors] = TokenizeAndSerialize(source);
    auto tokens = Tokenize(source);

    EXPECT_TRUE(errors.empty());
    ASSERT_EQ(tokens.size(), 2);
    EXPECT_EQ(tokens[0].Kind, TokenKind::Literal_String);
    EXPECT_EQ(tokens[0].Text.View(source).size(), 17);
    EXPECT_EQ(tokens[1].Text.View(source), "identifier");
}

TEST(Utf8, ValidatesCodePointEncoding)
{
    constexpr auto Invalid = std::to_array<std::string_view>({
        std::string_view("\x80", 1),
        std::string_view("\xc0\x80", 2),
        std::string_view("\xe0\x80\x80", 3),
        std::string_view("\xed\xa0\x80", 3),
        std::string_view("\xf4\x90\x80\x80", 4),
        std::string_view("\xf0\x90\x80", 3),
    });
    for (auto encoding : Invalid) {
        EXPECT_FALSE(unicode::utf8::GetFirstCodePoint(encoding));
    }

    constexpr auto Valid = std::to_array<std::string_view>({
        std::string_view("\x7f", 1),
        std::string_view("\xc2\x80", 2),
        std::string_view("\xe0\xa0\x80", 3),
        std::string_view("\xf4\x8f\xbf\xbf", 4),
    });
    for (auto encoding : Valid) {
        EXPECT_TRUE(unicode::utf8::GetFirstCodePoint(encoding));
    }
}

TEST(Tokenizer, RejectsMalformedUtf8)
{
    constexpr std::array Source{
        static_cast<char>(0xf0),
        static_cast<char>(0x28),
        static_cast<char>(0x8c),
        static_cast<char>(0x28),
    };
    auto errors =
        TokenizeErrors(std::string_view(Source.data(), Source.size()));

    EXPECT_EQ(
        errors,
        (std::vector{
            TokenizationErrorKind::InvalidUtf8CodePoint,
            TokenizationErrorKind::InvalidUtf8CodePoint}));
}
