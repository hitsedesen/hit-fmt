#include "zeta.hpp"
#include "alpha.hpp"

namespace sample {

enum class Mode
{
    Quiet,
    Loud
};

void Run(int item)
{
    auto total = Collect(item, 2); 	// retain the ordinary comment gap
    auto result =
        BuildSessionForRequest()
            .SelectUnusuallyLongProcessingModeForCurrentRequest()
            .FinalizeUnusuallyLongOutputForCurrentRequest();
    if (item) {
        Collect(item, 3);
    }
    auto callback = [] { return 1; };
}

} // namespace sample
