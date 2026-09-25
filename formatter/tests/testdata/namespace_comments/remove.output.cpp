namespace alpha {
namespace beta {
int value;
}
}

namespace {
int anonymous;
}
namespace empty {}
namespace missing {}
namespace odd { }
namespace done {
}
namespace scoped::inner {
}
namespace inlineSpace { } int after = 1;
namespace withBlock {
} /* preserve this block */
namespace alias = alpha;
int function() { return 1; } // not a namespace
