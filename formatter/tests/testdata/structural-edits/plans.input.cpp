#include <z.h>
#include "a.h"
;
namespace n { ; }
void f() {
    ;
    if (a) if (b) x(); else y();
    while (c) ;
    label: ;
    if (d) [[likely]] x();
    if (e) y(); // attached
    // clang-format off
    while (f) z();
    // clang-format on
    for (;;) x();
}
