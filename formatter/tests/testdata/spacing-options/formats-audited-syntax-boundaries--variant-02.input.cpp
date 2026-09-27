struct S{int x; virtual void f()override;};
namespace N{int x;} enum E{A,B};
int f(){return(x);} void g(){throw(x);}
auto h(){co_return(x);}
auto l=[]()mutable{return 1;}; auto m=[]mutable{return 2;};
template<class T> concept C=requires(T t){t;};
auto a=static_cast<int>(x); auto b=const_cast<int*>(p);
struct Empty{}; namespace NE{} enum EE{};
template<class T> concept EC=requires{};
