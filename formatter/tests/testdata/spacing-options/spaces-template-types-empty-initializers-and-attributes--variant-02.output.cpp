template<class T=int> struct Box { T value; };
struct S { S operator+()const; int operator[](int)const; operator int()const; };
[[maybe_unused]] Box<int> item{}; Box<> empty{};
