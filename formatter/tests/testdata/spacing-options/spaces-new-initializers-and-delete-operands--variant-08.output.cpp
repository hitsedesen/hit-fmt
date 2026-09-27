struct T {};
void f(){auto a=new T{1}; auto b=new T(1); auto c=new T(); delete[]p; auto d=new(buffer)T(2); auto e=new(buffer)T{3}; auto empty=new T{};}
