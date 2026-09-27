[[nodiscard]]int f();
struct alignas(16) S { [[maybe_unused]]int value; };
alignas(16) int value;
void g(){alignas(8) int local; auto p=new(buffer) S; auto a=static_cast<int>(value); auto b=const_cast<int*>(ptr); auto c=reinterpret_cast<int*>(ptr); auto d=dynamic_cast<S*>(base);}
