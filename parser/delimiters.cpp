#include "delimiters.hpp"

#include "syntax_tree.hpp"

#include <algorithm>
#include <bit>
#include <cassert>

namespace parser::detail {

bool MatchDelimiters(TokenPool& tokens, Arena& scratch, DelimiterBlockClassifier classify)
{
    assert(classify);
    struct Delimiter
    {
        TokenIndex Index;
        tokenizer::TokenKind Closing;
    };

    Vector<Delimiter> openings(&scratch);
    openings.reserve(256);
    bool valid = true;
    for (TokenIndex begin = 0; begin < tokens.Size();) {
        const auto kinds = tokens.KindBlock(begin);
        const auto matching = tokens.MatchingBlock(begin);
        for (u32 offset = 0; offset < kinds.size(); offset += DelimiterBlockSize) {
            const auto count = std::min<u32>(DelimiterBlockSize, kinds.size() - offset);
            auto mask = classify(kinds.data() + offset, count);

            while (mask) {
                const auto position = offset + static_cast<u32>(std::countr_zero(mask));
                const auto index = begin + position;
                const auto kind = kinds[position];
                mask &= mask - 1;

                // Classifiers select only the consecutive open/close pairs
                // whose ordering is checked by IsDelimiter.
                const auto delimiter =
                    static_cast<u8>(kind) - static_cast<u8>(tokenizer::TokenKind::Operator_OpenBrace);

                if ((delimiter & 1) == 0) {
                    openings.push_back({
                        .Index = index,
                        .Closing = static_cast<tokenizer::TokenKind>(static_cast<u8>(kind) + 1),
                    });
                } else if (openings.empty() || openings.back().Closing != kind) {
                    valid = false;
                } else {
                    const auto opening = openings.back();
                    openings.pop_back();
                    tokens.SetMatchingDelimiter(opening.Index, index);
                    matching[position] = opening.Index;
                }
            }
        }
        begin += kinds.size();
    }
    return valid && openings.empty();
}

} // namespace parser::detail
