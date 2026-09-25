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
void use() {
    int value = tight  (1);
    auto lambda = [] (int n) { return n; };
    if (value) {}
    DECLARE(Macro);
}
