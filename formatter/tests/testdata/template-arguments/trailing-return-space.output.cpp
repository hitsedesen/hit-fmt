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
