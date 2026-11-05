void f(bool alpha, bool beta, bool gamma) {
    if (alpha && beta &&
        gamma)
    {
        run();
    } else {
        stop();
    }
    while (alpha && beta &&
        gamma)
    {
        run();
    }
    if (alpha && beta && done) {
        run();
    } else if (alpha &&
        active)
    {
        stop();
    }
    if (alpha && beta &&
        active)
    {
        run();
    }
    if (check(alpha, beta,
        ok))
    {
        run();
    }
}
