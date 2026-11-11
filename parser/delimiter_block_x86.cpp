#include "delimiters.hpp"

#include <immintrin.h>

#include <cassert>

namespace parser::detail {

// This source is compiled separately for each instruction set; AVX uses
// VEX-encoded 128-bit comparisons because it has no 256-bit byte comparisons.
u64 HIT_FMT_DELIMITER_CLASSIFIER(const tokenizer::TokenKind* kinds, u32 count) noexcept
{
    assert(count <= DelimiterBlockSize);
    using TK = tokenizer::TokenKind;
    constexpr auto first = static_cast<char>(TK::Operator_OpenBrace);
    constexpr auto last = static_cast<char>(TK::Operator_CloseParenthesis);
    static_assert(static_cast<u8>(TK::Operator_CloseParenthesis) < 127);

#if HIT_FMT_DELIMITER_WIDTH == 64
    const auto active = count == 64 ? ~u64(0) : (u64(1) << count) - 1;
    const auto bytes = _mm512_maskz_loadu_epi8(active, kinds);
    return _mm512_cmpge_epu8_mask(bytes, _mm512_set1_epi8(first)) &
        _mm512_cmple_epu8_mask(bytes, _mm512_set1_epi8(last)) & active;
#else
    u64 mask = 0;
    u32 offset = 0;
#if HIT_FMT_DELIMITER_WIDTH == 32
    for (; offset + 32 <= count; offset += 32) {
        const auto bytes = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(kinds + offset));
        const auto delimiters = _mm256_and_si256(
            _mm256_cmpgt_epi8(bytes, _mm256_set1_epi8(first - 1)),
            _mm256_cmpgt_epi8(_mm256_set1_epi8(last + 1), bytes));
        mask |= static_cast<u64>(static_cast<u32>(_mm256_movemask_epi8(delimiters))) << offset;
    }
#else
    for (; offset + 16 <= count; offset += 16) {
        const auto bytes = _mm_loadu_si128(reinterpret_cast<const __m128i*>(kinds + offset));
        const auto delimiters = _mm_and_si128(
            _mm_cmpgt_epi8(bytes, _mm_set1_epi8(first - 1)),
            _mm_cmpgt_epi8(_mm_set1_epi8(last + 1), bytes));
        mask |= static_cast<u64>(static_cast<u32>(_mm_movemask_epi8(delimiters))) << offset;
    }
#endif

    for (; offset < count; ++offset) {
        mask |= static_cast<u64>(IsDelimiter(kinds[offset])) << offset;
    }

    return mask;
#endif
}

} // namespace parser::detail
