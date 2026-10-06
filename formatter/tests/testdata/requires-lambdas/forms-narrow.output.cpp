template<class T>
concept Requirements = requires(T value) {
    value.touch();
    value[0] < value[1];
    typename T;
    typename ::Names::Box<
        T>::template Rebind<Box<Box<T>>>::type;
    typename Holder<decltype([]<class U>(
        U item
    ) { return item; }(value))>::type;
    { value.touch() };
    { value.touch() } noexcept;
    { value.touch() } -> Convertible;
    { value.touch() } noexcept
        -> ::Names::Accepts<Box<Box<T>>, (1 < 2)>;
    requires Enabled<T> && (sizeof(T) > 0);
    requires requires(T other) {
        typename T::value_type;
        { other.touch() } -> Names::Result<T>;
    };
};
int outside;

using Local = int;
template<class T>
concept Scoped = requires(int Local) {
    (Local) * sizeof(T);
    (Local) & 1;
};
Local outside_type;

template<class... Types>
concept Pack = requires(Types... values) {
    sizeof...(values);
};
