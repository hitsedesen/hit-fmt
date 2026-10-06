namespace alpha {
int value;
}; // namespace alpha

namespace scoped::inner {
int value;
};; // namespace scoped::inner

namespace {
int value;
}; // anonymous namespace

namespace outer {
namespace nested {
int value;
}; // nested closing comment
}; // outer closing comment

namespace direct {
int value;
} // wrong closing comment

namespace missing {
int value;
};

namespace block {
int value;
}; /* keep this block comment */

namespace separate {
int value;
};
// keep this standalone comment

struct Record {
int value;
}; // not a namespace comment
