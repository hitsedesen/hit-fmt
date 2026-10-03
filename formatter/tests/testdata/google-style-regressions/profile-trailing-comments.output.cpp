int value = 1;  // note
int label = 2;  // namespace is ordinary prose
int item; /* block */
// own-line note
namespace Existing {
int inside = 3;  // detail
}    // namespace Existing
namespace Generated {
int first;  // keep this namespace multiline
int second;
}  // namespace Generated
namespace Short { int first; int second; }
