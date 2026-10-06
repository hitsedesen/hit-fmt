template<class T>
concept Broken = requires(T value) {
    typename;
    typename T::;
    typename int;
    typename T<int;
    requires;
    { };
    { value } noexcept(true);
    { value } ->;
    { value } -> int;
    { value } -> Names::;
    { value } -> Concept<T> extra;
    value extra;
    value +;
    value.touch()
};
int outside;

template<class T>
concept Defaults = requires(T value = {}) {
    value;
};
template<class T>
concept Varargs = requires(T value, ...) {
    value;
};
template<class T>
concept Missing = requires(T value);
template<class T>
concept Empty = requires {};
template<class T>
concept EmptyItem = requires { ;
};
int final_outside;
