#include "version.hpp"

#include <build_version.hpp>

namespace cli {

const char* BuildVersion()
{
    return HIT_FMT_VERSION;
}

} // namespace cli
