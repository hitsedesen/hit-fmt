namespace N
{ // no gap
class C
{ int x; };
struct S { int x; };
union U { int x; };
enum E { A, B };
enum class Scoped { Value };
class Later
{
};
struct Forward;
int f() { return 1; }
int g() { return 2; }
int h() {
    return 3;
}
int commented() /* keep */ { return 4; }
int trailing() // keep this comment
{ return 5; }
auto lambda = [](int n) { return n; };
auto other = [] { return 1; };
auto detached = [] { return 2; };
int untouched() { if (true){ int values[]{1}; return values[0]; } return 0; }
}
namespace Other
{ }
