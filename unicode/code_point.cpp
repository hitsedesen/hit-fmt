#include "code_point.hpp"

#include <algorithm>
#include <vector>

namespace unicode {

extern std::vector<std::pair<char32_t, char32_t>> XIDStartRanges;
extern std::vector<std::pair<char32_t, char32_t>> XIDContinueRanges;

namespace {

bool IsInRange(
    CodePoint cp, const std::vector<std::pair<char32_t, char32_t>>& range)
{
    return std::ranges::binary_search(
        range,
        std::pair(static_cast<char32_t>(cp), static_cast<char32_t>(cp)),
        [](const auto& lhs, const auto& rhs) {
            if (lhs.first < rhs.first && lhs.second < rhs.second) {
                return true;
            }
            return false;
        });
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
