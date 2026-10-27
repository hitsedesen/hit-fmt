#include <tokenizer/tokenizer.hpp>

#include <gtest/gtest.h>

#include <tests/golden.hpp>
#include <tests/input.hpp>

#include <format>
#include <ranges>
#include <string>
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

std::pair<std::string, std::vector<std::string>> TokenizeAndSerialize(const std::string& input)
{
    std::vector<std::string> errors;
    const test::Input paddedInput(input);
    Tokenizer tokenizer(
        paddedInput.View(),
        [&] (TokenizationError error) {
            errors.push_back(
                std::format("{} error: {}", error.ErrorPosition, GetTokenizationErrorDescription(error.Kind)));
        });
    std::string result;
    for (const auto& token : tokenizer) {
        result += std::format("{}: {} {}\n", token.Position, token.Kind, token.Text.View(input));
    }
    return {std::move(result), std::move(errors)};
}

class GoodTest
    : public testing::TestWithParam<GoldenCase>
{ };

TEST_P(GoodTest, Golden)
{
    const test::golden::Fixture fixture{TOKENIZER_TESTDATA_DIR, GetParam().Name};
    auto [actual, errors] = TokenizeAndSerialize(fixture.InputNamed(".in"));
    EXPECT_TRUE(errors.empty());
    fixture.Expect(actual, ".out");
}

class BadTest
    : public testing::TestWithParam<GoldenCase>
{ };

TEST_P(BadTest, Golden)
{
    const test::golden::Fixture fixture{TOKENIZER_TESTDATA_DIR, GetParam().Name};
    auto [actual, errors] = TokenizeAndSerialize(fixture.InputNamed(".in"));
    EXPECT_FALSE(errors.empty()) << "should output errors";
    std::string actualErrors;
    for (const auto& error : errors) {
        actualErrors += error;
        actualErrors += "\n";
    }
    fixture.Expect(actual, ".out");
    fixture.Expect(actualErrors, ".err");
}

INSTANTIATE_TEST_SUITE_P(
    Tokenizer,
    GoodTest,
    testing::Values(
        GoldenCase{"empty"},
        GoldenCase{"multiline_preprocessing"},
        GoldenCase{"program_exists"},
        GoldenCase{"keywords"},
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
    [] (const auto& info) {
        return info.param.Name;
    });

INSTANTIATE_TEST_SUITE_P(
    Tokenizer,
    BadTest,
    testing::Values(
        GoldenCase{"bad_tokens"},
        GoldenCase{"unterminated_raw_string"},
        GoldenCase{"validation_errors"},
        GoldenCase{"cursor_utf8"},
        GoldenCase{"unterminated_string"},
        GoldenCase{"unterminated_character"}),
    [] (const auto& info) {
        return info.param.Name;
    });
