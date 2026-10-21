#include "ascii_block.hpp"

#if defined(__SSE2__) || defined(_M_X64) || \
    (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#include <emmintrin.h>
#endif

#if defined(HIT_FMT_HAS_AVX2) && defined(_MSC_VER)
#include <intrin.h>
#endif

namespace tokenizer::detail {

#if defined(HIT_FMT_HAS_AVX2)
AsciiBlock ClassifyAsciiBlockAvx2(const unsigned char* source) noexcept;

namespace {

bool HasAvx2() noexcept
{
#if defined(_MSC_VER)
    int registers[4];
    __cpuid(registers, 0);
    if (registers[0] < 7) {
        return false;
    }
    __cpuidex(registers, 1, 0);
    constexpr int AvxAndOsxsave = (1 << 28) | (1 << 27);
    if ((registers[2] & AvxAndOsxsave) != AvxAndOsxsave ||
        (_xgetbv(0) & 6) != 6)
    {
        return false;
    }
    __cpuidex(registers, 7, 0);
    // MSVC /arch:AVX2 also emits BMI1/BMI2 scalar instructions.
    constexpr int RequiredFeatures = (1 << 3) | (1 << 5) | (1 << 8);
    return (registers[1] & RequiredFeatures) == RequiredFeatures;
#else
    return __builtin_cpu_supports("avx2");
#endif
}

} // namespace
#endif

AsciiBlockClassifier GetAsciiBlockClassifier() noexcept
{
#if defined(HIT_FMT_HAS_AVX2)
    static const auto Classifier = HasAvx2()
        ? ClassifyAsciiBlockAvx2
        : ClassifyAsciiBlock;
    return Classifier;
#else
    return ClassifyAsciiBlock;
#endif
}

AsciiBlock ClassifyAsciiBlock(const unsigned char* source) noexcept
{
    AsciiBlock result{};
#if defined(__SSE2__) || defined(_M_X64) || \
    (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
    for (uint32_t offset = 0; offset < AsciiBlock::Size; offset += 16) {
        const auto bytes =
            _mm_loadu_si128(reinterpret_cast<const __m128i*>(source + offset));
        const auto equal = [=] (char value) {
            return _mm_cmpeq_epi8(bytes, _mm_set1_epi8(value));
        };
        const auto mask = [] (__m128i value) {
            return static_cast<uint32_t>(_mm_movemask_epi8(value));
        };
        const auto letters = _mm_or_si128(bytes, _mm_set1_epi8(0x20));
        const auto alpha = _mm_and_si128(
            _mm_cmpgt_epi8(letters, _mm_set1_epi8('a' - 1)),
            _mm_cmpgt_epi8(_mm_set1_epi8('z' + 1), letters));
        const auto digit = _mm_and_si128(
            _mm_cmpgt_epi8(bytes, _mm_set1_epi8('0' - 1)),
            _mm_cmpgt_epi8(_mm_set1_epi8('9' + 1), bytes));
        const auto identifier =
            mask(_mm_or_si128(_mm_or_si128(alpha, digit), equal('_')));
        const auto controls = _mm_and_si128(
            _mm_cmpgt_epi8(bytes, _mm_set1_epi8('\t' - 1)),
            _mm_cmpgt_epi8(_mm_set1_epi8('\r' + 1), bytes));
        const auto newline = mask(equal('\n'));
        const auto spaces = mask(_mm_or_si128(controls, equal(' '))) & ~newline;
        const auto ordinary = static_cast<uint32_t>(~(mask(bytes) |
            mask(equal('\0')) | newline)) & 0xffff;
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
#else
    for (uint32_t offset = 0; offset < AsciiBlock::Size; ++offset) {
        const auto byte = source[offset];
        const bool space = byte == ' ' || byte == '\t' || byte == '\r' ||
            byte == '\f' || byte == '\v';
        const bool identifier =
            (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
                (byte >= '0' && byte <= '9') || byte == '_';
        const bool ordinary = byte < 0x80 && byte != '\0' && byte != '\n';
        const std::array<bool, static_cast<size_t>(AsciiRun::Count)> runs{
            space,
            identifier,
            ordinary && byte != '"' && byte != '\\',
            ordinary && byte != '\'' && byte != '\\',
            ordinary && byte != '*',
            ordinary && byte != ')',
            ordinary,
            ordinary && byte != '\\' && byte != '/' && byte != '"' &&
                byte != '\'' && byte != 'R',
            ordinary && byte != '\\',
        };
        for (size_t kind = 0; kind < runs.size(); ++kind) {
            result.Continuations[kind] |=
                static_cast<uint64_t>(runs[kind]) << offset;
        }
    }
#endif
    return result;
}

} // namespace tokenizer::detail
