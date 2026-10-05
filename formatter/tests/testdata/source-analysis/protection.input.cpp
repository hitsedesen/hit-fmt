const char* text = "// hit-fmt off"; // hit-fmt off
  // hit-fmt off
KEEP( a, nested( b ) );
/* hit-fmt off */
int nested;
// hit-fmt on
int frozen;
// hit-fmt on
int editable = 1; // note
int next = 2;
#define ADD(x) x + \
  1
KEEP( x, ( y ) );
LOUD( z );
bool ready = left and not right;
import <pkg/sub.h>;
// line token
int last;
#define EOF_VALUE 1