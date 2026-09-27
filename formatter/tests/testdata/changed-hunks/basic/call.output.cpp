#include "z.h"
#include "a.h"

int f( int a, int b );
int g( int a, int b );

void x() {
  f(first,
      second,
      third);
  g(first, second, third);
}
