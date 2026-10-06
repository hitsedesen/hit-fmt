auto Declared() -> Pair<int, long>;
auto Compact() -> Pair<int, long>;
auto Nested() -> Box<Pair<int, long>>;
auto Qualified() -> types::Pair<int, types::Box<long>>*;
auto Defined() -> Pair<int, long> {
    return {};
}
auto Constrained() -> Pair<int, long> requires Ready<int>;
auto lambda = []() -> Pair<int, long> {
    return {};
};
void Use() {
    Invoke([]() -> Pair<int, long> {
        return {};
    }, value);
}
auto Dependent() -> decltype(Make<int, long>());
auto Compared() -> decltype(left < middle, middle > right);
auto Shifted() -> Fixed<(bits >> 1), 7>;
auto Pointer() -> int (*)(int, long);
auto Reference() -> int (&)(int, long);
auto Member() -> int (Worker::*)(int, long) const noexcept;
auto Callback() -> Wrapper<int (*)(int, long)>;
auto Variadic() -> void (*)(int, ...);
auto Named() -> int (*)(int first, long second);
auto IncludedTypes() -> Result (*)(First, Second);
auto ConstPointer() -> int (* const)(int, long);
auto ExpressionArgument() -> Fixed<int(left * right), 7>;
auto PointerDefined() -> int(*)(int, long) {
    return nullptr;
}
auto DependentPointer() -> decltype(Make<int, long>()) (*)(int, long);
