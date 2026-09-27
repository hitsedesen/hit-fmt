namespace alpha {
namespace beta {
int value;
} // namespace wrong
} // alpha

namespace {
int anonymous;
}
namespace empty {} // namespace empty
namespace missing {}
namespace odd { }  //namespace odd  
namespace done {
} // namespace done
namespace scoped::inner {
} // namespace scoped
namespace inlineSpace { } int after = 1;
namespace withBlock {
} /* preserve this block */
namespace alias = alpha;
int function() { return 1; } // not a namespace
