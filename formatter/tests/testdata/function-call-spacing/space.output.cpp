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
    (__m128i *) (RotateMask + n);
    _mm_shuffle_epi8 (v, _mm_loadu_si128 ((__m128i *) (RotateMask + n)));
    (__m128i const * const) (RotateMask + n);
    (T&) (value);
    (T) (value);
    (callback)  (n);
    (&callback) (n);
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
