template<class T>
concept Transformable = requires(T value) {
    []<
        class LeftArgumentWithContext,
        class RightArgumentWithContext,
        class ExtraArgumentWithContext>(
        LeftArgumentWithContext left,
        RightArgumentWithContext right,
        ExtraArgumentWithContext extra
    ) {
        return combine(
            left,
            right,
            extra,
            make_extra_argument(left));
    }(value, value, value);
    { []<class Argument, class ExtraArgument>(
        Argument argument,
        ExtraArgument extra
    ) { return combine(argument, extra); }(value, value) }
        noexcept -> ResultFor<T>;
    requires ([]<class Argument>(Argument argument) {
        return sizeof(argument) > 0 &&
            sizeof(argument) < 64;
    }(value));
    requires requires {
        []<class Argument>(Argument argument) {
            return argument.touch();
        }(value);
    };
    typename Wrapper<decltype([]<class Argument>(
        Argument argument
    ) { return argument; }(value))>::type;
    []<class A>(A a) { return a; }(value);
    [](int item) { return item + 1; }(0);
    value[0] < value[1];
};

template<class T>
requires requires(T value) {
    []<class FirstArgument, class SecondArgument>(
        FirstArgument first,
        SecondArgument second
    ) {
        return combine(
            first,
            second,
            make_extra_argument(first));
    }(value, value);
}
void check(T value) {
}

template<class T>
void trailing(T value) requires requires(T item) {
    { []<class Argument>(Argument argument) {
        return combine(
            argument,
            make_extra_argument(argument));
    }(item) };
} {
}
