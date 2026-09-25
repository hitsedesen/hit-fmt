struct Inline
  : Alpha,
  Beta {};
class Previous
  : public Base {};
class Leading
  : public Base {};
class Long
    final
  : public Alpha,
  private Beta {};
class Commented /* keep */
    : public Base {};
struct Outer {
  struct Nested
    : public Alpha,
    Beta {};
};
enum class Flag : unsigned { On };
struct Incomplete : public {};
union NotDerived : Base {};
