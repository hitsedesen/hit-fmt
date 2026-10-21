#include "ascii_block.hpp"

#include <immintrin.h>

namespace tokenizer::detail {

AsciiBlock ClassifyAsciiBlockAvx2(const unsigned char* source) noexcept
{
    AsciiBlock result{};
    for (uint32_t offset = 0; offset < AsciiBlock::Size; offset += 32) {
        const auto bytes = _mm256_loadu_si256(
            reinterpret_cast<const __m256i*>(source + offset));
        const auto equal = [=] (char value) {
            return _mm256_cmpeq_epi8(bytes, _mm256_set1_epi8(value));
        };
        const auto mask = [] (__m256i value) {
            return static_cast<uint32_t>(_mm256_movemask_epi8(value));
        };
        const auto letters = _mm256_or_si256(bytes, _mm256_set1_epi8(0x20));
        const auto alpha = _mm256_and_si256(
            _mm256_cmpgt_epi8(letters, _mm256_set1_epi8('a' - 1)),
            _mm256_cmpgt_epi8(_mm256_set1_epi8('z' + 1), letters));
        const auto digit = _mm256_and_si256(
            _mm256_cmpgt_epi8(bytes, _mm256_set1_epi8('0' - 1)),
            _mm256_cmpgt_epi8(_mm256_set1_epi8('9' + 1), bytes));
        const auto identifier =
            mask(_mm256_or_si256(_mm256_or_si256(alpha, digit), equal('_')));
        const auto controls = _mm256_and_si256(
            _mm256_cmpgt_epi8(bytes, _mm256_set1_epi8('\t' - 1)),
            _mm256_cmpgt_epi8(_mm256_set1_epi8('\r' + 1), bytes));
        const auto newline = mask(equal('\n'));
        const auto spaces =
            mask(_mm256_or_si256(controls, equal(' '))) & ~newline;
        const auto ordinary = ~(mask(bytes) | mask(equal('\0')) | newline);
        const auto backslash = mask(equal('\\'));
        const auto quote = mask(equal('"'));
        const auto apostrophe = mask(equal('\''));
        const auto star = mask(equal('*'));
        const auto close = mask(equal(')'));
        const auto slash = mask(equal('/'));
        const auto raw = mask(equal('R'));
        const std::array<uint32_t, static_cast<size_t>(AsciiRun::Count)> runs{
            spaces,
            identifier,
            ordinary & ~(quote | backslash),
            ordinary & ~(apostrophe | backslash),
            ordinary & ~star,
            ordinary & ~close,
            ordinary,
            ordinary & ~(backslash | slash | quote | apostrophe | raw),
            ordinary & ~backslash,
        };
        for (size_t kind = 0; kind < runs.size(); ++kind) {
            result.Continuations[kind] |=
                static_cast<uint64_t>(runs[kind]) << offset;
        }
    }
    return result;
}

} // namespace tokenizer::detail
