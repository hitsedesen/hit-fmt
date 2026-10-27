#include "code_point.hpp"

#include <algorithm>
#include <span>
#include <utility>

namespace unicode {

extern const std::span<const std::pair<char32_t, char32_t>> XIDStartRanges;

extern const std::span<const std::pair<char32_t, char32_t>> XIDContinueRanges;

namespace {

bool IsInRange(CodePoint cp, std::span<const std::pair<char32_t, char32_t>> ranges)
{
    auto value = static_cast<char32_t>(cp);
    auto candidate = std::ranges::lower_bound(ranges, value, {}, &std::pair<char32_t, char32_t>::second);
    return candidate != ranges.end() && candidate->first <= value;
}

} // namespace

bool IsXIDStart(CodePoint cp)
{
    return IsInRange(cp, XIDStartRanges);
}

bool IsXIDContinue(CodePoint cp)
{
    return IsInRange(cp, XIDContinueRanges);
}

} // namespace unicode
