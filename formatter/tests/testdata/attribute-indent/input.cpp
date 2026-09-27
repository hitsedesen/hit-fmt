void f(bool ready) {
  switch (1) {
    case 1:
      if (!ready)
        return;
         [[fallthrough]];
    default:
      break;
  }
}
