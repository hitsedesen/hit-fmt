void f(bool a) {
    if (a) /* keep */ Go();
}

void f(int x)
{
    switch (x) {
        Go();
    }
}

void f(bool a)
{
    if (a) {
        Go();
    }
    Other();
}

void g(int x) {
    switch (x) case 0:
    Go();
}
