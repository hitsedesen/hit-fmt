int Next(int x) { return x + 1; }
void Reset() {
    Clear();
    Notify();
}
void ClearOnce() { Clear(); }
void Declare() { int value = 1; }
void Initialize() { int values[] = {1, 2}; }
void Assert() { static_assert(true); }
const char* Delimiter() { return ";"; }
void Attributed() { [[likely]] Clear(); }
void Long() {
    ReportAnUnusuallyLongMessage(
        "this statement does not fit on one line");
}
void Empty() {}
void CommentOnly() {
    /* explanation */
}
void LeadingComment() {
    /* explanation */
    Clear();
}
void TrailingComment() {
    Clear(); /* explanation */
}
void LineComment() {
    Clear(); // explanation
}
void Nested() {
    { Clear(); }
}
void Branch(bool ready) {
    if (ready) { Clear(); }
}
void Loop(bool ready) {
    while (ready) {
        Clear();
        Notify();
    }
}
void Unbraced(bool ready) {
    if (ready) Clear();
    else Notify();
}
void NestedControl(bool ready) {
    if (ready)
        while (ready) Clear();
}
void Labeled() {
    done:
    return;
}
void Raise() { throw 1; }
void Yield() { co_yield 1; }
void Coro() { co_return; }
void Condition() {
#if ENABLED
    Clear();
#endif
}
void Unknown() { OPAQUE_BODY() }
struct SingleMember {
    int value;
};
namespace SingleDeclaration {
int value;
}
auto next = [](int x) { return x + 1; };
auto reset = [] {
    Clear();
    Notify();
};
auto empty = [] {};
auto callback = [] {
    /* explanation */
    Clear();
};
auto factory = [] {
    return [] {
        Clear();
        Notify();
    };
};
