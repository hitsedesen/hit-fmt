#if OUTER
int x=1; // trailing
#ifdef INNER
#define N(x) ((x) + 1)
#else
#define N(x) ((x) + 2)
#endif
#define VALUE 1 // inside directive
#endif
  // standalone
