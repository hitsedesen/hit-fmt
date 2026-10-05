META(Key, const Item *, const Value *)
const Item *Factory::build(int n) const {
  return nullptr;
}
META(Key, const Item *, const Value *)
[[maybe_unused]] static bool check(const Item *p) {
  return p;
}
BIND(Key, const Item *) TRACK(Group, const Item *) MAP(Key, const Item *, const Value *)
const Item *Factory::sequence(int n) const {
  return nullptr;
}
NOTE(0) NOTE2(1) Vec annotated(int n) { return n; }
using Type = int;
static inline Vec NOTE("legacy") load(int Type) { Type * value; }
Vec NOTE(0) declaration(int n);
RAW(int n) & noexcept { return n; }
RAW(int n) const [[maybe_unused]];
struct C {
  C(int n) noexcept {
  }
  ~C() noexcept {
  }
  operator int() const {
    return 0;
  }
  virtual int f(int n) const volatile & noexcept (true) __attribute__((unused)) override {
    return n;
  }
  int g(int n) [[maybe_unused]];
};
int(grouped)(int n) {
  return n;
}
int (*pointer(int n))(int);
__attribute__((unused)) static int extension(int n) {
  return n;
}
__attribute__((destructor(101))) static void Cleanup() {
}
static __attribute__((unused)) int tagged;
__attribute__((used)) __attribute__((noinline)) int consecutive() {
  return 0;
}
__attribute__ /* annotation */ ((format(printf, 1, 2))) int log(const char *format, ...);
__declspec(dllexport) int exported();
void suffix() __attribute__((cold));
Vec NOTE(0) load(int n) noexcept(true) try { return n; } /* first */ catch(int Type) { Type * value; } /* between */ catch(...) { return 0; }
RAW(int n) & noexcept try { return n; } catch(...) { return 0; }
int genuine(int n) try {
  return n;
} catch (int code) {
  return code;
} catch (...) {
  return 0;
}
int tail;
