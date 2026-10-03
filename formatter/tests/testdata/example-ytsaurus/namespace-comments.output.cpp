namespace NStore {
int item;
} // namespace NStore
namespace NMissing {
void Empty()
{ }
} // namespace NMissing
void Run()
{
    int item = 1;   // About namespace ownership, not a closing name.
}
