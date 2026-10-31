template<Ready T> requires Accepts<const T&> void Leading(T value);
template<scope::Ready<int> T> void Trailing(T value) requires Accepts<T const&>;
template<::scope::Ready... Ts> requires Accepts<volatile Ts*...> void Pack(Ts... values);
template<Ready T> concept Checked = requires {
    requires Accepts<const volatile T&>;
};
auto lambda = []<Ready T>(T value) requires Accepts<const T&> { return value; };
template<Ready auto N> requires Accepts<N * factor> void NonType();
template<Ready T> requires Accepts<decltype(new   T)> void Operand(T value);
template<Ready T> requires Accepts<T, unsigned long> void Multiword(T value);
template<Ready T> requires Accepts<const Box<T>> void Nested(T value);
template<int N> requires (left < N > right) void Comparison();
template<auto N> requires Accepts<int(N)> void Construction();
