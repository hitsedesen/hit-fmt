void Outer() { Clear(); Notify(); }
auto multi = [] { Clear(); Notify(); };
auto empty = [] {};
auto next = [](int x) {
    return x + 1;
};
