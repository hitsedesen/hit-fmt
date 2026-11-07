#define TWICE(x) x + \
    x
// hit-fmt off
void frozen(){if (ready) work(a,b);}
// hit-fmt on
// clang-format off
int clang_value=1;
// clang-format on
void format(){
    KEEP( a, nested( b ) );
    unknown * value;
    if (ready) work(a,b);
    if (blocked) label: work(a,b);
    invoke(a,,b);
    @;
}
import <pkg/sub.h>;
const char* text="é"; @;
