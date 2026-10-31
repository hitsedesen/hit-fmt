#if OUTER
  #ifdef INNER
    %:include "detail.h"
    #define VALUE first \
    second
  #elifdef OTHER
  #else
  #endif
  %:ifndef ANOTHER
    %:define FLAG 1
  %:elifndef FALLBACK
  %:else
  %:endif
#endif
#pragma once
// hit-fmt off
  # if UNTOUCHED
	 # define KEEP 1
  # endif
// hit-fmt on
