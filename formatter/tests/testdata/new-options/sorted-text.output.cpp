#if OUTER
    #if INNER
        #include "alpha.h"
        #include "zeta.h"
        #define MULTI(x) do { \
    short_call(x);            \
    much_longer_call(x);      \
} while (false)
        #define JOIN alpha\
beta
        #define STRING "inside \
continued"
        #define COMMENT value // note \
continued
    #elif SECOND
int second;
    #else
int other;
    #endif
#endif
#if DIGRAPH
    %:if INNER
        %:define VALUE first \
    second                   \
    third
    %:endif
#endif
// heading

////////////////

int item;
// ordinary comment

////////////////

// next heading
int following; //////
/// documentation is not a ruler
// hit-fmt off
//////////////////
# if OFF
#define UNTOUCHED(x) x \
    x   \
    x
# endif
// hit-fmt on
