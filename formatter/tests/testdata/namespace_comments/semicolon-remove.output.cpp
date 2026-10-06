namespace alpha {
int value;
}

namespace scoped::inner {
int value;
}

namespace {
int value;
}

namespace outer {
namespace nested {
int value;
}
}

namespace direct {
int value;
}

namespace missing {
int value;
}

namespace block {
int value;
} /* keep this block comment */

namespace spaced {
int value;
}

int after;

namespace standalone {
int value;
}

; // keep this standalone semicolon

namespace alias = direct; // not a namespace body

namespace separate {
int value;
}

// keep this standalone comment

struct Record {
    int value;
}; // not a namespace comment

namespace eof {
int value;
}