void tight(int value);
void wide (int first , int second);
void tabbed(int value);
void empty();
void emptyWide(  );
void openLine(
    int value);
void closeLine(int value
    );
void leading(/* keep */ int value);
void trailing(int value /* keep */);
template<class T> void generic(T value);
struct Widget {
    Widget(int value);
    ~Widget( );
    int operator()(int value) const;
    int method(int value) { return value; }
};
void (*callback)(int value);
template<class T> concept HasValue = requires (const T& value) {
    value();
};
void use(int value) {
    tight( value );
    auto lambda = []( int n ) { return n; };
    if (value) { tight(value); }
}
