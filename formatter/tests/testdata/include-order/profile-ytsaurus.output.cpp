#include <project/engine/widget.h>
#include <project/engine/z.hpp>

#include <project/a.hpp>

#include <project/engine/detail/a.hpp>
#include <project/engine/detail/z.hpp>

#include <a.hpp>
#include <widget.hpp>

#include <project/core/a.hpp>
#include <project/core/z.hpp>

#include <foreign/a.hpp>
#include <foreign/b.hpp>

#include <other/widget.hpp>

#include <system/a.hpp>
#include <system/z.hpp>

#include <algorithm>
#include <vector>
#include <widget.txt>

#if FEATURE
#include <widget.hpp>

#include <project/core/a.hpp>
#include <project/core/z.hpp>
#else
#include <other/a.hpp>
#include <other/b.hpp>
#endif

// Attached comments keep their block unchanged.
#include <project/core/z.hpp>
#include <project/core/a.hpp>

#include HEADER
#include <project/engine/a.hpp>
#include <project/engine/z.hpp>

// hit-fmt off
#include "project/core/z.hpp"
#include "project/core/a.hpp"
// hit-fmt on

int value;
