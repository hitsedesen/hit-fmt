#include <parser/delimiters.hpp>
#include <parser/syntax_tree.hpp>

#include <gtest/gtest.h>

#include <array>
#include <limits>
#include <utility>

namespace {

using TK = tokenizer::TokenKind;
using InstructionSet = parser::detail::DelimiterInstructionSet;

class DelimiterTest
    : public testing::TestWithParam<InstructionSet>
{ };

TEST_P(DelimiterTest, ClassifiesEveryByteAndPartialBlock)
{
    const auto classify = parser::detail::GetDelimiterBlockClassifier(GetParam());

    if (!classify) {
        GTEST_SKIP() << "Instruction set unavailable";
    }

    std::array<TK, 128> kinds;

    for (u32 alignment : {0u, 1u, 7u}) {
        for (u32 first = 0; first < 256; first += 17) {
            kinds.fill(TK::Operator_OpenBrace);

            for (u32 index = 0; index < 64; ++index) {
                kinds[alignment + index] = static_cast<TK>(static_cast<u8>(first + index));
            }

            u64 expected = 0;

            for (u32 count = 0; count <= 64; ++count) {
                ASSERT_EQ(classify(kinds.data() + alignment, count), expected) << "alignment=" << alignment << " first="
                    << first << " count=" << count;

                if (count < 64) {
                    const auto kind = kinds[alignment + count];

                    if (kind == TK::Operator_OpenBrace || kind == TK::Operator_CloseBrace ||
                        kind == TK::Operator_OpenBracket || kind == TK::Operator_CloseBracket ||
                        kind == TK::Operator_OpenParenthesis || kind == TK::Operator_CloseParenthesis)
                    {
                        expected |= u64(1) << count;
                    }
                }
            }
        }
    }
}

TEST_P(DelimiterTest, PreservesMatchingAcrossBlocksAndMismatches)
{
    const auto classify = parser::detail::GetDelimiterBlockClassifier(GetParam());

    if (!classify) {
        GTEST_SKIP() << "Instruction set unavailable";
    }

    constexpr auto none = std::numeric_limits<parser::TokenIndex>::max();
    constexpr std::array openers = {TK::Operator_OpenBrace, TK::Operator_OpenBracket, TK::Operator_OpenParenthesis};
    constexpr std::array closers = {TK::Operator_CloseBrace, TK::Operator_CloseBracket, TK::Operator_CloseParenthesis};
    for (u32 count : {0u, 15u, 16u, 31u, 32u, 63u, 64u, 65u, 511u, 512u, 513u, 1200u, 5000u}) {
        for (bool malformed : {false, true}) {
            Arena memory;
            Arena scratch;
            parser::detail::TokenPool tokens(memory);
            Vector<parser::TokenIndex> expected(&memory);
            Vector<std::pair<parser::TokenIndex, TK>> openings(&memory);
            expected.resize(count, none);
            bool valid = true;

            for (u32 index = 0; index < count; ++index) {
                TK kind = TK::Identifier;

                // Deep mixed nesting crosses SIMD lanes, token chunks and the
                // initial stack capacity. Comments and literals are opaque.
                if (index < count / 3) {
                    kind = openers[index % openers.size()];
                } else if (index >= count - count / 3) {
                    const auto opening = count - index - 1;
                    kind = closers[opening % closers.size()];
                } else if (index % 3 == 0) {
                    kind = TK::Comment;
                } else if (index % 3 == 1) {
                    kind = TK::Literal_String;
                }

                if (malformed && index == count - 1) {
                    kind = kind == TK::Operator_CloseBrace ? TK::Operator_CloseParenthesis : TK::Operator_CloseBrace;
                }

                tokens.Add({.Kind = kind, .Position = {1, index + 1}, .Text = {index, index + 1}});
                switch (kind) {
                    case TK::Operator_OpenBrace:
                        openings.emplace_back(index, TK::Operator_CloseBrace);
                        break;
                    case TK::Operator_OpenParenthesis:
                        openings.emplace_back(index, TK::Operator_CloseParenthesis);
                        break;
                    case TK::Operator_OpenBracket:
                        openings.emplace_back(index, TK::Operator_CloseBracket);
                        break;
                    case TK::Operator_CloseParenthesis:
                    case TK::Operator_CloseBracket:
                    case TK::Operator_CloseBrace:
                        if (openings.empty() || openings.back().second != kind) {
                            valid = false;
                        } else {
                            const auto opening = openings.back().first;
                            openings.pop_back();
                            expected[opening] = index;
                            expected[index] = opening;
                        }

                        break;
                    default:
                        break;
                }
            }

            EXPECT_EQ(parser::detail::MatchDelimiters(tokens, scratch, classify), valid && openings.empty());
            tokens.Seal();

            for (u32 index = 0; index < count; ++index) {
                EXPECT_EQ(tokens.MatchingDelimiter(index), expected[index]) << "count=" << count << " malformed="
                    << malformed << " index=" << index;
            }
        }
    }
}

INSTANTIATE_TEST_SUITE_P(
    Parser,
    DelimiterTest,
    testing::Values(
        InstructionSet::Scalar,
        InstructionSet::Sse2,
        InstructionSet::Avx,
        InstructionSet::Avx2,
        InstructionSet::Avx512),
    [] (const testing::TestParamInfo<InstructionSet>& info) {
        switch (info.param) {
            case InstructionSet::Scalar:
                return "Scalar";
            case InstructionSet::Sse2:
                return "Sse2";
            case InstructionSet::Avx:
                return "Avx";
            case InstructionSet::Avx2:
                return "Avx2";
            default:
                return "Avx512";
        }
    });

} // namespace
