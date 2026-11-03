struct Iterator {
int operator*() const;
int Get() const { auto value = operator*(); return value; }
};
void use(Iterator it, Iterator* ptr) {
auto a = it.operator*();
auto b = ptr->operator*();
auto c = operator+(1);
auto d = operator()();
auto e = operator[](0);
auto f = a * b + c;
}
