template <typename Class, typename PropertyType, typename Matcher>
inline PolymorphicMatcher<internal::PropertyMatcher<
    Class, PropertyType, PropertyType (Class::*)() const>>
Property(PropertyType (Class::*property)() const,
         const Matcher& matcher) { return {}; }
void f() {
  EXPECT_EQ(kOther, GMOCK_KIND_OF_(char**));
  EXPECT_DEATH_IF_SUPPORTED({ callback(); }, "");
  EXPECT_NONFATAL_FAILURE(
      {  // NOLINT
        callback();
      },
      "");
  int product = a * *p;
  T value {1, 2};
}
