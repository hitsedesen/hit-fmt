#include <tokenizer/ascii_block.hpp>
#include <tokenizer/tokenizer.hpp>

#include <gtest/gtest.h>

#include <tests/golden.hpp>

#include <unicode/code_point.hpp>
#include <unicode/utf8.hpp>

#include <util/file.hpp>
#include <util/input.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <format>
#include <memory>
#include <ranges>
#include <span>
#include <string>
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
        {
            .Data = paddedInput.data(),
            .Size = static_cast<uint32_t>(input.size()),
        },
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
        {
            .Data = paddedInput.data(),
            .Size = static_cast<uint32_t>(input.size()),
        },
        [&] (TokenizationError error) {
            errors.push_back(error.Kind);
        });
    for ([[maybe_unused]] const auto& token : tokenizer) { }
    return errors;
}

struct TokenizationSnapshot
{
    std::vector<Token> Tokens;
    std::vector<TokenizationError> Errors;
    std::vector<size_t> ErrorCalls;
};

TokenizationSnapshot SnapshotTokenization(
    std::string_view input,
    size_t backingOffset)
{
    // No string terminator or spare capacity beyond the required padding.
    auto storage = std::make_unique<char[]>(
        backingOffset + input.size() + util::SourcePaddingSize);
    auto* data = storage.get() + backingOffset;
    std::ranges::copy(input, data);
    if (backingOffset != 0) {
        EXPECT_NE(reinterpret_cast<uintptr_t>(data) % 64, 0);
    }

    TokenizationSnapshot result;
    size_t call = 0;
    Tokenizer tokenizer(
        {.Data = data, .Size = static_cast<uint32_t>(input.size())},
        [&] (TokenizationError error) {
            result.Errors.push_back(error);
            result.ErrorCalls.push_back(call);
        });
    // Bound recovery even if a regression stops the streaming cursor advancing.
    for (size_t i = 0; i <= input.size(); ++i) {
        ++call;
        auto token = tokenizer.Get();
        result.Tokens.push_back(token);
        if (token.Kind == TokenKind::EndOfFile) {
            break;
        }
    }
    const auto& eof = result.Tokens.back();
    EXPECT_EQ(eof.Kind, TokenKind::EndOfFile);
    EXPECT_EQ(eof.Text.Begin, input.size());
    EXPECT_EQ(eof.Text.End, input.size());
    return result;
}

void ExpectShiftedTokenization(
    const TokenizationSnapshot& actual,
    const TokenizationSnapshot& reference,
    util::SourceOffset shift)
{
    ASSERT_EQ(actual.Tokens.size(), reference.Tokens.size());
    for (size_t i = 0; i < reference.Tokens.size(); ++i) {
        SCOPED_TRACE(std::format("token {}", i));
        auto expected = reference.Tokens[i];
        // The initial semicolon stays at offset zero; the snippet and EOF move.
        if (i != 0) {
            expected.Text.Begin += shift;
            expected.Text.End += shift;
        }
        const auto& token = actual.Tokens[i];
        EXPECT_EQ(token.Kind, expected.Kind);
        EXPECT_EQ(token.Text, expected.Text);
        EXPECT_EQ(token.Position.Line, expected.Position.Line);
        EXPECT_EQ(token.Position.Column, expected.Position.Column);
    }
    ASSERT_EQ(actual.Errors.size(), reference.Errors.size());
    EXPECT_EQ(actual.ErrorCalls, reference.ErrorCalls);
    for (size_t i = 0; i < reference.Errors.size(); ++i) {
        SCOPED_TRACE(std::format("diagnostic {}", i));
        const auto& error = actual.Errors[i];
        const auto& expected = reference.Errors[i];
        EXPECT_EQ(error.Kind, expected.Kind);
        EXPECT_EQ(error.TokenStart.Line, expected.TokenStart.Line);
        EXPECT_EQ(error.TokenStart.Column, expected.TokenStart.Column);
        EXPECT_EQ(error.ErrorPosition.Line, expected.ErrorPosition.Line);
        EXPECT_EQ(error.ErrorPosition.Column, expected.ErrorPosition.Column);
        EXPECT_EQ(error.TokenEnd.Line, expected.TokenEnd.Line);
        EXPECT_EQ(error.TokenEnd.Column, expected.TokenEnd.Column);
    }
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
            {
                .Data = storage.data(),
                .Size = static_cast<uint32_t>(input.size()),
            },
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

TEST(Tokenizer, ClassifiesEveryByteInEveryBlockLane)
{
    using detail::AsciiRun;
    const struct
    {
        AsciiRun Run;
        std::string_view Accepted;
        std::string_view Excluded;
    } Runs[]{
        {AsciiRun::Whitespace, " \t\r\f\v", ""},
        {
            AsciiRun::Identifier,
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz_0123456789",
            "",
        },
        {AsciiRun::String, "", "\n\"\\"},
        {AsciiRun::Character, "", "\n'\\"},
        {AsciiRun::BlockComment, "", "\n*"},
        {AsciiRun::RawString, "", "\n)"},
        {AsciiRun::LineComment, "", "\n"},
        {AsciiRun::Directive, "", "\n\\/\"'R"},
        {AsciiRun::DirectiveComment, "", "\n\\"},
    };
    static_assert(std::size(Runs) == static_cast<size_t>(AsciiRun::Count));
    constexpr auto StorageSize = detail::AsciiBlock::Size + 1;
    alignas(64) std::array<unsigned char, StorageSize> storage{};
    auto* data = storage.data() + 1;
    for (unsigned phase = 0; phase < 256; ++phase) {
        SCOPED_TRACE(std::format("byte phase {}", phase));
        // Every byte visits every lane, with different adjacent byte values.
        for (size_t lane = 0; lane < detail::AsciiBlock::Size; ++lane) {
            data[lane] = static_cast<unsigned char>((phase + lane) % 256);
        }
        detail::AsciiBlock actual;
        detail::AsciiBlock selected;
        detail::ClassifyAsciiBlock(data, actual);
        detail::GetAsciiBlockClassifier()(data, selected);
        for (unsigned i = 0;
            i < static_cast<unsigned>(detail::AsciiRun::Count);
            ++i)
        {
            const auto run = static_cast<detail::AsciiRun>(i);
            EXPECT_EQ(selected[run], actual[run]);
        }
        EXPECT_EQ(selected.Digits, actual.Digits);
        EXPECT_EQ(selected.Newlines, actual.Newlines);
        EXPECT_EQ(selected.NonAscii, actual.NonAscii);
        EXPECT_EQ(selected.Nulls, actual.Nulls);
        EXPECT_EQ(selected.Quotes, actual.Quotes);
        EXPECT_EQ(selected.Apostrophes, actual.Apostrophes);
        EXPECT_EQ(selected.Backslashes, actual.Backslashes);
        EXPECT_EQ(selected.Slashes, actual.Slashes);
        EXPECT_EQ(selected.Stars, actual.Stars);
        EXPECT_EQ(selected.Punctuation, actual.Punctuation);
        EXPECT_EQ(selected.Singles, actual.Singles);
        EXPECT_EQ(selected.Pairs, actual.Pairs);
        EXPECT_EQ(selected.Triples, actual.Triples);

        for (const auto& run : Runs) {
            SCOPED_TRACE(std::format("run {}", static_cast<unsigned>(run.Run)));
            uint64_t expected = 0;
            for (size_t lane = 0; lane < detail::AsciiBlock::Size; ++lane) {
                const auto byte = data[lane];
                const bool continues = !run.Accepted.empty()
                    ? run.Accepted.find(static_cast<char>(byte)) !=
                        std::string_view::npos
                    : byte != 0 && byte < 0x80 &&
                        run.Excluded.find(static_cast<char>(byte)) ==
                            std::string_view::npos;
                if (continues) {
                    expected |= uint64_t{1} << lane;
                }
            }
            EXPECT_EQ(actual[run.Run], expected);
        }
    }
}

TEST(Tokenizer, FindsCompoundSymbolsAcrossVectorLanes)
{
    constexpr std::string_view Pairs[]{
        "::",
        ":>",
        ".*",
        "->",
        "!=",
        "+=",
        "++",
        "-=",
        "--",
        "*=",
        "/=",
        "%=",
        "^=",
        "&=",
        "&&",
        "|=",
        "||",
        "==",
        "<:",
        "<%",
        "<=",
        "<<",
        ">=",
        ">>",
    };
    constexpr std::string_view Triples[]{"...", "->*", "<<=", ">>=", "<=>"};
    std::array<unsigned char, 64> bytes;
    for (auto spelling : {
        std::span<const std::string_view>(Pairs),
        std::span<const std::string_view>(Triples),
    })
    {
        for (auto text : spelling) {
            for (size_t offset = 0; offset < bytes.size(); ++offset) {
                bytes.fill(' ');
                for (size_t i = 0;
                    i < text.size() && offset + i < bytes.size();
                    ++i)
                {
                    bytes[offset + i] = static_cast<unsigned char>(text[i]);
                }
                detail::AsciiBlock result;
                detail::GetAsciiBlockClassifier()(bytes.data(), result);
                uint64_t pairs = 0;
                uint64_t triples = 0;
                for (size_t i = 0; i < bytes.size(); ++i) {
                    const std::string_view remaining(
                        reinterpret_cast<const char*>(bytes.data() + i),
                        bytes.size() - i);
                    for (auto candidate : Pairs) {
                        if (remaining.starts_with(candidate)) {
                            pairs |= uint64_t{1} << i;
                        }
                    }
                    for (auto candidate : Triples) {
                        if (remaining.starts_with(candidate)) {
                            triples |= uint64_t{1} << i;
                        }
                    }
                }
                EXPECT_EQ(result.Pairs, pairs);
                EXPECT_EQ(result.Triples, triples);
            }
        }
    }
}

TEST(Tokenizer, DeliversLookaheadDiagnosticsOnTheRequestingCall)
{
    const struct
    {
        std::string_view Text;
        std::vector<size_t> Calls;
        std::vector<TokenizationErrorKind> Kinds;
    } Cases[]{
        {";a\x80", {2}, {TokenizationErrorKind::InvalidUtf8CodePoint}},
        {";+\x80", {2}, {TokenizationErrorKind::InvalidUtf8CodePoint}},
        {";++\x80", {3}, {TokenizationErrorKind::InvalidUtf8CodePoint}},
        {";<::\x80", {4}, {TokenizationErrorKind::InvalidUtf8CodePoint}},
        {"z; \x80", {3}, {TokenizationErrorKind::InvalidUtf8CodePoint}},
        {";\"x\"\x80", {2}, {TokenizationErrorKind::InvalidUtf8CodePoint}},
        {
            ";..\x80",
            {2, 2},
            {TokenizationErrorKind::InvalidUtf8CodePoint,
                TokenizationErrorKind::UnfinishedEllipsis},
        },
    };
    for (const auto& testCase : Cases) {
        for (size_t prefix : {0u, 80u}) {
            for (unsigned padding = 0; padding < 64; ++padding) {
                SCOPED_TRACE(
                    std::format("prefix {}, offset {}", prefix, padding));
                const auto input = std::string(prefix, ';') +
                    std::string(padding, ' ') + std::string(testCase.Text);
                const auto result = SnapshotTokenization(input, 1);
                auto calls = testCase.Calls;
                for (auto& call : calls) {
                    call += prefix;
                }
                EXPECT_EQ(result.ErrorCalls, calls);
                std::vector<TokenizationErrorKind> kinds;
                for (const auto& error : result.Errors) {
                    kinds.push_back(error.Kind);
                }
                EXPECT_EQ(kinds, testCase.Kinds);
                if (testCase.Text == "z; \x80") {
                    ASSERT_EQ(result.Errors.size(), 1);
                    const auto& error = result.Errors.front();
                    EXPECT_EQ(error.TokenStart.Column, prefix + padding + 2);
                    EXPECT_EQ(error.ErrorPosition.Column, prefix + padding + 4);
                    EXPECT_EQ(error.TokenEnd.Column, prefix + padding + 4);
                }
            }
        }
    }
}

TEST(Tokenizer, RejectsOversizedInputWithStableEmptyEof)
{
    std::vector<TokenizationError> errors;
    Tokenizer tokenizer(
        {
            .Data = nullptr,
            .Size = static_cast<uint32_t>(util::MaxSourceSize + 1),
        },
        [&] (TokenizationError error) {
            errors.push_back(error);
        });
    ASSERT_EQ(errors.size(), 1);
    const auto& error = errors.front();
    EXPECT_EQ(error.Kind, TokenizationErrorKind::InputTooLarge);
    EXPECT_EQ(error.TokenStart.Line, 1);
    EXPECT_EQ(error.TokenStart.Column, 1);
    EXPECT_EQ(error.ErrorPosition.Line, 1);
    EXPECT_EQ(error.ErrorPosition.Column, 1);
    EXPECT_EQ(error.TokenEnd.Line, 1);
    EXPECT_EQ(error.TokenEnd.Column, 1);
    for (int i = 0; i < 2; ++i) {
        const auto eof = tokenizer.Get();
        EXPECT_EQ(eof.Kind, TokenKind::EndOfFile);
        EXPECT_EQ(eof.Text.Begin, 0);
        EXPECT_EQ(eof.Text.End, 0);
        EXPECT_EQ(eof.Position.Line, 1);
        EXPECT_EQ(eof.Position.Column, 1);
    }
    EXPECT_EQ(errors.size(), 1);
}

TEST(Tokenizer, PreservesTokensAndDiagnosticsAcrossBlockAndBackingOffsets)
{
    const struct
    {
        std::string_view Name;
        std::string Input;
        bool Valid;
    } Cases[]{
        {"empty", "", true},
        {
            "identifiers_and_suffixes",
            "const auto _A0z9 = 12_customSuffix9 + 0x1.fp2f64;\n" +
                std::string(193, 'a') + "9_β; \"x\"_literalSuffix42; " +
                "α" + std::string(129, 'Z') + "2_",
            true,
        },
        {
            "whitespace_and_operators",
            std::string(193, ' ') + "\t\f\v\r\n" +
                "a<=>b; x->*p; a<<=2; a...b;\n" +
                std::string(129, '\t') + "\r\n;\\\r\nnext;\n" +
                std::string(129, ' '),
            true,
        },
        {
            "comments",
            "// " + std::string(193, 'c') + " α\\\r\n" +
                "after; /* " + std::string(129, 'b') + "**/\n" +
                "/*\r\n \t*/ tail;\n// eof " + std::string(129, 'e'),
            true,
        },
        {
            "escaped_literals",
            std::string(
                R"src(u8"quote: \" slash: \\ \t \x41 \u03b1"_suffix )src"
                R"src('a'_letter U'α'; )src") +
                "\"" + std::string(193, 's') +
                "\\\r\ncontinued\\\nα\"_suffix99",
            true,
        },
        {
            "raw_strings",
            "u8R\"tag(" + std::string(193, 'r') +
                ")ta\" )wrong)tagx\"\r\nα\n)tag\"_rawSuffix; " +
                "R\"abcdefghijklmnop(body)abcdefghijklmnop\"",
            true,
        },
        {
            "preprocessing",
            "#define VALUE(x) " + std::string(193, 'p') +
                " \\\r\n ((x) + 1) /* block\r\ncomment */ " +
                "\"quoted // text\" R\"d(raw\ntext)d\"\r\n" +
                "#define COUNT 1'234 // tail \\\ncontinued α\n" +
                "%:define LETTER 'a'\nnext;\n#define EOF_VALUE 123",
            true,
        },
        {
            "unicode_and_raw_nul",
            std::string("α中\U0003D000 a\u0301; U'α' u8\"中\"_β; R\"z(before") +
                '\0' + "after)z\"_tail",
            true,
        },
        {
            "malformed_utf8",
            "\x80" " a\xc0\x80" "9; \"s\xed\xa0\x80" "\"; u'\x80'; "
                "/*c\xf4\x90\x80\x80" "*/\n#define X \xf0\x28\x8c\x28\n"
                "//c\xe0\x80\x80\nR\"d(raw\xc2)d\"; tail\xf0\x90\x80",
            false,
        },
        {
            "embedded_nul_recovery",
            std::string("a;") + '\0' + "b; R\"d(raw" + '\0' +
                "body)d\"; // line" + '\0' + "\nafter",
            false,
        },
        {
            "invalid_escapes_and_numbers",
            R"src("\q \x \uD800" u'ab' '' 0b2 0x' 1''2 1e+ 0x1.8; .. #)src",
            false,
        },
        {
            "invalid_raw_delimiters",
            R"src(R"bad delim(body)bad delim"; )src"
                R"src(R"abcdefghijklmnopq(body)abcdefghijklmnopq";)src",
            false,
        },
        {
            "unterminated_comment",
            "/*" + std::string(129, 'c') + "*\r\nα",
            false,
        },
        {
            "unterminated_string",
            "\"" + std::string(129, 's') + "\\",
            false,
        },
        {
            "unterminated_raw_string",
            "R\"tag(" + std::string(129, 'r') + ")ta",
            false,
        },
        {"unterminated_character", "U'α", false},
    };
    for (const auto& testCase : Cases) {
        SCOPED_TRACE(testCase.Name);
        const auto reference = SnapshotTokenization(";\n" + testCase.Input, 0);
        ASSERT_EQ(reference.Errors.empty(), testCase.Valid);
        for (util::SourceOffset shift = 0; shift < 64; ++shift) {
            SCOPED_TRACE(std::format("source shift {}", shift));
            // LF fixes all snippet columns, including diagnostic token starts
            // retained by lookahead from the semicolon on the preceding line.
            const auto input =
                ";" + std::string(shift, ' ') + "\n" + testCase.Input;
            for (size_t backingOffset : {0, 1, 15, 31, 63}) {
                SCOPED_TRACE(std::format("backing offset {}", backingOffset));
                const auto actual = SnapshotTokenization(input, backingOffset);
                ExpectShiftedTokenization(actual, reference, shift);
            }
        }
    }
}
