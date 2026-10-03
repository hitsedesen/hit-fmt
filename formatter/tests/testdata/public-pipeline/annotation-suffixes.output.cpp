struct C {
  NOTE(0) C(int n) noexcept : member{n} { use(n); }
  NOTE(1) C(int n, int tag) noexcept [[maybe_unused]] try : member{n}, other(tag) { use(n); } catch(...) { recover(); }
  NOTE(2) C(int n, int tag, int key) requires Ready<int> && requires(int x) { x; } : member{n} { use(key); }
  int member;
  int other;
};
int tail() {
  return 1;
}
