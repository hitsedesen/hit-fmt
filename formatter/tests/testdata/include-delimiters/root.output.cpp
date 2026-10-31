#include <zeta/header.h>
#include "project/engine/widget.h"
#include "project/engine/alpha.h"
#include "./local.h"
#include "bare.h"
#include <stdio.h>
#include <vector>
#include <sys/types.h>
#include "../engine/normalized.h"
#include <../sibling/header.h>

// An attached comment prevents sorting, but permits delimiter changes.
#include <external/commented.h> // Keep this comment.
#include <external/block.h> /* Keep > and " here. */

#if FEATURE
#  include <external/conditional.h>
%:include "./digraph.h"
#endif

// clang-format off
#include "external/protected.h"
// clang-format on

#include HEADER_NAME
#include_next "external/next.h"
#include \
    "external/spliced.h"
#include "external/trailing.h" EXTRA
#include "external/contains>delimiter.h"
#include <external/last.h>