void f() {
    while (
        alpha &&
        beta
    ) { run(); }
    for (
        int i = 0;
        i < limit; ++i
    ) { run(); }
    try { run(); } catch (
        const Error&
        error
    ) { recover(); }
    if (alpha /* keep */ &&
        beta) { run(); }
    run(alpha,
        beta);
}
