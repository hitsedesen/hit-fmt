namespace Outer {
    namespace Inner {
        // hit-fmt off
  int  frozen;
// hit-fmt on
        void Work() {
            Run();
        }
    }
    struct Record {
        // hit-fmt off
  int  member;
// hit-fmt on
        void Work() {
            Run();
        }
    };
}
int after;

// hit-fmt off
    namespace Frozen
    {
  int  value;
    }
// hit-fmt on

namespace Conditional {
#if ENABLED
    int value;
#endif
}
