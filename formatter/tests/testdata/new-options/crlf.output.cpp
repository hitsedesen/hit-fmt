#if OUTER
  #if INNER
    #define MULTI(x) do { \
    short_call(x);        \
    much_longer_call(x);  \
} while (false)
  #endif
#endif
// heading

////////////////

int item;
/// documentation
