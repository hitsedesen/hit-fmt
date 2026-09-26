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
}
