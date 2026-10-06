#define DECLARE(A, B) void A##B
DECLARE(One, Two)()
{
}
Declare(Three ,  Four)(int first, int second)
{
}
MAKE_NAME(Choose(Left, Right), Other)()
{
}
DECLARE(First,
    Second)()
{
}
void PREFIX(Scope, Name)(int first,  int second)
{
}
void ordinary(int first, int second)
{
    consume(first, second);
}
void (*callback)(int first, int second);
