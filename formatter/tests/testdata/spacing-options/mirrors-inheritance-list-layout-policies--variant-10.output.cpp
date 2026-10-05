struct Base { Base(int); };
struct C : Base {
C(int x ,int y):/* keep */ Base(x) ,value(y) {}
int value;
};
