#include "a.hpp"
#include "foreign/a.hpp"
#include "foreign/b.hpp"
#include "other/widget.hpp"
#include "project/a.hpp"
#include "project/core/a.hpp"
#include "project/core/z.hpp"
#include "project/engine/detail/a.hpp"
#include "project/engine/detail/z.hpp"
#include "project/engine/z.hpp"
#include "widget.txt"
#include <vector>

#include "project/engine/widget.h"
#include "widget.hpp"
#include <algorithm>
#include <system/a.hpp>
#include <system/z.hpp>

#if FEATURE
#include "widget.hpp"
#include "project/core/a.hpp"
#include "project/core/z.hpp"
#else
#include "other/a.hpp"
#include "other/b.hpp"
#endif

// Attached comments keep their block unchanged.
#include "project/core/z.hpp"
#include "project/core/a.hpp"

#include HEADER
#include "project/engine/a.hpp"
#include "project/engine/z.hpp"

// hit-fmt off
#include "project/core/z.hpp"
#include "project/core/a.hpp"
// hit-fmt on

int value;
