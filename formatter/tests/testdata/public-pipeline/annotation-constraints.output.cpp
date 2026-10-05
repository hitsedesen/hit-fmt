struct C {
  NOTE(0) C() requires (true) || true try : member{} {} catch(...) {}
  int member;
};
Vec NOTE(1) load() requires lhs < probe(rhs) {}
int tail() {
  return 1;
}
bool later = a > b;
