decltype(0) extract(const int& value) { return value; }
static decltype(auto) forward(int&& value) { return value; }
decltype(*source) dereference(const int* value);
decltype((source)) nested(const int& value);
decltype(combine((source), 0)) expression(const int& value);
decltype(factory<int>(0)) templated(const int& value);
decltype([](int item) { return item; }(0)) lambda(const int& value);
decltype /* operand */ (0) commented(const int& value);
decltype(0) scalar(int value);
decltype(0) stored = 0;
decltype(*source) (*callback)(const int& value);
decltype(0) (*select(const int& value))(int);
int (*ordinary)(const int& value);
auto trailing(const int& value) -> decltype((value));
struct Callable { int operator()(const int& value); };
alignas(16) int aligned;
int plain(const int& value);

template<class Store>
    requires requires(Store store) {
    store.Count();
    { store.Read() } noexcept;
    typename Store::value_type;
    requires requires { typename Store::key_type; };
    }
struct CheckedStore {};

template<class Store>
concept Readable = requires(Store store) { store.Count(); store.Read(); };
