void f(bool ready) {
  #if FEATURE
wrong();
#else
   other();
#endif
  if (ready) {
    notIndented();
  }
  auto callback = []() {
    callbackBody();
  };
  // a comment remains source-owned
}
