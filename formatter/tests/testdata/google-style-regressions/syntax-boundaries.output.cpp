template <typename Owner, typename Result, typename Rule>
inline Adapter<detail::MethodRule<
    Owner, Result, Result (Owner::*)() const>>
select(Result (Owner::*member)() const,
       const Rule& rule) {
  return {};
}
void f() {
  ASSERT_SAME(kTag, MACRO_TYPE_(char**));
  VERIFY_FAILS({
      invoke();
    }, "");
  CHECK_BLOCK(
      {  // block argument
      invoke();
    },
      "");
  int product = a * *p;
  T value {1, 2};
}
