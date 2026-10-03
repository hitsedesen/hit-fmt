class S {
 public:
  int x;
  int f() {
    if (x) {
      return x;
    }
    return 0;
  }
};
class LIB_EXPORT Widget {
 public:
  int x{1};
};
int f() {
  return {1};
}
