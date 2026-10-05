namespace alpha {
namespace beta {
int value;
} // namespace beta
} // namespace alpha

namespace {
int anonymous;
} // namespace
namespace empty {} // namespace empty
namespace missing {} // namespace missing
namespace odd { } // namespace odd
namespace done {
} // namespace done
namespace scoped::inner {
} // namespace scoped::inner
namespace inlineSpace { } int after = 1;
namespace withBlock {
} /* preserve this block */
namespace alias = alpha;
int function() { return 1; } // not a namespace
