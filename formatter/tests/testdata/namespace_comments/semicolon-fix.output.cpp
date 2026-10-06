namespace alpha {
int value;
} // namespace alpha

namespace scoped::inner {
int value;
} // namespace scoped::inner

namespace {
int value;
} // namespace

namespace outer {
namespace nested {
int value;
} // namespace nested
} // namespace outer

namespace direct {
int value;
} // namespace direct

namespace missing {
int value;
} // namespace missing

namespace block {
int value;
} /* keep this block comment */

namespace spaced {
int value;
} // namespace spaced

int after;

namespace standalone {
int value;
}

; // keep this standalone semicolon

namespace alias = direct; // not a namespace body

namespace separate {
int value;
} // namespace separate

// keep this standalone comment

struct Record {
    int value;
}; // not a namespace comment

namespace eof {
int value;
} // namespace eof