template<class T>
concept Small = requires(T item) { item.read(); };
template<class T>
concept Readable = requires(
    T item
) {
    typename T::value_type;
    { item.read() } noexcept -> Result<T>;
    requires Ready<T>;
    requires requires { item.write(); };
};
template<class T>
concept Callable = requires(T item) {
    [] {
        return 1;
    }();
    { item.invoke() };
};
template<class T>
concept LongRequirement = requires(
    T item
) { item.update(first_argument, second_argument, third_argument, fourth_argument); };
template<class T>
requires Ready<T>
void Check(T item) requires requires { item.read(); } {
    use(item);
}
struct Record {
    int value;
};
auto action = [] {
    run();
};
