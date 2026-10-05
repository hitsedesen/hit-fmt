#include "sample.h"
#include "z.h"
#include <vector>
#include "a.h"

#include "deep/z.h"
#include "deep/a.h"

int Good( int value );

#if FLAG
  %:include "branch/z.h"
# include "branch/a.h"
#endif

// hit-fmt off
#include "locked/z.h"
#include "locked/a.h"
// hit-fmt on

int Tail;
