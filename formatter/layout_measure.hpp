#pragma once
#include "layout_types.hpp"

#include <util/ascii.hpp>
#include <util/types.hpp>

#include <unicode/utf8.hpp>

#include <algorithm>
namespace formatter::layout::detail {

inline constexpr auto Infinity = UINT32_MAX;

inline u32 AddWidth(u32 a, u32 b)
{
    return b > Infinity - a ? Infinity : a + b;
}

inline u32 Offset(u32 base, i32 offset)
{
    const auto value = static_cast<i64>(base) + offset;
    return static_cast<u32>(
        std::clamp<i64>(value, 0, Infinity - 1));
}

struct Measure
{
    u32 Prefix = 0;
    bool Line = false;
    bool CanBreak = false;
    // ASCII bytes without tabs/newlines advance by their length. Leading
    // indentation still needs inspection; Unicode retains code-point rules.
    bool Plain = true;

    u32 Width() const
    {
        return Line ? Infinity : Prefix;
    }
};

static_assert(sizeof(Measure) <= 8);

inline Measure Join(Measure a, Measure b)
{
    return {
        a.Line ? a.Prefix : AddWidth(a.Prefix, b.Prefix),
        a.Line || b.Line,
        a.CanBreak || b.CanBreak,
        a.Plain && b.Plain,
    };
}

// Write summary fields directly rather than repacking a returned aggregate.
inline void Prepend(Measure& destination, const Measure& prefix)
{
    destination.Prefix = prefix.Line ? prefix.Prefix : AddWidth(prefix.Prefix, destination.Prefix);
    destination.Line = prefix.Line || destination.Line;
    destination.CanBreak = prefix.CanBreak || destination.CanBreak;
    destination.Plain = prefix.Plain && destination.Plain;
}

inline Measure MeasureText(std::string_view text, u8 tabWidth)
{
    Measure result;
    // Source ranges and borrowed text are bounded by 32-bit byte offsets.
    // Even all tabs fit a 64-bit sum; saturate once, not per code point.
    const auto ascii = util::PlainAsciiPrefix(text);

    if (ascii == text.size()) {
        return {static_cast<u32>(ascii), false, false};
    }

    u64 prefix = ascii;
    for (us i = ascii; i < text.size(); ++i) {
        const auto c = static_cast<unsigned char>(text[i]);

        if (c >= 0x80) {
            i += unicode::utf8::GetFirstCodePointSize(text.substr(i)).value_or(1) - 1;
        }
        if (c == '\n' || c == '\r') {
            result.Line = true;
            result.CanBreak = true;
            result.Plain = false;
            // No suffix fact is consumed after the first physical boundary.
            result.Prefix = static_cast<u32>(
                std::min<u64>(prefix, Infinity));
            return result;
        } else {
            result.Plain = result.Plain && c != '\t' && static_cast<unsigned char>(c) < 0x80;
            // Conservative only for literal/source tabs: actual emission uses
            // tab stops. This summary never underestimates flat width.
            prefix += c == '\t' ? tabWidth : 1;
        }
    }
    result.Prefix = static_cast<u32>(
        std::min<u64>(prefix, Infinity));
    return result;
}

inline Measure MeasureFlatGap(GapSpec gap, std::string_view source, u8 tabWidth)
{
    if (gap.Kind == GapKind::Hard) {
        return {0, true, true};
    }

    if (gap.FlatSourceBegin != InvalidId) {
        auto measured =
            MeasureText(source.substr(gap.FlatSourceBegin, gap.FlatSourceEnd - gap.FlatSourceBegin), tabWidth);
        measured.CanBreak = gap.Kind == GapKind::Soft;
        return measured;
    }

    return {gap.Spaces, false, gap.Kind == GapKind::Soft};
}

inline u32 SourceColumn(std::string_view text, u8 tabWidth)
{
    u32 column = 0;

    for (us i = 0; i < text.size(); ++i) {
        const auto c = static_cast<unsigned char>(text[i]);

        if (c >= 0x80) {
            i += unicode::utf8::GetFirstCodePointSize(text.substr(i)).value_or(1) - 1;
        }

        column = AddWidth(column, c == '\t' ? tabWidth - column % tabWidth : 1);
    }

    return column;
}

inline bool Multiline(std::string_view text)
{
    for (const auto c : text) {
        if (c == '\r' || c == '\n') {
            return true;
        }
    }

    return false;
}

// LF offsets identify complete authorized atoms. Contracting CRLF must not
// pair its original bare-CR predecessor with the surviving LF.
inline bool ChangesNewline(std::string_view source, us lf, bool targetCRLF)
{
    const bool crlf = lf && source[lf - 1] == '\r';
    return crlf != targetCRLF && (!crlf || lf < 2 || source[lf - 2] != '\r');
}

inline bool ValidGapNewline(GapNewline value)
{
    return value == GapNewline::Inherit || value == GapNewline::LF || value == GapNewline::CRLF;
}

} // namespace formatter::layout::detail
