template<int... Args> void f() {
    static_assert(TDependentFalse<Args...>, "Wrong number of arguments");
}
