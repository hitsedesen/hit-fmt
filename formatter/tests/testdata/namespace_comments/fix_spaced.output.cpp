namespace alpha {
namespace beta {
int value;
}  // namespace beta
}  // namespace alpha

namespace {
int anonymous;
}  // namespace
namespace empty {}  // namespace empty
namespace missing {}  // namespace missing
namespace odd { }  // namespace odd
namespace done {
}  // namespace done
namespace scoped::inner {
}  // namespace scoped::inner
namespace inlineSpace { } int after = 1;
namespace withBlock {
} /* preserve this block */
namespace alias = alpha;
int function() { return 1; } // not a namespace

namespace Forward::Detail {} // declaration so the using-directive below is valid
using namespace Forward::Detail;
namespace Body {
int value;
} // namespace holds the shared state
namespace Suffix {}; // keep the declaration explanation
namespace Label {}  // namespace Label
namespace LabelWithNote {} // namespace LabelWithNote - used by adapters
namespace Prefix {} // namespaces are declared before use
namespace Unicode {}  // namespace Unicode
namespace {}  // namespace
