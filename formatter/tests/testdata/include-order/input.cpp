// License header stays at the top.
#ifndef EXAMPLE_H
#define EXAMPLE_H

#include <vector>
#include "zeta.h"
#include <algorithm>
#include "alpha.h"
#include "alpha.h"

#include "second/zeta.h"
#include "second/alpha.h"
// Keep the comment between groups.
#include "commented/zeta.h"
#include "commented/alpha.h"

#if defined(FEATURE)
#include <zlib.h>
#include "feature/b.h"
#include "feature/a.h"
#else
#include "fallback/b.h"
#include "fallback/a.h"
#endif

#include "with-comment-z.h" // tied to this header
#include "with-comment-a.h"
#include NEXT_HEADER
#include "after-macro-z.h"
#include "after-macro-a.h"

# include "spaced-z.h"
#include "spaced-a.h"

  #include "indented-z.h"
  #include "indented-a.h"
#include "boundary-z.h"
#include "boundary-a.h"
#include_next <alternate.h>
#include "tail-z.h"
#include "tail-a.h"
#include "continued.h" \
    /* not a standalone include */
#include "final-z.h"
#include "final-a.h"

// Must stay with protected-z.h.
#include "protected-z.h"
#include "protected-c.h"
#include "protected-a.h"

namespace example {
void use();
}

#endif
