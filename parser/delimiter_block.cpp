#include "delimiters.hpp"

#include <array>
#include <cassert>

#if defined(_MSC_VER) && (defined(HIT_FMT_DELIMITER_SSE2) || defined(HIT_FMT_DELIMITER_AVX) || \
    defined(HIT_FMT_DELIMITER_AVX2) || defined(HIT_FMT_DELIMITER_AVX512))
#include <intrin.h>
#endif

namespace parser::detail {

#if defined(HIT_FMT_DELIMITER_SSE2)
u64 ClassifyDelimiterBlockSse2(const tokenizer::TokenKind* kinds, u32 count) noexcept;
#endif
#if defined(HIT_FMT_DELIMITER_AVX)
u64 ClassifyDelimiterBlockAvx(const tokenizer::TokenKind* kinds, u32 count) noexcept;
#endif
#if defined(HIT_FMT_DELIMITER_AVX2)
u64 ClassifyDelimiterBlockAvx2(const tokenizer::TokenKind* kinds, u32 count) noexcept;
#endif
#if defined(HIT_FMT_DELIMITER_AVX512)
u64 ClassifyDelimiterBlockAvx512(const tokenizer::TokenKind* kinds, u32 count) noexcept;
#endif

namespace {

u64 ClassifyDelimiterBlockScalar(const tokenizer::TokenKind* kinds, u32 count) noexcept
{
    assert(count <= DelimiterBlockSize);
    u64 mask = 0;

    for (u32 offset = 0; offset < count; ++offset) {
        mask |= static_cast<u64>(IsDelimiter(kinds[offset])) << offset;
    }

    return mask;
}

std::array<DelimiterBlockClassifier, 6> AvailableClassifiers() noexcept
{
    std::array<DelimiterBlockClassifier, 6> available{};
    available[static_cast<us>(DelimiterInstructionSet::Scalar)] = ClassifyDelimiterBlockScalar;
#if defined(_MSC_VER) && (defined(HIT_FMT_DELIMITER_SSE2) || defined(HIT_FMT_DELIMITER_AVX) || \
    defined(HIT_FMT_DELIMITER_AVX2) || defined(HIT_FMT_DELIMITER_AVX512))
    i32 registers[4];
    __cpuid(registers, 0);
    const auto maxLeaf = registers[0];
    if (maxLeaf >= 1) {
        __cpuidex(registers, 1, 0);
#if defined(HIT_FMT_DELIMITER_SSE2)
        if (registers[3] & (1 << 26)) {
            available[static_cast<us>(DelimiterInstructionSet::Sse2)] = ClassifyDelimiterBlockSse2;
        }
#endif
        constexpr i32 AvxAndOsxsave = (1 << 28) | (1 << 27);
        if ((registers[2] & AvxAndOsxsave) == AvxAndOsxsave) {
            const auto state = _xgetbv(0);
            if ((state & 6) == 6) {
#if defined(HIT_FMT_DELIMITER_AVX)
                available[static_cast<us>(DelimiterInstructionSet::Avx)] = ClassifyDelimiterBlockAvx;
#endif
                if (maxLeaf >= 7) {
                    __cpuidex(registers, 7, 0);
                    // MSVC's wider /arch settings may also emit BMI instructions.
                    constexpr u32 Avx2Features = (1u << 3) | (1u << 5) | (1u << 8);
#if defined(HIT_FMT_DELIMITER_AVX2)
                    if ((static_cast<u32>(registers[1]) & Avx2Features) == Avx2Features) {
                        available[static_cast<us>(DelimiterInstructionSet::Avx2)] = ClassifyDelimiterBlockAvx2;
                    }
#endif
#if defined(HIT_FMT_DELIMITER_AVX512)
                    constexpr u32 Avx512Features =
                        Avx2Features | (1u << 16) | (1u << 17) | (1u << 28) | (1u << 30) | (1u << 31);

                    if ((state & 0xe6) == 0xe6 && (static_cast<u32>(registers[1]) & Avx512Features) == Avx512Features) {
                        available[static_cast<us>(DelimiterInstructionSet::Avx512)] = ClassifyDelimiterBlockAvx512;
                    }
#endif
                }
            }
        }
    }
#else
#if defined(HIT_FMT_DELIMITER_SSE2)
    if (__builtin_cpu_supports("sse2")) {
        available[static_cast<us>(DelimiterInstructionSet::Sse2)] = ClassifyDelimiterBlockSse2;
    }
#endif
#if defined(HIT_FMT_DELIMITER_AVX)
    if (__builtin_cpu_supports("avx")) {
        available[static_cast<us>(DelimiterInstructionSet::Avx)] = ClassifyDelimiterBlockAvx;
    }
#endif
#if defined(HIT_FMT_DELIMITER_AVX2)
    if (__builtin_cpu_supports("avx2")) {
        available[static_cast<us>(DelimiterInstructionSet::Avx2)] = ClassifyDelimiterBlockAvx2;
    }
#endif
#if defined(HIT_FMT_DELIMITER_AVX512)
    if (__builtin_cpu_supports("avx512f") && __builtin_cpu_supports("avx512bw")) {
        available[static_cast<us>(DelimiterInstructionSet::Avx512)] = ClassifyDelimiterBlockAvx512;
    }
#endif
#endif
    for (us index = 1; index < available.size(); ++index) {
        if (available[index]) {
            available[0] = available[index];
        }
    }

    return available;
}

} // namespace

DelimiterBlockClassifier GetDelimiterBlockClassifier(DelimiterInstructionSet instructionSet) noexcept
{
    static const auto Available = AvailableClassifiers();
    const auto index = static_cast<us>(instructionSet);
    return index < Available.size() ? Available[index] : nullptr;
}

} // namespace parser::detail
