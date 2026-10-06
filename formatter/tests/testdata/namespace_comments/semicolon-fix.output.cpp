namespace alpha {
int value;
}

; // namespace alpha

namespace scoped::inner {
int value;
}

; // namespace scoped::inner

namespace {
int value;
}

; // anonymous namespace

namespace outer {
namespace nested {
int value;
}

; // nested closing comment
}

; // outer closing comment

namespace direct {
int value;
} // namespace direct

namespace missing {
int value;
} // namespace missing

namespace block {
int value;
}

; /* keep this block comment */

namespace separate {
int value;
} // namespace separate

// keep this standalone comment

struct Record {
    int value;
}; // not a namespace comment
