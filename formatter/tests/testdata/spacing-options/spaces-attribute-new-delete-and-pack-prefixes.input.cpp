struct S { int[[maybe_unused]] member; };
template<class... Ts> int count(){return sizeof...(Ts);}
void f(){int* p=new int(1); delete[] p; auto q=new (buffer) int(2); auto r=new int{3};}
