struct Base { Base(int); };
struct C : Base {
C(int x, int y) : Base(
    x), value(y) {}
int value;
};
