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
class TaggedBuffer : public StorageBase {
public:
  template <size_t Capacity>
  TaggedBuffer(const char (&Bytes)[Capacity])
#if defined(__clang__) && __has_attribute(enable_if)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu"
      __attribute((enable_if(Capacity > 0, "nonempty buffer")))
#pragma clang diagnostic pop
#endif
      : StorageBase(Bytes, Capacity) {
  }
};
