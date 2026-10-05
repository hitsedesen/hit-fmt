std::vector<int> values;
void Use(const ::NOther::TValue* value);
void Inspect(volatile ::NOther::TValue& value);
const volatile ::NOther::TValue* Make();
void Dependent(typename ::NOther::TType* value);
typedef const ::NOther::TValue ConstValue;
template<class T> void Consume(
    const ::Wrapper<T>::Type& value);
Wrapper<Wrapper<int>>::Type* nested;
void Commented(
    const /* qualifier */ ::NOther::TValue* value);
::NOther::TValue global;
void f() {
  auto x = obj . member + ptr -> member;
  auto y = obj .* field + ptr ->* field;
  auto smaller = value > ::NOther::Limit;
  auto shifted = value >> ::NOther::Shift;
}
