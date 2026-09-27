void foo(int used, int unused);
void foo(int used, int /*unused*/)
{
    print(used);
}
void missing(int, int*);
void missing(int /*commented*/);
void defined(int, int /*unused*/) {}
void pointer(void (*)(int inner));
void okay(void (*callback)(int));
void uncertain(T);
void empty(void);
void variadic(int named, ...);
void defined_bad(int /*note here*/) {}
void defined_named(int /*note*/ value) {}
void multiline(
    int named,
    int)
{
}
void arrays(int [3], int = 1);
