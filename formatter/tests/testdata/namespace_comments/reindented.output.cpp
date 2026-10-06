namespace Example {

/*
 * Documentation
 */
void Run();

} // namespace Example
/* Neighbor
   documentation */
namespace Outer {

namespace Inner {

/* Nested
 * documentation */
void Run();

} // namespace Inner
/* Following
 * documentation */

} // namespace Outer
namespace {

/* Anonymous
 * documentation */
int value;

} // namespace
namespace Protected {

// hit-fmt off
    /* Preserved
     * documentation */
// hit-fmt on
int value;

} // namespace Protected
