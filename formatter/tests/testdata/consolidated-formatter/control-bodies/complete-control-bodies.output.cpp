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

void comment_boundaries(bool ready, bool fallback)
{
    if (ready) {
        goto finish;
    }
    if (fallback) {
        goto finish;
    }

    // Start the next phase.
    Work();

    if (ready) {
        if (fallback) {
            Work();
        }
    }
    // Outside both controls.
    Work();

    if (ready)
        Work(); // attached
    Work();

    if (ready) /* body comment */ Work();

    if (ready)
        Work();
    // Explain the alternative.
    else
        Work();

    if (ready) {
        Work();
    } else {
        Work();
    }
    /* Start another phase. */
    Work();

    finish:
    Work();
}
