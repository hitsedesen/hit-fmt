void target(int);
void use(int n) {
    target(n);
    target(n);
    target(n);
    target();
    target(  );
    target( // keep
        n);
    target(/* keep */ n);
    target(n /* keep */);
    target(
        n);
    target(n
    );
    object.method(n, target(n));
    factory()(n);
    (*callback)(n);
    (callback)(  n  );
    (T) (  n  );
    (__m128i *) (  mask  );
    target((__m128i *) (  mask  ));
    int constructed = int ( n );
    if (n) { target(n); }
    auto lambda = []( int x ) { return x; };
#define CALL(x) x
    CALL(n);
    target(n) broken(  n  );
    switch (n) {
        UNROLLED(1)
        UNROLLED( 2)
        UNROLLED( 3);
    }
}
template<class T> concept C = requires (T value) { value(); };
