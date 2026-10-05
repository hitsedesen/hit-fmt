struct Registry {
};
struct Item {
};
const Registry entry(7, [] {
    return 2;
});
Registry wrapped(wrap([] {
    return 3;
}));
APPLY(7, ::ops::combine(::ops::make()), [](const auto& value) -> std::string {
    return value;
});
APPLY([] {
    return 1;
}, 7);
RENAMED(7, [seed = 2](int value) mutable noexcept (true) -> int {
    return value + seed;
}, 8);
APPLY([]() noexcept (noexcept(Item{})) -> decltype(Item{}) {
    return Item{};
});
void local() {
    Registry entry(7, [] {
        return 2;
    });
    install([](int value) -> int {
        return value;
    });
}
template<class, auto, class> struct Box {
};
template<class, auto, class> int make();
void declared(Box<int, []() -> int { return 1; }, int> value);
void declared(Box<int, int, []() -> int { return 1; }> value);
void declared(int value = make<int, []() -> int { return 1; }, int>());
void declared(int value = make<int, int, []() -> int { return 1; }>());
void declared(Foreign<int, []() -> int { return 1; }, int> value);
void declared(bool value = 1 < 2, int other = [] { return 1; }());
void declared(void (*callback)(int));
void declared([[maybe_unused]] int value);
void declared([ /* attribute */ [maybe_unused]] int value);
auto [left, right] = pair;
int following;
