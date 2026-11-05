template <class T>
concept PairLike = requires(T value) { value.first(); value.second(); };
template <class T>
concept Forms = requires(T v) { typename T::type; { v.get() } noexcept -> C<T>; requires C<T>; v.put(); };
template <class T>
concept Nested = requires(T v) { requires requires { v.a(); v.b(); }; v.c(); };
template <class T>
concept Expressions = requires(T v) {
    [] {
        a();
        b();
    }();
    v.x(1, 2);
};
template <class T>
concept Commented = requires(T v) { v.a(); /* next */ v.b(); };
void Statements() {
    first();
    second();
    for (int i = 0; i < 2; ++i) {
        third();
        fourth();
    }
}
