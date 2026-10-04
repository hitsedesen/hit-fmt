void f(bool a, bool b) {
    if (a) if (b) Go(); else Stop();
    for (;;) Go();
    while (a) Go();
    while (a);
    do Go(); while (a);
}
