APPLY(char**, [] {
    return 2;
});
APPLY(*, [] {
    return 2;
});
APPLY(value + *, [] {
    return 2;
});
APPLY("head" /* join */ "tail" /* trailing */, [] {
    return 2;
});
APPLY([]<class T, class U>(T left, U right) {
    return left;
}, 7);
APPLY(7, []<class T, class U>(T left, U right) {
    return left;
}, 8);
APPLY(7, []<class T, class U>(T left, U right) {
    return left;
});
void declared(int value = []<class T, class U>(T left, U right) {
    return left;
}(1, 2), int other = 0);
void declared(int value = wrap([]<class T, class U>(T left, U right) {
    return left;
}), int other = 0);
APPLY(7, ::ops::template make<int>(value), [] {
    return 2;
});
APPLY(7, object.template make<int>(value), [] {
    return 2;
});
void declared(int value = ::ops::template make<int>(other), int second = [] {
    return 1;
}());
int following;
