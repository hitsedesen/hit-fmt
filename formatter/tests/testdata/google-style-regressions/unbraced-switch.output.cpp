void f() {
  switch (0)
  case 0:
    GTEST_CHECK_(true) << "Check failed in switch case";
}
void g() {
  switch (1) {
    case 1:
      break;
  }
}
