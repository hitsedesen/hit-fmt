#include <string>
#include <vector>

namespace NContainers {

    int Count() {
        auto strings = std::vector<std::string>(3, "x");
        auto numbers = std::vector<int>(3, 1);
        auto nested = std::vector<std::vector<int>>(2, numbers);
        return strings.size() + nested.size();
    }

} // namespace NContainers

namespace NComparisons {
    bool Compare() {
        return lower < current > (limit);
    }
} // namespace NComparisons

int Following;

namespace Incomplete {
    int MissingClose;
