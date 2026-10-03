void sample(int foo, int bar) {
    if (foo) {
        // do something
    }
    if (foo &&
        bar)
    {
        // do something else
    }
    if (foo) {
    } else {
    }
    if (foo &&
        bar)
    {} else {
    }
    for (int i = 0; i < foo; ++i) {
    }
    while (foo) {}
    do {
    } while (bar);
    switch (foo) {
    default: break;
    }
    try {} catch (int error) {
    }
    if (bar) bar++;
    if (foo) // keep
    {
    }
    auto lambda = [] {
    };
    int values[] = {1, 2};
    { bar++; }
}
