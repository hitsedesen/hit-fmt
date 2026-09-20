#include <tokenizer/tokenizer.hpp>
#include <unicode/utf8.hpp>
#include <util/file.hpp>
#include <util/input.hpp>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <format>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <vector>

using namespace tokenizer;

bool operator==(const util::Position& lhs, const util::Position& rhs)
{
    return lhs.Column == rhs.Column && lhs.Line == rhs.Line;
}

bool operator==(const Token lhs, const Token rhs)
{
    return lhs.Kind == rhs.Kind && lhs.Position == rhs.Position
        && lhs.Text == rhs.Text;
}

struct TestCase
{
    std::string_view Input;
    std::string_view Expected;
};

std::ostream& operator<<(std::ostream& os, const TestCase& tc)
{
    return os << tc.Input;
}

std::ostream& operator<<(std::ostream& os, const TokenizationError& error)
{
    return os << GetTokenizationErrorDescription(error.Kind);
}

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
    Tokenizer tokenizer(input, [&](TokenizationError error) {
        errors.push_back(
            std::format(
                "{} error: {}",
                error.ErrorPosition,
                GetTokenizationErrorDescription(error.Kind)));
    });
    std::string result;
    std::vector<std::string> tokens;
    auto token = tokenizer.Get();
    while (token.Kind != TokenKind::EndOfFile) {
        result += std::format(
            "{}: {} {}\n", token.Position, token.Kind, token.Text.View(input));
        token = tokenizer.Get();
    }
    return {std::move(result), std::move(errors)};
}

std::vector<Token> Tokenize(std::string_view input)
{
    Tokenizer tokenizer(input);
    std::vector<Token> tokens;
    for (auto token = tokenizer.Get(); token.Kind != TokenKind::EndOfFile;
         token = tokenizer.Get())
    {
        tokens.push_back(token);
    }
    return tokens;
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
    EXPECT_THAT(errors, testing::IsEmpty());
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
        GoldenCase{"string_literals"}),
    [](const auto& info) {
        return info.param.Name;
    });

INSTANTIATE_TEST_SUITE_P(
    Tokenizer,
    GoldenBadTest,
    testing::Values(
        GoldenCase{"bad_tokens"}, GoldenCase{"unterminated_raw_string"}),
    [](const auto& info) {
        return info.param.Name;
    });

TEST(Tokenizer, KeepsMultilineMacroWithBlockCommentTogether)
{
    constexpr std::string_view Source =
        "#define VALUE(x) x /* comment\ncontinued */\\\n + 1\nint value;";
    auto tokens = Tokenize(Source);

    ASSERT_GT(tokens.size(), 1);
    EXPECT_EQ(tokens[0].Kind, TokenKind::PreprocessingDirective);
    EXPECT_EQ(
        tokens[0].Text.View(Source),
        "#define VALUE(x) x /* comment\ncontinued */\\\n + 1");
}

TEST(Tokenizer, KeepsRawStringsInsidePreprocessingDirectives)
{
    constexpr std::string_view Source =
        "#define DOCUMENT R\"(first line\nsecond line)\"\nint value;";
    auto tokens = Tokenize(Source);

    ASSERT_GT(tokens.size(), 1);
    EXPECT_EQ(tokens[0].Kind, TokenKind::PreprocessingDirective);
    EXPECT_EQ(
        tokens[0].Text.View(Source),
        "#define DOCUMENT R\"(first line\nsecond line)\"");
}

TEST(Tokenizer, KeepsLineSplicedStringsInsidePreprocessingDirectives)
{
    constexpr std::string_view Source =
        "#define DOCUMENT \\\n"
        "\"first line\\n\\\n"
        "second line\\n\"\n"
        "int value;";
    auto [serialized, errors] = TokenizeAndSerialize(std::string(Source));
    auto tokens = Tokenize(Source);

    EXPECT_THAT(errors, testing::IsEmpty());
    ASSERT_GT(tokens.size(), 1);
    EXPECT_EQ(tokens[0].Kind, TokenKind::PreprocessingDirective);
    EXPECT_EQ(
        tokens[0].Text.View(Source),
        "#define DOCUMENT \\\n"
        "\"first line\\n\\\n"
        "second line\\n\"");
}

TEST(Tokenizer, KeepsDigitSeparatorsInExponentDigits)
{
    constexpr std::string_view Source = "auto value = int(1'2e0'1);";
    auto [serialized, errors] = TokenizeAndSerialize(std::string(Source));
    auto tokens = Tokenize(Source);

    EXPECT_THAT(errors, testing::IsEmpty());
    auto literal = std::ranges::find_if(tokens, [&](Token token) {
        return token.Text.View(Source) == "1'2e0'1";
    });
    ASSERT_NE(literal, tokens.end());
    EXPECT_EQ(literal->Kind, TokenKind::Literal_UserDefinedInteger);
}

TEST(Tokenizer, KeepsEmbeddedNullInRawStringLiteral)
{
    std::string source = "R\"(before";
    source.push_back('\0');
    source += "after)\" identifier";
    auto [serialized, errors] = TokenizeAndSerialize(source);
    auto tokens = Tokenize(source);

    EXPECT_THAT(errors, testing::IsEmpty());
    ASSERT_EQ(tokens.size(), 2);
    EXPECT_EQ(tokens[0].Kind, TokenKind::Literal_String);
    EXPECT_EQ(tokens[0].Text.View(source).size(), 17);
    EXPECT_EQ(tokens[1].Text.View(source), "identifier");
}

TEST(Tokenizer, AppliesLessColonColonLexingException)
{
    auto tokens = Tokenize("value<::type>();");

    ASSERT_GE(tokens.size(), 7);
    EXPECT_EQ(tokens[1].Kind, TokenKind::Operator_Less);
    EXPECT_EQ(tokens[2].Kind, TokenKind::Operator_ScopeResolution);
}

TEST(Tokenizer, ConsumesIncrementAndDecrementOperators)
{
    auto tokens = Tokenize("++value; \\\n--value;");

    ASSERT_GE(tokens.size(), 4);
    EXPECT_EQ(tokens[0].Kind, TokenKind::Operator_Increment);
    EXPECT_EQ(tokens[3].Kind, TokenKind::Operator_Decrement);
}

TEST(Tokenizer, CalculatesTokenPositionsFromSource)
{
    constexpr std::string_view Source = "\xCE\xB1 beta\n  gamma";
    auto tokens = Tokenize(Source);

    ASSERT_EQ(tokens.size(), 3);
    auto first = tokens[0].Position;
    auto second = tokens[1].Position;
    auto third = tokens[2].Position;
    EXPECT_EQ(first.Line, 1);
    EXPECT_EQ(first.Column, 1);
    EXPECT_EQ(second.Line, 1);
    EXPECT_EQ(second.Column, 3);
    EXPECT_EQ(third.Line, 2);
    EXPECT_EQ(third.Column, 3);
}
