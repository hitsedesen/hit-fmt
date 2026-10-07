void f() {
   switch (1) {
    case 1:
       [[fallthrough]];
    case 2:
      {
         Use();
      }
    default:
       break;
   }
    [[maybe_unused]] int value = 0;
    [[maybe_unused]][[deprecated]] int other = 1;
}
