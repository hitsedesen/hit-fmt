class Widget {
public:
  int a;
private:
  int b;
    // Keep this comment at its source indentation.
  int c;
#ifdef OPTIONAL_MEMBER
  int d;
#endif
  struct Nested {
  public:
    int value;
  };
};
struct Item {
private:
  int count;
};
union Value {
  int number;
  float fraction;
};
enum class Shade : unsigned {
  Light,
  Dark = 2,
};
struct Forward;
struct Inline { int member; };
