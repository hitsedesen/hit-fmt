#include <cstddef>
namespace N {}
namespace Alias = N;
export ;
namespace Outer {
    namespace Inner {}
    int value;
    ; // Keep this placeholder.
}
extern "C" {
    void run();
}
export {
    int exported;
}
struct S {};
struct Methods { void run() {}; };
enum E { A };
void g() {}
void f() {
    work();
}
namespace WithDirective {
#if FLAG
    ;
#endif
    ;
}
namespace WithComment {
    ; /* Keep this placeholder. */
}
; // Keep this placeholder.
