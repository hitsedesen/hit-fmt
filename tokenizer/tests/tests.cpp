#include <tokenizer/tokenizer.hpp>

#include <gtest/gtest.h>

#include <tests/golden.hpp>

#include <unicode/code_point.hpp>
#include <unicode/utf8.hpp>

#include <util/file.hpp>
#include <util/input.hpp>

#include <array>
#include <format>
#include <ranges>
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

std::pair<std::string, std::vector<std::string>> TokenizeAndSerialize(
    const std::string& input)
{
    std::vector<std::string> errors;
    auto paddedInput = input;
    paddedInput.append(util::SourcePaddingSize, '\0');
    Tokenizer tokenizer(
        {.Data = paddedInput.data(), .Size = input.size()},
        [&] (TokenizationError error) {
            errors.push_back(std::format(
                "{} error: {}",
                error.ErrorPosition,
                GetTokenizationErrorDescription(error.Kind)));
        });
    std::string result;
    for (const auto& token : tokenizer) {
        result += std::format(
            "{}: {} {}\n",
            token.Position,
            token.Kind,
            token.Text.View(input));
    }
    return {std::move(result), std::move(errors)};
}

std::vector<TokenizationErrorKind> TokenizeErrors(std::string_view input)
{
    std::vector<TokenizationErrorKind> errors;
    std::string paddedInput(input);
    paddedInput.append(util::SourcePaddingSize, '\0');
    Tokenizer tokenizer(
        {.Data = paddedInput.data(), .Size = input.size()},
        [&] (TokenizationError error) {
            errors.push_back(error.Kind);
        });
    for ([[maybe_unused]] const auto& token : tokenizer) { }
    return errors;
}

class GoodTest
    : public testing::TestWithParam<GoldenCase>
{ };

TEST_P(GoodTest, Golden)
{
    const test::golden::Fixture fixture{
        TOKENIZER_TESTDATA_DIR,
        GetParam().Name,
    };
    auto [actual, errors] = TokenizeAndSerialize(fixture.InputNamed(".in"));
    EXPECT_TRUE(errors.empty());
    fixture.Expect(actual, ".out");
}

class BadTest
    : public testing::TestWithParam<GoldenCase>
{ };

TEST_P(BadTest, Golden)
{
    const test::golden::Fixture fixture{
        TOKENIZER_TESTDATA_DIR,
        GetParam().Name,
    };
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
        GoldenCase{"cursor_utf8"}),
    [] (const auto& info) {
        return info.param.Name;
    });

TEST(Tokenizer, KeepsEofStableAfterAsciiUnicodeAndInvalidUtf8)
{
    constexpr std::array<std::string_view, 4> Inputs{
        "",
        "ascii",
        "α",
        std::string_view("\x80", 1),
    };
    for (auto input : Inputs) {
        SCOPED_TRACE(input);
        std::string storage(input);
        storage.append(util::SourcePaddingSize, '\0');
        size_t errors = 0;
        Tokenizer tokenizer(
            {.Data = storage.data(), .Size = input.size()},
            [&] (TokenizationError error) {
                ++errors;
                EXPECT_EQ(
                    error.Kind,
                    TokenizationErrorKind::InvalidUtf8CodePoint);
                EXPECT_EQ(error.TokenStart.Line, 1);
                EXPECT_EQ(error.TokenStart.Column, 1);
                EXPECT_EQ(error.ErrorPosition.Line, 1);
                EXPECT_EQ(error.ErrorPosition.Column, 1);
            });
        Token eof{};
        for (size_t i = 0; i <= input.size(); ++i) {
            eof = tokenizer.Get();
            if (eof.Kind == TokenKind::EndOfFile) {
                break;
            }
        }
        ASSERT_EQ(eof.Kind, TokenKind::EndOfFile);
        EXPECT_EQ(eof.Text.Begin, input.size());
        EXPECT_EQ(eof.Text.End, input.size());
        auto repeated = tokenizer.Get();
        EXPECT_EQ(repeated.Kind, TokenKind::EndOfFile);
        EXPECT_EQ(repeated.Position.Line, eof.Position.Line);
        EXPECT_EQ(repeated.Position.Column, eof.Position.Column);
        EXPECT_EQ(repeated.Text, eof.Text);
        EXPECT_EQ(errors, input == std::string_view("\x80", 1) ? 1 : 0);
    }
}

TEST(Unicode, UsesUnicode18IdentifierProperties)
{
    // New starts in both the BMP and a supplementary CJK block.
    EXPECT_TRUE(unicode::IsXIDStart(U'\u058B'));
    EXPECT_TRUE(unicode::IsXIDStart(U'\U0003D000'));
    EXPECT_TRUE(unicode::IsXIDStart(U'\U0003FC3F'));
    EXPECT_TRUE(unicode::IsXIDContinue(U'\U0003FC3F'));

    // A new combining mark can continue an identifier but cannot start one.
    EXPECT_FALSE(unicode::IsXIDStart(U'\u05C8'));
    EXPECT_TRUE(unicode::IsXIDContinue(U'\u05C8'));
    EXPECT_FALSE(unicode::IsXIDStart(U'\U0003FC40'));
    EXPECT_FALSE(unicode::IsXIDContinue(U'\U0003FC40'));
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
    // Both cached and uncached ASCII boundaries must retain UTF-8 errors.
    constexpr std::array<std::string_view, 5> Prefixes{
        "",
        "a;",
        "a; ",
        "a;\n",
        "a;\\\n",
    };
    for (auto prefix : Prefixes) {
        SCOPED_TRACE(prefix);
        std::string source(prefix);
        source.append(Source.data(), Source.size());
        auto errors = TokenizeErrors(source);
        EXPECT_EQ(
            errors,
            (std::vector{
                TokenizationErrorKind::InvalidUtf8CodePoint,
                TokenizationErrorKind::InvalidUtf8CodePoint,
            }));
    }
}
