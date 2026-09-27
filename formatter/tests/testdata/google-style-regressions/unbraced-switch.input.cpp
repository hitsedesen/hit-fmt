void f() {
  switch (0)
  case 0:
    VERIFY_FLAG_(true) << "unexpected state";
}
void g() {
  switch (1) {
  case 1:
    break;
  }
}
