#include "ascii_block.hpp"

#include <util/types.hpp>

#include <immintrin.h>

#include <array>

namespace tokenizer::detail {

namespace {

template <us Size>
constexpr std::array<unsigned char, 16> MakeMembershipTable(const char (&members)[Size]) noexcept
{
    std::array<unsigned char, 16> table{};

    // Printable ASCII uses six high-nibble bits, all below the sign bit.
    for (us index = 0; index + 1 < Size; ++index) {
        const auto value = static_cast<unsigned char>(members[index]);
        table[value & 0x0f] |= static_cast<unsigned char>(
            1u << ((value >> 4) - 2));
    }

    return table;
}

constexpr auto RepeatedTable = MakeMembershipTable("+-&|<>");

constexpr auto GroupEqualsTable = MakeMembershipTable("+-&|<>!=*/%^");

constexpr auto SinglesTable = MakeMembershipTable("{}[]();?~,");

constexpr auto PunctuationTable = MakeMembershipTable("{}[]();?~,+-&|<>!=*/%^:.");

template <u32 Offset>
#if defined(_MSC_VER)
__forceinline
#else
__attribute__((always_inline)) inline
#endif
void ClassifyHalf(__m256i bytes, __m256i following, AsciiBlock& result) noexcept
{
    auto store = [] (u64& target, u32 value) {
        if constexpr (Offset == 0) {
            target = value;
        } else {
            target |= static_cast<u64>(value) << Offset;
        }
    };
    const auto equal = [=] (char value) {
        return _mm256_cmpeq_epi8(bytes, _mm256_set1_epi8(value));
    };
    const auto mask = [] (__m256i value) {
        return static_cast<u32>(_mm256_movemask_epi8(value));
    };
    const auto letters = _mm256_or_si256(bytes, _mm256_set1_epi8(0x20));
    const auto alpha = _mm256_and_si256(
        _mm256_cmpgt_epi8(letters, _mm256_set1_epi8('a' - 1)),
        _mm256_cmpgt_epi8(_mm256_set1_epi8('z' + 1), letters));
    const auto digit = _mm256_and_si256(
        _mm256_cmpgt_epi8(bytes, _mm256_set1_epi8('0' - 1)),
        _mm256_cmpgt_epi8(_mm256_set1_epi8('9' + 1), bytes));
    const auto digits = mask(digit);
    const auto identifier = mask(_mm256_or_si256(alpha, equal('_'))) | digits;
    const auto controls = _mm256_and_si256(
        _mm256_cmpgt_epi8(bytes, _mm256_set1_epi8('\t' - 1)),
        _mm256_cmpgt_epi8(_mm256_set1_epi8('\r' + 1), bytes));
    const auto newline = mask(equal('\n'));
    const auto spaces = mask(_mm256_or_si256(controls, equal(' '))) & ~newline;
    const auto nonAscii = mask(bytes);
    const auto nulls = mask(equal('\0'));

    const auto backslash = mask(equal('\\'));
    const auto quote = mask(equal('"'));
    const auto apostrophe = mask(equal('\''));
    const auto star = mask(equal('*'));
    const auto close = mask(equal(')'));
    const auto slash = mask(equal('/'));
    const auto raw = mask(equal('R'));
    store(result.Digits, digits);
    store(result.Newlines, newline);
    store(result.NonAscii, nonAscii);
    store(result.Nulls, nulls);
    store(result.Quotes, quote);
    store(result.Apostrophes, apostrophe);
    store(result.Backslashes, backslash);
    store(result.Slashes, slash);
    store(result.Stars, star);
    store(result.Whitespace, spaces);
    store(result.Identifiers, identifier);
    store(result.ClosingParentheses, close);
    store(result.RawPrefixes, raw);

    const auto highNibbles = _mm256_and_si256(_mm256_srli_epi16(bytes, 4), _mm256_set1_epi8(0x0f));
    const auto highBits = _mm256_shuffle_epi8(
        _mm256_broadcastsi128_si256(_mm_setr_epi8(0, 0, 1, 2, 4, 8, 16, 32, 0, 0, 0, 0, 0, 0, 0, 0)),
        highNibbles);
    const auto membership = [=] (const std::array<unsigned char, 16>& table) {
        const auto lookup =
            _mm256_broadcastsi128_si256(_mm_loadu_si128(reinterpret_cast<const __m128i*>(table.data())));
        // PSHUFB indexes by the low nibble and rejects non-ASCII bytes.
        // The high-nibble bit makes membership exact, including controls.
        const auto bits = _mm256_and_si256(_mm256_shuffle_epi8(lookup, bytes), highBits);
        return _mm256_cmpgt_epi8(bits, _mm256_setzero_si256());
    };
    const auto repeated = membership(RepeatedTable);
    const auto groupEquals = membership(GroupEqualsTable);
    store(result.Singles, mask(membership(SinglesTable)));
    store(result.Punctuation, mask(membership(PunctuationTable)));

    // Supply each 128-bit lane with its following lane. The last lane is
    // followed by zeroes, so lookahead never reads beyond this block.
    const auto nextLanes = _mm256_permute2x128_si256(bytes, following, 0x21);
    const auto nextBytes = _mm256_alignr_epi8(nextLanes, bytes, 1);
    const auto nextNextBytes = _mm256_alignr_epi8(nextLanes, bytes, 2);
    const auto nextEqual = [=] (char value) {
        return _mm256_cmpeq_epi8(nextBytes, _mm256_set1_epi8(value));
    };
    const auto nextNextEqual = [=] (char value) {
        return _mm256_cmpeq_epi8(nextNextBytes, _mm256_set1_epi8(value));
    };
    const auto sameNext = _mm256_cmpeq_epi8(bytes, nextBytes);
    const auto nextEquals = nextEqual('=');
    const auto nextGreater = nextEqual('>');
    auto pairs = _mm256_and_si256(groupEquals, nextEquals);
    pairs = _mm256_or_si256(pairs, _mm256_and_si256(_mm256_or_si256(repeated, equal(':')), sameNext));
    pairs = _mm256_or_si256(pairs, _mm256_and_si256(_mm256_or_si256(equal(':'), equal('-')), nextGreater));
    pairs = _mm256_or_si256(pairs, _mm256_and_si256(equal('.'), nextEqual('*')));
    pairs = _mm256_or_si256(pairs, _mm256_and_si256(equal('<'), _mm256_or_si256(nextEqual(':'), nextEqual('%'))));
    store(result.Pairs, mask(pairs));

    auto triples = _mm256_and_si256(_mm256_and_si256(equal('.'), sameNext), nextNextEqual('.'));
    triples = _mm256_or_si256(triples, _mm256_and_si256(_mm256_and_si256(equal('-'), nextGreater), nextNextEqual('*')));
    triples = _mm256_or_si256(
        triples,
        _mm256_and_si256(_mm256_and_si256(_mm256_or_si256(equal('<'), equal('>')), sameNext), nextNextEqual('=')));
    triples = _mm256_or_si256(triples, _mm256_and_si256(_mm256_and_si256(equal('<'), nextEquals), nextNextEqual('>')));
    store(result.Triples, mask(triples));

}

} // namespace

void ClassifyAsciiBlockAvx2(const unsigned char* source, AsciiBlock& result) noexcept
{
    const auto first = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(source));
    const auto second = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(source + 32));
    ClassifyHalf<0>(first, second, result);
    ClassifyHalf<32>(second, _mm256_setzero_si256(), result);
}

} // namespace tokenizer::detail
