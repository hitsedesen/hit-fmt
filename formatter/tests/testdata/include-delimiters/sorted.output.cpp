#include "project/engine/widget.h"

#include "project/engine/alpha.h"
#include "bare.h"
#include "./local.h"
#include "../engine/normalized.h"

#include "./detail/header.h"

#include "../sibling/header.h"

#include "../../shared/header.h"

#include <stdio.h>

#include <sys/types.h>

#include <zeta/header.h>

#include <vector>

// An attached comment prevents sorting, but permits delimiter changes.
#include <external/commented.h> // Keep this comment.
#include <external/block.h> /* Keep > and " here. */
#include "../sibling/commented.h" // Keep this relative path.

#if FEATURE
    %:include "./digraph.h"

    #include "../sibling/conditional.h"

    #include <external/conditional.h>
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