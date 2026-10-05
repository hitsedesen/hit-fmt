// License header stays at the top.
#ifndef EXAMPLE_H
#define EXAMPLE_H

#include "alpha.h"
#include "alpha.h"
#include "zeta.h"
#include <algorithm>
#include <vector>

#include "second/alpha.h"
#include "second/zeta.h"
// Keep the comment between groups.
#include "commented/zeta.h"
#include "commented/alpha.h"

#if defined(FEATURE)
#include "feature/a.h"
#include "feature/b.h"
#include <zlib.h>
#else
#include "fallback/a.h"
#include "fallback/b.h"
#endif

#include "with-comment-z.h" // tied to this header
#include "with-comment-a.h"
#include NEXT_HEADER
#include "after-macro-a.h"
#include "after-macro-z.h"

#include "spaced-a.h"
# include "spaced-z.h"

#include "boundary-a.h"
#include "boundary-z.h"
  #include "indented-a.h"
  #include "indented-z.h"
#include_next <alternate.h>
#include "tail-a.h"
#include "tail-z.h"
#include "continued.h" \
    /* not a standalone include */
#include "final-a.h"
#include "final-z.h"

// Must stay with protected-z.h.
#include "protected-z.h"
#include "protected-c.h"
#include "protected-a.h"

namespace example {
void use();
}

#endif
