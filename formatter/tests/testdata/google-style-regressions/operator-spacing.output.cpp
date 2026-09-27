template <typename T = int>
void TestNotEmpty() {
  static_assert(sizeof(T) == 0, "Empty arguments are not allowed.");
  int v = (int)value;
  if (alignof(T) == 1) return;
}
