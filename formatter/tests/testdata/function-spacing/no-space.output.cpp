int tight(int);
int spaced(int);
void tabbed(int);
void empty();
int commented /* keep */ (int);
int split
(int);
template <class T> T generic(T value);
struct Widget {
    Widget(int);
    ~Widget();
    int operator()(int) const;
    int method(int) { return 0; }
};
int ns::qualified(int);
int (*callback) (int);
int (parenthesized) (int);
#define DECLARE(name) int name (int)
template<class T> concept Check = requires (const T& val) {
    { [] (auto&&... args) {} (val) };
};
template<class TValue, class... Args>
concept Callable = requires (TValue& val) {
    [] (Args...) {} (val);
} || requires (const TValue& val) {
    val();
};
template<class T> concept TightCheck = requires(const T& val) {
    val.value();
};
template<class T> requires (sizeof(T) > 0)
void constrained(T);
void use() {
    int value = tight  (1);
    auto lambda = [] (int n) { return n; };
    if (value) {}
    DECLARE(Macro);
}
