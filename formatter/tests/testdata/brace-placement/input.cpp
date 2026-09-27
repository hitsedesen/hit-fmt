namespace alpha { // namespace inline comment
class Forward;
class C
{
    int value;
};
class InlineClass { // class comment
    int value;
};
struct S { int value; };
struct InlineComment { // opening comment
    int value;
};
struct CommentedStruct // struct comment
{ int value; };
struct WithConstructor {
    WithConstructor() : value{1}
    {
    }
    int value;
};
union U
{
    int value;
};
union InlineUnion { // union comment
    int value;
};
union CommentedUnion // union comment
{ int value; };
class Commented // keep this comment
{
};

int attached(int x) { return x; }
int preceded() /* header */ { return 2; }
int trailing() { // function comment
    return 3;
}
int blockTrailing() { /* function block comment */
    return 4;
}
int detached(int x)
{
    return x;
}
int annotated() // header comment
{ return 1; }
int noted() /* keep this comment */
{
    return 0;
}
auto action = [value = 1]{ return value; };
auto withParams = [](int n){ return n; };
auto notedLambda = [] // lambda comment
{ return 4; };
auto inlineLambda = [] { // lambda inline comment
    return 5;
};
auto first = [] { return 1; };
auto second = [](int x)
{
    return x;
};

int wrapped(
    int a,
    int b) { return a + b; }
int control(int x) {
    if (x) { return x; }
    int values[] = {1, 2};
    return values[0];
}
} // alpha
namespace beta // namespace comment
{
struct Nested
{
    int method() { return 3; }
};
}
