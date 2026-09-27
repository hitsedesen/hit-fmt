void f(bool ready) {
  switch (0) {
case 0:
    work();
    break;
  case 1: {
    if (ready)
      work();
    break;
  }
  default:
    work();
  }
}
