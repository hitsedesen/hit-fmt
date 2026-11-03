inline namespace café {
void First();
void Second();
} //namespace
// café
// explanation after the continuation

namespace outer::inner {
void First();
void Second();
} // namespace
// outer::other

namespace outer::inner {
void First();
void Second();
} // namespace
// outer::inner contains helpers

namespace outer::inner {
void First();
void Second();
} // namespace
// namespace outer::inner

namespace outer::inner {
void First();
void Second();
} // namespace

// outer::inner

namespace outer::inner {
void First();
void Second();
} // namespace
/* outer::inner */

namespace outer::inner {
void First();
void Second();
} // namespace outer::inner
// outer::inner

namespace outer::inner {
void First();
void Second();
} // explanatory comment
// outer::inner

namespace outer::inner {
void First();
void Second();
} // namespace
// hit-fmt off
// outer::inner
// hit-fmt on

namespace {
void First();
void Second();
} // namespace
// anonymous

namespace last {
void First();
void Second();
} // namespace
// last