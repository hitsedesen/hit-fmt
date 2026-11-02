#pragma once
#include "layout_types.hpp"

#include <util/ascii.hpp>

#include <unicode/utf8.hpp>

#include <algorithm>
namespace formatter::layout::detail {

inline constexpr auto Infinity = UINT32_MAX;

inline std::uint32_t AddWidth(std::uint32_t a, std::uint32_t b)
{
    return b > Infinity - a ? Infinity : a + b;
}

inline std::uint32_t Offset(std::uint32_t base, std::int32_t offset)
{
    const auto value = static_cast<std::int64_t>(base) + offset;
    return static_cast<std::uint32_t>(
        std::clamp<std::int64_t>(value, 0, Infinity - 1));
}

struct Measure
{
    std::uint32_t Prefix = 0;
    bool Line = false;
    bool CanBreak = false;
    // ASCII bytes without tabs/newlines advance by their length. Leading
    // indentation still needs inspection; Unicode retains code-point rules.
    bool Plain = true;

    std::uint32_t Width() const
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

inline Measure MeasureText(std::string_view text, std::uint8_t tabWidth)
{
    Measure result;
    // Source ranges and borrowed text are bounded by 32-bit byte offsets.
    // Even all tabs fit a 64-bit sum; saturate once, not per code point.
    const auto ascii = util::PlainAsciiPrefix(text);

    if (ascii == text.size()) {
        return {static_cast<std::uint32_t>(ascii), false, false};
    }

    std::uint64_t prefix = ascii;
    for (std::size_t i = ascii; i < text.size(); ++i) {
        const auto c = static_cast<unsigned char>(text[i]);

        if (c >= 0x80) {
            i += unicode::utf8::GetFirstCodePointSize(text.substr(i)).value_or(1) - 1;
        }
        if (c == '\n' || c == '\r') {
            result.Line = true;
            result.CanBreak = true;
            result.Plain = false;
            // No suffix fact is consumed after the first physical boundary.
            result.Prefix = static_cast<std::uint32_t>(
                std::min<std::uint64_t>(prefix, Infinity));
            return result;
        } else {
            result.Plain = result.Plain && c != '\t' && static_cast<unsigned char>(c) < 0x80;
            // Conservative only for literal/source tabs: actual emission uses
            // tab stops. This summary never underestimates flat width.
            prefix += c == '\t' ? tabWidth : 1;
        }
    }
    result.Prefix = static_cast<std::uint32_t>(
        std::min<std::uint64_t>(prefix, Infinity));
    return result;
}

inline Measure MeasureFlatGap(GapSpec gap, std::string_view source, std::uint8_t tabWidth)
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

inline std::uint32_t SourceColumn(std::string_view text, std::uint8_t tabWidth)
{
    std::uint32_t column = 0;

    for (std::size_t i = 0; i < text.size(); ++i) {
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
inline bool ChangesNewline(std::string_view source, std::size_t lf, bool targetCRLF)
{
    const bool crlf = lf && source[lf - 1] == '\r';
    return crlf != targetCRLF && (!crlf || lf < 2 || source[lf - 2] != '\r');
}

inline bool ValidGapNewline(GapNewline value)
{
    return value == GapNewline::Inherit || value == GapNewline::LF || value == GapNewline::CRLF;
}

} // namespace formatter::layout::detail
