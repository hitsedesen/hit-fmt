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
void ClassifyAsciiBlockAvx2(const unsigned char* source, AsciiBlock& result) noexcept;

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

    if ((registers[2] & AvxAndOsxsave) != AvxAndOsxsave || (_xgetbv(0) & 6) != 6) {
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
    static const auto Classifier = HasAvx2() ? ClassifyAsciiBlockAvx2 : ClassifyAsciiBlock;
    return Classifier;
#else
    return ClassifyAsciiBlock;
#endif
}

void ClassifyAsciiBlock(const unsigned char* source, AsciiBlock& result) noexcept
{
    result = {};
    uint64_t colon = 0;
    uint64_t dot = 0;
    uint64_t minus = 0;
    uint64_t bang = 0;
    uint64_t plus = 0;
    uint64_t percent = 0;
    uint64_t caret = 0;
    uint64_t ampersand = 0;
    uint64_t pipe = 0;
    uint64_t equals = 0;
    uint64_t less = 0;
    uint64_t greater = 0;
#if defined(__SSE2__) || defined(_M_X64) || \
    (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
    for (uint32_t offset = 0; offset < AsciiBlock::Size; offset += 16) {
        const auto bytes = _mm_loadu_si128(reinterpret_cast<const __m128i*>(source + offset));
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
        const auto digit =
            _mm_and_si128(_mm_cmpgt_epi8(bytes, _mm_set1_epi8('0' - 1)), _mm_cmpgt_epi8(_mm_set1_epi8('9' + 1), bytes));
        const auto digits = mask(digit);
        const auto identifier = mask(_mm_or_si128(alpha, equal('_'))) | digits;
        const auto controls = _mm_and_si128(
            _mm_cmpgt_epi8(bytes, _mm_set1_epi8('\t' - 1)),
            _mm_cmpgt_epi8(_mm_set1_epi8('\r' + 1), bytes));
        const auto newline = mask(equal('\n'));
        const auto spaces = mask(_mm_or_si128(controls, equal(' '))) & ~newline;
        const auto nonAscii = mask(bytes);
        const auto nulls = mask(equal('\0'));

        const auto backslash = mask(equal('\\'));
        const auto quote = mask(equal('"'));
        const auto apostrophe = mask(equal('\''));
        const auto star = mask(equal('*'));
        const auto close = mask(equal(')'));
        const auto slash = mask(equal('/'));
        const auto raw = mask(equal('R'));
        const auto colons = mask(equal(':'));
        const auto dots = mask(equal('.'));
        const auto minuses = mask(equal('-'));
        const auto bangs = mask(equal('!'));
        const auto pluses = mask(equal('+'));
        const auto percents = mask(equal('%'));
        const auto carets = mask(equal('^'));
        const auto ampersands = mask(equal('&'));
        const auto pipes = mask(equal('|'));
        const auto equalSigns = mask(equal('='));
        const auto lesses = mask(equal('<'));
        const auto greaters = mask(equal('>'));
        auto punctuation = _mm_or_si128(equal('{'), equal('}'));
        punctuation = _mm_or_si128(punctuation, _mm_or_si128(equal('['), equal(']')));
        punctuation = _mm_or_si128(punctuation, _mm_or_si128(equal('('), equal(';')));
        punctuation = _mm_or_si128(punctuation, _mm_or_si128(equal('?'), equal('~')));
        punctuation = _mm_or_si128(punctuation, equal(','));
        result.Digits |= static_cast<uint64_t>(digits) << offset;
        result.Newlines |= static_cast<uint64_t>(newline) << offset;
        result.NonAscii |= static_cast<uint64_t>(nonAscii) << offset;
        result.Nulls |= static_cast<uint64_t>(nulls) << offset;
        result.Quotes |= static_cast<uint64_t>(quote) << offset;
        result.Apostrophes |= static_cast<uint64_t>(apostrophe) << offset;
        result.Backslashes |= static_cast<uint64_t>(backslash) << offset;
        result.Slashes |= static_cast<uint64_t>(slash) << offset;
        result.Stars |= static_cast<uint64_t>(star) << offset;
        result.Punctuation |= static_cast<uint64_t>(mask(punctuation) | close) << offset;
        result.Singles |= static_cast<uint64_t>(mask(punctuation) | close) << offset;
        colon |= static_cast<uint64_t>(colons) << offset;
        dot |= static_cast<uint64_t>(dots) << offset;
        minus |= static_cast<uint64_t>(minuses) << offset;
        bang |= static_cast<uint64_t>(bangs) << offset;
        plus |= static_cast<uint64_t>(pluses) << offset;
        percent |= static_cast<uint64_t>(percents) << offset;
        caret |= static_cast<uint64_t>(carets) << offset;
        ampersand |= static_cast<uint64_t>(ampersands) << offset;
        pipe |= static_cast<uint64_t>(pipes) << offset;
        equals |= static_cast<uint64_t>(equalSigns) << offset;
        less |= static_cast<uint64_t>(lesses) << offset;
        greater |= static_cast<uint64_t>(greaters) << offset;
        result.Whitespace |= static_cast<uint64_t>(spaces) << offset;
        result.Identifiers |= static_cast<uint64_t>(identifier) << offset;
        result.ClosingParentheses |= static_cast<uint64_t>(close) << offset;
        result.RawPrefixes |= static_cast<uint64_t>(raw) << offset;
    }
#else
    for (uint32_t offset = 0; offset < AsciiBlock::Size; ++offset) {
        const auto byte = source[offset];
        const auto bit = uint64_t{1} << offset;
        const bool space = byte == ' ' || byte == '\t' || byte == '\r' || byte == '\f' || byte == '\v';
        const bool digit = byte >= '0' && byte <= '9';
        const bool identifier = (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') || digit || byte == '_';
        const bool newline = byte == '\n';
        const bool nonAscii = byte >= 0x80;
        const bool null = byte == '\0';
        const bool quote = byte == '"';
        const bool apostrophe = byte == '\'';
        const bool backslash = byte == '\\';
        const bool slash = byte == '/';
        const bool star = byte == '*';

        result.Digits |= digit ? bit : 0;
        result.Newlines |= newline ? bit : 0;
        result.NonAscii |= nonAscii ? bit : 0;
        result.Nulls |= null ? bit : 0;
        result.Quotes |= quote ? bit : 0;
        result.Apostrophes |= apostrophe ? bit : 0;
        result.Backslashes |= backslash ? bit : 0;
        result.Slashes |= slash ? bit : 0;
        result.Stars |= star ? bit : 0;
        colon |= byte == ':' ? bit : 0;
        dot |= byte == '.' ? bit : 0;
        minus |= byte == '-' ? bit : 0;
        bang |= byte == '!' ? bit : 0;
        plus |= byte == '+' ? bit : 0;
        percent |= byte == '%' ? bit : 0;
        caret |= byte == '^' ? bit : 0;
        ampersand |= byte == '&' ? bit : 0;
        pipe |= byte == '|' ? bit : 0;
        equals |= byte == '=' ? bit : 0;
        less |= byte == '<' ? bit : 0;
        greater |= byte == '>' ? bit : 0;
        switch (byte) {
            case '{':
            case '}':
            case '[':
            case ']':
            case '(':
            case ')':
            case ';':
            case '?':
            case '~':
            case ',':
                result.Punctuation |= bit;
                result.Singles |= bit;
                break;
            default:
                break;
        }

        result.Whitespace |= space ? bit : 0;
        result.Identifiers |= identifier ? bit : 0;
        result.ClosingParentheses |= byte == ')' ? bit : 0;
        result.RawPrefixes |= byte == 'R' ? bit : 0;
    }
#endif
    result.Punctuation |= colon | dot | minus | bang | plus | percent | caret | ampersand | pipe | equals | less |
        greater | result.Stars | result.Slashes;
    result.Pairs = (colon & ((colon | greater) >> 1)) | (dot & (result.Stars >> 1)) |
        (minus & ((minus | greater) >> 1)) | (plus & (plus >> 1)) | (ampersand & (ampersand >> 1)) |
        (pipe & (pipe >> 1)) | (less & ((colon | percent | less) >> 1)) | (greater & (greater >> 1)) |
        ((bang | plus | minus | result.Stars | result.Slashes | percent | caret | ampersand | pipe | equals | less |
            greater) & (equals >> 1));
    result.Triples = (dot & (dot >> 1) & (dot >> 2)) | (minus & (greater >> 1) & (result.Stars >> 2)) |
        (less & (less >> 1) & (equals >> 2)) | (greater & (greater >> 1) & (equals >> 2)) |
        (less & (equals >> 1) & (greater >> 2));

}

} // namespace tokenizer::detail
