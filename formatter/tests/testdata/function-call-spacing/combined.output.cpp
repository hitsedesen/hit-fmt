void f(int);
void g(int n) {
    f (n);
    if (n) f (n);
    auto fn = [] (int x) { return x; };
}
