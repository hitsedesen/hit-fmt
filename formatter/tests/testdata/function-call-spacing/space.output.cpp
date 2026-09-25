void target(int);
void use(int n) {
    target (n);
    target (n);
    target ();
    obj.method (n);
    ptr->method (n);
    ns::target (n);
    factory () (n);
    (*callback) (n);
    [] (int x) { return x; } (n);
    target (target (n));
    functor.template invoke<int> (n);
    target /* keep */ (n);
    target
    (n);
    if (n) { target (n); }
    int array[2]; auto x = array  [n];
    int constructed = int (n);
    // target (n);
#define TARGET(x) x
    TARGET (n);
}
template<class T> concept C = requires (T value) { value(); };
